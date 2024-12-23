CC = gcc
CFLAGS = -Wall -Wextra -g
TARGET = server 

all: $(TARGET) CLIENT TRAIN HOST

$(TARGET): server.c 
	$(CC) $(CFLAGS) -o $(TARGET) server.c 
TRAIN: train_subserver.c
	$(CC) $(CFLAGS) -o train_subserver train_subserver.c 
CLIENT: client.c
	$(CC) $(CFLAGS) -o client client.c
HOST: host.c
	$(CC) $(CFLAGS) -o host host.c

clean:
	rm -f $(TARGET) client train_subserver host
