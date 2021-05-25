#ifndef CLIENT_PROTOCOL_H
#define CLIENT_PROTOCOL_H
#include <stdint.h>
#include "utils.h"


/* Exact summary size of integer variables stored in
 * client UDP datagram that is to be passed to the game
 * server every 30ms
 */
#define CLIENT_DGRAM_INTEGERS_LEN                        13


/* Maximum length (in bytes) of data included in client
 * datagram: equal to summary size of integers stored within
 * this datagram and player name (at most 20 characters)
 */
#define MAX_CLIENT_DGRAM_LENGTH                          33


/* Size of buffer for storing serialized (packed) form
 * of data from client datagram. Exact size of client
 * datagram sent to server is 33 so the buffer size
 * is set to 50 to make sure that no memory past
 * the buffer will be accessed
 */
#define CLIENT_DGRAM_BUFFER_SIZE                         50



/* Size of buffer for sending messages from
 * client to the GUI server (large enough to fit
 * every possible message that is to be sent)
 */
#define MSG_GUI_BUFFER_LENGTH                          1024
#define PARTIAL_MSG_BUFFER_LENGTH                        32


#define LENGTH_LEFT_KEY_DOWN                             14
#define LENGTH_LEFT_KEY_UP                               12
#define LENGTH_RIGHT_KEY_DOWN                            15
#define LENGTH_RIGHT_KEY_UP                              13


typedef struct client_dgram_t client_dgram_t;
typedef struct basic_event_data_t basic_event_data_t;
typedef struct client_game_state_t client_game_state_t;


/* Structure of client datagram
 */
struct client_dgram_t {
    uint64_t session_id;
    uint8_t turn_direction;
    uint32_t next_expected_event_no;
    char player_name[20];
};


struct basic_event_data_t {
    uint32_t x;
    uint32_t y;

    uint8_t event_type;
    uint8_t player_no;
    uint8_t ready_to_send;
};


struct client_game_state_t {
    uint32_t game_id;
    uint32_t next_expected;
    uint32_t board_dimension_x;
    uint32_t board_dimension_y;

    uint8_t players_count;
    uint8_t client_turn_direction;

    ssize_t partial_gui_msg_len;

    bool played_any;
    bool game_over;

    int server_socket;
    int gui_socket;

    size_t player_name_len;
    char player_name[20];

    bool is_alive[MAX_PLAYERS];
    char game_players[MAX_PLAYERS][MAX_PLAYER_NAME_LENGTH + 1];

    basic_event_data_t data_for_gui;
};


/* Prepares appropriate text message for GUI server according
 * to the obtained event data. Returns length (in bytes) of
 * created message for further purposes.
 */
size_t prepare_message(const client_game_state_t *, char *);


/* Initialises client game state with some default values where necessary
 */
void initialise_client_game_state(client_game_state_t *);


/* Serializes client datagram to char buffer so that it can be sent
 * as binary data through UDP socket to the game server
 */
void serialize_client_dgram(const client_dgram_t *, size_t, char *);


/* Deserializes client datagram from buffer so that its content
 * can be processed by the game server. Returns 0 on success
 * (if serialized data satisfied the required conditions) and -1
 * when data is considered bad (e.g. too long message, illegal
 * characters in player name etc.)
 */
ssize_t deserialize_client_dgram(client_dgram_t *, char *, ssize_t);


/* Deserializes event record passed from game server. Updates client game
 * state according to the data stored in record. Returns positive integer
 * indicating the offset by which the buffer position has to be advanced
 * after parsing the event data or negative integer indicating error: -1
 * if CRC_32 checksum is bad or -2 if CRC_32 checksum is correct but
 * event record contains nonsense values. The latter case leads to
 * termination of the client program
 */
ssize_t deserialize_event_record(client_game_state_t *, char *, ssize_t);


#endif /* CLIENT_PROTOCOL_H */
