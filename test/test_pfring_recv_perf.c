#include "../../uca-phantom-communicate.h"
#include <glib.h>

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

typedef struct  {
    struct timeval start_time;
    struct timeval end_time;
    guint64 data_received;
    gboolean stop;
} StatData;

double delta_time (struct timeval * now,
		   struct timeval * before) {
  time_t delta_seconds;
  time_t delta_microseconds;

  /*
   * compute delta in second, 1/10's and 1/1000's second units
   */
  delta_seconds      = now -> tv_sec  - before -> tv_sec;
  delta_microseconds = now -> tv_usec - before -> tv_usec;

  if(delta_microseconds < 0) {
    /* manually carry a one from the seconds field */
    delta_microseconds += 1000000;  /* 1e6 */
    -- delta_seconds;
  }
  return((double)(delta_seconds * 1000) + (double)delta_microseconds/1000);
}


double get_process_cpu_usage(pid_t pid) {
    char stat_path[64];
    sprintf(stat_path, "/proc/%d/stat", pid);

    FILE *stat_file = fopen(stat_path, "r");
    if (!stat_file) {
        perror("Error opening stat file");
        return -1;
    }

    long utime, stime, starttime;
    int ret = fscanf(stat_file, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %ld %ld %*d %*d %*d %*d %*d %*d %ld",
           &utime, &stime, &starttime);
    if (ret != 3) {
        perror("Error reading stat file");
        return -1;
    }

    fclose(stat_file);

    FILE *uptime_file = fopen("/proc/uptime", "r");
    if (!uptime_file) {
        perror("Error opening uptime file");
        return -1;
    }

    double uptime;
    ret = fscanf(uptime_file, "%lf", &uptime);
    if (ret != 1) {
        perror("Error reading uptime file");
        return -1;
    }
    fclose(uptime_file);

    long clock_ticks = sysconf(_SC_CLK_TCK);
    double utime_seconds = (double) utime / clock_ticks;
    double stime_seconds = (double) stime / clock_ticks;
    double elapsed_time = uptime - ((double) starttime / clock_ticks);
    double total_cpu_usage = 100 * (utime_seconds + stime_seconds) / elapsed_time;

    return total_cpu_usage;
}

double get_overall_cpu_usage() {
    FILE *stat_file = fopen("/proc/stat", "r");
    if (!stat_file) {
        perror("Error opening stat file");
        return -1;
    }

    long user, nice, system, idle, iowait, irq, softirq, steal;
    int ret = fscanf(stat_file, "cpu %ld %ld %ld %ld %ld %ld %ld %ld", &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
    if (ret != 8) {
        perror("Error reading stat file");
        return -1;
    }
    fclose(stat_file);

    long total = user + nice + system + idle + iowait + irq + softirq + steal;
    long total_non_idle = user + nice + system + irq + softirq + steal;
    double overall_cpu_usage = 100 * (double) total_non_idle / total;

    return overall_cpu_usage;
}

// "ether proto 0x88b7"
/**
 * Stats format:
 * current time | 
 * number of images |
 * 
 * total packets received | 
 * total packets dropped | 
 * total packets shunted | 
 * total data received (Bytes) |
 * 
 * currently received packets  | 
 * currently dropped | 
 * currently shunted | 
 * current data received (Bytes) |
 * 
 * current data rate (MBps) | 
 * Global CPU usage (%)
 * Process CPU usage (%)
 * 
 */
gpointer stats_printer (gpointer data) {
    guint64 lastDataReceived = 0, current_data_received = 0;
    int *ptr_nb_images = (int*) data;
    int nb_images = *ptr_nb_images;

    // Print data to a file 
    gchar *stats_file_name = g_strdup_printf("stats_%d.txt", nb_images);
    FILE *stats_file = fopen(stats_file_name, "w");
    g_free(stats_file_name);
    if (!stats_file) {
        perror("Error opening stats file");
        return NULL;
    }

    statistics.recv = 0;
    statistics.drop = 0;
    statistics.shunt = 0;

    while (!stop) {
        StatData *stat_data = g_async_queue_pop (stats);

        if (stat_data->stop) {
            g_free (stat_data);
            return NULL;
        }

        // This comprises the total received packets, dropped packets, shunted packets
        // pfring_stats(ring, &statistics);

        // u_int64_t total_pkt_received = statistics.recv;
        // u_int64_t total_pkt_dropped = statistics.drop;
        // u_int64_t total_pkt_shunted = statistics.shunt;
        // guint64 current_pkt_received = total_pkt_received - lastPktReceived;
        // guint64 current_pkt_dropped = statistics.drop - lastDrops;
        // guint64 current_pkt_shunted = statistics.shunt - lastShunts;
        current_data_received = stat_data->data_received - lastDataReceived;

        // lastPktReceived = total_pkt_received;
        // lastDrops = total_pkt_dropped;
        // lastShunts = total_pkt_shunted;
        lastDataReceived = current_data_received;

        gettimeofday(&current_time, NULL);

        double deltaMillisec = delta_time (&current_time, &(stat_data->start_time));
        double current_data_rate = current_data_received / ((double)(deltaMillisec / 1000.0));

        double process_cpu_usage = get_process_cpu_usage (getpid());
        double global_cpu_usage = get_overall_cpu_usage ();

        fprintf (stats_file, "%ld.%06ld %d %ld %ld %lf %.2f %.2f\n", 
            current_time.tv_sec, current_time.tv_usec, nb_images, 
            stat_data->data_received, current_data_received, 
            current_data_rate, global_cpu_usage, process_cpu_usage);
        
        g_free (stat_data);
    }

    return NULL;
}

gpointer capture_thread(gpointer data) {
    guint8 *buffer;

    u_int

    int *ptr_nb_images = (int*) data;
    int nb_images = *ptr_nb_images;
    guint64 data_size = nb_images * 2048 * 1952 * 1.5;

    buffer = g_malloc0(data_size+32*500);
    if (buffer == NULL) {
        printf("Error allocating buffer\n");
        return NULL;
    }

    guint64 total_data_received = 0;
    struct timeval start;
    struct timeval end;

    while (!stop) {
        gettimeofday(&start, NULL);
        if (pfring_recv(ring, &buffer, data_size, &hdr, 1) > 0) {
            // Get the number of bytes received
            total_data_received += hdr.len;
        }
        gettimeofday(&start, NULL);

        StatData *data = g_new0 (StatData, sizeof (StatData));

        if (total_data_received >= data_size) {
            data->stop = TRUE;
            read_all = TRUE;
            g_async_queue_push (stats, data);
            break;
        }

        data->start_time = start;
        data->end_time = end;
        data->data_received = total_data_received;
        data->stop = FALSE;
        g_async_queue_push(stats, data);
    }

    g_print ("Total data received: %ld\n", total_data_received);

    g_free (buffer);

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
    gboolean result = FALSE;
    stats = g_async_queue_new();
    
    // Register the signal handler for SIGINT (Ctrl+C)
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);
    signal(SIGINT,  handle_sigint);

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

    for (int nb_images = 10; nb_images < max_nb_images; nb_images += 10) {
        result = uca_phantom_communicate_request_images(communicator, cine, nb_images, IMG_P12L, TS_NONE, &error);
        if (!result && error != NULL) {
            g_print ("Yo there was an error: %s\n", error->message);
            g_error_free (error);
            g_object_unref (communicator);
            return FALSE;
        }

        // Start stats_thread
        GThread *stats_thread = g_thread_new("stats", stats_printer, &nb_images);

        for (i = 0; i < NUM_THREADS; i++) {
            name = g_strdup_printf("capture-%d", i);
            threads[i] = g_thread_new(name, capture_thread, &nb_images);
            g_free(name);
        }

        g_print ("T | N | T Re PKT | T Dr PKT | T Sh PKT | T Re DATA (B) | Re PKT | Dr PKT | Sh PKT | Re DATA (B) | rate (Bps) | CPU\n"); 
        
        while (!read_all) {
            sleep(0.01);
        }

        g_print ("Stopping threads\n");
        
        g_thread_join(stats_thread);
        for (i = 0; i < NUM_THREADS; i++) {
            g_thread_join(threads[i]);
        }
    }

    

    pfring_close(ring); 

    return 0;
}