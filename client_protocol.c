#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <endian.h>
#include "game_server_protocol.h"
#include "client_protocol.h"
#include "utils.h"


/* Function which parses player names that are included
 * in NEW_GAME event data from game server. Returns true
 * if and only if values passed by game server are correct
 * in sense of characters and basic logic, that is each name
 * consists only of allowed characters, its length does not
 * exceed 20 and the name is followed by ASCII NUL character.
 */
static
bool parse_player_names(char *buffer,
                        uint32_t remaining,
                        client_game_state_t *state) {
    uint8_t names_count = 0;
    uint32_t current_name_len = 0;
    uint32_t offset = 0;

    while(offset < remaining) {
        if(buffer[offset] >= PLAYER_NAME_MINIMAL_ASCII &&
           buffer[offset] <= PLAYER_NAME_MAXIMAL_ASCII) {
            current_name_len++;
        }
        else if(buffer[offset] == 0) {
            /* Empty player name -> nonsense value */
            if(current_name_len == 0) {
                return false;
            }
            else {
                for(uint32_t i = 0; i < current_name_len; ++i) {
                    state->game_players[names_count][i] = buffer[offset - current_name_len + i];
                }

                state->players_count++;
                names_count++;
                current_name_len = 0;
            }
        }
        else {
            /* Illegal character in sequence */
            return false;
        }

        /* Nonsense value (illegal player name that
         * violates constraint imposed upon length (max: 20)
         */
        if(current_name_len > 20) {
            return false;
        }

        offset++;
    }

    /* Return true iff (after above checking)
     * values make sense i.e. at least 2 player names
     * were included in message and EACH of these
     * included names was followed by ASCII NUL character
     * (checked by condition current_name_len == 0)
     */
    return (current_name_len == 0);
}


static
bool check_names_order(client_game_state_t *state) {
    for(uint8_t i = 0; i < state->players_count - 1; ++i) {
        if(strcmp(state->game_players[i], state->game_players[i+1]) >= 0) {
            return false;
        }
    }
    return true;
}


/* Injects string representation of integers passed as x and
 * y arguments to the function in such way that between string
 * representations of these digits exactly one space occurs.
 * Function returns the number of characters appended to the buffer
 */
static
size_t inject_two_separated_numbers(char *buffer,
                                    size_t x,
                                    size_t digits_x,
                                    size_t y,
                                    size_t digits_y) {
    size_t generated_offset = 0;

    snprintf(buffer,
             digits_x + 1,
             "%lu",
             x);

    generated_offset += digits_x;
    buffer[generated_offset] = ' ';
    generated_offset++;

    snprintf(buffer + generated_offset,
             digits_y + 1,
             "%lu",
             y);

    generated_offset += digits_y;
    buffer[generated_offset] = ' ';
    generated_offset++;

    return generated_offset;
}


/* Prepares message in buffer according to the data_for_gui structure
 * held in client_game_state_t structure to which passed pointer
 * @p state points. Returns the length of created message to that it
 * can be sent to the GUI server later.
 */
size_t prepare_message(const client_game_state_t *state, char *buffer) {
    uint8_t type = state->data_for_gui.event_type;
    size_t buffer_offset = 0;

    if(type == EVENT_NEW_GAME) {
        uint32_t dim_x = state->data_for_gui.x;
        uint32_t dim_y = state->data_for_gui.y;

        size_t digits_dim_x = digits_count(dim_x);
        size_t digits_dim_y = digits_count(dim_y);

        memcpy(buffer, "NEW_GAME ", 9);
        buffer_offset = 9;

        buffer_offset += inject_two_separated_numbers(buffer + buffer_offset,
                                                      dim_x,
                                                      digits_dim_x,
                                                      dim_y,
                                                      digits_dim_y);

        size_t len;

        /* Player names within client_game_state_t structure to which
         * pointer passed to function points are sorted
         * alphabetically so simple loop will do
         */
        for(uint8_t i = 0; i < state->players_count; ++i) {
            len = strlen(state->game_players[i]);

            snprintf(buffer + buffer_offset,
                    len + 1,
                    "%s",
                    state->game_players[i]);
            buffer_offset += len;

            if(i < state->players_count - 1) {
                buffer[buffer_offset] = ' ';
                buffer_offset++;
            }
        }

        buffer[buffer_offset] = '\n';
        buffer_offset++;

        return buffer_offset;
    }
    else if(type == EVENT_PIXEL) {
        uint32_t coord_x = state->data_for_gui.x;
        uint32_t coord_y = state->data_for_gui.y;

        size_t digits_coord_x = digits_count(coord_x);
        size_t digits_coord_y = digits_count(coord_y);

        uint8_t player_no = state->data_for_gui.player_no;
        size_t name_length = strlen(state->game_players[player_no]);

        memcpy(buffer, "PIXEL ", 6);
        buffer_offset = 6;

        buffer_offset += inject_two_separated_numbers(buffer + buffer_offset,
                                                      coord_x,
                                                      digits_coord_x,
                                                      coord_y,
                                                      digits_coord_y);

        snprintf(buffer + buffer_offset,
                 name_length + 1,
                 "%s",
                 state->game_players[player_no]);

        buffer_offset += name_length;
        buffer[buffer_offset] = '\n';
        buffer_offset++;

        return buffer_offset;
    }
    else if(type == EVENT_PLAYER_ELIMINATED) {
        uint8_t player_no = state->data_for_gui.player_no;
        size_t name_len = strlen(state->game_players[player_no]);

        memcpy(buffer, "PLAYER_ELIMINATED ", 18);
        buffer_offset = 18;

        snprintf(buffer + buffer_offset,
                 name_len + 1,
                 "%s",
                 state->game_players[player_no]);

        buffer_offset += name_len;
        buffer[buffer_offset] = '\n';
        buffer_offset++;

        return buffer_offset;
    }
    else {
        return 0;
    }
}


void initialise_client_game_state(client_game_state_t *state) {
    state->client_turn_direction = 0;
    state->next_expected = 0;
    state->players_count = 0;
    state->game_over = false;
    state->partial_gui_msg_len = 0;

    for(size_t i = 0; i < MAX_PLAYERS; ++i) {
        state->is_alive[i] = true;
    }

    memset(state->game_players, 0, sizeof(state->game_players));

    state->data_for_gui.ready_to_send = 0;
}


ssize_t deserialize_client_dgram(client_dgram_t *datagram,
                                 char *buffer,
                                 ssize_t message_size) {
    /* Bad client datagram length */
    if(message_size > MAX_CLIENT_DGRAM_LENGTH ||
       message_size < CLIENT_DGRAM_INTEGERS_LEN) {

        return -1;
    }

    uint64_t received_session_id = be64toh(*(uint64_t *) buffer);
    uint8_t received_turn_direction = *(uint8_t *) (buffer + 8);
    uint32_t received_expected_event_no = ntohl(*(uint32_t *) (buffer + 9));

    /* Bad turn direction, corresponding to none of proper values */
    if(received_turn_direction != 0 && received_turn_direction != 1 &&
       received_turn_direction != 2) {
        return -1;
    }

    datagram->session_id = received_session_id;
    datagram->turn_direction = received_turn_direction;
    datagram->next_expected_event_no = received_expected_event_no;

    for(ssize_t i = CLIENT_DGRAM_INTEGERS_LEN; i < message_size; ++i) {
        /* Illegal characters in player name */
        if(buffer[i] < PLAYER_NAME_MINIMAL_ASCII ||
           buffer[i] > PLAYER_NAME_MAXIMAL_ASCII) {

            return -1;
        }

        datagram->player_name[i - CLIENT_DGRAM_INTEGERS_LEN] = buffer[i];
    }

    return 0;
}


void serialize_client_dgram(const client_dgram_t *datagram,
                            size_t player_name_len,
                            char *buffer) {

    uint64_t n_session_id = htobe64(datagram->session_id);
    uint8_t n_turn_direction = datagram->turn_direction;
    uint32_t n_next_expected_event_no = htonl(datagram->next_expected_event_no);

    memcpy(buffer, &n_session_id, 8);
    memcpy(buffer + 8, &n_turn_direction, 1);
    memcpy(buffer + 9, &n_next_expected_event_no, 4);
    memcpy(buffer + 13, datagram->player_name, player_name_len);
}


ssize_t deserialize_event_record(client_game_state_t *state,
                                 char *buffer,
                                 ssize_t remaining) {
    /* Some VERY strange data, i don't know if anything other
     * than terminating the client should be done in such case
     * (correctness of CRC_32 value can't be checked since no offset
     * from which we can get this value can be determined)
     */
    if(remaining < MINIMAL_EVENT_RECORD_LENGTH) {
        return -2;
    }

    uint32_t event_fields_len = ntohl(*(uint32_t *) buffer);
    uint32_t event_no = ntohl(*(uint32_t *) (buffer + 4));
    uint8_t event_type = *(uint8_t *) (buffer + 8);

    ssize_t event_record_size = (ssize_t) event_fields_len + 8;

    /* Not enough space to fit the data consisting of
     * event_ fields and CRC_32 checksum value -> terminate
     * the client (no crc_32 verification can be done so
     * behaviour shall be undefined)
     */
    if(event_record_size > remaining) {
        return -2;
    }

    uint32_t crc_value = ntohl(*(uint32_t *) (buffer + 4 + event_fields_len));

    if(crc_32(buffer, event_fields_len + 4) != crc_value) {
        /* Incorrect CRC_32 checksum */
        return -1;
    }

    /* From now on each anomaly (in accordance to task specification)
     * results in terminating the client. I distinguish following types of them:
     * 1) Too few players in NEW_GAME event data content (less than 2 player names)
     * 2) player name in NEW_GAME containing illegal character
     * 3) player name in NEW_GAME event which is either too short (0) or too long (>20)
     * 4) player_name in NEW_GAME which is not followed by ASCII NUL
     * 5) Bad order of player names in NEW_GAME event_data content (names are not
     * sorted alphabetically contrary to specification which states that names should
     * be in alphabetical order)
     * 5) x in PIXEL which is greater or equal to board X dimension (sending such value
     * to GUI server would probably end with it being terminated due to SIGSEGV)
     * 6) y in PIXEL which is greater or equal to board Y dimension (sending such value
     * to the GUI server would probably end with it being terminated due to SIGSEGV)
     * 7) Incorrect player_number in PIXEL or PLAYER_ELIMINATED (>= players count)
     * 8) Incorrect length of event record that is always of fixed length
     * (PIXEL, PLAYER_ELIMINATED and GAME_OVER)
     * 9) Incorrect player_number in PLAYER_ELIMINATED, corresponding to the player
     * which has already been eliminated from the game
     */

    ssize_t event_data_size = event_fields_len - 5;

    if(event_type == EVENT_NEW_GAME) {
        uint32_t dimension_x = ntohl(*(uint32_t *) (buffer + 9));
        uint32_t dimension_y = ntohl(*(uint32_t *) (buffer + 13));

        if(!parse_player_names(buffer + EVENT_DATA_BYTE_OFFSET + 8,
                               event_data_size - 8,
                               state)) {
            /* Nonsense values */
            return -2;
        }

        if(!check_names_order(state)) {
            /* Nonsense values */
            return -2;
        }

        if(event_no > 0) {
            /* Nonsense value */
            return -2;
        }

        state->board_dimension_x = dimension_x;
        state->board_dimension_y = dimension_y;

        if(event_no == state->next_expected) {
            state->data_for_gui.event_type = EVENT_NEW_GAME;
            state->data_for_gui.x = dimension_x;
            state->data_for_gui.y = dimension_y;
            state->data_for_gui.ready_to_send = 1;

            state->next_expected++;
        }
    }
    else if(event_type == EVENT_PIXEL) {
        if(event_record_size != EVENT_RECORD_LENGTH_PIXEL) {
            /* Nonsense value */
            return -2;
        }

        uint8_t player_no = *(uint8_t *) (buffer + EVENT_DATA_BYTE_OFFSET);
        uint32_t coordinate_x = ntohl(*(uint32_t *) (buffer + EVENT_DATA_BYTE_OFFSET + 1));
        uint32_t coordinate_y = ntohl(*(uint32_t *) (buffer + EVENT_DATA_BYTE_OFFSET + 5));

        if(coordinate_x >= state->board_dimension_x ||
           coordinate_y >= state->board_dimension_y) {
            /* Nonsense value */
            return -2;
        }

        if(player_no >= state->players_count) {
            /* Nonsense value */
            return -2;
        }

        if(event_no == state->next_expected) {
            state->data_for_gui.event_type = EVENT_PIXEL;
            state->data_for_gui.player_no = player_no;
            state->data_for_gui.x = coordinate_x;
            state->data_for_gui.y = coordinate_y;
            state->data_for_gui.ready_to_send = 1;

            state->next_expected++;
        }
    }
    else if(event_type == EVENT_PLAYER_ELIMINATED) {
        if(event_record_size != EVENT_RECORD_LENGTH_PLAYER_ELIMINATED) {
            /* Nonsense value */
            return -2;
        }

        uint8_t player_no = *(uint8_t *) (buffer + 9);

        if(player_no >= state->players_count) {
            /* Nonsense value */
            return -2;
        }

        if(event_no == state->next_expected) {
            if(!state->is_alive[player_no]) {
                /* Nonsense value */
                return -2;
            }

            state->is_alive[player_no] = false;

            state->data_for_gui.event_type = EVENT_PLAYER_ELIMINATED;
            state->data_for_gui.player_no = player_no;
            state->data_for_gui.ready_to_send = 1;

            state->next_expected++;
        }
    }
    else if(event_type == EVENT_GAME_OVER) {
        if(event_record_size != EVENT_RECORD_LENGTH_GAME_OVER) {
            /* Nonsense value */
            return -2;
        }

        if(event_no == state->next_expected) {
            state->game_over = true;
            state->next_expected++;
        }
    }

    return event_record_size;
}