#ifndef INCLUDED_RINGBUF_H
#define INCLUDED_RINGBUF_H


#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/memfd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>

#include <glib.h>

typedef struct _ringbuf_t ringbuf_t;

/**
 * ringbuf_new:
 * @size: Desired size in bytes (may be rounded to page size at runtime).
 * @block: Whether to block when the ring buffer is full.
 *
 * Creates and initializes a new ring buffer. Returns NULL on error.
 */
ringbuf_t *ringbuf_new (gsize size, gboolean block);

/**
 * ringbuf_buffer_size:
 * @rb: A valid ring buffer object.
 *
 * Returns the total internal buffer size in bytes.
 */
gsize ringbuf_buffer_size(const ringbuf_t *rb);

/**
 * ringbuf_free:
 * @rb: A valid ring buffer object.
 *
 * Frees associated memory. Invalidates @rb.
 */
void ringbuf_free(ringbuf_t *rb);

/**
 * ringbuf_reset:
 * @rb: A valid ring buffer object.
 *
 * Clears all data and resets head/tail pointers to zero.
 */
void ringbuf_reset(ringbuf_t *rb);

/**
 * ringbuf_bytes_free:
 * @rb: A valid ring buffer object.
 *
 * Returns how many bytes are still available to write.
 */
gsize ringbuf_bytes_free(ringbuf_t *rb);

/**
 * ringbuf_is_full:
 * @rb: A valid ring buffer object.
 *
 * Returns nonzero if ring buffer has no more space.
 */
int ringbuf_is_full(ringbuf_t *rb);

/**
 * ringbuf_is_empty:
 * @rb: A valid ring buffer object.
 *
 * Returns nonzero if ring buffer is empty.
 */
int ringbuf_is_empty(ringbuf_t *rb);

/**
 * ringbuf_tail:
 * @rb: A valid ring buffer object.
 *
 * Gets the current tail pointer for reading.
 */
gconstpointer ringbuf_tail(ringbuf_t *rb);

/**
 * ringbuf_head:
 * @rb: A valid ring buffer object.
 *
 * Gets the current head pointer for writing.
 */
gconstpointer ringbuf_head(ringbuf_t *rb);

/**
 * ringbuf_push:
 * @dst: Destination ring buffer.
 * @src: Source data to copy.
 * @size: Number of bytes to copy.
 *
 * Writes data into the buffer, growing the head pointer.
 */
gpointer ringbuf_push(ringbuf_t *dst, gconstpointer src, gsize size);

/**
 * ringbuf_pop:
 * @dst: Destination buffer to receive data.
 * @src: Source ring buffer.
 * @size: Number of bytes to copy.
 *
 * Reads data from the buffer, moving the tail pointer.
 */
gpointer ringbuf_pop (gpointer dst, ringbuf_t *src, gsize size);

/**
 * ringbuf_timed_pop:
 * @dst: Destination buffer to receive data.
 * @src: Source ring buffer.
 * @size: Number of bytes to copy.
 * @timeout: Monotonic deadline in microseconds.
 *
 * Attempts to read data within the specified timeout. Returns NULL on timeout.
 */
gpointer ringbuf_timed_pop (gpointer dst, ringbuf_t *src, gsize size, guint64 timeout);

/**
 * ringbuf_direct_copy:
 * @src: Source ring buffer.
 * @dst: Destination ring buffer.
 * @size: Number of bytes to copy.
 *
 * Copies data directly from @src to @dst. Returns TRUE on success.
 */
gboolean ringbuf_direct_copy (ringbuf_t *src, ringbuf_t *dst, gsize size);

/**
 * ringbuf_move_tail:
 * @rb: A valid ring buffer object.
 * @size: Number of bytes to move.
 *
 * Advances the tail pointer by @size to discard data without copying.
 */
gconstpointer ringbuf_move_tail (ringbuf_t *rb, gsize size);

/**
 * ringbuf_move_head:
 * @rb: A valid ring buffer object.
 * @size: Number of bytes to move.
 *
 * Advances the head pointer by @size for a manual write operation.
 */
gconstpointer ringbuf_move_head (ringbuf_t *rb, gsize size);

/**
 * ringbuf_reserve:
 * @rb: A valid ring buffer object.
 * @size: Number of bytes to reserve.
 *
 * Reserves space in the buffer without writing immediately.
 */
gpointer ringbuf_reserve (ringbuf_t *rb, gsize size);

/**
 * ringbuf_commit:
 * @rb: A valid ring buffer object.
 *
 * Signals that reserved data has now been written.
 */
void ringbuf_commit (ringbuf_t *rb, gsize size);

/**
 * ringbuf_wait_for_data:
 * @rb: A valid ring buffer object.
 * @size: Number of bytes to wait for.
 *
 * Blocks until at least @size bytes are available. Returns how many bytes are used.
 */
gsize ringbuf_wait_for_data (ringbuf_t *rb, gsize size);

#endif /* INCLUDED_RINGBUF_H */