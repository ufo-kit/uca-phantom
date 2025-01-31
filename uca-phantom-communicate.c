/**
 * @file uca-phantom-communicate.c
 * @brief Implementation of the UcaPhantomCommunicate class
 * @ingroup UcaPhantomCommunicate
 *
 * @author Gabriel Lefloch
 */

// Standard libraries
#include <gio/gio.h>
#include <glib-object.h>
#include <gmodule.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Accelerated image unpacking
#include <nmmintrin.h>
#include <omp.h>

// Networking
#include <linux/if.h>
#include <pcap.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "phantom-enums.h"
#include "uca-phantom-variables.h"
#include "uca-phantom-commands.h"
#include "uca-phantom-communicate.h"
#include "ringbuf.h"

// Note: if you wish to screw everything up, please tweak this macro
#define ETHERNET_HEADER_SIZE 32        // 16 bytes for L1 ethernet header, 16 bytes for custom header
#define MAX_KERNEL_BUF_SIZE 2000000000 // Maximum size of kernel buffer (uint32_t)
#define MAX_REPLY_SIZE 65536 // ref : v16.0.0

#define MAX_NETWORK_REQUEST_SIZE (USER_MAX_NETWORK_REQUEST_SIZE > MAX_KERNEL_BUF_SIZE ? MAX_KERNEL_BUF_SIZE : USER_MAX_NETWORK_REQUEST_SIZE)
#define MAX_BUFFERED_IMAGES (USER_MAX_BUFFERED_IMAGES > 40 || USER_MAX_BUFFERED_IMAGES < 2 ? 40 : USER_MAX_BUFFERED_IMAGES)
#define PCAP_TIMEOUT (USER_PCAP_TIMEOUT > 5000 || USER_PCAP_TIMEOUT < 0 ? 5000 : USER_PCAP_TIMEOUT)
#define THROTTLE_FACTOR (USER_THROTTLE_FACTOR > 1 || USER_THROTTLE_FACTOR < 0 ? .75 : USER_THROTTLE_FACTOR)
#define NUM_THREADS (USER_NUM_THREADS > 16 || USER_NUM_THREADS < 1 ? 16 : USER_NUM_THREADS)
#define USE_MEMPOOL USER_USE_MEMPOOL
#define NO_DROP USER_NO_DROP

/**
 * @defgroup NetworkStructures Network related structures
 * TODO: Add documentation
 * @{
 */
enum TerminatePhantomDiscover
{
    ALL,
    REGEX,
    RECEIVE,
    SOCKET_END,
    BCAST
};

typedef enum
{
    CONNECTED,
    DISCONNECTED
} ConnectionState;

typedef enum
{
    ACQUIRING,
    IDLE
} AcquisitionState;
/** @} */

/**
 * @defgroup PhantomStructures Phantom related structures
 * TODO: Add documentation
 * @{
 */

typedef struct
{
    gint x, y;
    guint w, h, threshold, area, speed, mode;
} Trigger;

typedef struct __attribute__((packed)) _short_time_stamp
{                           // cam.ts_format = 0
    unsigned int csecs;     // time from beginning of the year in 1/100 sec units
    unsigned short exptime; // exposure time in us
    unsigned short frac;    // bits[15..2]: fractions (us to 10000); b[1]:event;[0]:lock
} short_time_stamp;
typedef struct __attribute__((packed)) _short_time_stamp32
{                             // cam.ts_format = 1
    unsigned int csecs;       // time from beginning of the year in 1/100 sec units
    unsigned short exptime;   // exposure time in us
    unsigned short frac;      // bits[15..2]: fractions (us to 10000); b[1]:event; b[0]:lock
    unsigned short exptime32; // exposure time extension (1/65536 of a us)
    unsigned short frac32;    // time stamp extension (1/65536 of a us)
} short_time_stamp32;
typedef struct __attribute__((packed)) _long_time_stamp
{                           // cam.ts_format = 2
    unsigned int csecs;     // time from beginning of the year in 1/100 sec units
    unsigned short exptime; // exposure time in us
    unsigned short frac;    // bits[15..2]: fractions (us to 10000); bit[1]:event; bit[0]:lock
    unsigned int range_d0;  // first 32bits received as rangedata, lsb first, big endian
    unsigned int range_d1;  // second 32bits received as rangedata, lsb first, big endian
    unsigned int range_d2;  // third 32bits received as rangedata, lsb first, big endian
    unsigned int range_d3;  // fourth 32bits received as rangedata, lsb first, big endian
} long_time_stamp;
typedef struct __attribute__((packed)) _long_time_stamp32
{                             // cam.ts_format = 3
    unsigned int csecs;       // time from beginning of the year in 1/100 sec units
    unsigned short exptime;   // exposure time in us
    unsigned short frac;      // bits[15..2]: fractions (us to 10000); bit[1]:event; bit[0]:lock
    unsigned short exptime32; // exposure time extension (1/65536 of a us)
    unsigned short frac32;    // time stamp extension (1/65536 of a us)
    unsigned int range_d0;    // first 32bits received as rangedata, lsb first, big endian
    unsigned int range_d1;    // second 32bits received as rangedata, lsb first, big endian
    unsigned int range_d2;    // third 32bits received as rangedata, lsb first, big endian
    unsigned int range_d3;    // fourth 32bits received as rangedata, lsb first, big endian
} long_time_stamp32;

/** @} */

/**
 * @defgroup CommunicationStructures Phantom communication structures
 * TODO: Add documentation
 * @{
 */
struct _PhantomRequest
{
    PhantomCommand command;
    gchar *message;
    gsize size;
    gssize write_size;
};

struct _PhantomReply
{
    gchar *raw;
    GValue value;
    gsize size;
    gssize read_size;
};

typedef struct _ImageRequest
{
    gboolean end_request;
    gint start;
    guint64 nb_images;
    ImageFormat img_format;
    guint buffer_index;
    CaptureSettings settings;
    gboolean discard;
} ImageRequest;

typedef struct _TsRequest
{
    gboolean end_request;
    gint start;
    guint count;
    guint cine;
    TimestampFormat ts_format;
} TsRequest;

/**
 */
typedef struct __attribute__((packed))
{
    guint32 csecs;     // time from beginning of the year in 1/100 sec units
    guint16 exptime;   // exposure time in us
    guint16 frac;      // bits[15..2]: fractions (us to 10000); bit[1]:event; bit[0]:lock
    guint16 exptime32; // exposure time extension (1/65536 of a us)
    guint16 frac32;    // time stamp extension (1/65536 of a us)
    guint32 range_d0;  // first 32bits received as rangedata, lsb first, big endian
    guint32 range_d1;  // second 32bits received as rangedata, lsb first, big endian
    guint32 range_d2;  // third 32bits received as rangedata, lsb first, big endian
    guint32 range_d3;  // fourth 32bits received as rangedata, lsb first, big endian
    gint32 cine;
} PhantomTimestamp;
/** @} */

typedef struct
{
    guint64 trigger_time;
    guint64 exposure_time;
    guint64 shutter_close_time;
} UcaTimestamp;

typedef struct
{
    gint cine;
    gint start_index;
    guint nb_images;
    gboolean end_request;
    gboolean earlyimg;
    CaptureSettings settings;
    gboolean discard;
} CineInfo;

/**
 * @brief Image data structure
 *
 * @param width Image width
 * @param height Image height
 * @param format Image format
 * @param raw Raw image data
 * @param ts Timestamp
 *
 */
typedef struct _CineData
{
    ImageFormat ImgFormat;
    TimestampFormat ts_format;

    guint NbImages;
    guint NbPixelsPerImage;
    gsize SizePerImageRaw;
    gsize SizePerImageUnpacked;
    guint16 width;
    guint16 height;

    guint8 *RawImages;
    gpointer UnpackedImages;
    guint buffer_index;
} CineData;

typedef struct _TimestampData
{
    gint start;
    guint count;
    TimestampFormat ts_format;
    PhantomTimestamp *timestamps;
    gboolean end_request;
} TimestampData;

typedef struct Chunk
{
    guint8 *memoryloc;
    struct Chunk *next;
    guint16 nb_chunks;
    guint16 current_chunk;
} mempool_t;

mempool_t *mempool_init(gsize chunk_size, guint nb_chunks)
{
    mempool_t *pool = g_new0(mempool_t, 1);
    if (pool == NULL)
    {
        g_warning("Could not allocate memory for memory pool");
        return NULL;
    }

    mempool_t *prev_pool = pool;
    pool->memoryloc = g_malloc0(chunk_size);
    if (pool->memoryloc == NULL)
    {
        g_warning("Could not allocate memory for pool memoryloc");
        return NULL;
    }
    pool->nb_chunks = nb_chunks;
    pool->current_chunk = 0;

    for (guint i = 0; i < nb_chunks; i++)
    {
        mempool_t *next_pool = g_new0(mempool_t, 1);
        if (next_pool == NULL)
        {
            g_warning("Could not allocate memory for chunk");
            return NULL;
        }
        next_pool->memoryloc = g_malloc0(chunk_size);
        if (next_pool->memoryloc == NULL)
        {
            g_warning("Could not allocate memory for chunk memoryloc");
            return NULL;
        }
        next_pool->nb_chunks = nb_chunks;
        next_pool->current_chunk = prev_pool->current_chunk + 1;

        prev_pool->next = next_pool;
        prev_pool = next_pool;
    }

    // Loop back to the first pool
    prev_pool->next = pool;

    return pool;
}

void mempool_free(mempool_t *pool)
{
    if (pool == NULL)
    {
        return;
    }

    mempool_t *current = pool;
    mempool_t *next = NULL;
    do
    {
        next = current->next;
        g_free(current->memoryloc);
        g_free(current);
        current = next;
    } while (current != pool);
}

const ImageFormatSpec ImageFormatSpecs[] = {{"8", 8, 1}, {"8R", 8, 1}, {"P16", 16, 2}, {"P16R", 16, 2}, {"P10", 10, 1.25}, {"P12L", 12, 1.5}};

const TimestampSpec TimestampSpecs[] = {{"0", 8},
                                        {"1", 12},
                                        {"2", 24},
                                        {"3", 28}};

// Forward declaration of overrideable functions
static void uca_phantom_communicate_set_property(GObject *object, guint property_id, const GValue *value,
                                                 GParamSpec *pspec);
static void uca_phantom_communicate_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void uca_phantom_communicate_constructed(GObject *object);
static void uca_phantom_communicate_dispose(GObject *object);
static void uca_phantom_communicate_finalize(GObject *object);

static GParamSpec *uca_phantom_communicate_properties[N_COM_PROPERTIES] = {
    NULL,
};

struct _UcaPhantomCommunicate
{
    GObject parent_object;

    gboolean xenabled;
    gchar *phantom_ip, *phantom_xip;
    gchar *netcard_ip, *netcard_xip;
    gchar *netcard, *xnetcard;
    guint control_port, data_port, discovery_port;
    guint8 mac_address[6];
    gchar *mac_address_str, *request_image_string;
    IpSource phantom_ipsource;

    ConnectionState control_connection_state;
    ConnectionState data_connection_state;
    ConnectionState xdata_connection_state;
    AcquisitionState readout_state;
    AcquisitionState phantom_acquisition_state;
    AcquisitionState live_image_acquisition;
    AcquisitionState ts_acquisition_state;

    // camera setup variables
    CaptureSettings settings;

    // Command stream connection variables
    GSocketConnection *control_connection;
    GSocketClient *control_client;
    GOutputStream *output_controlstream;
    GInputStream *input_controlstream;
    GMutex control_connection_mutex;

    // Data stream connection variables (1 GbE)
    GSocketListener *service;
    GSocketConnection *data_connection;
    GInputStream *input_datastream;
    GOutputStream *output_datastream;
    GMutex data_connection_mutex;
    GCond data_connection_cond;
    GCond data_finished_connection_cond;
    gboolean finished_receiving_1gb;

    // Data stream connection variables (10 GbE)
    pcap_t *handle;
    ringbuf_t *packed_ring_buffer;
    ringbuf_t *unpacked_ring_buffer;
    ringbuf_t *ts_ring_buffer;
    GThread *xdata_receiver;
    GThread *data_unpacker;
    GThread *data_receiver;
    GThread *ts_receiver;
    GThread *request_thread;
    GPtrArray *unpacked_images;

    GAsyncQueue *packed_queue;
    GAsyncQueue *unpacked_queue;
    GAsyncQueue *request_queue;
    GAsyncQueue *ts_request_queue;
    GAsyncQueue *cine_request_queue;

    // Throttle the data transfer
    GAsyncQueue *throttle_queue;

    // Live buffering
    ringbuf_t *live_ring_buffer;
    GThread *live_images_receiver;
    GAsyncQueue *live_images_request_queue;

    guint32 irig_yearbegin;
};

G_DEFINE_TYPE(UcaPhantomCommunicate, uca_phantom_communicate, G_TYPE_OBJECT)

G_DEFINE_QUARK("uca-phantom-communicate-error-quark", uca_phantom_communicate_error)

static void uca_phantom_communicate_class_init(UcaPhantomCommunicateClass *class)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(class);

    gobject_class->set_property = uca_phantom_communicate_set_property;
    gobject_class->get_property = uca_phantom_communicate_get_property;
    gobject_class->constructed = uca_phantom_communicate_constructed;
    gobject_class->dispose = uca_phantom_communicate_dispose;
    gobject_class->finalize = uca_phantom_communicate_finalize;

    // install properties
    uca_phantom_communicate_properties[PROP_COM_PHANTOM_IP] = g_param_spec_string(
        "phantom_ip", "Phantom IP address", "IP address of the Phantom camera over normal 1Gb ethernet",
        "100.100.189.164", G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_COM_PHANTOM_XIP] = g_param_spec_string(
        "phantom_xip", "Phantom 10Gb IP address", "IP address of the Phantom camera over a 10Gb ethernet",
        "172.16.31.157", G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_COM_NETCARD_IP] = g_param_spec_string("netcard_ip", "Network card IP", "IP address of the network card used for 1Gb ethernet",
                                                                                  "100.100.100.1", G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_COM_NETCARD_XIP] = g_param_spec_string(
        "netcard_xip", "10Gb network card IP", "IP address of the network card used for 10Gb ethernet", "172.16.0.1",
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_COM_NETCARD] = g_param_spec_string("netcard", "Network card", "Name of the network card used for 1Gb ethernet", "eth0",
                                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_COM_XNETCARD] = g_param_spec_string("xnetcard", "10Gb network card", "Name of the network card used for 10Gb ethernet", "eth1",
                                                                                G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_COM_XENABLED] = g_param_spec_boolean("xenabled", "Enable 10Gb data transfer", "Enable 10Gb data transfer", TRUE,
                                                                                 G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

    uca_phantom_communicate_properties[PROP_COM_CONTROL_PORT] = g_param_spec_uint(
        "control_port", "Set connection port", "Set the port used to establish TCP connection with phantom", 1024,
        49151, 7115, G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

    uca_phantom_communicate_properties[PROP_COM_PHANTOM_IPSOURCE] = g_param_spec_enum(
        "phantom_ipsource", "Set the IP source using IP flags", "Method used to get the IP of the phatnom camera.", IP_TYPE_SOURCE,
        USE_CLASS, G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

    g_object_class_install_properties(gobject_class, N_COM_PROPERTIES, uca_phantom_communicate_properties);
}

static void uca_phantom_communicate_init(UcaPhantomCommunicate *instance)
{
    instance->phantom_ip = NULL;
    instance->phantom_xip = NULL;
    instance->netcard = NULL;
    instance->xnetcard = NULL;
    instance->netcard_ip = NULL;
    instance->netcard_xip = NULL;
    instance->xenabled = TRUE;
    instance->mac_address_str = NULL;
    instance->request_image_string = g_strdup("{cine:%d, start:%d, cnt:%d, fmt:%s %s}");
    instance->data_port = 7116;
    instance->control_port = 7115;
    instance->discovery_port = 7380;

    instance->control_connection_state = DISCONNECTED;
    instance->data_connection_state = DISCONNECTED;
    instance->xdata_connection_state = DISCONNECTED;
    instance->ts_acquisition_state = IDLE;
    instance->readout_state = IDLE;
    instance->phantom_acquisition_state = IDLE;
    instance->live_image_acquisition = IDLE;

    instance->control_client = g_socket_client_new();
    instance->control_connection = NULL;
    instance->input_controlstream = NULL;
    instance->output_controlstream = NULL;
    g_mutex_init(&instance->control_connection_mutex);
    g_mutex_init(&instance->data_connection_mutex);
    g_cond_init(&instance->data_connection_cond);
    g_cond_init(&instance->data_finished_connection_cond);
    instance->finished_receiving_1gb = FALSE;

    instance->service = g_socket_listener_new();
    instance->data_connection = NULL;
    instance->input_datastream = NULL;
    instance->output_datastream = NULL;

    instance->unpacked_images = g_ptr_array_new_with_free_func(g_free);
    instance->xdata_receiver = NULL;
    instance->data_receiver = NULL;
    instance->data_unpacker = NULL;
    instance->unpacked_queue = g_async_queue_new();
    instance->request_queue = g_async_queue_new();
    instance->ts_request_queue = g_async_queue_new();

    instance->cine_request_queue = g_async_queue_new();
    instance->throttle_queue = g_async_queue_new();

    instance->live_images_receiver = NULL;
    instance->live_images_request_queue = g_async_queue_new();

    instance->irig_yearbegin = 0;

    g_log_set_default_handler(g_log_default_handler, NULL);
}

static void uca_phantom_communicate_constructed(GObject *object)
{
    UcaPhantomCommunicate *instance = UCA_PHANTOM_COMMUNICATE(object);

    instance->control_connection_state = DISCONNECTED;

    if (instance->xenabled)
        instance->packed_queue = g_async_queue_new();
    else
        instance->packed_queue = NULL;

    G_OBJECT_CLASS(uca_phantom_communicate_parent_class)->constructed(object);
}

static void uca_phantom_communicate_dispose(GObject *object)
{
    UcaPhantomCommunicate *instance = UCA_PHANTOM_COMMUNICATE(object);

    if (instance->handle != NULL)
    {
        pcap_close(instance->handle);
    }

    g_free(instance->phantom_ip);
    g_free(instance->phantom_xip);
    g_free(instance->netcard_ip);
    g_free(instance->netcard_xip);
    g_free(instance->netcard);
    g_free(instance->xnetcard);
    g_free(instance->mac_address_str);
    g_free(instance->request_image_string);

    g_ptr_array_free(instance->unpacked_images, TRUE);

    CineData *cine_data = NULL;
    while ((cine_data = g_async_queue_try_pop(instance->packed_queue)) && cine_data != NULL)
    {
        g_free(cine_data);
    }
    cine_data = NULL;
    while ((cine_data = g_async_queue_try_pop(instance->unpacked_queue)) && cine_data != NULL)
    {
        g_free(cine_data);
    }
    ImageRequest *req = NULL;
    while ((req = g_async_queue_try_pop(instance->live_images_request_queue)) && req != NULL)
    {
        g_free(req);
    }
    CineInfo *cine_info = NULL;
    while ((cine_info = g_async_queue_try_pop(instance->cine_request_queue)) && cine_info != NULL)
    {
        g_free(cine_info);
    }
    while (g_async_queue_try_pop(instance->throttle_queue) != NULL)
    {
    }

    g_mutex_clear(&instance->control_connection_mutex);
    g_mutex_clear(&instance->data_connection_mutex);
    g_cond_clear(&instance->data_connection_cond);
    g_cond_clear(&instance->data_finished_connection_cond);

    TsRequest *ts_request = NULL;
    while ((ts_request = g_async_queue_try_pop(instance->ts_request_queue)) && ts_request != NULL)
    {
        g_free(ts_request);
    }

    if (instance->control_connection_state == CONNECTED)
    {
        g_object_unref(instance->control_client);

        GError *error = NULL;
        if (!g_io_stream_close(G_IO_STREAM(instance->control_connection), NULL, &error))
        {
            g_warning("Failed to close stream: %s", error->message);
            g_clear_error(&error);
        }
        g_object_unref(instance->control_connection);

        instance->control_connection_state = DISCONNECTED;
    }
    if (instance->data_connection_state == CONNECTED)
    {
        GError *error = NULL;
        if (!g_io_stream_close(G_IO_STREAM(instance->data_connection), NULL, &error))
        {
            g_warning("Failed to close stream: %s", error->message);
            g_clear_error(&error);
        }
        g_object_unref(instance->data_connection);
        g_socket_listener_close(instance->service);
        g_object_unref(instance->service);
    }

    if (instance->live_ring_buffer != NULL)
    {
        ringbuf_free(instance->live_ring_buffer);
    }

    G_OBJECT_CLASS(uca_phantom_communicate_parent_class)->dispose(object);
}

static void uca_phantom_communicate_finalize(GObject *object)
{
    UcaPhantomCommunicate *instance = UCA_PHANTOM_COMMUNICATE(object);

    // Free the 10GbE resources
    if (instance->packed_queue != NULL)
    {
        g_async_queue_unref(instance->packed_queue);
    }
    if (instance->unpacked_queue != NULL)
    {
        g_async_queue_unref(instance->unpacked_queue);
    }
    if (instance->request_queue != NULL)
    {
        g_async_queue_unref(instance->request_queue);
    }
    if (instance->ts_request_queue != NULL)
    {
        g_async_queue_unref(instance->ts_request_queue);
    }
    if (instance->live_images_request_queue != NULL)
    {
        g_async_queue_unref(instance->live_images_request_queue);
    }
    if (instance->cine_request_queue != NULL)
    {
        g_async_queue_unref(instance->cine_request_queue);
    }
    if (instance->throttle_queue != NULL)
    {
        g_async_queue_unref(instance->throttle_queue);
    }

    G_OBJECT_CLASS(uca_phantom_communicate_parent_class)->finalize(object);
}

static void uca_phantom_communicate_set_property(GObject *object, guint property_id, const GValue *value,
                                                 GParamSpec *pspec)
{
    UcaPhantomCommunicate *self = UCA_PHANTOM_COMMUNICATE(object);

    switch (property_id)
    {
    case PROP_COM_PHANTOM_IP:
        self->phantom_ip = g_value_dup_string(value);
        break;
    case PROP_COM_PHANTOM_XIP:
        self->phantom_xip = g_value_dup_string(value);
        break;
    case PROP_COM_NETCARD_IP:
        self->netcard_ip = g_value_dup_string(value);
        break;
    case PROP_COM_NETCARD_XIP:
        self->netcard_xip = g_value_dup_string(value);
        break;
    case PROP_COM_NETCARD:
        self->netcard = g_value_dup_string(value);
        break;
    case PROP_COM_XNETCARD:
        if (self->xnetcard != NULL)
        {
            g_free(self->xnetcard);
        }
        self->xnetcard = g_value_dup_string(value);
        break;
    case PROP_COM_XENABLED:
        self->xenabled = g_value_get_boolean(value);
        break;
    case PROP_COM_CONTROL_PORT:
        self->control_port = g_value_get_uint(value);
        break;
    case PROP_COM_PHANTOM_IPSOURCE:
        self->phantom_ipsource = g_value_get_enum(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void uca_phantom_communicate_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    UcaPhantomCommunicate *self = UCA_PHANTOM_COMMUNICATE(object);

    switch (property_id)
    {
    case PROP_COM_PHANTOM_IP:
        g_value_set_string(value, self->phantom_ip);
        break;
    case PROP_COM_PHANTOM_XIP:
        g_value_set_string(value, self->phantom_xip);
        break;
    case PROP_COM_NETCARD_IP:
        g_value_set_string(value, self->netcard_ip);
        break;
    case PROP_COM_NETCARD_XIP:
        g_value_set_string(value, self->netcard_xip);
        break;
    case PROP_COM_NETCARD:
        g_value_set_string(value, self->netcard);
        break;
    case PROP_COM_XNETCARD:
        g_value_set_string(value, self->xnetcard);
        break;
    case PROP_COM_XENABLED:
        g_value_set_boolean(value, self->xenabled);
        break;
    case PROP_COM_CONTROL_PORT:
        g_value_set_uint(value, self->control_port);
        break;
    case PROP_COM_PHANTOM_IPSOURCE:
        g_value_set_enum(value, self->phantom_ipsource);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static gchar *uca_phantom_communicate_discover(UcaPhantomCommunicate *self, GError **error_loc)
{
    g_return_val_if_fail(UCA_IS_PHANTOM_COMMUNICATE(self), NULL);
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, NULL);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;
    GSocket *socket = NULL;
    GMatchInfo *info = NULL;
    GSocketAddress *remote_socket_addr = NULL;
    GSocketAddress *result = NULL;
    const gchar request[] = "phantom?";
    const gchar pattern[] = "PH16 (\\d+) (\\d+) (\\d+)";
    gint FLAG = ALL;
    guint port = self->discovery_port;

    gchar reply[128] = {
        0,
    };

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Starting the Phantom discovery process...");

    const gchar *bcast_address = "100.100.255.255";
    GSocketAddress *bcast_socket_addr = g_inet_socket_address_new_from_string(bcast_address, port);

    if (bcast_socket_addr == NULL)
    {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_BCAST_ADDR,
                    "Failed to parse broadcasting address '%s' on port '%d'\n", bcast_address, port);
        g_propagate_error(error_loc, phantom_error);

        FLAG = BCAST;
        goto cleanup;
    }

    socket = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, &sub_error);

    if (socket == NULL)
    {
        if (sub_error == NULL)
        {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SOCKET,
                        "Failed to create socket\n");
        }
        else
        {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SOCKET,
                        "Failed to create socket: %s\n", sub_error->message);
            g_error_free(sub_error);
        }
        g_propagate_error(error_loc, phantom_error);
        FLAG = SOCKET_END;
        goto cleanup;
    }

    g_socket_set_broadcast(socket, TRUE);

    gssize wrote = g_socket_send_to(socket, bcast_socket_addr, request, strlen(request), NULL, &sub_error);

    if (wrote < -1)
    {
        if (sub_error == NULL)
        {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SEND,
                        "Failed to send broadcast\n");
        }
        else
        {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SEND,
                        "Failed to send broadcast: %s\n", sub_error->message);
            g_error_free(sub_error);
        }
        g_propagate_error(error_loc, phantom_error);

        FLAG = SOCKET_END;
        goto cleanup;
    }

    gssize received = g_socket_receive_from(socket, &remote_socket_addr, reply, strlen(reply), NULL, &sub_error);

    if (received < -1)
    {
        if (sub_error == NULL)
        {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RECEIVE,
                        "Failed to receive broadcast\n");
        }
        else
        {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RECEIVE,
                        "Failed to receive broadcast: %s\n", sub_error->message);
            g_error_free(sub_error);
        }
        g_propagate_error(error_loc, phantom_error);

        FLAG = RECEIVE;
        goto cleanup;
    }

    GRegex *regex = g_regex_new(pattern, 0, 0, &sub_error);

    if (regex == NULL)
    {
        if (sub_error == NULL)
        {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REGEX,
                        "Failed to create regex\n");
        }
        else
        {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REGEX,
                        "Failed to create regex: %s\n", sub_error->message);
            g_error_free(sub_error);
        }
        g_propagate_error(error_loc, phantom_error);

        FLAG = REGEX;
        goto cleanup;
    }

    gboolean matched = g_regex_match(regex, reply, 0, &info);

    if (!matched)
    {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REGEX,
                    "Reply '%s' does not match expected pattern.\n", reply);
        g_propagate_error(error_loc, phantom_error);

        FLAG = ALL;
        goto cleanup;
    }

    gchar *port_string = g_match_info_fetch(info, 1);

    if (port_string == NULL)
    {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REGEX,
                    "Regex pattern number 1 not found.\n");
        g_propagate_error(error_loc, phantom_error);

        FLAG = ALL;
        goto cleanup;
    }

    g_free(port_string);

    result = g_inet_socket_address_new(g_inet_socket_address_get_address((GInetSocketAddress *)remote_socket_addr), port);

    gchar *ip_address = g_inet_address_to_string(g_inet_socket_address_get_address((GInetSocketAddress *)result));

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Phantom discovered at IP address '%s' on port %d.", ip_address, port);

cleanup:
    switch (FLAG)
    {
    case ALL:
        g_match_info_free(info);
        /* fall through */
    case REGEX:
        g_regex_unref(regex);
        /* fall through */
    case RECEIVE:
        g_object_unref(remote_socket_addr);
        /* fall through */
    case SOCKET_END:
        g_object_unref(socket);
        /* fall through */
    case BCAST:
        g_object_unref(bcast_socket_addr);
        break;
    default:
        g_error("Invalid flag. Implementation error.\n");
        return NULL;
    }

    return ip_address;
}

/**
 * UcaPhantomCommunicate:
 *
 * The #UcaPhantomCommunicate struct contains only private data and should
 * only be accessed using the provided API.
 */
static gboolean uca_phantom_communicate(
    UcaPhantomCommunicate *self, GString *request, GString *reply, GError **error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(request != NULL || reply != NULL, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);
    g_return_val_if_fail(G_IS_INPUT_STREAM(self->input_controlstream), FALSE);
    g_return_val_if_fail(G_IS_OUTPUT_STREAM(self->output_controlstream), FALSE);

    GError *sub_error = NULL;
    gsize total_bytes_written = 0;
    if (!g_output_stream_write_all(self->output_controlstream, request->str, request->len, &total_bytes_written, NULL, &sub_error))
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SEND,
                    "Failed to write request to control output stream: %s\n", sub_error->message);
        g_clear_error(&sub_error);
        return FALSE;
    }

    if (total_bytes_written != request->len)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SEND,
                    "Failed to write all bytes to control output stream: %ld/%ld\n", total_bytes_written, request->len);
        return FALSE;
    }

    gssize remaining_bytes = MAX_REPLY_SIZE;
    const gsize packet_size = 4096;
    gsize total_bytes_read = 0;
    while (remaining_bytes > 0)
    {
        gchar tmp[packet_size+1];
        const gssize bytes_read = g_input_stream_read(self->input_controlstream, tmp, packet_size, NULL, error_loc);
        if (bytes_read < 0)
        {
            return FALSE;
        }
        else if (bytes_read == 0)
        {
            break;
        }
        g_string_append_len(reply, tmp, bytes_read);
        tmp[bytes_read] = '\0';
        remaining_bytes -= bytes_read;
        total_bytes_read += bytes_read;

        if (g_str_has_suffix(tmp, "\r\n"))
        {
            break;
        }
    }

    g_output_stream_flush(self->output_controlstream, NULL, NULL);
    return TRUE;
}

/*
 * Public methods
 *
 */
UcaPhantomCommunicate *uca_phantom_communicate_new(void)
{
    return g_object_new(UCA_TYPE_PHANTOM_COMMUNICATE, NULL);
}

gboolean uca_phantom_communicate_connect_controlstream(UcaPhantomCommunicate *self, GError **error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(self->control_connection_state == DISCONNECTED, FALSE);

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Connecting to the Phantom server...");

    GError *sub_error = NULL;
    gchar *ip_address = NULL;

    switch (self->phantom_ipsource)
    {
    case USE_ENV:
        ip_address = g_strdup(getenv("PHANTOM_IP"));
        break;
    case USE_CLASS:
        ip_address = g_strdup(self->phantom_ip);
        break;
    case USE_BCAST:
        ip_address = uca_phantom_communicate_discover(self, &sub_error);
        break;
    default:
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT,
                    "Invalid ip source.\n");
        return FALSE;
    }

    if (ip_address == NULL)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT,
                    "Could not get the IP address.\n");
        return FALSE;
    }
    else if (*ip_address == '\0')
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT,
                    "Could not get the IP address.\n");
        return FALSE;
    }

    self->control_connection = g_socket_client_connect_to_host(self->control_client, ip_address, self->control_port, NULL, &sub_error);

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Successfully connected to the Phantom server.");

    g_free(ip_address);

    if (self->control_connection == NULL)
    {
        if (sub_error != NULL)
        {
            g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT,
                        "Could not connect to the phantom: %s\n", sub_error->message);
        }
        else
        {
            g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT,
                        "Could not connect to the phantom: Unknown error when attempting to connect.\n");
        }

        return FALSE;
    }

    self->control_connection_state = CONNECTED;

    self->output_controlstream = g_io_stream_get_output_stream(G_IO_STREAM(self->control_connection));
    self->input_controlstream = g_io_stream_get_input_stream(G_IO_STREAM(self->control_connection));

    if (!G_IS_OUTPUT_STREAM(self->output_controlstream) || !G_IS_INPUT_STREAM(self->input_controlstream))
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT,
                    "Could not get control streams.\n");
        return FALSE;
    }

    // Get the remote address of the connection
    GSocketAddress *remote_address = g_socket_connection_get_remote_address(self->control_connection, &sub_error);
    if (sub_error != NULL || remote_address == NULL)
    {
        GError *phantom_error = NULL;
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT,
                    "Could not get remote address: %s\n", sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_clear_error(&sub_error);
        return FALSE;
    }
    GSocketAddress *local_address = g_socket_connection_get_local_address(self->control_connection, &sub_error);
    if (sub_error != NULL || local_address == NULL)
    {
        GError *phantom_error = NULL;
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT,
                    "Could not get local address: %s\n", sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_clear_error(&sub_error);
        return FALSE;
    }

    guint remote_port = g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(remote_address));
    guint local_port = g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(local_address));

    GInetAddress *remote_inet_address = g_inet_socket_address_get_address(G_INET_SOCKET_ADDRESS(remote_address));
    GInetAddress *local_inet_address = g_inet_socket_address_get_address(G_INET_SOCKET_ADDRESS(local_address));

    gchar *remote_ip_address = g_inet_address_to_string(remote_inet_address);
    gchar *local_ip_address = g_inet_address_to_string(local_inet_address);

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG,
          "Established control connection from local %s:%d to phantom %s:%d.",
          local_ip_address, local_port, remote_ip_address, remote_port);

    g_object_unref(remote_address);
    g_object_unref(local_address);

    g_free(remote_ip_address);
    g_free(local_ip_address);

    return TRUE;
}

GString *uca_phantom_communicate_run_command(UcaPhantomCommunicate *self, guint command_flag, gchar *command_arg,
                                             GError **error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(command_flag < N_UNIT_COMMANDS, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);

    // Setup the request
    PhantomCommand command = Commands[command_flag];
    command_arg = command_arg == NULL ? "" : command_arg;

    gsize request_size = (strlen(command.name) + 1 + strlen(command_arg) + strlen("\r\n")) * sizeof(gchar);
    GString *request_message = g_string_new_len(NULL, request_size + 1);
    if (request_message == NULL)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RUN_COMMAND,
                    "Could not allocate request message. Fatal error.\n");
        return NULL;
    }
    g_string_append_printf(request_message, "%s %s\r\n", command.name, command_arg);

    GString *reply_message = g_string_new_len(NULL, MAX_REPLY_SIZE);
    if (reply_message == NULL)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RUN_COMMAND,
                    "Could not allocate reply message. Fatal error.\n");
        return NULL;
    }

    g_log(DEBUG, G_LOG_LEVEL_DEBUG, "> request:\n%s \n", request_message->str);
    gboolean error_occurred = FALSE;

    // Communicate request to phantom
    g_mutex_lock(&self->control_connection_mutex);

    GError *sub_error = NULL;
    if (!uca_phantom_communicate(self, request_message, reply_message, &sub_error))
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RUN_COMMAND,
                    "Failed to run command %s: %s\n", command.name, sub_error->message);
        g_clear_error(&sub_error);
        error_occurred = TRUE;
        goto cleanup;
    }

    g_log(DEBUG, G_LOG_LEVEL_DEBUG, "> reply:\n%s \n", reply_message->str);

    if (g_strrstr(reply_message->str, "ERR") != NULL)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RUN_COMMAND,
                    "Phantom returned an error when running command '%s': %s\n", command.name, reply_message->str);
        error_occurred = TRUE;
        goto cleanup;
    }

    if (command_flag == CMD_GET_TIMESTAMPS || command_flag == CMD_GET_IMAGES)
    {
        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Waiting for 1GB line data to be received...\n");
        while (!self->finished_receiving_1gb)
        {
            g_cond_wait(&self->data_finished_connection_cond, &self->control_connection_mutex);
        }
        self->finished_receiving_1gb = FALSE;
    }

    cleanup:
    g_mutex_unlock(&self->control_connection_mutex);
    g_string_free(request_message, TRUE);

    // g_str_to_ascii
    gchar *corrected = g_strcompress(reply_message->str);
    g_string_assign(reply_message, corrected);

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Command '%s' completed successfully.", command.name);

    if (error_occurred)
    {
        return NULL;
    }
    
    return reply_message;
}

gboolean uca_phantom_communicate_get_variable(UcaPhantomCommunicate *self, guint variable_flag, gint cine, GValue *return_value,
                                              GError **error_loc)
{
    g_return_val_if_fail(variable_flag < N_CINE_PROPERTIES, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);

    gchar pattern[] = "\\s:\\s";
    gchar *command_arg = NULL;

    if (variable_flag >= UNIT_USETS_VALID_NUMBER && variable_flag <= UNIT_C_META_GPS)
    {
        command_arg = g_strdup_printf(variables[variable_flag].name, cine);
    }
    else
    {
        command_arg = g_strdup(variables[variable_flag].name);
    }

    GError *sub_error = NULL;
    GString *reply = uca_phantom_communicate_run_command(self, CMD_GET, command_arg, &sub_error);
    g_free(command_arg);
    if (reply == NULL)
    {
        g_propagate_error(error_loc, sub_error);
        return FALSE;
    }

    // Extract the actual data from the raw reply
    GRegex *regex = g_regex_new(pattern, 0, 0, &sub_error);
    if (regex == NULL)
    {
        g_propagate_error(error_loc, sub_error);
        g_string_free(reply, TRUE);
        return FALSE;
    }

    gchar **matched = g_regex_split(regex, reply->str, 0);
    if (matched == NULL)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_VARIABLE,
                    "Failed to extract variable value from reply: %s\n", reply->str);
        g_string_free(reply, TRUE);
        g_regex_unref(regex);
        return FALSE;
    }
    gchar *prefix = matched[0];
    gchar *suffix = matched[1];

    if (suffix == NULL || prefix == NULL)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_VARIABLE,
                    "Failed to extract variable value from reply: %s\n", reply->str);
        g_string_free(reply, TRUE);
        g_strfreev(matched);
        g_regex_unref(regex);
        return FALSE;
    }

    // Check for error mesage from phantom
    if (g_str_has_prefix(prefix, "ERR"))
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_VARIABLE,
                    "Invalid phantom command: %s\n", reply->str);
        g_string_free(reply, TRUE);
        g_strfreev(matched);
        g_regex_unref(regex);
        return FALSE;
    }

    g_value_unset(return_value);
    g_value_init(return_value, variables[variable_flag].type);

    // Use Gvalue container to store it
    switch (variables[variable_flag].type)
    {
    case G_TYPE_STRING:
        g_value_set_string(return_value, suffix);
        break;
    case G_TYPE_UINT:
        g_value_set_uint(return_value, strtoul(suffix, NULL, 0));
        break;
    case G_TYPE_INT:
        g_value_set_int(return_value, atoi(suffix));
        break;
    case G_TYPE_FLOAT:
        g_value_set_float(return_value, strtof(suffix, NULL));
        break;
    case G_TYPE_DOUBLE:
        g_value_set_double(return_value, strtod(suffix, NULL));
        break;
    case G_TYPE_BOOLEAN:
        g_value_set_boolean(return_value, g_ascii_strtoull(suffix, NULL, 10));
        break;
    // // TODO : handle these cases in a more custom way in the future ?
    // case PHANTOM_TYPE_HEX:
    // case PHANTOM_TYPE_RES:
    default:
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_VARIABLE,
                    "Unsupported variable type: %s\n", g_type_name(variables[variable_flag].type));
        break;
    }

    // Cleanup
    g_strfreev(matched);
    matched = NULL;
    g_regex_unref(regex);
    regex = NULL;
    g_string_free(reply, TRUE);
    reply = NULL;

    return TRUE;
}

gboolean uca_phantom_communicate_set_variable(UcaPhantomCommunicate *self, guint variable_flag, const gchar *value,
                                              GError **error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(variable_flag < N_UNIT_PROPERTIES, FALSE);
    g_return_val_if_fail(value != NULL, FALSE);
    g_return_val_if_fail(variables[variable_flag].flags & G_PARAM_WRITABLE, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);

    GError *sub_error = NULL;
    gchar *arg = g_strdup_printf("%s %s", variables[variable_flag].name, value);
    GString *reply = uca_phantom_communicate_run_command(self, CMD_SET, arg, &sub_error);
    if (reply == NULL)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_VARIABLE,
                    "Failed to set variable '%s':\n\t> %s\n", variables[variable_flag].name, sub_error->message);
        g_clear_error(&sub_error);
        return FALSE;
    }
    
    g_free(arg);
    arg = NULL;

    g_string_free(reply, TRUE);

    return TRUE;
}

gboolean uca_phantom_communicate_get_resolution(UcaPhantomCommunicate *self, guint16 *width, guint16 *height,
                                                GError **error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;
    const gchar *pattern = "([0-9]+)\\sx\\s([0-9]+)";
    GValue resolution = G_VALUE_INIT;
    const gchar *reply = NULL;

    gboolean res = uca_phantom_communicate_get_variable(self, UNIT_DEFC_RES, 0, &resolution, &sub_error);

    if (res != TRUE && sub_error != NULL)
    {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_RESOLUTION,
                    "Failed to get resolution from phantom. Aborting...: %s\n", sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_clear_error(&sub_error);
        return FALSE;
    }

    reply = g_value_get_string(&resolution);

    // Extract the actual data from the raw reply
    GRegex *regex = g_regex_new(pattern, 0, 0, &sub_error);

    if (regex == NULL)
    {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_RESOLUTION,
                    "Failed to create regex object. Aborting...: %s\n", sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_clear_error(&sub_error);
        g_value_unset(&resolution);
        return FALSE;
    }

    gchar **matched = g_regex_split(regex, reply, 0);
    g_return_val_if_fail(matched != NULL, FALSE);

    // Check for error mesage from phantom
    if (g_str_has_prefix(matched[0], "ERR"))
    {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_VARIABLE,
                    "Invalid phantom command: %s\n", reply);
        g_propagate_error(error_loc, phantom_error);

        g_strfreev(matched);
        g_regex_unref(regex);
        return FALSE;
    }

    gchar *width_s = matched[1];
    gchar *height_s = matched[2];

    *width = g_ascii_strtoull(width_s, NULL, 10);
    *height = g_ascii_strtoull(height_s, NULL, 10);

    g_strfreev(matched);
    g_regex_unref(regex);
    g_value_unset(&resolution);

    return TRUE;
}

/**
 * @brief Helpher function to set the number of cines.
 *
 * This function sets the number of cines in the Phantom camera using
 * set_variable function.
 *
 * @param self A pointer to the UcaPhantomCommunicate object.
 * @param nb_cines The number of cines to set.
 * @param error_loc A pointer to a GError object to store any errors that occur.
 *
 * @return TRUE if the number of cines was set successfully, FALSE otherwise.
 */
gboolean uca_phantom_communicate_set_nb_cines(UcaPhantomCommunicate *self, guint nb_cines, GError **error_loc)
{
    // Set the number of cines as well
    GError *sub_error = NULL;
    gchar *arg = g_strdup_printf("%d", nb_cines);
    GString *reply = uca_phantom_communicate_run_command(self, CMD_PARTITION_CINE_MEMORY, arg, &sub_error);
    g_free(arg);
    if (reply == NULL)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_NB_CINES,
                    "Failed to set number of cines: %s\n", sub_error->message);
        g_clear_error(&sub_error);
        return FALSE;
    }

    g_string_free(reply, TRUE);
    return TRUE;
}

gboolean uca_phantom_communicate_connect_datastream(UcaPhantomCommunicate *self, GError **error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(self->data_connection_state != CONNECTED, TRUE);
    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Attempting to allow the phantom to connect to port: %d\n", self->data_port);

    GError *sub_error = NULL;
    if (!g_socket_listener_add_inet_port(G_SOCKET_LISTENER(self->service), self->data_port, NULL, &sub_error))
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_DATASTREAM,
                    "Failed to listen on port %d:\n\t> %s\n", self->data_port, sub_error->message);
        g_clear_error(&sub_error);
        return FALSE;
    }
    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Listening on port %d\n", self->data_port);

    // Send the request to connect to the datastream
    gchar *arg = g_strdup_printf("{port:%d}", self->data_port);
    GString *reply = uca_phantom_communicate_run_command(self, CMD_START_DATA_CONNECTION, arg, &sub_error);
    g_free(arg);

    if (reply == NULL)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_DATASTREAM,
                    "Failed to connect to datastream on port %d:\n\t> %s\n", self->data_port, sub_error->message);
        g_clear_error(&sub_error);
        return FALSE;
    }

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Waiting for connection...\n");

    // Set the data connection
    self->data_connection = g_socket_listener_accept(G_SOCKET_LISTENER(self->service), NULL, NULL, &sub_error);

    // Get the input and output streams
    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Connection established!\n");

    // Set the input and output streams
    self->input_datastream = g_io_stream_get_input_stream(G_IO_STREAM(self->data_connection));
    self->output_datastream = g_io_stream_get_output_stream(G_IO_STREAM(self->data_connection));

    // Get the remote and local address of the connection
    GSocketAddress *remote_address = g_socket_connection_get_remote_address(self->data_connection, NULL);
    GSocketAddress *local_address = g_socket_connection_get_local_address(self->data_connection, NULL);

    guint remote_port = g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(remote_address));
    guint local_port = g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(local_address));

    GInetAddress *remote_inet_address = g_inet_socket_address_get_address(G_INET_SOCKET_ADDRESS(remote_address));
    GInetAddress *local_inet_address = g_inet_socket_address_get_address(G_INET_SOCKET_ADDRESS(local_address));

    gchar *remote_ip_address = g_inet_address_to_string(remote_inet_address);
    gchar *local_ip_address = g_inet_address_to_string(local_inet_address);

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG,
          "Established data connection from local %s:%d to phantom %s:%d.",
          local_ip_address, local_port, remote_ip_address, remote_port);

    self->data_connection_state = CONNECTED;
    g_object_unref(remote_address);
    g_object_unref(local_address);
    g_free(remote_ip_address);
    g_free(local_ip_address);
    g_string_free(reply, TRUE);


    return TRUE;
}

gboolean uca_phantom_communicate_connect_xdatastream(UcaPhantomCommunicate *self, GError **error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);

    GError *phantom_error = NULL;

    char errbuf[PCAP_ERRBUF_SIZE];
    struct bpf_program fp;

    // Use libpcap to capture ethernet frames from the NIC called "self->xnetcard"

    // Open the device for live capture
    self->handle = pcap_create(self->xnetcard, errbuf);
    if (self->handle == NULL)
    {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_XDATASTREAM,
                    "Failed to create pcap handle:\n\t> %s\n", errbuf);
        g_propagate_error(error_loc, phantom_error);
        return FALSE;
    }
    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Opening device %s for packet capture\n", self->xnetcard);

    // Set the capture options
    pcap_set_snaplen(self->handle, 2048); // packets seem to be of size 1504
    pcap_set_promisc(self->handle, FALSE);
    pcap_set_timeout(self->handle, PCAP_TIMEOUT);
    pcap_set_rfmon(self->handle, FALSE);

    pcap_set_buffer_size(self->handle, MAX_KERNEL_BUF_SIZE);
    pcap_set_immediate_mode(self->handle,
                            TRUE); // Set the capture mechanism to PACKET_MMAP

    // Activate the capture self->handle
    if (pcap_activate(self->handle) == -1)
    {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_XDATASTREAM,
                    "Failed to activate pcap handle:\n\t> %s\n", pcap_geterr(self->handle));
        g_propagate_error(error_loc, phantom_error);

        pcap_close(self->handle);
        self->handle = NULL;
        return FALSE;
    }
    // Compile the filter to capture packets with ethertype 0x88b7
    if (pcap_compile(self->handle, &fp, "ether proto 0x88b7", 1, PCAP_NETMASK_UNKNOWN) == -1)
    {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_XDATASTREAM,
                    "Error compiling filter:\n\t> %s\n", pcap_geterr(self->handle));
        g_propagate_error(error_loc, phantom_error);

        pcap_close(self->handle);
        self->handle = NULL;
        return FALSE;
    }
    if (pcap_setfilter(self->handle, &fp) == -1)
    {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_XDATASTREAM,
                    "Error settings filter on pcap handle:\n\t> %s\n", pcap_geterr(self->handle));
        g_propagate_error(error_loc, phantom_error);

        pcap_close(self->handle);
        self->handle = NULL;
        return FALSE;
    }
    pcap_freecode(&fp);

    self->xdata_connection_state = CONNECTED;

    return TRUE;
}

// gboolean uca_phantom_communicate_disconnect_datastream(UcaPhantomCommunicate *self, GError **error_loc)
// {
//     // First check if started readout
//     g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
//     g_return_val_if_fail(G_IS_INPUT_STREAM(self->input_datastream), FALSE);
//     g_return_val_if_fail(G_IS_SOCKET_CONNECTION(self->data_connection), FALSE);

//     GError *sub_error = NULL;
//     GError *phantom_error = NULL;

//     // Data stream is ended when the socket is closed
//     g_input_stream_close(self->input_datastream, NULL, &sub_error);
//     if (sub_error != NULL)
//     {
//         g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_DISCONNECT_DATASTREAM,
//                     "Failed to close input stream:\n\t> %s\n", sub_error->message);
//         g_propagate_error(error_loc, phantom_error);
//         g_clear_error(&sub_error);
//         return FALSE;
//     }

//     self->data_connection_state = DISCONNECTED;

//     return TRUE;
// }

gboolean uca_phantom_communicate_disconnect_xdatastream(UcaPhantomCommunicate *self, GError **error_loc)
{
    // First check if started readout
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(G_IS_INPUT_STREAM(self->input_datastream), FALSE);
    g_return_val_if_fail(G_IS_SOCKET_CONNECTION(self->data_connection), FALSE);

    pcap_close(self->handle);
    self->handle = NULL;

    self->xdata_connection_state = DISCONNECTED;

    return TRUE;
}

static gboolean uca_phantom_communicate_get_mac_address(UcaPhantomCommunicate *self, GError **error_loc)
{
    g_return_val_if_fail(UCA_IS_PHANTOM_COMMUNICATE(self), FALSE);

    GError *phantom_error = NULL;

// Get the MAC address on Linux platform
#ifdef __linux__
    struct ifreq ifr = {
        0,
    };

    // Open a socket for the ioctl call
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_MAC_ADDRESS,
                    "Failed to open socket");
        g_propagate_error(error_loc, phantom_error);
        return FALSE;
    }

    // Set the interface name using g_strlcpy
    g_strlcpy(ifr.ifr_name, self->xnetcard, IFNAMSIZ);

    // Get the MAC address
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0)
    {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_MAC_ADDRESS,
                    "Failed to get MAC address");
        g_propagate_error(error_loc, phantom_error);
        close(fd);
        return FALSE;
    }

    // Copy the MAC address to the self->mac_address
    // memcpy(self->mac_address, ifr.ifr_hwaddr.sa_data, 6);
    self->mac_address[0] = ifr.ifr_hwaddr.sa_data[0];
    self->mac_address[1] = ifr.ifr_hwaddr.sa_data[1];
    self->mac_address[2] = ifr.ifr_hwaddr.sa_data[2];
    self->mac_address[3] = ifr.ifr_hwaddr.sa_data[3];
    self->mac_address[4] = ifr.ifr_hwaddr.sa_data[4];
    self->mac_address[5] = ifr.ifr_hwaddr.sa_data[5];

    // Close the socket
    close(fd);
#endif

    // // Get the MAC address on Windows platform
    // #elif _WIN32
    //     // Get the MAC address
    //     IP_ADAPTER_INFO AdapterInfo[16];
    //     DWORD dwBufLen = sizeof(AdapterInfo);
    //     DWORD dwStatus = GetAdaptersInfo(AdapterInfo, &dwBufLen);
    //     if (dwStatus != ERROR_SUCCESS) {
    //         g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR,
    //         UCA_PHANTOM_COMMUNICATE_ERROR_GET_MAC_ADDRESS, "Failed to get MAC
    //         address"); g_propagate_error (error_loc, phantom_error); return
    //         FALSE;
    //     }

    //     // Find the MAC address of the interface
    //     PIP_ADAPTER_INFO pAdapterInfo = AdapterInfo;
    //     do {
    //         if (g_strcmp0 (pAdapterInfo->AdapterName, self->interface) == 0) {
    //             break;
    //         }
    //         pAdapterInfo = pAdapterInfo->Next;
    //     } while (pAdapterInfo);

    //     // Check if the interface was found
    //     if (pAdapterInfo == NULL) {
    //         g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR,
    //         UCA_PHANTOM_COMMUNICATE_ERROR_GET_MAC_ADDRESS, "Failed to find
    //         interface"); g_propagate_error (error_loc, phantom_error); return
    //         FALSE;
    //     }

    return TRUE;
}

gboolean uca_phantom_communicate_request_live_images(UcaPhantomCommunicate *self, CaptureSettings settings, GError **error_loc)
{
    g_return_val_if_fail(UCA_IS_PHANTOM_COMMUNICATE(self), FALSE);
    g_return_val_if_fail(self->live_image_acquisition == ACQUIRING, FALSE);

    gchar *request_format = NULL, *additional = NULL;

    // printf ("Resolution: %dx%d\n", settings.sensor_width, settings.sensor_height);

    guint count = 1;
    guint start = 0;
    guint cine = -1;
    guint image_format = settings.image_format;

    // Setup the arguments for image transfer on 1Gb ethernet
    additional = g_strdup("");
    request_format = g_strdup_printf(self->request_image_string, cine, start, count,
                                     ImageFormatSpecs[image_format].format_string, additional);
    if (request_format == NULL)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
                    "Failed to allocate memory for get_ximages command");
        g_free(additional);
        return FALSE;
    }
    guint command = CMD_GET_IMAGES;

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Asked for live images\n");

    // Create internal request
    ImageRequest *request = g_new0(ImageRequest, 1);
    request->start = start;
    request->nb_images = count;
    request->img_format = settings.image_format;
    request->settings = settings;
    request->end_request = FALSE;
    request->discard = FALSE;

    // Push request to request queue
    g_async_queue_push(self->live_images_request_queue, request);

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Requesting live images\n");

    // Request the datatransfer
    GString *reply = uca_phantom_communicate_run_command(self, command, request_format, error_loc);

    if (reply == NULL)
    {
        g_free(additional);
        return FALSE;
    }

    g_free(request_format);
    g_free(additional);
    g_string_free(reply, TRUE);

    return TRUE;
}

gboolean uca_phantom_communicate_release_cine(UcaPhantomCommunicate *self, gint cine, GError **error_loc)
{
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);

    GError *sub_error = NULL;
    gchar *cine_str = g_strdup_printf("%d", cine);
    GString *reply = uca_phantom_communicate_run_command(self, CMD_RELEASE_A_CINE, cine_str, &sub_error);
    g_free(cine_str);

    if (reply == NULL)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING,
                    "Failed to arm:\n\t> %s\n", sub_error->message);
        g_clear_error(&sub_error);
        return FALSE;
    }

    g_string_free(reply, TRUE);
    return TRUE;
}

gboolean uca_phantom_communicate_delete_cine(UcaPhantomCommunicate *self, gint cine, GError **error_loc)
{
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);

    GError *sub_error = NULL;
    gchar *cine_str = g_strdup_printf("%d", cine);
    GString *reply = uca_phantom_communicate_run_command(self, CMD_DELETE_A_CINE, cine_str, &sub_error);
    g_free(cine_str);

    if (reply == NULL)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING,
                    "Failed to arm:\n\t> %s\n", sub_error->message);
        g_clear_error(&sub_error);
        return FALSE;
    }

    g_string_free(reply, TRUE);
    return TRUE;
}

gboolean uca_phantom_communicate_arm(UcaPhantomCommunicate *self, gint cine, GError **error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Arming cine %d\n", cine);

    GError *sub_error = NULL;
    gchar *cine_str = g_strdup_printf("%d", cine);
    GString *reply = uca_phantom_communicate_run_command(self, CMD_START_RECORDING_IN_A_CINE, cine_str, &sub_error);
    g_free(cine_str);
    if (reply == NULL)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING,
                    "Failed to arm:\n\t> %s\n", sub_error->message);
        g_clear_error(&sub_error);
        return FALSE;
    }


    // Update the cine number
    if (cine > 0)
    {
        self->settings.current_cine = cine;
    }
    // Update the acquisition state
    self->phantom_acquisition_state = ACQUIRING;

    // if (cine == -1)
    // self->live_images_requester = g_thread_new("live_images_requester", (GThreadFunc)uca_phantom_communicate_request_live_images, self);

    // // Call notify command to be asynchronously updated on when the cine is ready
    // GString *reply = uca_phantom_communicate_run_command (self, CMD_ENABLE_STATUS_CHANGE_NOTIFICATIONS, "111", NULL, &sub_error);
    // if (res != TRUE && phantom_error != NULL) {
    //     g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING,
    //         "Failed to enable status change notifications:\n\t> %s\n", sub_error->message);
    //     g_propagate_error(error_loc, phantom_error);
    //     g_clear_error(&sub_error);
    //     return FALSE;
    // }

    g_string_free(reply, TRUE);
    return TRUE;
}

gboolean uca_phantom_communicate_disarm(UcaPhantomCommunicate *self, GError **error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);
    g_return_val_if_fail(self->phantom_acquisition_state == ACQUIRING, FALSE);

    self->phantom_acquisition_state = IDLE;

    return TRUE;
}

GString *uca_phantom_communicate_get_cine_state (UcaPhantomCommunicate *self, gint cine, GError **error_loc) {
    GValue flags = G_VALUE_INIT;

    if (!uca_phantom_communicate_get_variable(self, UNIT_C_STATE, cine, &flags, error_loc)){
        return FALSE;
    }

    GString *flags_string = g_string_new(g_value_get_string(&flags));
    g_value_unset(&flags);

    return flags_string;
}

gboolean uca_phantom_communicate_verify_cine_state(UcaPhantomCommunicate *self, gint cine, gchar *flag, GError **error_loc)
{
    GString *flags_str = uca_phantom_communicate_get_cine_state(self, cine, error_loc);
    gchar *needle = g_strrstr(flags_str->str, flag);

    g_string_free(flags_str, TRUE);
    return (needle != NULL);
}

gboolean uca_phantom_communicate_get_cine_index(UcaPhantomCommunicate *self, gint cine, guint prop, gint *res, GError **error_loc)
{
    if (prop != UNIT_C_FRCOUNT && prop != UNIT_C_FIRSTFR && prop != UNIT_C_LASTFR)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_CINE_INDEX,
                    "Only frcount, firstfr and lastfr are allowed.\n");
        return FALSE;
    }

    // TODO
    // set error
    GValue index = G_VALUE_INIT;
    if (!uca_phantom_communicate_get_variable(self, prop, cine, &index, error_loc))
    {
        const gchar *prop_str = NULL;

        if (prop == UNIT_C_FRCOUNT) {
            prop_str = "frcount";
        }
        else if (prop == UNIT_C_FIRSTFR){
            prop_str = "firstfr";
        }
        else if (prop == UNIT_C_LASTFR){
            prop_str = "lastfr";
        }
        
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_CINE_INDEX, "Failed to get %s\n", prop_str);
        return FALSE;
    }

    if (prop == UNIT_C_FRCOUNT)
    {
        *res = (gint)g_value_get_uint(&index);
        g_value_unset(&index);
        return TRUE;
    }

    *res = g_value_get_int(&index);
    g_value_unset(&index);

    return TRUE;
}

gboolean uca_phantom_communicate_get_active_cine (UcaPhantomCommunicate *self, gint *cine, GError **error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);

    GString *reply = uca_phantom_communicate_run_command(self, CMD_GET_CINE_STATES, NULL, error_loc);
    if (reply == NULL)
    {
        return FALSE;
    }

    // Extract the actual data from the raw reply
    const gchar *format = "c(\\d+): \\{([^}]+)\\}";
    GRegex *regex = g_regex_new(format, 0, 0, error_loc);
    if (regex == NULL)
    {
        g_string_free(reply, TRUE);
        return FALSE;
    }

    GMatchInfo *match_info;
    g_regex_match(regex, reply->str, 0, &match_info);

    while (g_match_info_matches(match_info))
    {
        gchar *cine_str = g_match_info_fetch(match_info, 1);
        gint cine_nb = atoi(cine_str);

        gchar *flags_str = g_match_info_fetch(match_info, 2);

        if (g_strrstr(flags_str, "ACT") != NULL)
        {
            *cine = cine_nb;
            g_free(cine_str);
            g_free(flags_str);
            g_match_info_free(match_info);
            g_string_free(reply, TRUE);
            return TRUE;
        }

        g_free(cine_str);
        g_free(flags_str);
        g_match_info_next(match_info, NULL);
    }

    g_match_info_free(match_info);
    g_string_free(reply, TRUE);
    g_regex_unref(regex);

    return TRUE;
}

gboolean uca_phantom_communicate_trigger(UcaPhantomCommunicate *self, GError **error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);

    GError *sub_error = NULL;

    GString *reply = uca_phantom_communicate_run_command(self, CMD_SOFTWARE_TRIGGER, NULL, &sub_error);
    if (reply == NULL)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING,
                    "Failed to trigger:\n\t> %s\n", sub_error->message);
        g_clear_error(&sub_error);
        return FALSE;
    }

    g_string_free(reply, TRUE);

    return TRUE;
}

gboolean throttled_requester(UcaPhantomCommunicate *self, CineInfo *info, GError **error_loc)
{
    CaptureSettings settings = info->settings;

    // The maximum number of images that can be requested in a single request
    guint nb_pixels = settings.sensor_width * settings.sensor_height;
    gsize ImageSize = nb_pixels * ImageFormatSpecs[settings.image_format].byte_depth;

    gsize frame_size = ImageSize + ETHERNET_HEADER_SIZE;
    guint MaxNumberImagesPerRequest = MAX_NETWORK_REQUEST_SIZE / frame_size;
    gchar *imgfmt_str = ImageFormatSpecs[settings.image_format].format_string;
    gchar *additional = NULL;
    gdouble period = 1.0 / settings.frames_per_second;
    guint cmd_type = 0;

    if (settings.image_format == IMG_P10 || settings.image_format == IMG_P12L)
    {
        cmd_type = CMD_GET_XIMAGES;
        // gchar *mac = info->discard ? "000000000000" : self->mac_address_str;
        additional = g_strdup_printf(", dest:%s, from:%d", self->mac_address_str, 0);
    }
    else
    {
        cmd_type = CMD_GET_IMAGES;
        additional = g_strdup_printf(" ");
    }

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Thread %p: Image throttler starting to request image group. Start index: %d, count: %d, cine: %d\n", g_thread_self(), info->start_index, info->nb_images, info->cine);

    /**
     * If earlyimg, wait for frame 0 to exist then request the images.
     * If not, then we wait for the whole cine to be finished recording
     */
    gint first_frame = 1; // First frame must be negative
    gint16 safety_counter = 0;
    while (!(first_frame <= 0) && ++safety_counter >= 0) {

        g_usleep(period * G_USEC_PER_SEC);

        if (!uca_phantom_communicate_get_cine_index(self, info->cine, UNIT_C_FIRSTFR, &first_frame, error_loc))
        {
            g_warning("Failed to get first frame of cine %d\n", info->cine);
            return FALSE;
        }
        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t\t>Thread %p: Image throttler: first frame %d of cine %d: \n", g_thread_self(), first_frame, info->cine);
    }
    
    safety_counter = 0;
    while (!info->earlyimg && ++safety_counter > 0 &&
            !uca_phantom_communicate_verify_cine_state(self, info->cine, "STR", error_loc)) {
        if (*error_loc != NULL){
            return FALSE;
        }

        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Thread %p: Image throttler: waiting for cine %d to be completed\n", g_thread_self(), info->cine);
        g_usleep(period * G_USEC_PER_SEC);
    }

    // safety_counter = 0;
    // guint min_count = info->nb_images / 2;
    guint frame_count = 0;
    // do {
    //     if (!uca_phantom_communicate_get_cine_index(self, info->cine, UNIT_C_FRCOUNT, &frame_count, error_loc))
    //     {
    //         return FALSE;
    //     }
    //     g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t\t>Thread %p: Image throttler: frame count: %d\n", g_thread_self(), frame_count);
    //     g_usleep(period * G_USEC_PER_SEC);
    // } while (frame_count < min_count && ++safety_counter > 0);

    guint total = 0, start = info->start_index;
    guint left = info->nb_images;
    while (total < info->nb_images)
    {
        guint count = MIN(left, MaxNumberImagesPerRequest);
        
        guint16 safety_counter = 0;
        guint frame_count = 0;
        do {
            if (!uca_phantom_communicate_get_cine_index(self, info->cine, UNIT_C_FRCOUNT, &frame_count, error_loc))
            {
                return FALSE;
            }
            g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t\t>Thread %p: Image throttler: frame count: %d\n", g_thread_self(), frame_count);
            g_usleep(period * G_USEC_PER_SEC);
        } while (frame_count < count && ++safety_counter > 0);

        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Thread %p: Image throttler: %d images to request\n", g_thread_self(), count);

        // Ask for images
        gchar *request_format = g_strdup_printf(self->request_image_string, info->cine, start, count,
                                                imgfmt_str, additional);
        if (request_format == NULL)
        {
            g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
                        "Failed to allocate memory for get_ximages command");
            g_free(additional);
            return FALSE;
        }

        // Create internal request
        ImageRequest *request = g_new0(ImageRequest, 1);
        request->start = start;
        request->nb_images = count;
        request->img_format = settings.image_format;
        request->settings = settings;
        request->end_request = FALSE;
        request->discard = info->discard;

        // Push request to request queue
        g_async_queue_push(self->request_queue, request);

        left -= count;
      
        // Request the datatransfer
        GString *reply = uca_phantom_communicate_run_command(self, cmd_type, request_format, error_loc);
        g_free(request_format);
        request_format = NULL;
        if (reply == NULL)
        {
            g_free(additional);
            return FALSE;
        }
        g_string_free(reply, TRUE);

        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Thread %p: Image throttler: requested %d images from cine %d\n", g_thread_self(), count, info->cine);

        // This does the actual throttling
        if (!info->discard)
        {
            guint64 time = g_get_monotonic_time();
#if NO_DROP
            guint reduced_count = count * THROTTLE_FACTOR;
            for (guint i = 0; i < reduced_count; i++)
            {
                g_async_queue_pop(self->throttle_queue);
            }
            // empty the queue
            while (g_async_queue_try_pop(self->throttle_queue) != NULL)
                ;
#else
            g_async_queue_pop(self->throttle_queue);
#endif

#ifdef PERFORMANCE
            guint64 time_ms = (g_get_monotonic_time() - time) / 1000;
            gfloat rate = ((gfloat)count * ImageSize) / (time_ms) * 1e-3;
            g_log(PERFORMANCE, G_LOG_LEVEL_DEBUG, "\t>Thread %p: Image throttler: transfer rate: %.2f MB/s\n", g_thread_self(), rate);
#endif
            total += count;
            start += count;
        }
        else {
            break;
        }
    }

    g_free(additional);
    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Thread %p: Image throttler: finished requesting image group\n", g_thread_self());

    return TRUE;
}

/**
 * The Phantom camera can issue 2Gb of data per request at most.
 * Therefore, we need to split the request into multiple requests if the number of images
 * exceeds this limit. See docs for proof.
 * Moreover, we are limited by the readout speed of the user. A solution is to throttle the
 * requests to the readout speed of the user. The network buffer can only hold 2Gb of data.
 */
gpointer uca_phantom_communicate_request_images_thread(gpointer data)
{
    UcaPhantomCommunicate *self = UCA_PHANTOM_COMMUNICATE(data);
    GError *sub_error = NULL;

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Thread %p: Image requester thread started\n", g_thread_self());

    while (TRUE)
    {
        // Receive a cine request
        CineInfo *cine_info = g_async_queue_pop(self->cine_request_queue);

        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Thread %p: Received cine request\n", g_thread_self());

        if (cine_info->end_request)
        {
            g_free(cine_info);

            // Push end request to the queue to unblock the xdata_receiver thread
            ImageRequest *request = g_new0(ImageRequest, 1);
            request->end_request = TRUE;
            request->nb_images = 0;
            request->img_format = 0;
            request->settings = self->settings;
            request->discard = FALSE;

            g_async_queue_push(self->request_queue, request);

            break;
        }

        if (!throttled_requester(self, cine_info, &sub_error))
        {
            GError *error_loc = g_error_new(UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
                                            "Failed to request images:\n\t> %s\n", sub_error->message);
            g_clear_error(&sub_error);
            return error_loc;
        }

        TimestampFormat ts_format = cine_info->settings.timestamp_format;

        if (self->settings.timestamp_format != TS_NONE)
        {
            // Request the timestamps:
            // format: time {cine:<cine_number>, start:<first_frame>,
            // cnt:<stamp_count>[, from:<image_source>]}
            gchar *request_format = g_strdup_printf("{cine:%d, start:%d, cnt:%d, from:%d}", cine_info->cine, cine_info->start_index, cine_info->nb_images, 0);
            if (request_format == NULL)
            {
                GError *error_loc = g_error_new(UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
                                                "Failed to allocate memory for time command");
                return error_loc;
            }

            // Create timestamp request
            TsRequest *ts_request = g_new0(TsRequest, 1);
            ts_request->start = cine_info->start_index;
            ts_request->count = cine_info->nb_images;
            ts_request->end_request = cine_info->end_request;
            ts_request->cine = cine_info->cine;
            ts_request->ts_format = ts_format;

            // Push request to request queue
            g_async_queue_push(self->ts_request_queue, ts_request);

            GString *reply = uca_phantom_communicate_run_command(self, CMD_GET_TIMESTAMPS, request_format, &sub_error);
            if (reply == NULL)
            {
                GError *error_loc = g_error_new(UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
                                                "Failed to request timestamps:\n\t> %s\n", sub_error->message);
                g_clear_error(&sub_error);
                return error_loc;
            }

            g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Thread %p: finished requesting timestamps\n", g_thread_self());

            g_free(request_format);
            g_string_free(reply, TRUE);
        }

        g_free(cine_info);
    }

    return NULL;
}

gboolean uca_phantom_communicate_request_images(UcaPhantomCommunicate *self, CaptureSettings settings, gboolean earlyimg,
                                                GError **error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);
    g_return_val_if_fail(self->readout_state == ACQUIRING, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;

    self->settings = settings;
    gint cine = settings.current_cine;

    // Make sure we have the mac address, should already be done though
    if (self->xenabled && self->mac_address_str == NULL)
    {
        gboolean res = uca_phantom_communicate_get_mac_address(self, &sub_error);
        if (res != TRUE && sub_error != NULL)
        {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
                        "Failed to get MAC address:\n\t> %s\n", sub_error->message);
            g_clear_error(&sub_error);
            g_propagate_error(error_loc, phantom_error);
            return FALSE;
        }

        self->mac_address_str = g_strdup_printf("%02x%02x%02x%02x%02x%02x", self->mac_address[0], self->mac_address[1],
                                                self->mac_address[2], self->mac_address[3], self->mac_address[4], self->mac_address[5]);

        if (self->mac_address_str == NULL)
        {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
                        "Failed to allocate memory for MAC address");
            g_propagate_error(error_loc, phantom_error);
            return FALSE;
        }
    }

    if (earlyimg)
    {
        // Make sure the cine is indeed triggered
        while (!uca_phantom_communicate_verify_cine_state(self, cine, "TRG", NULL))
        {
            g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Waiting for cine to be ready to be triggered \n");
        }

        // Now we progressively and asynchronously ask for the post frames as they're being acquired
        // followed by the pre frames
        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Pushing early image cine request to queue\n");
        if (settings.nb_post_trigger_frames > 0)
        {
            g_usleep( G_USEC_PER_SEC * 0.5);
            g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Pushing post trigger cine request to queue\n");
            CineInfo *post_info = g_new0(CineInfo, 1);
            post_info->cine = cine;
            post_info->start_index = 0;
            post_info->nb_images = settings.nb_post_trigger_frames;
            post_info->end_request = FALSE;
            post_info->earlyimg = earlyimg;
            post_info->discard = FALSE;
            post_info->settings = settings;
            g_async_queue_push(self->cine_request_queue, post_info);
        }
        else
        {
            g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "No post trigger frames\n");
        }

        if (settings.nb_pre_trigger_frames > 0)
        {
            g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Pushing pre trigger cine request to queue\n");
            CineInfo *pre_info = g_new0(CineInfo, 1);
            pre_info->cine = cine;
            pre_info->start_index = -settings.nb_pre_trigger_frames;
            pre_info->nb_images = settings.nb_pre_trigger_frames;
            pre_info->end_request = FALSE;
            pre_info->earlyimg = earlyimg;
            pre_info->discard = FALSE;
            pre_info->settings = settings;
            g_async_queue_push(self->cine_request_queue, pre_info);
        }
        else
        {
            g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "No pre trigger frames\n");
        }
    }

    else
    {
        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Pushing complete cine request to queue\n");
        if (settings.nb_post_trigger_frames + settings.nb_pre_trigger_frames > 0)
        {
            CineInfo *full_info = g_new0(CineInfo, 1);
            full_info->cine = cine;
            full_info->start_index = -settings.nb_pre_trigger_frames;
            full_info->nb_images = settings.nb_post_trigger_frames;
            full_info->end_request = FALSE;
            full_info->earlyimg = FALSE;
            full_info->discard = FALSE;
            full_info->settings = settings;
            g_async_queue_push(self->cine_request_queue, full_info);
        }
    }

    return TRUE;
}

static gboolean uca_phantom_communicate_receive_1gb_images(
    UcaPhantomCommunicate *self, guint8 *data, gsize frame_size, GError **error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);

    guint8 *buffer_pointer = data;

    g_mutex_lock(&self->control_connection_mutex);
    self->finished_receiving_1gb = FALSE;
    g_mutex_unlock(&self->control_connection_mutex);
    
    gssize remaining_bytes = frame_size;

    while (remaining_bytes > 0)
    {
        const gssize bytes_read = g_input_stream_read(self->input_datastream, buffer_pointer, remaining_bytes, NULL, error_loc);
        if (bytes_read < 0)
        {
            g_error("Error reading image frame from datastream: \n");
            return FALSE;
        }
        else if (bytes_read == 0)
        {
            g_error("End of stream reached\n");
            return FALSE;
        }

        buffer_pointer += bytes_read;
        remaining_bytes -= bytes_read;
    }

    if (remaining_bytes != 0)
    {
        g_error("Failed to read the full image frame from datastream\n");
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RECEIVE,
                    "Failed to read the full image frame from datastream\n");
        return FALSE;
    }

    // Unlock the input control stream to allow the next request
    g_mutex_lock(&self->control_connection_mutex);
    self->finished_receiving_1gb = TRUE;
    g_cond_signal(&self->data_finished_connection_cond);
    g_mutex_unlock(&self->control_connection_mutex);

    return TRUE;
}

/**
 * Read images from 1Gb ethernet connection
 *
 * TODO: return error
 *
 * @param data: pointer to UcaPhantomCommunicate
 * @return
 *
 * @note This function uses a TCP connection to read images from the camera.
 */
static gpointer uca_phantom_communicate_accept_img(gpointer data)
{
    UcaPhantomCommunicate *self = UCA_PHANTOM_COMMUNICATE(data);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, NULL);

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Thread %p: 1Gb Buffering thread started\n", g_thread_self());
    GError *sub_error = NULL;

    while (TRUE)
    {
        // Wait for a request to be available
        ImageRequest *request = g_async_queue_pop(self->request_queue);

        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Thread %p: 1Gb Request received\n", g_thread_self());

        if (request == NULL)
        {
            return NULL;
        }
        if (request->end_request == TRUE)
        {
            g_free(request);
            return NULL;
        }

        CaptureSettings settings = request->settings;

        // Calculate the size of the image buffer
        gsize byte_depth = ImageFormatSpecs[request->img_format].byte_depth;
        guint nb_pixels = settings.sensor_width * settings.sensor_height;
        gsize image_size = nb_pixels * byte_depth;

        gsize packet_size = image_size * request->nb_images; // the packed image packet size

        CineData *cine_data = g_new0(CineData, 1);
        cine_data->SizePerImageUnpacked = image_size;
        cine_data->NbImages = request->nb_images;

        // First push cine info
        if (ringbuf_push(self->unpacked_ring_buffer, (guint8 *)cine_data, sizeof(CineData)) == NULL)
        {
            return g_error_new(UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE,
                               "Failed to push cine data to ring buffer");
        }
        guint8 *image_buffer = ringbuf_reserve(self->unpacked_ring_buffer, packet_size);
        if (image_buffer == NULL)
        {
            return g_error_new(UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE,
                               "Failed to allocate memory for unpacked image");
        }
        if (!uca_phantom_communicate_receive_1gb_images(self, image_buffer, packet_size, &sub_error))
        {
            g_error("Failed to receive image frame from datastream: %s\n", sub_error->message);
            g_free(image_buffer);
            return sub_error;
        }

        ringbuf_commit(self->unpacked_ring_buffer, packet_size);

        g_free(request);
        g_free(cine_data);

        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Thread %p: 1Gb finished receiving images\n", g_thread_self());
    }

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Thread %p: Buffering thread finished\n", g_thread_self());

    return NULL;
}

gpointer uca_phantom_communicate_accept_live_img(gpointer data)
{
    g_return_val_if_fail(UCA_IS_PHANTOM_COMMUNICATE(data), NULL);

    UcaPhantomCommunicate *self = UCA_PHANTOM_COMMUNICATE(data);

    GError *sub_error = NULL;

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Thread %p: Live image thread started\n", g_thread_self());

    while (TRUE)
    {
        // Wait for a request to be available
        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Thread %p: Waiting for live image request\n", g_thread_self());

        ImageRequest *request = g_async_queue_pop(self->live_images_request_queue);

        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Thread %p: live image request received\n", g_thread_self());

        if (request == NULL || request->end_request == TRUE)
        {
            g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Thread %p: live image thread end request received\n", g_thread_self());
            g_free(request);
            return NULL;
        }

        CaptureSettings settings = request->settings;
        ImageFormat img_format = settings.image_format;

        // Calculate the size of the image buffer
        gsize byte_depth = ImageFormatSpecs[img_format].byte_depth;
        guint nb_pixels = settings.sensor_width * settings.sensor_height;
        gsize image_size = nb_pixels * byte_depth;
        gsize packet_size = image_size * request->nb_images; // the packed image packet size

        // First push cine info
        CineData *cine_data = g_new0(CineData, 1);
        cine_data->SizePerImageUnpacked = image_size;
        cine_data->NbImages = request->nb_images;

        if (ringbuf_push(self->live_ring_buffer, (guint8 *)cine_data, sizeof(CineData)) == NULL)
        {
            return g_error_new(UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE,
                               "Failed to push cine data to ring buffer");
        }

        guint8 *image_buffer = ringbuf_reserve(self->live_ring_buffer, packet_size);

        if (!uca_phantom_communicate_receive_1gb_images(self, image_buffer, packet_size, &sub_error))
        {
            return sub_error;
        }

        ringbuf_commit(self->live_ring_buffer, packet_size);

        g_free(request);
    }

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Thread %p: live image thread finished\n", g_thread_self());

    return NULL;
}

/**
 * @brief Accepts images from the camera and stores them in the unpacked queue
 *
 * @param data
 * @return gpointer
 */
static gpointer uca_phantom_communicate_accept_ximg(gpointer data)
{
    UcaPhantomCommunicate *self = UCA_PHANTOM_COMMUNICATE(data);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, NULL);

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Thread %p: accept ximg thread started\n", g_thread_self());

#ifdef USE_MEMPOOL
    gsize buffer_size = MAX_NETWORK_REQUEST_SIZE;
    mempool_t *mempool = mempool_init(buffer_size, 2);
    if (mempool == NULL)
    {
        return g_error_new(UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PAHTNOM_COMMUNICATE_ERROR_MEMPOOL,
                           "Failed to initialize memory pool");
    }

    mempool_t *current_mempool = mempool;
#endif

    guint count = 0;

    while (TRUE)
    {
        // Wait for a request to be available
        ImageRequest *request = g_async_queue_pop(self->request_queue);

        // g_log (VERBOSE, G_LOG_LEVEL_DEBUG,"accept_ximg: Request received\n");
        CaptureSettings settings = request->settings;

        if (request == NULL)
        {
            break;
        }
        else if (request->end_request == TRUE)
        {
            g_free(request);

            // Push the end request to the packed queue
            CineData *cine_data = g_new0(CineData, 1);
            cine_data->RawImages = NULL;
            cine_data->UnpackedImages = NULL;

            // Add the data to the queue
            g_async_queue_push(self->packed_queue, cine_data);

            if (settings.timestamp_format != TS_NONE)
            {
                // Create timestamp request
                TsRequest *ts_request = g_new0(TsRequest, 1);
                ts_request->end_request = TRUE;

                // Push request to request queue
                g_async_queue_push(self->ts_request_queue, ts_request);
            }

            break;
        }
        else if (request->nb_images == 0)
        {
            g_free(request);
            continue;
        }

        // Calculate the size of the image buffer
        guint16 ow = settings.sensor_width;
        guint16 oh = settings.sensor_height;
        guint64 nb_pixels = ow * oh;

        // if (settings.crop) {
        //     g_print("Cropping\n");
        //     g_print ("Nb pixels: %ld\n", settings.window_width * settings.window_height);
        // }
        // else {
        //     g_print("Not cropping\n");
        //     g_print ("Nb pixels: %ld\n", settings.sensor_width * settings.sensor_height);
        // }

        gsize packed_image_size = nb_pixels * ImageFormatSpecs[request->img_format].byte_depth; // the packed image size
        gsize packed_packet_size = packed_image_size * request->nb_images;                      // the packed image packet size
        gsize unpacked_image_size = nb_pixels * sizeof(guint16);                                // the unpacked image size
        gsize buffer_size = packed_packet_size;

        if (buffer_size == 0)
        {
            return g_error_new(UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_ACCEPT_XIMG,
                               "Buffer size is 0");
        }

        gint64 start_time = g_get_monotonic_time();

        gsize remaining_bytes = buffer_size;

#ifdef USE_MEMPOOL
        guint8 *buffer = current_mempool->memoryloc;
        current_mempool = current_mempool->next;
#else
        guint8 *buffer = g_malloc0(buffer_size + 128);
#endif

        if (buffer == NULL)
        {
            return g_error_new(UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_ACCEPT_XIMG,
                               "Failed to allocate memory for image buffer");
        }

        if (self->handle == NULL)
        {
            return g_error_new(UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_ACCEPT_XIMG,
                               "Failed to open pcap handle");
        }

        // Read the image data directly from kernel buffer using pcap_next_ex
        const guint8 *pkt_data = NULL;
        struct pcap_pkthdr *pkt_header = NULL;
        guint8 *buffer_pointer = buffer;

        gboolean error_occured = FALSE;
        while (remaining_bytes > 0)
        {
            int read_all = pcap_next_ex(self->handle, &pkt_header, &pkt_data);

            error_occured = (read_all != 1);

            // Check if the packet size exceeds the remaining space in the buffer
            gsize to_read = pkt_header->len - ETHERNET_HEADER_SIZE;
            if (to_read > remaining_bytes)
            {
                to_read = remaining_bytes;
            }

            // Copy the data to the image buffer
            memcpy(buffer_pointer, pkt_data + ETHERNET_HEADER_SIZE, to_read);

            buffer_pointer += to_read;
            remaining_bytes -= to_read;
        }

        if (error_occured)
        {
            return g_error_new(UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_ACCEPT_XIMG,
                               "Pcap error while reading image data");
        }

        gsize bytes_read = packed_packet_size - remaining_bytes;

        if (bytes_read != packed_packet_size)
        {
            g_debug("Failed to read all bytes of image frame from datastream. Read %ld "
                    "bytes, expected %ld bytes. Buffer will be padded.\n",
                    bytes_read, packed_packet_size);
        }

        gint64 end_time = g_get_monotonic_time();

        // // Save the image to a file
        // gchar *filename = g_strdup_printf("image_%d.raw", count++);
        // save_to_file(buffer, packed_packet_size, filename);
        // g_free(filename);

        if (request->discard)
        {
            g_free(buffer);
            g_free(request);
            continue;
        }

#ifdef PERFORMANCE
        g_log(PERFORMANCE, G_LOG_LEVEL_INFO, "xrcv (start usec, end usec, bytes read): %ld,%ld,%ld\n", start_time, end_time, bytes_read);
#endif

        CineData *cine_data = g_new0(CineData, 1);
        cine_data->ImgFormat = request->img_format;

        cine_data->NbImages = request->nb_images;
        cine_data->NbPixelsPerImage = nb_pixels;
        cine_data->SizePerImageRaw = packed_image_size;
        cine_data->SizePerImageUnpacked = unpacked_image_size;
        cine_data->width = ow;
        cine_data->height = oh;

        cine_data->RawImages = buffer;
        cine_data->UnpackedImages = NULL;

        g_async_queue_push(self->packed_queue, cine_data);

#if NO_DROP
#else
        guint img_count = request->nb_images;
        g_async_queue_push(self->throttle_queue, &img_count);
#endif

        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Thread %p: finished receiving images\n", g_thread_self());

        g_free(request);
    }

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Thread %p: accept ximg thread finished\n", g_thread_self());

#ifdef USE_MEMPOOL
    mempool_free(mempool);
#endif

    return NULL;
}

/**
 * @brief Unpacks the image data from the packed format to the unpacked format
 *
 * @param cine_data
 * @param error_loc
 * @return gboolean
 */
static gboolean uca_phantom_communicate_unpack_image_p10(UcaPhantomCommunicate *self, CineData *cine_data,
                                                         GError **error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(cine_data != NULL, FALSE);

    static guint16 *unpacked_buffer = NULL;
    static gsize previous_size = 0;

    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(cine_data != NULL, FALSE);

    GError *phantom_error = NULL;

    gsize output_size = cine_data->SizePerImageUnpacked * cine_data->NbImages;

    gint64 start_time = g_get_monotonic_time();

    // Allocate memory for the unpacked image
    if (previous_size < output_size)
    {
        if (unpacked_buffer != NULL)
        {
            g_free(unpacked_buffer);
        }

        unpacked_buffer = g_malloc0(output_size);
        if (unpacked_buffer == NULL)
        {
            g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE,
                        "Failed to allocate memory for unpacked image");
            return FALSE;
        }

        previous_size = output_size;
    }

    __m128i sm0 = _mm_setr_epi8(1, 0, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 6, 5, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80);
    __m128i sm1 = _mm_setr_epi8(0x80, 0x80, 2, 1, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 7, 6, 0x80, 0x80, 0x80, 0x80);
    __m128i sm2 = _mm_setr_epi8(0x80, 0x80, 0x80, 0x80, 3, 2, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 8, 7, 0x80, 0x80);
    __m128i sm3 = _mm_setr_epi8(0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 4, 3, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 9, 8);

    __m128i mask2 = _mm_setr_epi8(0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111,
                                  0b11111111, 0b11111111, 0b11111111, 0, 0, 0, 0, 0, 0);
    __m128i m0 = _mm_setr_epi8(0b11000000, 0b11111111, 0, 0, 0, 0, 0, 0, 0b11000000, 0b11111111, 0, 0, 0, 0, 0, 0);
    __m128i m1 = _mm_setr_epi8(0, 0, 0b11110000, 0b00111111, 0, 0, 0, 0, 0, 0, 0b11110000, 0b00111111, 0, 0, 0, 0);
    __m128i m2 = _mm_setr_epi8(0, 0, 0, 0, 0b11111100, 0b00001111, 0, 0, 0, 0, 0, 0, 0b11111100, 0b00001111, 0, 0);
    __m128i m3 = _mm_setr_epi8(0, 0, 0, 0, 0, 0, 0b11111111, 0b00000011, 0, 0, 0, 0, 0, 0, 0b11111111, 0b00000011);

    guint input_index = 0;
    guint output_index = 0;

    if (cine_data->NbPixelsPerImage % 8 != 0)
    {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE,
                    "Image size is not a multiple of 8");
        g_propagate_error(error_loc, phantom_error);
        return FALSE;
    }

    __m128i input, shifted0, shifted1, shifted2, shifted3, result;
    while (output_index < cine_data->NbPixelsPerImage * cine_data->NbImages)
    {
        // Load 8 pixels, i.e. 80 bits = 10 bytes
        input = _mm_loadu_si128((__m128i *)(cine_data->RawImages + input_index));

        // Mask
        input = _mm_and_si128(input, mask2);

        // Shift
        shifted0 = _mm_and_si128(_mm_shuffle_epi8(input, sm0), m0) >> 6;
        shifted1 = _mm_and_si128(_mm_shuffle_epi8(input, sm1), m1) >> 4;
        shifted2 = _mm_and_si128(_mm_shuffle_epi8(input, sm2), m2) >> 2;
        shifted3 = _mm_and_si128(_mm_shuffle_epi8(input, sm3), m3);

        // Result
        result = _mm_or_si128(_mm_or_si128(shifted0, shifted1), _mm_or_si128(shifted2, shifted3));

        // Store
        _mm_storeu_si128((__m128i *)(unpacked_buffer + output_index), result);

        output_index += 8;
        input_index += 10;
    }

    if (output_index != cine_data->NbPixelsPerImage * cine_data->NbImages)
    {
        g_warning("Error while unpacking image");
    }

    // copy into the ring buffer
    ringbuf_push(self->unpacked_ring_buffer, unpacked_buffer, output_size);
    gint64 end_time = g_get_monotonic_time();

#ifdef PERFORMANCE
    g_log(PERFORMANCE, G_LOG_LEVEL_INFO, "xupack: %ld,%ld,%ld\n", start_time, end_time, output_size);
#endif

    // free the unpacked buffer
    // g_free (unpacked_buffer);
    g_free(cine_data->RawImages);

    return TRUE;
}

/**
 * @brief Unpacks the image data from the P12L format to a 16bit array
 *
 * @param cine_data
 * @param error_loc
 * @return gboolean
 */
static gboolean unpack_image_p12l(const guint8 *input, guint16 *output, guint64 total_pixels)
{
    __m128i sm0 = _mm_setr_epi8(1, 0, 0x80, 0x80, 4, 3, 0x80, 0x80, 7, 6, 0x80, 0x80, 10, 9, 0x80, 0x80);
    __m128i sm1 = _mm_setr_epi8(0x80, 0x80, 2, 1, 0x80, 0x80, 5, 4, 0x80, 0x80, 8, 7, 0x80, 0x80, 11, 10);
    __m128i m0 = _mm_setr_epi8(0b11110000, 0b11111111, 0, 0, 0b11110000, 0b11111111, 0, 0, 0b11110000, 0b11111111, 0, 0,
                               0b11110000, 0b11111111, 0, 0);
    __m128i m1 = _mm_setr_epi8(0, 0, 0b11111111, 0b00001111, 0, 0, 0b11111111, 0b00001111, 0, 0, 0b11111111, 0b00001111,
                               0, 0, 0b11111111, 0b00001111);
    __m128i mask2 = _mm_setr_epi8(0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111,
                                  0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0, 0, 0, 0);
    gboolean success = TRUE;

    if (total_pixels % 8 != 0)
    {
        success = FALSE;
        g_warning("Error while unpacking image: total_pixels is not a multiple of 8");
        goto cleanup;
    }

    const guint nb_pixels_per_register = 8;
    const guint nb_workers = NUM_THREADS;
    const guint nb_registers = total_pixels / nb_pixels_per_register; // in number of registers

#pragma omp parallel num_threads(nb_workers) shared(input, output, total_pixels, nb_registers, mask2, sm0, sm1, m0, m1)
    {
#pragma omp for schedule(monotonic : static)
        for (int i = 0; i < nb_registers; i++)
        {
            // Load 16 pixels, i.e. 16*1.5 = 24bytes
            __m128i inputvec = _mm_loadu_si128((__m128i *)(input + i * 12));

            // Mask
            inputvec = _mm_and_si128(inputvec, mask2);

            // Shift
            __m128i shifted0 = _mm_and_si128(_mm_shuffle_epi8(inputvec, sm0), m0) >> 4;
            __m128i shifted1 = _mm_and_si128(_mm_shuffle_epi8(inputvec, sm1), m1);

            // Result
            __m128i result = _mm_or_si128(shifted0, shifted1);

            // Store
            _mm_storeu_si128((__m128i *)(output + i * 8), result);
        }
    }

cleanup:
    return success;
}

/**
 * @brief Unpack the 10/12bit image data into 16bit using SIMD SSE instructions
 *
 * @param data
 * @return gpointer
 *
 * TODO: May need to modify the data structure that holds the cine data!
 *
 */
static gpointer uca_phantom_communicate_unpack_ximg(gpointer data)
{
    UcaPhantomCommunicate *self = UCA_PHANTOM_COMMUNICATE(data);

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Unpacking images\n");

    // Loop on CineData objects in the queue
    while (TRUE)
    {
        // Get the next CineData object
        CineData *cine_data = g_async_queue_pop(self->packed_queue);

        if (cine_data->UnpackedImages == NULL && cine_data->RawImages == NULL)
        {
            g_free(cine_data);
            break;
        }

        guint64 total_pixels = cine_data->NbPixelsPerImage * cine_data->NbImages;
        gsize output_size = total_pixels * sizeof(guint16);

        // Assing unpack_image_p12l to a general function pointer
        gboolean (*unpack_images)(const guint8 *, guint16 *, guint64) = NULL;
        if (cine_data->ImgFormat == IMG_P12L)
        {
            unpack_images = unpack_image_p12l;
        }
        else
        {
            return g_error_new(UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE,
                               "Image format not supported over 10Gb Ethernet.");
        }

        if (cine_data->NbImages < 1)
        {
            return g_error_new(UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE,
                               "Number of images is less than 1");
        }

        // Allocate memory for the unpacked image
        guint64 start_time = g_get_monotonic_time();
        const guint8 *input = cine_data->RawImages;

        // Push cine data first
        if (ringbuf_push(self->unpacked_ring_buffer, cine_data, sizeof(CineData)) == NULL)
        {
            return g_error_new(UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE,
                               "Failed to allocate memory for unpacked image");
        }
        guint8 *output = ringbuf_reserve(self->unpacked_ring_buffer, output_size);
        if (output == NULL)
        {
            return g_error_new(UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE,
                               "Failed to allocate memory for unpacked image");
        }
        // Unpack the image data
        gboolean retval = unpack_images(input, output, total_pixels);
        ringbuf_commit(self->unpacked_ring_buffer, output_size);

        if (retval == FALSE)
        {
            return g_error_new(UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE,
                               "Error occurred while unpacking images");
        }
        guint64 end_time = g_get_monotonic_time();

        g_log(PERFORMANCE, G_LOG_LEVEL_INFO, "xupack (start usec, end usec, bytes read): %ld,%ld,%ld\n", start_time, end_time, output_size);

// Free the cine data
#ifdef USE_MEMPOOL
#else
        g_free(cine_data->RawImages);
#endif

        g_free(cine_data);
    }

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Thread %p: Unpacking done\n", g_thread_self());
    return NULL;
}

/**
 * @brief Accepts timestamp data from the input datastream and pushes it to the
 * timestamp queue
 *
 * @details This function is called in a new thread to accept timestamp data
 * from the input datastream and push it to the timestamp queue. It loops on
 * CineData objects in the queue and receives the timestamp data from the input
 * datastream. If an error occurs during the read, it returns a GError object.
 * Otherwise, it creates a TimestampData struct and pushes it to the timestamp
 * queue.
 *
 * @param data A pointer to the UcaPhantomCommunicate object
 * @return gpointer NULL
 */
static gpointer uca_phantom_communicate_accept_timestamps(gpointer data)
{
    UcaPhantomCommunicate *self = UCA_PHANTOM_COMMUNICATE(data);

    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, NULL);

    GError *sub_error = NULL;

    guint8 *ts_data = NULL;

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Thread %p: Accept timestamps thread started\n", g_thread_self());

    // Loop on CineData objects in the queue
    while (TRUE)
    {

        // Lock the control connection mutex
        TsRequest *ts_request = g_async_queue_pop(self->ts_request_queue);

        if (ts_request->end_request == TRUE)
        {
            // exit the thread
            g_free(ts_request);
            break;
        }

        gsize ts_size = TimestampSpecs[ts_request->ts_format].byte_size;
        gsize packet_size = ts_request->count * ts_size;
        gsize remaining_bytes = packet_size;

        // Allocate memory for the timestamp data
        ts_data = g_realloc_n(ts_data, ts_request->count, ts_size);
        if (ts_data == NULL)
        {
            return g_error_new(UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_ACCEPT_TIMESTAMPS,
                               "Failed to allocate memory for timestamp data");
        }

        guint8 *buffer_pointer = ts_data;

        // Receive the timestamp data from the self->input_datastream
        while (remaining_bytes > 0)
        {
            gssize bytes_read = g_input_stream_read(self->input_datastream, buffer_pointer, remaining_bytes, NULL, &sub_error);
            if (bytes_read < 0)
            {
                g_free(ts_data);
                return sub_error;
            }
            else if (bytes_read == 0)
            {
                g_free(ts_data);
                return sub_error;
            }
            remaining_bytes -= bytes_read;
            buffer_pointer += bytes_read;
        }

        // Fill in the timestamps
        PhantomTimestamp *ts = g_new0(PhantomTimestamp, ts_request->count);
        gsize phantom_ts_size = sizeof(PhantomTimestamp);
        for (guint i = 0; i < ts_request->count; i++)
        {
            memcpy(ts + i, ts_data + i * ts_size, ts_size);
            ts[i].cine = ts_request->cine;
        }

        ringbuf_push(self->ts_ring_buffer, ts, ts_request->count * phantom_ts_size);
        g_free(ts_request);

        // Signal the control connection condition
        g_mutex_lock(&self->control_connection_mutex);
        self->finished_receiving_1gb = TRUE;
        g_cond_signal(&self->data_finished_connection_cond);

        // Unlock the control connection mutex
        g_mutex_unlock(&self->control_connection_mutex);
    }

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Thread %p: Accept timestamps thread finished\n", g_thread_self());

    if (ts_data)
    {
        g_free(ts_data);
    }

    return NULL;
}

gboolean uca_phantom_communicate_start_readout(
    UcaPhantomCommunicate *self, gboolean live_images, CaptureSettings *settings, GError **error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;
    gboolean res = FALSE;

    self->settings = *settings;

    guint nb_images = settings->nb_post_trigger_frames + settings->nb_pre_trigger_frames;

    // The maximum number of images that can be requested in a single request
    gsize nb_pixels = settings->sensor_width * settings->sensor_height;
    gsize image_size = nb_pixels * ImageFormatSpecs[settings->image_format].byte_depth;

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Starting readout\n");

    if (self->readout_state == IDLE)
    {
        self->readout_state = ACQUIRING;
        self->phantom_acquisition_state = ACQUIRING;
    }

    if (settings->timestamp_format != TS_NONE)
    {
        // Create timestamp ring buffer
        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Starting timestamp thread\n");
        self->ts_ring_buffer = ringbuf_new(10 * nb_images * sizeof(PhantomTimestamp), TRUE);
        // Start new thread to read timestamps
        self->ts_receiver = g_thread_new("ts_receiver", uca_phantom_communicate_accept_timestamps, self);
        self->ts_acquisition_state = ACQUIRING;
    }

    if (live_images)
    {
        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Starting live thread\n");
        if (self->live_ring_buffer == NULL)
            self->live_ring_buffer = ringbuf_new(MAX_BUFFERED_IMAGES * image_size, TRUE);
        else
        {
            ringbuf_reset(self->live_ring_buffer);
        }

        self->live_images_receiver = g_thread_new("live_images_receiver", uca_phantom_communicate_accept_live_img, self);
        self->live_image_acquisition = ACQUIRING;

        return TRUE;
    }

    gsize unpacked_rb_size = 0;

    if (self->xenabled)
    {
        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Starting x threads\n");
        if (self->mac_address_str == NULL)
        {
            res = uca_phantom_communicate_get_mac_address(self, &sub_error);
            if (res != TRUE && sub_error != NULL)
            {
                g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR,
                            UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to get MAC address:\n\t> %s\n",
                            sub_error->message);
                g_propagate_error(error_loc, phantom_error);
                g_clear_error(&sub_error);
                return FALSE;
            }

            self->mac_address_str = g_strdup_printf("%02x%02x%02x%02x%02x%02x", self->mac_address[0], self->mac_address[1],
                                                    self->mac_address[2], self->mac_address[3], self->mac_address[4], self->mac_address[5]);
            if (self->mac_address_str == NULL)
            {
                g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR,
                            UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to allocate memory for MAC address");
                g_propagate_error(error_loc, phantom_error);
                return FALSE;
            }
        }

        self->xdata_receiver = g_thread_new("xdata_receiver", uca_phantom_communicate_accept_ximg, self);
        self->data_unpacker = g_thread_new("data_unpacker", uca_phantom_communicate_unpack_ximg, self);

        unpacked_rb_size = (nb_pixels * sizeof(guint16) + sizeof(CineData)) * nb_images;
    }
    else
    {
        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Starting data receiver thread\n");
        self->data_receiver = g_thread_new("data_receiver", uca_phantom_communicate_accept_img, self);

        unpacked_rb_size = (image_size + sizeof(CineData)) * nb_images;
    }

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Unpacked ring buffer size: %ld\n", unpacked_rb_size);

    self->unpacked_ring_buffer = ringbuf_new(unpacked_rb_size, TRUE);
    if (self->unpacked_ring_buffer == NULL)
    {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING,
                    "Failed to allocate memory for unpacked image buffer");
        g_propagate_error(error_loc, phantom_error);
        return FALSE;
    }

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Starting buffering thread\n");
    self->request_thread = g_thread_new("request_images_thread", uca_phantom_communicate_request_images_thread, self);

    return TRUE;
}

gboolean uca_phantom_communicate_grab_image(UcaPhantomCommunicate *self, gpointer data, GError **error_loc)
{
    static guint count = 0;
    static gsize image_size = 0;

#if NO_DROP
    g_async_queue_push(self->throttle_queue, &count);
#endif

    // copy the image data to the output buffer
    // CAUTION : no verification is done on the size of the output buffer...
    // This is dangerous as it as it puts the user in charge of allocating the
    // right amount of memory with the good bit depth.
    // TODO : Mqybe consider GBytes for the output buffer ?
    if (data == NULL)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GRAB_IMAGE,
                    "Output buffer is NULL");
        return FALSE;
    }

    // first pop the nb_pixels and nb_images
    if (count == 0)
    {
        CineData tmp;
        ringbuf_pop(&tmp, self->unpacked_ring_buffer, sizeof(CineData));
        image_size = tmp.SizePerImageUnpacked;
        count = tmp.NbImages;
    }

    if (image_size == 0)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GRAB_IMAGE,
                    "Failed to get the number of pixels and images");
        return FALSE;
    }

    g_log (DEBUG, G_LOG_LEVEL_DEBUG, "Grabbing image %d of size %ld\n", count, image_size);

    ringbuf_pop(data, self->unpacked_ring_buffer, image_size);

    g_log(DEBUG, G_LOG_LEVEL_DEBUG, "Grabbed image %d of size %ld\n", count, image_size);
    count--;

    return TRUE;
}

gboolean uca_phantom_communicate_grab_live_image(UcaPhantomCommunicate *self, gpointer data, CaptureSettings settings, GError **error_loc)
{
    static guint count = 0;
    static gsize image_size = 0;

    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);

    if (self->live_ring_buffer == NULL)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GRAB_LIVE_IMAGE,
                    "Live ring buffer is NULL");
        return FALSE;
    }

    if (data == NULL)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GRAB_LIVE_IMAGE,
                    "Output buffer is NULL");
        return FALSE;
    }

    // Request the image
    gboolean ret = uca_phantom_communicate_request_live_images(self, settings, error_loc);

    if (ret == FALSE)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GRAB_LIVE_IMAGE,
                    "Failed to request live image");
        return FALSE;
    }

    if (count == 0)
    {
        CineData tmp;
        ringbuf_pop(&tmp, self->live_ring_buffer, sizeof(CineData));
        image_size = tmp.SizePerImageUnpacked;
        count = tmp.NbImages;
    }

    if (image_size == 0)
    {
        g_set_error(error_loc, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GRAB_LIVE_IMAGE,
                    "Failed to get the number of pixels and images");
        return FALSE;
    }

    // CAUTION : no verification is done on the size of the output buffer...
    // This is dangerous as it as it puts the user in charge of allocating the
    // right amount of memory with the good bit depth.
    // TODO : Mqybe consider GBytes for the output buffer ?
    // gsize image_size = self->settings.sensor_width * self->settings.sensor_height * ImageFormatSpecs[settings.image_format].byte_depth;
    ringbuf_pop(data, self->live_ring_buffer, image_size);

    g_log(DEBUG, G_LOG_LEVEL_DEBUG, "Live image grabbed\n");

    count--;

    return TRUE;
}

guint64 decode_timestamp(PhantomTimestamp tmp, TimestampFormat ts_format, guint32 yearbegin)
{
    guint32 csecs = 0, frac = 0, exptime = 0;
    guint16 exptime32 = 0, frac32 = 0;

    switch (ts_format)
    {
    case TS_LONG32:
        // fall through
    case TS_LONG:
        // fall through
    case TS_SHORT32:
        tmp.exptime32 = ntohs(tmp.exptime32);
        exptime32 = tmp.exptime32;
        tmp.frac32 = ntohs(tmp.frac32); // in 1/65536 of a microsecond
        frac32 = tmp.frac32;
        // fall through
    case TS_SHORT:
        tmp.csecs = ntohl(tmp.csecs);
        tmp.exptime = ntohs(tmp.exptime);
        tmp.frac = ntohs(tmp.frac) >> 2;
        csecs = tmp.csecs;
        frac = tmp.frac;
        exptime = tmp.exptime;
        break;
    case TS_NONE:
        // fall through
    default:
        return 0;
    }

    guint64 tv_sec = csecs / 100 + yearbegin; // seconds since epoch
    guint64 tv_usec = (csecs % 100) * 10000 + frac + frac32 / 65536;

    return tv_sec * 1000000 + tv_usec;
}

gboolean uca_phantom_communicate_grab_timestamp(UcaPhantomCommunicate *self, guint64 *time, TimestampFormat format, GError **error_loc)
{
    static gint prev_cine = 0;
    static gint64 prev_trigger_time = 0, prev_trigger_time_frac = 0;

    // CAUTION : no verification is done on the size of the output buffer...
    // This is dangerous as it as it puts the user in charge of allocating the
    // right amount of memory with the good bit depth.
    // TODO : Mqybe consider GBytes for the output buffer ?
    PhantomTimestamp tmp;
    if (self->ts_ring_buffer == NULL)
    {
        return FALSE;
    }
    ringbuf_pop(&tmp, self->ts_ring_buffer, sizeof(PhantomTimestamp));

    if (self->irig_yearbegin == 0)
    {
        GValue value = G_VALUE_INIT;
        uca_phantom_communicate_get_variable(self, UNIT_IRIG_YEARBEGIN, 0, &value, error_loc);
        self->irig_yearbegin = g_value_get_uint(&value);
        g_value_unset(&value);
    }

    if (prev_cine != tmp.cine)
    {
        prev_cine = tmp.cine;
        GValue value = G_VALUE_INIT;
        gboolean res = uca_phantom_communicate_get_variable(self, UNIT_C_TRIGTIME_SECS, tmp.cine, &value, error_loc);
        if (res == FALSE || error_loc != NULL)
        {
            return FALSE;
        }
        prev_trigger_time = g_value_get_uint(&value);

        uca_phantom_communicate_get_variable(self, UNIT_C_TRIGTIME_FRAC, tmp.cine, &value, error_loc);
        if (res == FALSE || error_loc != NULL)
        {
            return FALSE;
        }
        prev_trigger_time_frac = g_value_get_uint(&value);

        prev_trigger_time = prev_trigger_time * 1e6 + prev_trigger_time_frac;
        g_value_unset(&value);
    }

    *time = decode_timestamp(tmp, format, self->irig_yearbegin);

    // Print; time on trigger, exposure time, time on close shutter
    // g_print("Time on trigger: %d, Exposure time: %d, Time on close shutter: %ld\n", tmp.cine, tmp.exptime, *time);

    return TRUE;
}

static gboolean
uca_phantom_join_thread(GThread *thread,
                        GError **error_loc,
                        GQuark domain,
                        gint code,
                        const gchar *thread_name)
{
    GError *sub_error = g_thread_join(thread);
    if (sub_error != NULL)
    {
        GError *phantom_error = NULL;
        g_set_error(&phantom_error, domain, code,
                    "Failed to join %s thread:\n\t> %s\n",
                    thread_name, sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_clear_error(&sub_error);
        return FALSE;
    }
    return TRUE;
}

gboolean uca_phantom_communicate_stop_readout(UcaPhantomCommunicate *self, GError **error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);
    g_return_val_if_fail(self->readout_state == ACQUIRING, FALSE);

    g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "Stopping readout\n");

    if (self->live_image_acquisition == ACQUIRING)
    {
        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Stopping liveimage thread\n");

        ImageRequest *request = g_new0(ImageRequest, 1);
        request->end_request = TRUE;
        g_async_queue_push(self->live_images_request_queue, request);

        if (!uca_phantom_join_thread(
                self->live_images_receiver, error_loc,
                UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_STOP_READOUT, "live_images_receiver"))
        {
            return FALSE;
        }
        self->live_image_acquisition = IDLE;

        ringbuf_free(self->live_ring_buffer);
        self->live_ring_buffer = NULL;
    }
    else
    {
        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Stopping request thread\n");

        CineInfo *cine_info = g_new0(CineInfo, 1);
        cine_info->end_request = TRUE;
        g_async_queue_push(self->cine_request_queue, cine_info);

        if (!uca_phantom_join_thread(
                self->request_thread, error_loc,
                UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_STOP_READOUT, "request_thread"))
        {
            return FALSE;
        }

        ringbuf_free(self->unpacked_ring_buffer);
        self->unpacked_ring_buffer = NULL;
    }

    if (self->ts_acquisition_state == ACQUIRING)
    {
        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Stopping timestamp receiver thread\n");

        TsRequest *ts_request = g_new0(TsRequest, 1);
        ts_request->end_request = TRUE;
        g_async_queue_push(self->ts_request_queue, ts_request);

        if (!uca_phantom_join_thread(
                self->ts_receiver, error_loc,
                UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_STOP_READOUT, "ts_receiver"))
        {
            return FALSE;
        }

        self->ts_acquisition_state = IDLE;
    }

    if (self->xenabled)
    {
        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Stopping xdata_receiver thread\n");

        if (!uca_phantom_join_thread(
                self->xdata_receiver, error_loc,
                UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_STOP_READOUT, "xdata_receiver"))
        {
            return FALSE;
        }

        // Stop the data unpacker thread
        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Stopping xdata_unpacker thread\n");

        if (!uca_phantom_join_thread(
                self->data_unpacker, error_loc,
                UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_STOP_READOUT, "data_unpacker"))
        {
            return FALSE;
        }

        // Make sure packed queue is empty
        while (g_async_queue_try_pop(self->packed_queue) != NULL)
        {
            continue;
        }
    }
    else
    {
        g_log(VERBOSE, G_LOG_LEVEL_DEBUG, "\t>Stopping data_receiver thread\n");
        if (self->data_receiver != NULL)
        {
            if (!uca_phantom_join_thread(
                    self->data_receiver, error_loc,
                    UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_STOP_READOUT, "data_receiver"))
            {
                return FALSE;
            }
        }
    }

    self->phantom_acquisition_state = IDLE;
    self->readout_state = IDLE;

    return TRUE;
}
