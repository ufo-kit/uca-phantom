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
#include <stdint.h>
#include <inttypes.h>
#include <time.h>

#include <gio/gio.h>
#include <gmodule.h>
#include <string.h>
#include <unistd.h>

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
#include <uca/uca-camera.h>
#include "uca-phantom-camera.h"


// ***************************************
// HARDCODING THE IP ADDRESS OF THE CAMERA
// ***************************************

//#define IP_ADDRESS "100.100.189.164"
#define IP_ADDRESS      "127.0.0.1"
//#define IP_ADDRESS      "172.16.31.157"
//#define INTERFACE       "enp3s0f0"
#define INTERFACE       "enp1s0"
#define PROTOCOL        ETH_P_ALL
//#define PROTOCOL        0x88b7
#define X_NETWORK       TRUE

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

// *************************************
// STRUCTS FOR THE RAW SOCKET CONNECTION
// *************************************

struct block_desc {
    uint32_t version;
    uint32_t offset_to_priv;
    struct tpacket_hdr_v1 h1;
};

struct ring {
    struct iovec *rd;
    uint8_t *map;
    struct tpacket_req3 req;
};


// ***************************
// STRUCT AND ENUM DEFINITIONS
// ***************************

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

// 27.03.2019
// Added the two formats "IMAGE_FORMAT_P10" and "IMAGE_FORMAT_P8"
typedef enum {
    IMAGE_FORMAT_P16,
    IMAGE_FORMAT_P12L,
    IMAGE_FORMAT_P10,
    IMAGE_FORMAT_P8
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

// 27.03.2019
// Added the two formats "IMAGE_FORMAT_P10" and "IMAGE_FORMAT_P8"
static GEnumValue image_format_values[] = {
    { IMAGE_FORMAT_P16,     "IMAGE_FORMAT_P16",     "image_format_p16" },
    { IMAGE_FORMAT_P12L,    "IMAGE_FORMAT_P12L",    "image_format_p12l" },
    { IMAGE_FORMAT_P10,     "IMAGE_FORMAT_P10",     "image_format_p10"},
    { IMAGE_FORMAT_P8,      "IMAGE_FORMAT_P8",      "image_format_p8"},
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


// ***************************************
// BASIC NETWORK INTERACTIONS WITH PHANTOM
// ***************************************

static UnitVariable *
phantom_lookup_by_id (gint property_id)
{
    for (guint i = 0; variables[i].name != NULL; i++) {
        if (variables[i].property_id == property_id)
            return &variables[i];
    }

    return NULL;
}

/**
 * @brief Sends the given request to the phantom and returns the response
 *
 * This function sends the given request string @p request to the phantom camera using the socket streams and then
 * received the response and returns it.
 *
 * @author Matthias Vogelgesang
 *
 * @param priv
 * @param request
 * @param reply_loc
 * @param reply_loc_size
 * @param error_loc
 * @return
 */
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

    // Here we are actually pushing the request string to the output stream, which will send over the nertwork to
    // the phantom. The output of the write all function is a boolean indicator, of whether it worked or not.
    gboolean output_write_success;
    output_write_success = g_output_stream_write_all (ostream, request, strlen (request), &size, NULL, &error);

    // In case the write did not work, we will inform the user first and then return NULL, terminating this function.
    if (!output_write_success) {
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

    // So this seems like, we are either using the reply size/string, that has been passed to the this function already
    // and essentially append to it. But in case no already existing reply string has been passed we are creating a new
    // buffer where the reply will later be saved into
    reply_size = reply_loc ? reply_loc_size : 512;
    reply = reply_loc ? reply_loc : g_malloc0 (reply_size);

    // Here we are actually reading from the input stream/receiving the response from the camera. The actual characters
    // string will be saved into the "reply" buffer (passed as pointer argument). The return value of the function is a
    // boolean value indicating of whether there was an issue or not.
    gboolean input_read_success;
    input_read_success = g_input_stream_read (istream, reply, reply_size, NULL, &error);

    if (!input_read_success) {
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

    // Of course, things cant only go wrong on this end. A malformed request or other things may cause an error inside
    // The phantom. The phantom will tell us so, by sending an error message, always (!) starting with "ERR:".
    // So here we are detecting, if the response is an error message and if so, the uses is notified.
    gboolean is_phantom_error;
    is_phantom_error = g_str_has_prefix (reply, "ERR: ");

    if (is_phantom_error) {
        if (error_loc != NULL) {
            g_set_error (error_loc, UCA_CAMERA_ERROR, UCA_CAMERA_ERROR_DEVICE,
                         "Phantom error: %s", reply + 5);
        }
        else
            g_warning ("Error: %s", reply + 5);
    }

    // Returning the final reply
    return reply;
}

/**
 * @brief Acquires the value of the attribute given by its name from the phantom
 *
 * Given the @p name of the attribute to be fetched, this function will talk to the phantom, by sending a get request
 * for that value and then return the response result.
 *
 * @author Matthias Vogelgesang
 *
 * @param priv
 * @param name
 * @return
 */
static gchar *
phantom_get_string_by_name (UcaPhantomCameraPrivate *priv, const gchar *name)
{
    GMatchInfo *info;
    gchar *request;
    gchar *cr;
    gchar *value = NULL;
    gchar *reply = NULL;

    gboolean regex_matches;

    // This will assemble the request command by using the "get" keyword and the given attribute name of the camera
    request = g_strdup_printf ("get %s\r\n", name);
    // Actually sending the request to the camera and receiving its reply.
    reply = phantom_talk (priv, request, NULL, 0, NULL);
    g_free (request);

    if (reply == NULL)
        return NULL;

    cr = strchr (reply, '\r');

    if (cr != NULL)
        *cr = '\0';

    // g_warning(reply);
    // This function will apply the response pattern defined using the phantom camera network protocol (the regex
    // format string is now stored in priv->response_pattern) to the reply from the camera.
    // The function returns a value of whether the match worked or not, but the actual info about the match is being
    // stored into the info object.
    regex_matches = g_regex_match (priv->response_pattern, reply, 0, &info);
    if (!regex_matches) {
        g_warning ("Cannot parse `%s'", reply);
        g_free (reply);
        return NULL;
    }

    // The return from the phantom camera always prepends the name of the attribute we asked for like this:
    // "defc.res: 1500 x 1000". Thus here we take the second value.
    value = g_match_info_fetch (info, 2);
    g_match_info_free (info);
    g_free (reply);

    return value;
}

/**
 * @brief Given the camera and the attribute to acqquire from it, this will return the string reponse
 *
 * This function will return the string response for getting the given vriable from it
 *
 * @param priv
 * @param var
 * @return
 */
static gchar *
phantom_get_string (UcaPhantomCameraPrivate *priv, UnitVariable *var)
{
    return phantom_get_string_by_name (priv, var->name);
}

/**
 * @brief Given the camera object gets the height and width of the images
 *
 * This function will send a request to the camera for its resolution property and then extract the height and width
 * integer values from the response string. The values will be passed out of this function using the two references
 * @p *width and @p *height passed to the function as arguments. The actual return value of  the function is a boolean
 * indicating the succcess of retrieving the values.
 *
 * @param priv
 * @param name
 * @param width
 * @param height
 * @return
 */
static gboolean
phantom_get_resolution_by_name (UcaPhantomCameraPrivate *priv,
                                const gchar *name,
                                guint *width,
                                guint *height)
{
    // For extracting the resolution from the returned result string, we are going to need yet another regex match,
    // because the resolution will be encoded in a string like "1500 x 1000", with the numbers being separated by an
    // x character
    GMatchInfo *info;
    gchar *result_string;

    if (width)
        *width = 0;

    if (height)
        *height = 0;

    // This will send the actual request for the resolution property to the phantom an return the response string sent
    // by the camera
    result_string = phantom_get_string_by_name (priv, name);

    // Of course if the camera response is not even a string we return FALSE, to indicate, that getting the resolution
    // failed
    if (result_string == NULL)
        return FALSE;

    // Also if the regex match on the given string fails, FALSE is also returned, because then again, getting the
    // resolution didnt work.
    if (!g_regex_match (priv->res_pattern, result_string, 0, &info)) {
        g_free (result_string);
        return FALSE;
    }

    if (width)
        // The width of the image will be in the first field of the regex match info object, but as a string so we need
        // to convert it to an integer first
        *width = atoi (g_match_info_fetch (info, 1));

    if (height)
        // The height of the image will be in the second field of the regex match info object, but as a string so we need
        // to convert it to an integer first
        *height = atoi (g_match_info_fetch (info, 2));

    g_free (result_string);
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

/**
 * @brief Returns the size of the expected data to be received for the given camera config
 *
 * Depending on the total amount of pixels in the image (width * height / resolution) and the used data transfer
 * format, this function will return the amount of bytes to expect from the transmission.
 *
 * @authors Matthias Vogelgesang, Jonas Teufel
 *
 * @param priv
 * @return
 */
static gsize
get_buffer_size (UcaPhantomCameraPrivate *priv)
{
    // This will hold the actual size of the buffer later on and wil be returned
    int buffer_size;

    // Depending on the transfer format used, the buffer needs a different size, because the formats use a different
    // amount of bits to represent a single pixel. Of course the basis of all the buffer sizes is the total amount of
    // pixels in the image (width * height) but the P16 uses 16 Bit (2 bytes), the P10 uses 10 bit (1.25 bytes) per
    // pixel etc. These will be the multipliers for the final size.
    switch (priv->format) {
        case IMAGE_FORMAT_P16:
            buffer_size = priv->roi_width * priv->roi_height * 2;
            break;
        case IMAGE_FORMAT_P10:
            buffer_size = (priv->roi_width * priv->roi_height * 5) / 4;
            break;
        case IMAGE_FORMAT_P12L:
            buffer_size = (priv->roi_width * priv->roi_height * 3) / 2;
            break;
        default:
            buffer_size = (priv->roi_width * priv->roi_height);
    }

    return buffer_size;
}

static void print_buffer(guint8 *buffer, int length) {
    char string[100000];
    char temp[20];
    for (int i = 0; i < length; i++) {
        sprintf(temp, "%02x ", buffer[i]);
        strcat(string, temp);
    }
    //g_warning("BUFFER: %s", string);
}


// *********************************************
// "NORMAL" NETWORK INTERFACE IMAGE TRANSMISSION
// *********************************************

/**
 * @brief Actually receives the image data for normal network connection
 *
 * This function receives all the image bytes from the given @p istream (the socket connected to the camera).
 * The finished bytes for the image are stored int the "buffer" of the @p priv camera.
 *
 * @author Matthias Vogelgesang
 *
 * @param priv
 * @param istream
 * @param error
 */
static void
read_data (UcaPhantomCameraPrivate *priv, GInputStream *istream, GError **error)
{
    gsize to_read;

    to_read = get_buffer_size (priv);

    // This loop exits after all the bytes of the image have been received. The "to_read" variable contains the amount
    // of bytes to be received in the beginning and is then step by step decremented by the amount, that has been
    // received.
    while (to_read > 0) {
        gsize bytes_read;

        if (!g_input_stream_read_all (istream, priv->buffer, to_read, &bytes_read, NULL, error))
            return;

        to_read -= bytes_read;
    }
}

/**
 * @brief Thread, which will listen for new data connections from phantom and receive image data.
 *
 * This function will create a new listening socket, waiting for the phantom to make a new data connection.
 * This thread is connected to the main program using a async message queue. This thread will receive image data until
 * the main program sends a stop message. The end of one transmission is indicated by this thread pushing a message
 * to the queue. The actual image data will be saved in the buffer of shared object @p priv
 *
 * @author Matthias Vogelgesang
 *
 * @param priv
 * @return
 */
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

    // Listening on the socket, waiting for the phantom to establish a new connection
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

    // In case a connection has been established on the listening port, we are extracting the IP address of the client,
    // that has connected (this will be the IP address of the phantom).
    remote_addr = g_socket_connection_get_remote_address (connection, NULL);
    inet_addr = g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (remote_addr));
    addr = g_inet_address_to_string (inet_addr);
    g_debug ("%s connected", addr);
    g_warning("%s connected", addr);
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

                // This function, actually does the job of receiving the bytes over the socket connection. The function
                // will be blocking, until all bytes have been received. The received image data will be saved into
                // the "priv->buffer".
                read_data (priv, istream, &result->error);

                // After all the image data has been received, a message indicating the success will be put into the
                // queue, so that the main thread knows, that it can retrieve the image data from the "buffer" now.
                result->type = RESULT_IMAGE;
                result->success = TRUE;
                g_async_queue_push (priv->result_queue, result);
                // g_warning("receive error %s", result->error);
                break;

            case MESSAGE_READ_TIMESTAMP:
                // not implemented
                break;

            case MESSAGE_STOP:
                // If a "stop message" has been put into the queue by the main thread, then the "stop" variable will
                // be set, which will break the loop and the whole function exits.
                stop = TRUE;
                break;
        }

        g_free (message);
    }

    //g_warning("EXITS THE RECEIVE LOOP");

    if (!g_io_stream_close (G_IO_STREAM (connection), NULL, &error)) {
        g_warning ("Could not close connection: %s\n", error->message);
        g_error_free (error);
    }

    g_object_unref (connection);

    return NULL;
}


// ******************************
// 10G NETWORK IMAGE TRANSMISSION
// ******************************

/**
 * @brief Returns the size of the transmitted image in bytes, using the P10 transfer format
 *
 * @author Jonas Teufel
 * @deprecated
 *
 * @param priv
 * @return
 */
int
P10_byte_size(UcaPhantomCameraPrivate *priv)
{
    // The "roi"(region of interest) fields of the camera object store the x and y resolution of the image to be
    // transmitted. The amount of pixels of the image is width times height, obviously.
    int pixel_amount = priv->roi_height * priv->roi_width;

    // The amount of bytes to be received for the 10G format is 10 bit per pixel, which comes down to (5/4) aka 1.25
    // bytes per pixel
    int bytes_amount = (pixel_amount * 5) / 4;
    //g_warning("height %i, width %i, pixels %i, bytes %i", priv->roi_height, priv->roi_width, pixel_amount, bytes_amount);

    return bytes_amount;
}

/**
 * @brief If the given block is finished it is being released back to the kernel space
 *
 * @author Jonas Teufel
 *
 * @param block_description
 * @param finished
 */
static void flush_block(struct block_desc *block_description, gboolean *finished) {

    // If the block has been completely processed (all payload data extracted from all the packages in it), then it has
    // to be flushed, meaning that it has to be "given back" to the kernel, so new data can be written to it.
    // unless it's status isn't changed, the kernel cannot write new packages into this block of the ring buffer.
    if (*finished) {
       block_description->h1.block_status = TP_STATUS_KERNEL;
    }
}

/**
 * @brief Extracts the data from one block in the ring buffer and adds it to the destination buffer.
 *
 * Given the @p destination buffer, where to store the image data in and the @p block_description of the currently
 * processed block of the ring buffer, this function will read out the payload data of every package inside this block
 * and copy it to the buffer.
 * The function returns the total amount of bytes it was able to fetch from this one block.
 *
 * @author Jonas Teufel
 *
 * @param block_description
 * @param destination
 * @param expected
 * @param total
 * @param finished
 * @param header
 * @return
 */
int process_block(
        struct block_desc *block_description,
        guint8 *destination,
        const int expected,
        gsize *total,
        gboolean *finished,
        struct tpacket3_hdr *header,
        int *packet_index)
{
    // Each block can hold multiple actual packets (frames). But the actual amount how many packets are in one block
    // depends on the size of the packet, speed of transmission etc. In general, the amount is not previously known,
    // but once the block is done writing, is stored inside the "num_pckts" of its descriptor.
    int packet_amount = block_description->h1.num_pkts;

    // This will store the total amount of payload(!) bytes received int the processed block and will also be returned
    int bytes = 0;

    // This will be the pointer directed at the start of the actual packet data! The data contained in a packet is
    // byte wise, which means its a unsigned 8 bit format.
    guint8 *data;
    // This will store the size of the packet's payload
    int length;
    int remaining;

    // g_warning("%u", destination);
    // This is the header, which will be used
    uint8_t buffer[2];
    short state = 0;

    // The finished boolean variable is an indicator of whether the currently processed block is finished or not.
    // We have to consider the following case: If the loop below break's because all the expected data has been
    // received for one package, there could possibly still be data of the next image in that ring buffer block.
    // and we need to indicate this to know, if we should start in this or the next block when attempting to get the
    // data for that next image.
    *finished = TRUE;

    int i;
    //g_warning("IDX: %i", *packet_index);
    for (i = *packet_index; i < packet_amount; i++) {
        // Calculation of the actual payload(!) length. The packages sent by the phantom have a overhead of 32 bytes!
        length = header->tp_snaplen - 32;

        // After exactly 94 bytes into the package the info about the used protocol can be extracted. And after 114
        // bytes the overhead ends and the actual payload starts.
        data = (guint8 *) header;
        //data += 94;
        //memcpy(buffer, data, 2);
        //data += 20;
        
        if (data[94] == 136 && data[95] == 183) {
            data += 114;
        // if (buffer[0] == 136 && buffer[1] == 183) {

            *total += length;

            // With this we copy all the data (using the complete length of the payload) onto the destination buffer (where
            // the final image data will be stored)
            memcpy (destination, data, length);

            // After that we need to increment the pointer of the destination buffer, so that with the next memcpy no
            // existing data will be overwritten, but instead be appended
            destination += length;
            bytes += length;

            // The amount of remaining bytes can be received
            remaining = expected - *total;

            if (length > remaining + 1492) {
                header = (struct tpacket3_hdr *) ((uint8_t *) header + header->tp_next_offset);
                break;
            }
        }

        // We are incrementing the loop by moving on to the location of the next packet
        header = (struct tpacket3_hdr *) ((uint8_t *) header + header->tp_next_offset);
    }

    // We need to pass to the outside at which packet index the loop ended, so we ca possibly resume processing at
    // this point. of course this only be the case if the block exits as unfinished.
    *packet_index = i + 1;

    // If we reach this point in time, if the loop above simply finished, than the block has been processed completely.
    // But in case the loop was broken, due to one complete image being received, we need to indicate, that this block
    // is not finished and that we need to resume processing it with the next call to this function
    
    if (packet_amount - 1 > i) {
        *finished = FALSE;
        //g_warning("packet amount: %i, IDX: %i", packet_amount, i);
    }
    
    return bytes;
}

/**
 * @brief Receives / reads the image data being transmitted over the 10G interface.
 *
 * @author Jonas Teufel
 *
 * @param priv
 * @param fd
 * @param ring
 * @param poll_fd
 * @param block_index
 * @param finished
 * @param header
 * @param error
 * @return
 */
static int
read_ximg_data (
        UcaPhantomCameraPrivate *priv,
        gint fd,
        struct ring *ring,
        struct pollfd *poll_fd,
        unsigned int block_index,
        gboolean *finished,
        int *packet_index,
        struct tpacket3_hdr *header,
        GError **error)
{
    // I assume the "priv->buffer" is the internal buffer of the PhantomCamera object and the FINISHED image, meaning
    // all bytes have been received has to be put into this buffer at the end.
    guint8 *dst = priv->buffer;
    //g_warning("DESTINATION POINTER: %u, PRIV-BUFFER POINTER: %u", dst, priv->buffer);

    // With this we keep track of how many bytes have already been received.
    gsize total = 0;
    int bytes;
    int remaining;
    
    unsigned long header_address;

    // This is the amount of bytes that has to be received, based on the resolution of the image and the structure of
    // the 10G transfer format (always P10).
    int expected_bytes = get_buffer_size(priv);

    // This struct will contain all the necessary iformation about the block of the ring buffer, that is currently
    // being processed
    struct block_desc *block_description;

    //unsigned int block_index = 0;
    unsigned int block_amount = 64;
    struct timespec tstart={0,0}, tend={0,0};
    struct timespec pstart={0,0}, pend={0,0};
    
    //clock_gettime(CLOCK_MONOTONIC, &tstart);
    while (total < expected_bytes) {
        
        // Creating the block description for the current block index from the ring buffer.
        block_description = (struct block_desc *) ring->rd[block_index].iov_base;
        
        // Polling.
        // Once the kernel has finished writing to a block of the buffer (either because it is full, or because the
        // timer ran out), this block is being released to the user space (-> this program) and only then we can
        // read it. So the program execution of the loop will be skipped here, if the next block has not yet been
        // released to the user space.
        
        if ((block_description->h1.block_status & TP_STATUS_USER) == 0) {
            //clock_gettime(CLOCK_MONOTONIC, &pstart);
            poll(poll_fd, 1, -1);
            //clock_gettime(CLOCK_MONOTONIC, &pend);
            //g_warning("PROFILE DELTA %.5f",((double)pend.tv_sec + 1.0e-9*pend.tv_nsec) - ((double)pstart.tv_sec + 1.0e-9*pstart.tv_nsec));
            continue;
        }
        
        // In case block was finished during the previous run of this function, the header struct will be created
        // to point to the first packet of the current (new) block.
        // Although if it wasn't finished the header for the packet, where the last loop left of will be reused.
        if (*finished == TRUE) {
            header = (struct tpacket3_hdr *) ((uint8_t *) block_description + block_description->h1.offset_to_first_pkt);
            // of course for an entirely new block we start processing the first packet.
            *packet_index = 0;
        } else {
            header = header_address;
            header = (struct tpacket3_hdr *) ((uint8_t *) header + header->tp_next_offset);
        }

        // Actually extracting the data of the packages in that block into the destination buffer.
        
        
        bytes = process_block(block_description, dst, expected_bytes, &total, finished, header, packet_index);
        flush_block(block_description, finished);
        header_address = header;
        // We need to increment the buffer pointer address
        dst += bytes;

        // If the block is not yet finished to be processed we cannot increment the index, so that with the next call
        // of this function the rest of the unfinished block will be processed first.
        if (*finished == TRUE) {
            // Going to the next block. If the last block has been reached, it starts with the first block again,
            // after all this is how a ring buffer works.
            block_index = (block_index + 1) % block_amount;
        } 
    }
    //clock_gettime(CLOCK_MONOTONIC, &tend);
    //g_warning("TIME DELTA %.5f", ((double)tend.tv_sec + 1.0e-9*tend.tv_nsec) - ((double)tstart.tv_sec + 1.0e-9*tstart.tv_nsec));

    // g_warning("AFTER\nDESTINATION POINTER: %u, PRIV-BUFFER POINTER: %u", dst, priv->buffer);
    return block_index;

}

/**
 * @brief sets up the raw socket to be used for the ethernet frames, using the ring buffer
 *
 * @author Jonas Teufel
 *
 * @param ring
 * @param netdev
 * @return
 */
static int setup_raw_socket(struct ring *ring, char *netdev) {

    guint sock_opt;
    // This will be the variable, into which we are saving the exit codes of all the functions. These functions will
    // return a negative int (-1) if an error occurred.
    int err;
    // This is the file descriptor int, which will later hold the actual socket.
    int fd;
    // Here we specify which version of the "packet_mmap(2)" ring buffer we are using. In this
    // case it is version 3
    int version = TPACKET_V3;

    // The ring buffer consist of multiple so called blocks. And each block will hold multiple "frames" (The actual
    // packets send over the network). Here we define How many bytes are assigned to one block and how many bytes one
    // frame can consume. Also we define the amount of blocks the ring buffer is supposed to have.
    // These values will later be used to define (the size of) the ring buffer struct.
    unsigned int block_size = 1 << 14; // 22
    unsigned int frame_size = 1 << 8;  // 11
    unsigned int block_amount = 64;
    // The amount of frames is directly derived from the previous config.
    unsigned int frame_amount = (block_size * block_amount) / frame_size;

    // the total size of the ring is the size of all blocks combined
    unsigned int ring_size = block_size * block_amount;

    // The socket needs to know where it is operating using a socketaddr. Usually with a TCP socket for example this
    // would be a combination of a IP and PORT to listen on. But raw sockets use the name if the INTERFACE (ethernet).
    // This struct, will represent a socket address, to which the listening/receiving socket will be bound.
    struct sockaddr_ll ll;


    // Here we are creating the actual RAW socket
    fd = socket(AF_PACKET, SOCK_RAW, htons(PROTOCOL));
    if (fd < 0) {
        g_warning("Raw Socket creation failed");
    }

    // reuse socket
    setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof (sock_opt));

    // Now we are telling the socket, that it is supposed to be used in combination with a ring buffer, by configuring
    // the option for which packet_mmap version is being used.
    err = setsockopt(fd, SOL_PACKET, PACKET_VERSION, &version, sizeof(version));
    if (err < 0) {
        g_warning("Error while setting socket options");
    }

    // Here we are actually setting up the ring buffer, by telling it how many blocks/frames and their sizes it is
    // supposed to have.
    // "req" for "requirement"?
    memset(&ring->req, 0, sizeof(ring->req));
    ring->req.tp_block_size         = block_size;
    ring->req.tp_frame_size         = frame_size;
    ring->req.tp_block_nr           = block_amount;
    ring->req.tp_frame_nr           = frame_amount;
    ring->req.tp_retire_blk_tov     = 1;
    ring->req.tp_feature_req_word   = TP_FT_REQ_FILL_RXHASH;
    // Assigning the ring to the socket
    err = setsockopt(fd, SOL_PACKET, PACKET_RX_RING, &ring->req, sizeof(ring->req));
    if (err < 0) {
        g_warning("Error while assigning the ring to the socket");
    }
    // This is the actual memory mapping for the ring, previously it was just setting up configuration
    ring->map = mmap(NULL, ring_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, fd, 0);
    if (ring->map == MAP_FAILED) {
        g_warning("Error allocating ring memory");
    }

    // ?
    // As far as I have understood it, here we are defining the starting positions of all the blocks in memory
    // When calling "ring->rd[0].iov_base" for example, that will give us a pointer to the start of the first (index 0)
    // block.
    ring->rd = malloc(ring->req.tp_block_nr * sizeof(*ring->rd));
    for (int i = 0; i < ring->req.tp_block_nr; ++i) {
        ring->rd[i].iov_base = ring->map + (i * ring->req.tp_block_size);
        ring->rd[i].iov_len = ring->req.tp_block_size;
    }

    // BINDING THE SOCKET

    // "netdev" is the name of the interface to be used, but here we need the index of this interface:
    int interface_index = if_nametoindex(netdev);
    // Configuration of the socket address to be bound to
    memset(&ll, 0, sizeof(ll));
    ll.sll_family       = PF_PACKET;
    ll.sll_protocol     = htons(PROTOCOL);
    ll.sll_hatype       = 0;
    ll.sll_pkttype      = 0;
    ll.sll_halen        = 0;
    ll.sll_ifindex      = interface_index;

    err = bind(fd, (struct sockaddr *) &ll, sizeof(ll));
    if (err < 0) {
        g_warning("Error binding the socket to the interface");
    }

    return fd;
}

/**
 * @brief Porperly closing the used ring buffer and socket at the end of the program
 *
 * @author Jonas Teufel
 *
 * @param ring
 * @param fd
 */
static void teardown_raw_socket(struct ring *ring, int fd) {

    // The complete size of the ring
    unsigned int ring_size = ring->req.tp_block_size * ring->req.tp_block_nr;
    // This command will delete the memory mapping for the ring, for that it needs the pointer to the start of the
    // memory map (ring->map) and the ring
    munmap(ring->map, ring_size);

    // During the setup phase, we dynamically allocated memory (array) to store the information for the starting points
    // of the blocks inside the ring buffer. This allocated memory has to be free'ed
    free(ring->rd);

    // Closing the socket properly
    close(fd);
}

/**
 * @brief The Thread, which receives the images using the 10G network
 *
 * @author Jonas Teufel
 *
 * @param priv
 * @return
 */
static gpointer
accept_ximg_data (UcaPhantomCameraPrivate *priv)
{
    Result *result;
    gint fd;
    gboolean stop = FALSE;

    struct ifreq if_opts = {0,};

    result = g_new0(Result, 1);
    result->type = RESULT_READY;
    result->success = FALSE;

    struct ring ring;
    struct pollfd poll_fd;

    // This function completely configures the raw socket to be used.
    memset(&ring, 0, sizeof(ring));
    fd = setup_raw_socket(&ring, INTERFACE);

    memset(&poll_fd, 0, sizeof(poll_fd));
    poll_fd.fd      = fd;
    poll_fd.events  = POLLIN | POLLERR;
    poll_fd.revents = 0;

    // The ximg command to send to the phantom needs the MAC address of the ethernet interface to send to (the one this
    // program is using) as a parameter. So we are getting this here.
    strncpy (if_opts.ifr_name, priv->iface, strlen (priv->iface));
    ioctl (fd, SIOCGIFHWADDR, &if_opts);

    priv->mac_address[0] = if_opts.ifr_hwaddr.sa_data[0];
    priv->mac_address[1] = if_opts.ifr_hwaddr.sa_data[1];
    priv->mac_address[2] = if_opts.ifr_hwaddr.sa_data[2];
    priv->mac_address[3] = if_opts.ifr_hwaddr.sa_data[3];
    priv->mac_address[4] = if_opts.ifr_hwaddr.sa_data[4];
    priv->mac_address[5] = if_opts.ifr_hwaddr.sa_data[5];
    
    result->success = TRUE;
    g_async_queue_push (priv->result_queue, result);

    //g_warning("10G setup complete");
    unsigned int block_index = 0;
    struct tpacket3_hdr *header;
    gboolean finished = TRUE;
    int packet_index = 0;

    while (!stop) {
        InternalMessage *message;

        result = g_new0 (Result, 1);
        message = g_async_queue_pop (priv->message_queue);

        switch (message->type) {
            case MESSAGE_READ_IMAGE:

                // Here we are calling the function, which actually uses the socket to receive the image piece by piece
                // The actual image will be saved in the buffer of the camra object's "priv" internal buffer
                // "priv->buffer".
                block_index = read_ximg_data(priv, fd, &ring, &poll_fd, block_index, &finished, &packet_index, header, &result->error);

                // Once the image was completely received we push a new message, indicating that image reception was a
                // success, into the queue, so that the main thread which is watching the queue can retrieve the image
                // from the buffer.
                result->type = RESULT_IMAGE;
                result->success = TRUE;

                // g_warning("ERROR: %s", result->error);
                g_async_queue_push (priv->result_queue, result);
                //g_warning("PUSHED RESULT ");
                break;

            case MESSAGE_READ_TIMESTAMP:
                // Not implemented
                break;

            case MESSAGE_STOP:
                stop = TRUE;
                break;
        }

    }

    // Closing socket connection and freeing dynamically allocated memory etc
    //g_warning("TEARING DOWN");
    teardown_raw_socket(&ring, fd);
    return NULL;
}

/**
 * @brief Actually receives the image data for 10G network, using raw ethernet frames
 *
 * This function uses a raw socket @p fd to receive ethernet frames, then extracts the payload from it and saves
 * the complete image data into the "buffer" of the given camera @p priv.
 *
 * @deprecated
 *
 * @param priv
 * @param fd
 * @param error
 */
static void
_read_ximg_data (UcaPhantomCameraPrivate *priv,
                 gint fd,
                 GError **error)
{
    //g_warning("ATTEMPTING TO RECEIVE RAW DATA");

    // This is the buffer, in which the received data will be saved into
    guint8 buffer[10000];

#if CHECK_ETHERNET_HEADER
    const struct ether_header *eth_h = (struct ether_header *) buffer;
#endif

    // I assume the "priv->buffer" is the internal buffer of the PhantomCamera object and the FINISHED image, meaning
    // all bytes have been received has to be put into this buffer at the end.
    guint8 *dst = priv->buffer;

    // With this we keep track of how many bytes have already been received.
    gsize total = 0;

    const gsize header_size = 32;
    const gsize end_of_frame_size = 4;
    const gsize overhead = header_size + end_of_frame_size;

    /* XXX: replace recvfrom with the ring buffer code by Radu Corlan */

    // IMPORTANT
    // This loop essentially keeps track of how many bytes of the image have already been received and ends after X
    // bytes have been received. At the moment the X bytes are a constant number, but they strongly depend on which
    // resolution has been set to the camera and should be computed dynamically!
    while (total < 2500000) {
        gsize size;
        //g_warning("TOTAL %u", total);
        size = recvfrom (fd, buffer, sizeof(buffer), 0, NULL, NULL); // Problem is here
        if (size < 0)
            break;

        // Here we extract the two bytes from the ethernet header, which contain info about which protocol is being
        // used. If the protocol does not match the protocol of the phantom, the data is being discarded.
        guint8 a = buffer[12];
        guint8 b = buffer[13];
        // g_warning("%u %u", a, b);
        if (a != 0 && b != 0) {
            continue;
        }

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

        // With this the actual payload from the ethernet frame is being copied into the "dst" which points to
        // "priv->buffer" (which is where we want the final image to be).
        // buffer + header_size configures the pointer to the buffer array in such a way, that the pointer starts at
        // the actual payload of the package and skips the header bytes.
        memcpy (dst, buffer + header_size, size - overhead);

        // Updating the pointer to the "dst" array in such a way, that the next copy process appends the new data at
        // the end of the current data and not overwrites it.
        dst += size - overhead;
        total += size - overhead;
    }
}

/**
 * @brief Thread, which will listen for raw ethernet frames and receive image data.
 *
 * This function will create a raw socket, receiving all the raw ethernet frames on the ethernet interface that is
 * defined in @p priv 's configuration.
 * This thread is connected to the main program using a async message queue. This thread will receive image data until
 * the main program sends a stop message. The end of one transmission is indicated by this thread pushing a message
 * to the queue. The actual image data will be saved in the buffer of shared object @p priv
 *
 * @deprecated
 *
 * @param priv
 * @return
 */
static void
_accept_ximg_data (UcaPhantomCameraPrivate *priv)
{
    //g_warning("ACCEPTING 10G DATA");
    Result *result;
    gint fd;
    gint sock_opt;
    struct ifreq if_opts = {0,};
    gboolean stop = FALSE;

    result = g_new0 (Result, 1);
    result->type = RESULT_READY;
    result->success = FALSE;

    // Before htons was 0x88B7
    // htons(ETH_P_ALL) simply tells the socket, that it is supposed to receive all ethernet frames, regardless of what
    // interface they are using. This is mainly bad, because then we have to filter all the packets, that do not belong
    // to the image transmission ourselves in the code, but this was the only thing, that made it work
    fd = socket (PF_PACKET, SOCK_RAW, htons (ETH_P_ALL));

    if (fd == -1) {
        g_set_error_literal (&result->error, UCA_CAMERA_ERROR, UCA_CAMERA_ERROR_DEVICE,
                             "Could not open raw socket");
        g_async_queue_push (priv->result_queue, result);
        return;
    }

    // The ximg command to send to the phantom needs the MAC address of the ethernet interface to send to (the one this
    // program is using) as a parameter. So we are getting this here.
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

    g_warning ("Accepting raw ethernet frames ...");
    result->success = TRUE;
    g_async_queue_push (priv->result_queue, result);

    while (!stop) {
        InternalMessage *message;

        result = g_new0 (Result, 1);
        message = g_async_queue_pop (priv->message_queue);

        switch (message->type) {
            case MESSAGE_READ_IMAGE:

                // Here we are calling the function, which actually uses the socket to receive the image piece by piece
                // The actual image will be saved in the buffer of the camra object's "priv" internal buffer
                // "priv->buffer".
                _read_ximg_data (priv, fd, &result->error);

                // Once the image was completely received we push a new message, indicating that image reception was a
                // success, into the queue, so that the main thread which is watching the queue can retrieve the image
                // from the buffer.
                result->type = RESULT_IMAGE;
                result->success = TRUE;
                // g_warning("ERROR: %s", result->error);
                g_async_queue_push (priv->result_queue, result);
                break;

            case MESSAGE_READ_TIMESTAMP:
                // Not implemented
                break;

            case MESSAGE_STOP:
                stop = TRUE;
                break;
        }

        g_free (message);
    }

    // At the end we properly close the socket.
    close (fd);
}


// *********************************
// STARTING AND STOPPING THE READOUT
// *********************************

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

        // Start the socket connection

        return;
    }

    g_free (priv->buffer);
    //priv->buffer = g_malloc0 (get_buffer_size (priv));
    priv->buffer = g_malloc0(9000000);

    if (priv->enable_10ge) {
        //g_warning("THIS IS WRONG NO 10G");
        priv->accept_thread = g_thread_new (NULL, (GThreadFunc) accept_ximg_data, priv);

        result = (Result *) g_async_queue_pop (priv->result_queue);
        g_assert (result->type == RESULT_READY);

        if (result->error != NULL) {
            g_propagate_error (error, result->error);
            g_free (result);
            return;
        }

        g_free (result);

        /* no startdata necessary for ximg */
    }
    else {
        gchar *reply;
        g_warning("DATA CONNECTION STARTED");
        const gchar *request = "startdata {port:7116}\r\n";

        /* set up listener */
        g_socket_listener_add_inet_port (priv->listener, 7116, G_OBJECT (camera), error);
        priv->accept = g_cancellable_new ();
        priv->accept_thread = g_thread_new (NULL, (GThreadFunc) accept_img_data, priv);

        /* wait for listener to become ready */
        result = (Result *) g_async_queue_pop (priv->result_queue);
        g_assert (result->type == RESULT_READY);

        if (result->error != NULL) {
            g_propagate_error (error, result->error);
            g_free (result);
            return;
        }

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
    //g_warning("STOP READOUT");

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
uca_phantom_camera_start_recording (UcaCamera *camera,
                                    GError **error)
{
    uca_phantom_camera_start_readout(camera, error);

    //g_warning("START RECORDING");

    const gchar *rec_request = "rec 1\r\n";
    const gchar *trig_request = "trig\r\n";

    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));
    //g_free (phantom_talk (UCA_PHANTOM_CAMERA_GET_PRIVATE (camera), rec_request, NULL, 0, error));
    /* TODO: check previous error */
    //g_free (phantom_talk (UCA_PHANTOM_CAMERA_GET_PRIVATE (camera), trig_request, NULL, 0, error));
}

static void
uca_phantom_camera_stop_recording (UcaCamera *camera,
                                   GError **error)
{
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));

    // Stop the readout
    uca_phantom_camera_stop_readout(camera, error);

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
unpack_p10 (guint16 *output,
             const guint8 *input,
             const guint num_pixels)
{
    guint i, j, h, k;

    // guint16 current;

    guint64 temp;

    for (i = 0, j = 0; j < num_pixels; i += 5, j += 4) {

        // Assemble the long number
        temp = 0;
        for (k = 0; k < 5; k++) {
            temp |= input[i + k];
            temp <<= 8;
        }
        temp >>= 8;

        // Extract the bytes from that big number
        for (h = 0; h < 4; h++) {
            const guint16 current = (guint16) (temp & 0b1111111111);
            temp >>= 10;
            output[j + (3 - h)] = current;
        }
    }
    // g_warning("i:%i j:%i", i, j);
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

int a = 0;

// ***********************************************
// MAIN INTERFACE FUNCTION FOR RETRIEVING AN IMAGE
// ***********************************************



/**
 * @brief Sends the instruction to transmit an image to the phantom and receives the image in separate thread
 *
 * This function will assemble the command string to be sent to the phantom based on the settings of the @p camera
 * object. (The string needs to be different, depending on whether we use normal or 10G connection).
 * This instruction is then send to the camere, issuing it to send the raw byte data to the previously established
 * data connection (This is important! A secondary channel for data transmission has to be established before calling
 * this function).
 * This function will also decode the transfer format into a usable image format. The resulting image will be stored in
 * the buffer of the camera object.
 *
 * @param camera
 * @param data
 * @param error
 * @return
 */
static gboolean
uca_phantom_camera_grab (UcaCamera *camera,
                         gpointer data,
                         GError **error)
{
    //g_warning("ATTEMPTING TO GRAB");
    UcaPhantomCameraPrivate *priv;
    InternalMessage *message;
    Result *result;
    gchar *request;
    gchar *reply;
    gchar *additional;
    const gchar *command;
    const gchar *format;

    // This is the basic layout of the command string, that has to be send to the phantom camera to obtain an image
    // The first "%s" is for the specific command identifier: "img" for normal network and "ximg" for 10G transmission
    // the second is for the format identifier to specify the transfer format (how many bytes per pixel used)
    // The last one is for optional additional parameters (only needed for the ximg commad)
    const gchar *request_fmt = "%s {cine:1, start:0, cnt:4, fmt:%s %s}\r\n";
    gboolean return_value = TRUE;

    //g_warning("HERE TO GRAB");

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    if (priv->enable_10ge && priv->iface == NULL) {
        g_set_error_literal (error, UCA_CAMERA_ERROR, UCA_CAMERA_ERROR_DEVICE,
                             "Trying to use 10GE but no valid MAC address is given");
        return FALSE;
    }
    //g_warning("CREATED PRIVATE OBJECT");

    // This is the main function called when an image is to be retrieved. The actual process of receiving the image
    // using network sockets etc will be done in a separate thread though. The two programs communicate with an async
    // message queue.
    // Here we push a message, indicating that the thread is supposed to start receiving the image now.
    message = g_new0 (InternalMessage, 1);
    message->data = data;
    message->type = MESSAGE_READ_IMAGE;
    g_async_queue_push (priv->message_queue, message);
    //g_warning("PUSHED THE MESSAGE REQUEST INTO QUEUE");

    // 27.03.2019
    // The 10G connection ALWAYS ONLY uses the P10 image transfer format
    if (priv->enable_10ge) priv->format = IMAGE_FORMAT_P10;
    // Depending on the chosen transfer format, we need to create an according string, which is being used as a
    // parameter of the img request, that is being sent to the camera.
    switch (priv->format) {
        case IMAGE_FORMAT_P16:
            format = "P16";
            break;
        case IMAGE_FORMAT_P12L:
            format = "P12L";
            break;
        case IMAGE_FORMAT_P10:
            format = "P10";
            break;
    }

    // Of course depending on whether 10G transmission is enabled in the settings, the command to initiate the
    // transmission has to be assembled differently.
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

    // Here we assemble the final string to be sent to the phantom, using the specific command, format and additional
    // parameters computed from the settings in the code above.
    request = g_strdup_printf (request_fmt, command, format, additional);

    if (priv->enable_10ge)
        g_free (additional);

    /* send request */
    reply = phantom_talk (priv, request, NULL, 0, error);
    //g_warning("IMG REQUEST SEND %s", reply);
    g_free (request);

    if (reply == NULL)
        return FALSE;

    g_free (reply);

    // Here the call to pop a message from the queue will be blocking the program execution, until the thread actually
    // puts a message into the queue, indicating, that it is now finished receiving the image data.
    //g_warning("ATTEMPTING TO POP THE RESULTS");
    result = g_async_queue_pop (priv->result_queue);
    //g_warning("IMAGE TRANSFER FINISHED");
    g_assert (result->type == RESULT_IMAGE);
    return_value = result->success;

    // The result of the transmission will be a long series of bytes. An actual image can only be reconstructed from
    // these bytes with the knowledge of the resolution of the image (how many pixels were there in total) and the used
    // transfer format (how many bytes used per pixel)
    if (result->success) {
        switch (priv->format) {
            case IMAGE_FORMAT_P16:
                unpack_p10(data, priv->buffer, priv->roi_width * priv->roi_height);
                break;
            case IMAGE_FORMAT_P12L:
                unpack_p12l (data, priv->buffer, priv->roi_width * priv->roi_height);
                break;
            case IMAGE_FORMAT_P10:
                unpack_p10(data, priv->buffer, priv->roi_width * priv->roi_height);
                break;
        }
    }

    if (result->error != NULL)
        g_propagate_error (error, result->error);

    g_free (result);
    //g_warning("RETURNING IMAGE");

    // The returned value will be boolean indicating the success (TRUE) of the image retrieval process.
    return return_value;
}

// This is pretty much not even needed
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


// *************************************
// GETTING AND SETTING CAMERA ATTRIBUTES
// *************************************

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


// ****************************************
// FINALIZING THE CAMERA / END OF PROGRAM ?
// ****************************************

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


// **************************
// PHANTOM DISCOVERY PROTOCOL
// **************************

/**
 * @brief Returns the network address of the phantom camera
 *
 * Usually the phantom cameras have a broadcast discovery protcol, so the code doesnt have to know their IP address.
 * That is not working at the moment though. So instead of executing the discovery routine, this function returns the
 * hardcoded IP address defined at the top of this file.
 *
 * @param error
 * @return
 */
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

    port = 7115;
    result = g_inet_socket_address_new (g_inet_address_new_from_string (IP_ADDRESS), port);
    return result;

    if (socket == NULL) {
        result = FALSE;
        goto cleanup_discovery_addr;
    }

    g_socket_set_broadcast (socket, TRUE);

    if (g_socket_send_to (socket, bcast_socket_addr, request, sizeof (request), NULL, error) < 0)
        goto cleanup_discovery_socket;

    g_warning("IT HAS SENT THE REQUEST");

    if (g_socket_receive_from (socket, &remote_socket_addr, reply, sizeof (reply), NULL, error) < 0)
        goto cleanup_discovery_socket;

    g_warning("IT HAS RECEIVED A RESPONSE");

    g_debug ("Phantom UDP discovery reply: `%s'", reply);
    regex = g_regex_new ("PH16 (\\d+) (\\d+) (\\d+)", 0, 0, error);

    if (!g_regex_match (regex, reply, 0, &info)) {
        g_set_error (error, UCA_CAMERA_ERROR, UCA_CAMERA_ERROR_DEVICE,
                     "`%s' does not match expected reply", reply);
        goto cleanup_discovery_addr;
    }

    port_string = g_match_info_fetch (info, 1);
    //port = atoi (port_string);

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


// **********************************
// CAMERA OBJECT INITIALIZATION STUFF
// **********************************

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
            IMAGE_FORMAT_P16, G_PARAM_READWRITE);

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
    g_warning("HELLO");
    UcaPhantomCameraPrivate *priv;

    self->priv = priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (self);

    priv->construct_error = NULL;
    priv->client = g_socket_client_new ();
    priv->listener = g_socket_listener_new ();
    priv->connection = NULL;
    priv->accept = NULL;
    priv->buffer = NULL;
    priv->features = NULL;
    priv->format = IMAGE_FORMAT_P16;
    priv->acquisition_mode = ACQUISITION_MODE_STANDARD;
    priv->enable_10ge = X_NETWORK;
    priv->iface = INTERFACE;
    priv->have_ximg = X_NETWORK;
    priv->message_queue = g_async_queue_new ();
    priv->result_queue = g_async_queue_new ();

    priv->response_pattern = g_regex_new ("\\s*([A-Za-z0-9]+)\\s*:\\s*{?\\s*\"?([A-Za-z0-9\\s]+)\"?\\s*}?", 0, 0, NULL);

    priv->res_pattern = g_regex_new ("\\s*([0-9]+)\\s*x\\s*([0-9]+)", 0, 0, NULL);

    uca_camera_register_unit (UCA_CAMERA (self), "frame-delay", UCA_UNIT_SECOND);
    uca_camera_register_unit (UCA_CAMERA (self), "sensor-temperature", UCA_UNIT_DEGREE_CELSIUS);
    uca_camera_register_unit (UCA_CAMERA (self), "camera-temperature", UCA_UNIT_DEGREE_CELSIUS);
    g_warning("finished init");
}

G_MODULE_EXPORT GType
camera_plugin_get_type (void)
{
    return UCA_TYPE_PHANTOM_CAMERA;
}
