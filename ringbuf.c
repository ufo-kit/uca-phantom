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

#include <stdlib.h>
/*
 * The code is written for clarity, not cleverness or performance, and
 * contains many g_assert()s to enforce invariant assumptions and catch
 * bugs. Feel free to optimize the code and to remove asserts for use
 * in your own projects, once you're comfortable that it functions as
 * intended.
 */

struct ringbuf_t {
    guint8 *buf;
    gpointer head, tail;
    gsize size;
};

ringbuf_t ringbuf_new(gsize capacity) {
    ringbuf_t rb = g_new0 (struct ringbuf_t, 1);
    if (rb) {
        /* One byte is used for detecting the full condition. */
        rb->size = capacity + 1;
        rb->buf = g_malloc(rb->size);
        if (rb->buf)
            ringbuf_reset(rb);
        else {
            free(rb);
            return 0;
        }
    }
    return rb;
}

gsize ringbuf_buffer_size(const struct ringbuf_t *rb) {
    return rb->size;
}

void ringbuf_reset(ringbuf_t rb) {
    g_atomic_pointer_set(&rb->head, rb->buf);
    g_atomic_pointer_set(&rb->tail, rb->buf);
}

void ringbuf_free(ringbuf_t *rb) {
    g_assert(rb && *rb);
    free((*rb)->buf);
    free(*rb);
    *rb = 0;
}

gsize ringbuf_capacity(const struct ringbuf_t *rb) {
    return ringbuf_buffer_size(rb) - 1;
}

/*
 * Return a pointer to one-past-the-end of the ring buffer's
 * contiguous buffer. You shouldn't normally need to use this function
 * unless you're writing a new ringbuf_* function.
 */
static const guint8 *ringbuf_end(const struct ringbuf_t *rb) {
    return rb->buf + ringbuf_buffer_size(rb);
}

gsize ringbuf_bytes_free(const struct ringbuf_t *rb) {
    guint8 *head = g_atomic_pointer_get(&rb->head);
    guint8 *tail = g_atomic_pointer_get(&rb->tail);
    if (head >= tail)
        return ringbuf_capacity(rb) - (head - tail);
    else
        return tail - head - 1;
}

gsize ringbuf_bytes_used(const struct ringbuf_t *rb) {
    return ringbuf_capacity(rb) - ringbuf_bytes_free(rb);
}

gint ringbuf_is_full(const struct ringbuf_t *rb) {
    return ringbuf_bytes_free(rb) == 0;
}

gint ringbuf_is_empty(const struct ringbuf_t *rb) {
    return ringbuf_bytes_free(rb) == ringbuf_capacity(rb);
}

const gpointer ringbuf_tail(const struct ringbuf_t *rb) {
    return g_atomic_pointer_get(&rb->tail);
}

const gpointer ringbuf_head(const struct ringbuf_t *rb) {
    return g_atomic_pointer_get(&rb->head);
}

/*
 * Given a ring buffer rb and a pointer to a location within its
 * contiguous buffer, return the a pointer to the next logical
 * location in the ring buffer.
 */
static guint8 *ringbuf_nextp(ringbuf_t rb, const guint8 *p) {
    /*
     * The g_assert guarantees the expression (++p - rb->buf) is
     * non-negative; therefore, the modulus operation is safe and
     * portable.
     */
    g_assert((p >= rb->buf) && (p < ringbuf_end(rb)));
    return rb->buf + ((++p - rb->buf) % ringbuf_buffer_size(rb));
}

gsize ringbuf_memset(ringbuf_t dst, gint c, gsize len) {
    const guint8 *bufend = ringbuf_end(dst);
    gsize nwritten = 0, n;
    gsize count = MIN(len, ringbuf_buffer_size(dst));
    guint overflow = count > ringbuf_bytes_free(dst);
    guint8 *dhead = g_atomic_pointer_get(&dst->head);
    while (nwritten != count) {
        /* don't copy beyond the end of the buffer */
        g_assert(bufend > dhead);
        n = MIN(bufend - dhead, count - nwritten);
        memset(dhead, c, n);
        dhead += n;
        nwritten += n;

        /* wrap? */
        if (dhead == bufend)
            dhead = dst->buf;
    }
    g_atomic_pointer_set(&dst->head, dhead);

    if (overflow) {
        gpointer new_tail = ringbuf_nextp(dst, dhead);
        g_atomic_pointer_set(&dst->tail, new_tail);
        g_assert(ringbuf_is_full(dst));
    }

    return nwritten;
}

gpointer ringbuf_memcpy_into(ringbuf_t dst, const gpointer src, gsize count) {
    const guint8 *u8src = src;
    const guint8 *bufend = ringbuf_end(dst);
    guint overflow = count > ringbuf_bytes_free(dst);
    gsize nread = 0;

    guint8 * dhead = g_atomic_pointer_get(&dst->head);
    while (nread != count) {
        /* don't copy beyond the end of the buffer */
        g_assert(bufend > dhead);
        gsize n = MIN(bufend - dhead, count - nread);
        memcpy(dhead, u8src + nread, n);
        dhead += n;
        nread += n;

        /* wrap? */
        if (dhead == bufend)
            dhead = dst->buf;
    }
    g_atomic_pointer_set(&dst->head, dhead);

    if (overflow) {
        gpointer new_tail = ringbuf_nextp(dst, dhead);
        g_atomic_pointer_set(&dst->tail, new_tail);
        g_assert(ringbuf_is_full(dst));
    }

    return dhead;
}

gssize ringbuf_read (gint fd, ringbuf_t rb, gsize count) {
    const guint8 *bufend = ringbuf_end (rb);
    gssize nfree = ringbuf_bytes_free (rb);

    /* don't write beyond the end of the buffer */
    guint8 *head = g_atomic_pointer_get (&rb->head);
    g_assert (bufend > head);
    count = MIN (bufend - head, count);
    gssize n = read (fd, head, count);
    if (n > 0) {
        g_assert (head + n <= bufend);
        head += n;

        /* wrap? */
        if (head == bufend)
            head = rb->buf;

        /* fix up the tail pointer if an overflow occurred */
        if (n > nfree) {
            g_atomic_pointer_set (&rb->tail, ringbuf_nextp (rb, head));
            g_assert (ringbuf_is_full(rb));
        }
    }
    g_atomic_pointer_set (&rb->head, head);

    return n;
}

void *ringbuf_memcpy_from(void *dst, ringbuf_t src, gsize count) {
    gsize bytes_used = ringbuf_bytes_used(src), n;
    if (count > bytes_used)
        return 0;

    guint8 *u8dst = dst;
    const guint8 *bufend = ringbuf_end(src);
    gsize nwritten = 0;
    guint8 *tail = g_atomic_pointer_get(&src->tail);
    while (nwritten != count) {
        g_assert(bufend > tail);
        n = MIN(bufend - tail, count - nwritten);
        memcpy(u8dst + nwritten, tail, n);
        tail += n;
        nwritten += n;

        /* wrap ? */
        if (tail == bufend)
            tail = src->buf;
    }
    g_atomic_pointer_set(&src->tail, tail);

    g_assert(count + ringbuf_bytes_used(src) == bytes_used);
    return tail;
}

gssize ringbuf_write (gint fd, ringbuf_t rb, gsize count) {
    gsize bytes_used = ringbuf_bytes_used(rb);
    if (count > bytes_used)
        return 0;

    const guint8 *bufend = ringbuf_end(rb);
    guint8 *tail = g_atomic_pointer_get(&rb->tail);
    guint8 *head = g_atomic_pointer_get(&rb->head);
    g_assert(bufend > head);
    count = MIN(bufend - tail, count);
    gssize n = write(fd, tail, count);
    if (n > 0) {
        g_assert(tail + n <= bufend);
        tail += n;

        /* wrap? */
        if (tail == bufend)
            tail = rb->buf;

        g_assert(n + ringbuf_bytes_used(rb) == bytes_used);
    }
    g_atomic_pointer_set(&rb->tail, tail);

    return n;
}

void *ringbuf_copy (ringbuf_t dst, ringbuf_t src, gsize count) {
    gsize src_bytes_used = ringbuf_bytes_used(src);
    if (count > src_bytes_used)
        return 0;
    gint overflow = count > ringbuf_bytes_free(dst);

    const guint8 * src_bufend = ringbuf_end(src);
    const guint8 * dst_bufend = ringbuf_end(dst);
    gsize ncopied = 0;

    guint8 * tail = g_atomic_pointer_get (&src->tail);
    guint8 * dhead = g_atomic_pointer_get (&dst->head);
    while (ncopied != count) {
        g_assert(src_bufend > tail);
        gsize nsrc = MIN(src_bufend - tail, count - ncopied);
        g_assert(dst_bufend > dhead);
        gsize n = MIN(dst_bufend - dhead, nsrc);
        memcpy(dhead, tail, n);
        tail += n;
        dhead += n;
        ncopied += n;

        /* wrap ? */
        if (tail == src_bufend)
            tail = src->buf;
        if (dhead == dst_bufend)
            dhead = dst->buf;
    }
    g_atomic_pointer_set(&src->tail, tail);
    g_atomic_pointer_set(&dst->head, dhead);

    g_assert(count + ringbuf_bytes_used(src) == src_bytes_used);
    
    if (overflow) {
        g_atomic_pointer_set(&dst->tail, ringbuf_nextp(dst, dhead));
        g_assert(ringbuf_is_full(dst));
    }

    return dhead;
}
