CC = gcc
CFLAGS = -Wall
LDFLAGS = -lpthread

all: server client

server: server.c
	$(CC) $(CFLAGS) server.c -o server $(LDFLAGS)

client: client.c
	$(CC) $(CFLAGS) client.c -o client

clean:
	rm -f server client program.out received_*.out output_*.txt