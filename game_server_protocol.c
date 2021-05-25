#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <string.h>
#include "game_server_protocol.h"
#include "utils.h"


uint32_t generate_random(seed_status_t *status) {
    if(status->seed_no) {
        uint64_t cast_value = (uint64_t) status->seed;
        cast_value *= (uint64_t) SEED_MULTIPLIER;
        cast_value %= (uint64_t) SEED_MODULUS;

        status->seed = cast_value;
    }

    status->seed_no++;
    return status->seed;
}


void sort_players(server_game_state_t *state) {
    client_t swap_client;
    uint8_t index_min;

    for(uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        index_min = i;

        for(uint8_t j = i; j < MAX_PLAYERS; ++j) {
            if(strcmp(state->players[j].name, state->players[index_min].name) < 0) {
                index_min = j;
            }
        }

        swap_client = state->players[i];
        state->players[i] = state->players[index_min];
        state->players[index_min] = swap_client;
    }
}


void enqueue_event(server_game_state_t *state, event_data_t *event) {
    uint32_t next_free = state->events_count;

    if(next_free >= state->events_queue_size - 2) {
        void *realloc_ptr = realloc(state->events_queue,
                                    2 * state->events_queue_size * sizeof(event_data_t));
        if(realloc_ptr == NULL) {
            perror("realloc");
            exit(1);
        }

        state->events_queue_size *= 2;
    }

    state->events_queue[next_free].x = event->x;
    state->events_queue[next_free].y = event->y;
    state->events_queue[next_free].event_type = event->event_type;
    state->events_queue[next_free].player_number = event->player_number;

    /* Increase the number of events */
    state->events_count++;
}


static
ssize_t serialize_event_record(server_game_state_t *state,
                               uint32_t event_no,
                               char *buffer,
                               ssize_t remaining_space) {

    event_data_t *data = &state->events_queue[event_no];

    uint32_t crc32;
    uint32_t event_fields_length;
    ssize_t record_size = 0;

    uint32_t conv_event_fields_length;
    uint32_t conv_event_no;
    uint32_t conv_x;
    uint32_t conv_y;
    uint32_t conv_crc32;

    if(data->event_type == EVENT_NEW_GAME) {
        /* Always as the first, no need to check
         * buffer space overflow
         */

        record_size = INTEGER_FIELDS_LEN_EVENT_RECORD_NEW_GAME;
        event_fields_length = EVENT_FIELDS_LENGTH_NEW_GAME_RAW;

        uint32_t space_for_player_name;

        size_t offset = 17;

        for(uint8_t i = 0; i < state->players_count; ++i) {
            space_for_player_name = strlen(state->game_primary_player_names[i]) + 1;

            memcpy(buffer + offset,
                   state->game_primary_player_names[i],
                   space_for_player_name);

            record_size += space_for_player_name;
            event_fields_length += space_for_player_name;
            offset += space_for_player_name;
        }

        conv_event_fields_length = htonl(event_fields_length);
        memcpy(buffer, &conv_event_fields_length, 4);

        conv_event_no = htonl(event_no);

        memcpy(buffer + 4, &conv_event_no, 4);
        memcpy(buffer + 8, &data->event_type, 1);

        conv_x = htonl(data->x);
        conv_y = htonl(data->y);

        memcpy(buffer + 9, &conv_x, 4);
        memcpy(buffer + 13, &conv_y, 4);

        crc32 = crc_32(buffer, 4 + event_fields_length);
        conv_crc32 = htonl(crc32);

        memcpy(buffer + offset, &conv_crc32, 4);
    }
    else if(data->event_type == EVENT_PIXEL) {
        if(EVENT_RECORD_LENGTH_PIXEL > remaining_space) {
            return -1;
        }

        event_fields_length = EVENT_FIELDS_LENGTH_PIXEL;
        event_fields_length = htonl(event_fields_length);

        memcpy(buffer, &event_fields_length, 4);

        conv_event_no = htonl(event_no);

        memcpy(buffer + 4, &conv_event_no, 4);
        memcpy(buffer + 8, &data->event_type, 1);
        memcpy(buffer + 9, &data->player_number, 1);

        conv_x = htonl(data->x);
        conv_y = htonl(data->y);

        memcpy(buffer + 10, &conv_x, 4);
        memcpy(buffer + 14, &conv_y, 4);

        crc32 = crc_32(buffer, 4 + EVENT_FIELDS_LENGTH_PIXEL);
        conv_crc32 = htonl(crc32);

        memcpy(buffer + 4 + EVENT_FIELDS_LENGTH_PIXEL, &conv_crc32, 4);

        record_size = EVENT_RECORD_LENGTH_PIXEL;
    }
    else if(data->event_type == EVENT_PLAYER_ELIMINATED) {
        if(EVENT_RECORD_LENGTH_PLAYER_ELIMINATED > remaining_space) {
            return -1;
        }

        event_fields_length = EVENT_FIELDS_LENGTH_PLAYER_ELIMINATED;
        event_fields_length = htonl(event_fields_length);

        memcpy(buffer, &event_fields_length, 4);

        conv_event_no = htonl(event_no);

        memcpy(buffer + 4, &conv_event_no, 4);
        memcpy(buffer + 8, &data->event_type, 1);
        memcpy(buffer + 9, &data->player_number, 1);

        crc32 = crc_32(buffer, 4 + EVENT_FIELDS_LENGTH_PLAYER_ELIMINATED);
        conv_crc32 = htonl(crc32);

        memcpy(buffer + 4 + EVENT_FIELDS_LENGTH_PLAYER_ELIMINATED, &conv_crc32, 4);

        record_size = EVENT_RECORD_LENGTH_PLAYER_ELIMINATED;
    }
    else {
        if(EVENT_RECORD_LENGTH_GAME_OVER > remaining_space) {
            return -1;
        }

        event_fields_length = EVENT_FIELDS_LENGTH_GAME_OVER;
        event_fields_length = htonl(event_fields_length);

        memcpy(buffer, &event_fields_length, 4);

        conv_event_no = htonl(event_no);

        memcpy(buffer + 4, &conv_event_no, 4);
        memcpy(buffer + 8, &data->event_type, 1);


        crc32 = crc_32(buffer, 4 + EVENT_FIELDS_LENGTH_GAME_OVER);
        conv_crc32 = htonl(crc32);

        memcpy(buffer + 4 + EVENT_FIELDS_LENGTH_GAME_OVER, &conv_crc32, 4);

        record_size = EVENT_RECORD_LENGTH_GAME_OVER;
    }

    return record_size;
}


void send_game_data(server_game_state_t  *state,
                    uint32_t since_event,
                    uint8_t client_no) {

    uint32_t first_not_sent = since_event;
    ssize_t ret_val;

    while(first_not_sent < state->events_count) {
        ssize_t datagram_size = pack_events(state,
                                            first_not_sent,
                                            &first_not_sent,
                                            MAX_SERVER_UDP_DGRAM_LENGTH);

        ret_val = sendto(state->server_socket,
                         state->server_buffer,
                         datagram_size,
                         0,
                         (struct sockaddr *) &state->players[client_no].conn.address,
                         state->players[client_no].conn.address_length);

        if(ret_val < 0 || ret_val != datagram_size) {
            perror("sendto");
        }
    }
}


void broadcast_events(server_game_state_t *state, uint32_t since_event) {
    uint32_t first_not_sent = since_event;
    ssize_t ret_val;

    while(first_not_sent < state->events_count) {
        ssize_t datagram_size = pack_events(state,
                                            first_not_sent,
                                            &first_not_sent,
                                            MAX_SERVER_UDP_DGRAM_LENGTH);

        for(uint8_t i = 0; i < MAX_PLAYERS; ++i) {
            if(state->players[i].conn.is_connection_active) {
                ret_val = sendto(state->server_socket,
                                 state->server_buffer,
                                 datagram_size,
                                 0,
                                 (struct sockaddr *) &(state->players[i].conn.address),
                                 state->players[i].conn.address_length);

                if(ret_val < 0 || ret_val != datagram_size) {
                    perror("sendto");
                }
            }
        }
    }
}


void initiate_game(server_game_state_t *state) {
    sort_players(state);

    /* Clear the game board before placing players on their initial positions */
    for(uint32_t i = 0; i < state->game_params.board_dimension_x; ++i) {
        for(uint32_t j = 0; j < state->game_params.board_dimension_y; ++j) {
            state->game_board[i][j] = false;
        }
    }

    uint8_t player_no = 0;

    state->players_count = state->ready_players;
    state->alive_players_count = state->players_count;

    state->events_count = 0;

    for(uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        state->alive[i] = true;

        /* Distribute player numbers among clients that are playing
         * in the game that is currently being initiated
         */
        if(state->players[i].is_playing) {
            state->players[i].player_number = player_no;

            memcpy(state->game_primary_player_names[player_no],
                   state->players[i].name,
                   MAX_PLAYER_NAME_LENGTH + 1);

            player_no++;
        }
    }

    state->game_id = generate_random(&state->random);

    event_data_t current_event;

    current_event.event_type = EVENT_NEW_GAME;
    current_event.x = state->game_params.board_dimension_x;
    current_event.y = state->game_params.board_dimension_y;

    enqueue_event(state, &current_event);

    for(uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if(state->players[i].is_playing) {

            state->players[i].x_pos = (double) (generate_random(&state->random) %
                                                state->game_params.board_dimension_x) +
                                                0.5;

            state->players[i].y_pos = (double) (generate_random(&state->random) %
                                                state->game_params.board_dimension_y) +
                                                0.5;

            state->players[i].direction = generate_random(&state->random) % 360;

            int32_t integer_coord_x = (int32_t) state->players[i].x_pos;
            int32_t integer_coord_y = (int32_t) state->players[i].y_pos;

            if(integer_coord_x < 0 || integer_coord_y < 0 ||
               (uint32_t) integer_coord_x >= state->game_params.board_dimension_x ||
               (uint32_t) integer_coord_y >= state->game_params.board_dimension_y ||
               state->game_board[integer_coord_x][integer_coord_y]) {

                current_event.event_type = EVENT_PLAYER_ELIMINATED;
                current_event.player_number = state->players[i].player_number;
            }
            else {
                /* Mark board field as occupied by the player */
                state->game_board[integer_coord_x][integer_coord_y] = true;

                current_event.event_type = EVENT_PIXEL;
                current_event.player_number = state->players[i].player_number;
                current_event.x = (uint32_t) integer_coord_x;
                current_event.y = (uint32_t) integer_coord_y;
            }

            enqueue_event(state, &current_event);
        }
    }

    state->game_status = GAME_STATE_GAME_STARTED;

    broadcast_events(state, 0);

    state->fds[1].fd = timerfd_create(CLOCK_MONOTONIC, 0);

    if(state->fds[1].fd < 0) {
        perror("timerfd_create");
    }

    timerfd_settime(state->fds[1].fd, 0, &state->round_params, NULL);
}


ssize_t pack_events(server_game_state_t *state,
                    uint32_t from_which,
                    uint32_t *first_not_packed,
                    ssize_t remaining_space) {

    /* Before any processing, append game id */
    uint32_t conv_game_id = htonl(state->game_id);
    memcpy(state->server_buffer, &conv_game_id, 4);

    ssize_t free_space = remaining_space - 4;
    ssize_t datagram_size = 4;

    ssize_t ret_val;
    uint32_t event_no;

    for(event_no = from_which; event_no < state->events_count; ++event_no) {
        ret_val = serialize_event_record(state,
                                         event_no,
                                         state->server_buffer + datagram_size,
                                         free_space);

        if(ret_val < 0) {
            break;
        }
        else {
            datagram_size += ret_val;
            free_space -= ret_val;
        }
    }

    *first_not_packed = event_no;
    return datagram_size;
}



void update_players_after_game(server_game_state_t *state) {
    state->ready_players = 0;
    state->players_count = 0;

    for(uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        state->players[i].ready = false;

        if(state->players[i].conn.is_connection_active) {
            if(strlen(state->players[i].name) > 0) {
                state->players_count++;

                state->players[i].is_playing = true;
                state->players[i].is_spectator = false;
            }
            else {
                state->players[i].is_playing = false;
                state->players[i].is_spectator = true;
            }
        }
    }
}