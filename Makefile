CC = gcc
CFLAGS = -Wall -g # -Werror -pedantic
LDFLAGS = 

all: rpc-server rpc-client

rpc-server: server.o rpc.o
	$(CC) $(LDFLAGS) -o rpc-server server.o rpc.o

rpc-client: client.o rpc.o
	$(CC) $(LDFLAGS) -o rpc-client client.o rpc.o

server.o: server.c rpc.h
	$(CC) $(CFLAGS) -c server.c

client.o: client.c rpc.h
	$(CC) $(CFLAGS) -c client.c

rpc.o: rpc.c rpc.h
	$(CC) $(CFLAGS) -c rpc.c

clean:
	rm -f *.o rpc_server rpc_client client server rpc-server rpc-client
