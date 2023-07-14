/**
 * @file uca-phantom-communicate.c
 * @brief Implementation of the UcaPhantomCommunicate class
 * @ingroup UcaPhantomCommunicate
 *
 * @author Gabriel Lefloch
 */

// GLib and GObject related includes
#include <gio/gio.h>
#include <glib-object.h>
#include <gmodule.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Intel intrinsics
#include <nmmintrin.h>

// Network related includes
#include <linux/if.h>
#include <pcap.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "uca-phantom-communicate.h"
#include "uca-phantom-variables.h"
#include "uca-phantom-commands.h"
#include "ringbuf.h"

// Note: if you wish to mess everything up, please change the following macros
#define ETHERNET_HEADER_SIZE 32 // 16 bytes for L1 ethernet header, 16 bytes for custom header
#define MAX_KERNEL_BUF_SIZE 2147483647 // 2GBytes = 2^31 - 1 Bytes
#define MAX_HEAP_BUF_SIZE MAX_KERNEL_BUF_SIZE

#define MAX_SENSOR_PIXEL_WIDTH 2048 // Maximum sensor pixel width
#define MAX_SENSOR_PIXEL_HEIGHT 1952 // Maximum sensor pixel height

#define MAX_NB_IMAGES_BUFFERING 50
/**
 * TODO:
 * - Verify the requested data size is smaller than the available space in the
 * camera cine
 * - timestamps
 * - signals ?
 * - Checkout the memread nonsense, external pins and whatnot
 * - Implement cancelable functions
 * - Add documentation
 */

enum {
    PROP_PHANTOM_IP = 1,
    PROP_PHANTOM_XIP,
    PROP_NETCARD_IP,
    PROP_NETCARD_XIP,
    PROP_NETCARD,
    PROP_XNETCARD,
    PROP_XENABLED,
    PROP_CONTROL_PORT,
    PROP_PHANTOM_IPSOURCE,
    PROP_TIMESTAMPING,
    PROP_BUFFERING,
    N_PROPERTIES
} UcaPhantomCommunicateProperties;

/**
 * @defgroup NetworkStructures Network related structures
 * TODO: Add documentation
 * @{
 */
enum TerminatePhantomDiscover {
    ALL,
    REGEX,
    RECEIVE,
    SOCKET_END,
    BCAST
};

typedef enum {
    CONNECTED,
    DISCONNECTED
} ConnectionState;

typedef enum {
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

typedef struct _short_time_stamp { // cam.tsformat = 0
    unsigned int csecs; // time from beginning of the year in 1/100 sec units
    unsigned short exptime; // exposure time in us
    unsigned short frac; // bits[15..2]: fractions (us to 10000); b[1]:event;[0]:lock
} short_time_stamp;
typedef struct _short_time_stamp32 { // cam.tsformat = 1
    unsigned int csecs; // time from beginning of the year in 1/100 sec units
    unsigned short exptime; // exposure time in us
    unsigned short frac; // bits[15..2]: fractions (us to 10000); b[1]:event; b[0]:lock
    unsigned short exptime32; // exposure time extension (1/65536 of a us)
    unsigned short frac32; // time stamp extension (1/65536 of a us)
} short_time_stamp32;
typedef struct _long_time_stamp { // cam.tsformat = 2
    unsigned int csecs; // time from beginning of the year in 1/100 sec units
    unsigned short exptime; // exposure time in us
    unsigned short frac; // bits[15..2]: fractions (us to 10000); bit[1]:event; bit[0]:lock
    unsigned int range_d0; // first 32bits received as rangedata, lsb first, big endian
    unsigned int range_d1; // second 32bits received as rangedata, lsb first, big endian
    unsigned int range_d2; // third 32bits received as rangedata, lsb first, big endian
    unsigned int range_d3; // fourth 32bits received as rangedata, lsb first, big endian
} long_time_stamp;
typedef struct _long_time_stamp32 { // cam.tsformat = 3
    unsigned int csecs; // time from beginning of the year in 1/100 sec units
    unsigned short exptime; // exposure time in us
    unsigned short frac; // bits[15..2]: fractions (us to 10000); bit[1]:event; bit[0]:lock
    unsigned short exptime32; // exposure time extension (1/65536 of a us)
    unsigned short frac32; // time stamp extension (1/65536 of a us)
    unsigned int range_d0; // first 32bits received as rangedata, lsb first, big endian
    unsigned int range_d1; // second 32bits received as rangedata, lsb first, big endian
    unsigned int range_d2; // third 32bits received as rangedata, lsb first, big endian
    unsigned int range_d3; // fourth 32bits received as rangedata, lsb first, big endian
} long_time_stamp32;

/** @} */

/**
 * @defgroup CommunicationStructures Phantom communication structures
 * TODO: Add documentation
 * @{
 */
struct _PhantomRequest {
    PhantomCommand command;
    gchar* message;
    gsize size;
    gssize write_size;
};

struct _PhantomReply {
    gchar* raw;
    GValue value;
    gsize size;
    gssize read_size;
};

typedef struct _ImageRequest {
    gboolean end_request;
    gint start;
    guint64 nb_images;
    ImageFormat img_format;
    guint buffer_index;
} ImageRequest;

typedef struct _TsRequest {
    gboolean end_request;
    gint start;
    guint count;
    TimestampFormat TsFormat;
} TsRequest;

/**
 * @brief Timestamp union
 *
 * Format    :   0   1   2   3
 * size(bytes):  8   12  24  28
 *
 */
typedef union {
    short_time_stamp ts0;
    short_time_stamp32 ts1;
    long_time_stamp ts2;
    long_time_stamp32 ts3;
} Timestamp;
/** @} */

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
typedef struct _CineData {
    CaptureSettings* Settings;
    ImageFormat ImgFormat;
    TimestampFormat TsFormat;

    guint NbImages;
    guint NbPixelsPerImage;
    gsize SizePerImageRaw;
    gsize SizePerImageUnpacked;

    guint8* RawImages;
    gpointer UnpackedImages;
    guint buffer_index;
} CineData;

typedef struct _TimestampData {
    gint start;
    guint count;
    TimestampFormat TsFormat;
    Timestamp* timestamps;
    gboolean end_request;
} TimestampData;

static GEnumValue sync_mode_values[] = {
    { SYNC_MODE_FREE_RUN, "SYNC_MODE_FREE_RUN", "sync_mode_free_run" },
    { SYNC_MODE_FSYNC, "SYNC_MODE_FSYNC", "sync_mode_fsync" },
    { SYNC_MODE_IRIG, "SYNC_MODE_IRIG", "sync_mode_irig" },
    { SYNC_MODE_VIDEO_FRAME_RATE, "SYNC_MODE_VIDEO_FRAME_RATE", "sync_mode_video_frame_rate" },
    { 0, NULL, NULL }
};

static GEnumValue acquisition_mode_values[] = {
    { ACQUISITION_MODE_STANDARD, "ACQUISITION_MODE_STANDARD", "acquisition_mode_standard" },
    { ACQUISITION_MODE_STANDARD_BINNED, "ACQUISITION_MODE_STANDARD_BINNED", "acquisition_mode_standard_binned" },
    { ACQUISITION_MODE_HS, "ACQUISITION_MODE_HS", "acquisition_mode_hs" },
    { ACQUISITION_MODE_HS_BINNED, "ACQUISITION_MODE_HS_BINNED", "acquisition_mode_hs_binned" },
    { 0, NULL, NULL }
};

const ImageFormatSpec ImageFormatSpecs[] = { { "8", 8, 1 }, { "8R", 8, 1 }, { "P16", 16, 2 }, { "P16R", 16, 2 }, { "P10", 10, 1.25 }, { "P12L", 12, 1.5 } };

const TimestampSpec TimestampSpecs[] = { { "0", sizeof(short_time_stamp) },
    { "1", sizeof(short_time_stamp32) },
    { "2", sizeof(long_time_stamp) },
    { "3", sizeof(long_time_stamp32) } };

const gdouble MaxNumberImages = MAX_KERNEL_BUF_SIZE / ((double)(MAX_SENSOR_PIXEL_HEIGHT * MAX_SENSOR_PIXEL_WIDTH) + ETHERNET_HEADER_SIZE);

const guint MaxNumberImagesPerRequest[] = { (MaxNumberImages), (MaxNumberImages), (MaxNumberImages / 2),
    (MaxNumberImages / 2), (MaxNumberImages / 1.25), (MaxNumberImages / 1.5) };

// Forward declaration of overrideable functions
static void uca_phantom_communicate_set_property(GObject* object, guint property_id, const GValue* value,
    GParamSpec* pspec);
static void uca_phantom_communicate_get_property(GObject* object, guint property_id, GValue* value, GParamSpec* pspec);
static void uca_phantom_communicate_constructed(GObject* object);
static void uca_phantom_communicate_dispose(GObject* object);
static void uca_phantom_communicate_finalize(GObject* object);

static gboolean uca_phantom_communicate_get_resolution(UcaPhantomCommunicate* self, guint16* width, guint16* height,
    GError** error_loc);
static GParamSpec* uca_phantom_communicate_properties[N_PROPERTIES] = {
    NULL,
};

struct _UcaPhantomCommunicate {
    GObject parent_object;

    gboolean xenabled, timestamping, buffering;
    gchar *phantom_ip, *phantom_xip;
    gchar *netcard_ip, *netcard_xip;
    gchar *netcard, *xnetcard;
    guint control_port, data_port, discovery_port;
    guint8 mac_address[6];
    gchar *mac_address_str, *request_image_string;
    guint phantom_ipsource;

    ConnectionState control_connection_state;
    ConnectionState data_connection_state;
    ConnectionState xdata_connection_state;
    AcquisitionState local_acquisition_state;
    AcquisitionState phantom_acquisition_state;
    AcquisitionState buffer_state; // Currently buffering images or not ?

    // camera setup variables
    CaptureSettings settings;

    // Command stream connection variables
    GSocketConnection* control_connection;
    GSocketClient* control_client;
    GOutputStream* output_controlstream;
    GInputStream* input_controlstream;

    // Data stream connection variables (1 GbE)
    GSocketService* service;
    GSocketConnection* data_connection;
    GInputStream* input_datastream;
    GOutputStream* output_datastream;

    // Data stream connection variables (10 GbE)
    pcap_t* handle;
    GThread* data_receiver;
    GThread* data_unpacker;
    GThread* ts_receiver;
    GPtrArray* unpacked_images;
    GPtrArray* capture_settings;
    guint buffer_index_counter; // used to keep track of the buffer index of the
                                // unpacked_images
    GAsyncQueue* packed_queue;
    GAsyncQueue* unpacked_queue;
    GAsyncQueue* ts_queue;
    GAsyncQueue* ts_request_queue;
    GAsyncQueue* request_queue;

    // Live buffering
    ringbuf_t ring_buffer;
    GThread* buffering_thread;
    GThread* buffer_request_thread;
};

G_DEFINE_TYPE(UcaPhantomCommunicate, uca_phantom_communicate, G_TYPE_OBJECT)

G_DEFINE_QUARK("uca-phantom-communicate-error-quark", uca_phantom_communicate_error)

static void uca_phantom_communicate_class_init(UcaPhantomCommunicateClass* class)
{
    GObjectClass* gobject_class = G_OBJECT_CLASS(class);

    gobject_class->set_property = uca_phantom_communicate_set_property;
    gobject_class->get_property = uca_phantom_communicate_get_property;
    gobject_class->constructed = uca_phantom_communicate_constructed;
    gobject_class->dispose = uca_phantom_communicate_dispose;
    gobject_class->finalize = uca_phantom_communicate_finalize;

    // install properties
    uca_phantom_communicate_properties[PROP_PHANTOM_IP] = g_param_spec_string(
        "phantom_ip", "Phantom IP address", "IP address of the Phantom camera over normal 1Gb ethernet",
        "100.100.189.164", G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_PHANTOM_XIP] = g_param_spec_string(
        "phantom_xip", "Phantom 10Gb IP address", "IP address of the Phantom camera over a 10Gb ethernet",
        "172.16.31.157", G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_NETCARD_IP] = g_param_spec_string("netcard_ip", "Network card IP", "IP address of the network card used for 1Gb ethernet",
        "100.100.100.1", G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_NETCARD_XIP] = g_param_spec_string(
        "netcard_xip", "10Gb network card IP", "IP address of the network card used for 10Gb ethernet", "172.16.0.1",
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_NETCARD] = g_param_spec_string("netcard", "Network card", "Name of the network card used for 1Gb ethernet", "eth0",
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_XNETCARD] = g_param_spec_string("xnetcard", "10Gb network card", "Name of the network card used for 10Gb ethernet", "eth1",
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_XENABLED] = g_param_spec_boolean("xenabled", "Enable 10Gb data transfer", "Enable 10Gb data transfer", TRUE,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

    uca_phantom_communicate_properties[PROP_CONTROL_PORT] = g_param_spec_uint(
        "control_port", "Set connection port", "Set the port used to establish TCP connection with phantom", 1024,
        49151, 7115, G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

    uca_phantom_communicate_properties[PROP_PHANTOM_IPSOURCE] = g_param_spec_uint(
        "phantom_ipsource", "Set the IP source using IP flags", "Possible flags: USE_ENV, USE_CLASS, USE_DISCOVER.", 0,
        N_IP_FLAGS, USE_CLASS, G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

    uca_phantom_communicate_properties[PROP_TIMESTAMPING] = g_param_spec_boolean(
        "timestamping", "Enable timestamping", "Enable timestamping", FALSE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

    uca_phantom_communicate_properties[PROP_BUFFERING] = g_param_spec_boolean(
        "buffering", "Enable buffering", "Enable buffering of the data in a ring buffer", FALSE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

    g_object_class_install_properties(gobject_class, N_PROPERTIES, uca_phantom_communicate_properties);
}

static void uca_phantom_communicate_init(UcaPhantomCommunicate* instance)
{
    instance->phantom_ip = NULL;
    instance->phantom_xip = NULL;
    instance->netcard = NULL;
    instance->xnetcard = NULL;
    instance->netcard_ip = NULL;
    instance->netcard_xip = NULL;
    instance->xenabled = TRUE;
    instance->timestamping = FALSE;
    instance->mac_address_str = NULL;
    instance->request_image_string = g_strdup("{cine:%d, start:%d, cnt:%d, fmt:%s %s}");
    instance->data_port = 7116;
    instance->control_port = 7115;
    instance->discovery_port = 7380;

    instance->control_connection_state = DISCONNECTED;
    instance->data_connection_state = DISCONNECTED;
    instance->xdata_connection_state = DISCONNECTED;
    instance->local_acquisition_state = IDLE;
    instance->phantom_acquisition_state = IDLE;

    // create a new control connection
    instance->control_client = g_socket_client_new();
    instance->control_connection = NULL;
    instance->input_controlstream = NULL;
    instance->output_controlstream = NULL;

    // create a new data connection
    instance->service = g_socket_service_new();
    instance->data_connection = NULL;
    instance->input_datastream = NULL;
    instance->output_datastream = NULL;

    instance->unpacked_images = g_ptr_array_new_with_free_func(g_free);
    instance->capture_settings = g_ptr_array_new_with_free_func(g_free);
    instance->data_receiver = NULL;
    instance->data_unpacker = NULL;
    instance->unpacked_queue = g_async_queue_new();
    instance->request_queue = g_async_queue_new();
    instance->ts_request_queue = g_async_queue_new();
    instance->ts_queue = g_async_queue_new();

    instance->buffering = FALSE;
    instance->buffer_state = ACQUIRING;

    // Camera setup variables
    instance->settings = (CaptureSettings) {
        .sync_mode = SYNC_MODE_VIDEO_FRAME_RATE,
        .acquisition_mode = ACQUISITION_MODE_STANDARD,
        .image_format = IMG_P10,
        .timestamp_format = TS_NONE,
        .sensor_pixel_width = 0,
        .sensor_pixel_height = 0,
        .sensor_bit_depth = ImageFormatSpecs[IMG_P12L].bit_depth,
        .trigger_source = UCA_CAMERA_TRIGGER_SOURCE_SOFTWARE,
        .trigger_type = UCA_CAMERA_TRIGGER_TYPE_EDGE,
        .frames_per_second = 1000.0,
        .exposure_time = 0.00009,
        .edr_exp = 0.00009,
        .roi_pixel_x = 0,
        .roi_pixel_y = 0,
        .roi_pixel_width = 2048,
        .roi_pixel_height = 1952,
        .roi_width_multiplier = 1,
        .roi_height_multiplier = 1,
        .focal_length = 0.0,
        .aperture = 0.0,
        .shutter_off = 1,
        .aexpmode = AUTO_EXP_MODE_AVERAGE,
        .aexpcomp = 0.0,
        .nb_post_trigger_frames = 0,
        .nb_pre_trigger_frames = 1,
        .current_cine = 1,
    };
}

static void uca_phantom_communicate_constructed(GObject* object)
{
    UcaPhantomCommunicate* instance = UCA_PHANTOM_COMMUNICATE(object);

    instance->control_connection_state = DISCONNECTED;

    if (instance->xenabled)
        instance->packed_queue = g_async_queue_new();
    else
        instance->packed_queue = NULL;

    G_OBJECT_CLASS(uca_phantom_communicate_parent_class)->constructed(object);
}

static void uca_phantom_communicate_dispose(GObject* object)
{
    UcaPhantomCommunicate* instance = UCA_PHANTOM_COMMUNICATE(object);
    // free the handle
    if (instance->handle != NULL) {
        pcap_close(instance->handle);
    }

    // if (instance->data_connection != NULL) {
    //     g_socket_close (instance->data_connection, NULL);
    // }

    g_free(instance->phantom_ip);
    g_free(instance->phantom_xip);
    g_free(instance->netcard_ip);
    g_free(instance->netcard_xip);
    g_free(instance->netcard);
    g_free(instance->xnetcard);
    g_free(instance->mac_address_str);
    g_free(instance->request_image_string);

    // Empty the unpacked_images and the settings arrays
    g_ptr_array_free(instance->unpacked_images, TRUE);
    g_ptr_array_free(instance->capture_settings, TRUE);

    // Empty the queues, even though they should be empty
    while (instance->packed_queue && g_async_queue_length(instance->packed_queue) > 0) {
        CineData* cine_data = g_async_queue_try_pop(instance->packed_queue);
        g_free(cine_data);
    }
    while (instance->unpacked_queue && g_async_queue_length(instance->unpacked_queue) > 0) {
        CineData* cine_data = g_async_queue_try_pop(instance->unpacked_queue);
        g_free(cine_data);
    }

    // Disconnect the data and control streams
    GError* error = NULL;
    if (!g_io_stream_close(G_IO_STREAM(instance->control_connection), NULL, &error)) {
        g_warning("Failed to close stream: %s", error->message);
        g_error_free(error);
    }

    if (instance->timestamping || !instance->xenabled) {
        // Empty the timestamp queue
        while (instance->ts_queue && g_async_queue_length(instance->ts_queue) > 0) {
            TimestampData* ts_data = g_async_queue_try_pop(instance->ts_queue);
            g_free(ts_data);
        }
        // Empty the timestamp request queue
        while (instance->ts_request_queue && g_async_queue_length(instance->ts_request_queue) > 0) {
            TsRequest* ts_request = g_async_queue_try_pop(instance->ts_request_queue);
            g_free(ts_request);
        }
        // TODO: check this function
        if (instance->data_connection && !g_io_stream_close(G_IO_STREAM(instance->data_connection), NULL, &error)) {
            g_warning("Failed to close stream: %s", error->message);
            g_error_free(error);
        }
    }

    G_OBJECT_CLASS(uca_phantom_communicate_parent_class)->dispose(object);
}

static void uca_phantom_communicate_finalize(GObject* object)
{
    UcaPhantomCommunicate* instance = UCA_PHANTOM_COMMUNICATE(object);

    g_socket_service_stop(instance->service);

    // Free control connection resources
    if (G_IS_SOCKET_CLIENT(instance->control_client)) {
        g_object_unref(instance->control_client);
    }
    if (G_IS_SOCKET_CONNECTION(instance->control_connection)) {
        g_object_unref(instance->control_connection);
    }

    // Free data connection resources
    if (G_IS_SOCKET_SERVICE(instance->service)) {
        g_object_unref(instance->service);
    }
    if (G_IS_SOCKET_CONNECTION(instance->data_connection)) {
        g_object_unref(instance->data_connection);
    }

    // Free the 10GbE resources
    if (instance->packed_queue != NULL) {
        g_async_queue_unref(instance->packed_queue);
    }
    if (instance->unpacked_queue != NULL) {
        g_async_queue_unref(instance->unpacked_queue);
    }
    if (instance->request_queue != NULL) {
        g_async_queue_unref(instance->request_queue);
    }
    if (instance->ts_queue != NULL) {
        g_async_queue_unref(instance->ts_queue);
    }
    if (instance->ts_request_queue != NULL) {
        g_async_queue_unref(instance->ts_request_queue);
    }

    G_OBJECT_CLASS(uca_phantom_communicate_parent_class)->finalize(object);
}

/*
 * Private class definitions
 */
static void uca_phantom_communicate_set_phantom_ip(UcaPhantomCommunicate* self, const gchar* property)
{
    g_free(self->phantom_ip);
    self->phantom_ip = g_strdup(property);
}
static void uca_phantom_communicate_set_phantom_xip(UcaPhantomCommunicate* self, const gchar* property)
{
    g_free(self->phantom_xip);
    self->phantom_xip = g_strdup(property);
}
static void uca_phantom_communicate_set_netcard_ip(UcaPhantomCommunicate* self, const gchar* property)
{
    g_free(self->netcard_ip);
    self->netcard_ip = g_strdup(property);
}
static void uca_phantom_communicate_set_netcard_xip(UcaPhantomCommunicate* self, const gchar* property)
{
    g_free(self->netcard_xip);
    self->netcard_xip = g_strdup(property);
}
static void uca_phantom_communicate_set_netcard(UcaPhantomCommunicate* self, const gchar* property)
{
    g_free(self->netcard);
    self->netcard = g_strdup(property);
}
static void uca_phantom_communicate_set_xnetcard(UcaPhantomCommunicate* self, const gchar* property)
{
    g_free(self->xnetcard);
    self->xnetcard = g_strdup(property);
}
static void uca_phantom_communicate_set_xenabled(UcaPhantomCommunicate* self, gboolean property)
{
    self->xenabled = property;
}
static void uca_phantom_communicate_set_control_port(UcaPhantomCommunicate* self, guint property)
{
    self->control_port = property;
}
static void uca_phantom_communicate_set_ip_source(UcaPhantomCommunicate* self, guint property)
{
    self->phantom_ipsource = property;
}
static void uca_phantom_communicate_set_timestamping(UcaPhantomCommunicate* self, gboolean property)
{
    self->timestamping = property;
}
static void uca_phantom_communicate_set_buffering (UcaPhantomCommunicate* self, gboolean property)
{
    self->buffering = property;
}

static gchar* uca_phantom_communicate_get_phantom_ip(UcaPhantomCommunicate* self)
{
    return self->phantom_ip;
}
static gchar* uca_phantom_communicate_get_phantom_xip(UcaPhantomCommunicate* self)
{
    return self->phantom_xip;
}
static gchar* uca_phantom_communicate_get_netcard_ip(UcaPhantomCommunicate* self)
{
    return self->netcard_ip;
}
static gchar* uca_phantom_communicate_get_netcard_xip(UcaPhantomCommunicate* self)
{
    return self->netcard_xip;
}
static gchar* uca_phantom_communicate_get_netcard(UcaPhantomCommunicate* self)
{
    return self->netcard;
}
static gchar* uca_phantom_communicate_get_xnetcard(UcaPhantomCommunicate* self)
{
    return self->xnetcard;
}
static gboolean uca_phantom_communicate_get_xenabled(UcaPhantomCommunicate* self)
{
    return self->xenabled;
}
static guint uca_phantom_communicate_get_control_port(UcaPhantomCommunicate* self)
{
    return self->control_port;
}
static guint uca_phantom_communicate_get_ip_source(UcaPhantomCommunicate* self)
{
    return self->phantom_ipsource;
}
static gboolean uca_phantom_communicate_get_timestamping(UcaPhantomCommunicate* self)
{
    return self->timestamping;
}
static gboolean uca_phantom_communicate_get_buffering(UcaPhantomCommunicate* self)
{
    return self->buffering;
}

static void uca_phantom_communicate_set_property(GObject* object, guint property_id, const GValue* value,
    GParamSpec* pspec)
{
    UcaPhantomCommunicate* self = UCA_PHANTOM_COMMUNICATE(object);

    switch (property_id) {
    case PROP_PHANTOM_IP:
        uca_phantom_communicate_set_phantom_ip(self, g_value_get_string(value));
        break;
    case PROP_PHANTOM_XIP:
        uca_phantom_communicate_set_phantom_xip(self, g_value_get_string(value));
        break;
    case PROP_NETCARD_IP:
        uca_phantom_communicate_set_netcard_ip(self, g_value_get_string(value));
        break;
    case PROP_NETCARD_XIP:
        uca_phantom_communicate_set_netcard_xip(self, g_value_get_string(value));
        break;
    case PROP_NETCARD:
        uca_phantom_communicate_set_netcard(self, g_value_get_string(value));
        break;
    case PROP_XNETCARD:
        uca_phantom_communicate_set_xnetcard(self, g_value_get_string(value));
        break;
    case PROP_XENABLED:
        uca_phantom_communicate_set_xenabled(self, g_value_get_boolean(value));
        break;
    case PROP_CONTROL_PORT:
        uca_phantom_communicate_set_control_port(self, g_value_get_uint(value));
        break;
    case PROP_PHANTOM_IPSOURCE:
        uca_phantom_communicate_set_ip_source(self, g_value_get_uint(value));
        break;
    case PROP_TIMESTAMPING:
        uca_phantom_communicate_set_timestamping(self, g_value_get_boolean(value));
        break;
    case PROP_BUFFERING:
        uca_phantom_communicate_set_buffering(self, g_value_get_boolean(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void uca_phantom_communicate_get_property(GObject* object, guint property_id, GValue* value, GParamSpec* pspec)
{
    UcaPhantomCommunicate* self = UCA_PHANTOM_COMMUNICATE(object);

    switch (property_id) {
    case PROP_PHANTOM_IP:
        g_value_set_string(value, uca_phantom_communicate_get_phantom_ip(self));
        break;
    case PROP_PHANTOM_XIP:
        g_value_set_string(value, uca_phantom_communicate_get_phantom_xip(self));
        break;
    case PROP_NETCARD_IP:
        g_value_set_string(value, uca_phantom_communicate_get_netcard_ip(self));
        break;
    case PROP_NETCARD_XIP:
        g_value_set_string(value, uca_phantom_communicate_get_netcard_xip(self));
        break;
    case PROP_NETCARD:
        g_value_set_string(value, uca_phantom_communicate_get_netcard(self));
        break;
    case PROP_XNETCARD:
        g_value_set_string(value, uca_phantom_communicate_get_xnetcard(self));
        break;
    case PROP_XENABLED:
        g_value_set_boolean(value, uca_phantom_communicate_get_xenabled(self));
        break;
    case PROP_CONTROL_PORT:
        g_value_set_uint(value, uca_phantom_communicate_get_control_port(self));
        break;
    case PROP_PHANTOM_IPSOURCE:
        g_value_set_uint(value, uca_phantom_communicate_get_ip_source(self));
        break;
    case PROP_TIMESTAMPING:
        g_value_set_boolean(value, uca_phantom_communicate_get_timestamping(self));
        break;
    case PROP_BUFFERING:
        g_value_set_boolean(value, uca_phantom_communicate_get_buffering(self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static gchar* uca_phantom_communicate_discover(UcaPhantomCommunicate* self, GError** error_loc)
{
    // Note: find a way to do this without a goto statement.
    g_return_val_if_fail (UCA_IS_PHANTOM_COMMUNICATE(self), NULL);
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, NULL);

    GError* sub_error = NULL;
    GError* phantom_error = NULL;
    GSocket* socket = NULL;
    GMatchInfo* info = NULL;
    GSocketAddress* remote_socket_addr = NULL;
    GSocketAddress* result = NULL;
    const gchar request[] = "phantom?";
    const gchar pattern[] = "PH16 (\\d+) (\\d+) (\\d+)";
    gint FLAG = ALL;
    guint port = self->discovery_port;

    gchar reply[128] = {
        0,
    };

    g_message("Attempting to discover the phantom...\n");

    const gchar* bcast_address = "100.100.255.255";
    GSocketAddress* bcast_socket_addr = g_inet_socket_address_new_from_string(bcast_address, port);

    if (bcast_socket_addr == NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_BCAST_ADDR,
            "Failed to parse broadcasting address '%s' on port '%d'\n", bcast_address, port);
        g_propagate_error(error_loc, phantom_error);

        FLAG = BCAST;
        goto cleanup;
    }

    socket = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, &sub_error);

    if (socket == NULL) {
        if (sub_error == NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SOCKET,
                "Failed to create socket\n");
        } else {
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

    if (wrote < -1) {
        if (sub_error == NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SEND,
                "Failed to send broadcast\n");
        } else {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SEND,
                "Failed to send broadcast: %s\n", sub_error->message);
            g_error_free(sub_error);
        }
        g_propagate_error(error_loc, phantom_error);

        FLAG = SOCKET_END;
        goto cleanup;
    }

    gssize received = g_socket_receive_from(socket, &remote_socket_addr, reply, strlen(reply), NULL, &sub_error);
    g_print("received: %ld\n", received);

    if (received < -1) {
        if (sub_error == NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RECEIVE,
                "Failed to receive broadcast\n");
        } else {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RECEIVE,
                "Failed to receive broadcast: %s\n", sub_error->message);
            g_error_free(sub_error);
        }
        g_propagate_error(error_loc, phantom_error);

        FLAG = RECEIVE;
        goto cleanup;
    }

    GRegex* regex = g_regex_new(pattern, 0, 0, &sub_error);

    if (regex == NULL) {
        if (sub_error == NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REGEX,
                "Failed to create regex\n");
        } else {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REGEX,
                "Failed to create regex: %s\n", sub_error->message);
            g_error_free(sub_error);
        }
        g_propagate_error(error_loc, phantom_error);

        FLAG = REGEX;
        goto cleanup;
    }

    gboolean matched = g_regex_match(regex, reply, 0, &info);

    if (!matched) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REGEX,
            "Reply '%s' does not match expected pattern.\n", reply);
        g_propagate_error(error_loc, phantom_error);

        FLAG = ALL;
        goto cleanup;
    }

    gchar* port_string = g_match_info_fetch(info, 1);

    if (port_string == NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REGEX,
            "Regex pattern number 1 not found.\n");
        g_propagate_error(error_loc, phantom_error);

        FLAG = ALL;
        goto cleanup;
    }

    g_free(port_string);

    result = g_inet_socket_address_new(g_inet_socket_address_get_address((GInetSocketAddress*)remote_socket_addr), port);

    gchar* ip_address = g_inet_address_to_string(g_inet_socket_address_get_address((GInetSocketAddress*)result));

    g_message("Phantom found on port %d with the IPV4 address: %s.\n", port, ip_address);

cleanup:
    switch (FLAG) {
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
        g_warning("Flag set to invalid value! Fatal error.");
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
static gboolean uca_phantom_communicate(UcaPhantomCommunicate* self, PhantomRequest* request, PhantomReply* reply,
    GError** error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(request != NULL || reply != NULL, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);
    g_return_val_if_fail(G_IS_INPUT_STREAM(self->input_controlstream), FALSE);
    g_return_val_if_fail(G_IS_OUTPUT_STREAM(self->output_controlstream), FALSE);

    GError* sub_error = NULL;
    GError* phantom_error = NULL;

    request->write_size = g_output_stream_write(self->output_controlstream, request->message, request->size, NULL, &sub_error);

    if (request->write_size < -1) {
        g_warning("Failed to write request to control output stream: %s\n", sub_error->message);
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SEND,
            "Failed to write request to control output stream: %s\n", sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_clear_error(&sub_error);

        return FALSE;
    }

    reply->read_size = g_input_stream_read(self->input_controlstream, reply->raw, reply->size, NULL, &sub_error);

    if (reply->read_size < -1) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RECEIVE,
            "Failed to read reply from control input stream: %s\n", sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_clear_error(&sub_error);

        return FALSE;
    } else if (reply->read_size == 0) {
        g_warning("Reached EOF on stream.\n");
    }

    g_debug("uca_phantom_communicate: %s\n", reply->raw);

    g_output_stream_flush(self->output_controlstream, NULL, NULL);

    return TRUE;
}

/*
 * Public methods
 *
 */
UcaPhantomCommunicate* uca_phantom_communicate_new(void)
{
    return g_object_new(UCA_TYPE_PHANTOM_COMMUNICATE, NULL);
}

gboolean uca_phantom_communicate_connect_controlstream(UcaPhantomCommunicate* self, GError** error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(self->control_connection_state == DISCONNECTED, FALSE);

    GError* sub_error = NULL;
    GError* phantom_error = NULL;
    gchar* ip_address = NULL;

    switch (self->phantom_ipsource) {
    case USE_CLASS:
        ip_address = g_strdup(self->phantom_ip);
        break;
    case USE_BCAST:
        ip_address = uca_phantom_communicate_discover(self, &sub_error);
        break;
    default:
        g_warning("Invalid ip source.\n");
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT,
            "Invalid ip source.\n");
        g_propagate_error(error_loc, phantom_error);
        return FALSE;
        break;
    }

    // /* resolve the server address */
    // server_address = g_inet_socket_address_new_from_string(ip_address,
    // self->control_port); if (!server_address) {
    //     g_print("Invalid address!\n");
    //     return 1;
    // }

    self->control_connection = g_socket_client_connect_to_host(self->control_client, ip_address, self->control_port, NULL, &sub_error);

    g_message("Connected to server!\n");

    g_free(ip_address);

    if (self->control_connection == NULL) {
        if (sub_error != NULL) {
            g_warning("Could not connect to the phantom: %s\n", sub_error->message);
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT,
                "Could not connect to the phantom: %s\n", sub_error->message);
            g_propagate_error(error_loc, phantom_error);
        } else {
            g_warning("Could not connect to the phantom.\n");
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT,
                "Could not connect to the phantom: Unknown error when "
                "attempting to connect.\n");
            g_propagate_error(error_loc, phantom_error);
        }

        return FALSE;
    }

    self->control_connection_state = CONNECTED;

    self->output_controlstream = g_io_stream_get_output_stream(G_IO_STREAM(self->control_connection));
    self->input_controlstream = g_io_stream_get_input_stream(G_IO_STREAM(self->control_connection));

    if (!G_IS_OUTPUT_STREAM(self->output_controlstream) || !G_IS_INPUT_STREAM(self->input_controlstream)) {
        g_warning("Could not get control streams.\n");
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT,
            "Could not get control streams.\n");
        g_propagate_error(error_loc, phantom_error);
        return FALSE;
    }

    // Get the remote address of the connection
    GSocketAddress* remote_address = g_socket_connection_get_remote_address(self->control_connection, &sub_error);
    if (sub_error != NULL || remote_address == NULL) {
        g_warning("Could not get remote address: %s\n", sub_error->message);
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT,
            "Could not get remote address: %s\n", sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_clear_error(&sub_error);
    }
    GSocketAddress* local_address = g_socket_connection_get_local_address(self->control_connection, &sub_error);
    if (sub_error != NULL || local_address == NULL) {
        g_warning("Could not get local address: %s\n", sub_error->message);
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT,
            "Could not get local address: %s\n", sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_clear_error(&sub_error);
    }

    guint remote_port = g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(remote_address));
    guint local_port = g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(local_address));

    GInetAddress* remote_inet_address = g_inet_socket_address_get_address(G_INET_SOCKET_ADDRESS(remote_address));
    GInetAddress* local_inet_address = g_inet_socket_address_get_address(G_INET_SOCKET_ADDRESS(local_address));

    gchar* remote_ip_address = g_inet_address_to_string(remote_inet_address);
    gchar* local_ip_address = g_inet_address_to_string(local_inet_address);

    g_message("Control connection established with phantom (%s:%d) -> (%s:%d)\n", local_ip_address, local_port,
        remote_ip_address, remote_port);

    g_object_unref(remote_address);
    g_object_unref(local_address);

    g_free(remote_ip_address);
    g_free(local_ip_address);

    return TRUE;
}

gboolean uca_phantom_communicate_run_command(UcaPhantomCommunicate* self, guint command_flag, gchar* command_arg,
    PhantomReply* reply, GError** error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(command_flag < N_UNIT_COMMANDS, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);

    GError* sub_error = NULL;
    GError* phantom_error = NULL;

    gboolean reply_is_local = FALSE;

    // If the reply is null, just make a new one
    if (reply == NULL) {
        reply_is_local = TRUE;
        reply = g_new0(PhantomReply, 1);
    }

    // Setup the request
    PhantomRequest request = { .command = Commands[command_flag], .message = NULL, .size = 0, .write_size = 0 };

    if (command_arg == NULL) {
        command_arg = "";
    }

    // Manually build the message to ensure that the string is correclty
    // CRLF-ended
    request.size = (strlen(request.command.name) + 1 + strlen(command_arg) + strlen("\r\n")) * sizeof(gchar);
    request.message = g_strconcat(request.command.name, " ", command_arg, "\r\n", NULL);

    if (request.message == NULL || strlen(request.message) != request.size) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RUN_COMMAND,
            "Could not allocate and assemble request message. Fatal error.\n");
        g_propagate_error(error_loc, phantom_error);
        return FALSE;
    }

    // Setup the reply
    *reply = (PhantomReply) { .raw = NULL,
        .size = 65536, // ref : v16.0.0
        .value = G_VALUE_INIT,
        .read_size = 0 };

    reply->raw = g_malloc0(reply->size);

    if (reply->raw == NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RUN_COMMAND,
            "Could not allocate reply message. Fatal error.\n");
        g_propagate_error(error_loc, phantom_error);
        g_free(request.message);
        return FALSE;
    }

    // Communicate request to phantom
    gboolean communicated = uca_phantom_communicate(self, &request, reply, &sub_error);

    g_message("> request:\n%s \n", request.message);
    g_message("> reply:\n%s \n", reply->raw);
    g_free(request.message);
    request.message = NULL;

    if (!communicated && sub_error != NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RUN_COMMAND,
            "Failed to run command %s: %s\n", request.command.name, sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_clear_error(&sub_error);

        g_free(reply->raw);
        return FALSE;
    }

    if (communicated && sub_error != NULL) {
        g_warning("Successfully ran command %s. However, an error occured: %s\n", request.command.name,
            sub_error->message);
        g_clear_error(&sub_error);
    }

    if (g_str_has_prefix(reply->raw, "ERR:") == TRUE) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RUN_COMMAND,
            "Phantom returned an error when running command '%s': \n\t> %s\n", request.command.name,
            reply->raw);
        g_propagate_error(error_loc, phantom_error);
        g_free(reply->raw);
        reply->raw = NULL;

        return FALSE;
    }

    if (reply_is_local == TRUE) {
        g_free(reply->raw);
        reply->raw = NULL;
        g_free(reply);
        reply = NULL;
    }

    return TRUE;
}

gboolean uca_phantom_communicate_get_variable(UcaPhantomCommunicate* self, guint variable_flag, GValue* return_value,
    GError** error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(variable_flag < N_UNIT_PROPERTIES, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);

    GError* sub_error = NULL;
    GError* phantom_error = NULL;
    gchar pattern[] = "\\s:\\s";
    PhantomReply reply;
    gchar* name = NULL;
    gboolean res = FALSE;

    // Check if the command is between CT_STATE and CT_META_GPS
    if (variable_flag >= UNIT_CT_STATE && variable_flag <= UNIT_CT_META_GPS) {
        name = g_strdup_printf(variables[variable_flag].name, self->settings.current_cine);
        res = uca_phantom_communicate_run_command(self, CMD_GET, name, &reply, &sub_error);
        g_free(name);
    } else {
        res = uca_phantom_communicate_run_command(self, CMD_GET, (gchar *)variables[variable_flag].name, &reply, &sub_error);
    }

    if (res != TRUE && sub_error != NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_VARIABLE,
            "Failed to get variable %s: %s\n", variables[variable_flag].name, sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_clear_error(&sub_error);
        return FALSE;
    }

    // Extract the actual data from the raw reply
    GRegex* regex = g_regex_new(pattern, 0, 0, &sub_error);

    if (regex == NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_VARIABLE,
            "Failed to create regex object. Aborting...: %s\n", sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_error_free(sub_error);

        g_free(reply.raw);
        return FALSE;
    }

    gchar** matched = g_regex_split(regex, reply.raw, 0);
    g_return_val_if_fail(matched != NULL, FALSE);
    gchar* prefix = matched[0];
    gchar* suffix = matched[1];

    if (suffix == NULL || prefix == NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_VARIABLE,
            "Failed to extract variable value from reply: %s\n", reply.raw);
        g_propagate_error(error_loc, phantom_error);

        g_free(reply.raw);
        g_strfreev(matched);
        g_regex_unref(regex);
        return FALSE;
    }

    // Check for error mesage from phantom
    if (g_str_has_prefix(prefix, "ERR")) {
        g_warning("Invalid phantom command: %s\n", reply.raw);

        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_VARIABLE,
            "Invalid phantom command: %s\n", reply.raw);
        g_propagate_error(error_loc, phantom_error);

        g_free(reply.raw);
        g_strfreev(matched);
        g_regex_unref(regex);
        return FALSE;
    }

    g_value_unset(return_value);
    g_value_init(return_value, variables[variable_flag].type);

    // Use Gvalue container to store it
    switch (variables[variable_flag].type) {
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
    // // TODO : handle these cases in a more custom way in the future ?
    // case PHANTOM_TYPE_HEX:
    // case PHANTOM_TYPE_RES:
    default:
        g_warning("Type not handled yet!\n");
        break;
    }

    // Cleanup
    g_strfreev(matched);
    matched = NULL;
    g_regex_unref(regex);
    regex = NULL;
    g_free(reply.raw);
    reply.raw = NULL;

    return TRUE;
}

gboolean uca_phantom_communicate_set_variable(UcaPhantomCommunicate* self, guint variable_flag, const gchar* value,
    GError** error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(variable_flag < N_UNIT_PROPERTIES, FALSE);
    g_return_val_if_fail(value != NULL, FALSE);
    g_return_val_if_fail(variables[variable_flag].flags & G_PARAM_WRITABLE, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);

    GError* sub_error = NULL;
    GError* phantom_error = NULL;
    PhantomReply reply;

    gchar* arg = g_strdup_printf("%s %s", variables[variable_flag].name, value);
    gboolean res = uca_phantom_communicate_run_command(self, CMD_SET, arg, &reply, &sub_error);

    if (res != TRUE && sub_error != NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_VARIABLE,
            "Failed to set variable '%s':\n\t> %s\n", variables[variable_flag].name, sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_clear_error(&sub_error);
        return FALSE;
    }
    g_free(reply.raw);
    reply.raw = NULL;
    g_free(arg);
    arg = NULL;

    return TRUE;
}

static gboolean uca_phantom_communicate_get_resolution(UcaPhantomCommunicate* self, guint16* width, guint16* height,
    GError** error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);

    GError* sub_error = NULL;
    GError* phantom_error = NULL;
    const gchar* pattern = "([0-9]+)\\sx\\s([0-9]+)";
    GValue resolution = G_VALUE_INIT;
    const gchar* reply = NULL;

    gboolean res = uca_phantom_communicate_get_variable(self, UNIT_DEFC_RES, &resolution, &sub_error);

    if (res != TRUE && sub_error != NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_RESOLUTION,
            "Failed to get resolution from phantom. Aborting...: %s\n", sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_error_free(sub_error);
        g_clear_error(&sub_error);
        return FALSE;
    }

    reply = g_value_get_string(&resolution);

    // Extract the actual data from the raw reply
    GRegex* regex = g_regex_new(pattern, 0, 0, &sub_error);

    if (regex == NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_RESOLUTION,
            "Failed to create regex object. Aborting...: %s\n", sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_error_free(sub_error);
        return FALSE;
    }

    gchar** matched = g_regex_split(regex, reply, 0);
    g_return_val_if_fail(matched != NULL, FALSE);

    // Check for error mesage from phantom
    if (g_str_has_prefix(matched[0], "ERR")) {
        g_warning("Invalid phantom command: %s\n", reply);

        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_VARIABLE,
            "Invalid phantom command: %s\n", reply);
        g_propagate_error(error_loc, phantom_error);

        g_strfreev(matched);
        g_regex_unref(regex);
        return FALSE;
    }

    gchar* width_s = matched[1];
    gchar* height_s = matched[2];

    *width = g_ascii_strtoull(width_s, NULL, 10);
    *height = g_ascii_strtoull(height_s, NULL, 10);

    g_print("Resolution: %dx%d\n", *width, *height);

    g_strfreev(matched);
    g_regex_unref(regex);
    g_value_unset(&resolution);

    return TRUE;
}

guint uca_phantom_communicate_get_pixel_width(UcaPhantomCommunicate* self)
{
    g_return_val_if_fail(self->control_connection_state == CONNECTED, 0);
    return self->settings.sensor_pixel_width;
}
guint uca_phantom_communicate_get_pixel_height(UcaPhantomCommunicate* self)
{
    g_return_val_if_fail(self->control_connection_state == CONNECTED, 0);
    return self->settings.sensor_pixel_height;
}

/**
 * Print the current capture settings
 */
void uca_phantom_communicate_print_settings(UcaPhantomCommunicate* self)
{
    g_return_if_fail(self != NULL);
    g_return_if_fail(self->control_connection_state == CONNECTED);
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
gboolean uca_phantom_communicate_set_nb_cines(UcaPhantomCommunicate* self, guint nb_cines, GError** error_loc)
{
    // Set the number of cines as well
    GError* sub_error = NULL;
    GError* phantom_error = NULL;
    gchar* arg = g_strdup_printf("%d", nb_cines);

    gboolean res = uca_phantom_communicate_run_command(self, CMD_PARTITION_CINE_MEMORY, arg, NULL, &sub_error);
    g_free(arg);
    if (res != TRUE) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_NB_CINES,
            "Failed to set number of cines: %s\n", sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_clear_error(&sub_error);
        return FALSE;
    }
    return TRUE;
}

gboolean uca_phantom_communicate_set_settings(UcaPhantomCommunicate* self, CaptureSettings* ext_settings,
    GError** error_loc)
{
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);
    static gboolean first_call = TRUE;

    GError* sub_error = NULL;
    GError* phantom_error = NULL;
    gboolean res;

    if (ext_settings->trigger_source != self->settings.trigger_source || first_call) {
        self->settings.trigger_source = ext_settings->trigger_source;
        // TODO: Set trigger source
    }
    if (ext_settings->trigger_type != self->settings.trigger_type || first_call) {
        self->settings.trigger_type = ext_settings->trigger_type;
        // TODO: Set trigger type
    }
    if (ext_settings->acquisition_mode != self->settings.acquisition_mode || first_call) {
        self->settings.acquisition_mode = ext_settings->acquisition_mode;
        // TODO: Set acquisition mode
    }

    if (ext_settings->image_format != self->settings.image_format || first_call) {
        self->settings.image_format = ext_settings->image_format;
        self->settings.sensor_bit_depth = ext_settings->sensor_bit_depth; // is set when requesting the images
    }

    if (ext_settings->focal_length != self->settings.focal_length || first_call) {
        g_message("Focal cannot be set by software\n"); // Focal length can only be
                                                        // set manually
    }
    if (ext_settings->nb_pre_trigger_frames != self->settings.nb_pre_trigger_frames || first_call) {
        self->settings.nb_pre_trigger_frames = ext_settings->nb_pre_trigger_frames; // is set when requesting the images
    }
    if (ext_settings->current_cine != self->settings.current_cine || first_call) {
        self->settings.current_cine = ext_settings->current_cine; // is set when requesting the images
    }

    if (ext_settings->timestamp_format != self->settings.timestamp_format || first_call) {
        self->settings.timestamp_format = ext_settings->timestamp_format;
        if (self->settings.timestamp_format != TS_NONE) {
            self->timestamping = TRUE;
        } else {
            self->timestamping = FALSE;
        }
        res = uca_phantom_communicate_set_variable(
            self, UNIT_CAM_TSFORMAT, TimestampSpecs[self->settings.timestamp_format].format_string, &sub_error);
        if (res != TRUE && sub_error != NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_SETTINGS,
                "Failed to set timestamp format to %d:\n\t> %s\n", ext_settings->timestamp_format,
                sub_error->message);
            g_propagate_error(error_loc, phantom_error);
            g_clear_error(&sub_error);
            return FALSE;
        }
    }

    if (ext_settings->sync_mode != self->settings.sync_mode || first_call) {
        self->settings.sync_mode = ext_settings->sync_mode;
        gchar* sync_mode = g_strdup_printf("%d", ext_settings->sync_mode);
        res = uca_phantom_communicate_set_variable(self, UNIT_CAM_SYNCIMG, sync_mode, &sub_error);
        g_free(sync_mode);
        if (res != TRUE && sub_error != NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_SETTINGS,
                "Failed to set sync mode to %d:\n\t> %s\n", ext_settings->sync_mode, sub_error->message);
            g_propagate_error(error_loc, phantom_error);
            g_error_free(sub_error);
            return FALSE;
        }
    }

    if (ext_settings->frames_per_second != self->settings.frames_per_second || first_call) {
        self->settings.frames_per_second = ext_settings->frames_per_second;
        gchar* fps = g_strdup_printf("%f", ext_settings->frames_per_second);
        res = uca_phantom_communicate_set_variable(self, UNIT_DEFC_RATE, fps, &sub_error);
        g_free(fps);
        if (res != TRUE && sub_error != NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_SETTINGS,
                "Failed to set frames per second to %f:\n\t> %s\n", ext_settings->frames_per_second,
                sub_error->message);
            g_propagate_error(error_loc, phantom_error);
            g_clear_error(&sub_error);
            return FALSE;
        }
    }
    if (ext_settings->exposure_time != self->settings.exposure_time || first_call) {
        self->settings.exposure_time = ext_settings->exposure_time;
        gchar* exposure = g_strdup_printf("%f", ext_settings->exposure_time);
        res = uca_phantom_communicate_set_variable(self, UNIT_DEFC_EXP, exposure, &sub_error);
        g_free(exposure);
        if (res != TRUE && sub_error != NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_SETTINGS,
                "Failed to set exposure time to %f:\n\t> %s\n", ext_settings->exposure_time,
                sub_error->message);
            g_propagate_error(error_loc, phantom_error);
            g_clear_error(&sub_error);
            return FALSE;
        }
    }
    if (ext_settings->roi_pixel_x != self->settings.roi_pixel_x || first_call) {
        self->settings.roi_pixel_x = ext_settings->roi_pixel_x;
        gchar* roi = g_strdup_printf("%d", ext_settings->roi_pixel_x);
        res = uca_phantom_communicate_set_variable(self, UNIT_DEFC_META_OX, roi, &sub_error);
        g_free(roi);
        if (res != TRUE && sub_error != NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_SETTINGS,
                "Failed to set ROI to %dx%d:\n\t> %s\n", ext_settings->roi_pixel_x, ext_settings->roi_pixel_y,
                sub_error->message);
            g_propagate_error(error_loc, phantom_error);
            g_clear_error(&sub_error);
            return FALSE;
        }
    }
    if (ext_settings->roi_pixel_y != self->settings.roi_pixel_y || first_call) {
        self->settings.roi_pixel_y = ext_settings->roi_pixel_y;
        gchar* roi = g_strdup_printf("%d", ext_settings->roi_pixel_y);
        res = uca_phantom_communicate_set_variable(self, UNIT_DEFC_META_OY, roi, &sub_error);
        g_free(roi);
        if (res != TRUE && sub_error != NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_SETTINGS,
                "Failed to set ROI to %dx%d:\n\t> %s\n", ext_settings->roi_pixel_x, ext_settings->roi_pixel_y,
                sub_error->message);
            g_propagate_error(error_loc, phantom_error);
            g_clear_error(&sub_error);
            return FALSE;
        }
    }
    if ((ext_settings->roi_pixel_width != self->settings.roi_pixel_width || ext_settings->roi_pixel_height != self->settings.roi_pixel_height) || first_call) {
        self->settings.roi_pixel_height = ext_settings->roi_pixel_height;
        self->settings.roi_pixel_width = ext_settings->roi_pixel_width;
        gchar* resolution = g_strdup_printf("%dx%d", ext_settings->roi_pixel_width, ext_settings->roi_pixel_height);
        res = uca_phantom_communicate_set_variable(self, UNIT_DEFC_RES, resolution, &sub_error);
        g_free(resolution);
        if (res != TRUE && sub_error != NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_SETTINGS,
                "Failed to set sensor resolution %dx%d:\n\t> %s\n", ext_settings->sensor_pixel_width,
                ext_settings->sensor_pixel_height, sub_error->message);
            g_propagate_error(error_loc, phantom_error);
            g_clear_error(&sub_error);
            return FALSE;
        }
    }
    if (ext_settings->aperture != self->settings.aperture || first_call) {
        g_message("Aperture cannot be set by software\n"); // Focal length can only
                                                           // be set manually

        // PhantomReply reply;
        // self->settings.aperture = ext_settings->aperture;
        // gchar *aperture = g_strdup_printf("%f", ext_settings->aperture);
        // res = uca_phantom_communicate_run_command(self, CMD_SET_LENS_APERTURE,
        // &reply, &sub_error, aperture, NULL); g_free(aperture); g_free(reply.raw);
        // if (res != TRUE && sub_error != NULL) {
        //     g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR,
        //     UCA_PHANTOM_COMMUNICATE_ERROR_SET_SETTINGS, "Failed to set aperture
        //     to %f:\n\t> %s\n", ext_settings->aperture, sub_error->message);
        //     g_propagate_error(error_loc, phantom_error);
        //     g_clear_error(&sub_error);
        // }
    }
    if (ext_settings->edr_exp != self->settings.edr_exp || first_call) {
        self->settings.edr_exp = ext_settings->edr_exp;
        gchar* edr_exp = g_strdup_printf("%d", ext_settings->edr_exp);
        res = uca_phantom_communicate_set_variable(self, UNIT_DEFC_EDREXP, edr_exp, &sub_error);
        g_free(edr_exp);
        if (res != TRUE && sub_error != NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_SETTINGS,
                "Failed to set EDR exposure to %d:\n\t> %s\n", ext_settings->edr_exp, sub_error->message);
            g_propagate_error(error_loc, phantom_error);
            g_clear_error(&sub_error);
            return FALSE;
        }
    }
    if (ext_settings->shutter_off != self->settings.shutter_off || first_call) {
        self->settings.shutter_off = ext_settings->shutter_off;
        gchar* shutter_off = g_strdup_printf("%d", ext_settings->shutter_off);
        res = uca_phantom_communicate_set_variable(self, UNIT_DEFC_SHOFF, shutter_off, &sub_error);
        g_free(shutter_off);
        if (res != TRUE && sub_error != NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_SETTINGS,
                "Failed to set shutter off to %d:\n\t> %s\n", ext_settings->shutter_off, sub_error->message);
            g_propagate_error(error_loc, phantom_error);
            g_clear_error(&sub_error);
            return FALSE;
        }
    }
    if (ext_settings->aexpmode != self->settings.aexpmode || first_call) {
        self->settings.aexpmode = ext_settings->aexpmode;
        gchar* aexpmode = g_strdup_printf("%d", ext_settings->aexpmode);
        res = uca_phantom_communicate_set_variable(self, UNIT_DEFC_AEXPMODE, aexpmode, &sub_error);
        g_free(aexpmode);
        if (res != TRUE && sub_error != NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_SETTINGS,
                "Failed to set auto exposure mode to %d:\n\t> %s\n", ext_settings->aexpmode,
                sub_error->message);
            g_propagate_error(error_loc, phantom_error);
            g_clear_error(&sub_error);
            return FALSE;
        }
    }
    if (ext_settings->aexpcomp != self->settings.aexpcomp || first_call) {
        self->settings.aexpcomp = ext_settings->aexpcomp;
        gchar* aexpcomp = g_strdup_printf("%f", ext_settings->aexpcomp);
        res = uca_phantom_communicate_set_variable(self, UNIT_DEFC_AEXPCOMP, aexpcomp, &sub_error);
        g_free(aexpcomp);
        if (res != TRUE && sub_error != NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_SETTINGS,
                "Failed to set auto exposure compensation to %f:\n\t> %s\n", ext_settings->aexpcomp,
                sub_error->message);
            g_propagate_error(error_loc, phantom_error);
            g_clear_error(&sub_error);
            return FALSE;
        }
    }
    if (ext_settings->nb_post_trigger_frames != self->settings.nb_post_trigger_frames || first_call) {
        self->settings.nb_post_trigger_frames = ext_settings->nb_post_trigger_frames;
        gchar* nb_post_trigger_frames = g_strdup_printf("%d", ext_settings->nb_post_trigger_frames);
        res = uca_phantom_communicate_set_variable(self, UNIT_DEFC_PTFRAMES, nb_post_trigger_frames, &sub_error);
        g_free(nb_post_trigger_frames);
        if (res != TRUE && sub_error != NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_SETTINGS,
                "Failed to set number of post trigger frames to %d:\n\t> %s\n",
                ext_settings->nb_post_trigger_frames, sub_error->message);
            g_propagate_error(error_loc, phantom_error);
            g_clear_error(&sub_error);
            return FALSE;
        }
    }

    g_debug("Settings updated\n");
    first_call = FALSE;

    return TRUE;
}

gboolean uca_phantom_communicate_connect_datastream(UcaPhantomCommunicate* self, GError** error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);

    g_warning("uca_phantom_communicate_connect_datastream: connecting!\n");

    GError* sub_error = NULL;
    GError* phantom_error = NULL;

    if (!g_socket_listener_add_inet_port(G_SOCKET_LISTENER(self->service), self->data_port, NULL, &sub_error)) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_DATASTREAM,
            "Failed to listen on port %d:\n\t> %s\n", self->data_port, sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_clear_error(&sub_error);
        return FALSE;
    }

    // Send the request to connect to the datastream
    gchar* arg = g_strdup_printf("{port:%d}", self->data_port); // 7117
    gboolean res = uca_phantom_communicate_run_command(self, CMD_START_DATA_CONNECTION, arg, NULL, &sub_error);

    if (res != TRUE && sub_error != NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_DATASTREAM,
            "Failed to connect to datastream on port %d:\n\t> %s\n", self->data_port, sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_clear_error(&sub_error);
        return FALSE;
    }

    self->data_connection = g_socket_listener_accept(G_SOCKET_LISTENER(self->service), NULL, NULL, &sub_error);
    if (sub_error != NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_DATASTREAM,
            "Failed to connect to datastream on port %d:\n\t> %s\n", self->data_port, sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_clear_error(&sub_error);
        return FALSE;
    }

    // Set the input and output streams
    self->input_datastream = g_io_stream_get_input_stream(G_IO_STREAM(self->data_connection));
    self->output_datastream = g_io_stream_get_output_stream(G_IO_STREAM(self->data_connection));

    // Get the remote address of the connection
    GSocketAddress* remote_address = g_socket_connection_get_remote_address(self->data_connection, NULL);
    GSocketAddress* local_address = g_socket_connection_get_local_address(self->data_connection, NULL);

    guint remote_port = g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(remote_address));
    guint local_port = g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(local_address));

    GInetAddress* remote_inet_address = g_inet_socket_address_get_address(G_INET_SOCKET_ADDRESS(remote_address));
    GInetAddress* local_inet_address = g_inet_socket_address_get_address(G_INET_SOCKET_ADDRESS(local_address));

    gchar* remote_ip_address = g_inet_address_to_string(remote_inet_address);
    gchar* local_ip_address = g_inet_address_to_string(local_inet_address);

    g_message("Data connection established with phantom (%s:%d) -> (%s:%d)\n", local_ip_address, local_port,
        remote_ip_address, remote_port);

    self->data_connection_state = CONNECTED;

    g_object_unref(remote_address);
    g_object_unref(local_address);
    g_free(remote_ip_address);
    g_free(local_ip_address);

    return TRUE;
}

gboolean uca_phantom_communicate_connect_xdatastream(UcaPhantomCommunicate* self, GError** error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);

    GError* phantom_error = NULL;

    char errbuf[PCAP_ERRBUF_SIZE];
    struct bpf_program fp;

    // Use libpcap to capture ethernet frames from the NIC called "self->xnetcard"

    // Open the device for live capture
    self->handle = pcap_create(self->xnetcard, errbuf);
    if (self->handle == NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_XDATASTREAM,
            "Failed to create pcap handle:\n\t> %s\n", errbuf);
        g_propagate_error(error_loc, phantom_error);
        return FALSE;
    }
    g_debug("Opening device %s for packet capture\n", self->xnetcard);

    // Set the capture options
    pcap_set_snaplen(self->handle, 65535);
    pcap_set_promisc(self->handle, FALSE);
    pcap_set_timeout(self->handle, 5000);
    pcap_set_rfmon(self->handle, FALSE);

    pcap_set_buffer_size(self->handle, MAX_KERNEL_BUF_SIZE);
    pcap_set_immediate_mode(self->handle,
        FALSE); // Set the capture mechanism to PACKET_MMAP

    // Activate the capture self->handle
    if (pcap_activate(self->handle) == -1) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_XDATASTREAM,
            "Failed to activate pcap handle:\n\t> %s\n", pcap_geterr(self->handle));
        g_propagate_error(error_loc, phantom_error);

        pcap_close(self->handle);
        self->handle = NULL;
        return FALSE;
    }
    // Compile the filter to capture packets with ethertype 0x88b7
    if (pcap_compile(self->handle, &fp, "ether proto 0x88b7", 1, PCAP_NETMASK_UNKNOWN) == -1) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_XDATASTREAM,
            "Error compiling filter:\n\t> %s\n", pcap_geterr(self->handle));
        g_propagate_error(error_loc, phantom_error);

        pcap_close(self->handle);
        self->handle = NULL;
        return FALSE;
    }
    if (pcap_setfilter(self->handle, &fp) == -1) {
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

gboolean uca_phantom_communicate_disconnect_datastream(UcaPhantomCommunicate* self, GError** error_loc)
{
    // First check if started readout
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(G_IS_INPUT_STREAM(self->input_datastream), FALSE);
    g_return_val_if_fail(G_IS_SOCKET_CONNECTION(self->data_connection), FALSE);

    GError* sub_error = NULL;
    GError* phantom_error = NULL;

    // Data stream is ended when the socket is closed
    g_input_stream_close(self->input_datastream, NULL, &sub_error);
    if (sub_error != NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_DISCONNECT_DATASTREAM,
            "Failed to close input stream:\n\t> %s\n", sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_clear_error(&sub_error);
        return FALSE;
    }

    self->data_connection_state = DISCONNECTED;

    return TRUE;
}

gboolean uca_phantom_communicate_disconnect_xdatastream(UcaPhantomCommunicate* self, GError** error_loc)
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

static gboolean uca_phantom_communicate_get_mac_address(UcaPhantomCommunicate* self, GError** error_loc)
{
    g_return_val_if_fail(UCA_IS_PHANTOM_COMMUNICATE(self), FALSE);
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);

    GError* phantom_error = NULL;

// Get the MAC address on Linux platform
#ifdef __linux__
    struct ifreq ifr = {
        0,
    };

    // Open a socket for the ioctl call
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_MAC_ADDRESS,
            "Failed to open socket");
        g_propagate_error(error_loc, phantom_error);
        return FALSE;
    }

    // Set the interface name using g_strlcpy
    g_strlcpy(ifr.ifr_name, self->xnetcard, IFNAMSIZ);

    // Get the MAC address
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
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

gpointer uca_phantom_communicate_request_buffered_images (gpointer data) {
    UcaPhantomCommunicate *self = UCA_PHANTOM_COMMUNICATE(data);
    GError *error = NULL;
    GError *sub_error = NULL;

    gchar *request_format = NULL, *additional = NULL;

    gint cine = -1, start = 0; // start frame is ignored for cine -1
    guint img_format = self->settings.image_format, count = MAX_NB_IMAGES_BUFFERING;

    // Make sure we have the mac address, should already be done though
    if (self->xenabled && self->mac_address_str == NULL) {
        gboolean res = uca_phantom_communicate_get_mac_address(self, &sub_error);
        if (res != TRUE && sub_error != NULL) {
            return sub_error;
        }

        self->mac_address_str = g_strdup_printf("%02x%02x%02x%02x%02x%02x", self->mac_address[0], self->mac_address[1],
            self->mac_address[2], self->mac_address[3], self->mac_address[4], self->mac_address[5]);

        if (self->mac_address_str == NULL) {
            g_set_error(&error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
                "Failed to allocate memory for MAC address");
            return error;
        }
    }

    guint command_flag = 0;

    if (self->xenabled) {

        additional = g_strdup_printf(", dest:%s, from:%d", self->mac_address_str, 0);
        request_format = g_strdup_printf (
            self->request_image_string, cine, start, count, ImageFormatSpecs[img_format].format_string, additional);

        if (request_format == NULL) {
            g_set_error(&error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
                "Failed to allocate memory for get_ximages command");
            g_free(additional);
            return error;
        }

        command_flag = CMD_GET_XIMAGES;
    }
    else {
        // Setup the arguments for image transfer on 1Gb ethernet
        additional = g_strdup_printf("\n");
        request_format = g_strdup_printf(self->request_image_string, cine, start, count,
            ImageFormatSpecs[img_format].format_string, additional);
        if (request_format == NULL) {
            g_set_error(&error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
                "Failed to allocate memory for get_ximages command");
            g_free(additional);
            return error;
        }
        command_flag = CMD_GET_IMAGES;
    }

    guint current_buffer_index = 0;

    do {
        // Request the datatransfer
        gboolean res = uca_phantom_communicate_run_command(self, command_flag, request_format, NULL, &sub_error);
        g_free(request_format);
        g_free(additional);

        if (res != TRUE && sub_error != NULL) {
            g_free(additional);
            return sub_error;
        }

        // Create internal request
        ImageRequest* request = g_new0(ImageRequest, 1);
        request->start = start;
        request->nb_images = count;
        request->img_format = img_format;
        request->buffer_index = current_buffer_index++;
        request->end_request = self->buffer_state == ACQUIRING ? FALSE : TRUE;
        
        // Push request to request queue
        g_async_queue_push(self->request_queue, request);

        
    } while (self->buffer_state == IDLE || self->buffer_state != ACQUIRING);

    return NULL;
}

gboolean uca_phantom_communicate_arm(UcaPhantomCommunicate* self, guint cine, GError** error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);

    GError* sub_error = NULL;
    GError* phantom_error = NULL;
    gchar* cine_str;
    gboolean res;
    PhantomReply reply;

    // Set the capture settings
    cine_str = g_strdup_printf("%d", cine);
    res = uca_phantom_communicate_run_command(self, CMD_START_RECORDING_IN_A_CINE, cine_str, &reply, &sub_error);
    g_free(cine_str);
    if (res != TRUE && sub_error != NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING,
            "Failed to arm:\n\t> %s\n", sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_clear_error(&sub_error);
        g_free(reply.raw);
        return FALSE;
    }

    self->phantom_acquisition_state = ACQUIRING;

    if (self->buffering)
        self->buffer_request_thread = g_thread_new("buffer_request_thread", (GThreadFunc)uca_phantom_communicate_request_buffered_images, self);
    g_free(reply.raw);

    return TRUE;
}

gboolean uca_phantom_communicate_disarm(UcaPhantomCommunicate* self, GError** error_loc) {
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);
    
    if (self->buffering) {
        self->phantom_acquisition_state = IDLE;
        g_thread_join(self->buffer_request_thread);
    }

    return TRUE;
}

gboolean uca_phantom_communicate_trigger(UcaPhantomCommunicate* self, GError** error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);

    GError* sub_error = NULL;
    GError* phantom_error = NULL;

    gboolean res;

    res = uca_phantom_communicate_run_command(self, CMD_SOFTWARE_TRIGGER, NULL, NULL, &sub_error);
    if (res != TRUE && sub_error != NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING,
            "Failed to trigger:\n\t> %s\n", sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_clear_error(&sub_error);
        return FALSE;
    }

    return TRUE;
}

// static gboolean uca_phantom_communicate_notify (UcaPhantomCommunicate *self,
// GError **error_loc) {
//     g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
//     g_return_val_if_fail (self->control_connection_state == CONNECTED,
//     FALSE);

//     GError *sub_error = NULL;
//     GError *phantom_error = NULL;
//     gboolean result = FALSE;

//     PhantomReply reply;

//     // Send the notify command
//     result = uca_phantom_communicate_run_command (self,
//     CMD_ENABLE_STATUS_CHANGE_NOTIFICATIONS, &reply, &sub_error, "0", NULL);
//     if (!result) {
//         g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR,
//         UCA_PHANTOM_COMMUNICATE_ERROR_NOTIFY, "Failed to send notify
//         command:\n\t> %s\n", sub_error->message); g_propagate_error
//         (error_loc, phantom_error); g_clear_error (&sub_error);
//         g_free(reply.raw);

//         return FALSE;
//     }

//     g_print("uca_phantom_communicate_notify: %s", reply.raw);

//     g_free(reply.raw);

//     return TRUE;
// }



gboolean uca_phantom_communicate_request_images(UcaPhantomCommunicate* self, CaptureSettings* settings,
    GError** error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);
    g_return_val_if_fail(self->local_acquisition_state == ACQUIRING, FALSE);

    GError* phantom_error = NULL;
    GError* sub_error = NULL;
    gchar* request_format = NULL;
    GValue val = G_VALUE_INIT;
    GValue val2 = G_VALUE_INIT;
    gboolean result = FALSE;
    gint cine_start_index = 0;
    gint cine_nb_recorded_images = 0;
    gint cine = settings->current_cine;
    gint nb_images = settings->nb_post_trigger_frames + settings->nb_pre_trigger_frames;
    guint img_format = settings->image_format;

    

    if (cine >= 0) {
        // Get first image index from phantom and nb images
        result = uca_phantom_communicate_get_variable(self, UNIT_CT_FIRSTFR, &val, &phantom_error);
        if (result != TRUE && phantom_error != NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
                "Failed to get first image index:\n\t> %s\n", phantom_error->message);
            g_propagate_error(error_loc, phantom_error);
            return FALSE;
        }
        result = uca_phantom_communicate_get_variable(self, UNIT_CT_FRCOUNT, &val2, &phantom_error);
        if (result != TRUE && phantom_error != NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
                "Failed to get first image index:\n\t> %s\n", phantom_error->message);
            g_propagate_error(error_loc, phantom_error);
            return FALSE;
        }

        cine_start_index = g_value_get_int(&val);
        cine_nb_recorded_images = g_value_get_uint(&val2);
        g_value_unset(&val);
        g_value_unset(&val2);
    }

    gint start = 0, total = 0;

    if (cine_start_index < 0) {
        if (settings->nb_pre_trigger_frames < -cine_start_index) {
            start = -settings->nb_pre_trigger_frames;
            g_print("first -> start: %d, pre_trigger_frames: %d\n", start, -settings->nb_pre_trigger_frames);
        } else {
            start = cine_start_index;
            g_print("second -> start: %d, pre_trigger_frames: %d\n", start, -settings->nb_pre_trigger_frames);
        }
    } else {
        start = cine_start_index;
        g_print("third -> start: %d\n", start);
    }

    if (cine == -1) {
        total = 1;
    } else if (nb_images > cine_nb_recorded_images) {
        total = cine_nb_recorded_images;
    } else {
        total = nb_images;
    }

    g_print("cine_start_index: %d\n", cine_start_index);
    g_print("start: %d\n", start);
    g_print("settings->nb_pre_trigger_frames: %d\n", settings->nb_pre_trigger_frames);
    g_print("cine_nb_recorded_images: %d\n", cine_nb_recorded_images);

    // Make sure we have the mac address, should already be done though
    if (self->xenabled && self->mac_address_str == NULL) {
        gboolean res = uca_phantom_communicate_get_mac_address(self, &sub_error);
        if (res != TRUE && sub_error != NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
                "Failed to get MAC address:\n\t> %s\n", sub_error->message);
            g_propagate_error(error_loc, phantom_error);
            g_clear_error(&sub_error);
            return FALSE;
        }

        self->mac_address_str = g_strdup_printf("%02x%02x%02x%02x%02x%02x", self->mac_address[0], self->mac_address[1],
            self->mac_address[2], self->mac_address[3], self->mac_address[4], self->mac_address[5]);

        if (self->mac_address_str == NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
                "Failed to allocate memory for MAC address");
            g_propagate_error(error_loc, phantom_error);
            return FALSE;
        }
    }

    // Ask images by blocks of MaxNumberImagesPerRequest (if nb_images >
    // MaxNumberImagesPerRequest) so as to not overflow the buffer, and give time
    // to libuca to pull the images incrementally Sort of cheated version of a
    // real buffered image request. This method will use a lot of memory if the
    // images are not pulled fast enough by libuca. Will do fine for now.
    // TODO: finish implementing a thread safe ring buffer
    guint current_buffer_index = 0;

    if (self->xenabled) {
        gchar* additional = g_strdup_printf(", dest:%s, from:%d", self->mac_address_str, 0);
        guint nb_sub_images = 0, nb_images_left = total;

        while (nb_images_left > 0) {
            // format: {cine:<cine_number>, start:<first_frame>, cnt:<frame_count>,
            // fmt:<image_format>, dest:<mac_address>, from:<image_source>}
            nb_sub_images = nb_images_left > MaxNumberImagesPerRequest[img_format]
                ? MaxNumberImagesPerRequest[img_format]
                : nb_images_left;
            request_format = g_strdup_printf(self->request_image_string, cine, start, nb_sub_images,
                ImageFormatSpecs[img_format].format_string, additional);
            if (request_format == NULL) {
                g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
                    "Failed to allocate memory for get_ximages command");
                g_propagate_error(error_loc, phantom_error);
                g_free(additional);
                return FALSE;
            }

            // Request the datatransfer
            gboolean res = uca_phantom_communicate_run_command(self, CMD_GET_XIMAGES, request_format, NULL, &sub_error);
            g_free(request_format);
            request_format = NULL;
            if (res != TRUE && sub_error != NULL) {
                g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
                    "Failed to request images:\n\t> %s\n", sub_error->message);
                g_propagate_error(error_loc, phantom_error);
                g_clear_error(&sub_error);
                g_free(additional);
                return FALSE;
            }

            // Create internal request
            ImageRequest* request = g_new0(ImageRequest, 1);
            request->start = start;
            request->nb_images = nb_sub_images;
            request->img_format = img_format;
            request->end_request = FALSE;
            request->buffer_index = current_buffer_index++;

            // Update counters
            start += (nb_sub_images);
            nb_images_left -= (nb_sub_images);

            // Push request to request queue
            g_async_queue_push(self->request_queue, request);
        }
        g_free(additional);
    } else {
        // Setup the arguments for image transfer on 1Gb ethernet
        gchar* additional = g_strdup_printf("\n");
        guint nb_sub_images = 0, nb_images_left = total;

        while (nb_images_left > 0) {
            // format: {cine:<cine_number>, start:<first_frame>, cnt:<frame_count>,
            // fmt:<image_format>, dest:<mac_address>, from:<image_source>}
            nb_sub_images = nb_images_left > MaxNumberImagesPerRequest[img_format]
                ? MaxNumberImagesPerRequest[img_format]
                : nb_images_left;
            request_format = g_strdup_printf(self->request_image_string, cine, start, nb_sub_images,
                ImageFormatSpecs[img_format].format_string, additional);
            if (request_format == NULL) {
                g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
                    "Failed to allocate memory for get_ximages command");
                g_propagate_error(error_loc, phantom_error);
                g_free(additional);
                return FALSE;
            }
            start += nb_sub_images;
            nb_images_left -= nb_sub_images;

            // Request the datatransfer
            gboolean res = uca_phantom_communicate_run_command(self, CMD_GET_IMAGES, request_format, NULL, &sub_error);
            g_free(request_format);
            request_format = NULL;
            if (res != TRUE && sub_error != NULL) {
                g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
                    "Failed to request images:\n\t> %s\n", sub_error->message);
                g_propagate_error(error_loc, phantom_error);
                g_clear_error(&sub_error);
                g_free(additional);
                return FALSE;
            }

            // Create internal request
            ImageRequest* request = g_new0(ImageRequest, 1);
            request->nb_images = nb_sub_images;
            request->img_format = img_format;
            request->end_request = FALSE;

            // Push request to request queue
            g_async_queue_push(self->request_queue, request);
        }
        g_free(additional);
    }

    if (self->timestamping || settings->timestamp_format != TS_NONE) {
        // Request the timestamps:
        // format: time {cine:<cine_number>, start:<first_frame>,
        // cnt:<stamp_count>[, from:<image_source>]}
        gchar* request_format = g_strdup_printf("{cine:%d, start:%d, cnt:%d, from:%d}", cine, start, total, 0);
        if (request_format == NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
                "Failed to allocate memory for time command");
            g_propagate_error(error_loc, phantom_error);
            return FALSE;
        }
        gboolean res = uca_phantom_communicate_run_command(self, CMD_GET_TIMESTAMPS, request_format, NULL, &sub_error);
        if (res != TRUE && sub_error != NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
                "Failed to request timestamps:\n\t> %s\n", sub_error->message);
            g_propagate_error(error_loc, phantom_error);
            g_clear_error(&sub_error);
            return FALSE;
        }
        g_free(request_format);
    }

    return TRUE;
}

static void uca_phantom_communicate_free_request(ImageRequest* request)
{
    // TODO: check if CaptureSettings needs to be freed
    g_free(request);
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
    UcaPhantomCommunicate* self = UCA_PHANTOM_COMMUNICATE(data);

    g_return_val_if_fail(self->control_connection_state == CONNECTED, NULL);

    GError* error = NULL;
    GError* sub_error = NULL;
    gsize image_size = 0;
    gsize image_packet_size = 0;
    gsize bytes_read = 0;
    gpointer image_buffer = NULL;
    gboolean read_all = FALSE;
    guint nb_pixels = 0;

    self->input_datastream = g_io_stream_get_input_stream(G_IO_STREAM(self->data_connection));

    ImageRequest* request = NULL;

    while (TRUE) {
        bytes_read = 0;
        // Wait for a request to be available
        request = g_async_queue_pop(self->request_queue);

        if (request == NULL) {
            g_print("Failed to pop request from queue\n");
            g_error_new(UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_ACCEPT_IMG,
                "Failed to pop request from queue: request is NULL\n");
            return error;
        }
        if (request->end_request == TRUE) {
            uca_phantom_communicate_free_request(request);
            break;
        }

        // Calculate the size of the image buffer
        nb_pixels = self->settings.roi_pixel_width * self->settings.roi_pixel_height;
        image_size = nb_pixels * ImageFormatSpecs[request->img_format].byte_depth;
        image_packet_size = image_size * request->nb_images;

        // print number of images and image size
        g_print("Number of images requested: %ld\n", request->nb_images);

        g_print("Image size: %ld bytes, Image packet size: %ld bytes\n", image_size, image_packet_size);

        // Allocate the image buffer
        image_buffer = g_malloc0(image_packet_size);
        if (image_buffer == NULL) {
            error = g_error_new_literal(UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_ACCEPT_IMG,
                "Failed to allocate image buffer\n");
            return error;
        }

        // Read the image data from the data stream
        read_all = g_input_stream_read_all(self->input_datastream, image_buffer, image_packet_size, &bytes_read, NULL,
            &sub_error);
        if (read_all != TRUE && sub_error != NULL) {
            error = g_error_new(UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_ACCEPT_IMG,
                "Failed to read datastream:\n\t> %s\n", sub_error->message);
            g_clear_error(&sub_error);
            g_free(image_buffer);
            return error;
        } else if (bytes_read != image_packet_size) {
            g_print("Failed to read all bytes of image frame from datastream. Read %ld "
                    "bytes, expected %ld bytes. Buffer will be padded.\n",
                bytes_read, image_packet_size);
        }

        g_print("Read %ld bytes from datastream\n", bytes_read);

        // Append to the self->unpacked_images array
        g_ptr_array_add(self->unpacked_images, image_buffer);

        // Append a copy of the current capture settings
        CaptureSettings* csettings = g_memdup(&self->settings, sizeof(CaptureSettings));
        g_ptr_array_add(self->capture_settings, csettings);

        gpointer output_buffer = image_buffer;

        // Create a CineData struct for each image in the image buffer
        for (guint i = 0; i < request->nb_images; i++) {
            // Create a new CineData struct
            CineData* cine_data = g_new0(CineData, 1);
            cine_data->Settings = csettings;

            cine_data->NbImages = 1;
            cine_data->NbPixelsPerImage = nb_pixels;
            cine_data->SizePerImageRaw = image_size;
            cine_data->SizePerImageUnpacked = image_size;

            cine_data->RawImages = NULL;
            cine_data->UnpackedImages = output_buffer;

            // Increment the output buffer pointer
            output_buffer += nb_pixels;

            // Push it to the unpacked queue
            g_async_queue_push(self->unpacked_queue, cine_data);
        }

        // Free the request
        uca_phantom_communicate_free_request(request);
    }

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
    UcaPhantomCommunicate* self = UCA_PHANTOM_COMMUNICATE(data);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, NULL);

    guint nb_pixels = 0;
    gsize packed_image_size = 0, packed_packet_size = 0;
    gsize unpacked_image_size = 0, unpacked_packet_size = 0;
    gsize buffer_size = 0, to_read = 0;
    gsize remaining_bytes = 0;

    // gssize ts_size = 0;
    // gssize ts_packet_size = 0;
    gsize bytes_read = 0;
    guint8* image_buffer = NULL;
    guint8* buffer_pointer = NULL;
    const guint8* pkt_data = NULL;

    // gpointer ts_buffer = NULL;
    int read_all = FALSE;
    ImageRequest* request = NULL;

    struct pcap_pkthdr* pkt_header = NULL;

    // use g_socket_receive_from in a loop to receive the data from the socket
    while (TRUE) {
        bytes_read = 0;
        // Wait for a request to be available
        request = g_async_queue_pop(self->request_queue);

        if (request == NULL) {
            return NULL;
        }
        if (request->end_request == TRUE) {
            uca_phantom_communicate_free_request(request);

            // Push the end request to the packed queue
            CineData* cine_data = g_new0(CineData, 1);
            cine_data->RawImages = NULL;
            cine_data->UnpackedImages = NULL;

            // Add the data to the queue
            g_async_queue_push(self->packed_queue, cine_data);

            if (self->timestamping) {
                // Create timestamp request
                TsRequest* ts_request = g_new0(TsRequest, 1);
                ts_request->end_request = TRUE;

                // Push request to request queue
                g_async_queue_push(self->ts_request_queue, ts_request);
            }

            return NULL;
        }
        // Calculate the size of the image buffer
        nb_pixels = self->settings.roi_pixel_width * self->settings.roi_pixel_height;
        packed_image_size = nb_pixels * ImageFormatSpecs[request->img_format].byte_depth; // the packed image size
        packed_packet_size = packed_image_size * request->nb_images; // the packed image packet size
        unpacked_image_size = nb_pixels * sizeof(guint16); // the unpacked image size
        unpacked_packet_size = unpacked_image_size * request->nb_images; // the unpacked image packet size

        buffer_size = packed_packet_size;
        image_buffer = g_malloc0(buffer_size + 128);

        if (image_buffer == NULL) {
            g_warning("Out of RAM... \n");
            return data;
        }

        remaining_bytes = buffer_size;
        buffer_pointer = image_buffer;

        // Read the image data directly from kernel buffer using pcap_next_ex
        while (TRUE) {
            if (remaining_bytes <= 0) {
                break; // All bytes of the image frame have been read
            }

            read_all = pcap_next_ex(self->handle, &pkt_header, &pkt_data);

            if (read_all == 0) {
                g_debug("Beeing read from live capture\n");
            } else if (read_all == -1) {
                g_debug("Error occurred\n");
                gchar* error = pcap_geterr(self->handle);

                g_debug("Error: %s\n", error);
            } else if (read_all == -2) {
                g_debug("Being read from savefile\n");
            }

            // Check if the packet size exceeds the remaining space in the buffer
            if (pkt_header->len - ETHERNET_HEADER_SIZE > remaining_bytes) {
                to_read = remaining_bytes;
            } else {
                to_read = pkt_header->len - ETHERNET_HEADER_SIZE;
            }

            // Copy the data to the image buffer
            memcpy(buffer_pointer, pkt_data + ETHERNET_HEADER_SIZE, to_read);

            buffer_pointer += to_read;
            remaining_bytes -= to_read;
        }

        bytes_read = packed_packet_size - remaining_bytes;

        if (bytes_read != packed_packet_size) {
            g_debug("Failed to read all bytes of image frame from datastream. Read %ld "
                    "bytes, expected %ld bytes. Buffer will be padded.\n",
                bytes_read, packed_packet_size);
        }

        // Create a new CineData struct
        CineData* cine_data = g_new0(CineData, 1);
        cine_data->Settings = g_memdup(&self->settings, sizeof(CaptureSettings));
        cine_data->ImgFormat = request->img_format;

        cine_data->NbImages = request->nb_images;
        cine_data->NbPixelsPerImage = nb_pixels;
        cine_data->SizePerImageRaw = packed_image_size;
        cine_data->SizePerImageUnpacked = unpacked_image_size;

        cine_data->RawImages = image_buffer;
        cine_data->UnpackedImages = NULL;

        cine_data->buffer_index = request->buffer_index;

        // Add the data to the queue
        g_async_queue_push(self->packed_queue, cine_data);

        if (self->timestamping) {
            // Create timestamp request
            TsRequest* ts_request = g_new0(TsRequest, 1);
            ts_request->start = request->start;
            ts_request->count = request->nb_images;
            ts_request->end_request = FALSE;
            ts_request->TsFormat = self->settings.timestamp_format;

            // Push request to request queue
            g_async_queue_push(self->ts_request_queue, ts_request);
        }

        // Free the request
        uca_phantom_communicate_free_request(request);
    }

    return NULL;
}

/**
 * @brief Unpacks the image data from the packed format to the unpacked format
 *
 * @param cine_data
 * @param error_loc
 * @return gboolean
 */
static gboolean uca_phantom_communicate_unpack_image_p10(UcaPhantomCommunicate* self, CineData* cine_data,
    GError** error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(cine_data != NULL, FALSE);

    GError* phantom_error = NULL;
    gsize output_size = cine_data->SizePerImageUnpacked * cine_data->NbImages;

    // Allocate memory for the unpacked image
    guint16* unpacked_image = g_malloc0(output_size);
    if (unpacked_image == NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE,
            "Failed to allocate memory for unpacked image");
        g_propagate_error(error_loc, phantom_error);
        return FALSE;
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

    if (cine_data->NbPixelsPerImage % 8 != 0) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE,
            "Image size is not a multiple of 8");
        g_propagate_error(error_loc, phantom_error);
        return FALSE;
    }

    __m128i input, shifted0, shifted1, shifted2, shifted3, result;

    if (cine_data->RawImages == NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE,
            "Raw image data is NULL");
        g_propagate_error(error_loc, phantom_error);
        return FALSE;
    }

    gsize packet_size = cine_data->SizePerImageRaw * cine_data->NbImages;

    int new_length = 0;
    int usable_length = 0;
    int limit = 0;
    int i = 0;

    while (output_index < cine_data->NbPixelsPerImage * cine_data->NbImages) {
        new_length = packet_size - input_index;
        usable_length = new_length - (new_length % 10);
        limit = input_index + usable_length;

        for (i = input_index; i < limit; i += 10) {
            input = _mm_loadu_si128((__m128i*)(cine_data->RawImages + input_index));

            input = _mm_and_si128(input, mask2);

            shifted0 = _mm_and_si128(_mm_shuffle_epi8(input, sm0), m0) >> 6;
            shifted1 = _mm_and_si128(_mm_shuffle_epi8(input, sm1), m1) >> 4;
            shifted2 = _mm_and_si128(_mm_shuffle_epi8(input, sm2), m2) >> 2;
            shifted3 = _mm_and_si128(_mm_shuffle_epi8(input, sm3), m3);

            result = _mm_or_si128(_mm_or_si128(shifted0, shifted1), _mm_or_si128(shifted2, shifted3));

            _mm_storeu_si128((__m128i*)(unpacked_image + output_index), result);

            input_index += 10;
            output_index += 8;
        }
    }

    // if (output_index != cine_data->NbPixelsPerImage) {
    //     g_warning("Pixel index is not equal to the number of pixels");
    // }

    cine_data->UnpackedImages = unpacked_image;
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
static gboolean uca_phantom_communicate_unpack_image_p12l(UcaPhantomCommunicate* self, CineData* cine_data,
    GError** error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(cine_data != NULL, FALSE);

    GError* phantom_error = NULL;

    gsize output_size = cine_data->SizePerImageUnpacked * cine_data->NbImages;

    // Allocate memory for the unpacked image
    guint16* unpacked_image = g_malloc0(output_size);
    if (unpacked_image == NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE,
            "Failed to allocate memory for unpacked image");
        g_propagate_error(error_loc, phantom_error);
        return FALSE;
    }

    __m128i sm0 = _mm_setr_epi8(1, 0, 0x80, 0x80, 4, 3, 0x80, 0x80, 7, 6, 0x80, 0x80, 10, 9, 0x80, 0x80);
    __m128i sm1 = _mm_setr_epi8(0x80, 0x80, 2, 1, 0x80, 0x80, 5, 4, 0x80, 0x80, 8, 7, 0x80, 0x80, 11, 10);
    __m128i m0 = _mm_setr_epi8(0b11110000, 0b11111111, 0, 0, 0b11110000, 0b11111111, 0, 0, 0b11110000, 0b11111111, 0, 0,
        0b11110000, 0b11111111, 0, 0);
    __m128i m1 = _mm_setr_epi8(0, 0, 0b11111111, 0b00001111, 0, 0, 0b11111111, 0b00001111, 0, 0, 0b11111111, 0b00001111,
        0, 0, 0b11111111, 0b00001111);
    __m128i mask2 = _mm_setr_epi8(0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111,
        0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0, 0, 0, 0);

    guint input_index = 0;
    guint output_index = 0;

    if (cine_data->NbPixelsPerImage % 8 != 0) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE,
            "Image size is not a multiple of 8");
        g_propagate_error(error_loc, phantom_error);
        return FALSE;
    }

    __m128i input, shifted0, shifted1, result;

    while (output_index < cine_data->NbPixelsPerImage * cine_data->NbImages) {
        // Load 8 pixels, i.e. 80 bits = 10 bytes
        input = _mm_loadu_si128((__m128i*)(cine_data->RawImages + input_index));

        // Mask
        input = _mm_and_si128(input, mask2);

        // Shift
        shifted0 = _mm_and_si128(_mm_shuffle_epi8(input, sm0), m0) >> 4;
        shifted1 = _mm_and_si128(_mm_shuffle_epi8(input, sm1), m1);

        // Result
        result = _mm_or_si128(shifted0, shifted1);

        // Store
        _mm_storeu_si128((__m128i*)(unpacked_image + output_index), result);

        output_index += 8;
        input_index += 12;
    }

    if (output_index != cine_data->NbPixelsPerImage * cine_data->NbImages) {
        g_warning("Error while unpacking image");
    }

    cine_data->UnpackedImages = unpacked_image;
    g_free(cine_data->RawImages);

    return TRUE;
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
    UcaPhantomCommunicate* self = UCA_PHANTOM_COMMUNICATE(data);

    GError* sub_error = NULL;
    GError* phantom_error = NULL;

    // Loop on CineData objects in the queue
    while (TRUE) {
        // Get the next CineData object
        CineData* cine_data = g_async_queue_pop(self->packed_queue);

        if (cine_data->UnpackedImages == NULL && cine_data->RawImages == NULL) {
            // Push the CineData object to the queue anyways, to exit the other
            // threads g_async_queue_push (self->unpacked_queue, cine_data);
            g_free(cine_data);
            break;
        }

        // Unpack the image
        if (cine_data->ImgFormat == IMG_P10) {
            if (!uca_phantom_communicate_unpack_image_p10(self, cine_data, &sub_error)) {
                g_propagate_error(&phantom_error, sub_error);
                g_clear_error(&sub_error);
                return phantom_error;
            }
        } else if (cine_data->ImgFormat == IMG_P12L) {
            if (!uca_phantom_communicate_unpack_image_p12l(self, cine_data, &sub_error)) {
                g_propagate_error(&phantom_error, sub_error);
                g_clear_error(&sub_error);
                return phantom_error;
            }
        } else {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE,
                "Image format not supported over 10Gb Ethernet.");
            return phantom_error;
        }

        // Append to the self->unpacked_images array
        g_ptr_array_add(self->unpacked_images, cine_data->UnpackedImages);

        // Append a copy of the current capture settings
        g_ptr_array_add(self->capture_settings, cine_data->Settings);

        // Create a CineData struct for each image in the image buffer
        // CineData *new_cine_data = g_new0 (CineData, cine_data->nb_images);
        gpointer image_buffer_offset = cine_data->UnpackedImages;

        for (guint i = 0; i < cine_data->NbImages; i++) {
            // Get a pointer to the current CineData struct in the array
            CineData* new_cine_data = g_new0(CineData, 1);
            new_cine_data->Settings = cine_data->Settings;

            new_cine_data->NbImages = 1;
            new_cine_data->NbPixelsPerImage = cine_data->NbPixelsPerImage;
            new_cine_data->SizePerImageRaw = cine_data->SizePerImageRaw;
            new_cine_data->SizePerImageUnpacked = cine_data->SizePerImageUnpacked;

            new_cine_data->RawImages = NULL;
            new_cine_data->UnpackedImages = image_buffer_offset;

            new_cine_data->buffer_index = cine_data->buffer_index;

            // Update the offsets
            image_buffer_offset += cine_data->SizePerImageUnpacked;

            // Push it to the unpacked queue
            g_async_queue_push(self->unpacked_queue, new_cine_data);
        }

        // TODO: is this necessary? don't think so
        self->buffer_index_counter++;

        // Free the cine data
        g_free(cine_data);
    }

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
    UcaPhantomCommunicate* self = UCA_PHANTOM_COMMUNICATE(data);

    GError* sub_error = NULL;
    GError* phantom_error = NULL;
    gssize bytes_read = 0;

    // Loop on CineData objects in the queue
    while (TRUE) {
        // Get the next CineData object
        TsRequest* ts_request = g_async_queue_pop(self->ts_request_queue);

        if (ts_request->end_request == TRUE) {
            // exit the thread
            g_free(ts_request);
            break;
        }

        gsize packet_size = ts_request->count * TimestampSpecs[ts_request->TsFormat].byte_size;

        g_print("count: %d, size: %ld\n", ts_request->count, TimestampSpecs[ts_request->TsFormat].byte_size);

        g_print("Requesting %ld packet_size\n", packet_size);

        // Allocate memory for the timestamp data
        guint8* ts_data = g_malloc0(packet_size);

        // Receive the timestamp data from the self->input_datastream
        gsize bytes_received = 0;
        while (bytes_received < packet_size) {
            bytes_read = g_input_stream_read(self->input_datastream, ts_data + bytes_received,
                packet_size - bytes_received, NULL, &sub_error);
            if (bytes_read < 0) {
                g_propagate_error(&phantom_error, sub_error);
                g_clear_error(&sub_error);
                g_free(ts_data);
                g_free(ts_request);
                return phantom_error;
            }
            bytes_received += bytes_read;
        }

        Timestamp* timestamps = g_new0(Timestamp, ts_request->count);
        memcpy(timestamps, ts_data, packet_size);

        // Free the timestamp data
        g_free(ts_data);

        // Create a TimestampData struct
        TimestampData* timestamp_data = g_new0(TimestampData, 1);
        timestamp_data->start = ts_request->start;
        timestamp_data->count = ts_request->count;
        timestamp_data->TsFormat = ts_request->TsFormat;
        timestamp_data->timestamps = timestamps;

        // Print the first timestamp
        g_print("Timestamp: %u\n", (*timestamps).ts0.csecs);

        // Push it to the unpacked queue
        g_async_queue_push(self->ts_queue, timestamp_data);

        // Free the request
        g_free(ts_request);
    }

    return NULL;
}

gpointer uca_phantom_communicate_buffer_images (gpointer data) {
    UcaPhantomCommunicate *self = UCA_PHANTOM_COMMUNICATE (data);
    static guint prev_buffer_index = 0;

    gpointer result = NULL;
    CineData *cine_data;

    g_print ("Buffering thread started\n");

    // Loop on CineData objects in the queue
    // TODO: Remake so that whe dont loop over the queue, but over the buffer index
    // Then we can just copy eveything at once... 
    while (TRUE) {
        // Get the next CineData object
        cine_data = g_async_queue_pop (self->unpacked_queue);

        if (cine_data->UnpackedImages == NULL && cine_data->RawImages == NULL) {
            g_print ("Buffering thread: exiting because cine_data NULL\n");
            // // Push the CineData object to the queue anyways, to exit the other threads
            // g_async_queue_push (self->unpacked_queue, cine_data);
            g_free (cine_data);
            break;
        }

        // Write the image data to the ring buffer
        result = ringbuf_memcpy_into (self->ring_buffer, cine_data->UnpackedImages, cine_data->SizePerImageUnpacked);

        // Free the actual image data
        // If we arrive in a new buffer, free the previous one
        if (cine_data->buffer_index != prev_buffer_index) {
            g_debug("Freeing buffer %d\n", prev_buffer_index);
            g_free(g_ptr_array_remove_index(self->unpacked_images, prev_buffer_index));
            prev_buffer_index = cine_data->buffer_index;
        }

        // Free the cine data
        g_free (cine_data);

        if (result == NULL) {
            g_print ("Error in the ring buffer...\n");
            continue;
        }        
    }

    g_print ("Buffering thread finished\n");

    return NULL;
}

gboolean uca_phantom_communicate_start_readout(UcaPhantomCommunicate* self, GError** error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(self->local_acquisition_state == IDLE, FALSE);

    GError* sub_error = NULL;
    GError* phantom_error = NULL;
    gboolean res = FALSE;

    if (self->xenabled) {
        if (self->mac_address_str == NULL) {
            // Get the mac address of the camera
            res = uca_phantom_communicate_get_mac_address(self, &sub_error);
            if (res != TRUE && sub_error != NULL) {
                g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR,
                    UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to get MAC address:\n\t> %s\n",
                    sub_error->message);
                g_propagate_error(error_loc, phantom_error);
                g_clear_error(&sub_error);
                return FALSE;
            }

            self->mac_address_str = g_strdup_printf("%02x%02x%02x%02x%02x%02x", self->mac_address[0], self->mac_address[1],
                self->mac_address[2], self->mac_address[3], self->mac_address[4], self->mac_address[5]);
            if (self->mac_address_str == NULL) {
                g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR,
                    UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to allocate memory for MAC address");
                g_propagate_error(error_loc, phantom_error);
                return FALSE;
            }
        }

        self->data_receiver = g_thread_new("data_receiver", uca_phantom_communicate_accept_ximg, self);
        self->data_unpacker = g_thread_new("data_unpacker", uca_phantom_communicate_unpack_ximg, self);
    } else {
        // Start new thread to read data
        self->data_receiver = g_thread_new("data_receiver", uca_phantom_communicate_accept_img, self);
    }

    if (self->timestamping) {
        // Start new thread to read timestamps
        g_print("Starting timestamp thread\n");
        self->ts_receiver = g_thread_new("ts_receiver", uca_phantom_communicate_accept_timestamps, self);
    }

    if (self->buffering) {
        // Start new thread to buffer images
        g_print("Starting buffering thread\n");
        self->buffering_thread = g_thread_new("buffering_thread", uca_phantom_communicate_buffer_images, self);
    }

    self->local_acquisition_state = ACQUIRING;

    return TRUE;
}

gboolean uca_phantom_communicate_grab_image(UcaPhantomCommunicate* self, gpointer data, GError** error_loc)
{
    static CineData* cine_data = NULL;
    // static GError *sub_error = NULL;
    static GError* phantom_error = NULL;
    static guint prev_buffer_index = 0;

    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);

    cine_data = g_async_queue_pop(self->unpacked_queue);

    if (cine_data == NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GRAB_IMAGE,
            "No image available.");
        g_propagate_error(error_loc, phantom_error);
        return FALSE;
    }

    // copy the image data to the output buffer
    // CAUTION : no verification is done on the size of the output buffer...
    // This is dangerous as it as it puts the user in charge of allocating the
    // right amount of memory with the good bit depth.
    // TODO : Mqybe consider GBytes for the output buffer ?
    memcpy(data, cine_data->UnpackedImages, cine_data->SizePerImageUnpacked);

    // If we arrive in a new buffer, free the previous one
    if (cine_data->buffer_index != prev_buffer_index) {
        g_debug("Freeing buffer %d\n", prev_buffer_index);
        g_free(g_ptr_array_remove_index(self->unpacked_images, prev_buffer_index));
        prev_buffer_index = cine_data->buffer_index;
    }

    // Free the CineData object
    g_free(cine_data);

    return TRUE;
}

gboolean uca_phantom_communicate_grab_buffered_image (UcaPhantomCommunicate* self, gpointer data, GError** error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);

    // CAUTION : no verification is done on the size of the output buffer...
    // This is dangerous as it as it puts the user in charge of allocating the
    // right amount of memory with the good bit depth.
    // TODO : Mqybe consider GBytes for the output buffer ?
    gsize image_size = self->settings.roi_pixel_width * self->settings.roi_pixel_height * self->settings.sensor_bit_depth / 8;
    ringbuf_memcpy_from (self->ring_buffer, data, image_size);

    return TRUE;
}

gboolean uca_phantom_communicate_stop_readout(UcaPhantomCommunicate* self, GError** error_loc)
{
    g_return_val_if_fail(error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(self->control_connection_state == CONNECTED, FALSE);
    g_return_val_if_fail(self->local_acquisition_state == ACQUIRING, FALSE);
    g_return_val_if_fail(self->phantom_acquisition_state == ACQUIRING, FALSE);

    GError* sub_error = NULL;
    GError* phantom_error = NULL;

    // Push end request to the queue to unblock the data_receiver thread
    ImageRequest* request = g_new0(ImageRequest, 1);
    request->end_request = TRUE;
    request->nb_images = 0;
    request->img_format = 0;

    g_async_queue_push(self->request_queue, request);

    sub_error = g_thread_join(self->data_receiver);
    if (sub_error != NULL) {
        g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_STOP_READOUT,
            "Failed to join data_receiver thread:\n\t> %s\n", sub_error->message);
        g_propagate_error(error_loc, phantom_error);
        g_clear_error(&sub_error);
        return FALSE;
    }

    if (self->xenabled) {
        // Stop the data unpacker thread
        sub_error = g_thread_join(self->data_unpacker);
        if (sub_error != NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_STOP_READOUT,
                "Failed to join data_unpacker thread:\n\t> %s\n", sub_error->message);
            g_propagate_error(error_loc, phantom_error);
            g_clear_error(&sub_error);
            return FALSE;
        }
    }

    if (self->buffering) {
        // Stop the buffering thread
        sub_error = g_thread_join(self->buffering_thread);
        if (sub_error != NULL) {
            g_set_error(&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_STOP_READOUT,
                "Failed to join buffering_thread thread:\n\t> %s\n", sub_error->message);
            g_propagate_error(error_loc, phantom_error);
            g_clear_error(&sub_error);
            return FALSE;
        }
    }

    self->phantom_acquisition_state = IDLE;
    self->local_acquisition_state = IDLE;

    return TRUE;
}
