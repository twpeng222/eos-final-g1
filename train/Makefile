CC = gcc
CFLAGS = -Wall -Wextra -g
TARGET = server 

all: $(TARGET) CLIENT

$(TARGET): server.c 
	$(CC) $(CFLAGS) -o $(TARGET) server.c 

CLIENT: client.c
	$(CC) $(CFLAGS) -o client client.c


clean:
	rm -f $(TARGET) client
