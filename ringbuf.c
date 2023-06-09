/*
 * ringbuf.c - C ring buffer (FIFO) implementation.
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

#include "ringbuf.h"

struct ringbuf_t {
    guint8 *buf;
    guint8 *head, *tail;
    gsize size;
    GMutex mutex;
};

ringbuf_t ringbuf_new (gsize capacity) {
    ringbuf_t rb = g_new0(struct ringbuf_t, 1);
    if (rb) {
        /* One byte is used for detecting the full condition. */
        rb->size = capacity + 1;
        rb->buf = g_malloc(rb->size);
        if (rb->buf)
            ringbuf_reset(rb);
        else {
            g_free(rb);
            return 0;
        }
        g_mutex_init(&rb->mutex);
    }
    return rb;
}

gsize ringbuf_buffer_size (const struct ringbuf_t *rb) {
    return rb->size;
}

void ringbuf_reset (ringbuf_t rb) {
    g_mutex_lock(&rb->mutex);
    rb->head = rb->tail = rb->buf;
    g_mutex_unlock(&rb->mutex);
}

void ringbuf_free (ringbuf_t *rb) {
    g_assert(rb && *rb);
    g_mutex_clear(&(*rb)->mutex);
    g_free((*rb)->buf);
    (*rb)->buf = NULL;
    g_free(*rb);
    *rb = NULL;
}

gsize ringbuf_capacity (const struct ringbuf_t *rb) {
    return ringbuf_buffer_size(rb) - 1;
}

/*
 * Return a pointer to one-past-the-end of the ring buffer's
 * contiguous buffer. You shouldn't normally need to use this function
 * unless you're writing a new ringbuf_* function.
 */
static const guint8 *ringbuf_end (const struct ringbuf_t *rb) {
    return rb->buf + ringbuf_buffer_size(rb);
}

gsize ringbuf_bytes_free (struct ringbuf_t *rb) {
    gsize retval = 0;
    g_mutex_lock(&rb->mutex);
    if (rb->head >= rb->tail)
        retval = ringbuf_capacity(rb) - (gsize)(rb->head - rb->tail);
    else
        retval =  (gsize)(rb->tail - rb->head) - 1;
    g_mutex_unlock(&rb->mutex);
    return retval;
}

gsize ringbuf_bytes_used (struct ringbuf_t *rb) {
    return ringbuf_capacity(rb) - ringbuf_bytes_free(rb);
}

gboolean ringbuf_is_full (struct ringbuf_t *rb) {
    return (ringbuf_bytes_free(rb) == 0);
}

gboolean ringbuf_is_empty (struct ringbuf_t *rb) {
    return (ringbuf_bytes_free (rb) == ringbuf_capacity (rb));
}

gconstpointer ringbuf_tail (struct ringbuf_t *rb) {
    gconstpointer retval = 0;
    g_mutex_lock(&rb->mutex);
    retval = rb->tail;
    g_mutex_unlock(&rb->mutex);
    return retval;
}

gconstpointer ringbuf_head (struct ringbuf_t *rb) {
    gconstpointer retval = 0;
    g_mutex_lock(&rb->mutex);
    retval = rb->head;
    g_mutex_unlock(&rb->mutex);
    return retval;
}

/*
 * Given a ring buffer rb and a pointer to a location within its
 * contiguous buffer, return the a pointer to the next logical
 * location in the ring buffer.
 */
static guint8 *ringbuf_nextp (ringbuf_t rb, const guint8 *p) {
    /*
     * The assert guarantees the expression (++p - rb->buf) is
     * non-negative; therefore, the modulus operation is safe and
     * portable.
     */
    guint8 *retval = 0;
    g_mutex_lock(&rb->mutex);
    g_assert((p >= rb->buf) && (p < ringbuf_end(rb)));
    retval = rb->buf + ((++p - rb->buf) % ringbuf_buffer_size(rb));
    g_mutex_unlock(&rb->mutex);
    return retval;
}

gpointer ringbuf_memcpy_into(ringbuf_t dst, gconstpointer src, gsize count) {
    gpointer retval = NULL;

    const guint8 *u8src = src;
    const guint8 *bufend = ringbuf_end(dst);
    gboolean overflow = count > ringbuf_bytes_free(dst);
    gsize nread = 0, n, diff;
    
    g_mutex_lock(&dst->mutex);
    while (nread != count) {
        /* don't copy beyond the end of the buffer */
        g_assert(bufend > dst->head);
        diff = bufend - dst->head;
        n = MIN(diff, count - nread);
        memcpy(dst->head, u8src + nread, n);
        dst->head += n;
        nread += n;

        /* wrap? */
        if (dst->head == bufend)
            dst->head = dst->buf;
    }
    g_mutex_unlock(&dst->mutex);

    if (overflow) {
        dst->tail = ringbuf_nextp(dst, dst->head);
        g_assert(ringbuf_is_full(dst));
    }

    retval = dst->head;

    return retval;
}

gpointer ringbuf_memcpy_from (gpointer dst, ringbuf_t src, gsize count) {
    gpointer retval = NULL;
    g_mutex_lock(&src->mutex);

    gsize bytes_used = ringbuf_bytes_used(src), n = 0, diff = 0;
    if (count > bytes_used) {
        g_mutex_unlock(&src->mutex);
        return NULL;
    }

    guint8 *u8dst = dst;
    const guint8 *bufend = ringbuf_end(src);
    gsize nwritten = 0;
    while (nwritten != count) {
        g_assert(bufend > src->tail);
        diff = bufend - src->tail;
        n = MIN(diff, count - nwritten);
        memcpy(u8dst + nwritten, src->tail, n);
        src->tail += n;
        nwritten += n;

        /* wrap ? */
        if (src->tail == bufend)
            src->tail = src->buf;
    }

    g_assert(count + ringbuf_bytes_used(src) == bytes_used);

    retval = src->tail;
    g_mutex_unlock(&src->mutex);
    return retval;
}