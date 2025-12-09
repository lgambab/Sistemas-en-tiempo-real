#include "ring_buffer.h"


/**
 * @brief Initializes a ring buffer and its variables to their initial values.
*/
void ring_buffer_init(ring_buffer_t *rb, uint8_t *buffer, uint16_t capacity) 
{
    rb->buffer = buffer;
    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
    rb->full = false;
}

/**
 * @brief Writes a byte of data into the ring buffer, discarding old data if the buffer is full.
 * 
 * @param rb Pointer to the ring buffer.
 * @param data The byte of data to write.
 * @return true if the write was successful, false if the buffer is full.
 */
bool ring_buffer_write(ring_buffer_t *rb, uint8_t data)
{
    if (rb->full) {
        // If the buffer is full, we overwrite the oldest data
        rb->tail = (rb->tail + 1) % rb->capacity;
    }
    rb->buffer[rb->head] = data;
    rb->head = (rb->head + 1) % rb->capacity;
    rb->full = (rb->head == rb->tail);
    return true;
}

/**
 * @brief Reads a byte of data from the ring buffer.
 * 
 * @param rb Pointer to the ring buffer.
 * @param data Pointer to where the read data will be stored.
 * @return true if the read was successful, false if the buffer is empty.
 */
bool ring_buffer_read(ring_buffer_t *rb, uint8_t *data)
{
    if (rb->head == rb->tail && !rb->full) {
        // Buffer is empty
        return false;
    }
    *data = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % rb->capacity;
    rb->full = false; // After reading, the buffer can't be full
    return true;
}

/**
 * @brief Returns the number of elements currently in the ring buffer.
 * 
 * @param rb Pointer to the ring buffer.
 * @return The number of elements in the buffer.
 */
uint16_t ring_buffer_count(ring_buffer_t *rb)
{
    if (rb->full) {
        return rb->capacity;
    }
    if (rb->head >= rb->tail) {
        return rb->head - rb->tail;
    } else {
        return (rb->capacity - rb->tail) + rb->head;
    }
}

/**
 * @brief Checks if the ring buffer is empty.
 * 
 * @param rb Pointer to the ring buffer.
 * @return true if the buffer is empty, false otherwise.
 */
bool ring_buffer_is_empty(ring_buffer_t *rb)
{
    return (rb->head == rb->tail && !rb->full);
}

/**
 * @brief Checks if the ring buffer is full.
 * 
 * @param rb Pointer to the ring buffer.
 * @return true if the buffer is full, false otherwise.
 */
bool ring_buffer_is_full(ring_buffer_t *rb)
{
    return rb->full;
}

/**
 * @brief Flushes the ring buffer, clearing all data.
 * 
 * @param rb Pointer to the ring buffer.
 */
void ring_buffer_flush(ring_buffer_t *rb)
{
    rb->head = 0;
    rb->tail = 0;
    rb->full = false;
}
