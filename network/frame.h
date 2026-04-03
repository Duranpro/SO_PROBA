#ifndef FRAME_H
#define FRAME_H

#include "../utils/system.h"

#define CITADEL_FRAME_SIZE 320
#define CITADEL_FRAME_ORIGIN_SIZE 20
#define CITADEL_FRAME_DESTINATION_SIZE 20
#define CITADEL_FRAME_DATA_SIZE 275

#define FRAME_TYPE_PLEDGE 0x01
#define FRAME_TYPE_SIGIL_DATA 0x02
#define FRAME_TYPE_PLEDGE_RESPONSE 0x03
#define FRAME_TYPE_PRODUCTS_REQUEST 0x11
#define FRAME_TYPE_PRODUCTS_RESPONSE 0x12
#define FRAME_TYPE_PRODUCTS_DATA 0x13
#define FRAME_TYPE_TRADE_HEADER 0x14
#define FRAME_TYPE_TRADE_DATA 0x15
#define FRAME_TYPE_TRADE_RESPONSE 0x16
#define FRAME_TYPE_UNKNOWN_REALM 0x21
#define FRAME_TYPE_AUTH_ERROR 0x25
#define FRAME_TYPE_PING 0x26
#define FRAME_TYPE_DISCONNECT 0x27
#define FRAME_TYPE_ACK 0x31
#define FRAME_TYPE_MD5_ACK 0x32
#define FRAME_TYPE_NACK 0x69

typedef struct {
    uint8_t type;
    char origin[CITADEL_FRAME_ORIGIN_SIZE + 1];
    char destination[CITADEL_FRAME_DESTINATION_SIZE + 1];
    uint16_t data_length;
    unsigned char data[CITADEL_FRAME_DATA_SIZE];
    uint16_t checksum;
} NetworkFrame;

void frame_init(NetworkFrame *frame);
bool frame_set(NetworkFrame *frame, uint8_t type, const char *origin, const char *destination,
               const void *data, size_t data_length);
void frame_serialize(const NetworkFrame *frame, unsigned char buffer[CITADEL_FRAME_SIZE]);
bool frame_deserialize(const unsigned char buffer[CITADEL_FRAME_SIZE], NetworkFrame *frame);
uint16_t frame_calculate_checksum(const NetworkFrame *frame);
bool frame_validate_checksum(const NetworkFrame *frame);
char *frame_data_to_text(const NetworkFrame *frame);

#endif
