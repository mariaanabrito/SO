CC = gcc
CFLAGS = -Wall

all : client server

client : client.o
	$(CC) client.c -o client $(CFLAGS)

server : server.o
	$(CC) server.c -o server $(CFLAGS)

client.o : client.c
	cc -c client.c

server.o : server.c
	cc -c server.c

clean:
	rm -f *.o client server
