#include <arpa/inet.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/timerfd.h>
#include <sys/time.h>
#include <errno.h>
#include <netinet/tcp.h>
#include "client_protocol.h"
#include "game_server_protocol.h"
#include "utils.h"


static char *player_name = "";
static char *server_address = "";
static char *server_port = "2021";
static char *gui_address = "localhost";
static char *gui_port = "20210";


/* Static buffer for storing serialized data
 * from client datagram (sent every 30ms)
 */
static char client_dgram_buffer[CLIENT_DGRAM_BUFFER_SIZE];


/* Static buffer for storing data sent in UDP
 * datagram from game server to the client
 */
static char server_dgram_buffer[MAX_SERVER_UDP_DGRAM_LENGTH + 1];


static char client_to_gui_buffer[MSG_GUI_BUFFER_LENGTH];
static char partial_gui_msg[PARTIAL_MSG_BUFFER_LENGTH];
static ssize_t partial_gui_msg_length;


/* Static structures for resolving addresses
 * to both game server and GUI server
 */
static struct addrinfo addr_hints_server;
static struct addrinfo addr_hints_gui;


static struct addrinfo *addr_result_server;
static struct addrinfo *addr_result_gui;


/* Miscellaneous data used for exchanging datagrams with game server
 * by client
 */
static uint64_t player_session_id;


/* Do we need continue work? */
static bool continue_working = true;


static
void print_program_usage(const char *program_name) {
	fprintf(stderr, "Usage: %s game_server_address [-n player_name] [-p game_server_port] "
                   "[-i gui_server_address] [-r gui_server_port]\n", program_name);
}


static
void parse_program_arguments(int argc, char *argv[]) {
	int option = 0;
	
	while((option = getopt(argc, argv + 1, "n:p:i:r:")) != -1) {
		switch(option) {
			case 'n':
				player_name = optarg;
				break;
			case 'p':
				server_port = optarg;
				break;
			case 'i':
				gui_address = optarg;
				break;
			case 'r':
				gui_port = optarg;
				break;
			default:
				print_program_usage(argv[0]);
				exit(1);
		}
	}
}


static
void check_getaddrinfo(int ret_val) {
    if(ret_val == EAI_SYSTEM) { /* system error */
        fprintf(stderr, "getaddrinfo: %s", gai_strerror(ret_val));
        exit(1);
    }
    else if(ret_val != 0) { /* other error (host not found, etc.) */
        fprintf(stderr, "getaddrinfo: %s", gai_strerror(ret_val));
        exit(1);
    }
}


static
void initialise_sockets(int *socket_srv, int *socket_gui) {
    *socket_srv = socket(addr_result_server->ai_family,
                         addr_result_server->ai_socktype,
                         addr_result_server->ai_protocol);

    if(*socket_srv < 0) {
        perror("socket");
        exit(1);
    }

    *socket_gui = socket(addr_result_gui->ai_family,
                         addr_result_gui->ai_socktype,
                         addr_result_gui->ai_protocol);

    if(*socket_gui < 0) {
        perror("socket");
        exit(1);
    }
}


/* Function which sends datagram with client request for events
 * data from server
 */
static
void handle_keepalive(client_dgram_t  *data, client_game_state_t *state) {
    /* Update data concerning current player's turn
     * direction and next requested event number
     */
    data->turn_direction = state->client_turn_direction;
    data->next_expected_event_no = state->next_expected;

    /* Serialize binary data from client datagram structure
     * client_dgram_buffer so as to send this data to the game
     * server
     */
    serialize_client_dgram(data, strlen(player_name), client_dgram_buffer);

    ssize_t datagram_len = CLIENT_DGRAM_INTEGERS_LEN + strlen(player_name);

    ssize_t ret_val = sendto(state->server_socket,
                            client_dgram_buffer,
                            datagram_len,
                            0,
                            addr_result_server->ai_addr,
                            addr_result_server->ai_addrlen);

    if(ret_val != datagram_len) {
        perror("sendto");
    }
}


/* Function which handles getting information about events data
 * obtained from the game server
 */
static
void handle_server_message(client_game_state_t *state) {
    ssize_t read_bytes = recvfrom(state->server_socket,
                                  server_dgram_buffer,
                                  sizeof(server_dgram_buffer),
                                  0,
                                  addr_result_server->ai_addr,
                                  &addr_result_server->ai_addrlen);

    if(read_bytes > MAX_SERVER_UDP_DGRAM_LENGTH) {
        return;
    }

    if(read_bytes < 0) {
        perror("recvfrom");
        exit(1);
    }
    else if(read_bytes < MIN_SERVER_UDP_DGRAM_LENGTH) {
        fprintf(stderr, "Datagram length lower than minimal expected\n");
        return;
    }

    uint32_t received_game_id = *(uint32_t *) server_dgram_buffer;

    if(received_game_id != state->game_id || !state->played_any) {
        if(state->game_over) {
            state->game_id = received_game_id;
            state->next_expected = 0;
            state->players_count = 0;

            state->played_any = true;
            state->game_over = false;

            for(uint8_t i = 0; i < MAX_PLAYERS; ++i) {
                state->is_alive[i] = true;
            }
        }
        else {
            return;
        }
    }

    bool continue_parsing = true;
    ssize_t remaining_bytes = read_bytes - 4;
    ssize_t ret_val;
    ssize_t buffer_offset = 4;

    while(continue_parsing) {
        ret_val = deserialize_event_record(state,
                                           server_dgram_buffer + buffer_offset,
                                           remaining_bytes);

        if(ret_val == -1) {
            break;
        }
        else if(ret_val == -2) {
            fprintf(stderr, "Strange data from game server... terminating\n");
            exit(1);
        }
        else {
            buffer_offset += ret_val;
            remaining_bytes -= ret_val;

            if(state->data_for_gui.ready_to_send) {
                size_t length_to_be_sent = prepare_message(state, client_to_gui_buffer);

                ssize_t ret_write = write(state->gui_socket, client_to_gui_buffer, length_to_be_sent);

                if (ret_write < 0) {
                    perror("write");
                    exit(1);
                }

                state->data_for_gui.ready_to_send = 0;
            }
        }

        if(remaining_bytes == 0) {
            continue_parsing = false;
        }
    }
}


static
void handle_gui_message(client_game_state_t *state) {
    memset(client_to_gui_buffer, 0, MSG_GUI_BUFFER_LENGTH);

    ssize_t read_bytes = read(state->gui_socket,
                              client_to_gui_buffer,
                              MSG_GUI_BUFFER_LENGTH);

    if(read_bytes < 0) {
        if(errno == EINTR) {
            return;
        }
        else {
            perror("read");
            exit(1);
        }
    }
    else if(read_bytes == 0) {
        fprintf(stderr, "Connection to GUI server lost...\n");
        continue_working = false;
        exit(1);
    }
    else {
        ssize_t len = 0;
        bool newline = false;

        /* Check if new line is present in current TCP message
         */
        while(!newline && len < read_bytes) {
            if(client_to_gui_buffer[len] == '\n') {
                newline = true;
            }

            ++len;
        }

        ssize_t start = partial_gui_msg_length;

        partial_gui_msg_length += len;

        if(newline && (size_t) partial_gui_msg_length < PARTIAL_MSG_BUFFER_LENGTH) {
            for(ssize_t i = start; i < partial_gui_msg_length; ++i) {
                partial_gui_msg[i] = client_to_gui_buffer[i];
            }

            if(partial_gui_msg_length == LENGTH_LEFT_KEY_DOWN &&
               strcmp(partial_gui_msg, "LEFT_KEY_DOWN\n") == 0) {

                state->client_turn_direction = 2;
            }
            else if(partial_gui_msg_length == LENGTH_RIGHT_KEY_DOWN &&
                    strcmp(partial_gui_msg, "RIGHT_KEY_DOWN\n") == 0) {

                state->client_turn_direction = 1;
            }
            else if(partial_gui_msg_length == LENGTH_LEFT_KEY_UP &&
                    strcmp(partial_gui_msg, "LEFT_KEY_UP\n") == 0) {

                state->client_turn_direction = 0;
            }
            else if(partial_gui_msg_length == LENGTH_RIGHT_KEY_UP &&
                    strcmp(partial_gui_msg, "RIGHT_KEY_UP\n") == 0) {

                state->client_turn_direction = 0;
            }
        }

        if(newline) {
            memset(partial_gui_msg, 0, PARTIAL_MSG_BUFFER_LENGTH);
            partial_gui_msg_length = 0;
        }
    }
}


int main(int argc, char *argv[]) {
    struct timeval tv;

    if(gettimeofday(&tv, NULL) < 0) {
        perror("gettimeofday");
        exit(1);
    }

    player_session_id = 1000000 * tv.tv_sec + tv.tv_usec;
    partial_gui_msg_length = 0; /* set to initial value equal 0 */

	if(argc < 2) {
		print_program_usage(argv[0]);
		exit(1);
	}

	parse_program_arguments(argc - 1, argv);

    if(!check_player_name(player_name)) {
        fprintf(stderr, "Player name either too long or contains illegal characters\n");
        exit(1);
    }

	if(!check_integer(server_port) || !check_integer(gui_port)) {
	    fprintf(stderr, "Bad ports provided (non-digits characters detected)\n");
	    exit(1);
	}

	server_address = argv[1];

	addr_hints_server.ai_socktype = SOCK_DGRAM;
	addr_hints_gui.ai_socktype = SOCK_STREAM;

	int err;

	err = getaddrinfo(server_address,
                      server_port,
                      &addr_hints_server,
                      &addr_result_server);

    check_getaddrinfo(err);

    err = getaddrinfo(gui_address,
                      gui_port,
                      &addr_hints_gui,
                      &addr_result_gui);

    check_getaddrinfo(err);

    int socket_srv;
    int socket_gui;

    initialise_sockets(&socket_srv, &socket_gui);

    int opt = 1;

    if(setsockopt(socket_gui, IPPROTO_TCP, TCP_NODELAY, (void *) &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(1);
    }

	if(connect(socket_gui, addr_result_gui->ai_addr, addr_result_gui->ai_addrlen) < 0) {
	    perror("connect");
	    exit(1);
	}

    int timer_fd = timerfd_create(CLOCK_MONOTONIC,  0);
    struct itimerspec spec = { { 0, 30000000 }, { 0, 30000000 } };

    if(timer_fd < 0) {
        perror("timerfd_create");
        exit(1);
    }

    timerfd_settime(timer_fd, 0, &spec, NULL);

    client_game_state_t  game_state;
    initialise_client_game_state(&game_state);

    game_state.server_socket = socket_srv;
    game_state.gui_socket = socket_gui;
    game_state.player_name_len = strlen(player_name);
    game_state.game_id = 0; /* default value, no collision will ever occur
                             * because of field 'played_any'
                             */
    game_state.next_expected = 0;
    game_state.game_over = true;
    game_state.played_any = false;

    memcpy(game_state.player_name, player_name, strlen(player_name));

    client_dgram_t data;

    data.session_id = player_session_id;
    memcpy(data.player_name, player_name, strlen(player_name));

    uint64_t timers_elapsed;

    struct pollfd fds[3];

    fds[0].fd = timer_fd;
    fds[0].revents = 0;
    fds[0].events = POLLIN;

    fds[1].fd = socket_srv;
    fds[1].revents = 0;
    fds[1].events = POLLIN;

    fds[2].fd = socket_gui;
    fds[2].revents = 0;
    fds[2].events = POLLIN;

    ssize_t ret_val;

	while(continue_working) {
        int ret = poll(fds, 3, -1);

        if(ret > 0) {
            if(fds[0].revents & POLLIN) {
                ret_val = read(fds[0].fd, &timers_elapsed, 8);

                if(ret_val < 0) {
                    perror("read");
                }

                handle_keepalive(&data, &game_state);
            }

            if(fds[1].revents & POLLIN) {
                handle_server_message(&game_state);
            }

            if(fds[2].revents & POLLIN) {
                handle_gui_message(&game_state);
            }
        }
	}

    if(close(socket_srv) < 0) {
        perror("close");
        exit(1);
    }

	if(close(socket_gui) < 0) {
	    perror("close");
	    exit(1);
	}

    freeaddrinfo(addr_result_server);
    freeaddrinfo(addr_result_gui);

	exit(0);
}
