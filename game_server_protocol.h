#ifndef GAME_SERVER_PROTOCOL_H
#define GAME_SERVER_PROTOCOL_H

#include <arpa/inet.h>
#include <stdint.h>
#include <sys/timerfd.h>
#include <sys/poll.h>
#include "utils.h"


/* Minimal length of single UDP datagram
 * that (working fine) server should send
 */
#define MIN_SERVER_UDP_DGRAM_LENGTH                      17


/* Maximum length of single UDP datagram
 * that can be sent from the game server
 */
#define MAX_SERVER_UDP_DGRAM_LENGTH                     550


#define INTEGER_FIELDS_LEN_EVENT_RECORD_NEW_GAME         21
#define EVENT_FIELDS_LENGTH_NEW_GAME_RAW                 13


/* Constants representing length (in bytes) of
 * event records that correspond to specified
 * event_type
 */
#define EVENT_RECORD_LENGTH_PIXEL                        22
#define EVENT_RECORD_LENGTH_PLAYER_ELIMINATED            14
#define EVENT_RECORD_LENGTH_GAME_OVER                    13


/* Constants representing the summary length (in bytes)
 * of fields labelled with event_ prefix that are sent from
 * server for events which type is either EVENT_PIXEL or
 * EVENT_PLAYER_ELIMINATED or EVENT_GAME_OVER
 */
#define EVENT_FIELDS_LENGTH_PIXEL                        14
#define EVENT_FIELDS_LENGTH_PLAYER_ELIMINATED             6
#define EVENT_FIELDS_LENGTH_GAME_OVER                     5


/* Constant representing the minimal possible length
 * (in bytes) of event record
 */
#define MINIMAL_EVENT_RECORD_LENGTH                      13


/* Constants representing the types of event specified
 * in the task. Values are mapped according to the
 * specification
 */
#define EVENT_NEW_GAME                                    0
#define EVENT_PIXEL                                       1
#define EVENT_PLAYER_ELIMINATED                           2
#define EVENT_GAME_OVER                                   3


#define EVENT_DATA_BYTE_OFFSET                            9


/* Default size of queue for storing events that have
 * occurred since the game beginning
 */
#define DEFAULT_EVENTS_QUEUE_SIZE                      4096


/* Constants representing the states of the game
 */
#define GAME_STATE_GAME_STARTED                           1
#define GAME_STATE_WAITING_FOR_PLAYERS                    2


#define SERVER_POLL_DESCRIPTORS_COUNT                    27


typedef struct connection_data_t connection_data_t;
typedef struct client_t client_t;
typedef struct event_data_t event_data_t;
typedef struct seed_status_t seed_status_t;
typedef struct game_params_t game_params_t;
typedef struct server_game_state_t server_game_state_t;


struct connection_data_t {
    /* Id of the client session associated with
     * the connection
     */
    uint64_t session_id;

    /* Flag which indicates whether connection is active
     */
    bool is_connection_active;

    /* IPv6 address
     */
    struct sockaddr_in6 address;

    /* Length of address
     */
    socklen_t address_length;
};


struct client_t {
    /* Status of client's connection (address, length of address,
     * session_id and flag which indicates whether connection is
     * active - or equivalently - flag which indicates whether client
     * has not timeouted
     */
    connection_data_t conn;

    /* Direction of client's worm
     */
    int32_t direction;

    /* Player number that has been assigned to the client
     * in current game (if one is being played at the moment)
     */
    uint8_t player_number;

    /* Player's turn direction - default 0, can change according
     * to the data sent in datagram from client
     */
    uint8_t turn_direction;

    /* Char array which stores player name. Is updated on every
     * connection to current client slot. In case when client which
     * played from the beginning of the game has timeouted, the name
     * is stored in game_primary_player_names array in server_game_state_t
     * structure so that possible changes of the client name does not affect
     * the correctness of data included in EVENT_NEW_GAME messages sent
     * to the server clients if such are requested
     */
    char name[MAX_PLAYER_NAME_LENGTH + 1];

    /* Double-precision floating point number which denotes
     * the horizontal position of client's worm
     */
    double x_pos;

    /* Double precision floating point number which denotes
     * the vertical position of client's worm
     */
    double y_pos;

    /* Boolean flag which indicates whether player (if permitted to play,
     * that is he/she has a name of positive length) has been marked as ready
     * while waiting for players before new game (sending turn_direction != 0)
     */
    bool ready;

    /* Flag indicating whether client takes part in the game
     * (attention: client can be a spectator and this flag can be set
     * to true in case when playing client timeouted and another one connected
     * in his place. In such case is_spectator flag is set to true so as to
     * ignore datagrams incoming from this client and remain the possibility
     * of 'non-controlled' moves being done by the player
     */
    bool is_playing;

    /* Flag which indicates whether the player is a spectator of the game
     */
    bool is_spectator;

    /* Flag indicating whether player has sent any
     * datagram in last 2 seconds (value of this flag
     * indicated whether client should be disconnected)
     */
    bool message;
};


struct event_data_t {
    /* only in EVENT_PIXEL and EVENT_NEW_GAME) denotes
     * the x-coordinate of player that made the move or x-dimension
     * of the game board, respectively
     */
    uint32_t x;

    /* only in EVENT_PIXEL and EVENT_NEW_GAME) - denotes
     * the y-coordinate of player that made the move or y-dimension
     * of the game board, respectively
     */
    uint32_t y;

    /* EVENT_NEW_GAME or EVENT_PIXEL or EVENT_PLAYER_ELIMINATED
     * or EVENT_GAME_OVER - denotes the type of event
     */
    uint8_t event_type;

    /* only in EVENT_PIXEL and EVENT_PLAYER_ELIMINATED - denotes
     * the number of player which moved or was eliminated,
     * respectively
     */
    uint8_t player_number;
};


struct seed_status_t {
    /* Current value of seed
     */
    uint32_t seed;

    /* Ordinal number of seed, used for computations
     * (when seed_no == 0) the seed field is returned,
     * in other cases it is multiplied by SEED_MULTIPLIER
     * and then calculated modulo against SEED_MODULUS
     */
    uint32_t seed_no;
};


struct game_params_t {
    /* Turning speed that has been defined for the game
     */
    uint8_t turning_speed;

    /* Number of rounds per seconds that has been defined
     * for the game
     */
    uint32_t rounds_per_sec;

    /* Board width that has been defined for the game
     */
    uint32_t board_dimension_x;

    /* Board height that has been defined for the game
     */
    uint32_t board_dimension_y;
};


struct server_game_state_t {
    /* Descriptor of server UDP socket which handles
     * incoming connections from the clients
     */
    int32_t server_socket;

    /* Id of the currently played game
     */
    uint32_t game_id;

    /* Number of events that have occurred since the
     * game started
     */
    uint32_t events_count;

    /* Number of players that have been marked as ready (they have sent
     * a datagram containing turn_direction distinct from 0)
     */
    uint8_t ready_players;

    /* Describes the game status. Possible values (predefined)
     * are: GAME_STATE_WAITING_FOR_PLAYERS - before the first game
     * after launching the server or after the last game has finished.
     * Indicated that the server is waiting for players to connect /
     * send turn_direction != 0 as in the task specification
     */
    uint8_t game_status;

    /* Number of players that are currently connected to the game server
     */
    uint8_t connected_players;

    /* Array storing the data about clients (address, state: is
     * player a spectator or regular player, is player connected, etc.)
     */
    client_t players[MAX_PLAYERS];


    /* Buffer for sending UDP datagrams from server (size enough to fit
     * the largest datagram that server might ever have to send) and
     * receiving periodic datagrams send to the server by game clients
     */
    char server_buffer[MAX_SERVER_UDP_DGRAM_LENGTH];

    /* Parameters describing the game status at start (initial number of players)
     * and their original names - they are stored here since the ones in client_t
     * structures may vary depending on whether the client timeouts and some
     * other client takes his place on the server
     */
    uint8_t players_count;

    /* Array which stores 'primary' names of players in sense that these names
     * does not change regardless of the possible timeouts of clients that are
     * marked as the ones taking part in the current game. This allows us to
     * change 'name' field in client_t structure after another client takes
     * some other timeouted client's (which was playing in the current game)
     * slot so that when query for event with no = 0 is sent, the proper
     * list of players that were marked as playing at the beginning of the
     * game will be sent.
     */
    char game_primary_player_names[MAX_PLAYERS][MAX_PLAYER_NAME_LENGTH + 1];

    /* Parameters describing the game state concerned with number of alive players
     * and exact state of which player (alive / dead)
     */
    bool alive[MAX_PLAYERS];
    uint8_t alive_players_count;

    /* Game params consisting of turning speed, number of rounds per second,
     * board width and height
     */
    game_params_t game_params;

    /* Two-dimension board of booleans which keeps track of fields that have
     * already been eaten / are being eaten / are free
     */
    bool **game_board;

    /* Dynamic array which stores game history (all events of the game since its
     * beginning). When the size is not enough to fit a new event, its size is
     * being increased and the memory is reallocated to twice as big size as
     * before reallocation
     */
    event_data_t *events_queue;
    uint32_t events_queue_size;

    /* Address and integer variable which are for handling incoming data
     * from UDP server sockets. Moreover they are used for identification
     * of the clients / detecting new connections
     */
    struct sockaddr_in6 receive_address;
    socklen_t receive_address_length;

    /* Structure containing data about timeout interval. Used when new player
     * to set timerfd for him (handles client 2s-timeouts)
     */
    struct itimerspec timeout_params;

    /* Structure containing data about round time interval. Used each time a new
     * game is initiated to set up timers which are notifying server poll about
     * when new round should be proceeded
     */
    struct itimerspec round_params;

    /* Poll descriptors on which server listens for incoming POLLIN events from
     * either server socket or timeout descriptors for each connected client
     * or round time descriptor which notifies the poll each time a new
     * round should be conducted
     */
    struct pollfd fds[SERVER_POLL_DESCRIPTORS_COUNT];

    /* Struct used for generating random values according to the task
     * specification
     */
    seed_status_t random;
};


/* Function which generates random integer values according to the task
 * specification. Takes as parameter a pointer to seed_status_t structure
 * that is a component of server_game_state_t structure
 */
uint32_t generate_random(seed_status_t *);


/* Sorts players lexicographically (using mere strcmp). Executed
 * before game commencing
 */
void sort_players(server_game_state_t *);


/* Appends the event to the queue located in the server_game_state_t
 * structure to which the first argument points. Handles queue
 * resizing if necessary. On memory error (realloc returns NULL)
 * the program is terminated
 */
void enqueue_event(server_game_state_t *, event_data_t *);


/* Broadcasts all events since specified event_no to the players
 * that are connected to the server
 */
void broadcast_events(server_game_state_t *, uint32_t);


/* Sends the game data (history of events since the number with
 * specified number, passed as the second argument) to the client
 * with id which is passed as the third argument.
 */
void send_game_data(server_game_state_t *, uint32_t, uint8_t);


/* Updates player statuses after the game has been finished. Some
 * fields are set to default values and some are changed depending
 * on the client state at the end of the game (including change of
 * the state of players with non-empty name which were spectating the
 * current gameplay as they connected after the game had started)
 */
void update_players_after_game(server_game_state_t *);


/* Initiates the game. Handles restoring the default data (marks
 * each game board field as free etc.). Executed only when each of
 * connected players which has non-empty nickname is marked as ready
 */
void initiate_game(server_game_state_t *);


/* Function which handles packing events data to the buffer. Tries
 * to pack as many events from event_no (second_argument) as possible
 * Moreover, game_id is prepended before the data. Return value is the
 * total sum of bytes packed into the server buffer, further used
 * in sendto function as length of datagram. Function modified the integer
 * to which the third pointer argument points in such way that the value
 * of integer value after function execution is number of the first event
 * that has not been packed to the buffer.
 */
ssize_t pack_events(server_game_state_t *, uint32_t, uint32_t *, ssize_t);


#endif /* GAME_SERVER_PROTOCOL_H */
