//derived from https://github.com/qemu/qemu/blob/master/contrib/ivshmem-client/ivshmem-client.c

#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#include "ivshmem-client.h"

/* read message from the unix socket */
static int ivshmem_client_read_one_msg(IvshmemClient *client, int64_t *index, int *fd){
    int ret;
    struct msghdr msg;
    struct iovec iov[1];
    union {
        struct cmsghdr cmsg;
        char control[CMSG_SPACE(sizeof(int))];
    } msg_control;
    struct cmsghdr *cmsg;

    iov[0].iov_base = index;
    iov[0].iov_len = sizeof(*index);

    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_control = &msg_control;
    msg.msg_controllen = sizeof(msg_control);

    ret = (int)recvmsg(client->sock_fd, &msg, 0);
    if (ret < sizeof(*index)) {
        fprintf(stderr, "cannot read message: %s\n", strerror(errno));
        return -1;
    }
    if (ret == 0) {
        fprintf(stderr, "lost connection to server\n");
        return -1;
    }

    *fd = -1;

    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {

        if (cmsg->cmsg_len != CMSG_LEN(sizeof(int)) || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
            continue;
        }

        memcpy(fd, CMSG_DATA(cmsg), sizeof(*fd));
    }

    return 0;
}

/* init a new ivshmem client */
int ivshmem_client_init(IvshmemClient *client, const char *unix_sock_path, IvshmemClientNotifCb notif_cb){
    int ret;

    memset(client, 0, sizeof(*client));

    ret = snprintf(client->unix_sock_path, sizeof(client->unix_sock_path), "%s", unix_sock_path);

    if (ret < 0 || ret >= sizeof(client->unix_sock_path)) {
        fprintf(stderr, "could not copy unix socket path\n");
        return -1;
    }

    client->vector_fd = -1;

    client->id = -1;

    client->notif_cb = notif_cb;
    client->shm_fd = -1;
    client->sock_fd = -1;

    return 0;
}

/* create and connect to the unix socket */
int ivshmem_client_connect(IvshmemClient *client){
    struct sockaddr_un sun;
    int fd, ret;
    int64_t tmp;

    fprintf(stderr, "connect to client %s\n", client->unix_sock_path);

    client->sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client->sock_fd < 0) {
        fprintf(stderr, "cannot create socket: %s\n", strerror(errno));
        return -1;
    }

    sun.sun_family = AF_UNIX;
    ret = snprintf(sun.sun_path, sizeof(sun.sun_path), "%s",  client->unix_sock_path);
    if (ret < 0 || ret >= sizeof(sun.sun_path)) {
        fprintf(stderr, "could not copy unix socket path\n");
        goto err_close;
    }

    if (connect(client->sock_fd, (struct sockaddr *)&sun, sizeof(sun)) < 0) {
        fprintf(stderr, "cannot connect to %s: %s\n", sun.sun_path, strerror(errno));
        goto err_close;
    }

    /* first, we expect a protocol version */
    if (ivshmem_client_read_one_msg(client, &tmp, &fd) < 0 || (tmp != IVSHMEM_PROTOCOL_VERSION) || fd != -1) {
        fprintf(stderr, "cannot read from server\n");
        goto err_close;
    }

    /* then, we expect our index + a fd == -1 */
    if (ivshmem_client_read_one_msg(client, &client->id, &fd) < 0 || client->id < 0 || fd != -1) {
        fprintf(stderr, "cannot read from server (2)\n");
        goto err_close;
    }
    fprintf(stderr, "our_id=%lu\n", client->id);

    /* now, we expect shared mem fd + a -1 index */
    if (ivshmem_client_read_one_msg(client, &tmp, &fd) < 0 || tmp != -1 || fd < 0) {
        if (fd >= 0) {
            close(fd);
        }
        fprintf(stderr, "cannot read from server (3)\n");
        goto err_close;
    }
    client->shm_fd = fd;
    fprintf(stderr, "shm_fd=%d\n", fd);

    return 0;

err_close:
    close(client->sock_fd);
    client->sock_fd = -1;
    return -1;
}

/* close connection to the server, and free all peer structures */
void ivshmem_client_close(IvshmemClient *client){
    fprintf(stderr, "close client\n");

    close(client->shm_fd);
    client->shm_fd = -1;
    close(client->sock_fd);
    client->sock_fd = -1;
    client->id = -1;
    close(client->vector_fd);
    client->vector_fd = -1;
}

/* get the fd_set according to the unix socket and peer list */
void ivshmem_client_get_fds(const IvshmemClient *client, fd_set *fds, int *maxfd){
    FD_SET(client->sock_fd, fds);
    if (client->sock_fd >= *maxfd) {
        *maxfd = client->sock_fd + 1;
    }

    if (client->vector_fd >= 0) {
        FD_SET(client->vector_fd, fds);
        if (client->vector_fd >= *maxfd) {
            *maxfd = client->vector_fd + 1;
        }
    }
}

/* handle events from eventfd */
static int ivshmem_client_handle_event(IvshmemClient *client, const fd_set *cur, int maxfd){
    uint64_t kick;
    int ret;

    if (client->vector_fd>=0 && client->vector_fd < maxfd && FD_ISSET(client->vector_fd, cur)) {
        ret = (int)read(client->vector_fd, &kick, sizeof(kick));
        if (ret < 0) {
            return ret;
        }
        if (ret != sizeof(kick)) {
            fprintf(stderr, "invalid read size = %d\n", ret);
            errno = EINVAL;
            return -1;
        }

        if (client->notif_cb != NULL) {
            client->notif_cb();
        }
    }

    return 0;
}

/* handle message coming from server (new peer, new vectors)  */
static int ivshmem_client_handle_server_msg(IvshmemClient *client){
    int64_t peer_id;
    int ret, fd;

    ret = ivshmem_client_read_one_msg(client, &peer_id, &fd);
    if (ret < 0) {
        return -1;
    }

    //we ignore new peers, we only want to get our vector fd

    if(client->id == peer_id){
        if(client->vector_fd>=0){
            //we already have the vector fd
            return -1;
        }
        client->vector_fd = fd;

    }
    return 0;
}

/* read and handle new messages on the given fd_set */
int ivshmem_client_handle_fds(IvshmemClient *client, fd_set *fds, int maxfd){
    if (client->sock_fd < maxfd && FD_ISSET(client->sock_fd, fds) && ivshmem_client_handle_server_msg(client) < 0 && errno != EINTR) {
        fprintf(stderr, "ivshmem_client_handle_server_msg() failed\n");
        return -1;
    } else if (ivshmem_client_handle_event(client, fds, maxfd) < 0 && errno != EINTR) {
        fprintf(stderr, "ivshmem_client_handle_event() failed\n");
        return -1;
    }

    return 0;
}

/* listen on unix socket (notifications of new and dead peers or vectors), and on eventfd (IRQ request) */
int ivshmem_client_poll_events(IvshmemClient *client){
    fd_set fds;
    int ret, maxfd;

    while (1) {

        FD_ZERO(&fds);
        maxfd = 0;

        ivshmem_client_get_fds(client, &fds, &maxfd);

        ret = select(maxfd, &fds, NULL, NULL, NULL);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }

            fprintf(stderr, "select error: %s\n", strerror(errno));
            break;
        }
        if (ret == 0) {
            continue;
        }

        if (ivshmem_client_handle_fds(client, &fds, maxfd) < 0) {
            fprintf(stderr, "ivshmem_client_handle_fds() failed\n");
            usleep(250);
            break;
        }
    }

    return ret;
}
