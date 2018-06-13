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
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netinet/if_ether.h>
#include <net/if.h>
#include <linux/ip.h>
#include <uca/uca-camera.h>
#include "uca-phantom-camera.h"


#define UCA_PHANTOM_CAMERA_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UCA_TYPE_PHANTOM_CAMERA, UcaPhantomCameraPrivate))

#define CHECK_ETHERNET_HEADER   0

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

    /* 4.2.6 status */
    PROP_SENSOR_TEMPERATURE,
    PROP_CAMERA_TEMPERATURE,

    /* 4.4 cam structure */
    PROP_FRAME_SYNCHRONIZATION,
    PROP_FRAME_DELAY,
    PROP_NUM_CINES,

    /* 4.6 defc */
    PROP_POST_TRIGGER_FRAMES,

    /* 4.7.1 cine status */
    PROP_CINE_STATE,

    /* our own */
    PROP_ACQUISITION_MODE,
    PROP_IMAGE_FORMAT,
    PROP_ENABLE_10GE,
    PROP_NETWORK_INTERFACE,
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

typedef enum {
    ACQUISITION_MODE_STANDARD = 0,
    ACQUISITION_MODE_STANDARD_BINNED = 2,
    ACQUISITION_MODE_HS = 5,
    ACQUISITION_MODE_HS_BINNED = 7,
} AcquisitionMode;

static GEnumValue sync_mode_values[] = {
    { SYNC_MODE_FREE_RUN,           "SYNC_MODE_FREE_RUN",           "sync_mode_free_run" },
    { SYNC_MODE_FSYNC,              "SYNC_MODE_FSYNC",              "sync_mode_fsync" },
    { SYNC_MODE_IRIG,               "SYNC_MODE_IRIG",               "sync_mode_irig" },
    { SYNC_MODE_VIDEO_FRAME_RATE,   "SYNC_MODE_VIDEO_FRAME_RATE",   "sync_mode_video_frame_rate" },
    { 0, NULL, NULL }
};

static GEnumValue image_format_values[] = {
    { IMAGE_FORMAT_P16,     "IMAGE_FORMAT_P16",     "image_format_p16" },
    { IMAGE_FORMAT_P12L,    "IMAGE_FORMAT_P12L",    "image_format_p12l" },
    { 0, NULL, NULL }
};

static GEnumValue acquisition_mode_values[] = {
    { ACQUISITION_MODE_STANDARD,        "ACQUISITION_MODE_STANDARD",        "acquisition_mode_standard" },
    { ACQUISITION_MODE_STANDARD_BINNED, "ACQUISITION_MODE_STANDARD_BINNED", "acquisition_mode_standard_binned" },
    { ACQUISITION_MODE_HS,              "ACQUISITION_MODE_HS",              "acquisition_mode_hs" },
    { ACQUISITION_MODE_HS_BINNED,       "ACQUISITION_MODE_HS_BINNED",       "acquisition_mode_hs_binned" },
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
    GRegex              *res_pattern;

    guint                roi_width;
    guint                roi_height;
    guint8              *buffer;
    gchar               *features;
    gboolean             have_ximg;
    gboolean             enable_10ge;
    gchar               *iface;
    guint8               mac_address[6];
    ImageFormat          format;
    AcquisitionMode      acquisition_mode;
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
    { "info.imgformats", G_TYPE_STRING, G_PARAM_READABLE,  PROP_IMAGE_FORMATS,              TRUE },
    { "info.maxcines",   G_TYPE_UINT,   G_PARAM_READABLE,  PROP_MAX_NUM_CINES,              TRUE },
    { "info.xinc",       G_TYPE_UINT,   G_PARAM_READABLE,  PROP_ROI_WIDTH_MULTIPLIER,       TRUE },
    { "info.yinc",       G_TYPE_UINT,   G_PARAM_READABLE,  PROP_ROI_HEIGHT_MULTIPLIER,      TRUE },
    { "info.snstemp",    G_TYPE_UINT,   G_PARAM_READABLE,  PROP_SENSOR_TEMPERATURE,         TRUE },
    { "info.camtemp",    G_TYPE_UINT,   G_PARAM_READABLE,  PROP_CAMERA_TEMPERATURE,         TRUE },
    { "cam.syncimg",     G_TYPE_ENUM,   G_PARAM_READWRITE, PROP_FRAME_SYNCHRONIZATION,      TRUE },
    { "cam.frdelay",     G_TYPE_UINT,   G_PARAM_READWRITE, PROP_FRAME_DELAY,                FALSE },
    { "cam.cines",       G_TYPE_UINT,   G_PARAM_READWRITE, PROP_NUM_CINES,                  FALSE },
    { "defc.rate",       G_TYPE_FLOAT,  G_PARAM_READWRITE, PROP_FRAMES_PER_SECOND,          TRUE },
    { "defc.exp",        G_TYPE_UINT,   G_PARAM_READWRITE, PROP_EXPOSURE_TIME,              FALSE },
    { "defc.ptframes",   G_TYPE_UINT,   G_PARAM_READWRITE, PROP_POST_TRIGGER_FRAMES,        TRUE },
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
            g_set_error (error_loc, UCA_CAMERA_ERROR, UCA_CAMERA_ERROR_DEVICE,
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
    GMatchInfo *info;
    gchar *request;
    gchar *cr;
    gchar *value = NULL;
    gchar *reply = NULL;

    request = g_strdup_printf ("get %s\r\n", name);
    reply = phantom_talk (priv, request, NULL, 0, NULL);
    g_free (request);

    if (reply == NULL)
        return NULL;

    cr = strchr (reply, '\r');

    if (cr != NULL)
        *cr = '\0';

    if (!g_regex_match (priv->response_pattern, reply, 0, &info)) {
        g_warning ("Cannot parse `%s'", reply);
        g_free (reply);
        return NULL;
    }

    value = g_match_info_fetch (info, 2);
    g_match_info_free (info);
    g_free (reply);

    return value;
}

static gchar *
phantom_get_string (UcaPhantomCameraPrivate *priv, UnitVariable *var)
{
    return phantom_get_string_by_name (priv, var->name);
}

static gboolean
phantom_get_resolution_by_name (UcaPhantomCameraPrivate *priv,
                                const gchar *name,
                                guint *width,
                                guint *height)
{
    GMatchInfo *info;
    gchar *s;

    if (width)
        *width = 0;

    if (height)
        *height = 0;

    s = phantom_get_string_by_name (priv, name);

    if (s == NULL)
        return FALSE;

    if (!g_regex_match (priv->res_pattern, s, 0, &info)) {
        g_free (s);
        return FALSE;
    }

    if (width)
        *width = atoi (g_match_info_fetch (info, 1));

    if (height)
        *height = atoi (g_match_info_fetch (info, 2));

    g_free (s);
    g_match_info_free (info);
    return TRUE;
}

static void
phantom_get (UcaPhantomCameraPrivate *priv, UnitVariable *var, GValue *value)
{
    GValue reply_value = {0,};
    gchar *var_value;

    var_value = phantom_get_string (priv, var);

    g_value_init (&reply_value, G_TYPE_STRING);
    g_value_set_string (&reply_value, var_value);

    if (!g_value_transform (&reply_value, value))
        g_warning ("Could not transform `%s' to target value type %s", var_value, G_VALUE_TYPE_NAME (value));

    g_free (var_value);
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
phantom_set_resolution_by_name (UcaPhantomCameraPrivate *priv,
                                const gchar *name,
                                guint width,
                                guint height)
{
    gchar *request;
    gchar reply[256];

    request = g_strdup_printf ("set %s %u x %u\r\n", name, width, height);
    phantom_talk (priv, request, reply, sizeof (reply), NULL);
    g_free (request);
}

static gsize
get_buffer_size (UcaPhantomCameraPrivate *priv)
{
    /* Adapt this if we ever use 8 bit modes ... */
    return priv->roi_width * priv->roi_height * 2;
}

static void
uca_phantom_camera_start_recording (UcaCamera *camera,
                                    GError **error)
{
    const gchar *rec_request = "rec 1\r\n";
    const gchar *trig_request = "trig\r\n";

    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));
    g_free (phantom_talk (UCA_PHANTOM_CAMERA_GET_PRIVATE (camera), rec_request, NULL, 0, error));
    /* TODO: check previous error */
    g_free (phantom_talk (UCA_PHANTOM_CAMERA_GET_PRIVATE (camera), trig_request, NULL, 0, error));
}

static void
uca_phantom_camera_stop_recording (UcaCamera *camera,
                                   GError **error)
{
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));
}

static void
read_data (UcaPhantomCameraPrivate *priv, GInputStream *istream, GError **error)
{
    gsize to_read;

    to_read = get_buffer_size (priv);

    while (to_read > 0) {
        gsize bytes_read;

        if (!g_input_stream_read_all (istream, priv->buffer, to_read, &bytes_read, NULL, error))
            return;

        to_read -= bytes_read;
    }
}

static gpointer
accept_img_data (UcaPhantomCameraPrivate *priv)
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
        g_warning ("Listen cancelled\n");
        g_error_free (error);
        return NULL;
    }

    if (error != NULL) {
        g_warning ("Error: %s\n", error->message);
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

        istream = g_io_stream_get_input_stream (G_IO_STREAM (connection));
        message = g_async_queue_pop (priv->message_queue);

        switch (message->type) {
            case MESSAGE_READ_IMAGE:
                result = g_new0 (Result, 1);
                read_data (priv, istream, &result->error);
                result->type = RESULT_IMAGE;
                result->success = TRUE;
                g_async_queue_push (priv->result_queue, result);
                break;
            case MESSAGE_READ_TIMESTAMP:
                break;
            case MESSAGE_STOP:
                stop = TRUE;
                break;
        }

        g_free (message);
    }

    if (!g_io_stream_close (G_IO_STREAM (connection), NULL, &error)) {
        g_warning ("Could not close connection: %s\n", error->message);
        g_error_free (error);
    }

    g_object_unref (connection);

    return NULL;
}

static void
read_ximg_data (UcaPhantomCameraPrivate *priv,
                gint fd,
                GError **error)
{
    guint8 buffer[1506];

#if CHECK_ETHERNET_HEADER
    const struct ether_header *eth_h = (struct ether_header *) buffer;
#endif

    guint8 *dst = priv->buffer;
    gsize total = 0;

    const gsize header_size = 32;
    const gsize end_of_frame_size = 4;
    const gsize overhead = header_size + end_of_frame_size;

    /* XXX: replace recvfrom with the ring buffer code by Radu Corlan */
    while (total < 2500000) {
        gssize size;

        size = recvfrom (fd, buffer, 1506, 0, NULL, NULL);

        if (size < 0)
            break;

#if CHECK_ETHERNET_HEADER
        if (eth_h->ether_dhost[0] != priv->mac_address[0] ||
            eth_h->ether_dhost[1] != priv->mac_address[1] ||
            eth_h->ether_dhost[2] != priv->mac_address[2] ||
            eth_h->ether_dhost[3] != priv->mac_address[3] ||
            eth_h->ether_dhost[4] != priv->mac_address[4] ||
            eth_h->ether_dhost[5] != priv->mac_address[5]) {
            continue;
        }
#endif

        memcpy (dst, buffer + header_size, size - overhead);
        dst += size - overhead;
        total += size - overhead;
    }
}

static void
accept_ximg_data (UcaPhantomCameraPrivate *priv)
{
    Result *result;
    gint fd;
    gint sock_opt;
    struct ifreq if_opts = {0,};
    gboolean stop = FALSE;

    result = g_new0 (Result, 1);
    result->type = RESULT_READY;
    result->success = FALSE;
    fd = socket (PF_PACKET, SOCK_RAW, htons (0x88B7));

    if (fd == -1) {
        g_set_error_literal (&result->error, UCA_CAMERA_ERROR, UCA_CAMERA_ERROR_DEVICE,
                             "Could not open raw socket");
        g_async_queue_push (priv->result_queue, result);
        return;
    }

    /* get MAC address for ximg command */
    strncpy (if_opts.ifr_name, priv->iface, strlen (priv->iface));
    ioctl (fd, SIOCGIFHWADDR, &if_opts);

    priv->mac_address[0] = if_opts.ifr_hwaddr.sa_data[0];
    priv->mac_address[1] = if_opts.ifr_hwaddr.sa_data[1];
    priv->mac_address[2] = if_opts.ifr_hwaddr.sa_data[2];
    priv->mac_address[3] = if_opts.ifr_hwaddr.sa_data[3];
    priv->mac_address[4] = if_opts.ifr_hwaddr.sa_data[4];
    priv->mac_address[5] = if_opts.ifr_hwaddr.sa_data[5];

    /* re-use socket */
    if (setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof (sock_opt)) == -1) {
        g_set_error_literal (&result->error, UCA_CAMERA_ERROR, UCA_CAMERA_ERROR_DEVICE,
                             "Could not set socket mode to reuse");
        g_async_queue_push (priv->result_queue, result);
        close (fd);
        return;
    }

    /* bind to device */
    if (setsockopt (fd, SOL_SOCKET, SO_BINDTODEVICE, priv->iface, strlen (priv->iface)) == -1) {
        g_set_error (&result->error, UCA_CAMERA_ERROR, UCA_CAMERA_ERROR_DEVICE,
                     "Could not bind socket to %s", priv->iface);
        g_async_queue_push (priv->result_queue, result);
        close (fd);
        return;
    }

    g_debug ("Accepting raw ethernet frames ...");
    result->success = TRUE;
    g_async_queue_push (priv->result_queue, result);

    while (!stop) {
        InternalMessage *message;

        result = g_new0 (Result, 1);
        message = g_async_queue_pop (priv->message_queue);

        switch (message->type) {
            case MESSAGE_READ_IMAGE:
                read_ximg_data (priv, fd, &result->error);
                result->type = RESULT_IMAGE;
                result->success = TRUE;
                g_async_queue_push (priv->result_queue, result);
                break;
            case MESSAGE_READ_TIMESTAMP:
                break;
            case MESSAGE_STOP:
                stop = TRUE;
                break;
        }

        g_free (message);
    }

    close (fd);
}

static void
uca_phantom_camera_start_readout (UcaCamera *camera,
                                  GError **error)
{
    UcaPhantomCameraPrivate *priv;
    Result *result;

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    if (priv->enable_10ge && priv->iface == NULL) {
        g_set_error_literal (error, UCA_CAMERA_ERROR, UCA_CAMERA_ERROR_DEVICE,
                             "Trying to use 10GE but no network adapter is given");
        return;
    }

    g_free (priv->buffer);
    priv->buffer = g_malloc0 (get_buffer_size (priv));

    if (priv->enable_10ge) {
        priv->accept_thread = g_thread_new (NULL, (GThreadFunc) accept_ximg_data, priv);

        result = (Result *) g_async_queue_pop (priv->result_queue);
        g_assert (result->type == RESULT_READY);

        if (result->error != NULL)
            g_propagate_error (error, result->error);

        g_free (result);

        /* no startdata necessary for ximg */
    }
    else {
        gchar *reply;
        const gchar *request = "startdata {port:7116}\r\n";

        /* set up listener */
        g_socket_listener_add_inet_port (priv->listener, 7116, G_OBJECT (camera), error);
        priv->accept = g_cancellable_new ();
        priv->accept_thread = g_thread_new (NULL, (GThreadFunc) accept_img_data, priv);

        /* wait for listener to become ready */
        result = (Result *) g_async_queue_pop (priv->result_queue);
        g_assert (result->type == RESULT_READY);

        if (result->error != NULL)
            g_propagate_error (error, result->error);

        g_free (result);

        /* send startdata command */
        reply = phantom_talk (priv, request, NULL, 0, error);
        g_free (reply);
    }
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
             const guint8 *input,
             const guint num_pixels)
{
    guint i, j;

    for (i = 0, j = 0; i < num_pixels; i += 2, j += 3) {
        const guint16 tmp0 = (guint16) input[j + 0];
        const guint16 tmp1 = (guint16) input[j + 1];
        const guint16 tmp2 = (guint16) input[j + 2];

        output[i + 0] = tmp0 << 4 | (tmp1 >> 4);
        output[i + 1] = ((tmp1 & 0xF) << 8) | tmp2;
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
    gchar *additional;
    const gchar *command;
    const gchar *format;
    const gchar *request_fmt = "%s {cine:-1, start:0, cnt:1, fmt:%s %s}\r\n";
    gboolean return_value = TRUE;

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    if (priv->enable_10ge && priv->iface == NULL) {
        g_set_error_literal (error, UCA_CAMERA_ERROR, UCA_CAMERA_ERROR_DEVICE,
                             "Trying to use 10GE but no valid MAC address is given");
        return FALSE;
    }

    message = g_new0 (InternalMessage, 1);
    message->data = data;
    message->type = MESSAGE_READ_IMAGE;
    g_async_queue_push (priv->message_queue, message);

    switch (priv->format) {
        case IMAGE_FORMAT_P16:
            format = "P16";
            break;
        case IMAGE_FORMAT_P12L:
            format = "P12L";
            break;
    }

    if (priv->enable_10ge) {
        command = "ximg";
        additional = g_strdup_printf (", dest:%02x%02x%02x%02x%02x%02x",
                                      priv->mac_address[0], priv->mac_address[1],
                                      priv->mac_address[2], priv->mac_address[3],
                                      priv->mac_address[4], priv->mac_address[5]);
    }
    else {
        command = "img";
        additional = "";
    }

    request = g_strdup_printf (request_fmt, command, format, additional);

    if (priv->enable_10ge)
        g_free (additional);

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
                memcpy (data, priv->buffer, get_buffer_size (priv));
                break;
            case IMAGE_FORMAT_P12L:
                unpack_p12l (data, priv->buffer, priv->roi_width * priv->roi_height);
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
        case PROP_ROI_WIDTH:
            priv->roi_width = g_value_get_uint (value);
            phantom_set_resolution_by_name (priv, "defc.res", priv->roi_width, priv->roi_height);
            break;
        case PROP_ROI_HEIGHT:
            priv->roi_height = g_value_get_uint (value);
            phantom_set_resolution_by_name (priv, "defc.res", priv->roi_width, priv->roi_height);
            break;
        case PROP_IMAGE_FORMAT:
            priv->format = g_value_get_enum (value);
            break;
        case PROP_ACQUISITION_MODE:
            {
                gchar *request;
                GError *error = NULL;

                priv->acquisition_mode = g_value_get_enum (value);
                request = g_strdup_printf ("iload {mode:%i}\r\n", priv->acquisition_mode);
                g_free (phantom_talk (priv, request, NULL, 0, &error));
                g_free (request);

                if (error != NULL) {
                    g_warning ("Could not change acquisition mode: %s", error->message);
                    g_error_free (error);
                }
            }
            break;
        case PROP_ENABLE_10GE:
            if (!priv->have_ximg)
                g_warning ("10GE not supported by this camera");
            else
                priv->enable_10ge = g_value_get_boolean (value);
            break;
        case PROP_NETWORK_INTERFACE:
            g_free (priv->iface);
            priv->iface = g_value_dup_string (value);
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
        case PROP_ROI_WIDTH:
            g_value_set_uint (value, priv->roi_width);
            break;
        case PROP_ROI_HEIGHT:
            g_value_set_uint (value, priv->roi_height);
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
        case PROP_FEATURES:
            g_value_set_string (value, priv->features);
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
        case PROP_ACQUISITION_MODE:
            g_value_set_enum (value, priv->acquisition_mode);
            break;
        case PROP_ENABLE_10GE:
            g_value_set_boolean (value, priv->enable_10ge);
            break;
        case PROP_NETWORK_INTERFACE:
            if (priv->iface == NULL)
                g_value_set_string (value, "");
            else
                g_value_set_string (value, priv->iface);
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
        GError *error = NULL;

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
    g_regex_unref (priv->res_pattern);
    g_free (priv->buffer);
    g_free (priv->features);
    g_free (priv->iface);

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
        g_set_error (error, UCA_CAMERA_ERROR, UCA_CAMERA_ERROR_DEVICE,
                     "`%s' does not match expected reply", reply);
        goto cleanup_discovery_addr;
    }

    port_string = g_match_info_fetch (info, 1);
    port = atoi (port_string);
    g_free (port_string);
    result = g_inet_socket_address_new (g_inet_socket_address_get_address ((GInetSocketAddress *) remote_socket_addr), port);
    g_match_info_free (info);

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
        gchar *addr_string;

        priv->connection = g_socket_client_connect (priv->client, G_SOCKET_CONNECTABLE (addr), NULL, &priv->construct_error);
        addr_string = g_inet_address_to_string (g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (addr)));
        g_debug ("Connected to %s\n", addr_string);
        g_free (addr_string);
        g_object_unref (addr);

        phantom_get_resolution_by_name (priv, "defc.res", &priv->roi_width, &priv->roi_height);
        priv->features = phantom_get_string_by_name (priv, "info.features");
        priv->have_ximg = strstr (priv->features, "ximg") != NULL;
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

    phantom_properties[PROP_SENSOR_TEMPERATURE] =
        g_param_spec_uint ("sensor-temperature",
            "Sensor temperature",
            "Sensor temperature",
            0, G_MAXUINT, 0, G_PARAM_READABLE);

    phantom_properties[PROP_CAMERA_TEMPERATURE] =
        g_param_spec_uint ("camera-temperature",
            "Camera temperature",
            "Camera temperature",
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

    phantom_properties[PROP_POST_TRIGGER_FRAMES] =
        g_param_spec_uint ("post-trigger-frames",
            "Number of post-trigger frames",
            "Number of post-trigger frames",
            0, G_MAXUINT, 0, G_PARAM_READWRITE);

    phantom_properties[PROP_IMAGE_FORMAT] =
        g_param_spec_enum ("image-format",
            "Image format",
            "Image format",
            g_enum_register_static ("image-format", image_format_values),
            IMAGE_FORMAT_P12L, G_PARAM_READWRITE);

    phantom_properties[PROP_ACQUISITION_MODE] =
        g_param_spec_enum ("acquisition-mode",
            "Acquisition mode",
            "Acquisition mode",
            g_enum_register_static ("acquisition-mode", acquisition_mode_values),
            ACQUISITION_MODE_STANDARD, G_PARAM_READWRITE);

    /* Ideally, we would install this property only if ximg is available ... */
    phantom_properties[PROP_ENABLE_10GE] =
        g_param_spec_boolean ("enable-10ge",
            "Enable 10GE data transmission",
            "Enable 10GE data transmission",
            FALSE, G_PARAM_READWRITE);

    phantom_properties[PROP_NETWORK_INTERFACE] =
        g_param_spec_string ("network-interface",
            "Network interface name of the 10GE device",
            "Network interface name of the 10GE device",
            "", G_PARAM_READWRITE);

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
    priv->buffer = NULL;
    priv->features = NULL;
    priv->format = IMAGE_FORMAT_P12L;
    priv->acquisition_mode = ACQUISITION_MODE_STANDARD;
    priv->enable_10ge = FALSE;
    priv->iface = NULL;
    priv->have_ximg = FALSE;
    priv->message_queue = g_async_queue_new ();
    priv->result_queue = g_async_queue_new ();

    /*
     * Matches responses to `get` requests but covers only single value
     * responses, i.e. something like `def.res : 1024 x 976`.
     */
    priv->response_pattern = g_regex_new ("\\s*([A-Za-z0-9]+)\\s*:\\s*{?\\s*\"?([A-Za-z0-9\\s]+)\"?\\s*}?", 0, 0, NULL);

    /* Matches `1024 x 976`. */
    priv->res_pattern = g_regex_new ("\\s*([0-9]+)\\s*x\\s*([0-9]+)", 0, 0, NULL);

    uca_camera_register_unit (UCA_CAMERA (self), "frame-delay", UCA_UNIT_SECOND);
    uca_camera_register_unit (UCA_CAMERA (self), "sensor-temperature", UCA_UNIT_DEGREE_CELSIUS);
    uca_camera_register_unit (UCA_CAMERA (self), "camera-temperature", UCA_UNIT_DEGREE_CELSIUS);
}

G_MODULE_EXPORT GType
camera_plugin_get_type (void)
{
    return UCA_TYPE_PHANTOM_CAMERA;
}
