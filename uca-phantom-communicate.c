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
#include "unit_variables.h"


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

/*
 * 
 * Private structures
 * 
*/
typedef struct _PhantomRequest {
    Unit variable;
    gchar* raw;
    gsize size, write_size;
} PhantomRequest;

typedef struct _PhantomReply {
    gchar* raw;
    GValue value;
    gsize size;
    gssize read_size;
} PhantomReply;

typedef struct {
    gint x, y;
    guint w, h, threshold, area, speed, mode;
} Trigger;

// Forward declaration of overrideable functions
static void uca_phantom_communicate_set_property (GObject  *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void uca_phantom_communicate_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void uca_phantom_communicate_constructed (GObject *object);
static void uca_phantom_communicate_dispose (GObject *object);
static void uca_phantom_communicate_finalize (GObject *object);

static GParamSpec *uca_phantom_communicate_properties[N_PROPERTIES] = {NULL, };

struct _UcaPhantomCommunicate {
    GObject parent_object;

    gboolean xenabled;
    gchar *ip, *xip, *netcard, *xnetcard;
    guint port;

    guint ipsource;

    GSocketConnection *connection;
    GSocketClient *client;
    GSocketAddress *address;
    // TODO: consider other essential variables 
};

G_DEFINE_FINAL_TYPE (UcaPhantomCommunicate, uca_phantom_communicate, G_TYPE_OBJECT)

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
    instance->ip = NULL;
    instance->xip = NULL;
    instance->netcard = NULL;
    instance->xip = NULL;

    // create a new connection
    instance->client = g_socket_client_new();
    instance->address = NULL;
    instance->connection = NULL;
}

static void uca_phantom_communicate_constructed (GObject *object) {
    // UcaPhantomCommunicate *instance = UCA_PHANTOM_COMMUNICATE (object);
    G_OBJECT_CLASS (uca_phantom_communicate_parent_class)->constructed (object);
}

static void uca_phantom_communicate_dispose (GObject *object) {
    UcaPhantomCommunicate *instance = UCA_PHANTOM_COMMUNICATE (object);

    g_free (instance->ip);
    g_free (instance->xip);
    g_free (instance->netcard);
    g_free (instance->xnetcard);

    G_OBJECT_CLASS (uca_phantom_communicate_parent_class)->dispose (object);
}

static void uca_phantom_communicate_finalize (GObject *object) {
    UcaPhantomCommunicate *instance = UCA_PHANTOM_COMMUNICATE (object);

    if (instance->address != NULL) {
        g_object_unref (instance->address);
    }
    if (instance->connection != NULL) {
        g_object_unref (instance->connection);
    }
    if (instance->client != NULL) {
        g_object_unref (instance->client);
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
uca_phantom_communicate_discover (UcaPhantomCommunicate *self, GError **error) {
    // Note: ~~goto was originally used in this function to do memory clean up~~
    // But actually a switch statement does the work, given an extra flag
    // variable.

    GError *sub_error = NULL;
    GMatchInfo *info = NULL;
    GSocketAddress *remote_socket_addr = NULL;
    GSocketAddress *result = NULL;
    const gchar request[] = "phantom?";
    const gchar pattern[] = "PH16 (\\d+) (\\d+) (\\d+)";
    gint FLAG = 0;

    gchar reply[128] = {0,};

    gchar *bcast_address = (self->xenabled) ? "172.16.255.255" : "100.100.255.255";
    GSocketAddress *bcast_socket_addr = g_inet_socket_address_new_from_string (bcast_address, 7380);

    if (bcast_socket_addr == NULL) {
        g_warning ("Failed to parse broadcasting address: %s\n", sub_error->message);
        g_propagate_error (error, sub_error);
        g_error_free (sub_error);

        FLAG = BCAST;
    }

    GSocket *socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, &sub_error);

    if (socket == NULL) {
        g_warning ("Failed to create broadcasting socket: %s\n", sub_error->message);
        g_propagate_error (error, sub_error);
        g_error_free (sub_error);

        FLAG = SOCKET;
    }

    g_socket_set_broadcast (socket, TRUE);

    gssize wrote = g_socket_send_to (socket, bcast_socket_addr, request, sizeof (request), NULL, &sub_error);
    
    if (wrote < -1) {
        g_warning ("Failed to broadcast request: %s\n", sub_error->message);
        g_propagate_error (error, sub_error);
        g_error_free (sub_error);

        FLAG = SOCKET;
    }

    gssize received = g_socket_receive_from (socket, &remote_socket_addr, reply, sizeof (reply), NULL, &sub_error);
    
    if (received < -1) {
        g_warning ("Failed to receive from broadcast: %s\n", sub_error->message);
        g_propagate_error (error, sub_error);
        g_error_free (sub_error);

        FLAG = RECEIVE;
    }

    g_print ("Phantom UDP discovery reply: `%s'\n", reply);
    GRegex *regex = g_regex_new (pattern, 0, 0, &sub_error);

    if (regex == NULL) {
        g_warning ("Failed to create Regex object: %s\n", sub_error->message);
        g_propagate_error (error, sub_error);
        g_error_free (sub_error);

        FLAG = REGEX;
    }

    gboolean matched = g_regex_match (regex, reply, 0, &info);

    if (!matched) {
        g_print ("Reply '%s' does not match expected pattern.\n", reply);

        FLAG = ALL;
    }

    gchar *port_string = g_match_info_fetch (info, 1);

    if (port_string == NULL) {
        g_warning ("Failed to retrieve the matched regex information.\n");

        FLAG = ALL;
    }

    guint port = atoi (port_string);
    g_free(port_string);

    result = g_inet_socket_address_new (g_inet_socket_address_get_address ((GInetSocketAddress *) remote_socket_addr), port);

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

/*
 * Private communication method. All requests pass by here.
 * TODO: Doc
*/
static gboolean
uca_phantom_communicate (UcaPhantomCommunicate *self, PhantomRequest *request, PhantomReply *reply, GError **error) {
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
    g_return_val_if_fail (request != NULL || reply != NULL, FALSE);

    GError *sub_error = NULL;

    // TODO: check that the streams are succesfully fetched
    GOutputStream * ostream = g_io_stream_get_output_stream (G_IO_STREAM (self->connection));
    GInputStream * istream = g_io_stream_get_input_stream (G_IO_STREAM (self->connection));

    gboolean sucess = g_output_stream_write_all (
        ostream,
        request->raw,
        request->size,
        &request->write_size,
        NULL,
        &sub_error);
    
    if (!sucess) {
        g_warning ("Could not write request: %s\n", sub_error->message);
        g_propagate_error (error, sub_error);
        g_error_free (sub_error);

        // g_output_stream_close (ostream, NULL, NULL);
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
        g_propagate_error (error, sub_error);
        g_error_free (sub_error);

        g_input_stream_close (istream, NULL, NULL);
        return FALSE;
    }
    else if (reply->read_size == 0) {
        g_warning ("Reached EOF on stream.\n");
    }

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
    GError *error = NULL;

    switch (self->ipsource) {
    case USE_ENV:
        // TODO
        break;
    case USE_CLASS:
        self->address = g_inet_socket_address_new_from_string ((self->xenabled) ? self->xip : self->ip, self->port);
        break;
    case USE_DISCOVERY:
        self->address = uca_phantom_communicate_discover(self, &error);
        break;
    
    default:
        break;
    }

    g_message ("Attempting to connect to the phantom...\n");


    /* connect to the host */
    self->connection = g_socket_client_connect (
        self->client,
        G_SOCKET_CONNECTABLE (self->address),
        NULL,
        &error);
    

    /* don't forget to check for errors */
    if (error != NULL) {
        return FALSE;
    }

    g_message ("Connected to Phantom \n");
    // TODO print info on phantom

    return TRUE;
}

/*
 * Get unit variable
 * TODO: Doc
 * Note: All parameter data belongs to the user!
*/
gboolean uca_phantom_get_variable (UcaPhantomCommunicate *self, guint variable_flag, GValue *return_value, GError **error) {
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
    GError *sub_error = NULL;
    gchar pattern[] = "\\s:\\s";

    // Setup the request 
    PhantomRequest request = {
        .variable = variables[variable_flag],
        .raw = NULL,
        .size = 0,
        .write_size = 0
    };
    // Manually build the message to ensure that the string is correclty NULL-ended
    // request.raw = g_strconcat("get ", request.variable.name, "\n", NULL);
    request.size = (strlen (request.variable.name) + strlen ("get \r\n")) * sizeof (request.raw);
    request.raw = g_malloc0 ((request.size) * sizeof (request.raw));
    
    if (request.raw==NULL) {
        g_warning ("Could not allocate and assemble request message. Aborting\n");
        return FALSE;
    }

    g_strlcat(request.raw, "get ", request.size);
    g_strlcat(request.raw, request.variable.name, request.size);
    g_strlcat(request.raw, "\r\n", request.size);

    // Setup the reply
    PhantomReply reply = {
        .size = 512,
        .raw = NULL,
        .value = G_VALUE_INIT,
        .read_size = 0
    };
    reply.raw = g_malloc0 (reply.size * sizeof (reply.raw));

    if (reply.raw == NULL) {
        g_warning ("Could not allocate and assemble reply buffer. Aborting\n");
        return FALSE;
    }

    g_print (" > request: '%s' \n", request.raw);

    // Communicate request to phantom
    gboolean communicated = uca_phantom_communicate (self, &request, &reply, &sub_error);

    if (!communicated) {
        g_warning ("Failed to retrieve Unit variable %s: %s\n", request.variable.name, sub_error->message);
        
        if (sub_error != NULL) {
            g_propagate_error (error, sub_error);
            g_error_free (sub_error);
        }

        g_free (request.raw);
        g_free (reply.raw);
        return FALSE;
    }
    g_free (request.raw);
    request.raw  = NULL;

    g_print (" > reply: '%s' \n", reply.raw);

    // Extract the actual data from the raw reply
    GRegex* regex = g_regex_new (pattern, 0, 0, &sub_error);

    if (regex == NULL ) {
        g_warning ("Failed to create regex object. Aborting...");

        if (sub_error != NULL) {
            g_propagate_error (error, sub_error);
            g_error_free (sub_error);
        }

        g_free (request.raw);
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
        return FALSE;
    }

    g_value_unset (return_value);
    g_value_init (return_value, request.variable.type);

    // Use Gvalue container to store it
    switch (request.variable.type) {
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
    //     g_value_set_string (&reply.value, var);
    //     break;
    // case PHANTOM_TYPE_RES:
    //     g_value_set_string (&reply.value, var);
    //     break;
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