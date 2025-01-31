#ifndef UCA_PHANTOM_COMMUNICATE_H
#define UCA_PHANTOM_COMMUNICATE_H

#include <glib-object.h>
#include <uca/uca-camera.h>
#include "uca-phantom-properties.h"

G_BEGIN_DECLS

// Max number of bytes that can be sent in a single request
#ifndef USER_MAX_NETWORK_REQUEST_SIZE
#define USER_MAX_NETWORK_REQUEST_SIZE 500000000
#endif
// Max number of images that can be stored in the live images ring buffer
#ifndef USER_MAX_BUFFERED_IMAGES
#define USER_MAX_BUFFERED_IMAGES 40
#endif
// libpcap timeout
#ifndef USER_PCAP_TIMEOUT
#define USER_PCAP_TIMEOUT 5000  // ms
#endif
// Use mempool
#ifndef USER_USE_MEMPOOL
#define USER_USE_MEMPOOL FALSE
#endif
// Throttle based on the the number of images grabbed by user or the number of images received
#ifndef USER_NO_DROP
#define USER_NO_DROP FALSE
#endif
// Throttling performance
#ifndef USER_THROTTLE_FACTOR
#define USER_THROTTLE_FACTOR .75
#endif
// Max number of threads for openmp unpacking
#ifndef USER_NUM_THREADS
#define USER_NUM_THREADS 16
#endif

#define UCA_TYPE_PHANTOM_COMMUNICATE (uca_phantom_communicate_get_type ())
G_DECLARE_FINAL_TYPE (UcaPhantomCommunicate, uca_phantom_communicate, UCA, PHANTOM_COMMUNICATE, GObject)

typedef struct _PhantomRequest PhantomRequest;
typedef struct _PhantomReply PhantomReply;

#define UCA_PHANTOM_COMMUNICATE_ERROR (uca_phantom_communicate_error_quark ())
typedef enum {
    // Phantom general error codes
    UCA_PHANTOM_COMMUNICATE_ERROR_INIT,
    UCA_PHANTOM_COMMUNICATE_ERROR_REGEX,
    // Phantom connection error codes
    UCA_PHANTOM_COMMUNICATE_ERROR_BCAST_ADDR,
    UCA_PHANTOM_COMMUNICATE_ERROR_SOCKET,
    UCA_PHANTOM_COMMUNICATE_ERROR_SEND,
    UCA_PHANTOM_COMMUNICATE_ERROR_RECEIVE,
    UCA_PHANTOM_COMMUNICATE_ERROR_ADRESS,
    UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT,
    UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_DATASTREAM,
    UCA_PHANTOM_COMMUNICATE_ERROR_CONNECT_XDATASTREAM,
    UCA_PHANTOM_COMMUNICATE_ERROR_GET_MAC_ADDRESS,
    // Phantom communication error codes
    UCA_PHANTOM_COMMUNICATE_ERROR_GET_VARIABLE,
    UCA_PHANTOM_COMMUNICATE_ERROR_SET_VARIABLE,
    UCA_PHANTOM_COMMUNICATE_ERROR_RUN_COMMAND,
    UCA_PHANTOM_COMMUNICATE_ERROR_NOTIFY,
    UCA_PHANTOM_COMMUNICATE_ERROR_SET_NB_CINES,
    UCA_PHANTOM_COMMUNICATE_ERROR_GET_CINE_INDEX,
    UCA_PHANTOM_COMMUNICATE_ERROR_GET_SETTINGS,
    UCA_PHANTOM_COMMUNICATE_ERROR_SET_SETTINGS,
    UCA_PHANTOM_COMMUNICATE_ERROR_GET_RESOLUTION,
    UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING,
    UCA_PHANTOM_COMMUNICATE_ERROR_STOP_RECORDING,
    UCA_PHANTOM_COMMUNICATE_ERROR_DELETE_CINE,
    UCA_PHANTOM_COMMUNICATE_ERROR_TRIGGER,
    UCA_PHANTOM_COMMUNICATE_ERROR_REQUEST_IMAGES,
    UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE,
    UCA_PHANTOM_COMMUNICATE_ERROR_GRAB_IMAGE,
    UCA_PHANTOM_COMMUNICATE_ERROR_GRAB_LIVE_IMAGE,
    UCA_PHANTOM_COMMUNICATE_ERROR_GRAB_TIMESTAMP,
    UCA_PHANTOM_COMMUNICATE_ERROR_ACCEPT_XIMG,
    UCA_PAHTNOM_COMMUNICATE_ERROR_MEMPOOL,
    UCA_PHANTOM_COMMUNICATE_ERROR_ACCEPT_IMG,
    UCA_PHANTOM_COMMUNICATE_ERROR_ACCEPT_TIMESTAMPS,
    UCA_PHANTOM_COMMUNICATE_ERROR_DISCONNECT_DATASTREAM,
    UCA_PHANTOM_COMMUNICATE_ERROR_STOP_READOUT,
    UCA_PHANTOM_COMMUNICATE_ERROR_NEXT_EVENT,
    UCA_PHANTOM_COMMUNICATE_ERROR_NO_DATA,
    UCA_PHANTOM_COMMUNICATE_ERROR_INVALID_ARGUMENT,
    UCA_PHANTOM_COMMUNICATE_ERROR_MAYBE_CORRUPTED
} UcaPhantomCommunicateError;

/**
 * @brief Enumeration of synchronization modes for Phantom camera image capture.
 * 
 * This enumeration defines the possible synchronization modes that can be used when capturing images with the Phantom camera.
 * 
 * SYNC_MODE_FREE_RUN: Free run synchronization mode.
 * SYNC_MODE_FSYNC: Fsync synchronization mode.
 * SYNC_MODE_IRIG: IRIG synchronization mode.
 * SYNC_MODE_VIDEO_FRAME_RATE: Video frame rate synchronization mode.
 */
typedef enum {
    SYNC_MODE_FREE_RUN = 0,
    SYNC_MODE_FSYNC,
    SYNC_MODE_IRIG,
    SYNC_MODE_VIDEO_FRAME_RATE,
    SYNC_MODE_TRIGGER = 5
} SyncMode;

/**
 * @brief Enumeration of acquisition modes for Phantom camera image capture.
 * 
 * This enumeration defines the possible acquisition modes that can be used when capturing images with the Phantom camera.
 * 
 * ACQUISITION_MODE_STANDARD: Standard acquisition mode.
 * ACQUISITION_MODE_STANDARD_BINNED: Standard acquisition mode with binning.
 * ACQUISITION_MODE_HS: High-speed acquisition mode.
 * ACQUISITION_MODE_HS_BINNED: High-speed acquisition mode with binning.
 * ACQUISITION_MODE_BRIGHT_FIELD: Bright field acquisition mode.
 */
typedef enum {
    ACQUISITION_MODE_STANDARD = 0,
    ACQUISITION_MODE_STANDARD_BINNED = 2,
    ACQUISITION_MODE_HS = 5,
    ACQUISITION_MODE_HS_BINNED = 7,
    ACQUISITION_MODE_BRIGHT_FIELD
} AcquisitionMode;

/**
 * @brief Enumeration of auto exposure modes for Phantom camera image capture.
 * 
 * This enumeration defines the possible auto exposure modes that can be used when capturing images with the Phantom camera.
 * 
 */
typedef enum {
    AUTOEXP_MODE_OFF = 0,      /**< Auto exposure is turned off. */
    AUTOEXP_MODE_AVERAGE,      /**< Auto exposure is based on the average brightness of the image. */
    AUTOEXP_MODE_SPOT,         /**< Auto exposure is based on a spot meter reading. */
    AUTOEXP_MODE_CENTER        /**< Auto exposure is based on the center of the image. */
} AutoexpMode;

/**
 * @brief The bit depth of the image format.
 * 
 * This field specifies the bit depth of the image (i.e. image format). 
 * For example, if the image format is 8-bit, this field will be set to 8.
 * 
 */
typedef enum {
    IMG_8, /** 8 bits per pixel, FPN and PRNU corrected, linear, raw */
    IMG_8R, /** 8 bits per pixel, uncorrected, linear, raw */
    IMG_P16, /** 16 bits per pixel, FPN and PRNU corrected, linear, raw, little-endian. The range of values is 0-65535. */
    IMG_P16R, /** Same as P16 but uncorrected */
    IMG_P10, /** 10 bits per pixel packed into 32-bit big-endian words, FPN and PRNU corrected, non-linear, raw. */
    IMG_P12L /** 12 bits per pixel packed into 32-bit big-endian words, FPN and PRNU corrected, linear, raw. */
} ImageFormat;

/**
 * @brief Enumeration of timestamp formats for Phantom camera image capture.
 * 
 * This enumeration defines the possible timestamp formats that can be requested when capturing images with the Phantom camera.
 * 
 */
typedef enum {
    TS_SHORT,   /**< Short timestamp format. */
    TS_SHORT32, /**< 32-bit short timestamp format. */
    TS_LONG,    /**< Long timestamp format. */
    TS_LONG32,  /**< 32-bit long timestamp format. */
    TS_NONE     /**< No timestamp is requested. */
} TimestampFormat;

/**
 * @brief Enumeration of IP source flags for the TCP connection.
 * 
 * This enumeration defines the possible IP source flags that can be used to choose the IP source for the TCP connection
 * in the `connect_datastream` function.
 * 
 * USE_ENV: Use the IP address specified in the environment variable.
 * USE_CLASS: Use the IP address specified in the class.
 * USE_BCAST: Use the broadcast IP address.
 * N_IP_FLAGS: The number of IP source flags.
 */
typedef enum {
    USE_ENV,
    USE_CLASS,
    USE_BCAST,
    N_IP_FLAGS
} IpSource;

typedef enum {
    CINE_INV, // The cine is invalid; it has no memory allocated, nor does it participate in any way in camera operations.
    CINE_STR, // The cine contains a complete, valid recording.
    CINE_WTR, // The camera is currently recording this cine, and waiting for trigger.
    CINE_TRG, // A trigger has been received and accepted for this cine.
    CINE_RDY, // The cine is ready to receive a recording; RDY and STR cannot be present at the same time.
    CINE_DEF, // If this flag is set, when acquisition starts into this cine, the acquisition parameters are first copied from the default cine, defc. In ph16, all cines have the DEF flag set at all times.
    CINE_ABL, // If this flag is set, the cine can accept a trigger.
    CINE_PRE, // This flag marks a special cine that is used to obtain live preview images when all the other cines are full. Normally, c0 and only c0 has the PRE flag set. The preview cine never has any of ABL, WTR, TRG or STR set.
    CINE_ACT, // This flag marks the active cine, e.g. the cine into which images are acquired; only one cine can be active at any time
    CINE_REU // The cine content has been saved, so it can be deleted by the camera if the auto.acqrestart option is set.
} CineStatus;

/**
 * @brief Struct containing capture settings for the Phantom camera.
 * 
 * This struct contains various properties related to capturing images with the Phantom camera, 
 * including sensor pixel width and height, bit depth, trigger source and type, frames per second, 
 * exposure time, region of interest (ROI), focal length, aperture, EDR exposure time, shutter offset, 
 * auto exposure mode and compensation, number of pre- and post-trigger frames, current cine number, 
 * sync mode, acquisition mode, image format, and timestamp format.
 * 
 * The list of properties is not exhaustive, and is subject to change.
 */
typedef struct{
    // base properties
    guint sensor_width, sensor_height; // In pixels
    guint roi_x0, roi_y0, roi_width, roi_height; // In pixels
    guint sensor_bit_depth; // In bits

    gdouble frames_per_second;
    gdouble exposure_time;

    // phantom specific properties
    guint window_width, window_height;
    gfloat focal_length, aperture;
    guint edr_exp; // EDR exposure time
    gboolean shutter_off, crop;
    guint nb_post_trigger_frames, nb_pre_trigger_frames;
    guint current_cine; // Current cine number in which the camera is recording
    gfloat aexpcomp; // Auto exposure compensation


    AutoexpMode aexpmode;
    UcaCameraTriggerSource trigger_source;
    UcaCameraTriggerType trigger_type;
    SyncMode sync_mode;
    AcquisitionMode acquisition_mode;
    ImageFormat image_format;
    TimestampFormat timestamp_format;
} CaptureSettings;

typedef struct {
    gchar* format_string;
    guint bit_depth;
    gfloat byte_depth;
} ImageFormatSpec;

typedef struct
{
    gchar* format_string;
    gsize byte_size;
} TimestampSpec;

extern const ImageFormatSpec ImageFormatSpecs[];
extern const TimestampSpec TimestampSpecs[];

/**
 * @brief Creates a new UcaPhantomCommunicate object.
 * 
 * This function creates a new UcaPhantomCommunicate object.
 * 
 * @return UcaPhantomCommunicate* A pointer to the newly created UcaPhantomCommunicate object.
 */
UcaPhantomCommunicate *uca_phantom_communicate_new (void);

/**
 * @brief Connects to the TCP phantom control stream and returns a boolean indicating success or failure.
 * 
 * You need to connect the controlstream before you can send commands to the phantom.
 *
 * @param self A pointer to the UcaPhantomCommunicate object.
 * @param error_loc A pointer to a GError object that will be set if an error occurs.
 * @return gboolean Returns TRUE if the connection was successful, FALSE otherwise.
 *
 * @note The caller is responsible for freeing the GError object if it is set.
 */
gboolean uca_phantom_communicate_connect_controlstream (UcaPhantomCommunicate *self, GError **error_loc);

/**
 * @brief Opens TCP data connection with the Phantom camera.
 *
 * This function uses the port `self->data_port` to open a TCP connection with the Phantom camera.
 * This connection only takes timestamps and images of bitdeph 8 or 16.
 *
 * @param self The UcaPhantomCommunicate object.
 * @param error_loc A pointer to a GError pointer to store any errors that occur.
 * @return TRUE if the socket was opened successfully, FALSE otherwise.
 */
gboolean uca_phantom_communicate_connect_datastream(UcaPhantomCommunicate *self, GError **error_loc);

/**
 * @brief Opens a socket that accepts raw ethernet data frames from the Phantom.
 *
 * This function uses libpcap to capture ethernet frames from the NIC called `xnetcard`.
 *
 * @param self The UcaPhantomCommunicate object.
 * @param error_loc A pointer to a GError pointer to store any errors that occur.
 * @return TRUE if the socket was opened successfully, FALSE otherwise.
 */
gboolean uca_phantom_communicate_connect_xdatastream (UcaPhantomCommunicate *self, GError **error_loc);

/**
 * @brief Send a command to the phantom and get the reply
 *
 * @paragraph This function sends a command to the phantom and gets the reply.
 * The command is specified by the command_flag. The reply is stored in the caller-owned reply struct.
 * If the reply is NULL, a local one will be created.
 *
 * @param self
 * @param command_flag
 * @param command_arg
 * @param reply (optional)
 * @param error_loc
 * @return gboolean
 *
 */
GString *uca_phantom_communicate_run_command(UcaPhantomCommunicate *self, guint command_flag, gchar* command_arg, GError **error_loc);


/**
 * @brief Disconnects the data stream from the phantom.
 *
 * This function closes the input data stream and sets the data connection state to DISCONNECTED.
 *
 * @param self The UcaPhantomCommunicate object.
 * @param error_loc A pointer to a GError pointer to store any errors that occur.
 * @return TRUE if the data stream was disconnected successfully, FALSE otherwise.
 */
gboolean uca_phantom_communicate_disconnect_datastream(UcaPhantomCommunicate *self, GError **error_loc);

/**
 * @brief Disconnects the xdata stream from the phantom.
 *
 * This function closes the pcap handle and sets the xdata connection state to DISCONNECTED.
 *
 * @param self The UcaPhantomCommunicate object.
 * @param error_loc A pointer to a GError pointer to store any errors that occur.
 * @return TRUE if the xdata stream was disconnected successfully, FALSE otherwise.
 */
gboolean uca_phantom_communicate_disconnect_xdatastream(UcaPhantomCommunicate *self, GError **error_loc);

/**
 * @brief Get the value of a variable from the phantom
 *
 * @paragraph This function will get the value of a unit variable from the phantom using the uca_phantom_communicate_run_command function.
 *
 * @param self
 * @param variable_flag
 * @param cine // optional
 * @param return_value
 * @param error_loc
 * @return gboolean
 */
gboolean uca_phantom_communicate_get_variable (UcaPhantomCommunicate *self, guint variable_flag, gint cine, GValue *return_value, GError **error);

/**
 * @brief Set the value of a variable on the phantom
 *
 * @param self
 * @param variable_flag
 * @param value
 * @param error_loc
 * @return gboolean
 */
gboolean uca_phantom_communicate_set_variable (UcaPhantomCommunicate *self, guint variable_flag, const char *set_value, GError **error_loc);

/**
 * @brief Helpher function to set the number of cines.
 *
 * This function sets the number of cines in the Phantom camera using set_variable function.
 *
 * @param self A pointer to the UcaPhantomCommunicate object.
 * @param nb_cines The number of cines to set.
 * @param error_loc A pointer to a GError object to store any errors that occur.
 *
 * @return TRUE if the number of cines was set successfully, FALSE otherwise.
 * 
 * TODO: make sure the size of each cine is big enough to hold the images.
 */
gboolean uca_phantom_communicate_set_nb_cines (UcaPhantomCommunicate *self, guint nb_cines, GError **error_loc);

/**
 * @brief Sets the main capture settings of the Phantom camera.
 *
 * This function sets a few crucial settings for the Phantom camera, such as the sensor resolution, EDR exposure, shutter off, auto exposure mode, auto exposure compensation, and number of post trigger frames.
 *
 * @param self The UCA Phantom camera instance.
 * @param ext_settings The external settings to be set.
 * @param error_loc The location to store any errors that occur.
 *
 * @return TRUE if the settings were successfully set, FALSE otherwise.
 */
gboolean uca_phantom_communicate_set_settings (UcaPhantomCommunicate *self, CaptureSettings *settings, GError **error_loc);

/**
 * @brief Starts the readout process for the UcaPhantomCommunicate object
 * 
 * @details Starts a new thread to accept data from the camera (and unpack it if data is transmitted over 10Gb). 
 * If timestamping is enabled, it also starts a new thread to read timestamps. The function returns TRUE if the 
 * readout was successfully started, FALSE otherwise.
 * 
 * @param self Pointer to the UcaPhantomCommunicate object
 * @param error_loc Pointer to a GError object to store any errors that occur
 * @return gboolean TRUE if the readout was successfully started, FALSE otherwise
 */
gboolean uca_phantom_communicate_start_readout (UcaPhantomCommunicate* self, gboolean buffering, CaptureSettings *settings, GError **error_loc);

/**
 * @brief Stop readout function
 *
 * This function stops (waits for) all the threads that were started by uca_phantom_communicate_start_readout.
 * 
 * @param self Pointer to the UcaPhantomCommunicate object
 * @param error_loc Pointer to a GError object to store any errors that occur
 * @return gboolean TRUE if the readout was successfully stopped, FALSE otherwise
 */
gboolean uca_phantom_communicate_stop_readout(UcaPhantomCommunicate *self, GError **error_loc);

gboolean uca_phantom_communicate_release_cine(UcaPhantomCommunicate* self, gint cine, GError** error_loc);
gboolean uca_phantom_communicate_delete_cine(UcaPhantomCommunicate* self, gint cine, GError** error_loc);

/**
 * @brief Arms the phantom for acquisition in a cine partition.
 *
 * Once the phantom is armed, it will start recording in the cine partition at the rate set by the 
 * SyncMode flags. 
 *
 * @param self The UcaPhantomCommunicate object
 * @param cine The cine partition to record in
 * @param error_loc A GError object to store the error in
 * @return TRUE if the arm was successful, FALSE otherwise
 */
gboolean uca_phantom_communicate_arm (UcaPhantomCommunicate *self, gint cine, GError **error_loc);
gboolean uca_phantom_communicate_disarm (UcaPhantomCommunicate* self, GError** error_loc);

/**
 * @brief Trigger the phantom's internal cine RAM partition.
 * 
 * This will keep the frames stored in the cine RAM up until the trigger, and continue to store `post-frames` after the trigger.
 * The total number of frames in the cine partition is therefore `pre-frames + post-frames`.
 *
 * @param self The UcaPhantomCommunicate object
 * @param error_loc A GError object to store the error in
 * @return TRUE if the trigger was successful, FALSE otherwise
 */
gboolean uca_phantom_communicate_trigger (UcaPhantomCommunicate *self, GError **error_loc);

GString *uca_phantom_communicate_get_cine_state (UcaPhantomCommunicate *self, gint cine, GError **error_loc);
gboolean uca_phantom_communicate_verify_cine_state(UcaPhantomCommunicate *self, gint cine, gchar *flag, GError **error_loc);
gboolean uca_phantom_communicate_get_active_cine (UcaPhantomCommunicate *self, gint *cine, GError **error_loc);
gboolean uca_phantom_communicate_get_cine_index (UcaPhantomCommunicate* self, gint cine, guint prop, gint* res, GError** error_loc);
/**
 * @brief Send a request for images to the Phantom camera and push an internal request to receive images in the program.
 *
 * This function requests images from the Phantom camera and pushes them to the request queue for processing.
 * The function supports two modes of operation: one for 10Gb ethernet and one for 1Gb ethernet.
 *
 * @param self The UcaPhantomCommunicate object.
 * @param settings The CaptureSettings object. Definition can be found in the UcaPhantomCommunicate header.
 * @param error_loc A pointer to a GError object to store any errors that occur.
 * @return TRUE if the request was successful, FALSE otherwise.
 */
gboolean uca_phantom_communicate_request_images (UcaPhantomCommunicate *self, CaptureSettings settings, gboolean earlyimg, GError **error_loc);

/**
 * @brief Grab an image from the program's image queue
 * 
 * @details Copys the image data allocated in the internal image queue to the provided pointer.
 * This functions supposes that the provided pointer is large enough to hold the image data.
 * 
 * @note This function will block until an image is available in the queue
 *
 * @param self Pointer to the UcaPhantomCommunicate object
 * @param error_loc Pointer to a GError object to store any errors that occur
 * @return gboolean TRUE if the readout was successfully stopped, FALSE otherwise
 */
gboolean uca_phantom_communicate_grab_image (UcaPhantomCommunicate *self, gpointer data, GError **error_loc);

/**
 * @brief Grab a buffered image from the program's ring buffer
 * 
 * @details Copy a single image from the ring buffer to the provided pointer.
 * 
 * @note This function will block until an image is available in the ring buffer
 *
 * @param self Pointer to the UcaPhantomCommunicate object
 * @param error_loc Pointer to a GError object to store any errors that occur
 * @return gboolean TRUE if the readout was successfully stopped, FALSE otherwise
 */
gboolean uca_phantom_communicate_grab_live_image (UcaPhantomCommunicate* self, gpointer data, CaptureSettings settings, GError **error_loc);


/**
 * @brief Grab a timestamp from the program's timestamp queue
 * 
 * @details Copys the timestamp data allocated in the internal timestamp queue to the provided pointer.
 * This functions supposes that the provided pointer is large enough to hold the timestamp data.
 * 
 * @note This function will block until a timestamp is available in the queue
 *
 * @param self Pointer to the UcaPhantomCommunicate object
 * @param error_loc Pointer to a GError object to store any errors that occur
 * @return gboolean TRUE if the readout was successfully stopped, FALSE otherwise
 */
gboolean uca_phantom_communicate_grab_timestamp (UcaPhantomCommunicate* self, guint64 *time, TimestampFormat format, GError** error_loc);


/**
 * Retrieves the resolution of the UcaPhantomCommunicate instance.
 *
 * This function retrieves the width and height of the UcaPhantomCommunicate instance,
 * which represents the resolution of the communication.
 *
 * @param self The UcaPhantomCommunicate instance.
 * @param width A pointer to store the width of the resolution.
 * @param height A pointer to store the height of the resolution.
 * @param error_loc A pointer to a GError pointer to store any error that occurred during the operation.
 * 
 * @return TRUE if the resolution was successfully retrieved, FALSE otherwise.
 */
gboolean uca_phantom_communicate_get_resolution (UcaPhantomCommunicate *self, guint16 *width, guint16 *height, GError **error_loc);


/**
 * @brief The flags that represent phantom unit variables.
 *
 * The flags are used to identify the different types of variables that can be accessed or modified
 * in a Phantom unit. They are used in conjunction with the phantom set and get functions. 
 *
 * @see uca-phantom-commands.h and uca-phantom-variables.h for more information.
 */
// enum PhantomUnitIds {
//     UNIT_INFO_SENSOR,
//     UNIT_INFO_SNSVERSION,
//     UNIT_INFO_CFA,
//     UNIT_INFO_FILTER,
//     UNIT_INFO_HWVER,
//     UNIT_INFO_KERNEL,
//     UNIT_INFO_SWVER,
//     UNIT_INFO_XVER,
//     UNIT_INFO_MODEL,
//     UNIT_INFO_PVER,
//     UNIT_INFO_SVER,
//     UNIT_INFO_FVER,
//     UNIT_INFO_SERIAL,
//     UNIT_INFO_NAME,
//     UNIT_INFO_FEATURES,
//     UNIT_INFO_IMGFORMATS,
//     UNIT_INFO_VIDEOSYSTEMS,
//     UNIT_INFO_MAXCINES,
//     UNIT_INFO_XMAX,
//     UNIT_INFO_YMAX,
//     UNIT_INFO_XINC,
//     UNIT_INFO_YINC,
//     UNIT_INFO_WINX,
//     UNIT_INFO_WINY,
//     UNIT_INFO_KERNSZ,
//     UNIT_INFO_MEMSZ,
//     UNIT_INFO_CINEMEM,
//     UNIT_INFO_MDEPTHS,
//     UNIT_INFO_EXPDEAD,
//     UNIT_INFO_MINEXP,
//     UNIT_INFO_XBLOCK,
//     UNIT_INFO_YBLOCK,
//     UNIT_INFO_PIXPS,
//     UNIT_INFO_ROTPS,
//     UNIT_INFO_FOTPS,
//     UNIT_INFO_MINFRATE,
//     UNIT_INFO_MAXRATE,
//     UNIT_INFO_TMODEL,
//     UNIT_INFO_MAGTP,
//     UNIT_INFO_RTOBYTEPS,
//     UNIT_INFO_RTOPACKET,
//     UNIT_INFO_RTOPACKETOVHEAD,
//     UNIT_INFO_RTOFROVHEAD,
//     UNIT_INFO_RTOCHANNELS,
//     UNIT_INFO_BASECHROMA,
//     UNIT_INFO_BASEEI,
//     UNIT_INFO_SNSTEMP,
//     UNIT_INFO_TEPOWER,
//     UNIT_INFO_CAMTEMP,
//     UNIT_INFO_FANPOWER,
//     UNIT_INFO_BATTI,
//     UNIT_INFO_BATTV,
//     UNIT_INFO_BATTSTATE,
//     UNIT_INFO_BATTTIMER,
//     // UNIT_INFO_GENLOCKSTAT,
//     UNIT_INFO_MODES,
//     UNIT_PRESET_TONE1,
//     UNIT_PRESET_TONE2,
//     UNIT_PRESET_TONE3,
//     UNIT_PRESET_TONE4,
//     UNIT_PRESET_MATRIX1,
//     UNIT_PRESET_MATRIX2,
//     UNIT_PRESET_MATRIX3,
//     UNIT_PRESET_MATRIX4,
//     UNIT_PRESET_FILTER1,
//     UNIT_PRESET_FILTER2,
//     UNIT_PRESET_FILTER3,
//     UNIT_PRESET_FILTER4,
//     UNIT_PRESET_EI1,
//     UNIT_PRESET_EI2,
//     UNIT_PRESET_EI3,
//     UNIT_PRESET_EI4,
//     UNIT_PRESET_EI5,
//     UNIT_PRESET_EI6,
//     UNIT_PRESET_EI7,
//     UNIT_PRESET_EI8,
//     UNIT_CAM_SYNCIMG,
//     UNIT_CAM_FRDELAY,
//     UNIT_CAM_RTOEN,
//     UNIT_CAM_RTOTFR,
//     UNIT_CAM_MEMBPP,
//     UNIT_CAM_TRIGPOL,
//     UNIT_CAM_TRIGFILT,
//     UNIT_CAM_STARTONACQ,
//     UNIT_CAM_AUX1MODE,
//     UNIT_CAM_AUX2MODE,
//     UNIT_CAM_AUX3MODE,
//     UNIT_CAM_TSFORMAT,
//     UNIT_CAM_TCMODE,
//     UNIT_CAM_MASTER,
//     UNIT_CAM_APOFFDIS,
//     UNIT_CAM_LONGREADY,
//     UNIT_CAM_CINES,
//     UNIT_CAM_DARK,
//     UNIT_CAM_TSETSNS,
//     UNIT_CAM_TSETCAM,
//     // UNIT_CAM_TZ,
//     UNIT_CAM_MODE,
//     UNIT_AUTO_VIDEOPLAY,
//     UNIT_AUTO_FLASHSAVE,
//     UNIT_AUTO_FILESAVE,
//     UNIT_AUTO_ACQRESTART,
//     UNIT_AUTO_BREF,
//     UNIT_AUTO_FIRSTFRAME,
//     UNIT_AUTO_LASTFRAME,
//     UNIT_AUTO_LOOPS,
//     UNIT_AUTO_SPEED,
//     UNIT_AUTO_PROGRESS,
//     UNIT_AUTO_BREFPROGRESS,
//     UNIT_AUTO_TRIGGER_X,
//     UNIT_AUTO_TRIGGER_Y,
//     UNIT_AUTO_TRIGGER_W,
//     UNIT_AUTO_TRIGGER_H,
//     UNIT_AUTO_TRIGGER_THRESHOLD,
//     UNIT_AUTO_TRIGGER_AREA,
//     UNIT_AUTO_TRIGGER_SPEED,
//     UNIT_AUTO_TRIGGER_MODE,
//     UNIT_DEFC_RES,
//     UNIT_DEFC_RATE,
//     UNIT_DEFC_EXP,
//     UNIT_DEFC_EDREXP,
//     UNIT_DEFC_PTFRAMES,
//     UNIT_DEFC_SHOFF,
//     UNIT_DEFC_RAMP,
//     UNIT_DEFC_BCOUNT,
//     UNIT_DEFC_BPERIOD,
//     UNIT_DEFC_HQENABLE,
//     UNIT_DEFC_DECIMATION,
//     UNIT_DEFC_FRCOUNT,
//     UNIT_DEFC_FRSIZE,
//     UNIT_DEFC_AEXPMODE,
//     UNIT_DEFC_AEXPCOMP,
//     UNIT_DEFC_META_OX,
//     UNIT_DEFC_META_OY,
//     UNIT_DEFC_META_W,
//     UNIT_DEFC_META_H,
//     UNIT_DEFC_META_OW,
//     UNIT_DEFC_META_OH,
//     UNIT_DEFC_META_CROP,
//     // UNIT_DEFC_META_SCALE,
//     UNIT_IRIG_SEC,
//     UNIT_IRIG_YEARBEGIN,
//     UNIT_IRIG_FLAGS,
//     UNIT_IRIG_SIGNAL,
//     UNIT_IRIG_GPS,
//     // UNIT_IRIG_RANGE,
//     UNIT_META_NAME,
//     UNIT_META_LENS,
//     UNIT_META_FSTOP,
//     UNIT_META_FLEN,
//     UNIT_META_COMMENT,
//     UNIT_META_XSET,
//     UNIT_VIDEO_SYSTEM,
//     UNIT_VIDEO_OUTPUT,
//     UNIT_VIDEO_FIELDS,
//     UNIT_VIDEO_WIDESCREEN,
//     UNIT_VIDEO_GENLOCK,
//     UNIT_VIDEO_VFMODE,
//     UNIT_VIDEO_OSDDIS,
//     UNIT_VIDEO_OSDUT,
//     UNIT_VIDEO_PAX,
//     UNIT_VIDEO_PAY,
//     UNIT_VIDEO_PAOX,
//     UNIT_VIDEO_PAOY,
//     UNIT_VIDEO_UZOOM,
//     UNIT_VIDEO_VOX,
//     UNIT_VIDEO_VOY,
//     UNIT_VIDEO_VOW,
//     UNIT_VIDEO_VOH,
//     UNIT_VIDEO_VW,
//     UNIT_VIDEO_VH,
//     UNIT_VIDEO_ADJ_RED,
//     UNIT_VIDEO_ADJ_GREEN,
//     UNIT_VIDEO_ADJ_BLUE,
//     UNIT_VIDEO_ADJ_TOE,
//     UNIT_VIDEO_ADJ_GAMMA,
//     UNIT_VIDEO_ADJ_RGAMMA,
//     UNIT_VIDEO_ADJ_BGAMMA,
//     UNIT_VIDEO_ADJ_GAIN,
//     UNIT_VIDEO_ADJ_OFFSET,
//     UNIT_VIDEO_ADJ_FLARE,
//     UNIT_VIDEO_ADJ_HUE,
//     UNIT_VIDEO_ADJ_SAT,
//     UNIT_VIDEO_ADJ_RPED,
//     UNIT_VIDEO_ADJ_GPED,
//     UNIT_VIDEO_ADJ_BPED,
//     UNIT_VIDEO_ADJ_CHROMA,
//     UNIT_VIDEO_ADJ_TONE,
//     UNIT_VIDEO_ADJ_MATRIX,
//     UNIT_VIDEO_ADJ_LOG,
//     UNIT_VIDEO_ADJ_SDIMIN,
//     UNIT_VIDEO_ADJ_SDIMAX,
//     UNIT_VIDEO_ADJ_CMATRIX,
//     UNIT_VIDEO_ADJ_FILTER,
//     UNIT_VIDEO_ADJ_UMATRIX,
//     UNIT_VIDEO_ADJ_WBTEMP,
//     UNIT_VIDEO_ADJ_WBCC,
//     UNIT_VIDEO_ADJ_WBRED,
//     UNIT_VIDEO_ADJ_WBBlUE,
//     UNIT_VIDEO_ADJ_MMSAT,
//     UNIT_VIDEO_ADJ_MMHUE,
//     UNIT_VIDEO_ADJ_EDGEG,
//     UNIT_VIDEO_ADJ_EDGER,
//     UNIT_VIDEO_ADJ_EDGETH,
//     UNIT_VIDEO_PLAY_LIVE,
//     UNIT_VIDEO_PLAY_CINE,
//     UNIT_VIDEO_PLAY_MAG,
//     UNIT_VIDEO_PLAY_FN,
//     UNIT_VIDEO_PLAY_IN,
//     UNIT_VIDEO_PLAY_OUT,
//     UNIT_VIDEO_PLAY_SPEED,
//     UNIT_VIDEO_PLAY_STEP,
//     UNIT_VIDEO_PLAY_FRCOUNT,
//     UNIT_ETH_IP,
//     UNIT_ETH_NETMASK,
//     UNIT_ETH_BROADCAST,
//     UNIT_ETH_GATEWAY,
//     UNIT_ETH_MTU,
//     UNIT_ETH_XIP,
//     UNIT_ETH_XNETMASK,
//     UNIT_ETH_XBROADCAST,
//     UNIT_MAG_STATE,
//     UNIT_MAG_PROGRESS,
//     UNIT_MAG_PROTECT,
//     UNIT_MAG_SIZE,
//     UNIT_MAG_USED,
//     UNIT_MAG_TAKES,
//     UNIT_MAG_VERSION,
//     UNIT_MAG_ID,
//     UNIT_MAG_RUNSTOP,
//     UNIT_MAG_TYPE,
//     UNIT_CF_STATE,
//     UNIT_CF_ACTION,
//     UNIT_CF_SIZE,
//     UNIT_CF_USED,
//     UNIT_CF_PROGRESS,
//     UNIT_CF_ERRCODE,
//     UNIT_USETS_NSETS,
//     UNIT_USETS_VALID_NUMBER,
//     UNIT_C_STATE,
//     UNIT_C_FRCOUNT,
//     UNIT_C_FIRSTFR,
//     UNIT_C_LASTFR,
//     UNIT_C_FORMAT,
//     UNIT_C_IN,
//     UNIT_C_OUT,
//     UNIT_C_START,
//     UNIT_C_LEN,
//     UNIT_C_FRSIZE,
//     UNIT_C_FRSPACE,
//     UNIT_C_TRIGTIME_SECS,
//     UNIT_C_TRIGTIME_FRAC,
//     UNIT_C_META_PBRATE,
//     UNIT_C_META_TCRATE,
//     UNIT_C_META_UUID,
//     UNIT_C_META_SYSTEM,
//     UNIT_C_META_TRIGTC,
//     UNIT_C_META_PAX,
//     UNIT_C_META_PAY,
//     UNIT_C_META_PAOX,
//     UNIT_C_META_PAOY,
//     UNIT_C_META_VOX,
//     UNIT_C_META_VOY,
//     UNIT_C_META_VOW,
//     UNIT_C_META_VOH,
//     UNIT_C_META_VW,
//     UNIT_C_META_VH,
//     UNIT_C_META_OX,
//     UNIT_C_META_OY,
//     UNIT_C_META_OW,
//     UNIT_C_META_OH,
//     UNIT_C_META_W,
//     UNIT_C_META_H,
//     UNIT_C_META_CROP,
//     UNIT_C_META_RESIZE,
//     UNIT_C_META_GPS,
//     N_CINE_PROPERTIES,
//     // UNIT_HW_VPIX,
//     // UNIT_HW_VPIXL,
//     // UNIT_HW_VMEMH,
//     // UNIT_HW_VMEML,
//     // UNIT_HW_VRESH,
//     // UNIT_HW_VRESL,
//     // UNIT_HW_VRESDS,
//     // UNIT_HW_DCBLACK,
//     // UNIT_HW_BBIAS,
//     // UNIT_HW_TBIAS,
//     // UNIT_HW_TRBIAS,
//     // UNIT_HW_BRBIAS,
//     // UNIT_HW_VMLPRED,
//     // UNIT_HW_PREPW,
//     // UNIT_HW_PRESMPD,
//     // UNIT_HW_SMPPW,
//     // UNIT_HW_SMPVMHD,
//     // UNIT_HW_VMHRESD,
//     // UNIT_HW_RESPW,
//     // UNIT_HW_RESWAIT,
//     // UNIT_HW_EXPADJ,
//     // UNIT_HW_SEL2W,
//     // UNIT_HW_SHKOLMODE,
//     // UNIT_HW_RESLONG,
//     // UNIT_HW_SNSFLAGS,
//     // UNIT_HW_NSFLOAD,
//     // UNIT_HW_COLLOAD,
//     // UNIT_HW_OUTLOAD,
//     // UNIT_HW_FREQTRIM,
//     // UNIT_HW_IRIGPHASE,
//     // UNIT_HW_IRIGMODPHASE,
//     // UNIT_HW_MCODE,
//     // UNIT_HW_MEMPHASE,
//     // UNIT_HW_MEMFLAGS,
//     // UNIT_HW_ADPHASE,
//     // UNIT_HW_ADCFLAGS,
//     // UNIT_HW_DCPHASE,
//     // UNIT_HW_TOUCHDX,
//     // UNIT_HW_TOUCHDY,
//     // UNIT_HW_TOUCHSX,
//     // UNIT_HW_TOUCHSY,
//     // UNIT_HW_IGAIN,
//     // UNIT_HW_COLORCAL,
//     // UNIT_HW_SNTEMPCAL,
// };

// enum {
//     N_UNIT_PROPERTIES = UNIT_USETS_VALID_NUMBER,
// };

// enum PhantomCommandIds {
//     CMD_GET, //
//     CMD_SET, //
//     CMD_START_RECORDING_IN_A_CINE, //
//     CMD_DELETE_A_CINE, //
//     CMD_RELEASE_A_CINE, //
//     CMD_SOFTWARE_TRIGGER, //
//     CMD_GET_CINE_STATES, //
//     CMD_START_DATA_CONNECTION, //
//     CMD_ATTACH, //
//     CMD_GET_IMAGES, //
//     CMD_GET_XIMAGES, //
//     CMD_GET_TIMESTAMPS, //
//     CMD_SHOW_NETWORK_INTERFACE_CONFIGURATION, //
//     CMD_SHOW_ROUTING_TABLE, //
//     CMD_PARTITION_CINE_MEMORY, //
//     CMD_PERFORM_WHITE_BALANCE, //
//     CMD_PERFORM_BLACK_REFERENCE, //
//     CMD_PRNU_CORRECTION_UPDATE, //
//     CMD_BLACK_REFERENCE_UPDATE, //
//     CMD_FLASH_ERASE, //
//     CMD_FLASH_SAVE, //
//     CMD_STORAGE_DEVICE_SAVE, //
//     CMD_RETRIEVE_INTERNAL_CAMERA_LOG, //
//     CMD_PLAY_FRAMES_ON_VIDEO_OUTPUT, //
//     CMD_CHECK_FOR_VARIABLE_CHANGES, //
//     CMD_ENABLE_STATUS_CHANGE_NOTIFICATIONS, //
//     CMD_SAVE_FACTORY_DEFAULTS, //
//     CMD_LOAD_FACTORY_DEFAULTS, //
//     CMD_SAVE_USER_SETTINGS, //
//     CMD_LOAD_USER_SETTINGS, //
//     CMD_ERASE_USER_SETTINGS, //
//     CMD_LIST_USER_SETTINGS, //
//     CMD_DEBUG_CONSOLE_MODE,//
//     CMD_SET_LENS_APERTURE,//
//     CMD_MOVE_FOCUS,//
//     CMD_ISSUE_LENS_MOUNT_COMMAND,//
//     CMD_CHANGE_SERIAL_LINE_BAUDE_RATE,//
//     CMD_CAMERA_CALIBRATION,//
//     CMD_SYSTEM_MONITOR,//
//     CMD_GENERATE_TEST_IMAGE,//
//     CMD_SET_REAL_TIME_CLOCK,//
//     CMD_LIST_FILES_ON_STORAGE_DEVICE,//
//     CMD_REMOVE_FILE_FROM_STORAGE_DEVICE,//
//     CMD_FORMATE_STORAGE_DEVICE,//
//     CMD_FILE_READ_DATA_FROM_STORAGE_DEVICE,//
//     CMD_USE_COLOR_PRESET,//
//     CMD_SET_MULTI_MATRIX_AXIS,//
//     N_UNIT_COMMANDS
// };
G_END_DECLS
#endif