#ifndef UTILS_H
#define UTILS_H


#include <stdbool.h>
#include <stdint.h>
#include <arpa/inet.h>


#define MAX_PLAYERS                                      25
#define MAX_PLAYER_NAME_LENGTH                           20


#define DEFAULT_TURNING_SPEED                             6
#define DEFAULT_ROUNDS_PER_SEC                           50
#define DEFAULT_BOARD_WIDTH                             640
#define DEFAULT_BOARD_HEIGHT                            480


#define MAX_X_SIZE                                     1920
#define MAX_Y_SIZE                                     1440
#define MAX_TURNING_SPEED                                90
#define MAX_ROUNDS_PER_SEC                              100


#define PLAYER_NAME_MINIMAL_ASCII                        33
#define PLAYER_NAME_MAXIMAL_ASCII                       126


#define SEED_MULTIPLIER                           279410273
#define SEED_MODULUS                             4294967291


#define MILLIS_TO_NANO_MULTIPLIER                   1000000


#define UTILS_PI                                 3.14159265


bool check_player_name(char *);


bool check_integer(char *);


bool check_player_name_character(char);


bool check_player_in_message(char *, size_t);


int equal_addresses(struct sockaddr_in6 *, struct sockaddr_in6 *);


uint32_t crc_32(char *, size_t);


size_t digits_count(uint32_t);


#endif /* UTILS_H */
