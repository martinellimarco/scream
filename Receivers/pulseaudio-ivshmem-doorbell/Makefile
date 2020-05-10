CC=gcc
CFLAGS = -Wall

scream-pulse: scream-ivshmem-doorbell-pulse.c
	$(CC) $(CFLAGS) -o scream-ivshmem-doorbell-pulse scream-ivshmem-doorbell-pulse.c ivshmem-client.c -lpulse-simple -lpulse -O2
