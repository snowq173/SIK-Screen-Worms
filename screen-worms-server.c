#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/timerfd.h>
#include <math.h>
#include "game_server_protocol.h"
#include "client_protocol.h"
#include "utils.h"


#define MAX_PLAYERS         25


static char *str_port = NULL;
static char *str_seed = NULL;
static char *str_turning_speed = NULL;
static char *str_rounds_per_sec = NULL;
static char *str_width = NULL;
static char *str_height = NULL;


static
void print_program_usage(const char *program_name) {
    fprintf(stderr, "Usage: %s [-p port_number] [-s seed] [-t turning_speed] "
                    "[-v rounds per second] [-w board width] [-h board height]\n", program_name);
}


static
void parse_program_arguments(int argc, char *argv[]) {
    int option = 0;

    while((option = getopt(argc, argv, "p:s:t:v:w:h:")) != -1) {
        switch(option) {
            case 'p':
                str_port = optarg;
                break;
            case 's':
                str_seed = optarg;
                break;
            case 't':
                str_turning_speed = optarg;
                break;
            case 'v':
                str_rounds_per_sec = optarg;
                break;
            case 'w':
                str_width = optarg;
                break;
            case 'h':
                str_height = optarg;
                break;
            default:
                print_program_usage(argv[0]);
                exit(1);
        }
    }

    if(argc != optind) {
        print_program_usage(argv[0]);
        exit(1);
    }
}


static
void handle_timers(server_game_state_t *state) {
    ssize_t read_ret_val;
    ssize_t disconnected_name_len;
    ssize_t client_index = 0;
    uint64_t timers_elapsed;

    for(int i = 2; i < SERVER_POLL_DESCRIPTORS_COUNT; ++i) {
        if(state->fds[i].revents & POLLIN) {
            read_ret_val =  read(state->fds[i].fd,
                                 &timers_elapsed,
                                 sizeof(timers_elapsed));

            if(read_ret_val < 0) {
                perror("read");
            }

            client_index = i - 2;


            if(state->players[client_index].conn.is_connection_active &&
               !state->players[client_index].message) {

                //fprintf(stderr, "Disconnecting...\n");

                /* Close the descriptor corresponding to the timer which
                 * handles current client slot timeouts
                 */
                if(close(state->fds[i].fd) < 0) {
                    perror("close");
                }

                /* Disable connection with client */
                state->players[client_index].conn.is_connection_active = false;

                /* Decrease the number of connected players */
                state->connected_players--;

                /* Length of the name of the players which is being timeouted at the
                 * moment
                 */
                disconnected_name_len = strlen(state->players[client_index].name);

                /* Set client name buffer to NUL bytes
                 */
                memset(state->players[client_index].name, 0, MAX_PLAYER_NAME_LENGTH + 1);

                /* Take some actions when disconnect happens during waiting
                 * for players before new game can begin
                 */
                if(state->game_status == GAME_STATE_WAITING_FOR_PLAYERS) {
                    if(state->players[client_index].ready) {
                        state->players[client_index].ready = false;
                        state->ready_players--;
                    }

                    if(disconnected_name_len > 0) {
                        state->players_count--;
                    }

                    state->players[client_index].ready = false;

                    if(state->ready_players == state->players_count &&
                       state->ready_players > 1) {

                        /* Initiate new game as all remaining players have been
                         * marked as ready for participating in new game and there
                         * are at least two of such players
                         */
                        initiate_game(state);
                    }
                }
            }

            /* Reset the status of acquired message (for further timeout
             * checks)
             */
            state->players[client_index].message = false;
        }
    }
}


static
void handle_new_client(server_game_state_t *state,
                       ssize_t datagram_size,
                       client_dgram_t *dgram) {

    if(state->connected_players == MAX_PLAYERS) {
        return;
    }

    uint8_t index_for_player = 0;
    ssize_t name_length = datagram_size - CLIENT_DGRAM_INTEGERS_LEN;

    for(uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if(!state->players[i].conn.is_connection_active) {
            index_for_player = i;
            break;
        }
    }

    /* Update connection data for the new player */
    state->players[index_for_player].conn.session_id = dgram->session_id;
    state->players[index_for_player].conn.is_connection_active = true;
    state->players[index_for_player].conn.address = state->receive_address;
    state->players[index_for_player].conn.address_length = state->receive_address_length;

    /* Increase the number of connected players */
    state->connected_players++;

    if(state->game_status == GAME_STATE_GAME_STARTED) {
        state->players[index_for_player].is_spectator = true;
    }
    else {
        if(name_length > 0) {
            state->players[index_for_player].is_playing = true;


            state->players_count++;

            if(dgram->turn_direction != 0) {
                state->players[index_for_player].ready = true;
                state->ready_players++;
            }

            state->players[index_for_player].turn_direction = dgram->turn_direction;
        }
        else {
            state->players[index_for_player].is_spectator = true;
        }
    }

    /* Copy new player's name
     */
    memcpy(state->players[index_for_player].name, dgram->player_name, name_length);

    /* Set up descriptor for newly connected client timeout clock
     */
    if((state->fds[2 + index_for_player].fd = timerfd_create(CLOCK_MONOTONIC,  0)) < 0) {
        perror("timerfd_create");
    }

    /* Setup timeout timer for the newly connected client
     */
    if(timerfd_settime(state->fds[2 + index_for_player].fd,
                       0, &state->timeout_params,
                       NULL) < 0) {

        perror("timerfd_settime");
    }

    send_game_data(state, dgram->next_expected_event_no, index_for_player);

    if(state->game_status == GAME_STATE_WAITING_FOR_PLAYERS &&
       state->ready_players == state->players_count &&
       state->players_count > 1) {

        initiate_game(state);
    }
}


static
void handle_existing_client(server_game_state_t *state,
                            uint8_t addr_index,
                            ssize_t datagram_size,
                            client_dgram_t *dgram) {
    ssize_t name_length = datagram_size - CLIENT_DGRAM_INTEGERS_LEN;

    if(dgram->session_id > state->players[addr_index].conn.session_id) {
        state->players[addr_index].conn.session_id = dgram->session_id;

        if(state->game_status == GAME_STATE_GAME_STARTED) {
            state->players[addr_index].is_spectator = true;

            memset(state->players[addr_index].name, 0, MAX_PLAYER_NAME_LENGTH + 1);
            memcpy(state->players[addr_index].name, dgram->player_name, name_length);
        }
        else {
            if(strlen(state->players[addr_index].name) == 0) {
                if(name_length > 0) {
                    state->players[addr_index].is_spectator = false;
                    state->players_count++;

                    if(dgram->turn_direction != 0) {
                        state->ready_players++;
                    }
                }
            }
            else {
                if(name_length == 0) {
                    state->players[addr_index].is_spectator = true;
                    state->players_count--;

                    if(state->players[addr_index].ready) {
                        state->players[addr_index].ready = false;
                        state->ready_players--;
                    }
                }
            }
        }

        /* Update player name in client structure according to the data which is
         * stored in datagram
         */
        memset(state->players[addr_index].name, 0, MAX_PLAYER_NAME_LENGTH + 1);
        memcpy(state->players[addr_index].name, dgram->player_name, name_length);

        if(close(state->fds[2 + addr_index].fd) < 0) {
            perror("close");
        }

        /* Associate new clock descriptor with newly opened client session
         */
        if((state->fds[2 + addr_index].fd = timerfd_create(CLOCK_MONOTONIC, 0)) < 0) {
            perror("timerfd_create");
        }

        /* And set timeout timer to work
         */
        if(timerfd_settime(state->fds[2 + addr_index].fd, 0, &state->timeout_params, NULL) < 0) {
            perror("timerfd_settime");
        }
    }
    else if(dgram->session_id < state->players[addr_index].conn.session_id) {
        /* Ignore datagrams with smaller session_id as in the task
         * specification
         */
        return;
    }
    else {
        if(strlen(state->players[addr_index].name) != (size_t) name_length ||
           strncmp(state->players[addr_index].name, dgram->player_name, name_length) != 0) {

            return;
        }

        if(state->game_status == GAME_STATE_WAITING_FOR_PLAYERS) {
            if(dgram->turn_direction != 0 && !state->players[addr_index].ready) {
                state->players[addr_index].ready = true;
                state->ready_players++;
            }

            state->players[addr_index].turn_direction = dgram->turn_direction;
        }
        else {
            if(!state->players[addr_index].is_spectator &&
               state->alive[addr_index]) {

                /* Update client's worm direction if the client is participating in the game
                 * (is not a spectator) and is alive
                 */
                state->players[addr_index].turn_direction = dgram->turn_direction;
            }
        }

        /* Mark player as 'safe' from timeout by indicating that a datagram
         * has been received from the client
         */
        state->players[addr_index].message = true;
    }
}


static
void handle_client_datagram(server_game_state_t *state) {
    memset(&state->receive_address, 0, sizeof(struct sockaddr_in6));
    state->receive_address_length = sizeof(struct sockaddr_in6);

    ssize_t read_bytes = recvfrom(state->server_socket,
                                  state->server_buffer,
                                  sizeof(state->server_buffer),
                                  0,
                                  (struct sockaddr *) &state->receive_address,
                                  &state->receive_address_length);

    if(read_bytes < 0) {
        perror("recvfrom");
    }

    client_dgram_t dgram;

    ssize_t ret_val = deserialize_client_dgram(&dgram, state->server_buffer, read_bytes);

    if(ret_val < 0) {
        return;
    }

    ssize_t name_length = read_bytes - CLIENT_DGRAM_INTEGERS_LEN;

    uint8_t addr_index = MAX_PLAYERS;
    bool exists_name = false;

    for(uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if(name_length > 0 && (strlen(state->players[i].name) == (size_t) name_length) &&
           strncmp(state->players[i].name, dgram.player_name, name_length) == 0) {
            exists_name = true;
        }

        if(state->players[i].conn.is_connection_active &&
           equal_addresses(&state->players[i].conn.address, &state->receive_address)) {
            addr_index = i;
            break;
        }
    }

    if(addr_index < MAX_PLAYERS) {
        handle_existing_client(state, addr_index, read_bytes, &dgram);
    }
    else if(!exists_name) {
        handle_new_client(state, read_bytes, &dgram);
    }
}


static
void handle_board_update(server_game_state_t *state) {
    int32_t x_after_move;
    int32_t y_after_move;

    event_data_t current_event;

    uint32_t first_bo_be_broadcast = state->events_count;

    for(uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if(!state->players[i].is_playing) {
            continue;
        }

        if(state->alive[i]) {
            if(state->players[i].turn_direction == 1) {
                state->players[i].direction += state->game_params.turning_speed;
            }
            else if(state->players[i].turn_direction == 2) {
                state->players[i].direction -= state->game_params.turning_speed;
            }

            if(state->players[i].direction < 0) {
                state->players[i].direction += 360;
            }


            /* Player coordinates before position update (rounded down to nearest unsigned integers)) */
            uint32_t old_x = (uint32_t) floor(state->players[i].x_pos);
            uint32_t old_y = (uint32_t) floor(state->players[i].y_pos);


            /* Update player position according to the player's direction */
            state->players[i].x_pos += cos((double) state->players[i].direction * M_PI / 180);
            state->players[i].y_pos += sin((double) state->players[i].direction * M_PI / 180);


            /* Player coordinates after position update (rounded down to nearest signed integers) */
            x_after_move = (int32_t) floor(state->players[i].x_pos);
            y_after_move = (int32_t) floor(state->players[i].y_pos);


            if((uint32_t) x_after_move == old_x &&
               (uint32_t) y_after_move == old_y) {

                continue;
            }
            else if(x_after_move < 0 || (uint32_t) x_after_move >= state->game_params.board_dimension_x ||
                    y_after_move < 0 || (uint32_t) y_after_move >= state->game_params.board_dimension_y ||
                    state->game_board[x_after_move][y_after_move]) {

                state->alive_players_count--;
                state->alive[i] = false;

                current_event.event_type = EVENT_PLAYER_ELIMINATED;
                current_event.player_number = state->players[i].player_number;
            }
            else {
                /* Mark board field as used */
                state->game_board[x_after_move][y_after_move] = true;

                current_event.event_type = EVENT_PIXEL;
                current_event.player_number = state->players[i].player_number;
                current_event.x = (uint32_t) x_after_move;
                current_event.y = (uint32_t) y_after_move;
            }

            enqueue_event(state, &current_event);

            if(state->alive_players_count == 1) {
                /* Game over, we are waiting for players now */
                state->game_status = GAME_STATE_WAITING_FOR_PLAYERS;

                current_event.event_type = EVENT_GAME_OVER;
                enqueue_event(state, &current_event);

                if(close(state->fds[1].fd) < 0) {
                    perror("close");
                }

                state->fds[1].fd = -1;

                update_players_after_game(state);
                break;
            }
        }
    }

    /* Broadcast all events that occurred in this rounds to the connected
     * players (and spectators)
     */
    broadcast_events(state, first_bo_be_broadcast);
}


int main(int argc, char *argv[]) {
    parse_program_arguments(argc, argv);


    if(!check_integer(str_port)          || !check_integer(str_seed)           ||
       !check_integer(str_turning_speed) || !check_integer(str_rounds_per_sec) ||
       !check_integer(str_width)         || !check_integer(str_height)) {

        print_program_usage(argv[0]);
        exit(1);
    }


    /* If seed has been provided, check whether correct range was provided
     * (that is, its value does not exceed UINT32_MAX value)
     */
    if(str_seed != NULL) {
        uint64_t seed_range_test = strtoull(str_seed, NULL, 10);

        if(seed_range_test > UINT32_MAX) {
            print_program_usage(argv[0]);
            exit(1);
        }
    }


    uint32_t server_port = str_port ? atoi(str_port) : 2021;
    uint32_t seed = str_seed ? atoi(str_seed) : time(NULL);
    uint8_t turning_speed = str_turning_speed ? atoi(str_turning_speed) : DEFAULT_TURNING_SPEED;
    uint32_t rounds_per_sec = str_rounds_per_sec ? atoi(str_rounds_per_sec) : DEFAULT_ROUNDS_PER_SEC;
    uint32_t board_dimension_x = str_width ? atoi(str_width) : DEFAULT_BOARD_WIDTH;
    uint32_t board_dimension_y = str_height ? atoi(str_height) : DEFAULT_BOARD_HEIGHT;


    if(board_dimension_x > MAX_X_SIZE || board_dimension_y > MAX_Y_SIZE ||
       board_dimension_x == 0         || board_dimension_y == 0           ) {
        fprintf(stderr, "Incorrect board dimensions. Maximal accepted values are: width -  %d, height - %d,"
                        " positive integers\n",
                MAX_X_SIZE, MAX_Y_SIZE);
        exit(1);
    }

    if(turning_speed > MAX_TURNING_SPEED || turning_speed == 0) {
        fprintf(stderr, "Turning speed too big. Maximal accepted value: %d, "
                        "positive integer\n", MAX_TURNING_SPEED);
        exit(1);
    }

    if(rounds_per_sec > MAX_ROUNDS_PER_SEC || rounds_per_sec == 0) {
        fprintf(stderr, "Rounds per second incorrect. Maximal accepted value: %d, "
                        "positive integer\n", MAX_ROUNDS_PER_SEC);
        exit(1);
    }


    /* Allocate memory for server_game_state_t structure */
    server_game_state_t *state = malloc(sizeof(server_game_state_t));

    if(state == NULL) {
        perror("malloc");
        exit(1);
    }

    memset(&state->receive_address, 0, sizeof(struct sockaddr_in6));


    /* Initialise the game state with the values that will
     * not change thereafter (board dimensions etc.)
     */
    state->game_params.turning_speed = turning_speed;
    state->game_params.rounds_per_sec = rounds_per_sec;
    state->game_params.board_dimension_x = board_dimension_x;
    state->game_params.board_dimension_y = board_dimension_y;


    /* Allocate the memory for game board
     */
    state->game_board = malloc(board_dimension_x * sizeof(bool *));

    if(state->game_board == NULL) {
        free(state);
        perror("malloc");
        exit(1);
    }

    for(uint32_t i = 0; i < state->game_params.board_dimension_x; ++i) {
        state->game_board[i] = malloc(board_dimension_y * sizeof(bool));

        if(state->game_board[i] == NULL) {
            for(uint32_t j = 0; j < i; ++j) {
                free(state->game_board[j]);
            }

            free(state->game_board);
            free(state);

            perror("malloc");
            exit(1);
        }
    }


    /* Initialise the game state with default values for the
     * fields that will be changing
     */
    state->ready_players = 0;
    state->alive_players_count = 0;
    state->connected_players = 0;
    state->events_count = 0;

    state->random.seed = seed;
    state->random.seed_no = 0;

    state->game_status = GAME_STATE_WAITING_FOR_PLAYERS;

    /* Initialise data for the players */
    for(uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        state->players[i].conn.is_connection_active = false;
        state->players[i].is_playing = false;
        state->players[i].is_spectator = false;
        state->players[i].message = false;

        state->alive[i] = true;

        /* By default fill each of name buffers with ASCII NUL bytes */
        memset(state->players[i].name, 0, MAX_PLAYER_NAME_LENGTH + 1);
        memset(state->game_primary_player_names[i], 0, MAX_PLAYER_NAME_LENGTH + 1);
    }

    state->events_queue = malloc(DEFAULT_EVENTS_QUEUE_SIZE * sizeof(event_data_t));
    state->events_queue_size = DEFAULT_EVENTS_QUEUE_SIZE;

    if(state->events_queue == NULL) {
        perror("malloc");
        exit(1);
    }

    int sock = socket(AF_INET6, SOCK_DGRAM, 0);

    if(sock < 0) {
        perror("socket");
        exit(1);
    }

    state->server_socket = sock;

    struct sockaddr_in6 server_addr;
    memset(&server_addr, 0, sizeof(struct sockaddr_in6));

    server_addr.sin6_family = AF_INET6;
    server_addr.sin6_addr = in6addr_any;
    server_addr.sin6_port = htons(server_port);

    if(bind(sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }


    int poll_ret_val;
    uint64_t timers_elapsed;


    double relay_time = 1 / (double) state->game_params.rounds_per_sec;
    uint32_t relay_millis = 1000 * relay_time;
    uint32_t relay_nanos = relay_millis * (uint32_t) MILLIS_TO_NANO_MULTIPLIER;


    struct itimerspec spec = {{2, 0}, {2, 0}};
    struct itimerspec spec_moves = { {0, relay_nanos}, {0, relay_nanos} };


    /* Save timeout parameters specification in server data structure */
    state->timeout_params = spec;
    state->round_params = spec_moves;


    state->fds[0].fd = sock;
    state->fds[0].events = POLLIN;
    state->fds[0].revents = 0;

    for(size_t i = 1; i < SERVER_POLL_DESCRIPTORS_COUNT; ++i) {
        state->fds[i].fd = -1;
        state->fds[i].events = POLLIN;
        state->fds[i].revents = 0;
    }


    ssize_t read_ret_val;


    while(1) {
        poll_ret_val = poll(state->fds, 27, -1);

        if(poll_ret_val) {
            if(state->fds[1].revents & POLLIN) {
                read_ret_val = read(state->fds[1].fd,
                                    &timers_elapsed,
                                    sizeof(timers_elapsed));

                if(read_ret_val < 0) {
                    perror("read");
                }

                if(state->game_status == GAME_STATE_GAME_STARTED) {
                    printf("Updating the board...\n");
                    handle_board_update(state);
                }
            }

            handle_timers(state);

            if(state->fds[0].revents & POLLIN) {
                handle_client_datagram(state);
            }
        }
    }


    /* Deallocate the memory that was allocated
     * for holding server game data
     */
    free(state->events_queue);
    free(state);

    return 0;
}
