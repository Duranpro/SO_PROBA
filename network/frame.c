#include "frame.h"

void frame_init(NetworkFrame *frame) {
    if (frame == NULL) {
        return;
    }

    memset(frame, 0, sizeof(*frame));
}

bool frame_set(NetworkFrame *frame, uint8_t type, const char *origin, const char *destination,
               const void *data, size_t data_length) {
    if (frame == NULL || origin == NULL || destination == NULL || data_length > CITADEL_FRAME_DATA_SIZE) {
        return false;
    }

    frame_init(frame);
    frame->type = type;
    strncpy(frame->origin, origin, CITADEL_FRAME_ORIGIN_SIZE);
    strncpy(frame->destination, destination, CITADEL_FRAME_DESTINATION_SIZE);

    if (data != NULL && data_length > 0) {
        memcpy(frame->data, data, data_length);
    }

    frame->data_length = (uint16_t) data_length;
    frame->checksum = frame_calculate_checksum(frame);
    return true;
}

void frame_serialize(const NetworkFrame *frame, unsigned char buffer[CITADEL_FRAME_SIZE]) {
    uint16_t net_value = 0;

    memset(buffer, 0, CITADEL_FRAME_SIZE);
    if (frame == NULL) {
        return;
    }

    buffer[0] = frame->type;
    memcpy(buffer + 1, frame->origin, strnlen(frame->origin, CITADEL_FRAME_ORIGIN_SIZE));
    memcpy(buffer + 21, frame->destination, strnlen(frame->destination, CITADEL_FRAME_DESTINATION_SIZE));

    net_value = htons(frame->data_length);
    memcpy(buffer + 41, &net_value, sizeof(net_value));
    memcpy(buffer + 43, frame->data, frame->data_length);

    net_value = htons(frame->checksum);
    memcpy(buffer + 318, &net_value, sizeof(net_value));
}

bool frame_deserialize(const unsigned char buffer[CITADEL_FRAME_SIZE], NetworkFrame *frame) {
    uint16_t net_value = 0;

    if (buffer == NULL || frame == NULL) {
        return false;
    }

    frame_init(frame);
    frame->type = buffer[0];
    memcpy(frame->origin, buffer + 1, CITADEL_FRAME_ORIGIN_SIZE);
    memcpy(frame->destination, buffer + 21, CITADEL_FRAME_DESTINATION_SIZE);
    frame->origin[CITADEL_FRAME_ORIGIN_SIZE] = '\0';
    frame->destination[CITADEL_FRAME_DESTINATION_SIZE] = '\0';

    memcpy(&net_value, buffer + 41, sizeof(net_value));
    frame->data_length = ntohs(net_value);
    if (frame->data_length > CITADEL_FRAME_DATA_SIZE) {
        return false;
    }

    memcpy(frame->data, buffer + 43, frame->data_length);

    memcpy(&net_value, buffer + 318, sizeof(net_value));
    frame->checksum = ntohs(net_value);
    return true;
}

uint16_t frame_calculate_checksum(const NetworkFrame *frame) {
    unsigned char buffer[CITADEL_FRAME_SIZE];
    uint32_t total = 0;
    size_t i = 0;

    if (frame == NULL) {
        return 0;
    }

    frame_serialize(frame, buffer);
    buffer[318] = 0;
    buffer[319] = 0;

    for (i = 0; i < 318; ++i) {
        total += buffer[i];
    }

    return (uint16_t) (total % 65536U);
}

bool frame_validate_checksum(const NetworkFrame *frame) {
    if (frame == NULL) {
        return false;
    }

    return frame->checksum == frame_calculate_checksum(frame);
}

char *frame_data_to_text(const NetworkFrame *frame) {
    char *text = NULL;

    if (frame == NULL) {
        return NULL;
    }

    text = (char *) malloc((size_t) frame->data_length + 1);
    if (text == NULL) {
        return NULL;
    }

    memcpy(text, frame->data, frame->data_length);
    text[frame->data_length] = '\0';
    return text;
}
