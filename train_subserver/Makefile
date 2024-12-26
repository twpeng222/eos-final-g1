CC = gcc
CFLAGS = -Wall -Wextra -g
TARGET = server 

all: $(TARGET) CLIENT TRAIN HOST HSR CLIENT2

$(TARGET): server.c 
	$(CC) $(CFLAGS) -o $(TARGET) server.c 
TRAIN: train_subserver.c
	$(CC) $(CFLAGS) -o train_subserver train_subserver.c 
CLIENT: client.c
	$(CC) $(CFLAGS) -o client client.c
CLIENT2: client2.c
	$(CC) $(CFLAGS) -o client2 client2.c -lncurses
HOST: host.c
	$(CC) $(CFLAGS) -o host host.c
HSR: HSR_subserver.c
	$(CC) $(CFLAGS) -o HSR_subserver HSR_subserver.c
HOST_NEW: host_new1.c
	$(CC) $(CFLAGS) -o host_new host_new1.c
HOST_NEW_CLIENT: host_new1_client.c
	$(CC) $(CFLAGS) -o host_new_client host_new1_client.c


clean:
	rm -f $(TARGET) client train_subserver host HSR_subserver client2 host_new host_new_client
