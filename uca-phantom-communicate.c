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
    PhantomCommand command;
    gchar* message;
    gsize size;
    gssize write_size;
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
uca_phantom_communicate_discover (UcaPhantomCommunicate *self, GError **error_loc) {
    // Note: find a way to do this without a goto statement.
    // But actually a switch statement does the work, given an extra flag
    // variable.

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

    gchar *bcast_address = (self->xenabled) ? "172.16.255.255" : "100.100.255.255";
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
    GOutputStream * ostream = g_io_stream_get_output_stream (G_IO_STREAM (self->connection));
    GInputStream * istream = g_io_stream_get_input_stream (G_IO_STREAM (self->connection));

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
        self->address = g_inet_socket_address_new_from_string ((self->xenabled) ? self->xip : self->ip, self->port);
        break;
    case USE_DISCOVERY:
        self->address = uca_phantom_communicate_discover(self, &sub_error);
        break;
    
    default:
        g_warning ("Invalid ipsource value. Fatal error.");
        return FALSE;
        break;
    }

    if (self->address == NULL && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_ADRESS, "Could not discover the phantom: %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_error_free (sub_error);

        return FALSE;
    }
    if (self->address != NULL && sub_error != NULL) {
        g_warning ("Error when getting the adress of the phantom: %s\n", sub_error->message);
        g_clear_error (&sub_error);
    }

    g_message ("Attempting to connect to the phantom...\n");

    /* connect to the phantom */
    self->connection = g_socket_client_connect (
        self->client,
        G_SOCKET_CONNECTABLE (self->address),
        NULL,
        &sub_error);
    
    if (self->connection == NULL && sub_error != NULL) {
        g_set_error (&phantom_error, UCA_PHANTOM_COMMUNICATE_ERROR, UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT, "Could not connect to the phantom: %s\n", sub_error->message);
        g_propagate_error (error_loc, phantom_error);
        g_error_free (sub_error);

        return FALSE;
    }
    if (self->connection != NULL && sub_error != NULL) {
        g_warning ("Error occured when trying to connect to phantom: %s\n", sub_error->message);
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
gboolean uca_phantom_run_command (UcaPhantomCommunicate *self, guint command_flag, PhantomReply *reply, GError **error_loc, ...) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail (command_flag < N_UNIT_COMMANDS, FALSE);

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
    g_strlcat(request.message, " ", request.size);
    for (guint i = 0; i < nb_args-1; i++) {
        g_strlcat(request.message, args[i], request.size);
        g_strlcat(request.message, " ", request.size);
    }
    g_strlcat(request.message, args[nb_args-1], request.size); // Last arg does not have a space after it
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
 * @paragraph This function will get the value of a unit variable from the phantom using the uca_phantom_run_command function.
 * 
 * @param self 
 * @param variable_flag 
 * @param return_value 
 * @param error_loc 
 * @return gboolean 
 */
gboolean uca_phantom_get_variable(UcaPhantomCommunicate *self, guint variable_flag, GValue *return_value, GError **error_loc) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail (variable_flag < N_UNIT_PROPERTIES, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;
    gchar pattern[] = "\\s:\\s";
    PhantomReply reply;

    gboolean res = uca_phantom_run_command (self, CMD_GET, &reply, &sub_error, variables[variable_flag].name, NULL);

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

// Make set variable function using the run command function
gboolean uca_phantom_set_variable(UcaPhantomCommunicate *self, guint variable_flag, const gchar *value, GError **error_loc) {
    g_return_val_if_fail (error_loc == NULL || *error_loc == NULL, FALSE);
    g_return_val_if_fail (variable_flag < N_UNIT_PROPERTIES, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);
    g_return_val_if_fail (variables[variable_flag].flags & G_PARAM_WRITABLE, FALSE);

    GError *sub_error = NULL;
    GError *phantom_error = NULL;
    PhantomReply reply;

    gboolean res = uca_phantom_run_command (self, CMD_SET, &reply, &sub_error, variables[variable_flag].name, value, NULL);

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