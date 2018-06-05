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
    /* 4.2. info structure */
    PROP_SENSOR_TYPE = N_BASE_PROPERTIES,

    /* 4.2.1 sensor information */
    PROP_SENSOR_VERSION,

    /* 4.2.2 version and identification */
    PROP_HARDWARE_VERSION,
    PROP_KERNEL_VERSION,
    PROP_FIRMWARE_VERSION,
    PROP_FPGA_VERSION,
    PROP_MODEL,
    PROP_PROTOCOL_VERSION,
    PROP_SYSTEM_RELEASE_VERSION,
    PROP_FIRMWARE_RELEASE_VERSION,
    PROP_SERIAL_NUMBER,

    /* 4.2.3 capabilities */
    PROP_FEATURES,
    PROP_IMAGE_FORMATS,
    PROP_MAX_NUM_CINES,

    /* 4.4 cam structure */
    PROP_FRAME_SYNCHRONIZATION,
    PROP_FRAME_DELAY,
    PROP_NUM_CINES,

    /* 4.6 defc */
    PROP_ENABLE_HQ_MODE,

    /* 4.7.1 cine status */
    PROP_CINE_STATE,

    /* our own */
    PROP_IMAGE_FORMAT,
    N_PROPERTIES
};

static gint base_overrideables[] = {
    PROP_NAME,                  /* info.name */
    PROP_SENSOR_WIDTH,
    PROP_SENSOR_HEIGHT,
    PROP_SENSOR_BITDEPTH,
    PROP_ROI_X,
    PROP_ROI_Y,
    PROP_ROI_WIDTH,
    PROP_ROI_HEIGHT,
    PROP_ROI_WIDTH_MULTIPLIER,  /* info.xinc */
    PROP_ROI_HEIGHT_MULTIPLIER, /* info.yinc */
    PROP_EXPOSURE_TIME,         /* defc.exp */
    PROP_FRAMES_PER_SECOND,     /* defc.rate */
    PROP_RECORDED_FRAMES,
    PROP_HAS_STREAMING,
    PROP_HAS_CAMRAM_RECORDING,
    0
};

static GParamSpec *phantom_properties[N_PROPERTIES] = { NULL, };

/* static const gsize MAX_BUFFER_SIZE = 2048 * 1952 * 2; */
static const gsize MAX_BUFFER_SIZE = 1024 * 976 * 2;

typedef enum {
    SYNC_MODE_FREE_RUN = 0,
    SYNC_MODE_FSYNC,
    SYNC_MODE_IRIG,
    SYNC_MODE_VIDEO_FRAME_RATE,
} SyncMode;

typedef enum {
    IMAGE_FORMAT_P16 = 0,
    IMAGE_FORMAT_P12L,
} ImageFormat;

static GEnumValue sync_mode_values[] = {
    { SYNC_MODE_FREE_RUN, "SYNC_MODE_FREE_RUN", "sync_mode_free_run" },
    { SYNC_MODE_FSYNC, "SYNC_MODE_FSYNC", "sync_mode_fsync" },
    { SYNC_MODE_IRIG, "SYNC_MODE_IRIG", "sync_mode_irig" },
    { SYNC_MODE_VIDEO_FRAME_RATE, "SYNC_MODE_VIDEO_FRAME_RATE", "sync_mode_video_frame_rate" },
    { 0, NULL, NULL }
};

static GEnumValue image_format_values[] = {
    { IMAGE_FORMAT_P16,     "IMAGE_FORMAT_P16",     "image_format_p16" },
    { IMAGE_FORMAT_P12L,    "IMAGE_FORMAT_P12L",    "image_format_p12l" },
    { 0, NULL, NULL }
};

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
    GRegex              *response_pattern;
    guint32             *buffer;
    ImageFormat          format;
};

typedef struct  {
    const gchar *name;
    GType        type;
    GParamFlags  flags;
    gint         property_id;
    gboolean     handle_automatically;
} UnitVariable;

static UnitVariable variables[] = {
    { "info.sensor",     G_TYPE_UINT,   G_PARAM_READABLE,  PROP_SENSOR_TYPE,                TRUE },
    { "info.snsversion", G_TYPE_UINT,   G_PARAM_READABLE,  PROP_SENSOR_VERSION,             TRUE },
    { "info.hwver",      G_TYPE_UINT,   G_PARAM_READABLE,  PROP_HARDWARE_VERSION,           TRUE },
    { "info.kernel",     G_TYPE_UINT,   G_PARAM_READABLE,  PROP_KERNEL_VERSION,             TRUE },
    { "info.swver",      G_TYPE_UINT,   G_PARAM_READABLE,  PROP_FIRMWARE_VERSION,           TRUE },
    { "info.xver",       G_TYPE_UINT,   G_PARAM_READABLE,  PROP_FPGA_VERSION,               TRUE },
    { "info.model",      G_TYPE_STRING, G_PARAM_READABLE,  PROP_MODEL,                      TRUE },
    { "info.pver",       G_TYPE_UINT,   G_PARAM_READABLE,  PROP_PROTOCOL_VERSION,           TRUE },
    { "info.sver",       G_TYPE_UINT,   G_PARAM_READABLE,  PROP_SYSTEM_RELEASE_VERSION,     TRUE },
    { "info.fver",       G_TYPE_UINT,   G_PARAM_READABLE,  PROP_FIRMWARE_RELEASE_VERSION,   TRUE },
    { "info.serial",     G_TYPE_UINT,   G_PARAM_READABLE,  PROP_SERIAL_NUMBER,              TRUE },
    { "info.xmax",       G_TYPE_UINT,   G_PARAM_READABLE,  PROP_SENSOR_WIDTH,               TRUE },
    { "info.ymax",       G_TYPE_UINT,   G_PARAM_READABLE,  PROP_SENSOR_HEIGHT,              TRUE },
    { "info.name",       G_TYPE_STRING, G_PARAM_READABLE,  PROP_NAME,                       TRUE },
    { "info.features",   G_TYPE_STRING, G_PARAM_READABLE,  PROP_FEATURES,                   TRUE },
    { "info.imgformats", G_TYPE_STRING, G_PARAM_READABLE,  PROP_IMAGE_FORMATS,              TRUE },
    { "info.maxcines",   G_TYPE_UINT,   G_PARAM_READABLE,  PROP_MAX_NUM_CINES,              TRUE },
    { "info.xinc",       G_TYPE_UINT,   G_PARAM_READABLE,  PROP_ROI_WIDTH_MULTIPLIER,       TRUE },
    { "info.yinc",       G_TYPE_UINT,   G_PARAM_READABLE,  PROP_ROI_HEIGHT_MULTIPLIER,      TRUE },
    { "cam.syncimg",     G_TYPE_ENUM,   G_PARAM_READWRITE, PROP_FRAME_SYNCHRONIZATION,      TRUE },
    { "cam.frdelay",     G_TYPE_UINT,   G_PARAM_READWRITE, PROP_FRAME_DELAY,                FALSE },
    { "cam.cines",       G_TYPE_UINT,   G_PARAM_READWRITE, PROP_NUM_CINES,                  FALSE },
    { "defc.rate",       G_TYPE_FLOAT,  G_PARAM_READWRITE, PROP_FRAMES_PER_SECOND,          TRUE },
    { "defc.exp",        G_TYPE_UINT,   G_PARAM_READWRITE, PROP_EXPOSURE_TIME,              FALSE },
    { "defc.meta.w",     G_TYPE_UINT,   G_PARAM_READWRITE, PROP_ROI_WIDTH,                  TRUE },
    { "defc.meta.h",     G_TYPE_UINT,   G_PARAM_READWRITE, PROP_ROI_HEIGHT,                 TRUE },
    { "defc.hqenable",   G_TYPE_UINT,   G_PARAM_READWRITE, PROP_ENABLE_HQ_MODE,             TRUE },
    { "c1.frcount",      G_TYPE_UINT,   G_PARAM_READABLE,  PROP_RECORDED_FRAMES,            TRUE },
    { "c1.state",        G_TYPE_STRING, G_PARAM_READABLE,  PROP_CINE_STATE,                 TRUE },
    { NULL, }
};

typedef struct {
    enum {
        MESSAGE_READ_IMAGE = 1,
        MESSAGE_READ_TIMESTAMP,
        MESSAGE_STOP,
    } type;
    gpointer  data;
} InternalMessage;

typedef struct {
    enum {
        RESULT_READY = 1,
        RESULT_IMAGE,
    } type;
    gboolean success;
    GError *error;
} Result;

#define DEFINE_CAST(suffix, trans_func)                 \
static void                                             \
value_transform_##suffix (const GValue *src_value,      \
                         GValue       *dest_value)      \
{                                                       \
  const gchar* src = g_value_get_string (src_value);    \
  g_value_set_##suffix (dest_value, trans_func (src));  \
}

static gboolean
str_to_boolean (const gchar *s)
{
    return g_ascii_strncasecmp (s, "true", 4) == 0;
}

DEFINE_CAST (uchar,     atoi)
DEFINE_CAST (int,       atoi)
DEFINE_CAST (long,      atol)
DEFINE_CAST (uint,      atoi)
DEFINE_CAST (uint64,    atoi)
DEFINE_CAST (ulong,     atol)
DEFINE_CAST (float,     atof)
DEFINE_CAST (double,    atof)
DEFINE_CAST (enum,      atoi)   /* not super type safe */
DEFINE_CAST (boolean,   str_to_boolean)

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
phantom_get_string_by_name (UcaPhantomCameraPrivate *priv, const gchar *name)
{
    gchar *request;
    gchar *cr = NULL;
    gchar *reply = NULL;

    request = g_strdup_printf ("get %s\r\n", name);
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

static gchar *
phantom_get_string (UcaPhantomCameraPrivate *priv, UnitVariable *var)
{
    return phantom_get_string_by_name (priv, var->name);
}

static void
phantom_get (UcaPhantomCameraPrivate *priv, UnitVariable *var, GValue *value)
{
    gchar *reply;
    GValue reply_value = {0,};
    GMatchInfo *info;
    gchar *var_value;

    reply = phantom_get_string (priv, var);

    if (reply == NULL)
        return;

    if (!g_regex_match (priv->response_pattern, reply, 0, &info)) {
        g_warning ("Cannot parse `%s'", reply);
        return;
    }

    var_value = g_match_info_fetch (info, 2);

    g_value_init (&reply_value, G_TYPE_STRING);
    g_value_set_string (&reply_value, var_value);

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

    if (!(var->flags & G_PARAM_WRITABLE)) {
        g_warning ("%s cannot be written", var->name);
        return;
    }

    request = g_strdup_printf ("set %s %s\r\n", var->name, value);
    phantom_talk (priv, request, reply, sizeof (reply), NULL);

    g_free (request);
}

static void
phantom_set (UcaPhantomCameraPrivate *priv, UnitVariable *var, const GValue *value)
{
    GValue request_value = {0,};

    g_value_init (&request_value, G_TYPE_STRING);
    g_value_transform (value, &request_value);
    phantom_set_string (priv, var, g_value_get_string (&request_value));
    g_value_unset (&request_value);
}

static void
uca_phantom_camera_start_recording (UcaCamera *camera,
                                    GError **error)
{
    const gchar *rec_request = "rec 1\r\n";
    /* const gchar *trig_request = "trig\r\n"; */

    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));
    g_free (phantom_talk (UCA_PHANTOM_CAMERA_GET_PRIVATE (camera), rec_request, NULL, 0, error));
    /* TODO: check previous error */
    /* g_free (phantom_talk (UCA_PHANTOM_CAMERA_GET_PRIVATE (camera), trig_request, NULL, 0, error)); */
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
    Result *result;
    gchar *addr;
    gboolean stop = FALSE;
    GError *error = NULL;

    g_debug ("Accepting data connection ...");
    result = g_new0 (Result, 1);
    result->type = RESULT_READY;
    g_async_queue_push (priv->result_queue, result);
    connection = g_socket_listener_accept (priv->listener, NULL, priv->accept, &error);

    if (g_cancellable_is_cancelled (priv->accept)) {
        g_print ("Listen cancelled\n");
        g_error_free (error);
        return NULL;
    }

    if (error != NULL) {
        g_print ("Error: %s\n", error->message);
        g_error_free (error);
        return NULL;
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
        gsize bytes_read;

        result = g_new0 (Result, 1);
        istream = g_io_stream_get_input_stream (G_IO_STREAM (connection));
        message = g_async_queue_pop (priv->message_queue);

        switch (message->type) {
            case MESSAGE_READ_IMAGE:
                g_input_stream_read_all (istream, priv->buffer, MAX_BUFFER_SIZE, &bytes_read, NULL, &result->error);
                break;
            case MESSAGE_READ_TIMESTAMP:
                break;
            case MESSAGE_STOP:
                stop = TRUE;
                break;
        }

        g_free (message);
        result->type = RESULT_IMAGE;
        result->success = TRUE;
        g_async_queue_push (priv->result_queue, result);
    }

    if (!g_io_stream_close (G_IO_STREAM (connection), NULL, &error)) {
        g_warning ("Could not close connection: %s\n", error->message);
        g_error_free (error);
    }

    g_object_unref (connection);

    return NULL;
}

static void
uca_phantom_camera_start_readout (UcaCamera *camera,
                                  GError **error)
{
    UcaPhantomCameraPrivate *priv;
    Result *result;
    gchar *reply;
    const gchar *request = "startdata {port:7116}\r\n";

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    /* set up listener */
    g_socket_listener_add_inet_port (priv->listener, 7116, G_OBJECT (camera), error);
    priv->accept = g_cancellable_new ();
    priv->accept_thread = g_thread_new (NULL, (GThreadFunc) accept_data, priv);

    /* wait for listener to become ready */
    result = (Result *) g_async_queue_pop (priv->result_queue);
    g_assert (result->type == RESULT_READY);
    g_free (result);

    /* send startdata command */
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

static void
unpack_p12l (guint16 *output,
             const guint32 *input,
             const guint num_pixels)
{
    guint i, j;

    /*
     * Eight 12 bit values are packed into three 32 bit words. Each word is in
     * Big endian format, i.e. the four bytes of a 32 bit word are mirrored.
     * Once they are swapped to Little endian, you can think of it as a Big
     * Endian bit stream of three 4 bit words. The middle word becomes the
     * highest word, the lower word becomes the middle word and the highest word
     * becomes the lowest word.
     *
     * For anyone taking this up: good luck with vectorization!
     */
    for (i = 0, j = 0; i < num_pixels; i += 8, j += 3) {
        const guint32 tmp1 = g_ntohl (input[j + 0]);
        const guint32 tmp2 = g_ntohl (input[j + 1]);
        const guint32 tmp3 = g_ntohl (input[j + 2]);

        output[i + 0] = ((tmp1 & 0x00F00000) >> 16) |
                        ((tmp1 & 0x0F000000) >> 16) |
                        ((tmp1 & 0xF0000000) >> 28);

        output[i + 1] = ((tmp1 & 0x00000F00) >> 4) |
                        ((tmp1 & 0x0000F000) >> 4) |
                        ((tmp1 & 0x000F0000) >> 16);

        output[i + 2] = ((tmp2 & 0xF0000000) >> 24) |
                        ((tmp1 & 0x0000000F) << 8) |
                        ((tmp1 & 0x000000F0) >> 4);

        output[i + 3] = ((tmp2 & 0x000F0000) >> 12) |
                        ((tmp2 & 0x00F00000) >> 12) |
                        ((tmp2 & 0x0F000000) >> 24);

        output[i + 4] = ((tmp2 & 0x000000F0)) |
                        ((tmp2 & 0x00000F00)) |
                        ((tmp2 & 0x0000F000) >> 12);

        output[i + 5] = ((tmp3 & 0x0F000000) >> 20) |
                        ((tmp3 & 0xF0000000) >> 20) |
                        ((tmp2 & 0x0000000F));

        output[i + 6] = ((tmp3 & 0x0000F000) >> 8) |
                        ((tmp3 & 0x000F0000) >> 8) |
                        ((tmp3 & 0x00F00000) >> 20);

        output[i + 7] = ((tmp3 & 0x0000000F) << 4) |
                        ((tmp3 & 0x000000F0) << 4) |
                        ((tmp3 & 0x00000F00) >> 8);
    }
}

static gboolean
uca_phantom_camera_grab (UcaCamera *camera,
                         gpointer data,
                         GError **error)
{
    UcaPhantomCameraPrivate *priv;
    InternalMessage *message;
    Result *result;
    gchar *request;
    gchar *reply;
    gboolean return_value = TRUE;
    const gchar *request_fmt = "img {cine:1, start:1, cnt:100, fmt:%s}\r\n";

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    message = g_new0 (InternalMessage, 1);
    message->data = data;
    message->type = MESSAGE_READ_IMAGE;
    g_async_queue_push (priv->message_queue, message);

    switch (priv->format) {
        case IMAGE_FORMAT_P16:
            request = g_strdup_printf (request_fmt, "P16");
            break;
        case IMAGE_FORMAT_P12L:
            request = g_strdup_printf (request_fmt, "P12L");
            break;
    }

    /* send request */
    reply = phantom_talk (priv, request, NULL, 0, error);
    g_free (request);

    if (reply == NULL)
        return FALSE;

    g_free (reply);

    /* wait for image transfer to finish */
    result = g_async_queue_pop (priv->result_queue);
    g_assert (result->type == RESULT_IMAGE);
    return_value = result->success;

    if (result->success) {
        switch (priv->format) {
            case IMAGE_FORMAT_P16:
                memcpy (data, priv->buffer, MAX_BUFFER_SIZE);
                break;
            case IMAGE_FORMAT_P12L:
                unpack_p12l (data, priv->buffer, 1024 * 976);
                break;
        }
    }

    if (result->error != NULL)
        g_propagate_error (error, result->error);

    g_free (result);

    return return_value;
}

static void
uca_phantom_camera_trigger (UcaCamera *camera,
                            GError **error)
{
    const gchar *request = "trig\r\n";

    /*
     * XXX: note that this triggers the acquisition of an entire series of
     * images rather than triggering the exposure of a single image.
     */
    g_free (phantom_talk (UCA_PHANTOM_CAMERA_GET_PRIVATE (camera), request, NULL, 0, error));
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));
}

static void
uca_phantom_camera_set_property (GObject *object,
                                 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
    UcaPhantomCameraPrivate *priv;
    UnitVariable *var;

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (object);
    var = phantom_lookup_by_id (property_id);

    if (var != NULL && var->handle_automatically) {
        phantom_set (priv, var, value);
        return;
    }

    switch (property_id) {
        case PROP_EXPOSURE_TIME:
            {
                gchar *val;
                gdouble time;

                time = g_value_get_double (value);
                val = g_strdup_printf ("%u", (guint) (time * 1000 * 1000 * 1000));
                phantom_set_string (priv, var, val);
                g_free (val);
            }
            break;
        case PROP_IMAGE_FORMAT:
            priv->format = g_value_get_enum (value);
            break;
    }
}

static void
uca_phantom_camera_get_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
    UcaPhantomCameraPrivate *priv;
    UnitVariable *var;

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (object);
    var = phantom_lookup_by_id (property_id);

    if (var != NULL && var->handle_automatically) {
        phantom_get (priv, var, value);
        return;
    }

    switch (property_id) {
        case PROP_SENSOR_BITDEPTH:
            g_value_set_uint (value, 12);
            break;
        case PROP_EXPOSURE_TIME:
            /* fall through */
        case PROP_FRAME_DELAY:
            {
                gchar *s;
                gdouble time;
                s = phantom_get_string (priv, var);
                time = atoi (s) / 1000.0 / 1000.0 / 1000.0;
                g_value_set_double (value, time);
                g_free (s);
            }
            break;
        case PROP_NUM_CINES:
            {
                /*
                 * We want to handle setting the property, so we have to bail
                 * out on the automatic handling ...
                 */
                phantom_get (priv, var, value);
            }
            break;
        case PROP_IMAGE_FORMAT:
            g_value_set_enum (value, priv->format);
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
    UcaPhantomCameraPrivate *priv;

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (object);
    g_regex_unref (priv->response_pattern);
    g_free (priv->buffer);

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
    g_value_register_transform_func (G_TYPE_STRING, G_TYPE_ENUM,    value_transform_enum);
    g_value_register_transform_func (G_TYPE_STRING, G_TYPE_BOOLEAN, value_transform_boolean);

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

    phantom_properties[PROP_MODEL] =
        g_param_spec_string ("model",
            "Model",
            "Model",
            "", G_PARAM_READABLE);

    phantom_properties[PROP_PROTOCOL_VERSION] =
        g_param_spec_uint ("protocol-version",
            "Protocol version",
            "Protocol version",
            0, G_MAXUINT, 0, G_PARAM_READABLE);

    phantom_properties[PROP_SYSTEM_RELEASE_VERSION] =
        g_param_spec_uint ("system-release-version",
            "System release version",
            "System release version",
            0, G_MAXUINT, 0, G_PARAM_READABLE);

    phantom_properties[PROP_FIRMWARE_RELEASE_VERSION] =
        g_param_spec_uint ("firmware-release-version",
            "Firmware release version",
            "Firmware release version",
            0, G_MAXUINT, 0, G_PARAM_READABLE);

    phantom_properties[PROP_SERIAL_NUMBER] =
        g_param_spec_uint ("serial-number",
            "Serial number",
            "Serial number",
            0, G_MAXUINT, 0, G_PARAM_READABLE);

    phantom_properties[PROP_FEATURES] =
        g_param_spec_string ("features",
            "Features",
            "Features see 4.2.4",
            "", G_PARAM_READABLE);

    phantom_properties[PROP_IMAGE_FORMATS] =
        g_param_spec_string ("image-formats",
            "Image formats",
            "Image formats",
            "", G_PARAM_READABLE);

    phantom_properties[PROP_MAX_NUM_CINES] =
        g_param_spec_uint ("max-num-cines",
            "Number of maximum allocatable cines",
            "Number of maximum allocatable cines",
            0, G_MAXUINT, 0, G_PARAM_READABLE);

    phantom_properties[PROP_FRAME_SYNCHRONIZATION] =
        g_param_spec_enum ("frame-synchronization",
            "Frame synchronization mode",
            "Frame synchronization mode",
            g_enum_register_static ("sync-mode", sync_mode_values),
            SYNC_MODE_FREE_RUN,
            G_PARAM_READWRITE);

    phantom_properties[PROP_FRAME_DELAY] =
        g_param_spec_double ("frame-delay",
            "Frame delay in seconds",
            "Frame delay in seconds",
            0.0, G_MAXDOUBLE, 0.0, G_PARAM_READWRITE);

    phantom_properties[PROP_NUM_CINES] =
        g_param_spec_uint ("num-cines",
            "Number of maximum allocatable cines",
            "Number of maximum allocatable cines",
            0, G_MAXUINT, 0, G_PARAM_READWRITE);

    phantom_properties[PROP_CINE_STATE] =
        g_param_spec_string ("cine-state",
            "State of current cine",
            "State of current cine",
            "", G_PARAM_READABLE);

    phantom_properties[PROP_ENABLE_HQ_MODE] =
        g_param_spec_uint ("enable-hq-mode",
            "Enable HQ acquisition mode",
            "Enable HQ acquisition mode",
            0, G_MAXUINT, 0, G_PARAM_READWRITE);

    phantom_properties[PROP_IMAGE_FORMAT] =
        g_param_spec_enum ("image-format",
            "Image format",
            "Image format",
            g_enum_register_static ("image-format", image_format_values),
            IMAGE_FORMAT_P12L,
            G_PARAM_READWRITE);

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
    GError *error = NULL;

    self->priv = priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (self);

    priv->construct_error = NULL;
    priv->client = g_socket_client_new ();
    priv->listener = g_socket_listener_new ();
    priv->connection = NULL;
    priv->accept = NULL;
    priv->format = IMAGE_FORMAT_P12L;
    priv->message_queue = g_async_queue_new ();
    priv->result_queue = g_async_queue_new ();
    priv->response_pattern = g_regex_new ("\\s*([A-Za-z0-9]+)\\s*:\\s*{?\\s*\"?([A-Za-z0-9\\s]+)\"?\\s*}?", 0, 0, &error);

    /* TODO: make dynamic and don't waste too much space */
    priv->buffer = g_malloc0 (MAX_BUFFER_SIZE);

    if (error != NULL)
        g_print ("%s\n", error->message);

    uca_camera_register_unit (UCA_CAMERA (self), "frame-delay", UCA_UNIT_SECOND);
}

G_MODULE_EXPORT GType
camera_plugin_get_type (void)
{
    return UCA_TYPE_PHANTOM_CAMERA;
}
