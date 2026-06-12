#include "frame.h"

void frame_init(NetworkFrame *frame) {
    if (frame == NULL) {
        return;
    }

    memset(frame, 0, sizeof(*frame));
}

bool frame_set(NetworkFrame *frame, uint8_t tipus, const char *origen, const char *destination, const void *data, size_t mida_data) {
    if (frame == NULL || origen == NULL || destination == NULL || mida_data > CITADEL_FRAME_DATA_SIZE) {
        return false;
    }

    frame_init(frame);
    frame->tipus = tipus;
    strncpy(frame->origen, origen, CITADEL_FRAME_ORIGIN_SIZE);
    strncpy(frame->destination, destination, CITADEL_FRAME_DESTINATION_SIZE);

    if (data != NULL && mida_data > 0) {
        memcpy(frame->data, data, mida_data);
    }

    frame->mida_data = (uint16_t) mida_data;
    frame->checksum = frame_calcular_checksum(frame);
    return true;
}

void frame_serialize(const NetworkFrame *frame, unsigned char buffer[CITADEL_FRAME_SIZE]) {
    uint16_t net_value = 0;

    memset(buffer, 0, CITADEL_FRAME_SIZE);
    if (frame == NULL) {
        return;
    }

    buffer[0] = frame->tipus;
    memcpy(buffer + 1, frame->origen, strnlen(frame->origen, CITADEL_FRAME_ORIGIN_SIZE));
    memcpy(buffer + 21, frame->destination, strnlen(frame->destination, CITADEL_FRAME_DESTINATION_SIZE));

    net_value = htons(frame->mida_data);
    memcpy(buffer + 41, &net_value, sizeof(net_value));
    memcpy(buffer + 43, frame->data, frame->mida_data);

    net_value = htons(frame->checksum);
    memcpy(buffer + 318, &net_value, sizeof(net_value));
}

bool frame_deserialize(const unsigned char buffer[CITADEL_FRAME_SIZE], NetworkFrame *frame) {
    uint16_t net_value = 0;

    if (buffer == NULL || frame == NULL) {
        return false;
    }

    frame_init(frame);
    frame->tipus = buffer[0];
    memcpy(frame->origen, buffer + 1, CITADEL_FRAME_ORIGIN_SIZE);
    memcpy(frame->destination, buffer + 21, CITADEL_FRAME_DESTINATION_SIZE);
    frame->origen[CITADEL_FRAME_ORIGIN_SIZE] = '\0';
    frame->destination[CITADEL_FRAME_DESTINATION_SIZE] = '\0';

    memcpy(&net_value, buffer + 41, sizeof(net_value));
    frame->mida_data = ntohs(net_value);
    if (frame->mida_data > CITADEL_FRAME_DATA_SIZE) {
        return false;
    }

    memcpy(frame->data, buffer + 43, frame->mida_data);

    memcpy(&net_value, buffer + 318, sizeof(net_value));
    frame->checksum = ntohs(net_value);
    return true;
}

uint16_t frame_calcular_checksum(const NetworkFrame *frame) {
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

bool frame_validar_checksum(const NetworkFrame *frame) {
    if (frame == NULL) {
        return false;
    }

    return frame->checksum == frame_calcular_checksum(frame);
}

char *frame_data_to_text(const NetworkFrame *frame) {
    char *text = NULL;

    if (frame == NULL) {
        return NULL;
    }

    text = (char *) malloc((size_t) frame->mida_data + 1);
    if (text == NULL) {
        return NULL;
    }

    memcpy(text, frame->data, frame->mida_data);
    text[frame->mida_data] = '\0';
    return text;
}
