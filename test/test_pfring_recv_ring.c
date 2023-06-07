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
hid_t file_id, group_id, attr_id, dataset_id, dataspace_id;


// void init_hdf5_file() {
//     // Create a new HDF5 file
//     file_id = H5Fcreate(HDF5_FILE, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);

//     // Create a dataset with unlimited size
//     hsize_t initial_dims[1] = {0};
//     hsize_t max_dims[1] = {H5S_UNLIMITED};
//     hid_t dataspace_id = H5Screate_simple(1, initial_dims, max_dims);

//     // Create a dataset creation property list with chunked storage
//     hid_t plist_id = H5Pcreate(H5P_DATASET_CREATE);
//     hsize_t chunk_dims[1] = {1024};
//     H5Pset_chunk(plist_id, 1, chunk_dims);

//     // Create the dataset
//     dataset_id = H5Dcreate2(file_id, "data", H5T_NATIVE_USHORT, dataspace_id, H5P_DEFAULT, plist_id, H5P_DEFAULT);

//     // Close the dataspace and property list
//     H5Sclose(dataspace_id);
//     H5Pclose(plist_id);
// }

// void write_data_to_hdf5(uint8_t *data, size_t data_length) {
//     // Get the current size of the dataset
//     hid_t dataspace_id = H5Dget_space(dataset_id);
//     hsize_t current_dims[1];
//     H5Sget_simple_extent_dims(dataspace_id, current_dims, NULL);

//     // Extend the dataset
//     hsize_t new_dims[1] = {current_dims[0] + data_length};
//     H5Dset_extent(dataset_id, new_dims);

//     // Select the hyperslab to write the new data
//     H5Sselect_hyperslab(dataspace_id, H5S_SELECT_SET, current_dims, NULL, &data_length, NULL);

//     // Create a memory dataspace
//     hid_t memspace_id = H5Screate_simple(1, &data_length, NULL);

//     // Write the data to the dataset
//     H5Dwrite(dataset_id, H5T_NATIVE_USHORT, memspace_id, dataspace_id, H5P_DEFAULT, data);

//     // Close the dataspaces
//     H5Sclose(memspace_id);
//     H5Sclose(dataspace_id);
// }

// void close_hdf5_file() {
//     // Close the dataset and file
//     H5Dclose(dataset_id);
//     H5Fclose(file_id);
// }


gpointer capture_thread (gpointer data) {
    int *ptr_nb_images = (int*) data;
    int nb_images = *ptr_nb_images;

    guint64 total_data_received = 0;

    while (!stop) {
        gpointer head = ringbuf_head(ring_buffer);
        if (pfring_recv(ring, head, MAX_BUFFER_SIZE, &hdr, 1) > 0) {
            // Get the number of bytes received
            total_data_received += hdr.caplen;
        }
    }

    g_print ("Total data received: %ld\n", total_data_received);

    return NULL;
}

void handle_sigint (int sig) {
    printf("Received SIGINT\n");
    stop = TRUE;
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

    // Create the HDF5 file
    file_id = H5Fcreate(HDF5_FILE, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (file_id < 0) {
        printf("Error creating HDF5 file\n");
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

    
    ringbuf_free (ring_buffer);
    pfring_close(ring); 

    return 0;
}