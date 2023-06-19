#include "../../uca-phantom-communicate.h"
#include "../../ringbuf.h"
#include <glib.h>
#include <hdf5.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/time.h>

#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>

#include <stdio.h>
#include <pfring.h>
#include <pfring_zc.h>

#define ETH10GB "enp5s0f1"
#define HDF5_FILE "test.h5"
#define MAX_MTU_SNAPLEN 9000
#define MAX_BUFFER_SIZE G_MAXUINT32
#define NUM_THREADS 1

pfring *ring;
struct pfring_pkthdr hdr;
struct timeval current_time;
pfring_stat statistics;

GAsyncQueue *stats;

gboolean stop = FALSE;
gboolean read_all = FALSE;

ringbuf_t ring_buffer;
GArray *pkt_buffer;
gsize total_data_received = 0;

gdouble data_rate = 0;

void data_rate_thread (struct timeval *start) {
    static gsize last_total_data_received = 0;
    struct timeval now;
    gettimeofday (&now, NULL);

    gdouble delta = (now.tv_sec - start->tv_sec) + (now.tv_usec - start->tv_usec) / 1000000.0;
    if (delta == 0) {
        data_rate = 0;
        g_print ("Delta is 0\n");
    }
    else
        data_rate = (total_data_received - last_total_data_received) / delta;

    if (data_rate > 0)
        g_print ("%f\n", data_rate);

    last_total_data_received = total_data_received;
}

gpointer stats_thread (gpointer data) {
    struct timeval start;
    gettimeofday (&start, NULL);
    while (!stop) {
        data_rate_thread (&start);
        sleep (1);
    }

    g_print ("Closing stats thread\n");

    return NULL;
}

void capture_callback (const struct pfring_pkthdr *header, const u_char *packet, const u_char *user_bytes) {

    // Get the number of bytes received
    total_data_received += header->caplen;

    // Add the packet to the ring buffer
    ringbuf_memcpy_into(ring_buffer, packet, header->caplen);
}

gpointer capture_thread (gpointer data) {
    // Use pfring_loop  to receive packets
    g_print ("Starting capture thread\n");
    int retval = pfring_loop(ring, capture_callback, NULL, 0);
    if (retval < 0) {
        g_print ("Error receiving packets\n");
    }

    g_print ("Closing capture thread\n");

    // pkt_buff = g_malloc0 (MAX_MTU_SNAPLEN * sizeof (pkt_buff));
    // if (pkt_buff == NULL) {
    //     printf("Error allocating packet buffer\n");
    //     return NULL;
    // }

    // while (!stop) {
    //     if (pfring_recv(ring, &pkt_buff, MAX_MTU_SNAPLEN, &hdr, 1) > 0) {
    //         // Get the number of bytes received
    //         total_data_received += hdr.caplen;
    //         // Add the packet to the ring buffer
    //         ringbuf_memcpy_into(ring_buffer, pkt_buff, hdr.caplen);
    //     }
    // }
    return NULL;
}

gpointer read_thread (gpointer data) {
    g_print ("Starting read thread\n");
    guint8 *pkt_buff = g_malloc0 (MAX_MTU_SNAPLEN * sizeof (pkt_buff));
    if (pkt_buff == NULL) {
        printf("Error allocating packet buffer\n");
        return NULL;
    }

    while (!stop) {
        if (ringbuf_read(ring_buffer, pkt_buff, MAX_MTU_SNAPLEN) > 0) {
            // Get the number of bytes received
            total_data_received += hdr.caplen;
            // Add the packet to the ring buffer
            ringbuf_memcpy_into(ring_buffer, pkt_buff, hdr.caplen);
        }
    }
    return NULL;
}

gpointer unpack_thread (gpointer data) {

    gsize bytes_left = 0;

    while (!stop) {
        bytes_left = total_data_received;

        while (bytes_left > 0) {
        }
    }
    return NULL;
}

void handle_sigint (int sig) {
    printf("Received SIGINT\n");
    stop = TRUE;
    g_print ("Total data received: %ld\n", total_data_received);

    pfring_breakloop (ring);
}

int main(int argc, char *argv[]) {
    GError *error = NULL;
    GThread *threads[NUM_THREADS];
    gchar *name = NULL;
    int i = 0;
    int cine = 1;
    int max_nb_images = atoi (argv[1]);
    gsize image_size = 2048 * 1952 * 1.5;
    guint64 data_size = max_nb_images * image_size;
    gboolean result = FALSE;
    stats = g_async_queue_new();
    
    // Register the signal handler for SIGINT (Ctrl+C)
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);
    signal(SIGINT,  handle_sigint);

    // initialize the ring buffer
    if (data_size > MAX_BUFFER_SIZE) {
        printf("Error: image_size * max_nb_images > MAX_BUFFER_SIZE\n");
        return -1;
    }
    ring_buffer = ringbuf_new(data_size);
    if (ring_buffer == NULL) {
        printf("Error allocating ring buffer\n");
        return -1;
    }

    ring = pfring_open(ETH10GB, MAX_MTU_SNAPLEN, PF_RING_REENTRANT);
    if (ring == NULL) {
        printf("Error opening interface: %s\n", strerror(errno));
        return -1;
    }

    if (pfring_set_socket_mode(ring, send_and_recv_mode) != 0) {
        printf("Error setting socket mode\n");
        pfring_close(ring);
        return -1;
    }

    if (pfring_enable_ring(ring) != 0) {
        printf("Error enabling ring\n");
        pfring_close(ring);
        return -1;
    }

    UcaPhantomCommunicate *communicator = g_object_new (UCA_TYPE_PHANTOM_COMMUNICATE, 
        "phantom_ipsource", USE_CLASS,
        "xenabled", TRUE,
        "xnetcard", "enp5s0f1",
        "timestamping", FALSE,
        NULL);

    gboolean connected = uca_phantom_communicate_connect_controlstream(communicator, &error);
    if (!connected && error != NULL) {
        g_print ("Houston theres a problem: %s\n", error->message);
        g_error_free (error);
        g_object_unref (communicator);
        return FALSE;
    }
    
    result = uca_phantom_communicate_arm (communicator, cine, &error);
    if (!result && error != NULL) {
        g_print ("Yo there was an error: %s\n", error->message);
        g_error_free (error);
        g_object_unref (communicator);
        return FALSE;
    }

    sleep(2);

    result = uca_phantom_communicate_trigger (communicator, &error);
    if (!result && error != NULL) {
        g_print ("Yo there was an error: %s\n", error->message);
        g_error_free (error);
        g_object_unref (communicator);
        return FALSE;
    }

    sleep(2);

    // Start the capture thread
    for (i = 0; i < NUM_THREADS; i++) {
        threads[i] = g_thread_new ("capture_thread", capture_thread, NULL);
    }

    // Start stats thread
    GThread *stats = g_thread_new ("stats_thread", stats_thread, NULL);

    // Request the images from the camera
    for (i = 10; i < max_nb_images; i+=10) {
        g_print ("Requesting image %d\n", i);
        result = uca_phantom_communicate_request_images (communicator, cine, i, IMG_P12L, TS_NONE, &error);
        if (!result && error != NULL) {
            g_print ("Yo there was an error: %s\n", error->message);
            g_error_free (error);
            g_object_unref (communicator);
            return FALSE;
        }
    }

    // join the stats thread
    g_thread_join (stats);

    // Wait for the capture thread to finish
    for (i = 0; i < NUM_THREADS; i++) {
        g_thread_join (threads[i]);
        g_print ("Thread %d finished\n", i);
    }
    
    ringbuf_free (&ring_buffer);
    pfring_close(ring); 

    return 0;
}