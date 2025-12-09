#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t *buffer;
    uint16_t head;
    uint16_t tail;
    uint16_t capacity;
    bool full;
} ring_buffer_t;

void ring_buffer_init(ring_buffer_t *rb, uint8_t *buffer, uint16_t capacity);
bool ring_buffer_write(ring_buffer_t *rb, uint8_t data);
bool ring_buffer_read(ring_buffer_t *rb, uint8_t *data);
uint16_t ring_buffer_count(ring_buffer_t *rb);
bool ring_buffer_is_empty(ring_buffer_t *rb);
bool ring_buffer_is_full(ring_buffer_t *rb);
void ring_buffer_flush(ring_buffer_t *rb);

#endif // RING_BUFFER_H
