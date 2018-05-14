/* Copyright (C) 2018 Matthias Vogelgesang <matthias.vogelgesang@kit.edu>
   (Karlsruhe Institute of Technology)

   This library is free software; you can redistribute it and/or modify it
   under the terms of the GNU Lesser General Public License as published by the
   Free Software Foundation; either version 2.1 of the License, or (at your
   option) any later version.

   This library is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
   details.

   You should have received a copy of the GNU Lesser General Public License along
   with this library; if not, write to the Free Software Foundation, Inc., 51
   Franklin St, Fifth Floor, Boston, MA 02110, USA */

#include <stdlib.h>
#include <gio/gio.h>
#include <gmodule.h>
#include <string.h>

#include <uca/uca-camera.h>
#include "uca-phantom-camera.h"
#include "config.h"


#define UCA_PHANTOM_CAMERA_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UCA_TYPE_PHANTOM_CAMERA, UcaPhantomCameraPrivate))

static void uca_phantom_camera_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (UcaPhantomCamera, uca_phantom_camera, UCA_TYPE_CAMERA,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                uca_phantom_camera_initable_iface_init))

GQuark uca_phantom_camera_error_quark ()
{
    return g_quark_from_static_string("uca-net-camera-error-quark");
}

enum {
    /* info structure */
    PROP_SENSOR_TYPE = N_BASE_PROPERTIES,
    PROP_SENSOR_VERSION,
    PROP_HARDWARE_VERSION,
    PROP_KERNEL_VERSION,
    PROP_FIRMWARE_VERSION,
    PROP_FPGA_VERSION,
    N_PROPERTIES
};

static gint base_overrideables[] = {
    PROP_NAME,
    PROP_SENSOR_WIDTH,
    PROP_SENSOR_HEIGHT,
    PROP_SENSOR_BITDEPTH,
    PROP_ROI_X,
    PROP_ROI_Y,
    PROP_ROI_WIDTH,
    PROP_ROI_HEIGHT,
    PROP_EXPOSURE_TIME,
    PROP_HAS_STREAMING,
    PROP_HAS_CAMRAM_RECORDING,
    0
};

static GParamSpec *phantom_properties[N_PROPERTIES] = { NULL, };

struct _UcaPhantomCameraPrivate {
    GError              *construct_error;
    gchar               *host;
    GSocketClient       *client;
    GSocketConnection   *connection;
    GSocketListener     *listener;
    GCancellable        *accept;
    GThread             *accept_thread;
    GAsyncQueue         *message_queue;
    GAsyncQueue         *result_queue;
};

typedef struct  {
    const gchar *name;
    GType        type;
    GParamFlags  flags;
    gint         property_id;
    gboolean     handle_automatically;
} UnitVariable;

static UnitVariable variables[] = {
    { "info.sensor",     G_TYPE_UINT, G_PARAM_READABLE,  PROP_SENSOR_TYPE,      TRUE },
    { "info.snsversion", G_TYPE_UINT, G_PARAM_READABLE,  PROP_SENSOR_VERSION,   TRUE },
    { "info.hwver",      G_TYPE_UINT, G_PARAM_READABLE,  PROP_HARDWARE_VERSION, TRUE },
    { "info.kernel",     G_TYPE_UINT, G_PARAM_READABLE,  PROP_KERNEL_VERSION,   TRUE },
    { "info.swver",      G_TYPE_UINT, G_PARAM_READABLE,  PROP_FIRMWARE_VERSION, TRUE },
    { "info.xver",       G_TYPE_UINT, G_PARAM_READABLE,  PROP_FPGA_VERSION,     TRUE },
    { "info.name",       G_TYPE_UINT, G_PARAM_READABLE,  PROP_NAME,             TRUE },
    { "info.xmax",       G_TYPE_UINT, G_PARAM_READABLE,  PROP_SENSOR_WIDTH,     TRUE },
    { "info.ymax",       G_TYPE_UINT, G_PARAM_READABLE,  PROP_SENSOR_HEIGHT,    TRUE },
    { "video.paox",      G_TYPE_INT,  G_PARAM_READWRITE, PROP_ROI_X,            TRUE },
    { "video.paoy",      G_TYPE_INT,  G_PARAM_READWRITE, PROP_ROI_Y,            TRUE },
    { "defc.exp",        G_TYPE_UINT, G_PARAM_READWRITE, PROP_EXPOSURE_TIME,    FALSE },
    /* { "video.pax",          G_TYPE_UINT, G_PARAM_READWRITE, PROP_ROI_WIDTH }, */
    /* { "video.pay",          G_TYPE_UINT, G_PARAM_READWRITE, PROP_ROI_HEIGHT }, */
    { NULL, }
};

typedef struct {
    enum {
        MESSAGE_READ_IMAGE = 1,
        MESSAGE_READ_TIMESTAMP,
        MESSAGE_STOP,
    } type;
    gpointer  data;
    GError  **error;
} InternalMessage;

typedef enum {
    RESULT_SUCCESS = 1,
    RESULT_FAILURE,
} Result;

#define DEFINE_CAST(suffix, trans_func)                 \
static void                                             \
value_transform_##suffix (const GValue *src_value,      \
                         GValue       *dest_value)      \
{                                                       \
  const gchar* src = g_value_get_string (src_value);    \
  g_value_set_##suffix (dest_value, trans_func (src));  \
}

DEFINE_CAST (uchar,     atoi)
DEFINE_CAST (int,       atoi)
DEFINE_CAST (long,      atol)
DEFINE_CAST (uint,      atoi)
DEFINE_CAST (uint64,    atoi)
DEFINE_CAST (ulong,     atol)
DEFINE_CAST (float,     atof)
DEFINE_CAST (double,    atof)

static UnitVariable *
phantom_lookup_by_id (gint property_id)
{
    for (guint i = 0; variables[i].name != NULL; i++) {
        if (variables[i].property_id == property_id)
            return &variables[i];
    }

    return NULL;
}

static gchar *
phantom_talk (UcaPhantomCameraPrivate *priv,
              const gchar *request,
              gchar *reply_loc,
              gsize reply_loc_size,
              GError **error_loc)
{
    GOutputStream *ostream;
    GInputStream *istream;
    gsize size;
    gsize reply_size;
    GError *error = NULL;
    gchar *reply = NULL;

    ostream = g_io_stream_get_output_stream ((GIOStream *) priv->connection);
    istream = g_io_stream_get_input_stream ((GIOStream *) priv->connection);

    if (!g_output_stream_write_all (ostream, request, strlen (request), &size, NULL, &error)) {
        if (error_loc == NULL) {
            g_warning ("Could not write request: %s\n", error->message);
            g_error_free (error);
        }
        else {
            if (error != NULL)
                g_propagate_error (error_loc, error);
        }

        return NULL;
    }

    reply_size = reply_loc ? reply_loc_size : 512;
    reply = reply_loc ? reply_loc : g_malloc0 (reply_size);

    if (!g_input_stream_read (istream, reply, reply_size, NULL, &error)) {
        if (error_loc == NULL) {
            g_warning ("Could not read reply: %s\n", error->message);
            g_error_free (error);
        }
        else  {
            if (error != NULL)
                g_propagate_error (error_loc, error);
        }

        g_free (reply);
        return NULL;
    }

    if (g_str_has_prefix (reply, "ERR: ")) {
        if (error_loc != NULL) {
            g_set_error (error_loc, G_IO_ERROR, G_IO_ERROR_FAILED,
                         "Phantom error: %s", reply + 5);
        }
        else
            g_warning ("Error: %s", reply + 5);
    }

    return reply;
}

static gchar *
phantom_get_string (UcaPhantomCameraPrivate *priv, UnitVariable *var)
{
    gchar *request;
    gchar *cr = NULL;
    gchar *reply = NULL;

    request = g_strdup_printf ("get %s\r\n", var->name);
    reply = phantom_talk (priv, request, NULL, 0, NULL);

    if (reply == NULL)
        goto phantom_get_string_error;

    /* strip \r\n and properly zero-limit the string */
    cr = strchr (reply, '\r');

    if (cr != NULL)
        *cr = '\0';

phantom_get_string_error:
    g_free (request);
    return reply;
}

static void
phantom_get (UcaPhantomCameraPrivate *priv, UnitVariable *var, GValue *value)
{
    gchar *reply;
    GValue reply_value = {0,};

    reply = phantom_get_string (priv, var);

    if (reply == NULL)
        return;

    g_value_init (&reply_value, G_TYPE_STRING);
    g_value_set_string (&reply_value, reply);

    if (!g_value_transform (&reply_value, value))
        g_warning ("Could not transform `%s' to target value type %s", reply, G_VALUE_TYPE_NAME (value));

    g_free (reply);
    g_value_unset (&reply_value);
}

static void
phantom_set_string (UcaPhantomCameraPrivate *priv, UnitVariable *var, const gchar *value)
{
    gchar *request;
    gchar reply[256];

    request = g_strdup_printf ("set %s %s\r\n", var->name, value);
    phantom_talk (priv, request, reply, sizeof (reply), NULL);

    g_free (request);
}

static void
phantom_set (UcaPhantomCameraPrivate *priv, UnitVariable *var, const GValue *value)
{
    GValue request_value = {0,};

    if (!(var->flags & G_PARAM_WRITABLE))
        return;

    g_value_init (&request_value, G_TYPE_STRING);
    g_value_transform (value, &request_value);
    phantom_set_string (priv, var, g_value_get_string (&request_value));
    g_value_unset (&request_value);
}

static void
uca_phantom_camera_start_recording (UcaCamera *camera,
                                    GError **error)
{
    /* send command */
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));
}

static void
uca_phantom_camera_stop_recording (UcaCamera *camera,
                                   GError **error)
{
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));
}

static gpointer
accept_data (UcaPhantomCameraPrivate *priv)
{
    GSocketConnection *connection;
    GSocketAddress *remote_addr;
    GInetAddress *inet_addr;
    gchar *addr;
    gboolean stop = FALSE;
    GError *error = NULL;

    g_debug ("Accepting data connection ...");
    connection = g_socket_listener_accept (priv->listener, NULL, priv->accept, &error);

    if (g_cancellable_is_cancelled (priv->accept)) {
        g_print ("Listen cancelled\n");
        g_error_free (error);
        return NULL;
    }

    if (error != NULL) {
        g_print ("Error: %s\n", error->message);
        g_error_free (error);
    }

    remote_addr = g_socket_connection_get_remote_address (connection, NULL);
    inet_addr = g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (remote_addr));
    addr = g_inet_address_to_string (inet_addr);
    g_debug ("%s connected", addr);
    g_object_unref (remote_addr);
    g_free (addr);

    while (!stop) {
        InternalMessage *message;
        GInputStream *istream;
        gchar request[256];
        Result result = RESULT_SUCCESS;

        istream = g_io_stream_get_input_stream (G_IO_STREAM (connection));
        message = g_async_queue_pop (priv->message_queue);

        switch (message->type) {
            case MESSAGE_READ_IMAGE:
                g_input_stream_read (istream, request, sizeof (request), NULL, message->error);
                break;
            case MESSAGE_READ_TIMESTAMP:
                break;
            case MESSAGE_STOP:
                stop = TRUE;
                break;
        }

        g_free (message);
        g_async_queue_push (priv->result_queue, GINT_TO_POINTER (result));
    }

    if (!g_io_stream_close (G_IO_STREAM (connection), NULL, &error)) {
        g_warning ("Could not close connection: %s\n", error->message);
        g_error_free (error);
    }

    return NULL;
}

static void
uca_phantom_camera_start_readout (UcaCamera *camera,
                                  GError **error)
{
    UcaPhantomCameraPrivate *priv;
    gchar *reply;
    const gchar *request = "startdata {port:7116}";

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    /* set up listener */
    g_socket_listener_add_inet_port (priv->listener, 7116, G_OBJECT (camera), error);
    priv->accept = g_cancellable_new ();
    priv->accept_thread = g_thread_new (NULL, (GThreadFunc) accept_data, priv);

    /* send startdata command */
    /* FIXME: there might be a race condition with g_socket_listener_accept */
    reply = phantom_talk (priv, request, NULL, 0, error);
    g_free (reply);
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));
}

static void
uca_phantom_camera_stop_readout (UcaCamera *camera,
                                 GError **error)
{
    UcaPhantomCameraPrivate *priv;
    InternalMessage *message;

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    /* stop accept thread */
    message = g_new0 (InternalMessage, 1);
    message->type = MESSAGE_STOP;
    g_async_queue_push (priv->message_queue, message);

    /* stop listener */
    g_cancellable_cancel (priv->accept);
    g_socket_listener_close (priv->listener);
    g_thread_join (priv->accept_thread);
    g_thread_unref (priv->accept_thread);

    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));
}

static void
uca_phantom_camera_write (UcaCamera *camera,
                          const gchar *name,
                          gpointer data,
                          gsize size,
                          GError **error)
{
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));
}

static gboolean
uca_phantom_camera_grab (UcaCamera *camera,
                         gpointer data,
                         GError **error)
{
    UcaPhantomCameraPrivate *priv;
    InternalMessage *message;
    Result result;
    gchar *reply;
    const gchar *request = "ximg {cine:0, start:0, cnt:1}";

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    message = g_new0 (InternalMessage, 1);
    message->data = data;
    message->type = MESSAGE_READ_IMAGE;
    message->error = error;
    g_async_queue_push (priv->message_queue, message);

    /* send request */
    reply = phantom_talk (priv, request, NULL, 0, error);

    if (reply == NULL)
        return FALSE;

    g_free (reply);

    /* wait for image transfer to finish */
    result = (Result) GPOINTER_TO_INT (g_async_queue_pop (priv->result_queue));

    return result == RESULT_SUCCESS;
}

static void
uca_phantom_camera_trigger (UcaCamera *camera,
                        GError **error)
{
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));
}

static void
uca_phantom_camera_set_property (GObject *object,
                                 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
    UnitVariable *var;

    var = phantom_lookup_by_id (property_id);

    if (var != NULL) {
        phantom_set (UCA_PHANTOM_CAMERA_GET_PRIVATE (object), var, value);
        return;
    }
}

static void
uca_phantom_camera_get_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
    UnitVariable *var;

    var = phantom_lookup_by_id (property_id);

    if (var != NULL && var->handle_automatically) {
        phantom_get (UCA_PHANTOM_CAMERA_GET_PRIVATE (object), var, value);
        return;
    }

    switch (property_id) {
        case PROP_SENSOR_BITDEPTH:
            g_value_set_uint (value, 12);
            break;
        case PROP_ROI_WIDTH:
            g_value_set_uint (value, 2048);
            break;
        case PROP_ROI_HEIGHT:
            g_value_set_uint (value, 1952);
            break;
        case PROP_EXPOSURE_TIME:
            {
                gchar *s;
                gdouble time;
                s = phantom_get_string (UCA_PHANTOM_CAMERA_GET_PRIVATE (object), var);
                time = atoi (s) / 1000.0 / 1000.0 / 1000.0;
                g_value_set_double (value, time);
            }
            break;
        case PROP_HAS_STREAMING:
            g_value_set_boolean (value, FALSE);
            break;
        case PROP_HAS_CAMRAM_RECORDING:
            g_value_set_boolean (value, TRUE);
            break;
    };
}

static void
uca_phantom_camera_dispose (GObject *object)
{
    UcaPhantomCameraPrivate *priv;

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (object);

    if (priv->connection) {
        GOutputStream *ostream;
        gsize size;
        const gchar *request = "bye\r\n";
        GError *error = NULL;

        /* remove bye for real camera */
        ostream = g_io_stream_get_output_stream (G_IO_STREAM (priv->connection));
        g_output_stream_write_all (ostream, request, strlen (request), &size, NULL, NULL);

        if (!g_io_stream_close (G_IO_STREAM (priv->connection), NULL, &error)) {
            g_warning ("Could not close connection: %s\n", error->message);
            g_error_free (error);
        }

        g_object_unref (priv->connection);
        priv->connection = NULL;
    }

    if (priv->accept)
        g_object_unref (priv->accept);

    if (priv->listener)
        g_object_unref (priv->listener);

    g_object_unref (priv->client);
    g_async_queue_unref (priv->message_queue);
    g_async_queue_unref (priv->result_queue);

    G_OBJECT_CLASS (uca_phantom_camera_parent_class)->dispose (object);
}

static void
uca_phantom_camera_finalize (GObject *object)
{
    G_OBJECT_CLASS (uca_phantom_camera_parent_class)->finalize (object);
}

static gboolean
ufo_net_camera_initable_init (GInitable *initable,
                              GCancellable *cancellable,
                              GError **error)
{
    UcaPhantomCamera *camera;
    UcaPhantomCameraPrivate *priv;

    g_return_val_if_fail (UCA_IS_PHANTOM_CAMERA (initable), FALSE);

    if (cancellable != NULL) {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                             "Cancellable initialization not supported");
        return FALSE;
    }

    camera = UCA_PHANTOM_CAMERA (initable);
    priv = camera->priv;

    if (priv->construct_error != NULL) {
        if (error)
            *error = g_error_copy (priv->construct_error);

        return FALSE;
    }

    return TRUE;
}

static GSocketAddress *
phantom_discover (GError **error)
{
    GInetAddress *bcast_addr;
    GSocketAddress *bcast_socket_addr;
    GSocketAddress *remote_socket_addr;
    GSocket *socket;
    GRegex *regex;
    GMatchInfo *info;
    gchar *port_string;
    guint port;
    const gchar request[] = "phantom?";
    gchar reply[128] = {0,};
    GSocketAddress *result = NULL;

    bcast_addr = g_inet_address_new_from_string ("255.255.255.255");
    bcast_socket_addr = g_inet_socket_address_new (bcast_addr, 7380);
    socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, error);

    if (socket == NULL) {
        result = FALSE;
        goto cleanup_discovery_addr;
    }

    g_socket_set_broadcast (socket, TRUE);

    if (g_socket_send_to (socket, bcast_socket_addr, request, sizeof (request), NULL, error) < 0)
        goto cleanup_discovery_socket;

    if (g_socket_receive_from (socket, &remote_socket_addr, reply, sizeof (reply), NULL, error) < 0)
        goto cleanup_discovery_socket;

    g_debug ("Phantom UDP discovery reply: `%s'", reply);
    regex = g_regex_new ("PH16 (\\d+) (\\d+) (\\d+)", 0, 0, error);

    if (!g_regex_match (regex, reply, 0, &info)) {
        g_set_error (error, UCA_CAMERA_ERROR, UCA_CAMERA_ERROR_NOT_FOUND,
                     "`%s' does not match expected reply", reply);
        goto cleanup_discovery_addr;
    }

    port_string = g_match_info_fetch (info, 1);
    port = atoi (port_string);
    g_free (port_string);
    result = g_inet_socket_address_new (g_inet_socket_address_get_address ((GInetSocketAddress *) remote_socket_addr), port);

cleanup_discovery_addr:
    g_regex_unref (regex);
    g_object_unref (bcast_addr);
    g_object_unref (bcast_socket_addr);

cleanup_discovery_socket:
    g_object_unref (socket);

    return result;
}

static void
uca_phantom_camera_constructed (GObject *object)
{
    UcaPhantomCameraPrivate *priv;
    GSocketAddress *addr;

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (object);

    addr = phantom_discover (&priv->construct_error);

    if (addr != NULL) {
        priv->connection = g_socket_client_connect (priv->client, G_SOCKET_CONNECTABLE (addr), NULL, &priv->construct_error);
        g_object_unref (addr);
    }
}

static void
uca_phantom_camera_initable_iface_init (GInitableIface *iface)
{
    iface->init = ufo_net_camera_initable_init;
}

static void
uca_phantom_camera_class_init (UcaPhantomCameraClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);
    UcaCameraClass *camera_class = UCA_CAMERA_CLASS (klass);

    g_value_register_transform_func (G_TYPE_STRING, G_TYPE_UCHAR,   value_transform_uchar);
    g_value_register_transform_func (G_TYPE_STRING, G_TYPE_INT,     value_transform_int);
    g_value_register_transform_func (G_TYPE_STRING, G_TYPE_UINT,    value_transform_uint);
    g_value_register_transform_func (G_TYPE_STRING, G_TYPE_UINT64,  value_transform_uint64);
    g_value_register_transform_func (G_TYPE_STRING, G_TYPE_LONG,    value_transform_long);
    g_value_register_transform_func (G_TYPE_STRING, G_TYPE_ULONG,   value_transform_ulong);
    g_value_register_transform_func (G_TYPE_STRING, G_TYPE_FLOAT,   value_transform_float);
    g_value_register_transform_func (G_TYPE_STRING, G_TYPE_DOUBLE,  value_transform_double);

    oclass->set_property = uca_phantom_camera_set_property;
    oclass->get_property = uca_phantom_camera_get_property;
    oclass->constructed = uca_phantom_camera_constructed;
    oclass->dispose = uca_phantom_camera_dispose;
    oclass->finalize = uca_phantom_camera_finalize;

    camera_class->start_recording = uca_phantom_camera_start_recording;
    camera_class->stop_recording = uca_phantom_camera_stop_recording;
    camera_class->start_readout = uca_phantom_camera_start_readout;
    camera_class->stop_readout = uca_phantom_camera_stop_readout;
    camera_class->write = uca_phantom_camera_write;
    camera_class->grab = uca_phantom_camera_grab;
    camera_class->trigger = uca_phantom_camera_trigger;

    /*
     * XXX: we should try to construct the table from the UnitVariable table.
     */
    phantom_properties[PROP_SENSOR_TYPE] =
        g_param_spec_uint ("sensor-type",
            "Sensor type",
            "Sensor type",
            0, G_MAXUINT, 0, G_PARAM_READABLE);

    phantom_properties[PROP_SENSOR_VERSION] =
        g_param_spec_uint ("sensor-version",
            "Sensor type",
            "Sensor type",
            0, G_MAXUINT, 0, G_PARAM_READABLE);

    phantom_properties[PROP_HARDWARE_VERSION] =
        g_param_spec_uint ("hardware-version",
            "Hardware version",
            "Hardware version",
            0, G_MAXUINT, 0, G_PARAM_READABLE);

    phantom_properties[PROP_KERNEL_VERSION] =
        g_param_spec_uint ("kernel-version",
            "Kernel version",
            "Kernel version",
            0, G_MAXUINT, 0, G_PARAM_READABLE);

    phantom_properties[PROP_FIRMWARE_VERSION] =
        g_param_spec_uint ("firmware-version",
            "Firmware version",
            "Firmware version",
            0, G_MAXUINT, 0, G_PARAM_READABLE);

    phantom_properties[PROP_FPGA_VERSION] =
        g_param_spec_uint ("fpga-version",
            "FPGA version",
            "FPGA version",
            0, G_MAXUINT, 0, G_PARAM_READABLE);

    for (guint i = 0; i < base_overrideables[i]; i++)
        g_object_class_override_property (oclass, base_overrideables[i], uca_camera_props[base_overrideables[i]]);

    for (guint id = N_BASE_PROPERTIES; id < N_PROPERTIES; id++)
        g_object_class_install_property (oclass, id, phantom_properties[id]);

    g_type_class_add_private (klass, sizeof(UcaPhantomCameraPrivate));
}

static void
uca_phantom_camera_init (UcaPhantomCamera *self)
{
    UcaPhantomCameraPrivate *priv;

    self->priv = priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (self);

    priv->construct_error = NULL;
    priv->client = g_socket_client_new ();
    priv->listener = g_socket_listener_new ();
    priv->connection = NULL;
    priv->accept = NULL;
    priv->message_queue = g_async_queue_new ();
    priv->result_queue = g_async_queue_new ();
}

G_MODULE_EXPORT GType
camera_plugin_get_type (void)
{
    return UCA_TYPE_PHANTOM_CAMERA;
}
