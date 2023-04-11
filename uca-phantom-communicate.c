#include <gio/gio.h>
#include <gmodule.h>
#include <glib-object.h>
#include <string.h>
#include <unistd.h>
#include <nmmintrin.h>

#include <linux/socket.h>
#include <sys/socket.h>
#include <asm-generic/socket.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <sys/types.h>
#include <pcap.h>

#include <time.h>

// Include if.h, socket.h and string.h

#include <arpa/inet.h>
//#include <netinet/if_ether.h>
#include <poll.h>
#include <linux/if.h> // This is making trouble
#include <linux/if_tun.h> // This is making trouble
// #include <net/if.h> // This is making trouble
#include <linux/ip.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <netdb.h>

#include "uca-phantom-communicate.h"
#include "uca-phantom-variables.h"
#include "uca-phantom-commands.h"


#define ETHERNET_HEADER_SIZE 32 // 16 bytes for ethernet header, 16 bytes for custom header
#define MAX_KERNEL_RING_SIZE 1073741824 // 2GBytes = 2^31 - 1 Bytes
#define MAX_NUMBER_CINE_DATA 1000 // Maximum number of cine data packets to receive

/**
 * TODO: 
 * - Add documentation
 * - Automatically discover phantom IP if not specified
 * 
 * - Implement state variables for control connection and data connection
 * - Implement cancelable functions
 * - Implement 10Gb Ethernet support
*/

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
    N_PROPERTIES
} UcaPhantomProperties;

typedef enum {
    CONNECTED,
    DISCONNECTED,
    CONNECTING,
    DISCONNECTING
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

typedef struct {
    gint x, y;
    guint w, h, threshold, area, speed, mode;
} Trigger;

typedef struct _short_time_stamp { //cam.tsformat = 0
    unsigned int csecs; // time from beginning of the year in 1/100 sec units
    unsigned short exptime; // exposure time in us
    unsigned short frac; // bits[15..2]: fractions (us to 10000); b[1]:event;[0]:lock
} short_time_stamp;
typedef struct _short_time_stamp32 { //cam.tsformat = 1
    unsigned int csecs; // time from beginning of the year in 1/100 sec units
    unsigned short exptime; // exposure time in us
    unsigned short frac; // bits[15..2]: fractions (us to 10000); b[1]:event; b[0]:lock
    unsigned short exptime32; // exposure time extension (1/65536 of a us)
    unsigned short frac32; // time stamp extension (1/65536 of a us)
} short_time_stamp32;
typedef struct _long_time_stamp { //cam.tsformat = 2
    unsigned int csecs; // time from beginning of the year in 1/100 sec units
    unsigned short exptime; // exposure time in us
    unsigned short frac; // bits[15..2]: fractions (us to 10000); bit[1]:event; bit[0]:lock
    unsigned int range_d0; // first 32bits received as rangedata, lsb first, big endian
    unsigned int range_d1; // second 32bits received as rangedata, lsb first, big endian
    unsigned int range_d2; // third 32bits received as rangedata, lsb first, big endian
    unsigned int range_d3; // fourth 32bits received as rangedata, lsb first, big endian
} long_time_stamp;
typedef struct _long_time_stamp32 { //cam.tsformat = 3
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

gsize TimestampSize[4] = {sizeof(short_time_stamp), sizeof(short_time_stamp32), sizeof(long_time_stamp), sizeof(long_time_stamp32)};
gchar *TimestampFormatString[4] = {"0", "1", "2", "3"};
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

const gchar *ImageFormatString[] = {"8", "8R", "P16", "P16R", "P10", "P12L"};
gfloat ImageBitDepth[6] = {1, 1, 2, 2, 1.25, 1.5};


typedef struct _InternalRequest {
    guint64 nb_images;
    ImageFormat img_format;
    gboolean end_request;
} InternalRequest;

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
} TimeStamp;
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
    CaptureSettings *settings;
    ImageFormat format;
    TimestampFormat tsformat;
    gpointer RawImages;
    gpointer UnpackedImages;
    gpointer RawTimestamps;
    guint nb_images;
} CineData;

// Forward declaration of overrideable functions
static void uca_phantom_communicate_set_property (GObject  *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void uca_phantom_communicate_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void uca_phantom_communicate_constructed (GObject *object);
static void uca_phantom_communicate_dispose (GObject *object);
static void uca_phantom_communicate_finalize (GObject *object);

static gboolean uca_phantom_communicate_get_resolution (UcaPhantomCommunicate *self, guint16 *width, guint16 *height, GError **error_loc);
gboolean uca_phantom_communicate_get_capture_settings (UcaPhantomCommunicate *self, CaptureSettings *settings, GError **error_loc);

static GParamSpec *uca_phantom_communicate_properties[N_PROPERTIES] = {NULL, };

struct _UcaPhantomCommunicate {
    GObject parent_object;

    gboolean xenabled;
    gchar *phantom_ip, *phantom_xip;
    gchar *netcard_ip, *netcard_xip;
    gchar *netcard, *xnetcard;
    guint control_port;
    guint8 mac_address[6];
    guint phantom_ipsource;

    ConnectionState control_state;
    ConnectionState data_state;
    AcquisitionState acquisition_state;

    // camera setup variables
    gboolean timestamping;
    TimestampFormat ts_format;
    CaptureSettings settings;

    // Command stream connection variables
    GSocketConnection *control_connection;
    GSocketClient *control_client;
    GSocketAddress *control_address;
    
    // Data stream connection variables
    pcap_t *handle;
    
    int data_socket_fd;
    GSocketListener *listener;
    GSocketAddress *local_address;
    GSocketAddress *remote_address;
    GSocket *data_socket;
    GSocketConnection *data_connection;
    GInputStream *input_datastream;

    GThread *data_receiver;
    GThread *data_unpacker;
    GAsyncQueue *packed_queue;
    GAsyncQueue *unpacked_queue;
    GAsyncQueue *time_queue;
    GAsyncQueue *request_queue;
};

G_DEFINE_TYPE (UcaPhantomCommunicate, uca_phantom_communicate, G_TYPE_OBJECT)

G_DEFINE_QUARK (uca-phantom-communicate-error-quark, uca_phantom_communicate_error)

static void uca_phantom_communicate_class_init (UcaPhantomCommunicateClass *class) {
    GObjectClass *gobject_class = G_OBJECT_CLASS (class);

    gobject_class->set_property = uca_phantom_communicate_set_property;
    gobject_class->get_property = uca_phantom_communicate_get_property;
    gobject_class->constructed = uca_phantom_communicate_constructed;
    gobject_class->dispose = uca_phantom_communicate_dispose;
    gobject_class->finalize = uca_phantom_communicate_finalize;

    // install properties
    uca_phantom_communicate_properties[PROP_PHANTOM_IP] =
        g_param_spec_string (
            "phantom_ip",
            "Phantom IP address",
            "IP address of the Phantom camera over normal 1Gb ethernet",
            "100.100.189.164",
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_PHANTOM_XIP] =
        g_param_spec_string (
            "phantom_xip",
            "Phantom 10Gb IP address",
            "IP address of the Phantom camera over a 10Gb ethernet",
            "172.16.31.157",
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_NETCARD_IP] =
        g_param_spec_string (
            "netcard_ip",
            "Network card IP",
            "IP address of the network card used for 1Gb ethernet",
            "100.100.100.1",
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_NETCARD_XIP] =
        g_param_spec_string (
            "netcard_xip",
            "10Gb network card IP",
            "IP address of the network card used for 10Gb ethernet",
            "172.16.0.1",
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_NETCARD] =
        g_param_spec_string (
            "netcard",
            "Network card",
            "Name of the network card used for 1Gb ethernet",
            "eth0",
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_XNETCARD] =
        g_param_spec_string (
            "xnetcard",
            "10Gb network card",
            "Name of the network card used for 10Gb ethernet",
            "eth1",
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_XENABLED] =
        g_param_spec_boolean (
            "xenabled",
            "Enable 10Gb data transfer",
            "Enable 10Gb data transfer",
            FALSE,
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

    uca_phantom_communicate_properties[PROP_CONTROL_PORT] =
        g_param_spec_uint (
            "control_port",
            "Set connection port",
            "Set the port used to establish TCP connection with phantom",
            1024, 49151, 7115,
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    
    uca_phantom_communicate_properties[PROP_PHANTOM_IPSOURCE] =
        g_param_spec_uint (
            "phantom_ipsource",
            "Set the IP source using IP flags",
            "Possible flags: USE_ENV, USE_CLASS, USE_DISCOVER.",
            0, N_IP_FLAGS, USE_CLASS,
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    
            
    g_object_class_install_properties (
        gobject_class, 
        N_PROPERTIES, 
        uca_phantom_communicate_properties);
}

static void uca_phantom_communicate_init (UcaPhantomCommunicate *instance) {
    g_print ("Initializing UcaPhantomCommunicate\n");

    instance->phantom_ip = NULL;
    instance->phantom_xip = NULL;
    instance->netcard = NULL;
    instance->xnetcard = NULL;
    instance->netcard_ip = NULL;
    instance->netcard_xip = NULL;
    instance->xenabled = TRUE;

    instance->control_state = DISCONNECTED;
    instance->data_state = DISCONNECTED;
    instance->acquisition_state = IDLE;

    // create a new control connection
    instance->control_client = g_socket_client_new();
    instance->control_address = NULL;
    instance->control_connection = NULL;

    // create a new data connection
    instance->handle = NULL;
    instance->data_socket_fd = -1;
    instance->listener = g_socket_listener_new();
    instance->data_connection = NULL;
    
    instance->input_datastream = NULL;

    instance->data_receiver = NULL;
    instance->data_unpacker = NULL;
    instance->packed_queue = g_async_queue_new();
    instance->unpacked_queue = g_async_queue_new();
    instance->request_queue = g_async_queue_new();

    // Camera setup variables
    instance->timestamping = FALSE;
    instance->ts_format = TS_NONE;
    instance->settings = (CaptureSettings) {0, };
}

static void uca_phantom_communicate_constructed (GObject *object) {
    UcaPhantomCommunicate *instance = UCA_PHANTOM_COMMUNICATE (object);

    instance->control_state = DISCONNECTED;

    if (instance->xnetcard != NULL) {
        instance->xenabled = TRUE;
    }
    else {
        instance->xenabled = FALSE;
    }

    G_OBJECT_CLASS (uca_phantom_communicate_parent_class)->constructed (object);
}

static void uca_phantom_communicate_dispose (GObject *object) {
    UcaPhantomCommunicate *instance = UCA_PHANTOM_COMMUNICATE (object);

    g_print ("Disposing UcaPhantomCommunicate\n");
    // free the handle
    if (instance->handle != NULL) {
        pcap_close (instance->handle);
    }

    if (instance->data_connection != NULL) {
        g_socket_close (instance->data_connection, NULL);
    }

    g_free (instance->phantom_ip);
    g_free (instance->phantom_xip);
    g_free (instance->netcard_ip);
    g_free (instance->netcard_xip);
    g_free (instance->netcard);
    g_free (instance->xnetcard);

    G_OBJECT_CLASS (uca_phantom_communicate_parent_class)->dispose (object);
}

static void uca_phantom_communicate_finalize (GObject *object) {
    UcaPhantomCommunicate *instance = UCA_PHANTOM_COMMUNICATE (object);

    g_print ("Finalizing UcaPhantomCommunicate\n");
    if (instance->data_socket != NULL) {
        g_object_unref (instance->data_socket);
    }
    if (instance->control_connection != NULL) {
        g_object_unref (instance->control_connection);
    }

    if (instance->local_address != NULL) {
        g_object_unref (instance->local_address);
    }
    if (instance->remote_address != NULL) {
        g_object_unref (instance->remote_address);
    }

    if (instance->control_client != NULL) {
        g_object_unref (instance->control_client);
    }
    if (instance->control_address != NULL) {
        g_object_unref (instance->control_address);
    }

    if (instance->data_socket != NULL) {
        g_object_unref (instance->data_socket);
    }
    if (instance->data_connection != NULL) {
        g_object_unref (instance->data_connection);
    }
    if (instance->listener != NULL) {
        g_socket_listener_close (instance->listener);
        g_object_unref (instance->listener);
    }

    if (instance->packed_queue != NULL) {
        g_async_queue_unref (instance->packed_queue);
    }
    if (instance->unpacked_queue != NULL) {
        g_async_queue_unref (instance->unpacked_queue);
    }
    if (instance->request_queue != NULL) {
        g_async_queue_unref (instance->request_queue);
    }

    G_OBJECT_CLASS (uca_phantom_communicate_parent_class)->finalize (object);
}

/*
 * Private class definitions
*/
static void uca_phantom_communicate_set_phantom_ip (UcaPhantomCommunicate *self, const gchar *property) {
    g_free (self->phantom_ip);
    self->phantom_ip = g_strdup (property);
}
static void uca_phantom_communicate_set_phantom_xip (UcaPhantomCommunicate *self, const gchar *property) {
    g_free (self->phantom_xip);
    self->phantom_xip = g_strdup (property);
}
static void uca_phantom_communicate_set_netcard_ip (UcaPhantomCommunicate *self, const gchar *property) {
    g_free (self->netcard_ip);
    self->netcard_ip = g_strdup (property);
}
static void uca_phantom_communicate_set_netcard_xip (UcaPhantomCommunicate *self, const gchar *property) {
    g_free (self->netcard_xip);
    self->netcard_xip = g_strdup (property);
}
static void uca_phantom_communicate_set_netcard (UcaPhantomCommunicate *self, const gchar *property) {
    g_free (self->netcard);
    self->netcard = g_strdup (property);
}
static void uca_phantom_communicate_set_xnetcard (UcaPhantomCommunicate *self, const gchar *property) {
    g_free (self->xnetcard);
    self->xnetcard = g_strdup (property);
}
static void uca_phantom_communicate_set_xenabled (UcaPhantomCommunicate *self, gboolean property) {self->xenabled = property;}
static void uca_phantom_communicate_set_control_port (UcaPhantomCommunicate *self, guint property) {self->control_port = property;}
static void uca_phantom_communicate_set_ip_source (UcaPhantomCommunicate *self, guint property) {self->phantom_ipsource = property;}

static gchar *uca_phantom_communicate_get_phantom_ip (UcaPhantomCommunicate *self) {return self->phantom_ip;}
static gchar *uca_phantom_communicate_get_phantom_xip (UcaPhantomCommunicate *self) {return self->phantom_xip;}
static gchar *uca_phantom_communicate_get_netcard_ip (UcaPhantomCommunicate *self) {return self->netcard_ip;}
static gchar *uca_phantom_communicate_get_netcard_xip (UcaPhantomCommunicate *self) {return self->netcard_xip;}
static gchar *uca_phantom_communicate_get_netcard (UcaPhantomCommunicate *self) {return self->netcard;}
static gchar *uca_phantom_communicate_get_xnetcard (UcaPhantomCommunicate *self) {return self->xnetcard;}
static gboolean uca_phantom_communicate_get_xenabled (UcaPhantomCommunicate *self) {return self->xenabled;}
static guint uca_phantom_communicate_get_control_port (UcaPhantomCommunicate *self) {return self->control_port;}
static guint uca_phantom_communicate_get_ip_source (UcaPhantomCommunicate *self) {return self->phantom_ipsource;}

static void uca_phantom_communicate_set_property (
    GObject      *object,
    guint         property_id,
    const GValue *value,
    GParamSpec   *pspec) {
    UcaPhantomCommunicate *self = UCA_PHANTOM_COMMUNICATE (object);

    switch (property_id) {
    case PROP_PHANTOM_IP:
        uca_phantom_communicate_set_phantom_ip (self, g_value_get_string (value));
        break;
    case PROP_PHANTOM_XIP:
        uca_phantom_communicate_set_phantom_xip (self, g_value_get_string (value));
        break;
    case PROP_NETCARD_IP:
        uca_phantom_communicate_set_netcard_ip (self, g_value_get_string (value));
        break;
    case PROP_NETCARD_XIP:
        uca_phantom_communicate_set_netcard_xip (self, g_value_get_string (value));
        break;
    case PROP_NETCARD:
        uca_phantom_communicate_set_netcard (self, g_value_get_string (value));
        break;
    case PROP_XNETCARD:
        uca_phantom_communicate_set_xnetcard (self, g_value_get_string (value));
        break;
    case PROP_XENABLED:
        uca_phantom_communicate_set_xenabled (self, g_value_get_boolean (value));
        break;
    case PROP_CONTROL_PORT:
        uca_phantom_communicate_set_control_port (self, g_value_get_uint (value));
        break;
    case PROP_PHANTOM_IPSOURCE:
        uca_phantom_communicate_set_ip_source (self, g_value_get_uint (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void uca_phantom_communicate_get_property (
    GObject    *object,
    guint       property_id,
    GValue     *value,
    GParamSpec *pspec) {
    UcaPhantomCommunicate *self = UCA_PHANTOM_COMMUNICATE (object);

    switch (property_id) {
    case PROP_PHANTOM_IP:
        g_value_set_string (value, uca_phantom_communicate_get_phantom_ip (self));
        break;
    case PROP_PHANTOM_XIP:
        g_value_set_string (value, uca_phantom_communicate_get_phantom_xip (self));
        break;
    case PROP_NETCARD_IP:
        g_value_set_string (value, uca_phantom_communicate_get_netcard_ip (self));
        break;
    case PROP_NETCARD_XIP:
        g_value_set_string (value, uca_phantom_communicate_get_netcard_xip (self));
        break;
    case PROP_NETCARD:
        g_value_set_string (value, uca_phantom_communicate_get_netcard (self));
        break;
    case PROP_XNETCARD:
        g_value_set_string (value, uca_phantom_communicate_get_xnetcard (self));
        break;
    case PROP_XENABLED:
        g_value_set_boolean (value, uca_phantom_communicate_get_xenabled (self));
        break;
    case PROP_CONTROL_PORT:
        g_value_set_uint (value, uca_phantom_communicate_get_control_port (self));
        break;
    case PROP_PHANTOM_IPSOURCE:
        g_value_set_uint (value, uca_phantom_communicate_get_ip_source (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static gchar *
uca_phantom_communicate_discover (UcaPhantomCommunicate *self, GError **error_loc) {
    // Note: find a way to do this without a goto statement.

    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, NULL); // verify that error_loc is not set

    GError *sub_error = NULL;
    GError *phantom_error = NULL;
    GSocket *socket = NULL;
    GMatchInfo *info = NULL;
    GSocketAddress *remote_socket_addr = NULL;
    GSocketAddress *result = NULL;
    const gchar request[] = "phantom?";
    const gchar pattern[] = "PH16 (\\d+) (\\d+) (\\d+)";
    gint FLAG = ALL;
    const guint port = 7380;

    gchar reply[128] = {0,};

    g_message ("Attempting to discover the phantom...\n");

    const gchar *bcast_address = "100.100.255.255";
    GSocketAddress *bcast_socket_addr = g_inet_socket_address_new_from_string (bcast_address, port);

    if (bcast_socket_addr == NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_BCAST_ADDR, "Failed to parse broadcasting address '%s' on port '%d'\n", bcast_address, port);
        g_propagate_error (error_loc, phantom_error);

        FLAG = BCAST;
        goto cleanup;
    }

    socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, &sub_error);

    if (socket == NULL) {
        if (sub_error == NULL) {
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SOCKET, "Failed to create socket\n");
        }
        else {
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SOCKET, "Failed to create socket: %s\n", sub_error->message);
            g_error_free (sub_error);
        }
        g_propagate_error (error_loc, phantom_error);
        FLAG = SOCKET_END;
        goto cleanup;
    }

    g_socket_set_broadcast (socket, TRUE);

    gssize wrote = g_socket_send_to (socket, bcast_socket_addr, request, strlen (request), NULL, &sub_error);
    
    if (wrote < -1) {
        if (sub_error == NULL) {
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SEND, "Failed to send broadcast\n");
        }
        else {
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SEND, "Failed to send broadcast: %s\n", sub_error->message);
            g_error_free (sub_error);
        }
        g_propagate_error (error_loc, phantom_error);

        FLAG = SOCKET_END;
        goto cleanup;
    }

    gssize received = g_socket_receive_from (socket, &remote_socket_addr, reply, strlen (reply), NULL, &sub_error);
    g_print ("received: %ld\n", received);
    
    if (received < -1) {
        if (sub_error == NULL) {
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RECEIVE, "Failed to receive broadcast\n");
        }
        else {
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RECEIVE, "Failed to receive broadcast: %s\n", sub_error->message);
            g_error_free (sub_error);
        }
        g_propagate_error (error_loc, phantom_error);

        FLAG = RECEIVE;
        goto cleanup;
    }

    GRegex *regex = g_regex_new (pattern, 0, 0, &sub_error);

    if (regex == NULL) {
        if (sub_error == NULL) {
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REGEX, "Failed to create regex\n");
        }
        else {
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REGEX, "Failed to create regex: %s\n", sub_error->message);
            g_error_free (sub_error);
        }
        g_propagate_error (error_loc, phantom_error);

        FLAG = REGEX;
        goto cleanup;
    }

    gboolean matched = g_regex_match (regex, reply, 0, &info);

    if (!matched) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REGEX, "Reply '%s' does not match expected pattern.\n", reply);
        g_propagate_error (error_loc, phantom_error);

        FLAG = ALL;
        goto cleanup;
    }

    gchar *port_string = g_match_info_fetch (info, 1);

    if (port_string == NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_REGEX, "Regex pattern number 1 not found.\n");
        g_propagate_error (error_loc, phantom_error);

        FLAG = ALL;
        goto cleanup;
    }

    guint accept_port = atoi (port_string);
    g_free(port_string);

    result = g_inet_socket_address_new (g_inet_socket_address_get_address ((GInetSocketAddress *) remote_socket_addr), port);

    gchar *ip_address = g_inet_address_to_string (g_inet_socket_address_get_address ((GInetSocketAddress *) result));

    g_message ("Phantom found on port %d with the IPV4 address: %s.\n", port, ip_address);

    cleanup:
    switch (FLAG) {
        case ALL:
            g_match_info_free (info);
            /* fall through */
        case REGEX:
            g_regex_unref (regex);
            /* fall through */
        case RECEIVE:
            g_object_unref (remote_socket_addr);
            /* fall through */
        case SOCKET_END:
            g_object_unref (socket);
            /* fall through */
        case BCAST:
            g_object_unref (bcast_socket_addr);
            break;
        default:
            g_warning ("Flag set to invalid value! Fatal error.");
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
static gboolean
uca_phantom_communicate (UcaPhantomCommunicate *self, PhantomRequest *request, PhantomReply *reply, GError **error_loc) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail (request != NULL || reply != NULL, FALSE);
    g_return_val_if_fail (self->control_state == CONNECTED, FALSE);

    GError *sub_error = NULL;

    // TODO: check that the streams are succesfully fetched
    GOutputStream * ostream = g_io_stream_get_output_stream (G_IO_STREAM (self->control_connection));
    GInputStream * istream = g_io_stream_get_input_stream (G_IO_STREAM (self->control_connection));

    request->write_size = g_output_stream_write (
        ostream,
        request->message,
        request->size,
        NULL,
        &sub_error);
    
    if (request->write_size < -1) {
        g_warning ("Could not write request: %s\n", sub_error->message);
        g_propagate_error (error_loc, sub_error);

        g_output_stream_flush (ostream, NULL, NULL);

        return FALSE;
    }

    reply->read_size = g_input_stream_read (
        istream,
        reply->raw,
        reply->size,
        NULL,
        &sub_error);
    
    
    if (reply->read_size < -1) {
        g_warning ("Could not read reply: %s\n", sub_error->message);
        g_input_stream_close (istream, NULL, NULL);
        g_propagate_error (error_loc, sub_error);

        g_input_stream_close (istream, NULL, NULL);
        return FALSE;
    }
    else if (reply->read_size == 0) {
        g_warning ("Reached EOF on stream.\n");
    }

    g_debug ("raw: %s\n", reply->raw); 

    g_output_stream_flush (ostream, NULL, NULL);

    return TRUE;
}

/*
 * Public methods
 *
*/
UcaPhantomCommunicate *uca_phantom_communicate_new (void) {
    return g_object_new (UCA_TYPE_PHANTOM_COMMUNICATE, NULL);
}


gboolean uca_phantom_communicate_attempt_connect (UcaPhantomCommunicate *self, GError **error_loc) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;

    gchar *ip_address = NULL;

    switch (self->phantom_ipsource) {
    case USE_CLASS:
        ip_address = g_strdup (self->phantom_ip);
        break;
    case USE_BCAST:        
        ip_address = uca_phantom_communicate_discover (self, &sub_error);
        break;
    default:
        g_warning ("Invalid ip source.\n");
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT, "Invalid ip source.\n");
        g_propagate_error (error_loc, phantom_error);
        return FALSE;
        break;
    }

    // TODO: why doesn't this work? Check if reusing the data socket is possible

    // self->control_address = g_inet_socket_address_new_from_string (self->netcard_ip, self->control_port);
    // if (self->control_address == NULL) {
    //     g_warning ("Could not create local address.\n");
    //     g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT, "Could not create local address.\n");
    //     g_propagate_error (error_loc, phantom_error);
    //     return FALSE;
    // }

    // g_socket_client_set_local_address (self->control_client, self->control_address);

    g_message ("Attempting to establish control connection with phantom (%s:%d)\n", ip_address, self->control_port);
    self->control_connection = g_socket_client_connect_to_host (
        self->control_client,
        ip_address,
        self->control_port,
        NULL,
        &sub_error
    );

    if (self->control_connection == NULL) {
        
        if (sub_error != NULL) {
            g_warning ("Could not connect to the phantom: %s\n", sub_error->message);
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT, "Could not connect to the phantom: %s\n", sub_error->message);
            g_propagate_error (error_loc, phantom_error);
        }
        else {
            g_warning ("Could not connect to the phantom.\n");
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT, "Could not connect to the phantom: Unknown error when attempting to connect.\n");
            g_propagate_error (error_loc, phantom_error);
        }
        
        g_free (ip_address);
        return FALSE;
    }

    g_free (ip_address);


    // int optval = 1;
    // g_socket_set_option(self->control_connection, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval), &sub_error);

    if (sub_error != NULL) {
        g_warning ("Could not set socket options: %s\n", sub_error->message);
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT, "Could not set socket options: %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        return FALSE;
    }

    self->control_state = CONNECTED;

    // Set up the resolution 
    gboolean res = uca_phantom_communicate_get_resolution (self, &(self->settings.width), &(self->settings.height), error_loc);
    if (!res) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT, "Could not get resolution: %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        return FALSE;
    }
    
    return TRUE;
}

/**
 * @brief Send a command to the phantom and get the reply
 * 
 * @paragraph This variadic function is used to send a command to the phantom and get the reply. 
 * The command is specified by the command_flag. The reply is stored in the caller-owned reply struct.
 * 
 * @param self 
 * @param command_flag 
 * @param reply 
 * @param error_loc 
 * @param ... 
 * @return gboolean 
 * 
 * TODO: handle the variadic arguments better (i.e. format them into a string that fits the phantom's format)
 */
gboolean uca_phantom_communicate_run_command (UcaPhantomCommunicate *self, guint command_flag, PhantomReply *reply, GError **error_loc, ...) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail (command_flag < N_UNIT_COMMANDS, FALSE);
    g_return_val_if_fail (self->control_state == CONNECTED, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;

    // Setup the request 
    PhantomRequest request = {
        .command = Commands[command_flag],
        .message = NULL,
        .size = 0,
        .write_size = 0
    };

    // Setup args of command
    va_list va_args;
    guint nb_args = 0;
    guint nb_arg_max = Commands[command_flag].argc;
    gchar *next_arg = NULL;
    const gchar *args[nb_arg_max];

    va_start (va_args, error_loc);
    while ((next_arg = va_arg (va_args, gchar *)) != NULL) {
        nb_args++;
        if (nb_args > nb_arg_max) {
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RUN_COMMAND, "Too many arguments for command %s. Expected %d, got %d.\n", Commands[command_flag].name, Commands[command_flag].argc, nb_args);
            g_propagate_error (error_loc, phantom_error);
            return FALSE;
        }
        args[nb_args - 1] = next_arg;
    }
    va_end (va_args);

    gsize args_len = 0;
    for (guint i = 0; i < nb_args; i++) {
        args_len += strlen (args[i]);
    }

    // Manually build the message to ensure that the string is correclty NULL-ended
    request.size = (strlen (request.command.name) + request.command.argc + args_len + strlen ("\r\n")) * sizeof (request.message);
    request.message = g_malloc0 (request.size);
    
    if (request.message == NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RUN_COMMAND, "Could not allocate and assemble request message. Fatal error.\n");
        g_propagate_error (error_loc, phantom_error);
        return FALSE;
    }

    g_strlcat(request.message, request.command.name, request.size);
    for (guint i = 0; i < nb_args; i++) {
        g_strlcat(request.message, " ", request.size);
        g_strlcat(request.message, args[i], request.size);
    }
    g_strlcat(request.message, "\r\n", request.size);

    // Setup the reply
    *reply = (PhantomReply) {
        .raw = NULL,
        .size = 512,
        .value = G_VALUE_INIT,
        .read_size = 0
    };

    reply->raw = g_malloc0 (reply->size);

    if (reply->raw == NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RUN_COMMAND, "Could not allocate reply message. Fatal error.\n");
        g_propagate_error (error_loc, phantom_error);
        g_free (request.message);
        return FALSE;
    }

    // Communicate request to phantom
    gboolean communicated = uca_phantom_communicate (self, &request, reply, &sub_error);

    g_debug ("> request:\n%s \n", request.message);
    g_debug ("> reply:\n%s \n", reply->raw);
    g_free (request.message);
    request.message  = NULL;

    if (!communicated && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RUN_COMMAND, "Failed to run command %s: %s\n", request.command.name, sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);

        g_free (reply->raw);
        return FALSE;
    }

    if (communicated && sub_error != NULL) {
        g_warning ("Successfully ran command %s. However, an error occured: %s\n", request.command.name, sub_error->message);
        g_clear_error (&sub_error);
    }
    
    if (g_str_has_prefix (reply->raw, "ERR:") == TRUE) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_RUN_COMMAND, "Phantom returned an error when running command '%s': \n\t> %s\n", request.command.name, reply->raw);
        g_propagate_error (error_loc, phantom_error);
        g_free (reply->raw);
        reply->raw  = NULL;

        return FALSE;
    }
    
    return TRUE;
}

/**
 * @brief Get the value of a variable from the phantom
 * 
 * @paragraph This function will get the value of a unit variable from the phantom using the uca_phantom_communicate_run_command function.
 * 
 * @param self 
 * @param variable_flag 
 * @param return_value 
 * @param error_loc 
 * @return gboolean 
 */
gboolean uca_phantom_communicate_get_variable(UcaPhantomCommunicate *self, guint variable_flag, GValue *return_value, GError **error_loc) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail (variable_flag < N_UNIT_PROPERTIES, FALSE);
    g_return_val_if_fail (self->control_state == CONNECTED, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;
    gchar pattern[] = "\\s:\\s";
    PhantomReply reply;

    gboolean res = uca_phantom_communicate_run_command (self, CMD_GET, &reply, &sub_error, variables[variable_flag].name, NULL);

    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_VARIABLE, "Failed to get variable %s: %s\n", variables[variable_flag].name, sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }

    // Extract the actual data from the raw reply
    GRegex* regex = g_regex_new (pattern, 0, 0, &sub_error);

    if (regex == NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_VARIABLE, "Failed to create regex object. Aborting...: %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_error_free (sub_error);

        g_free (reply.raw);
        return FALSE;
    }

    gchar **matched = g_regex_split (regex, reply.raw, 0);
    g_return_val_if_fail (matched != NULL, FALSE);
    gchar* prefix = matched[0];
    gchar* suffix = matched[1];

    // Check for error mesage from phantom
    if (g_str_has_prefix (prefix, "ERR")) {
        g_warning ("Invalid phantom command: %s\n", reply.raw);

        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_VARIABLE, "Invalid phantom command: %s\n", reply.raw);
        g_propagate_error (error_loc, phantom_error);

        g_free (reply.raw);
        g_strfreev (matched);
        g_regex_unref (regex);
        return FALSE;
    }

    g_value_unset (return_value);
    g_value_init (return_value, variables[variable_flag].type);

    // Use Gvalue container to store it
    switch (variables[variable_flag].type) {
    case G_TYPE_STRING:
        g_value_set_string (return_value, suffix);
        break;
    case G_TYPE_UINT:
        g_value_set_uint (return_value, strtoul(suffix, NULL, 0));
        break;
    case G_TYPE_INT:
        g_value_set_int (return_value, atoi(suffix));
        break;
    case G_TYPE_FLOAT:
        g_value_set_float (return_value, strtof(suffix, NULL));
        break;
    // // TODO : handle these cases in a more custom way in the future ?
    // case PHANTOM_TYPE_HEX:
    // case PHANTOM_TYPE_RES:
    default:
        g_warning ("Type not handled yet!\n");
        break;
    }

    // Cleanup
    g_strfreev (matched);        
    matched = NULL;
    g_regex_unref (regex);
    regex = NULL;
    g_free (reply.raw);
    reply.raw  = NULL;
    
    return TRUE;
}

/**
 * @brief Set the value of a variable on the phantom
 * 
 * @param self 
 * @param variable_flag 
 * @param value 
 * @param error_loc 
 * @return gboolean 
 */
gboolean uca_phantom_communicate_set_variable (UcaPhantomCommunicate *self, guint variable_flag, const gchar *value, GError **error_loc) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail (variable_flag < N_UNIT_PROPERTIES, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);
    g_return_val_if_fail (variables[variable_flag].flags & G_PARAM_WRITABLE, FALSE);
    g_return_val_if_fail(self->control_state == CONNECTED, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;
    PhantomReply reply;

    gboolean res = uca_phantom_communicate_run_command (self, CMD_SET, &reply, &sub_error, variables[variable_flag].name, value, NULL);

    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_VARIABLE, "Failed to set variable '%s':\n\t> %s\n", variables[variable_flag].name, sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }
    g_free (reply.raw);
    reply.raw  = NULL;

    return TRUE;
}

static gboolean uca_phantom_communicate_get_resolution (UcaPhantomCommunicate *self, guint16 *width, guint16 *height, GError **error_loc) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail (self->control_state == CONNECTED, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;
    const gchar *pattern = "([0-9]+)\\sx\\s([0-9]+)";
    GValue resolution = G_VALUE_INIT;
    const gchar *reply = NULL;

    gboolean res = uca_phantom_communicate_get_variable (self, PROP_DEFC_RES, &resolution, &sub_error);

    if (res != TRUE && sub_error != NULL) {
        g_print("Failed to get resolution:\n\t> %s\n", sub_error->message);
        g_clear_error (&sub_error);
        return FALSE;
    }

    reply = g_value_get_string (&resolution);

    // Extract the actual data from the raw reply
    GRegex* regex = g_regex_new (pattern, 0, 0, &sub_error);

    if (regex == NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_RESOLUTION, "Failed to create regex object. Aborting...: %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_error_free (sub_error);
        return FALSE;
    }

    gchar **matched = g_regex_split (regex, reply, 0);
    g_return_val_if_fail (matched != NULL, FALSE);

    // Check for error mesage from phantom
    if (g_str_has_prefix (matched[0], "ERR")) {
        g_warning ("Invalid phantom command: %s\n", reply);

        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_VARIABLE, "Invalid phantom command: %s\n", reply);
        g_propagate_error (error_loc, phantom_error);

        g_strfreev (matched);
        g_regex_unref (regex);
        return FALSE;
    }

    gchar* width_s = matched[1];
    gchar* height_s = matched[2];

    *width = g_ascii_strtoull (width_s, NULL, 10);
    *height = g_ascii_strtoull (height_s, NULL, 10);

    g_print ("Resolution: %dx%d\n", *width, *height);

    g_strfreev (matched);
    g_regex_unref (regex);
    g_value_unset (&resolution);

    return TRUE;
}

gboolean uca_phantom_communicate_get_capture_settings (UcaPhantomCommunicate *self, CaptureSettings *settings, GError **error_loc) {
    // Use uca_phantom_commmunicate_get_variable to get the values of the variables in the CaptureSettings struct
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail (self->control_state == CONNECTED, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;
    GValue value = G_VALUE_INIT;
    gboolean res = FALSE;

    // Height and width
    res = uca_phantom_communicate_get_resolution (self, &(settings->width), &(settings->height), &sub_error);
    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_CAPTURE_SETTINGS, "Failed to get resolution:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }

    // FPS
    res = uca_phantom_communicate_get_variable (self, PROP_DEFC_RATE, &value, &sub_error);
    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_CAPTURE_SETTINGS, "Failed to get fps:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }
    settings->fps = g_value_get_float (&value);
    g_value_reset (&value);

    // Exposure
    res = uca_phantom_communicate_get_variable (self, PROP_DEFC_EXP, &value, &sub_error);
    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_CAPTURE_SETTINGS, "Failed to get exposure:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }
    settings->exposure = g_value_get_uint (&value);
    g_value_reset (&value);

    // Focal length
    res = uca_phantom_communicate_get_variable (self, PROP_META_FLEN, &value, &sub_error);
    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_CAPTURE_SETTINGS, "Failed to get focal length:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }
    settings->focal_length = g_value_get_float (&value);
    g_value_reset (&value);

    // Aperture
    res = uca_phantom_communicate_get_variable (self, PROP_META_FSTOP, &value, &sub_error);
    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_CAPTURE_SETTINGS, "Failed to get aperture:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }
    settings->aperture = g_value_get_float (&value);
    g_value_reset (&value);

    // Post trigger
    res = uca_phantom_communicate_get_variable (self, PROP_DEFC_PTFRAMES, &value, &sub_error);
    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_CAPTURE_SETTINGS, "Failed to get number of post trigger frames:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }
    settings->post_trigger = g_value_get_uint (&value);
    g_value_reset (&value);

    // TODO: Aquisition mode
    // res = uca_phantom_communicate_get_variable (self, PROP_DEFC_ACQMODE, &value, &sub_error);
    // if (res != TRUE && sub_error != NULL) {
    //     g_print("Failed to get aquisition mode:\n\t> %s\n", sub_error->message);
    //     g_clear_error (&sub_error);
    //     return FALSE;
    // }
    // self->settings->aquisition_mode = g_value_get_int (&value);

    // TODO: Trigger mode
    // res = uca_phantom_communicate_get_variable (self, PROP_DEFC_TRIGMODE, &value, &sub_error);
    // if (res != TRUE && sub_error != NULL) {
    //     g_print("Failed to get trigger mode:\n\t> %s\n", sub_error->message);
    //     g_clear_error (&sub_error);
    //     return FALSE;
    // }
    // self->settings->trigger_mode = g_value_get_int (&value);
    
    g_value_unset (&value);

    return TRUE;

}



/**
 * Print the current capture settings
*/
void uca_phantom_communicate_print_capture_settings (UcaPhantomCommunicate *self) {
    g_return_if_fail (self != NULL);
    g_return_if_fail (self->control_state == CONNECTED);

    g_print ("Capture settings:\n");
    g_print ("\tResolution: %dx%d\n", self->settings.width, self->settings.height);
    g_print ("\tFPS: %f\n", self->settings.fps);
    g_print ("\tExposure: %d\n", self->settings.exposure);
    g_print ("\tFocal length: %f\n", self->settings.focal_length);
    g_print ("\tAperture: %f\n", self->settings.aperture);
    g_print ("\tPost trigger: %d\n", self->settings.post_trigger);
    // g_print ("\tAquisition mode: %d\n", self->settings.aquisition_mode);
    // g_print ("\tTrigger mode: %d\n", self->settings.trigger_mode);
}


gboolean uca_phantom_communicate_set_capture_settings (UcaPhantomCommunicate *self, CaptureSettings *settings, GError **error_loc) {
    g_return_val_if_fail (settings != NULL, FALSE);
    g_return_val_if_fail(self->control_state == CONNECTED, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;
    PhantomReply reply;
    gboolean res;

    // use set command to set varaibles of Defc struct
    gchar *resolution = g_strdup_printf ("%dx%d", settings->width, settings->height);
    gchar *fps = g_strdup_printf ("%f", settings->fps);
    gchar *focal_length = g_strdup_printf ("%f", settings->focal_length);
    gchar *aperture = g_strdup_printf ("%f", settings->aperture);
    gchar *exposure = g_strdup_printf ("%d", settings->exposure);
    gchar *ptframes = g_strdup_printf ("%d", settings->post_trigger);
    // gchar *acqmode = g_strdup_printf ("%d", settings->aquisition_mode);
    // gchar *trigmode = g_strdup_printf ("%d", settings->trigger_mode);

    res = uca_phantom_communicate_set_variable (self, PROP_DEFC_RES, resolution, &sub_error);
    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_CAPTURE_SETTINGS, "Failed to set resolution:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }
    res = uca_phantom_communicate_set_variable (self, PROP_DEFC_RATE, fps, &sub_error);
    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_CAPTURE_SETTINGS, "Failed to set fps:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }
    res = uca_phantom_communicate_set_variable (self, PROP_DEFC_EXP, exposure, &sub_error);
    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_CAPTURE_SETTINGS, "Failed to set exposure:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }
    res = uca_phantom_communicate_set_variable (self, PROP_DEFC_PTFRAMES, ptframes, &sub_error);
    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_CAPTURE_SETTINGS, "Failed to set post_trigger:\n\t> %s\n", sub_error->message);
       g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }
    res = uca_phantom_communicate_run_command (self, CMD_SET_LENS_APERTURE, &reply, &sub_error, aperture, NULL);
    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_CAPTURE_SETTINGS, "Failed to set aperture:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }
    res = uca_phantom_communicate_run_command (self, CMD_MOVE_FOCUS, &reply, &sub_error, focal_length, NULL);
    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_CAPTURE_SETTINGS, "Failed to set focal_length:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }

    // gboolean res = uca_phantom_communicate_set_variable (self, PROP_DEFC_ACQMODE, acqmode, &sub_error);
    // if (res != TRUE && sub_error != NULL) {
    //     g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_CAPTURE_SETTINGS, "Failed to set aquisition_mode:\n\t> %s\n", sub_error->message);
    //     g_propagate_error (&priv->error, phantom_error);
    //     g_clear_error (&sub_error);
    //     return FALSE;
    // }
    // gboolean res = uca_phantom_communicate_set_variable (self, PROP_DEFC_TRIGMODE, trigmode, &sub_error);
    // if (res != TRUE && sub_error != NULL) {
    //     g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SET_CAPTURE_SETTINGS, "Failed to set trigger_mode:\n\t> %s\n", sub_error->message);
    //     g_propagate_error (&priv->error, phantom_error);
    //     g_clear_error (&sub_error);
    //     return FALSE;
    // }
    return TRUE;
}

gboolean uca_phantom_communicate_connect_datastream (UcaPhantomCommunicate *self, guint16 port, GError **error_loc) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;

    self->local_address = g_inet_socket_address_new_from_string (self->netcard_ip, port);
    self->remote_address = g_inet_socket_address_new_from_string (self->phantom_ip, port);

    g_print ("Connecting to %s:%d\n", self->phantom_ip, port);
    g_print ("From %s\n", self->netcard_ip);

    GSocket *local_socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, &sub_error);
    if (local_socket == NULL && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_DATASTREAM, "Failed to create socket:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }

    gboolean is_binded = g_socket_bind (local_socket, self->local_address, TRUE, &sub_error);
    if (is_binded != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_DATASTREAM, "Failed to bind socket:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }
    
    g_print ("Socket is binded and listening\n");
    gboolean is_listening = g_socket_listen (local_socket, &sub_error);
    if (is_listening != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_DATASTREAM, "Failed to listen on socket:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }
    g_print ("Socket is listening.\n");

    // Send the request to connect to the datastream
    PhantomReply reply;

    // 7117
    gchar *arg = g_strdup_printf ("{port:%d}", port);

    gboolean res = uca_phantom_communicate_run_command (self, CMD_START_DATA_CONNECTION, &reply, &sub_error, arg, NULL);

    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_DATASTREAM, "Failed to connect to datastream on port %d:\n\t> %s\n", 7117, sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }

    g_print ("reply: %s", reply.raw);

    g_free (arg);
    arg = NULL;
    g_free (reply.raw);
    reply.raw  = NULL;
    

    self->data_socket = g_socket_accept (local_socket, NULL, &sub_error);
    if (sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_DATASTREAM, "Failed to connect to datastream on port %d:\n\t> %s\n", port, sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }
    g_print ("Socket is accepted.\n");
    // Check if the socket is connected 
    if (g_socket_is_connected (self->data_socket) == TRUE) {
        g_print ("Socket is connected.\n");
    }
    else {
        g_print ("Socket is not connected.\n");
    }

    self->data_connection = g_socket_connection_factory_create_connection (self->data_socket);

    // Print the address of the connected socket
    GSocketAddress *address = g_socket_connection_get_remote_address (self->data_connection, NULL);
    GInetAddress *connected_inet_address = g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (address));
    gchar *address_string = g_inet_address_to_string (connected_inet_address);
    g_print (" on address %s \n", address_string);
    g_free (address_string);
    g_object_unref (address);
    g_object_unref (connected_inet_address);

    return TRUE;
}

/**
 * uca_phantom_communicate_connect_xdatastream:
 * 
 * Opens a socket that accepts connections from the Phantom on port @port.
 * 
*/
gboolean uca_phantom_communicate_connect_xdatastream (UcaPhantomCommunicate *self, guint16 port, GError **error_loc) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;

    char errbuf[PCAP_ERRBUF_SIZE];
    struct bpf_program fp;

    // Use libpcap to capture ethernet frames from the NIC called "self->xnetcard"

    // Open the device for live capture 
    self->handle = pcap_create(self->xnetcard, errbuf);
    if (self->handle == NULL) {
        g_print ("Error creating capture self->handle: %s\n", errbuf);
        return FALSE;
    }
    printf("Opening device %s for packet capture\n", self->xnetcard);

    // Set the capture options
    pcap_set_snaplen(self->handle, 65536);
    pcap_set_promisc(self->handle, FALSE);
    pcap_set_timeout(self->handle, 1000);
    pcap_set_rfmon(self->handle, FALSE);
    // Use size of a Jumbo frame
    pcap_set_buffer_size(self->handle, MAX_KERNEL_RING_SIZE);
    pcap_set_immediate_mode(self->handle, TRUE); // Set the capture mechanism to PACKET_MMAP

    // Activate the capture self->handle
    if (pcap_activate(self->handle) == -1) {
        g_print ("Error activating capture self->handle: %s\n", pcap_geterr(self->handle));
        pcap_close(self->handle);
        self->handle = NULL;
        return FALSE;
    }
    // Compile the filter to capture packets with ethertype 0x88b7
    if (pcap_compile(self->handle, &fp, "ether proto 0x88b7", 0, PCAP_NETMASK_UNKNOWN) == -1) {
        g_print ("Error compiling filter: %s\n", pcap_geterr(self->handle));
        pcap_close(self->handle);
        self->handle = NULL;
        return FALSE;
    }
    if (pcap_setfilter(self->handle, &fp) == -1) {
        g_print ("Error setting capture filter: %s\n", pcap_geterr(self->handle));
        pcap_close(self->handle);
        self->handle = NULL;
        return FALSE;
    }
    pcap_freecode(&fp);

    return TRUE;
}

gboolean uca_phantom_communicate_disconnect_datastream (UcaPhantomCommunicate *self, GError **error_loc) {
    // First check if started readout

    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);

    g_return_val_if_fail (G_IS_INPUT_STREAM (self->input_datastream), FALSE);
    g_return_val_if_fail (G_IS_SOCKET_CONNECTION (self->data_connection), FALSE);
    g_return_val_if_fail (G_IS_SOCKET_LISTENER (self->listener), FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;

    // Data stream is ended when the socket is closed 
    g_print ("Closing input stream\n");
    g_input_stream_close (self->input_datastream, NULL, &sub_error);
    if (sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_DISCONNECT_DATASTREAM, "Failed to close input stream:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }

    g_print ("Closing listener\n");
    g_socket_listener_close (self->listener);

    return TRUE;
}

gboolean uca_phantom_communicate_arm (UcaPhantomCommunicate *self, gchar *cine, GError **error_loc) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(self->control_state == CONNECTED, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;

    PhantomReply reply;

    gboolean res = uca_phantom_communicate_run_command (self, CMD_START_RECORDING_IN_A_CINE, &reply, &sub_error, cine, NULL);

    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to arm:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }

    g_free(reply.raw);

    return TRUE;
}

gboolean uca_phantom_communicate_trigger (UcaPhantomCommunicate *self, GError **error_loc) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail(self->control_state == CONNECTED, FALSE);

    g_print("stqrt uca_phantom_communicate_trigger\n");

    GError *sub_error = NULL;
    GError *phantom_error = NULL;

    PhantomReply reply;

    gboolean res = uca_phantom_communicate_run_command (self, CMD_SOFTWARE_TRIGGER, &reply, &sub_error, NULL);

    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to trigger:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }

    g_print("uca_phantom_communicate_trigger: %s", reply.raw);

    g_free(reply.raw);

    return TRUE;
}

/**
 * TODO: Add description
 * TODO: add CaptureSettings agument
*/
gboolean uca_phantom_communicate_request_images (
    UcaPhantomCommunicate *self,
    gint cine, 
    guint nb_images,
    guint img_format,
    guint ts_format,
    GError **error_loc) {
        
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail (self->control_state == CONNECTED, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;
    PhantomReply reply;

    // Check that img_format is valid
    if (img_format > IMG_P12L) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_INVALID_ARGUMENT, "Invalid image format: %d", img_format);
        g_propagate_error (error_loc, phantom_error);

        return FALSE;
    }
    if (ts_format > TS_NONE) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_INVALID_ARGUMENT, "Invalid timestamp format: %d", ts_format);
        g_propagate_error (error_loc, phantom_error);

        return FALSE;
    }

    // Check if the default timestamp format has changed and update it if necessary
    if (ts_format != self->ts_format && self->timestamping == TRUE) {
        self->ts_format = ts_format;
        gboolean res = uca_phantom_communicate_set_variable (self, PROP_CAM_TSFORMAT, TimestampFormatString[ts_format], &sub_error);
        if (res != TRUE && sub_error != NULL) {
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to set timestamp format:\n\t> %s\n", sub_error->message);
            g_propagate_error (error_loc, phantom_error);
            g_clear_error (&sub_error);
            return FALSE;
        }
    }

    // Setup the arguments for get_images command
    gchar *img_args = g_strdup_printf ("{cine:%d, start:%d, cnt:%d, fmt:%s, from:%s}", cine, 0, nb_images, ImageFormatString[img_format], "0");
    if (img_args == NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to allocate memory for get_images command");
        g_propagate_error (error_loc, phantom_error);
        return FALSE;
    }

    gboolean res = uca_phantom_communicate_run_command (
        self,
        CMD_GET_IMAGES,
        &reply,
        &sub_error,
        img_args,
        NULL);
    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to get images:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        g_free (img_args);
        return FALSE;
    }
    g_free (img_args);
    img_args = NULL;
    g_free (reply.raw);
    reply.raw = NULL;

    // Setup the arguments for get_timestamps command
    if (ts_format != TS_NONE && self->timestamping == TRUE) {
        gchar* ts_args = g_strdup_printf ("{cine:%d, start:%d, cnt:%d, from:%s}", cine, 0, nb_images, "0");
        if (ts_args == NULL) {
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to allocate memory for get_timestamps command");
            g_propagate_error (error_loc, phantom_error);
            return FALSE;
        }
        res = uca_phantom_communicate_run_command (
            self,
            CMD_GET_TIMESTAMPS,
            &reply,
            &sub_error,
            ts_args,
            NULL);
        if (res != TRUE && sub_error != NULL) {
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to get timestamping:\n\t> %s\n", sub_error->message);
            g_propagate_error (error_loc, phantom_error);
            g_clear_error (&sub_error);
            g_free (ts_args);
            return FALSE;
        }
        g_free (ts_args);
        ts_args = NULL;
        g_free (reply.raw);
        reply.raw = NULL;
    }

    // Create internal request
    InternalRequest *request = g_new0 (InternalRequest, 1);
    request->nb_images = nb_images;
    request->img_format = img_format;
    request->end_request = FALSE;

    // Push request to request queue
    g_async_queue_push (self->request_queue, request);

    return TRUE;
}

gboolean uca_phantom_communicate_get_mac_address (UcaPhantomCommunicate *self, GError **error_loc) {
    g_return_val_if_fail (UCA_IS_PHANTOM_COMMUNICATE (self), FALSE);
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);

    GError *phantom_error = NULL;
    GError *sub_error = NULL;


    // Get the MAC address on Linux platform
    #ifdef __linux__        
        struct ifreq ifr = {0, };

        // Open a socket for the ioctl call
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_MAC_ADDRESS, "Failed to open socket");
            g_propagate_error (error_loc, phantom_error);
            return FALSE;
        }

        // Set the interface name using g_strlcpy
        g_strlcpy(ifr.ifr_name, self->xnetcard, IFNAMSIZ);

        // Get the MAC address
        if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_MAC_ADDRESS, "Failed to get MAC address");
            g_propagate_error (error_loc, phantom_error);
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

    // print the mac address
    g_print ("MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n", 
        self->mac_address[0],
        self->mac_address[1],
        self->mac_address[2],
        self->mac_address[3],
        self->mac_address[4],
        self->mac_address[5]);

    // // Get the MAC address on Windows platform
    // #elif _WIN32
    //     // Get the MAC address
    //     IP_ADAPTER_INFO AdapterInfo[16];
    //     DWORD dwBufLen = sizeof(AdapterInfo);
    //     DWORD dwStatus = GetAdaptersInfo(AdapterInfo, &dwBufLen);
    //     if (dwStatus != ERROR_SUCCESS) {
    //         g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_MAC_ADDRESS, "Failed to get MAC address");
    //         g_propagate_error (error_loc, phantom_error);
    //         return FALSE;
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
    //         g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GET_MAC_ADDRESS, "Failed to find interface");
    //         g_propagate_error (error_loc, phantom_error);
    //         return FALSE;
    //     }
    
    return TRUE;
}

/**
 * @brief uca_phantom_communicate_request_ximages
 * 
 * @param self 
 * @param cine 
 * @param nb_images 
 * @param img_format 
 * @param ts_format 
 * @param error_loc 
 * @return gboolean 
 */
gboolean uca_phantom_communicate_request_ximages (UcaPhantomCommunicate *self, guint cine, guint nb_images, guint img_format, guint ts_format, GError **error_loc) {
    g_return_val_if_fail (UCA_IS_PHANTOM_COMMUNICATE (self), FALSE);
    g_return_val_if_fail (self->control_state == CONNECTED, FALSE);
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail (nb_images > 0, FALSE);
    g_return_val_if_fail (self->xenabled, FALSE);

    GError *phantom_error = NULL;
    GError *sub_error = NULL;
    PhantomReply reply;

    // Check if the image format is valid
    if (img_format > IMG_P12L) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_INVALID_ARGUMENT, "Invalid image format: %d", img_format);
        g_propagate_error (error_loc, phantom_error);

        return FALSE;
    }
    if (ts_format > TS_NONE) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_INVALID_ARGUMENT, "Invalid timestamp format: %d", ts_format);
        g_propagate_error (error_loc, phantom_error);

        return FALSE;
    }

    // Check if the default timestamp format has changed and update it if necessary
    if (ts_format != self->ts_format && self->timestamping == TRUE) {
        self->ts_format = ts_format;
        gboolean res = uca_phantom_communicate_set_variable (self, PROP_CAM_TSFORMAT, TimestampFormatString[ts_format], &sub_error);
        if (res != TRUE && sub_error != NULL) {
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to set timestamp format:\n\t> %s\n", sub_error->message);
            g_propagate_error (error_loc, phantom_error);
            g_clear_error (&sub_error);
            return FALSE;
        }
    }

    // Get the mac address of the camera
    gboolean res = uca_phantom_communicate_get_mac_address (self, &sub_error);
    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to get MAC address:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }

    gchar *mac = g_strdup_printf ("%02x%02x%02x%02x%02x%02x", 
        self->mac_address[0],
        self->mac_address[1],
        self->mac_address[2],
        self->mac_address[3],
        self->mac_address[4],
        self->mac_address[5]);
    if (mac == NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to allocate memory for MAC address");
        g_propagate_error (error_loc, phantom_error);
        return FALSE;
    }

    g_print ("MAC: %s\n", mac);

    // Setup the arguments for ximg command
    // ximg {cine:<cine_number>, start:<first_frame>, cnt:<frame_count>, dest:<mac_address>, from:<image_source>}
    gchar *img_args = g_strdup_printf ("{cine:%d, start:%d, cnt:%d, fmt:%s, dest:%s}", cine, 0, nb_images, ImageFormatString[img_format], mac);
    g_print ("img_args: %s\n", img_args);
    g_free (mac);
    if (img_args == NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to allocate memory for get_ximages command");
        g_propagate_error (error_loc, phantom_error);
        return FALSE;
    }

    res = uca_phantom_communicate_run_command (
        self,
        CMD_GET_XIMAGES,
        &reply,
        &sub_error,
        img_args,
        NULL);

    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to get images:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        g_free (img_args);
        return FALSE;
    }

    g_print ("Reply: %s\n", reply.raw);

    g_free (img_args);
    img_args = NULL;
    g_free (reply.raw);
    reply.raw = NULL;

    // Setup the arguments for get_timestamps command
    if (ts_format != TS_NONE && self->timestamping == TRUE) {
        gchar* ts_args = g_strdup_printf ("{cine:%d, start:%d, cnt:%d, from:%s}", cine, 0, nb_images, "0");
        if (ts_args == NULL) {
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to allocate memory for get_timestamps command");
            g_propagate_error (error_loc, phantom_error);
            return FALSE;
        }
        res = uca_phantom_communicate_run_command (
            self,
            CMD_GET_TIMESTAMPS,
            &reply,
            &sub_error,
            ts_args,
            NULL);
        if (res != TRUE && sub_error != NULL) {
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to get timestamping:\n\t> %s\n", sub_error->message);
            g_propagate_error (error_loc, phantom_error);
            g_clear_error (&sub_error);
            g_free (ts_args);
            return FALSE;
        }
        g_free (ts_args);
        ts_args = NULL;
        g_free (reply.raw);
        reply.raw = NULL;
    }

    // Create internal request
    InternalRequest *request = g_new0 (InternalRequest, 1);
    request->nb_images = nb_images;
    request->img_format = img_format;
    request->end_request = FALSE;

    // Push request to request queue
    g_async_queue_push (self->request_queue, request);

    return TRUE;
}

static void uca_phantom_communicate_free_request (InternalRequest *request) {
    // TODO: check if CaptureSettings needs to be freed
    g_free (request);
}

/**
 * Read images from 1Gb ethernet connection
 * 
 * @param data: pointer to UcaPhantomCommunicate
 * @return
 * 
 * @note This function uses a TCP connection to read images from the camera.
*/
static gpointer uca_phantom_communicate_accept_img (gpointer data) {
    UcaPhantomCommunicate *self = UCA_PHANTOM_COMMUNICATE (data);
    
    g_return_val_if_fail (self->control_state == CONNECTED, NULL);

    GError *sub_error = NULL;
    gsize image_size = 0;
    gsize ts_size = 0;
    gsize image_packet_size = 0;
    gsize ts_packet_size = 0;
    gsize bytes_read = 0;
    gpointer image_buffer = NULL;
    gpointer ts_buffer = NULL;
    gboolean read_all = FALSE;

    g_print ("Start accepting images\n");

    self->input_datastream = g_io_stream_get_input_stream (G_IO_STREAM (self->data_connection));

    InternalRequest *request = NULL;

    g_print ("Start loop in thread\n");

    while (TRUE) {
        bytes_read = 0;
        // Wait for a request to be available
        request = g_async_queue_pop (self->request_queue);

        g_print ("Popped request from queue!\n");

        if (request == NULL) {
            g_print ("Failed to pop request from queue\n");
            return data;
        }
        if (request->end_request == TRUE) {
            g_print ("Received end request\n");
            uca_phantom_communicate_free_request (request);
            break;
        }

        // Calculate the size of the image buffer
        image_size = self->settings.width * self->settings.height * ImageBitDepth[request->img_format];
        image_packet_size = image_size * request->nb_images;

        // print number of images and image size
        g_print ("Number of images: %ld\n", request->nb_images);

        g_print ("Image size: %ld bytes, Image packet size: %ld bytes\n", image_size, image_packet_size);

        // Allocate the image buffer
        image_buffer = g_malloc0 (image_packet_size);
        if (image_buffer == NULL) {
            g_print ("Failed to allocate image buffer\n");
            return data;
        }

        // Read the image data from the data stream
        read_all = g_input_stream_read_all (self->input_datastream, image_buffer, image_packet_size, &bytes_read, NULL, &sub_error);
        if (read_all != TRUE && sub_error != NULL) {
            g_print("Failed to read datastream:\n\t> %s\n", sub_error->message);
            g_clear_error (&sub_error);
            return data;
        }
        else if (bytes_read != image_packet_size) {
            g_print ("Failed to read all bytes of image frame from datastream. Read %ld bytes, expected %ld bytes. Buffer will be padded.\n", bytes_read, image_packet_size);
        }

        g_print ("Read %ld bytes from datastream\n", bytes_read);

        // If self->timestamping is enabled, read the timestamp from the data stream
        if (self->timestamping) {
            g_print ("Reading timestamp\n");
            // Allocate the timestamp buffer
            ts_size = TimestampSize[self->ts_format];
            ts_packet_size = ts_size * request->nb_images;
            ts_buffer = g_malloc0 (ts_packet_size);
            if (ts_buffer == NULL) {
                g_print ("Failed to allocate timestamp buffer\n");
                return data;
            }

            // Read the timestamp from the data stream
            read_all = g_input_stream_read_all (self->input_datastream, ts_buffer, ts_packet_size, &bytes_read, NULL, &sub_error);
            if (read_all != TRUE && sub_error != NULL) {
                g_print("Failed to read datastream:\n\t> %s\n", sub_error->message);
                g_clear_error (&sub_error);
                return data;
            }
            else if (bytes_read != ts_size) {
                g_print ("Failed to read all bytes of timestamp frame from datastream. Read %ld bytes, expected %ld bytes. Buffer will be padded.\n", bytes_read, ts_size);
            }
        }
        
        // Create a new CineData struct
        CineData *cine_data = g_new0 (CineData, 1);
        cine_data->RawImages = NULL;
        cine_data->UnpackedImages = image_buffer;
        cine_data->RawTimestamps = ts_buffer;
        cine_data->settings = g_memdup2 (&self->settings, sizeof (CaptureSettings));
        cine_data->format = request->img_format;
        cine_data->tsformat = self->ts_format;

        // Add the unpacked data to the data array
        g_async_queue_push (self->unpacked_queue, cine_data);
        // Free the request
        uca_phantom_communicate_free_request (request);
    }

    g_print ("Exiting image accept thread\n");
    
    return data;
}

/**
 * @brief Accepts images from the camera and stores them in the unpacked queue
 * 
 * @param data 
 * @return gpointer 
 */
static gpointer uca_phantom_communicate_accept_ximg (gpointer data) {
    UcaPhantomCommunicate *self = UCA_PHANTOM_COMMUNICATE (data);
    g_return_val_if_fail (self->control_state == CONNECTED, NULL);

    GError *sub_error = NULL;
    guint nb_pixels = 0;
    gssize image_size = 0;
    gssize image_packet_size = 0;
    // gssize ts_size = 0;
    // gssize ts_packet_size = 0;
    gssize bytes_read = 0;
    gpointer image_buffer = NULL;
    // gpointer ts_buffer = NULL;
    int read_all = FALSE;
    InternalRequest *request = NULL;

    struct pcap_pkthdr *pkt_header;
    const guint8 *pkt_data;

    g_print ("Start loop in thread\n");

    // use g_socket_receive_from in a loop to receive the data from the socket
    while (TRUE) {
        bytes_read = 0;
        // Wait for a request to be available
        request = g_async_queue_pop (self->request_queue);

        g_print ("Popped request from queue!\n");

        if (request == NULL) {
            g_print ("Failed to pop request from queue\n");
            return NULL;
        }
        if (request->end_request == TRUE) {
            g_print ("Received end request\n");
            uca_phantom_communicate_free_request (request);
            break;
        }

        // print settings.width and settings.height
        g_print ("Width: %d, Height: %d\n", self->settings.width, self->settings.height);

        // Calculate the size of the image buffer
        nb_pixels = self->settings.width * self->settings.height;
        image_size = nb_pixels * ImageBitDepth[request->img_format]; // the packed image size
        image_packet_size = nb_pixels * 2 * request->nb_images; // the unpacked image size

        // print number of images and image size
        g_print ("Number of images: %ld\n", request->nb_images);

        g_print ("Image size: %ld bytes, Image packet size: %ld bytes\n", image_size, image_packet_size);

        // Allocate the image buffer
        image_buffer = g_malloc0 (image_packet_size);
        if (image_buffer == NULL) {
            g_print ("Failed to allocate image buffer\n");
            return data;
        }

        // Read the image data directly from kernel buffer using pcap_next_ex
        while (TRUE) {
            read_all = pcap_next_ex (self->handle, &pkt_header, &pkt_data);
            if (read_all == 0) {
                g_print ("Beeing read from live capture\n");
            }
            else if (read_all == -1) {
                g_print ("Error occurred\n");
            }
            else if (read_all == -2) {
                g_print ("Being read from savefile\n");
            }

            // Copy the data to the image buffer
            memcpy (image_buffer + bytes_read, pkt_data + ETHERNET_HEADER_SIZE, pkt_header->len - ETHERNET_HEADER_SIZE);

            bytes_read += pkt_header->len - ETHERNET_HEADER_SIZE;

            if (bytes_read >= image_size) {
                break;
            }
        } 
        
        if (bytes_read != image_size) {
            g_print ("Failed to read all bytes of image frame from datastream. Read %ld bytes, expected %ld bytes. Buffer will be padded.\n", bytes_read, image_packet_size);
        }
        g_print ("Read %ld bytes from datastream\n", bytes_read);
        
        // Create a new CineData struct
        CineData *cine_data = g_new0 (CineData, 1);
        cine_data->RawImages = image_buffer;
        cine_data->UnpackedImages = NULL;
        cine_data->RawTimestamps = NULL;
        cine_data->settings = g_memdup2 (&self->settings, sizeof(CaptureSettings));
        cine_data->format = request->img_format;
        cine_data->tsformat = self->ts_format;
        cine_data->nb_images = request->nb_images;

        // Add the data to the queue
        g_async_queue_push (self->packed_queue, cine_data);
        // Free the request
        uca_phantom_communicate_free_request (request);
    }

    // g_print ("Exiting image accept thread\n");
    
    return data;
}

/**
 * Free the image data
*/
static void uca_phantom_communicate_free_cine_data (CineData *cine_data) {
    g_free (cine_data->RawImages);
    g_free (cine_data->UnpackedImages);
    g_free (cine_data->RawTimestamps);
    g_free (cine_data->settings);
    g_free (cine_data);
}

/**
 * @brief Unpacks the image data from the packed format to the unpacked format
 * 
 * @param cine_data 
 * @param error_loc 
 * @return gboolean 
 */
gboolean unpack_image_p10 (CineData *cine_data, GError **error_loc) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail (cine_data != NULL, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;

    // Allocate memory for the unpacked image
    guint16 *unpacked_image = g_malloc0 (cine_data->settings->width * cine_data->settings->height * sizeof(guint16));
    if (unpacked_image == NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE, "Failed to allocate memory for unpacked image");
        g_propagate_error (error_loc, phantom_error);
        return FALSE;
    }
    
    __m128i sm0 = _mm_setr_epi8(1, 0, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 6, 5, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80);
    __m128i sm1 = _mm_setr_epi8(0x80, 0x80, 2, 1, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 7, 6, 0x80, 0x80, 0x80, 0x80);
    __m128i sm2 = _mm_setr_epi8(0x80, 0x80, 0x80, 0x80, 3, 2, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 8, 7, 0x80, 0x80);
    __m128i sm3 = _mm_setr_epi8(0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 4, 3, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 9, 8);

    __m128i mask2 = _mm_setr_epi8(0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0, 0, 0, 0, 0, 0);
    __m128i m0 = _mm_setr_epi8 (0b11000000, 0b11111111, 0, 0, 0, 0, 0, 0, 0b11000000, 0b11111111, 0, 0, 0, 0, 0, 0);
    __m128i m1 = _mm_setr_epi8 (0, 0, 0b11110000, 0b00111111, 0, 0, 0, 0, 0, 0, 0b11110000, 0b00111111, 0, 0, 0, 0);
    __m128i m2 = _mm_setr_epi8 (0, 0, 0, 0, 0b11111100, 0b00001111, 0, 0, 0, 0, 0, 0, 0b11111100, 0b00001111, 0, 0);
    __m128i m3 = _mm_setr_epi8 (0, 0, 0, 0, 0, 0, 0b11111111, 0b00000011, 0, 0, 0, 0, 0, 0, 0b11111111, 0b00000011);

    guint input_index = 0;
    guint output_index = 0;
    guint nb_pixels = cine_data->settings->width * cine_data->settings->height;

    if (nb_pixels % 8 != 0) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE, "Image size is not a multiple of 8");
        g_propagate_error (error_loc, phantom_error);
        return FALSE;
    }

    __m128i input, shifted0, shifted1, shifted2, shifted3, result;

    if (cine_data->RawImages == NULL) {
        g_print ("Raw image data is NULL\n");
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE, "Raw image data is NULL");
        g_propagate_error (error_loc, phantom_error);
        return FALSE;
    }

    while (output_index < nb_pixels) {
        // Load 8 pixels, i.e. 80 bits = 10 bytes
        // Since the 8 10bit pixels are stored in a byte array, we need to load 10 bytes
        input = _mm_loadu_si128 ((__m128i*)(cine_data->RawImages + input_index));

        // Mask
        input = _mm_and_si128 (input, mask2);

        // Shift
        shifted0 = _mm_and_si128 (_mm_shuffle_epi8 (input, sm0), m0) >> 6;
        shifted1 = _mm_and_si128 (_mm_shuffle_epi8 (input, sm1), m1) >> 4;
        shifted2 = _mm_and_si128 (_mm_shuffle_epi8 (input, sm2), m2) >> 2;
        shifted3 = _mm_and_si128 (_mm_shuffle_epi8 (input, sm3), m3);

        // Result
        result = _mm_or_si128(_mm_or_si128(shifted0, shifted1), _mm_or_si128(shifted2, shifted3));

        // Store
        _mm_storeu_si128((__m128i*)(unpacked_image + output_index), result);

        output_index += 8;
        input_index += 10;
    }

    if (output_index != nb_pixels) {
        g_warning("Pixel index is not equal to the number of pixels");
    }

    g_print ("pixel index: %d\n", output_index);

    // Update the image data
    cine_data->UnpackedImages = (guint16*)unpacked_image;

    return TRUE;
}

/**
 * @brief Unpacks the image data from the P12L format to a 16bit array
 * 
 * @param cine_data 
 * @param error_loc 
 * @return gboolean 
 */
gboolean unpack_image_p12l (CineData *cine_data, GError **error_loc) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail (cine_data != NULL, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;

    // Allocate memory for the unpacked image
    guint16 *unpacked_image = g_malloc0 (cine_data->settings->width * cine_data->settings->height * sizeof(guint16));
    if (unpacked_image == NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE, "Failed to allocate memory for unpacked image");
        g_propagate_error (error_loc, phantom_error);
        return FALSE;
    }
    
    __m128i sm0 = _mm_setr_epi8(1, 0, 0x80, 0x80, 4, 3, 0x80, 0x80, 7, 6, 0x80, 0x80, 10, 9, 0x80, 0x80);
    __m128i sm1 = _mm_setr_epi8(0x80, 0x80, 2, 1, 0x80, 0x80, 5, 4, 0x80, 0x80, 8, 7, 0x80, 0x80, 11, 10);
    __m128i m0 = _mm_setr_epi8 (0b11110000, 0b11111111, 0, 0, 0b11110000, 0b11111111, 0, 0, 0b11110000, 0b11111111, 0, 0, 0b11110000, 0b11111111, 0, 0);
    __m128i m1 = _mm_setr_epi8 (0, 0, 0b11111111, 0b00001111, 0, 0,  0b11111111, 0b00001111, 0, 0,  0b11111111, 0b00001111, 0, 0,  0b11111111, 0b00001111);
    __m128i mask2 = _mm_setr_epi8 (0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0, 0, 0, 0);

    guint input_index = 0;
    guint output_index = 0;
    guint nb_pixels = cine_data->settings->width * cine_data->settings->height;

    if (nb_pixels % 8 != 0) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE, "Image size is not a multiple of 8");
        g_propagate_error (error_loc, phantom_error);
        return FALSE;
    }

    __m128i input, shifted0, shifted1, shifted2, shifted3, result;

    while (output_index < nb_pixels) {
        // Load 8 pixels, i.e. 80 bits = 10 bytes
        input = _mm_loadu_si128 ((__m128i*)(cine_data->RawImages + input_index));

        // Mask
        input = _mm_and_si128 (input, mask2);

        // Shift
        shifted0 = _mm_and_si128 (_mm_shuffle_epi8 (input, sm0), m0) >> 4;
        shifted1 = _mm_and_si128 (_mm_shuffle_epi8 (input, sm1), m1);

        // Result
        result = _mm_or_si128 (shifted0, shifted1);

        // Store
        _mm_storeu_si128((__m128i*)(unpacked_image + output_index), result);

        output_index += 8;
        input_index += 12;
    }

    if (output_index != nb_pixels) {
        g_warning("Pixel index is not equal to the number of pixels");
    }

    g_print ("pixel index: %d\n", output_index);

    // Update the image data
    cine_data->UnpackedImages = (guint16*)unpacked_image;

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
gpointer uca_phantom_communicate_unpack_ximg (gpointer data) {
    UcaPhantomCommunicate *self = UCA_PHANTOM_COMMUNICATE (data);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;

    // Loop on CineData objects in the queue
    while (TRUE) {
        // Get the next CineData object
        CineData *cine_data = g_async_queue_pop (self->packed_queue);

        g_print ("Unpacking image\n");

        if (cine_data == NULL) {
            g_print ("Unpacking thread is exiting because cine_data NULL\n");
            // Push the CineData object to the queue anyways, to exit the other threads
            g_async_queue_push (self->unpacked_queue, cine_data);
            break;
        }

        if (self->acquisition_state == IDLE && g_async_queue_length (self->packed_queue) == 0){
            g_print ("Unpacking thread is exiting because IDLE and nothing left\n");
            g_async_queue_push (self->unpacked_queue, cine_data);
            break;
        }

        // Unpack the image
        if (cine_data->format == IMG_P10 ) {
            g_print ("Unpack P10\n");
            if (!unpack_image_p10 (cine_data, &sub_error)) {
                g_print ("Unpack P10 failed\n");
                g_propagate_error (&phantom_error, sub_error);
                g_clear_error (&sub_error);
                return phantom_error;
            }
        }
        else if (cine_data->format == IMG_P12L) {
            g_print ("Unpack P12L\n");
            if (!unpack_image_p12l (cine_data, &sub_error)) {
                g_propagate_error (&phantom_error, sub_error);
                g_clear_error (&sub_error);
                return phantom_error;
            }
        }
        else {
            g_print ("Invalid format\n");
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE, "Image format not supported over 10Gb Ethernet.");
            return phantom_error;
        }

        // Push the CineData object to the queue
        g_async_queue_push (self->unpacked_queue, cine_data);

        g_print ("Unpacked image and pushed to unpacked_queue\n");
    }

    g_print ("Unpack thread finished\n");

    return NULL;
}


/**
 * @brief 
 * 
 * @param self 
 * @param error_loc 
 * @return gboolean 
 * 
 * dont hard code the port number
 */
gboolean uca_phantom_communicate_start_readout (UcaPhantomCommunicate *self, GError **error_loc) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail (self->control_state == CONNECTED, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;
    gboolean result = FALSE;
    guint port = 7116;

    if (self->xenabled) {
        result = uca_phantom_communicate_connect_xdatastream(self, port, &sub_error);
        g_print ("Connected to xdatastream\n");
    }
    else {
        result = uca_phantom_communicate_connect_datastream(self, port, &sub_error);
        g_print ("Connected to datastream\n");

    }

    self->acquisition_state = ACQUIRING;

    if (result != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to connect datastream:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }

    // Start new thread to read data
    if (self->xenabled) {
        g_print ("Starting ximg thread\n");
        self->data_receiver = g_thread_new("data_receiver", uca_phantom_communicate_accept_ximg, self);
        // Extra thread to unpack the 10bit image data
        self->data_unpacker = g_thread_new("data_unpacker", uca_phantom_communicate_unpack_ximg, self);
    }
    else {
        g_print ("Starting img thread\n");
        self->data_receiver = g_thread_new("data_receiver", uca_phantom_communicate_accept_img, self);
    }
    return TRUE;
}


/**
 * @brief Grab the next image from a given CinData object
 * 
 * @param self 
 * @param error_loc 
 * @return gboolean 
 */
gboolean uca_phantom_communicate_grab_next_image (UcaPhantomCommunicate *self, CineData *cine_data, guint image_index, gpointer data, GError **error_loc) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);

    GError *phantom_error = NULL;

    gsize image_size = 2 * cine_data->settings->width * cine_data->settings->height;

    g_print ("Grabbing image of size%ld\n", image_size);

    // Get the next image from the CineData object
    if (image_index < cine_data->nb_images) {
        // Copy the image data to the output buffer
        memcpy (data, (cine_data->UnpackedImages+image_index), image_size);
    }
    else {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GRAB_IMAGE, "Image index out of bounds.");
        g_propagate_error (error_loc, phantom_error);
        return FALSE;
    }

    return TRUE;
}

/**
 * @brief Generic grab image function
 * 
 * @param self 
 * @param error_loc 
 * @return gboolean 
 */
gboolean uca_phantom_communicate_grab_image (UcaPhantomCommunicate *self, gpointer data, GError **error_loc) {
    static guint current_image = 0;
    static CineData *cine_data;
    static GError *sub_error = NULL;
    static GError *phantom_error = NULL;

    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);

    cine_data = g_async_queue_pop(self->unpacked_queue);
    if (cine_data == NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_GRAB_IMAGE, "CineData object is NULL.");
        g_propagate_error (error_loc, phantom_error);
        return FALSE;
    }
    else if (cine_data->RawImages == NULL && cine_data->UnpackedImages == NULL) {
        g_print ("CineData object is empty \n");
        return TRUE;
    }
    
    g_print ("Grabbing image %d of %d\n", current_image, cine_data->nb_images);
    
    if (current_image < cine_data->nb_images) {
        // Get the next image from the CineData object
        if (!uca_phantom_communicate_grab_next_image (self, cine_data, current_image, data, &sub_error)) {
            g_propagate_error (&phantom_error, sub_error);
            g_clear_error (&sub_error);
            return FALSE;
        }
        current_image++;
    }

    // free the CineData object
    uca_phantom_communicate_free_cine_data (cine_data);

    return TRUE;
}

gboolean uca_phantom_communicate_stop_readout (UcaPhantomCommunicate *self, GError **error_loc) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail (self->control_state == CONNECTED, FALSE);
    g_return_val_if_fail (self->acquisition_state == ACQUIRING, FALSE);

    // Block until the Request queue to be empty
    GError *sub_error = NULL;
    GError *phantom_error = NULL;

    self->acquisition_state = IDLE;

    // Push end request to the queue to unblock the data_receiver thread
    InternalRequest *request = g_new0 (InternalRequest, 1);
    request->end_request = TRUE;
    request->nb_images = 0;
    request->img_format = 0;
    
    g_async_queue_push (self->request_queue, request);

    // Push null image to the queue to unblock the data_unpacker thread
    CineData *NULL_IMAGE = g_new0 (CineData, 1);
    NULL_IMAGE->UnpackedImages = NULL;
    NULL_IMAGE->RawImages = NULL;
    g_async_queue_push (self->packed_queue, NULL_IMAGE);

    g_thread_join (self->data_receiver);
    g_thread_join (self->data_unpacker);
    
    if (!self->xenabled) {
        gboolean result = uca_phantom_communicate_disconnect_datastream(self, &sub_error);
        if (result != TRUE && sub_error != NULL) {
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to disconnect datastream:\n\t> %s\n", sub_error->message);
            g_propagate_error (error_loc, phantom_error);
            g_clear_error (&sub_error);
            return FALSE;
        }
    }
    // Empty the packed queue if not empty
    while (g_async_queue_length(self->packed_queue) > 0) {
        g_print ("Popping packed image \n");
        CineData *cine_data = g_async_queue_try_pop(self->packed_queue);
        uca_phantom_communicate_free_cine_data(cine_data);
    }
    // Empty the unpacked array
    while (g_async_queue_length(self->unpacked_queue) > 0) {
        g_print ("Popping packed image \n");
        CineData *cine_data = g_async_queue_try_pop(self->unpacked_queue);
        uca_phantom_communicate_free_cine_data(cine_data);
    }

    g_print ("Readout stopped\n");

    return TRUE;
}