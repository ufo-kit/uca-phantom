#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
//#include <math.h>

#include <gio/gio.h>
#include <gmodule.h>
#include <glib-object.h>
#include <unistd.h>
#include <nmmintrin.h>

#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <arpa/inet.h>
//#include <netinet/if_ether.h>
#include <poll.h>
#include <net/if.h> // This is making trouble
#include <linux/ip.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <netdb.h>
#include <unistd.h>

#include "uca-phantom-communicate.h"
#include "uca-phantom-variables.h"
#include "uca-phantom-commands.h"

/**
 * TODO: 
 * - Add documentation
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
    SOCKET,
    BCAST
};

enum {
    PROP_IP = 1,
    PROP_XIP,
    PROP_NETCARD,
    PROP_XNETCARD,
    PROP_XENABLED,
    PROP_PORT,
    PROP_IPSOURCE,
    N_PROPERTIES
} UcaPhantomProperties;

enum {
    USE_ENV,
    USE_CLASS,
    USE_DISCOVERY,
    N_IP_FLAGS
} IP_FLAGS;

typedef enum {
    DATA_CONNECTED,
    DATA_DISCONNECTED
} DataConnectionState;

typedef enum {
    CONTROL_CONNECTED,
    CONTROL_DISCONNECTED
} ControlConnectionState;
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

const gchar *ImageFormatString[] = {"8", "8R", "P16", "P16R", "P10", "P12"};
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
typedef struct _ImageData {
    CaptureSettings *settings;
    ImageFormat format;
    TimestampFormat tsformat;
    gpointer RawImage;
    gpointer RawTimestamp;
} ImageData;

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
    gchar *ip, *xip, *netcard, *xnetcard;
    guint port;

    guint ipsource;

    enum {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        DISCONNECTING
    } connection_state;

    enum {
        IDLE,
        ARMED,
        ACQUIRING
    } acquisition_state;

    // camera setup variables
    gboolean timestamping;
    TimestampFormat ts_format;
    CaptureSettings settings;

    // Command stream connection variables
    GSocketConnection *control_connection;
    GSocketClient *control_client;
    GSocketAddress *control_address;
    
    // Data stream connection variables
    GSocketListener *listener;
    GSocket *data_socket;
    GSocketConnection *data_connection;
    GInputStream *input_datastream;

    GThread *data_receiver;
    GThread *data_unpacker;
    GAsyncQueue *image_queue;
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
    uca_phantom_communicate_properties[PROP_IP] =
        g_param_spec_string (
            "ip",
            "IP address",
            "IP address of the Phantom camera over normal 1Gb ethernet",
            "100.100.100.1",
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_XIP] =
        g_param_spec_string (
            "xip",
            "10Gb IP address",
            "IP address of the Phantom camera over a 10Gb ethernet",
            "172.16.0.1",
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_NETCARD] =
        g_param_spec_string (
            "netcard",
            "Network card",
            "Name of the network card used for 1Gb ethernet",
            "100.100.100.1",
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    uca_phantom_communicate_properties[PROP_XNETCARD] =
        g_param_spec_string (
            "xnetcard",
            "10Gb network card",
            "Name of the network card used for 10Gb ethernet",
            "172.16.0.1",
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

    uca_phantom_communicate_properties[PROP_XENABLED] =
        g_param_spec_boolean (
            "xenabled",
            "Enable 10Gb data transfer",
            "Enable 10Gb data transfer",
            TRUE,
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

    uca_phantom_communicate_properties[PROP_PORT] =
        g_param_spec_uint (
            "port",
            "Set connection port",
            "Set the port used to establish TCP connection with phantom",
            1024, 49151, 7115,
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    
    uca_phantom_communicate_properties[PROP_IPSOURCE] =
        g_param_spec_uint (
            "ipsource",
            "Set the IP source using IP flags",
            "Possible flags: USE_ENV, USE_CLASS, USE_DISCOVER.",
            0, N_IP_FLAGS, N_IP_FLAGS - 1,
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
            
    g_object_class_install_properties (
        gobject_class, 
        N_PROPERTIES, 
        uca_phantom_communicate_properties);
}

static void uca_phantom_communicate_init (UcaPhantomCommunicate *instance) {
    g_print ("Initializing UcaPhantomCommunicate\n");

    instance->ip = NULL;
    instance->xip = NULL;
    instance->netcard = NULL;
    instance->xip = NULL;

    instance->connection_state = DISCONNECTED;
    instance->acquisition_state = IDLE;

    // create a new control connection
    instance->control_client = g_socket_client_new();
    instance->control_address = NULL;
    instance->control_connection = NULL;

    // create a new data connection
    instance->listener = g_socket_listener_new();
    instance->data_connection = NULL;
    
    instance->input_datastream = NULL;

    instance->data_receiver = NULL;
    instance->data_unpacker = NULL;
    instance->image_queue = g_async_queue_new();
    instance->request_queue = g_async_queue_new();

    // Camera setup variables
    instance->timestamping = FALSE;
    instance->ts_format = TS_NONE;
    instance->settings = (CaptureSettings) {0, };
}

static void uca_phantom_communicate_constructed (GObject *object) {
    // UcaPhantomCommunicate *instance = UCA_PHANTOM_COMMUNICATE (object);
    G_OBJECT_CLASS (uca_phantom_communicate_parent_class)->constructed (object);
}

static void uca_phantom_communicate_dispose (GObject *object) {
    UcaPhantomCommunicate *instance = UCA_PHANTOM_COMMUNICATE (object);

    g_print ("Disposing UcaPhantomCommunicate\n");

    g_free (instance->ip);
    g_free (instance->xip);
    g_free (instance->netcard);
    g_free (instance->xnetcard);

    G_OBJECT_CLASS (uca_phantom_communicate_parent_class)->dispose (object);
}

static void uca_phantom_communicate_finalize (GObject *object) {
    UcaPhantomCommunicate *instance = UCA_PHANTOM_COMMUNICATE (object);

    g_print ("Finalizing UcaPhantomCommunicate\n");

    if (instance->control_connection != NULL) {
        g_object_unref (instance->control_connection);
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
    // if (instance->input_datastream != NULL) {
    //     g_object_unref (instance->input_datastream);
    // }
    if (instance->listener != NULL) {
        g_socket_listener_close (instance->listener);
        g_object_unref (instance->listener);
    }

    if (instance->data_unpacker != NULL) {
        g_thread_unref (instance->data_unpacker);
    }

    if (instance->image_queue != NULL) {
        g_async_queue_unref (instance->image_queue);
    }
    if (instance->request_queue != NULL) {
        g_async_queue_unref (instance->request_queue);
    }   

    G_OBJECT_CLASS (uca_phantom_communicate_parent_class)->finalize (object);
}

/*
 * Private class definitions
*/
static void uca_phantom_communicate_set_ip (UcaPhantomCommunicate *self, const gchar *property) {
    g_free (self->ip);
    self->ip = g_strdup (property);
}
static void uca_phantom_communicate_set_xip (UcaPhantomCommunicate *self, const gchar *property) {
    g_free (self->xip);
    self->xip = g_strdup (property);
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
static void uca_phantom_communicate_set_port (UcaPhantomCommunicate *self, guint property) {self->port = property;}
static void uca_phantom_communicate_set_ip_source (UcaPhantomCommunicate *self, guint property) {self->ipsource = property;}

static gchar *uca_phantom_communicate_get_ip (UcaPhantomCommunicate *self) {return self->ip;}
static gchar *uca_phantom_communicate_get_xip (UcaPhantomCommunicate *self) {return self->xip;}
static gchar *uca_phantom_communicate_get_netcard (UcaPhantomCommunicate *self) {return self->netcard;}
static gchar *uca_phantom_communicate_get_xnetcard (UcaPhantomCommunicate *self) {return self->xnetcard;}
static gboolean uca_phantom_communicate_get_xenabled (UcaPhantomCommunicate *self) {return self->xenabled;}
static guint uca_phantom_communicate_get_port (UcaPhantomCommunicate *self) {return self->port;}
static guint uca_phantom_communicate_get_ip_source (UcaPhantomCommunicate *self) {return self->ipsource;}

static void uca_phantom_communicate_set_property (
    GObject      *object,
    guint         property_id,
    const GValue *value,
    GParamSpec   *pspec) {
    UcaPhantomCommunicate *self = UCA_PHANTOM_COMMUNICATE (object);

    switch (property_id) {
    case PROP_IP:
        uca_phantom_communicate_set_ip (self, g_value_get_string (value));
        break;
    case PROP_XIP:
        uca_phantom_communicate_set_xip (self, g_value_get_string (value));
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
    case PROP_PORT:
        uca_phantom_communicate_set_port (self, g_value_get_uint (value));
        break;
    case PROP_IPSOURCE:
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
    case PROP_IP:
        g_value_set_string (value, uca_phantom_communicate_get_ip (self));
        break;
    case PROP_XIP:
        g_value_set_string (value, uca_phantom_communicate_get_xip (self));
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
    case PROP_PORT:
        g_value_set_uint (value, uca_phantom_communicate_get_port (self));
        break;
    case PROP_IPSOURCE:
        g_value_set_uint (value, uca_phantom_communicate_get_ip_source (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GSocketAddress *
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
    guint port = 7380;

    gchar reply[128] = {0,};

    g_message ("Attempting to discover the phantom...\n");

    const gchar *bcast_address = (self->xenabled) ? "172.16.255.255" : "100.100.255.255";
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
        FLAG = SOCKET;
        goto cleanup;
    }

    g_socket_set_broadcast (socket, TRUE);

    gssize wrote = g_socket_send_to (socket, bcast_socket_addr, request, sizeof (request), NULL, &sub_error);
    
    if (wrote < -1) {
        if (sub_error == NULL) {
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SEND, "Failed to send broadcast\n");
        }
        else {
            g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_SEND, "Failed to send broadcast: %s\n", sub_error->message);
            g_error_free (sub_error);
        }
        g_propagate_error (error_loc, phantom_error);

        FLAG = SOCKET;
        goto cleanup;
    }

    gssize received = g_socket_receive_from (socket, &remote_socket_addr, reply, sizeof (reply), NULL, &sub_error);
    
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

    port = atoi (port_string);
    g_free(port_string);

    result = g_inet_socket_address_new (g_inet_socket_address_get_address ((GInetSocketAddress *) remote_socket_addr), port);

    gchar *ip_adress = g_inet_address_to_string (g_inet_socket_address_get_address ((GInetSocketAddress *) result));

    g_message ("Phantom found on port %d with the IPV4 address: %s.\n", port, ip_adress);

    g_free (ip_adress);

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
        case SOCKET:
            g_object_unref (socket);
            /* fall through */
        case BCAST:
            g_object_unref (bcast_socket_addr);
            break;
        default:
            g_warning ("Flag set to invalid value! Fatal error.");
            return NULL;
    }

    return result;
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

    switch (self->ipsource) {
    case USE_ENV:
        // TODO
        break;
    case USE_CLASS:
        self->control_address = g_inet_socket_address_new_from_string ((self->xenabled) ? self->xip : self->ip, self->port);
        break;
    case USE_DISCOVERY:
        self->control_address = uca_phantom_communicate_discover(self, &sub_error);
        break;
    
    default:
        g_warning ("Invalid ipsource value. Fatal error.");
        return FALSE;
        break;
    }

    if (self->control_address == NULL && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_ADRESS, "Could not discover the phantom: %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_error_free (sub_error);

        return FALSE;
    }
    if (self->control_address != NULL && sub_error != NULL) {
        g_warning ("Error when getting the adress of the phantom: %s\n", sub_error->message);
        g_clear_error (&sub_error);
    }

    g_message ("Attempting to connect to the phantom...\n");

    /* connect to the phantom */
    self->control_connection = g_socket_client_connect (
        self->control_client,
        G_SOCKET_CONNECTABLE (self->control_address),
        NULL,
        &sub_error);
    
    if (self->control_connection == NULL && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT, "Could not connect to the phantom: %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_error_free (sub_error);

        return FALSE;
    }
    if (self->control_connection != NULL && sub_error != NULL) {
        g_warning ("Error occured when trying to connect to phantom: %s\n", sub_error->message);
        g_clear_error (&sub_error);
    }

    self->connection_state = CONNECTED;

    // TODO: get the basic properties of the phantom user CaptureSettings
    gboolean res = uca_phantom_communicate_get_capture_settings (self, &(self->settings), &sub_error);
    if (res == FALSE && sub_error != NULL) {
        g_warning ("Could not get capture settings of the phantom: %s\n", sub_error->message);
        g_clear_error (&sub_error);
    }

    g_message ("Connected to Phantom \n");
    // TODO print info on phantom
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
 */
gboolean uca_phantom_communicate_run_command (UcaPhantomCommunicate *self, guint command_flag, PhantomReply *reply, GError **error_loc, ...) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail (command_flag < N_UNIT_COMMANDS, FALSE);
    g_return_val_if_fail (self->connection_state == CONNECTED, FALSE);

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
    g_return_val_if_fail (self->connection_state == CONNECTED, FALSE);

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
    g_return_val_if_fail(self->connection_state == CONNECTED, FALSE);

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
    g_return_val_if_fail (self->connection_state == CONNECTED, FALSE);

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
    g_return_val_if_fail (self->connection_state == CONNECTED, FALSE);

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
    g_return_if_fail (self->connection_state == CONNECTED);

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
    g_return_val_if_fail(self->connection_state == CONNECTED, FALSE);

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

    gboolean setup_port = g_socket_listener_add_inet_port (self->listener, port, NULL, &sub_error);

    if (setup_port != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_DATASTREAM, "Failed to setup port %d:\n\t> %s\n", port, sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }

    PhantomReply reply;

    // 7116
    gchar *arg = g_strdup_printf ("{port:%d}", port);

    gboolean res = uca_phantom_communicate_run_command (self, CMD_START_DATA_CONNECTION, &reply, &sub_error, arg, NULL);

    if (res != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_DATASTREAM, "Failed to connect to datastream on port %d:\n\t> %s\n", port, sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }

    g_free (arg);
    arg = NULL;
    g_free (reply.raw);
    reply.raw  = NULL;

    // Accept the connection
    g_print ("Waiting for connection on port %d...\n", port);
    self->data_connection = g_socket_listener_accept(self->listener, NULL, NULL, &sub_error);
    if (sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_DATASTREAM, "Failed to connect to datastream on port %d:\n\t> %s\n", port, sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }
    g_print ("Connection accepted");

    // Print the address of the connected socket
    GSocketAddress *address = g_socket_connection_get_remote_address (self->data_connection, NULL);
    GInetAddress *inet_address = g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (address));
    gchar *address_string = g_inet_address_to_string (inet_address);
    g_print (" on address %s \n", address_string);

    //free everything
    g_free (address_string);
    address_string = NULL;
    g_object_unref (address);
    address = NULL;
       
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
    g_return_val_if_fail(self->connection_state == CONNECTED, FALSE);

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
    g_return_val_if_fail(self->connection_state == CONNECTED, FALSE);

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
    g_return_val_if_fail (self->connection_state == CONNECTED, FALSE);

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

static void uca_phantom_communicate_free_request (InternalRequest *request) {
    // TODO: check if CaptureSettings needs to be freed
    g_free (request);
}

static gpointer uca_phantom_communicate_accept_img (gpointer data) {
    UcaPhantomCommunicate *self = UCA_PHANTOM_COMMUNICATE (data);
    
    g_return_val_if_fail (self->connection_state == CONNECTED, NULL);

    GError *sub_error = NULL;
    gsize image_size = 0;
    gsize ts_size = 0;
    gsize image_packet_size = 0;
    gsize ts_packet_size = 0;
    gsize bytes_read = 0;
    gpointer image_buffer = NULL;
    gpointer ts_buffer = NULL;
    gboolean read_all = FALSE;

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
        
        // Create a new ImageData struct
        ImageData *image_data = g_new0 (ImageData, 1);
        image_data->RawImage = image_buffer;
        image_data->RawTimestamp = ts_buffer;
        image_data->settings = g_memdup (&self->settings, sizeof(CaptureSettings));
        image_data->format = request->img_format;
        image_data->tsformat = self->ts_format;

        // Add the data to the queue
        g_async_queue_push (self->image_queue, image_data);
        // Free the request
        uca_phantom_communicate_free_request (request);
    }

    g_print ("Exiting image accept thread\n");
    
    return data;
}

/**
 * Free the image data
*/
static void uca_phantom_communicate_free_image_data (ImageData *image_data) {
    g_free (image_data->RawImage);
    g_free (image_data->RawTimestamp);
    g_free (image_data->settings);
    g_free (image_data);
}

//  

gboolean uca_phantom_communicate_start_readout (UcaPhantomCommunicate *self, GError **error_loc) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail (self->connection_state == CONNECTED, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;

    gboolean result = uca_phantom_communicate_connect_datastream(self, 7116, &sub_error);

    self->acquisition_state = ACQUIRING;

    if (result != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to connect datastream:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }

    // Start new thread to read data
    self->data_receiver = g_thread_new("data_receiver", uca_phantom_communicate_accept_img, self);

    return TRUE;
}

static gboolean save_image_to_file (ImageData *image_data, const gchar* filename, GError **error_loc) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail (image_data != NULL, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;

    // Create a new file
    FILE *fp = fopen(filename, "wb");
    if (fp == NULL) {
        g_error("Failed to open file for writing: %s", filename);
    }
    fwrite(image_data->RawImage, sizeof(guint8), image_data->settings->width * image_data->settings->height, fp);
    fclose(fp);

    return TRUE;
}

gboolean uca_phantom_communicate_stop_readout (UcaPhantomCommunicate *self, GError **error_loc) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail (self->connection_state == CONNECTED, FALSE);
    g_return_val_if_fail (self->acquisition_state == ACQUIRING, FALSE);

    // Block until the Request queue to be empty
    GError *sub_error = NULL;
    GError *phantom_error = NULL;

    self->acquisition_state = IDLE;
    // Push end request to the queue to unblock the thread
    InternalRequest *request = g_new0 (InternalRequest, 1);
    request->end_request = TRUE;
    request->nb_images = 0;
    request->img_format = 0;
    
    g_async_queue_push (self->request_queue, request);

    g_thread_join (self->data_receiver);
    
    gboolean result = uca_phantom_communicate_disconnect_datastream(self, &sub_error);
    if (result != TRUE && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING, "Failed to disconnect datastream:\n\t> %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_clear_error (&sub_error);
        return FALSE;
    }
    int i = 0;
    // Empty the data queue
    while (g_async_queue_length(self->image_queue) > 0) {
        ImageData *image_data = g_async_queue_try_pop(self->image_queue);
        // file name change 
        gchar *filename = g_strdup_printf("test_%d.dat", i);
        save_image_to_file(image_data, filename, &sub_error);
        uca_phantom_communicate_free_image_data(image_data);
        i++;
    }

    g_print ("Readout stopped\n");

    return TRUE;
}