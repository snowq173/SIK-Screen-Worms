CC = gcc
CFLAGS = -Wall -Wextra -O2
LDLIBS = -lm

.PHONY: serwer clean

all: screen-worms-server screen-worms-client

screen-worms-server: screen-worms-server.o utils.o game_server_protocol.o client_protocol.o

screen-worms-client: screen-worms-client.o utils.o client_protocol.o game_server_protocol.o
	$(CC) $(LDFLAGS) -o $@ $^

client_protocol.o: client_protocol.c client_protocol.h
	$(CC) $(CFLAGS) -c $<

game_server_protocol.o: game_server_protocol.c game_server_protocol.h
	$(CC) $(CFLAGS) -c $<

utils.o: utils.c utils.h
	$(CC) $(CFLAGS) -c $<

screen-worms-server.o: screen-worms-server.c
	$(CC) $(CFLAGS) -c $<

screen-worms-client.o: screen-worms-client.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o screen-worms-client screen-worms-server testing
