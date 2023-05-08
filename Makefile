CC = gcc
CFLAGS = -Wall -Werror -std=gnu99 -pthread

all: dbserver dbclient

dbserver: dbserver.o
	$(CC) dbserver.o -o dbserver $(CFLAGS)

dbclient: dbclient.o
	$(CC) dbclient.o -o dbclient $(CFLAGS)

dbserver.o: dbserver.c msg.h
	$(CC) -c dbserver.c $(CFLAGS)

dbclient.o: dbclient.c msg.h
	$(CC) -c dbclient.c $(CFLAGS)

clean:
	rm -f dbserver dbclient

drop:
	rm -f db

reset: clean drop