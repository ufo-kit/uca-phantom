#ifndef INCLUDED_RINGBUF_H
#define INCLUDED_RINGBUF_H

#include <glib.h>

#define MAX_BYTE_POWER_OF_TWO 3
typedef guint64 ringbuf_max_gsize;
#define PLATFORM_MAX_BYTES 1 << MAX_BYTE_POWER_OF_TWO

/*
 * ringbuf.h - C ring buffer (FIFO) interface.
 *
 * Written in 2011 by Drew Hess <dhess-src@bothan.net>.
 *
 * To the extent possible under law, the author(s) have dedicated all
 * copyright and related and neighboring rights to this software to
 * the public domain worldwide. This software is distributed without
 * any warranty.
 *
 * You should have received a copy of the CC0 Public Domain Dedication
 * along with this software. If not, see
 * <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

/*
 * A byte-addressable ring buffer FIFO implementation.
 *
 * The ring buffer's head pointer points to the starting location
 * where data should be written when copying data *into* the buffer
 * (e.g., with ringbuf_read). The ring buffer's tail pointer points to
 * the starting location where data should be read when copying data
 * *from* the buffer (e.g., with ringbuf_write).
 */


typedef struct _ringbuf_t ringbuf_t;

/*
 * Create a new ring buffer with the given capacity (usable
 * bytes). Note that the actual internal buffer size may be one or
 * more bytes larger than the usable capacity, for bookkeeping.
 *
 * Returns the new ring buffer object, or 0 if there's not enough
 * memory to fulfill the request for the given capacity.
 */
ringbuf_t *ringbuf_new (gsize size, gboolean block, GError **error);

/*
 * The size of the internal buffer, in bytes. One or more bytes may be
 * unusable in order to distinguish the "buffer full" state from the
 * "buffer empty" state.
 *
 * For the usable capacity of the ring buffer, use the
 * ringbuf_capacity function.
 */
gsize ringbuf_buffer_size(const ringbuf_t *rb);

/*
 * Deallocate a ring buffer, and, as a side effect, set the pointer to
 * 0.
 */
void ringbuf_free(ringbuf_t *rb);

/*
 * Reset a ring buffer to its initial state (empty).
 */
void ringbuf_reset(ringbuf_t *rb);

// /*
//  * The usable capacity of the ring buffer, in bytes. Note that this
//  * value may be less than the ring buffer's internal buffer size, as
//  * returned by ringbuf_buffer_size.
//  */
// gsize ringbuf_capacity(const ringbuf_t *rb);

/*
 * The number of free/available bytes in the ring buffer. This value
 * is never larger than the ring buffer's usable capacity.
 */
gsize ringbuf_bytes_free(ringbuf_t *rb);

// /*
//  * The number of bytes currently being used in the ring buffer. This
//  * value is never larger than the ring buffer's usable capacity.
//  */
// gsize ringbuf_bytes_used(ringbuf_t *rb);

int ringbuf_is_full(ringbuf_t *rb);

int ringbuf_is_empty(ringbuf_t *rb);

/*
 * access to the head and tail pointers of the ring buffer.
 */
gconstpointer ringbuf_tail(ringbuf_t *rb);

gconstpointer ringbuf_head(ringbuf_t *rb);

/*
 * Copy n bytes from a contiguous memory area src into the ring buffer
 * dst. Returns the ring buffer's new head pointer.
 *
 * It is possible to copy more data from src than is available in the
 * buffer; i.e., it's possible to overflow the ring buffer using this
 * function. When an overflow occurs, the state of the ring buffer is
 * guaranteed to be consistent, including the head and tail pointers;
 * old data will simply be overwritten in FIFO fashion, as
 * needed. However, note that, if calling the function results in an
 * overflow, the value of the ring buffer's tail pointer may be
 * different than it was before the function was called.
 */
gpointer ringbuf_push(ringbuf_t *dst, gconstpointer src, gsize size);

/*
 * Copy n bytes from the ring buffer src, starting from its tail
 * pointer, into a contiguous memory area dst. Returns the value of
 * src's tail pointer after the copy is finished.
 *
 * Note that this copy is destructive with respect to the ring buffer:
 * the n bytes copied from the ring buffer are no longer available in
 * the ring buffer after the copy is complete, and the ring buffer
 * will have n more free bytes than it did before the function was
 * called.
 *
 * This function will *not* allow the ring buffer to underflow. If
 * count is greater than the number of bytes used in the ring buffer,
 * no bytes are copied, and the function will return 0.
 */
gpointer ringbuf_pop (gpointer dst, ringbuf_t *src, gsize size);
gpointer ringbuf_timed_pop (gpointer dst, ringbuf_t *src, gsize size, guint64 timeout);
gboolean ringbuf_direct_copy (ringbuf_t *src, ringbuf_t *dst, gsize size);

// /*
//  * This convenience function calls write(2) on the file descriptor fd,
//  * using the ring buffer rb as the source buffer for writing (starting
//  * at the ring buffer's tail pointer), and returns the value returned
//  * by write(2). It will only call write(2) once, and may return a
//  * short count.
//  *
//  * Note that this copy is destructive with respect to the ring buffer:
//  * any bytes written from the ring buffer to the file descriptor are
//  * no longer available in the ring buffer after the copy is complete,
//  * and the ring buffer will have N more free bytes than it did before
//  * the function was called, where N is the value returned by the
//  * function (unless N is < 0, in which case an error occurred and no
//  * bytes were written).
//  *
//  * This function will *not* allow the ring buffer to underflow. If
//  * count is greater than the number of bytes used in the ring buffer,
//  * no bytes are written to the file descriptor, and the function will
//  * return 0.
//  */
// gssize ringbuf_write(int fd, ringbuf_t *rb, gsize count);

#endif /* INCLUDED_RINGBUF_H */
