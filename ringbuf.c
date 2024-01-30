/*
 * Ring buffer implementation using GLib and virtual memory tricks.
 * More info:
 * - http://en.wikipedia.org/wiki/Circular_buffer 
 * - https://lo.calho.st/posts/black-magic-buffer/
 * 
 * Gabriel Lefloch
 */

#include "ringbuf.h"


#include <unistd.h>
#include <linux/memfd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/types.h>

typedef struct message_t {
    gsize len;
    guint8 *address;
} message_t;

struct _ringbuf_t {
    guint8 *buf;
    gint fd;
    gsize head, tail, buffer_size;
    GMutex mutex;
    GCond readable, writeable;
    gboolean block_on_full;
    GAsyncQueue *message_queue;
};

/** Convenience wrapper around memfd_create syscall, because apparently this is
  * so scary that glibc doesn't provide it...
  */
#if __GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 27)
static inline int memfd_create(const char *name, unsigned int flags) {
    return syscall(__NR_memfd_create, name, flags);
}
#endif

ringbuf_t *ringbuf_new (gsize size, gboolean block, GError **error) {
    // Check that the requested size is a multiple of a page. If it isn't, we're in trouble.
    gsize s = size;
    if (s % getpagesize() != 0) {
        g_warning ("Requested ring buffer size is not a multiple of a page");
        return NULL;
    }

    gint fd = -1;
    gpointer buffer = NULL;

    // Create an anonymous file backed by memory
    if((fd = memfd_create("queue_region", 0)) == -1){
        g_warning ("Failed to create anonymous file");
        return NULL;
    }

    // Set buffer size
    if(ftruncate(fd, s) != 0){
        g_warning ("Could not set size of anonymous file");
    }
    
    // Ask mmap for a good address
    if((buffer = mmap(NULL, 2 * s, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED){
        g_warning ("Could not allocate virtual memory");
    }
    
    // Mmap first region
    if(mmap(buffer, s, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED){
        g_warning ("Could not map buffer into virtual memory");
    }
    
    // Mmap second region, with exact address
    if(mmap(buffer + s, s, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED){
        g_warning ("Could not map buffer into virtual memory");
    }

    ringbuf_t *rb = g_new0(ringbuf_t, 1);
    if (rb == NULL) {
        g_warning ("Failed to allocate memory for ring buffer");
        return NULL;
    }

    // Init the condition variables
    g_cond_init(&rb->readable);
    g_cond_init(&rb->writeable);

    // Init the mutex
    g_mutex_init(&rb->mutex);
    
    /* One byte is used for detecting the full condition. */
    rb->buffer_size = s;
    rb->buf = buffer;
    rb->fd = fd;
    rb->head = rb->tail = 0;
    rb->block_on_full = block;
    
    return rb;
}

gsize ringbuf_buffer_size (const ringbuf_t *rb) {
    return rb->buffer_size;
}

void ringbuf_reset (ringbuf_t *rb) {
    g_mutex_lock(&rb->mutex);
    rb->head = rb->tail = 0;
    g_mutex_unlock(&rb->mutex);
}

void ringbuf_free (ringbuf_t *rb) {
    g_assert(rb);

    GString *error_msg = NULL;

    if(munmap(rb->buf + rb->buffer_size, rb->buffer_size) != 0){
        g_string_append(error_msg, "Could not unmap second buffer. ");
    }
    
    if(munmap(rb->buf, rb->buffer_size) != 0){
        g_string_append(error_msg, "Could not unmap buffer. ");
    }
    
    if(close(rb->fd) != 0){
        g_string_append(error_msg, "Could not close file descriptor. ");
    }

    if (error_msg != NULL) {
        g_warning ("Failed to free ring buffer: %s", error_msg->str);
        g_string_free(error_msg, TRUE);
    }

    g_mutex_clear(&rb->mutex);
    g_cond_clear(&rb->readable);
    g_cond_clear(&rb->writeable);

    g_free(rb);
}

static gsize ringbuf_bytes_free_unlocked (ringbuf_t *rb) {
    gsize free = 0;
    if (rb->tail <= rb->head) {
        free = rb->buffer_size - (rb->head - rb->tail);
    }
    else {
        free = rb->tail - rb->head;
    }
    return free;
}

static gsize ringbuf_bytes_used_unlocked (ringbuf_t *rb) {
    return rb->buffer_size - ringbuf_bytes_free_unlocked(rb);
}

gsize ringbuf_bytes_free (ringbuf_t *rb) {
    gsize free = 0;
    g_mutex_lock(&rb->mutex);
    free = ringbuf_bytes_free_unlocked(rb);
    g_mutex_unlock(&rb->mutex);
    return free;
}

gsize ringbuf_bytes_used (ringbuf_t *rb) {
    return rb->buffer_size - ringbuf_bytes_free(rb);
}

gboolean ringbuf_is_full (ringbuf_t *rb) {
    return (ringbuf_bytes_free(rb) == 0);
}

gboolean ringbuf_is_empty (ringbuf_t *rb) {
    return (ringbuf_bytes_free (rb) == ringbuf_bytes_free (rb));
}

gconstpointer ringbuf_tail (ringbuf_t *rb) {
    gpointer retval = NULL;
    g_mutex_lock(&rb->mutex);
    retval = rb->tail + rb->buf;
    g_mutex_unlock(&rb->mutex);
    return retval;
}

gconstpointer ringbuf_head (ringbuf_t *rb) {
    gpointer retval = NULL;
    g_mutex_lock(&rb->mutex);
    retval = rb->head + rb->buf;
    g_mutex_unlock(&rb->mutex);
    return retval;
}


gpointer ringbuf_push(ringbuf_t *dst, gconstpointer src, gsize size) {
    gpointer head = NULL;
  
    // Wait for space to become available
    g_mutex_lock(&dst->mutex);
    if (dst->block_on_full) {
        while (ringbuf_bytes_free_unlocked(dst) < size) {
            g_cond_wait(&dst->writeable, &dst->mutex);
        }
    }

    memcpy(dst->buf + dst->head, src, size);
    dst->head += size;
    head = dst->buf + dst->head;
    g_cond_signal(&dst->readable);
    g_mutex_unlock(&dst->mutex);


    return head;
}

gpointer ringbuf_pop (gpointer dst, ringbuf_t *src, gsize size) {
    gpointer tail = NULL;
    
    // Wait for data to become available
    g_mutex_lock(&src->mutex);
    while (ringbuf_bytes_used_unlocked(src) < size) {
        g_cond_wait (&src->readable, &src->mutex);
    }

    memcpy (dst, src->buf + src->tail, size);
    src->tail += size;
    tail = src->buf + src->tail;

    if(src->tail >= src->buffer_size) {
        src->head -= src->buffer_size;
        src->tail -= src->buffer_size;
    }

    g_cond_signal (&src->writeable);
    g_mutex_unlock (&src->mutex);
    

    return tail;
}

gpointer ringbuf_timed_pop (gpointer dst, ringbuf_t *src, gsize size, guint64 timeout) {
    gpointer tail = NULL;
    guint64 end_time = 0;
    
    // Wait for data to become available
    g_mutex_lock(&src->mutex);
    end_time = g_get_monotonic_time () + timeout;
    while (src->tail == src->head) {
        if (!g_cond_wait_until (&src->readable, &src->mutex, end_time)) {
            g_mutex_unlock (&src->mutex);
            return NULL;
        }
    }

    memcpy (dst, &src->buf[src->tail], size);
    src->tail += size;
    tail = src->buf + src->tail;

    if(src->tail >= src->buffer_size) {
        src->head -= src->buffer_size;
        src->tail -= src->buffer_size;
    }

    g_cond_signal (&src->writeable);
    g_mutex_unlock (&src->mutex);
    

    return tail;
}

gboolean ringbuf_direct_copy (ringbuf_t *src, ringbuf_t *dst, gsize size) {
    g_mutex_lock(&src->mutex);
    g_mutex_lock(&dst->mutex);

    // Wait for data to become available in src
    while (src->tail == src->head) {
        g_cond_wait (&src->readable, &src->mutex);
    }

    // Wait for space to become available in dst
    if (dst->block_on_full) {
        while (ringbuf_bytes_free(dst) < size) {
            g_cond_wait(&dst->writeable, &dst->mutex);
        }
    }

    memcpy (dst->buf + dst->head, src->buf + src->tail, size);
    dst->head += size;
    src->tail += size;

    if(src->tail >= src->buffer_size) {
        src->head -= src->buffer_size;
        src->tail -= src->buffer_size;
    }

    g_cond_signal (&dst->readable);
    g_cond_signal (&src->writeable);
    g_mutex_unlock(&dst->mutex);
    g_mutex_unlock(&src->mutex);

    return TRUE;
}