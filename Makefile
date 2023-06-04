PORT = 58370
CFLAGS= -DPORT=\$(PORT) -g -std=gnu99 -Wall -Werror

all: friend_server

friend_server : friend_server.o friends.o
	gcc ${CFLAGS} -o $@ $^

friends.o: friends.c friends.h
	gcc $(CFLAGS) -c friends.c

%.o: %.c
	gcc ${CFLAGS} -c $<

clean:
	rm *.o
