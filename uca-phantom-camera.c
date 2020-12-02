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
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
//#include <math.h>

#include <gio/gio.h>
#include <gmodule.h>
#include <string.h>
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
#include <uca/uca-camera.h>
#include "uca-phantom-camera.h"

// SSE(128) instructions AVX(256); library intrisincs 
// UCA UFO SSE 



#include <unistd.h>

// TODO:

//// NOTE: JUST ACCESS THE trigger-source directly over g_object_get (important)

//// Post trigger frames setzen setzt auch gleichzeitig memread count

// TRIGGER FLANKE EVTL EINSTELLEN (low)

//// PROPERTY DIE MAX COUNT AUSLIEST

// ERRORS FOR MAX ROI SIZE ALSO BOUNDRIES WITH ROI OFSETS (medium)

//// BEVORE MEMREAD REQUEST SEND CHECK IF AMOUNT OF READ FRAMES UP TO THIS POIMNT IS ENOUGH FOR CHUNK SIZE

// SENSOR PIXEL HEIGHT AUCH VON DER CAMERA AUSLESEN (low)

//// PUBLIC GITHUB ZIEHEN (medium)

//// DOCUMENTATION SCHREIBEN (medium)

//// KEINE ROOT RECHTE FUER PACKET_MAP brauchen? CAP_NET_RAW permission setting for user
//// CAP_NET_RAW, CAP_IPC_LOCK


// **************************************
// HARDCODING CONFIGURATION OF THE CAMERA
// **************************************

#define PROTOCOL        ETH_P_ALL
//#define PROTOCOL        0x88b7

// 26.06.2019
// Changed the Chunk size from 400 to 100, because after testing with the 2048 pixel width image settings. 400 images
// cause the ring buffer to overflow.
#define MEMREAD_CHUNK_SIZE  5

// 04.11.2019
// This macro will define the index which will be used as the start index for the very first packet request of the
// memread requests
#define INTERNAL_START_INDEX 0

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

uint8_t zero[4] = {0,0,0,0};
uint8_t *zero_pointer = (uint8_t *) zero;

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

typedef union {
    uint8_t *in;
    uint64_t *out;
} xbuffer;


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
    // 26.05.2019
    // Adding this additional property for the IP address string. This can be used to manually assign the IP address
    // in case the discovery protocol does not work
    PROP_NETWORK_ADDRESS,
    // 11.06.2019
    // Instead of using the "start_recording" function to connect to the camera (this would break existing programs
    // using libuca with other cameras), The camera is now connected by setting a boolean flag to True
    PROP_CONNECT,
    // This will be a boolean value, that returns, whether or not the triggered frame acquisition is done yet or not
    PROP_TRIGGER_RELEASED,

    // 07.05.2019
    // Introducing an additional mode of operation for the camera: "memread" mode.
    // In this mode the camera is not actively recording. Instead the memory of the camera is being
    // read. The amount of frames to be read can be specified and each consecutive call to the 
    // "grab" function of the camera after entering this call will be used to read one of these images.
    PROP_ENABLE_MEMREAD,
    PROP_MEMREAD_CINE,
    PROP_MEMREAD_START,
    PROP_MEMREAD_COUNT,
    // 30.06.2019
    // This property will be used to set the "cam.aux1mode" attribute of the camera. This is an integer property, where
    // the integer value set configures what functionality the first auxiliary port of the camera will have
    PROP_AUX_ONE_MODE,
    // 16.07.2019
    // This property will contain the boolean flag, which will indicate if a external trigger will be used for the
    // recording. If not strictly software triggers enabled
    PROP_EXTERNAL_TRIGGER,
    // 22.07.2019
    // This property is a boolean flag, which will be used to enable and disable memgate mode. This is one of the
    // special programmable functions of the first auxiliary port. When it is enabled no frames are being
    // saved to memory if the port 1 of the camera is put to HIGH
    PROP_ENABLE_MEMGATE,
    PROP_HARDWARE_MEMGATE_ENABLED,
    PROP_AUX_ONE_PARAMETERS,
    // 05.11.2019
    // PROP_FRAME_SIZE will be used to store the value of "c1.frsize", which is the size of a single frame within the
    // first cine partition. PROP_MEMORY_SIZE will hold the value of "c1.frspace", which is the total space within the
    // cine. PROP_MAX_FRAMES will be a computed property from the two previous ones and it will tell, what the maxmimum
    // amount of frames is to be put into the cine.
    PROP_FRAME_SIZE,
    PROP_MEMORY_SIZE,
    PROP_MAX_FRAMES,
    // 11.11.2019
    // This is the property, which holds the numeric index for which acquisition mode is currently active
    PROP_ACQUISITION_MODE_INDEX,

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
    PROP_TRIGGER_SOURCE,
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

// 06.04.2019
// Added the additional attribute 10g_buffer, which will be used to store the unpacked data (with the transfer format
// already decoded into the pixel values)
struct _UcaPhantomCameraPrivate {
    GError              *construct_error;
    gchar               *host;
    GSocketClient       *client;
    GSocketConnection   *connection;
    GSocketListener     *listener;
    GCancellable        *accept;
    GThread             *accept_thread;
    GThread             *unpack_thread;
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
    gchar               *ip_address;
    guint8               mac_address[6];
    ImageFormat          format;
    AcquisitionMode      acquisition_mode;

    UcaCameraTriggerSource uca_trigger_source;
    // 11.06.2019
    // The boolean flag indicating if the plugin is actaully connected (via socket on the ethernet interface) to the
    // camera
    gboolean             connected;

    struct block_desc   *xg_current_block;
    gsize                xg_total;
    gsize                xg_expected;
    gboolean             xg_block_finished;
    gboolean             xg_packet_skipped;
    gint                 xg_block_index;
    gint                 xg_packet_index;
    gint                 xg_data_index;
    struct tpacket3_hdr *xg_packet_header;
    
    xbuffer              xg_data_buffer;
    uint8_t             *xg_data_in;
    uint16_t            *xg_buffer;
    gint                 xg_buffer_index;
    guint8              *xg_packet_data;
    guint8               xg_remaining_data[40];
    gsize                xg_packet_length;
    gsize                xg_remaining_length;
    
    // 29.05.2019
    // We need to keep track of the amount of packages inside a block of the ring buffer as an attribute of the camera
    // object, because it is used in several different methods.
    gsize                xg_packet_amount;

    gint                 xg_unpack_length;
    gint                 xg_unpack_index;

    // 10.05.2019
    // These are the attributes needed for the "memread" mode.
    // This mode can be entered by setting a boolean flag on the camera object. In this mode an amount of frames to
    // be read can be specified and after the first "grab" call, the camera will send a continuous stream of images
    // (faster image transfer mode, for reading images already stored in the cameras memory)
    gboolean             enable_memread;
    gboolean             memread_request_sent;
    guint                memread_count;
    guint                memread_cine;      // not implemented yet
    guint                memread_start;     // not implemented yet
    // 29.05.2019
    guint                memread_remaining;
    guint                memread_index;
    // 11.06.2019
    // This is the index, that is being incremented by the unpacking thread, when the program is in memread mode.
    // The theory is to delay the sending of the next chunk request until the last image has been unpacked.
    // To hopefully not overflow the ring buffer
    guint                memread_unpack_index;
    // 30.06.2019
    // The aux1mode is a property of the camera, which defines the function of the first configurable auxiliary port
    // of the camera.
    guint                aux1mode;
    // 16.07.2019
    // This flag will indicate, whether an external trigger source is to be used. If an external trigger source is to
    // be used the "rec" command, which has to be sent in preparation of a trigger, will be sent during the
    // "start_recording" process.. Otherwise the "rec" command will be sent together with the software trigger command
    gboolean             triggered_externally;
};

typedef struct  {
    const gchar *name;
    GType        type;
    GParamFlags  flags;
    gint         property_id;
    gboolean     handle_automatically;
} UnitVariable;


// 05.11.2019
// Set the "defc.ptframes" to NOT be handled automatically, as the memread count has to be set as well in a manual
// implementation of the setter handling.

// The last item in the list is the boolean flag to indicate, whether the property should be handled automatically.
// for automatically handled properties there does not have to be new case defined/ does not have to be manually
// implemented. The handling is automtically done by sending the according requests to the camera.
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
    { "defc.ptframes",   G_TYPE_UINT,   G_PARAM_READWRITE, PROP_POST_TRIGGER_FRAMES,        FALSE },
    { "c1.frcount",      G_TYPE_UINT,   G_PARAM_READABLE,  PROP_RECORDED_FRAMES,            TRUE },
    { "c1.state",        G_TYPE_STRING, G_PARAM_READABLE,  PROP_CINE_STATE,                 TRUE },
    { "cam.aux1mode",    G_TYPE_UINT,   G_PARAM_READWRITE, PROP_AUX_ONE_MODE,               TRUE },
    { "hw.memgateen",    G_TYPE_UINT,   G_PARAM_READWRITE, PROP_HARDWARE_MEMGATE_ENABLED,   TRUE },
    { "cam.aux1pp",      G_TYPE_STRING, G_PARAM_READWRITE, PROP_AUX_ONE_PARAMETERS,         TRUE },
    // 05.11.2019
    { "c1.frsize",       G_TYPE_UINT,   G_PARAM_READABLE,  PROP_FRAME_SIZE,                 TRUE },
    { "c1.frspace",      G_TYPE_UINT,   G_PARAM_READABLE,  PROP_MEMORY_SIZE,                TRUE },
    // 11.11.2019
    { "cam.mode",        G_TYPE_UINT,   G_PARAM_READABLE, PROP_ACQUISITION_MODE_INDEX,      TRUE },
    { NULL, }
};

typedef struct {
    enum {
        MESSAGE_READ_IMAGE = 1,
        MESSAGE_UNPACK_IMAGE = 2,
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

    g_debug("C REQUEST: %s", request);

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
    g_debug("C REPLY: %s", reply);
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
    //g_warning("GET STRING VALUE: %s", value);
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
    //g_debug("get variable: %s", var);
    g_debug("get result: %s", var_value);
    //g_warning("TRIGGER SOURCE %i", priv->uca_trigger_source == UCA_CAMERA_TRIGGER_SOURCE_SOFTWARE);

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
                //g_warning("receive error %s", result->error);
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
 *
 * @deprecated
 * @param priv
 */
void unpack_packet(UcaPhantomCameraPrivate *priv) {
    if (priv->xg_remaining_length > 0) {
        priv->xg_packet_data -= priv->xg_remaining_length - 0;
        memcpy(priv->xg_packet_data, priv->xg_remaining_data, priv->xg_remaining_length);
    }

    int mask = 0b1111111111;
    int i;
    int length = (priv->xg_packet_length + priv->xg_remaining_length);
    int overlap = length % 5;
    guint64 temp;
    for (i = 0; i < length - overlap; i += 5) {
        temp = 0;
        temp = 0;
        for (int k = 0; k < 5; k++) {
            temp |= priv->xg_packet_data[i + k];
            temp <<= 8;
        }
        temp >>= 8;

        priv->xg_buffer[priv->xg_buffer_index + 3] = (guint16) temp & mask;
        priv->xg_buffer[priv->xg_buffer_index + 2] = (guint16) (temp >> 10) & mask;
        priv->xg_buffer[priv->xg_buffer_index + 1] = (guint16) (temp >> 20) & mask;
        priv->xg_buffer[priv->xg_buffer_index + 0] = (guint16) (temp >> 30) & mask;
        priv->xg_buffer_index += 4;
    }
    // Setting up the overlap for the next iteration
    priv->xg_packet_data += length - overlap;
    memcpy(priv->xg_remaining_data, priv->xg_packet_data, overlap);
    priv->xg_remaining_length = overlap;
}

/**
 * @deprecated
 * @param priv
 */
void mem_copy_packet(UcaPhantomCameraPrivate *priv) {

    memcpy(priv->xg_data_in, priv->xg_packet_data, priv->xg_packet_length);
    priv->xg_data_in += priv->xg_packet_length;
}

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
static void flush_block(UcaPhantomCameraPrivate *priv) {

    // If the block has been completely processed (all payload data extracted from all the packages in it), then it has
    // to be flushed, meaning that it has to be "given back" to the kernel, so new data can be written to it.
    // unless it's status isn't changed, the kernel cannot write new packages into this block of the ring buffer.
    if (priv->xg_block_finished == TRUE) {
        priv->xg_current_block->h1.block_status = TP_STATUS_KERNEL;
    }
}

/**
 * @brief Increments the pointer and the index of the currently processed package from the ring buffer.
 *
 * @author Jonas Teufel
 *
 * @param priv
 */
void increment_packet(UcaPhantomCameraPrivate *priv) {
    priv->xg_packet_index += 1;
    priv->xg_packet_header = (struct tpacket3_hdr *) ((uint8_t *) priv->xg_packet_header + priv->xg_packet_header->tp_next_offset);
}

/**
 * @brief Copies the packet data into the image buffer and prepares the pointers for the next iteration
 *
 * This function processes the data from one packet from the ring buffer
 *
 * @author Jonas Teufel
 *
 * CHANGELOG
 *
 * Added 20.05.2019
 *
 * Changed 29.05.2019
 * There was a bug here. When the processed package was the pre-last one in a block, but also the last one for an image
 * that cause the program to flag the block as finished even though the last block would never be looked at. Thus the
 * last packet in the block would disappear...
 * Added a if clause that checks for the edge case now.
 *
 * @param priv
 */
void process_packet(UcaPhantomCameraPrivate *priv) {

    // If the remaining length of the data is bigger, than the length of the packet, we can just use all the packet
    // data to append it to the buffer
    if (priv->xg_remaining_length >= priv->xg_packet_length) {

        // Here we copy the packet data into the image buffer and increment the pointer to the top of the image buffer
        // for the next iteration
        memcpy(priv->xg_data_in, priv->xg_packet_data, priv->xg_packet_length);
        priv->xg_data_in += priv->xg_packet_length;

        // Now we need to update the count of the total bytes received and the remaining bytes
        priv->xg_total += priv->xg_packet_length;
        priv->xg_remaining_length -= priv->xg_packet_length;

        // We also need to update the pointer to the packet, so that it points to the next packet
        // 29.05.2019
        // The edge case of incrementing a packet when ist is the pre-last in a block but the last one for an image
        // caused a nasty bug with disappearing packages... So we have to check for that now.
        if (!(priv->xg_remaining_length <= 0 && priv->xg_packet_index == priv->xg_packet_amount - 2)) {
            increment_packet(priv);
        } else {
            priv->xg_packet_skipped = TRUE;
            //g_warning("I AM ACTUALLY USEFUL");
        }
    }
    // THIS BASICALLY NEVER GETS EXECUTED...
    // This is the tricky case. This means the image would be completely finished with just a fraction of the data from
    // the current package. This also means, that some portion of the data from this package belongs to the next image
    // already... So we have to make sure to set the pointers up correctly, so the next image grab call will pick up
    // excatly where we left...
    else if (priv->xg_remaining_length < priv->xg_packet_length) {

        // Here we will take as many bytes from the buffer, as there are remaining to get the full image...
        memcpy(priv->xg_data_in, priv->xg_packet_data, priv->xg_remaining_length);

        // Now we update the stats for the image, signaling that all the bytes for this image have been received now
        // and that there are none remaining
        priv->xg_total += priv->xg_remaining_length;
        priv->xg_remaining_length = 0;

        // Now we can update the pointers for the next iteration / the next image. We can make sure that the next
        // iteration picks up here by just moving the data pointer accordingly by as many bytes as we have just used
        // and adjusting the data length accordingly. This way we dont need another special case.
        priv->xg_packet_data += priv->xg_remaining_length;
        priv->xg_packet_length -= priv->xg_remaining_length;

        // Also we will NOT increment the packet index, as we want the next iteration to pick up with this packet...
    }
}

/**
 * @brief Extracts the data from one block in the ring buffer and adds it to the destination buffer.
 *
 * @author Jonas Teufel
 *
 * CHANGELOG
 *
 * Added 28.04.2019
 *
 * Changed 20.05.2019
 * Completely moved the whole processing of the actual package data of a single packet (memcopy the contents and
 * incrementing of the pointers) to the method "process_packet"
 *
 * Changed 29.05.2019
 * Also saving the amount of packages inside a block into a attribute of the camera object now, so it can be used
 * in the process_packet method without explicitly passing it.
 *
 * @param block_description
 * @param destination
 * @param expected
 * @param total
 * @param finished
 * @param header
 * @return
 */
void process_block(UcaPhantomCameraPrivate *priv) {
    // Each block can hold multiple actual packets (frames). But the actual amount how many packets are in one block
    // depends on the size of the packet, speed of transmission etc. In general, the amount is not previously known,
    // but once the block is done writing, is stored inside the "num_pckts" of its descriptor.
    int packet_amount = priv->xg_current_block->h1.num_pkts;
    priv->xg_packet_amount = packet_amount;

    // This will be the pointer directed at the start of the actual packet data! The data contained in a packet is
    // byte wise, which means its a unsigned 8 bit format.
    guint8 *data;

    // This will store the size of the packet's payload
    int length;

    // The finished boolean variable is an indicator of whether the currently processed block is finished or not.
    // We have to consider the following case: If the loop below break's because all the expected data has been
    // received for one package, there could possibly still be data of the next image in that ring buffer block.
    // and we need to indicate this to know, if we should start in this or the next block when attempting to get the
    // data for that next image.
    //priv->xg_block_finished = TRUE;

    int i;
    
    // Ugly hack, should improve this
    if (priv->xg_packet_skipped == TRUE) {
        //g_warning("ALSO USEFUL");
        increment_packet(priv);
        priv->xg_packet_skipped = FALSE;
    }

    for (i = priv->xg_packet_index; i < priv->xg_packet_amount; i++) {

        if (priv->xg_remaining_length <= 0) {
            break;
        }

        // Calculation of the actual payload(!) length. The packages sent by the phantom have a overhead of 32 bytes!
        length = priv->xg_packet_header->tp_snaplen - 32;
        // After exactly 94 bytes into the package the info about the used protocol can be extracted. And after 114
        // bytes the overhead ends and the actual payload starts.
        data = (guint8 *) priv->xg_packet_header;
        //g_warning("length: %i", length);
        if (data[94] == 136 && data[95] == 183) {
            data += 114;

            // With this we copy all the data (using the complete length of the payload) onto the destination buffer (where
            // the final image data will be stored)
            priv->xg_packet_data = data;
            priv->xg_packet_length = length;

            process_packet(priv);

        } else {
            increment_packet(priv);
        }
    }

    // Here we are simply checking "Did the loop process all the packets of the blog?". Because if it did than obviously
    // This block is finished and we can flag it as such. But if it is not, than the next image has to pick up with
    // this block.
    if (priv->xg_packet_amount - 1 > priv->xg_packet_index) {
        priv->xg_block_finished = FALSE;
    } else {
        priv->xg_block_finished = TRUE;
        //g_warning("block finished");
    }
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
        GError **error)
{

    // With this we keep track of how many bytes have already been received.
    priv->xg_total = 0;
    gsize total = 0;
    int bytes;
    int remaining;
    
    // Resetting state variables
    priv->xg_remaining_length = 0;
    priv->xg_data_in = priv->xg_data_buffer.in;
    
    unsigned long header_address;

    // This is the amount of bytes that has to be received, based on the resolution of the image and the structure of
    // the 10G transfer format (always P10).
    int expected_bytes = get_buffer_size(priv);
    priv->xg_expected = get_buffer_size(priv);

    // This struct will contain all the necessary iformation about the block of the ring buffer, that is currently
    // being processed
    struct block_desc *block_description;

    //unsigned int block_index = 0;
    unsigned int block_amount = 10000;

    // For profiling the code
    struct timespec tstart={0,0}, tend={0,0};
    struct timespec pstart={0,0}, pend={0,0};
    
    priv->xg_remaining_length = priv->xg_expected;


    //clock_gettime(CLOCK_MONOTONIC, &tstart);
    while (priv->xg_total < priv->xg_expected) {

        // Creating the block description for the current block index from the ring buffer.
        //block_description = (struct block_desc *) ring->rd[block_index].iov_base;
        priv->xg_current_block = (struct block_desc *) ring->rd[priv->xg_block_index].iov_base;

        // Polling.
        // Once the kernel has finished writing to a block of the buffer (either because it is full, or because the
        // timer ran out), this block is being released to the user space (-> this program) and only then we can
        // read it. So the program execution of the loop will be skipped here, if the next block has not yet been
        // released to the user space.
        if ((priv->xg_current_block->h1.block_status & TP_STATUS_USER) == 0) {
            poll(poll_fd, 1, -1);
            continue;
        }

        if (priv->xg_block_finished == TRUE) {
            // In case block was finished during the previous run of this function, the header struct will be created
            // to point to the first packet of the current (new) block.
            // Although if it wasn't finished the header for the packet, where the last loop left of will be reused.
            priv->xg_packet_header = (struct tpacket3_hdr *) ((uint8_t *) priv->xg_current_block +
                                                              priv->xg_current_block->h1.offset_to_first_pkt);
            priv->xg_packet_index = 0;
        }

        // Actually extracting the data of the packages in that block into the destination buffer.

        process_block(priv);
        flush_block(priv);

        // If the block is not yet finished to be processed we cannot increment the index, so that with the next call
        // of this function the rest of the unfinished block will be processed first.
        if (priv->xg_block_finished == TRUE) {
            // Going to the next block. If the last block has been reached, it starts with the first block again,
            // after all this is how a ring buffer works.
            priv->xg_block_index = (priv->xg_block_index + 1) % block_amount;
        }
    }
    return 0;
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
    unsigned int block_size = 1 << 16; // 22
    unsigned int frame_size = 1 << 8;  // 11
    unsigned int block_amount = 10000;
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

unsigned int create_mask(int length) {
    return (1 << (length)) - 1;
}

unsigned int process_carry(unsigned int carry_length, unsigned long *carry, unsigned long *value) {

    unsigned int mask_length = carry_length + 4;
    int new_carry = create_mask(mask_length) & *value;
    *value = (*value >> carry_length) | (*carry << (64 - carry_length));
    *value >>= 4;
    *carry = new_carry;
    return mask_length;
}


// GENERAL INFORMATION ABOUT SSE VECTORS
// =====================================
// SSE vectors dont really work intuitively, as one would expect.
// They are 128 Bit integer vectors, but they dont quite work as if they are one single long integer. Consider the
// following example of a 128 Bit bit series, were each letter represents one byte (8bit) which is the smallest unit
// in which the data is organized in the vectors:
// A B C D F G H I J K L M N O P    (128 bit = 16 x 8 bit)
//
// 1)
// The first thing to note is, that the every 2 byte units within a vector are "separate" from each other in a certain
// sense. So they have to be imagined like this:
// A B | C D | E F | G H | I J | K L | M N | O P
// These separations do not let a shift operation pass through! So consider the following example of two such 2 byte
// blocks next to each other:
// 11111111 00000000 | 11110000 00000000
// For a shift to the right by 16 bit one would imagine something like this:
// 00000000 00000000 | 11111111 00000000
// When in reality it is this:
// 00000000 00000000 | 00000000 00000000
// The separations do not carry on a shift operation! In fact the shift operation is used on each of the 2 byte blocks
// separately
//
// 2)
// Data is not loaded into the blocks as one would imagine. The two units of a two byte block are switched in their
// order when the "load" function is used. Imagine the following sequence of bytes being inside an array and the array
// pointer being used to load the data directly AS A VECTOR:
// A B C D F G H I J K L M N O P
// The result will be this vector!
// B A | D C | F E | H G | J I | L K | N M | P O.
// But beware the same does not work, when saving the vector as an array again. For example one could make the array
// given above be interpreted as a SSE vector and then instantly cast it back as a array but then the result would be
// B A D C F E H G J I L K N M P O!
// The units are not switched back! this would have to be done manually with a epi8 shuffle for example.



/**
 * @brief Unpacks the P10-encoded data data from a 10G transmitted image frame
 *
 * CHANGELOG
 *
 * Added 25.05.2019
 *
 * Changed 11.06.2019
 * Now the memread index is being incremented at the end of the function
 *
 * Changed 12.07.2019
 * Added comments
 *
 * @param priv
 */
void unpack_image_p10(UcaPhantomCameraPrivate *priv) {

    int new_length = 0;
    int usable_length = 0;
    int limit = 0;

    int i,j;


    priv->xg_buffer_index = 0;
    priv->xg_unpack_index = 0;

    __m128i vector, t0, t1, t2, t3, t4, t5, t6, t7, vector_out, k0, k1, k2, k3, k4, k5, k6;
    __m128i temp;

    // THE GENERAL IDEA
    // The general way this unpacking works due to the weird way the SSE Vectors work is two steps:
    // 1) Shuffle: Specific parts of the input vector are shuffled into a new position (especially new 8 bit units of
    // the vector, in such a way, that the following shift operation does not transcend inter block separations. and
    // then the new part-vectors are AND-masked so that they only contain the relevant bits.
    // 2) Shift: All the part-vectors are shifted according to what their position demands and then they are combined
    // by OR-ing them into a single output vector.

    // THE SHUFFLE MASKS
    // these masks shuffle parts of the input vector into new positions, while also reversing the unit switch within
    // the two byte blocks, so this does not have to be dealt with when saving the vector into the output array again.
    // We can see, that every vector shuffles the required bytes for two pixels. This is because due to the symmetric
    // nature, the required shifts to be in the right position repeat after 4 pixels. So because 8 pixels are being
    // processed there is a pair of 2 pixels each that require the same shift.
    __m128i sm0 = _mm_setr_epi8(1, 0, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 6, 5, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80);
    __m128i sm1 = _mm_setr_epi8(0x80, 0x80, 2, 1, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 7, 6, 0x80, 0x80, 0x80, 0x80);
    __m128i sm2 = _mm_setr_epi8(0x80, 0x80, 0x80, 0x80, 3, 2, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 8, 7, 0x80, 0x80);
    __m128i sm3 = _mm_setr_epi8(0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 4, 3, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 9, 8);

    // THE AND MASKS
    // These masks will be applied after the shuffle masks. This is important because while the shuffle masks do indeed
    // already get rid of most the irrelevant bits within all the other irrelevant 8 bit units, we can see, that 4
    // 8 bit units, 32 bits in total are being persisted in total by the shuffle operation, but every part vector only
    // contains 2 pixels, 20 bits of information, so the additional bits have to be cropped by the AND masking.
    uint8_t mb0[16] = {0b11000000, 0b11111111, 0, 0, 0, 0, 0, 0, 0b11000000, 0b11111111, 0, 0, 0, 0, 0, 0};
    uint8_t mb1[16] = {0, 0, 0b11110000, 0b00111111, 0, 0, 0, 0, 0, 0, 0b11110000, 0b00111111, 0, 0, 0, 0};
    uint8_t mb2[16] = {0, 0, 0, 0, 0b11111100, 0b00001111, 0, 0, 0, 0, 0, 0, 0b11111100, 0b00001111, 0, 0};
    uint8_t mb3[16] = {0, 0, 0, 0, 0, 0, 0b11111111, 0b00000011, 0, 0, 0, 0, 0, 0, 0b11111111, 0b00000011};
    // This is the special mask that is applied to the whole input vector at the beginning, which will only let the
    // 80 relevant bit stay
    uint8_t maskb[16] = {0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0, 0, 0, 0, 0, 0};

    __m128i mask2 = _mm_loadu_si128((__m128i*)&maskb);
    __m128i m0 = _mm_loadu_si128((__m128i*)&mb0);
    __m128i m1 = _mm_loadu_si128((__m128i*)&mb1);
    __m128i m2 = _mm_loadu_si128((__m128i*)&mb2);
    __m128i m3 = _mm_loadu_si128((__m128i*)&mb3);

    uint16_t *buffer_pointer = priv->xg_buffer;

    uint8_t *data_pointer = priv->xg_data_buffer.in;
    uint16_t *output_pointer = priv->xg_buffer;

    gsize pixel_count = priv->roi_width * priv->roi_height;

    int counter = 0;

    while (priv->xg_buffer_index < pixel_count) {
        new_length = priv->xg_total - priv->xg_unpack_index;
        usable_length = new_length - (new_length % 10);
        limit = priv->xg_unpack_index + usable_length;

        // This loop iterates through the raw data buffer und the output buffer (where the unpacked 16 bit pixel
        // representations are being saved) at the same time.
        // .
        for (i = priv->xg_unpack_index, j = priv->xg_buffer_index; i<limit ; i+=10, j+=8) {

            // The "data_pointer" variable is a pointer, which points into array, where the raw data from the 10G image
            // transfer is being buffered.
            // To process it more effectively the data is loaded into a SSE 128 bit integer vector.
            vector = _mm_loadu_si128((__m128i*)data_pointer);

            // The P10 transfer format is a 10 bit format, which means that every 10 bit in the data array represent
            // the information about one pixel. This format has to be unpacked into a 16 bit format, where each pixel
            // is described by 16 bit.
            // 128 bit of the 10 bit format have already been loaded into "vector", but we only want to process 80 bit
            // of these, since these 8 pixels will be unpacked into 8x16=128 Bit output vector.
            // Thus here the vector is being masked, so that only the 80 relevant bits stay.
            vector = _mm_and_si128(vector, mask2);

            // The unpacking process starts with 80 consecutive bits within a 128 bit vector (where the rest besides
            // the 80 bits is zeros) where each 10 bit block describes one pixel.
            // The goal is to get a 128 bits of consecutive bits, where each 16 bit block describes one pixel. Example
            // 111111 110000  --> 111100 111100.

            t0 = _mm_and_si128(_mm_shuffle_epi8(vector, sm0), m0) >> 6;
            t1 = _mm_and_si128(_mm_shuffle_epi8(vector, sm1), m1) >> 4;
            t2 = _mm_and_si128(_mm_shuffle_epi8(vector, sm2), m2) >> 2;
            t3 = _mm_and_si128(_mm_shuffle_epi8(vector, sm3), m3);


            vector_out = _mm_or_si128(_mm_or_si128(t0, t1), _mm_or_si128(t2, t3));

            _mm_storeu_si128((__m128i*)output_pointer, vector_out);

            data_pointer += 10;
            output_pointer += 8;

            priv->xg_buffer_index += 8;
            priv->xg_unpack_index += 10;
        }

    }
    // For some reason it really does not work without this ???
    g_debug("");

    // 11.06.2019
    // Incrementing the memread unpack index, after the image has been received
    priv->memread_unpack_index += 1;
}

/**
 *
 * CHANGELOG
 *
 * Added 14.07.2019
 *
 * @param priv
 */
void unpack_image_p12l(UcaPhantomCameraPrivate *priv) {
    int new_length = 0;
    int usable_length = 0;
    int limit = 0;

    int i,j;


    priv->xg_buffer_index = 0;
    priv->xg_unpack_index = 0;

    __m128i vector, t0, t1, t2, t3, t4, t5, t6, t7, vector_out, k0, k1, k2, k3, k4, k5, k6;
    __m128i temp;

    // THE GENERAL IDEA
    // The general way this unpacking works due to the weird way the SSE Vectors work is two steps:
    // 1) Shuffle: Specific parts of the input vector are shuffled into a new position (especially new 8 bit units of
    // the vector, in such a way, that the following shift operation does not transcend inter block separations. and
    // then the new part-vectors are AND-masked so that they only contain the relevant bits.
    // 2) Shift: All the part-vectors are shifted according to what their position demands and then they are combined
    // by OR-ing them into a single output vector.

    // THE SHUFFLE MASKS
    __m128i sm0 = _mm_setr_epi8(1, 0, 0x80, 0x80, 4, 3, 0x80, 0x80, 7, 6, 0x80, 0x80, 10, 9, 0x80, 0x80);
    __m128i sm1 = _mm_setr_epi8(0x80, 0x80, 2, 1, 0x80, 0x80, 5, 4, 0x80, 0x80, 8, 7, 0x80, 0x80, 11, 10);

    // THE AND MASKS
    uint8_t mb0[16] = {0b11110000, 0b11111111, 0, 0, 0b11110000, 0b11111111, 0, 0, 0b11110000, 0b11111111, 0, 0, 0b11110000, 0b11111111, 0, 0};
    uint8_t mb1[16] = {0, 0, 0b11111111, 0b00001111, 0, 0,  0b11111111, 0b00001111, 0, 0,  0b11111111, 0b00001111, 0, 0,  0b11111111, 0b00001111,};

    uint8_t maskb[16] = {0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0, 0, 0, 0};

    __m128i mask2 = _mm_loadu_si128((__m128i*)&maskb);
    __m128i m0 = _mm_loadu_si128((__m128i*)&mb0);
    __m128i m1 = _mm_loadu_si128((__m128i*)&mb1);

    uint16_t *buffer_pointer = priv->xg_buffer;

    uint8_t *data_pointer = priv->xg_data_buffer.in;
    uint16_t *output_pointer = priv->xg_buffer;

    gsize pixel_count = priv->roi_width * priv->roi_height;

    int counter = 0;

    g_debug("");
    while (priv->xg_buffer_index < pixel_count) {
        new_length = priv->xg_total - priv->xg_unpack_index;
        usable_length = new_length - (new_length % 12);
        limit = priv->xg_unpack_index + usable_length;

        // This loop iterates through the raw data buffer und the output buffer (where the unpacked 16 bit pixel
        // representations are being saved) at the same time.
        // .
        for (i = priv->xg_unpack_index, j = priv->xg_buffer_index; i<limit ; i+=12, j+=8) {

            // The "data_pointer" variable is a pointer, which points into array, where the raw data from the 10G image
            // transfer is being buffered.
            // To process it more effectively the data is loaded into a SSE 128 bit integer vector.
            vector = _mm_loadu_si128((__m128i*)data_pointer);

            // The P10 transfer format is a 10 bit format, which means that every 10 bit in the data array represent
            // the information about one pixel. This format has to be unpacked into a 16 bit format, where each pixel
            // is described by 16 bit.
            // 128 bit of the 10 bit format have already been loaded into "vector", but we only want to process 80 bit
            // of these, since these 8 pixels will be unpacked into 8x16=128 Bit output vector.
            // Thus here the vector is being masked, so that only the 80 relevant bits stay.
            vector = _mm_and_si128(vector, mask2);

            // The unpacking process starts with 80 consecutive bits within a 128 bit vector (where the rest besides
            // the 80 bits is zeros) where each 10 bit block describes one pixel.
            // The goal is to get a 128 bits of consecutive bits, where each 16 bit block describes one pixel. Example
            // 111111 110000  --> 111100 111100.

            t0 = _mm_and_si128(_mm_shuffle_epi8(vector, sm0), m0) >> 4;
            t1 = _mm_and_si128(_mm_shuffle_epi8(vector, sm1), m1);


            vector_out = _mm_or_si128(t0, t1);

            _mm_storeu_si128((__m128i*)output_pointer, vector_out);

            data_pointer += 12;
            output_pointer += 8;

            priv->xg_buffer_index += 8;
            priv->xg_unpack_index += 12;
        }

    }
    // For some reason it really does not work without this ???
    g_debug("");

    // 11.06.2019
    // Incrementing the memread unpack index, after the image has been received
    priv->memread_unpack_index += 1;
}


/**
 *
 * CHANGELOG
 *
 * Added 25.05.2019
 *
 * Changed 14.07.2019
 * Added a switch statement which checks the used image format and according to the format different unpacking functions
 * are being called. At the moment the supported formats for 10G transfer are "P10" and "P12L"
 *
 * @param priv
 * @return
 */
static gpointer
unpack_ximg_data (UcaPhantomCameraPrivate *priv)
{
    Result *result;
    gint fd;
    gboolean stop = FALSE;

    result = g_new0(Result, 1);
    result->type = RESULT_READY;
    result->success = FALSE;

    while (!stop) {
        InternalMessage *message;

        result = g_new0 (Result, 1);
        message = g_async_queue_pop (priv->message_queue);

        switch (message->type) {
            case MESSAGE_UNPACK_IMAGE:
                
                //g_warning("Init the unpacking");
                // IMPLEMENT THE UNPACKING
                priv->xg_unpack_index = 0;

                // 14.07.2019
                // Added the switch case here. Because previously the default was the the P10 unpacking. Which was
                // basically the only option. But now a 12bit transfer format is also possible, which obviously
                // requires a different algorithm.
                switch (priv->format) {
                    case IMAGE_FORMAT_P10:
                        unpack_image_p10(priv);
                        break;
                    case IMAGE_FORMAT_P12L:
                        unpack_image_p12l(priv);
                        break;
                }

                result->type = RESULT_IMAGE;
                result->success = TRUE;

                // g_warning("ERROR: %s", result->error);
                g_async_queue_push (priv->result_queue, result);
                //g_warning("PUSHED RESULT ");
                break;

            case MESSAGE_READ_IMAGE:
                g_async_queue_push(priv->message_queue, message);

            case MESSAGE_READ_TIMESTAMP:
                // Not implemented
                break;

            case MESSAGE_STOP:
                stop = TRUE;
                break;
        }

    }

    return NULL;
}

/**
 * @brief The Thread, which receives the images using the 10G network
 *
 * CHANGELOG
 *
 * Added 10.04.2019
 *
 * Changed 26.06.2019
 * Removed the hardcoded usage of the defined INTERFACE makro and instead using the interface string in the priv->iface
 * property to bind the socket now.
 *
 * @author Jonas Teufel
 *
 * @param priv
 * @return
 */
static gpointer
accept_ximg_data (UcaPhantomCameraPrivate *priv)
{
    //g_warning("START ACCEPTING");
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
    fd = setup_raw_socket(&ring, priv->iface);

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

    priv->xg_packet_index = 0;
    priv->xg_block_index = 0;
    priv->xg_block_finished = TRUE;

    while (!stop) {
        InternalMessage *message;

        result = g_new0 (Result, 1);
        message = g_async_queue_pop (priv->message_queue);

        switch (message->type) {
            case MESSAGE_READ_IMAGE:
                
                //g_warning("Receiving the actual image");
                // Here we are calling the function, which actually uses the socket to receive the image piece by piece
                // The actual image will be saved in the buffer of the camra object's "priv" internal buffer
                // "priv->buffer".
                read_ximg_data(priv, fd, &ring, &poll_fd, &result->error);

                // Once the image was completely received we push a new message, indicating that image reception was a
                // success, into the queue, so that the main thread which is watching the queue can retrieve the image
                // from the buffer.
                //result->type = RESULT_IMAGE;
                //result->success = TRUE;

                // g_warning("ERROR: %s", result->error);
                //g_async_queue_push (priv->result_queue, result);
                //g_warning("PUSHED RESULT ");
                break;

            case MESSAGE_UNPACK_IMAGE:
                g_async_queue_push(priv->message_queue, message);

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


// ***********************************************
// CONNECTION PROCESS & PHANTOM DISCOVERY PROTOCOL
// ***********************************************

/**
 * @brief Whether or not a static IP address has been manually set for the camera
 *
 * @author Jonas Teufel
 *
 * CHANGELOG
 *
 * Added 26.05.2019
 *
 * @param priv
 * @return
 */
gboolean
phantom_has_static_ip(UcaPhantomCameraPrivate *priv) {
    // The default value for the IP address is an empty string. So if the property contains anything longer than an
    // empty string, we know it has to be a IP address set by the user.
    if (g_strcmp0(priv->ip_address, "") > 0) {
        return TRUE;
    } else {
        return FALSE;
    };
}

/**
 * @brief returns a GSocketAddress object for the static IP address, that has been set to the camera property
 *
 * @author Jonas Teufel
 *
 * CHANGELOG
 *
 * Added 26.05.2019
 *
 * @param priv
 * @param error
 * @return
 */
static GSocketAddress *
phantom_get_static_address (UcaPhantomCameraPrivate *priv, GError **error) {
    GInetAddress *ip_addr;
    GSocketAddress *socket_addr;

    // We are creating the IP address from the string, which has been set to the property
    ip_addr = g_inet_address_new_from_string(priv->ip_address);

    // And then the socket address using the just created IP address and the default port for the control connection
    // of a phantom camera: 7115 (it is reasonable to assume, that this port has not been altered)
    socket_addr = g_inet_socket_address_new(ip_addr, 7115);

    g_object_unref(ip_addr);
    return socket_addr;
}

/**
 * @brief Returns the network address of the phantom camera
 *
 * Usually the phantom cameras have a broadcast discovery protcol, so the code doesnt have to know their IP address.
 * That is not working at the moment though. So instead of executing the discovery routine, this function returns the
 * hardcoded IP address defined at the top of this file.
 *
 * @authors Matthias Vogelgesang, Jonas Teufel
 *
 * @param error
 * @return
 */
static GSocketAddress *
phantom_discover (UcaPhantomCameraPrivate *priv, GError **error)
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

    // The way every phantom camera works is that, if the normal 1G network is used the camera has a address in the
    // range 100.100.*.* and if the 10G network is used every camera has an IP in the range 172.16.*.* When using the
    // discovery protocol and the UDP broadcast it is important to only send the broadcast to these IP ranges because
    // otherwise it wont work.
    // Thus, we need to check if 10G is enabled or not.
    if (priv->enable_10ge) {
        bcast_addr = g_inet_address_new_from_string ("172.16.255.255");
    } else {
        bcast_addr = g_inet_address_new_from_string ("100.100.255.255");
    }

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

/**
 * @brief Returns the GSocketAddress object for the network address of the phantom camera
 *
 * This function checks, whether a ip address has been manually set for the camera, by setting one of its properties.
 * If that is the case a socket address will be created from that static ip address. If it is not the case, the phantom
 * discovery protocol will be used to obtain the IP address of a phantom camera automatically using a UDP broadcast.
 *
 * @author Jonas Teufel
 *
 * CHANGELOG
 *
 * Added 26.05.2019
 *
 * @param priv
 * @param error
 * @return
 */
static GSocketAddress *
phantom_get_address(UcaPhantomCameraPrivate *priv, GError **error) {

    if (phantom_has_static_ip(priv)) {
        return phantom_get_static_address(priv, error);
    } else {
        return phantom_discover(priv, error);
    }
}

/**
 * @brief Creates the control socket connection to the phantom camera
 *
 * First the address of the phantom camera is obtained, then the control socket connection is being established and
 * finally some important properties of the camera are being read out.
 *
 * @author Matthias Vogelgesang
 *
 * CHANGELOG
 *
 * Added 26.05.2019
 *
 * @param priv
 * @param error
 */
static void
phantom_connect (UcaPhantomCameraPrivate *priv, GError **error) {
    g_warning("Inside connect");
    GSocketAddress *addr;
    addr = phantom_get_address (priv, &priv->construct_error);
    g_warning("After address");
    if (addr != NULL) {
        gchar *addr_string;
        // Establishing a control connection to the camera
        g_warning("Before connect");
        priv->connection = g_socket_client_connect (priv->client, G_SOCKET_CONNECTABLE (addr), NULL, &priv->construct_error);
        addr_string = g_inet_address_to_string (g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (addr)));
        g_warning("After connect");
        g_debug ("Connected to %s\n", addr_string);
        g_free (addr_string);
        g_object_unref (addr);

        phantom_get_resolution_by_name (priv, "defc.res", &priv->roi_width, &priv->roi_height);
        priv->features = phantom_get_string_by_name (priv, "info.features");
        priv->have_ximg = strstr (priv->features, "ximg") != NULL;
    }
}


/**
 * @brief Sends a command to the camera, which tells it into which internal cine partition to record following triggers
 *
 * NOTE: This command obviously has to be called before any "trigger" commands are being sent to the camera, but it
 * also has to be called before a hardware trigger can work on the camera!
 *
 * CHANGELOG
 *
 * Added 10.06.2019
 *
 * @param priv
 */
static void
prepare_trigger(UcaPhantomCameraPrivate *priv) {
    // To simplify things for the user, whenever a trigger is issued, we are assuming that the frames are to be saved
    // into the first cine. Like this, the user does not have to know about the cone structure, but can simply use
    // the camera as a black box for image recording into a generic storage unit.
    const gchar *record_request = "rec 1\r\n";
    gchar *reply;

    // "phantom_talk" actually sends the request over the ethernet connection
    reply = phantom_talk(priv, record_request, NULL, 0, NULL);
    g_free(reply);
}

// ******************************
// ACQUISITION OF MULTIPLE FRAMES
// ******************************

/**
 * @brief Sends the instructions to start the acquistion of multiple frames
 *
 * This method sends the instruction to start acquiring multiple frames to the camera. The amount of frames to be
 * acquired is stored in the camera attribute "defc.ptframes" and can be modified. All the frames are saved into the
 * first cine of the camera.
 * Caution: Sending a trigger command will overwrite all the frames, which have been recorded by a previous trigger
 * command!
 *
 * CHANGELOG
 *
 * Changed 10.05.2019
 * Added the explicit assumption to always save all the frames into the first cine.
 *
 * Changed 29.05.2019
 * Added the "rec" command, which is being send before the trig command, to tell the camera into which cine to
 * put the recording.
 *
 * Changed 10.06.2019
 * Moved the "rec" command into its own function "prepare_trigger", which is now being invoked here inside this
 * function. It was moved, because the "rec" command may also be needed as a separate functionality in the future
 *
 * Changed 19.07.2019
 * The actual "trig" command is now only being sent, when the external-trigger flag is set to False. If it is set to
 * true the trigger method for the camera will merely send the prepare command so that subsequent hardware triggers
 * work.
 *
 * Changed 06.11.2019
 * This method will simply send the "trig" command itself to the camera. If there is not currently a recording running
 * in the camera, then that is the problem of the user.
 *
 * @param camera
 * @param error
 */
static void
uca_phantom_camera_trigger (UcaCamera *camera,
                            GError **error)
{
    // To simplify things for the user, whenever a trigger is issued, we are assuming that the frames are to be saved
    // into the first cine. Like this, the user does not have to know about the cone structure, but can simply use
    // the camera as a black box for image recording into a generic storage unit.
    const gchar *trigger_request = "trig\r\n";
    gchar *reply;

    UcaPhantomCameraPrivate *priv = UCA_PHANTOM_CAMERA_GET_PRIVATE(camera);

    reply = phantom_talk (priv, trigger_request, NULL, 0, error);
    g_free(reply);

    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));
}

/**
 * @brief Returns TRUE, when the camera is currently NOT recording a trigger and FALSE otherwise.
 *
 * CHANGELOG
 *
 * Added 29.05.2019
 *
 * @param priv
 * @return
 */
static gboolean
check_trigger_status(UcaPhantomCameraPrivate *priv) {
    const gchar *request = "get c1.state\r\n";
    gchar *reply;
    gboolean status;
    // Actually sending the request to the camera and receiving its reply.
    reply = phantom_talk (priv, request, NULL, 0, NULL);
    if (strstr(reply, "STR") != NULL) {
        status = TRUE;
    } else {
        status = FALSE;
    }
    return status;
}

// *********************************
// STARTING AND STOPPING THE READOUT
// *********************************

/**
 * @brief This method starts the readout for the camera.
 *
 * @authors Matthias Vogelgesang, Jonas Teufel
 *
 * CHANGELOG
 *
 * Added ??
 */
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
    priv->buffer = g_malloc0 (get_buffer_size (priv));
    //priv->buffer = g_malloc0(9000000);

    if (priv->enable_10ge) {
        // 06.04.2019
        // Using the 10G connection, the transfer format is being unpacked inside the actual receive loop (in-time
        // unpacking). The data is being unpacked into a uint16 buffer already for the actual pixel values. This buffer
        // needs to be init here with the resolution of the picture.
        //g_free(priv->xg_buffer);
        priv->xg_buffer = g_malloc0(priv->roi_height * priv->roi_width * 4);
        priv->xg_data_buffer.in = g_malloc(priv->roi_height * priv->roi_width * 4);
        
        priv->accept_thread = g_thread_new (NULL, (GThreadFunc) accept_ximg_data, priv);
        priv->unpack_thread = g_thread_new (NULL, (GThreadFunc) unpack_ximg_data, priv);

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
    UcaPhantomCameraPrivate *priv;
    InternalMessage *message;

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    /* stop accept thread */
    message = g_new0 (InternalMessage, 1);
    message->type = MESSAGE_STOP;
    g_async_queue_push (priv->message_queue, message);
    message = g_new0 (InternalMessage, 1);
    message->type = MESSAGE_STOP;
    g_async_queue_push (priv->message_queue, message);

    /* stop listener */
    g_cancellable_cancel (priv->accept);
    g_socket_listener_close (priv->listener);

    g_thread_join (priv->accept_thread);
    g_thread_unref (priv->accept_thread);

    g_thread_join(priv->unpack_thread);
    g_thread_unref(priv->unpack_thread);
    
    //g_free(priv->xg_data_buffer.in);
    //g_free(priv->xg_buffer);

    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));

}


/**
 * @brief Initializes the connection with the camera
 *
 * !NOTE: This function has to be called before any other operations on the camera are being performed.
 * This function will create the control connection to the camera and also creates the threads to manage the data
 * connection and decoding.
 *
 * @authors Matthias Vogelgesang, Jonas Teufel
 *
 * CHANGELOG
 *
 * Added ?
 *
 * Changed 26.05.2019
 * Removed the sending of the rec and trig request to the camera, because that functionality can already be achieved
 * by using the "trigger" command.
 * Added a call to the connect function here. The reason is the following: Connecting to the camera doesnt make sense
 * right after the camera object is created, because the user then doesnt even have the chance to make pre-connect
 * property configurations like enabeling 10G etc. So connecting is now being done here.
 *
 * Changed 11.06.2019
 * Removed the call to the "phantom_connect" function. The  camera is now no longer connected by calling the
 * "start_recording" function, but rather by setting the "connect" property to true!
 *
 * Changed 16.07.2019
 * If the "external-trigger" flag of the camera object has been set to true, the "prepare_trigger" function will be
 * invoked, which will send a "rec" command to the camera, which will tell the camera into which cine (internal memory
 * partition the frames are supposed to be saved into)
 *
 * @param camera
 * @param error
 */
static void
uca_phantom_camera_start_recording (UcaCamera *camera,
                                    GError **error)
{
    UcaPhantomCameraPrivate *priv;
    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    prepare_trigger(priv);

    priv->memread_index = -1;
    priv->memread_remaining = priv->memread_count;
    priv->memread_request_sent = FALSE;
    // 21.07.2019
    // The memread index is now being set to -1 instead of 0 to clearly indicate, that no grab command has been
    // called yet. The grab function needs to realize this state to calculate the initial offset of the starting
    // position within the cine
    // 11.06.2019
    // We obviously also need to reset the unpack index
    priv->memread_unpack_index = 0;
    // 06.11.2019
    // Getting the trigger source from the parent instance of the camera
    UcaCameraTriggerSource trigger_source;
    g_object_get(camera, "trigger-source", &trigger_source, NULL);

    uca_phantom_camera_start_readout(camera, error);

    // 06.07.2019
    // Using the priv->externally_triggered deprecated
    // 16.07.2019
    // If the trigger mode has been set to "EXTERNAL" we first need to send a "rec" command to the camera, so the
    // camera even knows into which cine (internal memory partition) it should record the frames into, upon being
    // triggered.
    // Without the "rec" command, a hardware trigger will not work.

    // 06.11.2019
    // No matter what trigger mode has been set, the trigger has to be prepared at the beginning of a recording
    // anyways. The prepare_trigger starts a recording.
    //prepare_trigger(priv);

    // The trigger mode "AUTO" is a special case. For this case it is intended, that a trigger is implicitly directly
    // sent as soon as the recording is being started.
    if (trigger_source == UCA_CAMERA_TRIGGER_SOURCE_AUTO) {
        uca_phantom_camera_trigger(camera, error);
    }
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
 * @brief helper function, which takes the parameters for the image request and creates the correct string.
 *
 * This function takes the configuration information stored within the camera object regarding whether 10G
 * transmission is enabled and about which transfer format to use in combination with the cine index and the amount
 * of frames to request given by the parameters and creates a correctly formatted command string to be sent to the
 * camera.
 * The creates string will be stored in the pointer given by the "request" parameter.
 *
 * @author Jonas Teufel
 *
 * CHANGELOG
 *
 * Added 10.05.2019
 *
 * Changed 29.05.2019
 * Added the additional parameter "frames_start", as the need to additionally supply the offset for chunk transmission
 * arose.
 *
 * Changed 14.07.2019
 * Added the "P12L" format to the switch case, so it can be used.
 * The P12L format can be used for 1G and 10G transmission. It is a 12 bit raw transfer format
 *
 * @param priv
 * @param cine
 * @param frame_count
 * @param request
 */
static gchar *
create_grab_request(UcaPhantomCameraPrivate *priv,
                                    gchar *cine,
                                    guint frame_start,
                                    guint frame_count)
{
    // These will hold the strings for the command and the format parameters. These will not be passed as arguments,
    // as they are defined through the camera properties and not by the actual "grab" call.
    const gchar *command;
    const gchar *format;
    // When using the 10G format, an additional parameter, specifying the mac address of the receiving machine
    // (the one executing this program) is needed.
    const gchar *additional;
    gchar *request;

    // The phantom camera expects a string, which
    const gchar *request_fmt = "%s {cine:%s, start:%s, cnt:%s, fmt:%s %s}\r\n";

    // Since the format string expects strings for every value, but the frame count is given as an integer, we need to
    // convert the int into a string first
    const gchar *frames = g_strdup_printf("%i", frame_count);
    // 29.05.2019
    // Same thing goes for the starting frame index
    const gchar *start = g_strdup_printf("%i", frame_start);

    // Now we check the camera object for its setting regarding the transfer format and the network type used.
    // 14.07.2019
    // Added the case for the "P12L" image format. This is the image format, which is very relevant for the 10G
    // transmission, as a 12 bit transfer format does not loose information with a 12 bit sensor depth
    switch (priv->format) {
        case IMAGE_FORMAT_P10:
            format = "P10";
            break;
        case IMAGE_FORMAT_P16:
            format = "P16";
            break;
        case IMAGE_FORMAT_P12L:
            format = "P12L";
            break;
    }

    // The camera expects different commands, based on whether it is supposed to transfer over the "normal" ethernet
    // interface or the 10G network
    if (priv->enable_10ge) {
        // NOTE!
        // The 10G interface always uses the P10 transfer format. Any format specified above will not have any
        // influence ont the ximg command (the parameter given will just be ignored by the camera).
        command = "ximg";
        // Creating the additional parameter with the MAC address
        additional = g_strdup_printf (", dest:%02x%02x%02x%02x%02x%02x",
                                      priv->mac_address[0], priv->mac_address[1],
                                      priv->mac_address[2], priv->mac_address[3],
                                      priv->mac_address[4], priv->mac_address[5]);
    } else {
        command = "img";
        additional = "";
    }

    // Actually using all the parameters to create the request string
    request = g_strdup_printf (request_fmt, command, cine, start, frames, format, additional);
    return request;
}

/**
 * @brief Tells the worker threads for receiving/decoding the image to start working
 *
 * This functions puts messages into the internal async message queue, which once received by the worker threads will
 * initiate them to listen for new image transmission.
 *
 * @author Jonas Teufel
 *
 * CHANGELOG
 *
 * Added 10.05.2019
 *
 * @param priv
 */
static void
start_receiving_image(UcaPhantomCameraPrivate *priv)
{
    // The way the image reception works with the phantom camera is, that there is the main program execution, which
    // maintains a TCP connection with the camera. And over this connection the command requests and replys are send.
    // But for the transmission of a image a secondary channel is being opened. And this connection is being handled
    // in a separate thread. When receiving an image we need to tell these threads to initiate the data transmission
    // connections. This communication with the threads is being done using an async message queue
    InternalMessage *message;

    // This message tells the thread to start listening for new incoming data.
    message = g_new0 (InternalMessage, 1);
    //message->data = data;
    message->type = MESSAGE_READ_IMAGE;
    g_async_queue_push (priv->message_queue, message);

    // With 10G there is also a thread to read the data, but there is also yet ANOTHER thread, which decodes the
    // transfer format of the data as it is being received. And we need to tell this thread to start working too, but
    // of course only if 10G transfer is enabled.
    if (priv->enable_10ge) {
        message = g_new0 (InternalMessage, 1);
        //message->data = data;
        message->type = MESSAGE_UNPACK_IMAGE;
        g_async_queue_push (priv->message_queue, message);
    }

}

/**
 * @brief Waits for the image to finish transmission, then decodes it and copies into the output buffer.
 *
 * This function waits for the worker threads to finish the transmission(/decoding) of the image (blocking call to get
 * the result from the result queue). Then it decodes the image if necessary and copies the contents into the result
 * buffer, thus passing it back to whatever instance made the call to the "grab" function.
 *
 * @author Jonas Teufel
 *
 * CHANGELOG
 *
 * Added 10.05.2019
 *
 * @param priv
 * @param data
 * @return
 */
static gboolean
finalize_receiving_image(UcaPhantomCameraPrivate *priv,
                            gpointer data,
                            GError **error)
{
    Result *result;
    gboolean is_success;

    
    // This is a blocking call, which will wait until a new "result" has been put into the async queue by the worker
    // thread, which he will do, when the image transmission is finished.
    result = g_async_queue_pop (priv->result_queue);

    // If the result is indeed an image and the transmission was a success, then the finalized image just needs to be
    // copied into the data buffer, thus returning it to whatever instance made the call to "grab" in the first place
    g_assert (result->type == RESULT_IMAGE);
    is_success = result->success;

    // In case it was no success, we propagate the error and return FALSE to indicate a transmission failure.
    if (!is_success) {
        g_propagate_error (error, result->error);
        g_free(result);
        return FALSE;
    }

    // Now there is a difference of how the image needs to be finalized depending on whether the image was transferred
    // normally or with 10G. Since in 10G there already is a separate unpack thread, that runs concurrent with the
    // reception, the contents of that threads buffer just have to be copied to the output buffer, but for normal
    // transmission there needs to be a decoding step based on what transfer format was used
    if (priv->enable_10ge) {
        // NOTE
        // priv->xg_buffer contains the decoded image. priv->buffer contains the raw data
        memcpy (data, priv->xg_buffer, priv->roi_width * priv->roi_height * 2);
    } else {
        switch (priv->format) {
            case IMAGE_FORMAT_P10:
                unpack_p10(data, priv->buffer, priv->roi_width * priv->roi_height);
                break;
            case IMAGE_FORMAT_P16:
                memcpy (data, priv->buffer, priv->roi_width * priv->roi_height * 2);
                break;
        }
    }

    g_free(result);
    return TRUE;
}

/**
 * @brief Sends request to acquire current singular frame and copies it into output buffer
 *
 * This function first sends a command to the phantom camera using the control connection and starts the worker threads
 * to start receiving the image data on the secondary connection. If the image transfer has been successful, the image
 * gets copied into the output buffer.
 *
 * @author Jonas Teufel
 *
 * CHANGELOG
 *
 * Added 10.05.2019
 *
 * @param priv
 * @param data
 * @param error
 * @return
 */
static gboolean
camera_grab_single (UcaPhantomCameraPrivate *priv,
                    gpointer data,
                    GError **error)
{
    // When using the camera in single frame mode, the grab command will issue the acquisition of the CURRENT frame,
    // which means the image, the camera sees in just that moment. This is done by using the cine index -1.
    // And obviously just a single image will be transmitted.
    const gchar *cine = "-1";
    const guint frame_count = 1;
    const guint frame_start = 0;

    gchar *request;
    gchar *reply;
    gboolean is_success;

    // Given the frame count and the cine source, this function will generate a request string for the camera, that is
    // based on the configuration of the camera object (10G/1G, transfer format etc..).
    // The final string will be put into the given request pointer.
    request = create_grab_request(priv, cine, frame_start, frame_count);

    // Before we send the actual request to the camera, we need to tell the worker threads that actually receive the
    // image to start working
    start_receiving_image(priv);

    // Sending the request to the camera. In case there is not reply we will return FALSE to indicate that the grab
    // process was not successful. The reply content itself is not relevant. It is only important (just an "OK!")
    reply = phantom_talk (priv, request, NULL, 0, error);
    g_free (request);
    if (reply == NULL)
        return FALSE;
    g_free (reply);

    // This function will wait (blocking call) until the worker thread has published its results into the internal
    // result queue and then decode the image based on the used image format before copying the results into the
    // return buffer "data"
    is_success = finalize_receiving_image(priv, data, error);
    return is_success;
}

/**
 * Returns the initial offset for the position of the first trigger frame within the cine
 * memory of the camera, by first requesting the how many frames were recorded and then subtracting
 * the post trigger frames.
 *
 * CHANGELOG
 *
 * Added 20.07.2019
 *
 * Changed 22.07.2019
 * Fixed the initialization of the GValue
 *
 * Deprecated 05.11.2019
 * This whole function was build due to the assumption, that the index of images starts at zero, then start to record
 * images and once the trigger signal has been received, the camera will save only as many post trigger frames as
 * specified and then mark the cine partition as finished.
 * This is not how it works. After the trigger signal has been received, the internal index, that references frames
 * is being rebased. The first frame after the trigger has index zero. The maximum index is exactly the number of
 * post trigger frames. The frames recorded previous to the trigger will have negative indices.
 *
 * @deprecated
 *
 * @param priv
 * @return
 */
static gint
get_memread_start(UcaPhantomCameraPrivate *priv) {
    UnitVariable *var, *var1;
    guint recorded_frames_count;
    gint start_internal_index;
    guint start_index;
    gint test;

    // 22.07.2019
    GValue value = G_VALUE_INIT;
    g_value_init(&value, G_TYPE_UINT);

    GValue value1 = G_VALUE_INIT;
    g_value_init(&value1, G_TYPE_INT);

    var = phantom_lookup_by_id (PROP_RECORDED_FRAMES);
    phantom_get(priv, var, &value);
    recorded_frames_count = g_value_get_uint(&value);

    start_index = recorded_frames_count - priv->memread_count;

    return start_index;
}

/**
 * This method will block the program execution for as long as the count of recorded frames within the camera is not
 * sufficient to request another chunk of images.
 *
 * CHANGELOG
 *
 * Added 05.11.2019
 *
 * Changed 06.11.2019
 * Added a differentiation of cases for the edge case of the next request to be sent not being the full chunk size,
 * but a few remaining frames instead. In such a case the program would hang, when waiting for the camera to suffice
 * for the transmission of a full chunk.
 *
 * @param priv
 * @return
 */
static void
wait_for_frames(UcaPhantomCameraPrivate *priv) {
    // Setting up the requesting of a value from the phantom camera
    UnitVariable *var;
    guint recorded_frames_count = 0;

    GValue value = G_VALUE_INIT;
    g_value_init(&value, G_TYPE_UINT);

    // 06.11.2019
    // Here we have to differentiate if the next request will request the full chunk size or if the remaining frames
    // for a full transmission is smaller than the chunk size
    g_debug("memread remaining %i", priv->memread_remaining);
    g_debug("chunk size %i", MEMREAD_CHUNK_SIZE);
    UcaCameraTriggerSource trigger_source = priv->uca_trigger_source;

    //if (trigger_source == UCA_CAMERA_TRIGGER_SOURCE_EXTERNAL) {
        while (check_trigger_status(priv)){
            usleep(10000);
            g_debug("waiting for hw trigger");
        }
    //}

    guint request_size = (MEMREAD_CHUNK_SIZE < priv->memread_remaining) ?  MEMREAD_CHUNK_SIZE : priv->memread_remaining;
    // Waiting for as long as the recorded frames do not suffice for the request of one "chunk"
    g_debug("requested_size %i", request_size);
    g_debug("recorded frame count %i", recorded_frames_count);
    while (recorded_frames_count < request_size) {
        g_debug("waiting recorded frame count %i", recorded_frames_count);
        // Getting the frame count
        var = phantom_lookup_by_id (PROP_RECORDED_FRAMES);

        phantom_get(priv, var, &value);
        recorded_frames_count = g_value_get_uint(&value);
        g_debug("recorded frame count %i", recorded_frames_count);
    }
}

/**
 * @brief memread mode: Reads out the cameras internal memory as configured with the camera object
 *
 * The configuration for the memread mode can be made by setting a value to the "memread_count" property of the camera
 * object.
 * The first call to the grab function will then send a command to the camera, which will cause it to send out a
 * continuous stream of data, which contains exactly as many frames as specified. Each successive call to "grab" will
 * not send another request, but simply receive and decode additional frames from that data stream.
 *
 * @author Jonas Teufel
 *
 * CHANGELOG
 *
 * Added 10.05.2019
 *
 * Changed 29.05.2019
 *
 * Changed 21.07.2019
 * A negative memread_index now indicates the start of the readout for a new recording. In such a case the initial
 * offset of the recording-frames within the cine of the camera is being calculated.
 *
 * Changed 05.11.2019
 * Removed the call to the get_memread_start function, as it is not necessary to compute that.
 *
 * @param priv
 * @param data
 * @param error
 * @return
 */
static gboolean
camera_grab_memread (UcaPhantomCameraPrivate *priv,
                     gpointer data,
                     GError **error)
{
    // When using the camera in memread mode, the first grab command will send a request to the camera, that will
    // prompt it to send multiple frames in succession (as many as the camera is configured for) and then successive
    // calls to grab will not send any more requests to the camera, but just read all the received frames from the
    // buffer.
    const gchar *cine = "1";
    guint frame_count;

    gchar *request;
    gchar *reply;
    gboolean is_success;

    // Before we send the actual request to the camera, we need to tell the worker threads that actually receive the
    // image to start working
    start_receiving_image(priv);

    // 21.07.2019
    // When the memread mode is enabled and this is the first "grab" call to a new readout then the memread index will
    // have a negative number. A negative memread index indicates, that the readout for a new recording has begun.
    // In this case the starting index offset for the first frame of the recording within the cine of the camera
    // is being calculated by the "get_memread_start" function.
    if (priv->memread_index == -1) {

        // 05.11.2019
        // It turns out the get memread start method is not necessary, as the internal index within the camera will
        // always reference the first frame after the trigger with index 0
        priv->memread_index = 0;
    }

    if (!priv->memread_request_sent || priv->memread_unpack_index % MEMREAD_CHUNK_SIZE == 0) {
        // The frame count to be calculated is either the chunk size or the remaining amount, if the remaining amount
        // is less than the chunk size. We also need to the update the remaining count afterwards


        if (priv->memread_remaining < MEMREAD_CHUNK_SIZE) {
            frame_count = priv->memread_remaining;
            priv->memread_remaining = 0;
        } else {
            frame_count = MEMREAD_CHUNK_SIZE;
            priv->memread_remaining -= MEMREAD_CHUNK_SIZE;
        }

        // 05.11.2019
        // This function will block the program execution for as long as the amount of recorded frames within the
        // camera is not sufficient to request another chunk
        wait_for_frames(priv);
        // Here we have to send a new request
        // Given the frame count and the cine source, this function will generate a request string for the camera,
        // that is based on the configuration of the camera object (10G/1G, transfer format etc..).
        // The final string will be put into the given request pointer.
        request = create_grab_request(priv, cine, priv->memread_index, frame_count);
        //g_warning("REQUEST %s 10G %i", request, priv->enable_10ge);

        // Sending the request to the camera. In case there is not reply we will return FALSE to indicate that the grab
        // process was not successful. The reply content itself is not relevant. It is only important (just an "OK!")
        reply = phantom_talk (priv, request, NULL, 0, error);
        //g_warning("REPLY %s", reply);

        g_free (request);
        if (reply == NULL)
            return FALSE;
        g_free (reply);

        // After the request has been sent we set the flag to TRUE to prevent any more requests from being sent.
        priv->memread_request_sent = TRUE;
    }


    // At the end of each memread grab, we increment the index to know at which position we are
    priv->memread_index ++;

    // 21.07.2019
    // If this is the last grab call (unpack index has reached the specified count), then we obviously have to reset
    // the memread index to a negative number to indicate for the next first grab call to recalculate the initial
    // index offset.
    if (priv->memread_unpack_index == priv->memread_count) {
        priv->memread_index = -1;
    }

    // This function will wait (blocking call) until the worker thread has published its results into the internal
    // result queue and then decode the image based on the used image format before copying the results into the
    // return buffer "data"
    is_success = finalize_receiving_image(priv, data, error);
    return is_success;

}

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
 * @authors Matthias Vogelgesang, Jonas Teufel
 *
 * CHANGELOG
 *
 * Changed 10.05.2019
 * Introduced "memread" mode. If the according flag is set in the configuration of the camera object, successive calls
 * to the grab function will read out the internal memory of the camera instead of getting the current frame.
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
    UcaPhantomCameraPrivate *priv;
    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    if (priv->enable_memread) {
        camera_grab_memread(priv, data, error);
    } else {
        camera_grab_single(priv, data, error);
    }
}

// ****************
// THE MEMGATE MODE
// ****************

/**
 * @brief Enables the memgate function of the camera
 *
 * Enables the memgate function by setting the according values to the involved properties "cam.aux1mode",
 * "cam.aux1pp" and "hw.memgateen".
 *
 * @author Jonas Teufel
 *
 * CHANGELOG
 *
 * Added 22.07.2019
 *
 * @param priv
 */
static void
enable_memgate_function(UcaPhantomCameraPrivate *priv) {
    UnitVariable *var;

    var = phantom_lookup_by_id(PROP_AUX_ONE_MODE);
    phantom_set_string(priv, var, "3");

    var = phantom_lookup_by_id(PROP_HARDWARE_MEMGATE_ENABLED);
    phantom_set_string(priv, var, "1");

    var = phantom_lookup_by_id(PROP_AUX_ONE_PARAMETERS);
    phantom_set_string(priv, var, "invert delay 0.000000 filter 0.000000");
}

/**
 * @brief Disables the memgate function of the camera
 *
 * Disables the memgate function by setting the according values to the involved properties "cam.aux1mode",
 * "cam.aux1pp" and "hw.memgateen".
 *
 * @author Jonas Teufel
 *
 * CHANGELOG
 *
 * Added 22.07.2019
 *
 *
 * @param priv
 */
static void
disable_memgate_function(UcaPhantomCameraPrivate *priv) {
    UnitVariable *var;

    var = phantom_lookup_by_id(PROP_AUX_ONE_MODE);
    phantom_set_string(priv, var, "0");

    var = phantom_lookup_by_id(PROP_HARDWARE_MEMGATE_ENABLED);
    phantom_set_string(priv, var, "0");

    var = phantom_lookup_by_id(PROP_AUX_ONE_PARAMETERS);
    phantom_set_string(priv, var, "");
}

// *************************************
// GETTING AND SETTING CAMERA ATTRIBUTES
// *************************************

/**
 * @brief Sets the properties of the phantom camera
 *
 * CHANGELOG
 *
 * Added ?
 *
 * Changed 10.05.2019
 * Added the cases for the properties of the memread mode, which are the boolean flag PROP_ENABLE_MEMREAD and the
 * amount of images to load from the camera PROP_MEMREAD_COUNT
 *
 * Changed 26.05.2019
 * Added the additional case for the property PROP_NETWORK_ADDRESS, which enables the manual setting of the phantoms
 * IP address in cases, where the discovery protocol fails/is unavailable.
 *
 * Changed 29.05.2019
 * Added additional initialization of properties to the case for PROP_MEMREAD_COUNT, which include setting the index,
 * that keeps track of the memread grab calls back to 0 and also setting the remaining amount to the count specified
 * for the memread count.
 *
 * Changed 16.07.2019
 * Added the case for the property PROP_EXTERNAL_TRIGGER, which is a boolean flag to indicate whether or not the
 * external triggering during a recording is to be enabled.
 *
 * Changed 21.07.2019
 * The memread index is now being set to -1 instead of 0 to clearly indicate, that no grab command has been
 * called yet. The grab function needs to realize this state to calculate the initial offset of the starting
 * position within the cine
 *
 * Changed 22.07.2019
 * Added the case for the PROP_ENABLE_MEMGATE, which is a boolean flag to enable and disable the memgate mode of the
 * first programmable IO of the camera.
 *
 * Changed 05.11.2019
 * Added the setter for PROP_POST_TRIGGER_FRAMES. This value defines how many frames the camera will record after a
 * trigger event has been received.
 * Additionally to setting the post trigger frames within the camera, the memread count is also set to the very same
 * value within this setter, as in 99% of all cases you would want it to be the same anyways. In case you dont, you can
 * still manually change it afterwards.
 *
 * @param object
 * @param property_id
 * @param value
 * @param pspec
 */
static void
uca_phantom_camera_set_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
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
        // 10.05.2019
        // The properties for the memread mode
        case PROP_ENABLE_MEMREAD:
            priv->enable_memread = g_value_get_boolean(value);
            priv->memread_request_sent = FALSE;
            break;
        case PROP_MEMREAD_COUNT:
            priv->memread_count = g_value_get_uint(value);
            // 29.05.2019
            // Whenever a new memread count is specified (indicating the intention to read out more frames) the index,
            // which keeps track of the grab calls needs to be reset and the remaining count is set to the total amount
            // initially.

            break;
        // 26.05.2019
        // Adding an additional property to manually set the IP address in cases, where the discovery mode might not
        // work
        case PROP_NETWORK_ADDRESS:
            priv->ip_address = g_value_dup_string (value);
            break;
        // 11.06.2019
        // Connecting to the camera with the "start_recodring" call doesnt make a whole lot of sense. So the connection
        // process is now handled by setting this boolean flag to TRUE.
        case PROP_CONNECT:
            // Setting the boolean flag of the object to indicate the state
            priv->connected = g_value_get_boolean(value);
            // if it was set to TRUE, the "connect" function will be executed
            if (priv->connected) {
                GError *error = NULL;
                phantom_connect(priv, &error);
            }
            break;
        // 16.07.2019
        // A boolean flag to indicate whether external triggering is to be enabled during a recording.
        case PROP_EXTERNAL_TRIGGER:
            priv->triggered_externally = g_value_get_boolean(value);
            break;
        // 22.07.2019
        // A boolean flag, with which the memgate function can be disabled. The memgate function is when a HIGH signal
        // on the first programmable IO port of the camera interrupts the saving of all frames to the cine memory.
        // The process of enabling the memgate mode requires the setting of SEVERAL properties of the camera, thus the
        // operations are capsuled into their own functions "enable_memgate_function" and
        // "disable_memgate_function" respectively.
        case PROP_ENABLE_MEMGATE:
            if (g_value_get_boolean(value)) {
                enable_memgate_function(priv);
            } else {
                disable_memgate_function(priv);
            }
            break;
        // 05.11.2019
        // This handles the case of the post-trigger-frames being set. This number defines how many frames the camera
        // records after receiving a trigger event.
        // The memread count is additionally being set to the very same value.
        case PROP_POST_TRIGGER_FRAMES: {
            guint val = g_value_get_uint(value);
            g_value_set_uint(value, val);
            // First we actually set the camera to this value
            phantom_set(priv, var, value);

            // but then we also have to set the memread count to the same value!
            // The following code section has been copied from the previous setter of memread-count.
            priv->memread_count = val;
            // 29.05.2019
            // Whenever a new memread count is specified (indicating the intention to read out more frames) the index,
            // which keeps track of the grab calls needs to be reset and the remaining count is set to the total amount
            // initially.
            priv->memread_remaining = priv->memread_count;
            priv->memread_request_sent = FALSE;
            // 21.07.2019
            // The memread index is now being set to -1 instead of 0 to clearly indicate, that no grab command has been
            // called yet. The grab function needs to realize this state to calculate the initial offset of the starting
            // position within the cine
            priv->memread_index = -1;
            // 11.06.2019
            // We obviously also need to reset the unpack index
            priv->memread_unpack_index = 0;
        }
            break;
    }
}

/**
 * @brief Gets the properties of the phantom camera
 *
 * CHANGELOG
 *
 * Added ?
 *
 * Changed 26.06.2019
 * Added a case for the "PROP_NETWORK_ADDRESS", as it is supposed to be a read-write property, but did not have a read
 * functionality defined, which lead to an error, when attempting to read it.
 * Also added a default case, which returns an empty string to prevent such an error from happening in the future.
 *
 * Changed 05.11.2019
 * Added a case for PROP_MAX_FRAMES, which will return the maximum number of frames, that fit into the primary cine
 * memory. It is a computed memory and for the calcualtion it recursively calls this function to get the values of
 * PROP_FRAME_SIZE and PROP_MEMORY_SIZE.
 *
 * Changed 05.11.2019
 * Added a case for PROP_POST_TRIGGER_FRAMES. This is not being handled automatically anymore. It holds the value of
 * how many frames the camera will record, after a trigger event occcured.
 *
 * @param object
 * @param property_id
 * @param value
 * @param pspec
 */
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
            {
                UnitVariable *var;

                GValue value_index = G_VALUE_INIT;
                g_value_init(&value_index, G_TYPE_UINT);

                var = phantom_lookup_by_id (PROP_ACQUISITION_MODE_INDEX);

                guint index;

                // Getting the frame size
                phantom_get(priv, var, &value_index);

                index = g_value_get_uint(&value_index);
                g_value_set_enum (value, index);
            }
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
        // 11.06.2019
        // Returns the boolean value of whether or nor the camera is currently connected to the program.
        case PROP_CONNECT:
            g_value_set_boolean(value, priv->connected);
            break;
        // Returns the boolean value of whether the frame trigger process is done yet or not
        case PROP_TRIGGER_RELEASED:
            g_value_set_boolean(value, check_trigger_status(priv));
            break;
        // 26.06.2019
        // The network address is a string of the IP address of the phantom camera, which has to be known before
        // attempting to connect.
        case PROP_NETWORK_ADDRESS:
            g_value_set_string(value, priv->ip_address);
            break;
        case PROP_ENABLE_MEMREAD:
            g_value_set_boolean(value, priv->enable_memread);
            break;
        case PROP_EXTERNAL_TRIGGER:
            g_value_set_boolean(value, priv->triggered_externally);
            break;
        case PROP_MEMREAD_COUNT:
            g_value_set_uint(value, priv->memread_count);
            break;
        case PROP_TRIGGER_SOURCE:
            g_value_set_enum(value, priv->uca_trigger_source);
            break;
        // 05.11.2019
        // This property will return the maximum number of frames that can be fit into the primary cine memory.
        case PROP_MAX_FRAMES:
            {
                UnitVariable *var, *var1;

                // 22.07.2019
                GValue value_frame_size = G_VALUE_INIT;
                g_value_init(&value_frame_size, G_TYPE_UINT);

                GValue value_memory_size = G_VALUE_INIT;
                g_value_init(&value_memory_size, G_TYPE_UINT);

                var = phantom_lookup_by_id (PROP_FRAME_SIZE);
                var1 = phantom_lookup_by_id (PROP_MEMORY_SIZE);

                guint frame_size;
                guint memory_size;
                guint max_frames;

                // Getting the frame size
                phantom_get(priv, var, &value_frame_size);
                phantom_get(priv, var1, &value_memory_size);
                frame_size = g_value_get_uint(&value_frame_size);

                // Getting the total memory size
                memory_size = g_value_get_uint(&value_memory_size);

                // g_warning("frame size %i, memory size %i", frame_size, memory_size);

                // Computing the frame amount as the amount of frame sizes, that can be fit into the total cine memory size
                max_frames = (guint) (memory_size / frame_size);

                g_value_set_uint(value, max_frames);
            }
            break;
        // 05.11.2019
        // The post trigger frames are now not being handled automatically anymore, because in the setter we have to
        // include the custom code, that sets the memread_count variable to the same value...
        case PROP_POST_TRIGGER_FRAMES:
            phantom_get (priv, var, value);
        default:
            g_value_set_string(value, "NO READ FUNCTIONALITY IMPLEMENTED!");
            break;
    }
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

    // This causes the segmentation fault at the end
    //g_free (priv->iface);


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


// **********************************
// CAMERA OBJECT INITIALIZATION STUFF
// **********************************



static void
uca_phantom_camera_constructed (GObject *object)
{
    UcaPhantomCameraPrivate *priv;
    GSocketAddress *addr;

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (object);
}

static void
uca_phantom_camera_initable_iface_init (GInitableIface *iface)
{
    iface->init = ufo_net_camera_initable_init;
}

/**
 * Sets up the camera class
 *
 * Added ?
 *
 * Changed 05.11.2019
 * Added the registration for the property PROP_MAX_FRAMES, which will hold the value of the maximum amount of frames,
 * which will fit into the primary cine partition.
 *
 * @param klass
 */
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

    // Here we set the additional function for reading out multiple cines

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
            TRUE, G_PARAM_READWRITE);

    phantom_properties[PROP_NETWORK_INTERFACE] =
        g_param_spec_string ("network-interface",
            "Network interface name of the 10GE device",
            "Network interface name of the 10GE device",
            "", G_PARAM_READWRITE);
            
    phantom_properties[PROP_ENABLE_MEMREAD] = 
        g_param_spec_boolean ("enable-memread", 
            "Enable readout of cine memory",
            "Enable readout of cine memory",
            FALSE, G_PARAM_READWRITE);
    
    phantom_properties[PROP_MEMREAD_CINE] = 
    g_param_spec_uint ("memread-cine",
            "The index of the cine, from which to read frames",
            "The index of the cine, from which to read frames",
            0, G_MAXUINT, 0, G_PARAM_READWRITE);
            
    phantom_properties[PROP_MEMREAD_COUNT] = 
    g_param_spec_uint ("memread-count",
            "The number of frames to be read from memory",
            "The number of frames to be read from memory",
            0, G_MAXUINT, 0, G_PARAM_READWRITE);
            
    phantom_properties[PROP_MEMREAD_START] = 
    g_param_spec_uint ("memread-start",
            "The number of the frame, after which to start the readout in the cine",
            "The number of the frame, after which to start the readout in the cine",
            0, G_MAXUINT, 0, G_PARAM_READWRITE);

    phantom_properties[PROP_NETWORK_ADDRESS] =
    g_param_spec_string ("network-address",
            "The network IP address of the phantom camera",
            "The network IP address of the phantom camera",
            "", G_PARAM_READWRITE);

    // POST FORK ADDITIONS by Jonas Teufel

    // A boolean flag to initialize the connection process
    phantom_properties[PROP_CONNECT] =
            g_param_spec_boolean ("connect",
                                  "Connect to the camera using the ethernet connection",
                                  "Connect to the camera using the ethernet connection",
                                  FALSE, G_PARAM_READWRITE);

    // A boolean flag to indicate, whether the frame acquisition caused by a trigger is done yet
    phantom_properties[PROP_TRIGGER_RELEASED] =
            g_param_spec_boolean ("trigger-released",
                                  "Whether the triggered frame acquisition process has finished",
                                  "Whether the triggered frame acquisition process has finished",
                                  FALSE, G_PARAM_READABLE);

    // 30.06.2019
    // Integer mode proprty setting the function of the first auxiliary port of the camera
    phantom_properties[PROP_AUX_ONE_MODE] =
            g_param_spec_uint ("aux-mode",
                               "The integer specifying which function the first auxiliary port will have",
                               "The integer specifying which function the first auxiliary port will have",
                               0, G_MAXUINT, 0, G_PARAM_READWRITE);

    // 16.07.2019
    // Boolean flag, of whether or not the external triggering is to be enabled during the "start_recording" process
    phantom_properties[PROP_EXTERNAL_TRIGGER] =
            g_param_spec_boolean ("external-trigger",
                                  "Flag of whether or not the camera is to be triggered externally during recording",
                                  "Flag of whether or not the camera is to be triggered externally during recording",
                                  FALSE, G_PARAM_READWRITE);

    // 22.07.2019
    // Boolean flag to enable and disable memgate programmable IO function
    phantom_properties[PROP_ENABLE_MEMGATE] =
            g_param_spec_boolean ("enable-memgate",
                                  "Enable memgate aux port 1 function, which will block frame saving in HIGH pulse",
                                  "Enable memgate aux port 1 function, which will block frame saving in HIGH pulse",
                                  FALSE, G_PARAM_READWRITE);

    // 05.11.2019
    // This is the property, which will indicate the maximum amount of frames, that will fit into the primary
    // cine partition
    phantom_properties[PROP_MAX_FRAMES] =
            g_param_spec_uint ("max-frames",
                                "The maximum number of frames fitting into the primary cine partition",
                                "The maximum number of frames fitting into the primary cine partition",
                                0, G_MAXUINT, 0, G_PARAM_READABLE);

    // 11.11.2019
    phantom_properties[PROP_ACQUISITION_MODE_INDEX] =
            g_param_spec_uint ("acquisition-mode-index",
                               "The maximum number of frames fitting into the primary cine partition",
                               "The maximum number of frames fitting into the primary cine partition",
                               0, G_MAXUINT, 0, G_PARAM_READABLE);

    for (guint i = 0; i < base_overrideables[i]; i++)
        g_object_class_override_property (oclass, base_overrideables[i], uca_camera_props[base_overrideables[i]]);

    for (guint id = N_BASE_PROPERTIES; id < N_PROPERTIES; id++)
        g_object_class_install_property (oclass, id, phantom_properties[id]);

    g_type_class_add_private (klass, sizeof(UcaPhantomCameraPrivate));
}



/**
 * @brief Initializes important attributes of the camera object
 *
 * CHANGELOG
 *
 * Added ?
 *
 * Changed 26.06.2019
 * The method now checks for the existance and the values of the environmental variables "PH_NETWORK_ADDRESS" and
 * "PH_NETWORK_INTERFACE" to possibly set the values for the ip address and the 10G interface specifier (also implicitly
 * enables 10G transmission).
 * Also if the IP property has been given via an environmental variable, the init function will also call the connect
 * function at the end.
 *
 * Changed 05.11.2019
 * Extended the regex pattern for matching the responses of the camera to also recognize negative numbers, which it was
 * previously unable to do.
 */
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
    priv->acquisition_mode = ACQUISITION_MODE_HS;
    priv->enable_10ge = FALSE;
    priv->xg_packet_skipped = FALSE;
    priv->iface = NULL;
    priv->ip_address = "";
    priv->have_ximg = TRUE;
    priv->xg_packet_amount = 0;
    priv->connected = FALSE;
    priv->message_queue = g_async_queue_new ();
    priv->result_queue = g_async_queue_new ();

    // 26.06.2019
    // The g_getenv functions return the string value of the specified environmental variable name if it exists and
    // NULL if it does not exists.
    // So the network address and the interface for 10G can be specified using an environmental variable
    const gchar *phantom_ethernet_interface = g_getenv("PH_NETWORK_INTERFACE");
    const gchar *phantom_ip_address = g_getenv("PH_NETWORK_ADDRESS");
    //g_warning("IP: %s", phantom_ip_address);
    if (phantom_ethernet_interface != NULL) {
        priv->enable_10ge = TRUE;
        priv->iface = phantom_ethernet_interface;
    }
    if (phantom_ip_address != NULL) {
        priv->ip_address = phantom_ip_address;
    }

    // 05.11.2019
    // This regex pattern was actually not able to detect a negative number, so it has been extended to recognize
    // an optional dash in front of any numerals within the reply message.
    priv->response_pattern = g_regex_new ("\\s*([A-Za-z0-9]+)\\s*:\\s*{?\\s*\"?(-?[A-Za-z0-9\\s]+)\"?\\s*}?", 0, 0, NULL);

    priv->res_pattern = g_regex_new ("\\s*([0-9]+)\\s*x\\s*([0-9]+)", 0, 0, NULL);

    uca_camera_register_unit (UCA_CAMERA (self), "frame-delay", UCA_UNIT_SECOND);
    uca_camera_register_unit (UCA_CAMERA (self), "sensor-temperature", UCA_UNIT_DEGREE_CELSIUS);
    uca_camera_register_unit (UCA_CAMERA (self), "camera-temperature", UCA_UNIT_DEGREE_CELSIUS);

    // 26.06.2019
    // If an IP address has been given (via an env variable) it does not make sense to wait any longer before
    // connecting, so the connect method is called
    if (priv->ip_address != "") {
        phantom_connect(priv, NULL);
    }
}

G_MODULE_EXPORT GType
camera_plugin_get_type (void)
{
    return UCA_TYPE_PHANTOM_CAMERA;
}
