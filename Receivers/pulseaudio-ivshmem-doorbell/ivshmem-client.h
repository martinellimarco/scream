//derived from https://github.com/qemu/qemu/blob/master/contrib/ivshmem-client/ivshmem-client.h

#ifndef IVSHMEM_CLIENT_H
#define IVSHMEM_CLIENT_H

#include <sys/select.h>
#include <stdint.h>

#define IVSHMEM_PROTOCOL_VERSION 0

typedef struct IvshmemClient IvshmemClient;

/**
 * Typedef of callback function used when our IvshmemClient receives a
 * notification from a peer.
 */
typedef void (*IvshmemClientNotifCb)();

/**
 * Structure describing an ivshmem client
 *
 * This structure stores all information related to our client: the name
 * of the server unix socket, the list of peers advertised by the
 * server, our own client information, and a pointer the notification
 * callback function used when we receive a notification from a peer.
 */
struct IvshmemClient {
    char unix_sock_path[1024];          /**< path to unix sock */
    int sock_fd;                        /**< unix sock filedesc */
    int shm_fd;                         /**< shm file descriptor */

    int64_t id;                              /**< the id of the peer */
    int vector_fd;                           /**< vector fd */

    IvshmemClientNotifCb notif_cb;      /**< notification callback */
};

/**
 * Initialize an ivshmem client
 *
 * @client:         A pointer to an uninitialized IvshmemClient structure
 * @unix_sock_path: The pointer to the unix socket file name
 * @notif_cb:       If not NULL, the pointer to the function to be called when
 *                  our IvshmemClient receives a notification from a peer
 * Returns:         0 on success, or a negative value on error
 */
int ivshmem_client_init(IvshmemClient *client, const char *unix_sock_path, IvshmemClientNotifCb notif_cb);

/**
 * Connect to the server
 *
 * Connect to the server unix socket, and read the first initial
 * messages sent by the server, giving the ID of the client and the file
 * descriptor of the shared memory.
 *
 * @client: The ivshmem client
 *
 * Returns: 0 on success, or a negative value on error
 */
int ivshmem_client_connect(IvshmemClient *client);

/**
 * Close connection to the server and free all structures
 *
 * @client: The ivshmem client
 */
void ivshmem_client_close(IvshmemClient *client);

/**
 * Fill a fd_set with file descriptors to be monitored
 *
 * This function will fill a fd_set with all file descriptors
 * that must be polled (unix server socket and peers eventfd). The
 * function will not initialize the fd_set, it is up to the caller
 * to do this.
 *
 * @client: The ivshmem client
 * @fds:    The fd_set to be updated
 * @maxfd:  Must be set to the max file descriptor + 1 in fd_set. This value is
 *          updated if this function adds a greater fd in fd_set.
 */
void ivshmem_client_get_fds(const IvshmemClient *client, fd_set *fds, int *maxfd);

/**
 * Read and handle new messages
 *
 * Given a fd_set filled by select(), handle incoming messages from server.
 *
 * @client: The ivshmem client
 * @fds:    The fd_set containing the file descriptors to be checked.
 * @maxfd:  The maximum fd in fd_set, plus one.
 *
 * Returns: 0 on success, or a negative value on error
 */
int ivshmem_client_handle_fds(IvshmemClient *client, fd_set *fds, int maxfd);

/**
 * @client: The ivshmem client
 *
 * Returns: 0 on success, or a negative value on error
 */
int ivshmem_client_poll_events(IvshmemClient *client);

#endif /* IVSHMEM_CLIENT_H */
