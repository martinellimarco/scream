# EXPERIMENTAL - feel free to contribute

# scream-ivshmem-doorbell-pulse

scream-ivshmem-doorbell-pulse is a scream receiver using pulseaudio as audio output and IVSHMEM-doorbell to share the ring buffer between guest and host without polling.

## Compile

You need pulseaudio headers in advance.

```shell
$ sudo yum install pulseaudio-libs-devel # Redhat, CentOS, etc.
or
$ sudo apt-get install libpulse-dev # Debian, Ubuntu, etc.
```

Run `make` command.

## Run ivshmem-server

It should have been installed with qemu, if not try qemu-utils for debian or see https://github.com/qemu/qemu/tree/master/contrib/ivshmem-server

Start the server before qemu or it will not start.

`ivshmem-server` will create an unix socket in /tmp/ivshmem_socket (you can change the location). Ensure qemu have access to this file.

```shell
ivshmem-server -l 1M -M scream-ivshmem -F -v
```

## VM Setup

You need an ivshmem-doorbell device.

In libvirt:

```xml
  <shmem name="scream-ivshmem">
    <model type="ivshmem-doorbell"/>
    <server path="/tmp/ivshmem_socket"/>
    <msi vectors="1" ioeventfd="on"/>
    <address type="pci" domain="0x0000" bus="0x00" slot="0x11" function="0x0"/>
  </shmem>
```

Unfortunately in libvirt it seems there isn't an option to specify the reconnect time and qemu doesn't reconnect if the ivshmem-server is restarted. If you find a way to do so please let me know.

In qemu command line (untested!):

```
-device ivshmem-doorbell,vectors=1,chardev=scream-ivshmem
-chardev socket,path=/tmp/ivshmem_socket,id=scream-ivshmem,reconnect=1
```

## Usage

Launch your VM, make sure to have read permission to the socket and execute

```shell
$ scream-ivshmem-doorbell-pulse /tmp/ivshmem_socket
``` 

