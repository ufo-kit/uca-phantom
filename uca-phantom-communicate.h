#ifndef UCA_PHANTOM_COMMUNICATE_H
#define UCA_PHANTOM_COMMUNICATE_H

#include <glib-object.h>
#include <uca/uca-camera.h>

G_BEGIN_DECLS

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
    UCA_PHANTOM_COMMUNICATE_ERROR_ACCEPT_XIMG,
    UCA_PHANTOM_COMMUNICATE_ERROR_ACCEPT_IMG,
    UCA_PHANTOM_COMMUNICATE_ERROR_DISCONNECT_DATASTREAM,
    UCA_PHANTOM_COMMUNICATE_ERROR_STOP_READOUT,
    UCA_PHANTOM_COMMUNICATE_ERROR_NEXT_EVENT,
    UCA_PHANTOM_COMMUNICATE_ERROR_NO_DATA,
    UCA_PHANTOM_COMMUNICATE_ERROR_INVALID_ARGUMENT,
    UCA_PHANTOM_COMMUNICATE_ERROR_MAYBE_CORRUPTED
} UcaPhantomCommunicateError;

typedef enum {
    SYNC_MODE_FREE_RUN = 0,
    SYNC_MODE_FSYNC,
    SYNC_MODE_IRIG,
    SYNC_MODE_VIDEO_FRAME_RATE,
} SyncMode;

typedef enum {
    ACQUISITION_MODE_STANDARD = 0,
    ACQUISITION_MODE_STANDARD_BINNED = 2,
    ACQUISITION_MODE_HS = 5,
    ACQUISITION_MODE_HS_BINNED = 7,
    ACQUISITION_MODE_BRIGHT_FIELD
} AcquisitionMode;

typedef enum {
    AUTO_EXP_MODE_OFF = 0,
    AUTO_EXP_MODE_AVERAGE,
    AUTO_EXP_MODE_SPOT,
    AUTO_EXP_MODE_CENTER
} AutoExpMode;

typedef enum {
    IMG_8,
    IMG_8R,
    IMG_P16,
    IMG_P16R,
    IMG_P10,
    IMG_P12L
} ImageFormat;

typedef enum {
    TS_SHORT,
    TS_SHORT32,
    TS_LONG,
    TS_LONG32,
    TS_NONE // No timestamp is requested
} TimestampFormat;

typedef struct {
    gchar *format_string;
    guint bit_depth;
    gfloat byte_depth;
} ImageFormatSpec;

typedef struct {
    gchar *format_string;
    guint bit_depth;
    gfloat byte_depth;
} TimestampSpec;

typedef struct{
    // base properties
    guint16 sensor_pixel_width, sensor_pixel_height, sensor_bit_depth;
    UcaCameraTriggerSource trigger_source;
    UcaCameraTriggerType trigger_type;
    gfloat frames_per_second;
    gdouble exposure_time;
    gint roi_pixel_x, roi_pixel_y, roi_pixel_width, roi_pixel_height;
    guint roi_width_multiplier, roi_height_multiplier;

    // phantom specific properties
    gfloat focal_length, aperture;
    guint edr_exp; // EDR exposure time
    guint shutter_off, aexpmode;
    gfloat aexpcomp; // Auto exposure compensation
    guint nb_post_trigger_frames, nb_pre_trigger_frames;
    guint current_cine; // Current cine number in which the camera is recording

    SyncMode sync_mode;
    AcquisitionMode acquisition_mode;
    ImageFormat image_format;
    TimestampFormat timestamp_format;
} CaptureSettings;

extern const ImageFormatSpec ImageFormatSpecs[];
extern const TimestampSpec TimestampSpecs[];

/*
 * Public methods
*/
UcaPhantomCommunicate *uca_phantom_communicate_new (void);
gboolean uca_phantom_communicate_connect_controlstream (UcaPhantomCommunicate *self, GError **error_loc);
gboolean uca_phantom_communicate_connect_datastream(UcaPhantomCommunicate *self, GError **error_loc);
gboolean uca_phantom_communicate_connect_xdatastream (UcaPhantomCommunicate *self, GError **error_loc);
gboolean uca_phantom_communicate_run_command (UcaPhantomCommunicate *self, guint command_flag, PhantomReply *reply, GError **error_loc, ...);
gboolean uca_phantom_communicate_get_variable (UcaPhantomCommunicate *self, guint variable_flag, GValue *return_value, GError **error);
gboolean uca_phantom_communicate_set_variable (UcaPhantomCommunicate *self, guint variable_flag, const char *set_value, GError **error_loc);
void uca_phantom_communicate_print_capture_settings (UcaPhantomCommunicate *self);
gboolean uca_phantom_communicate_get_settings (UcaPhantomCommunicate *self, CaptureSettings *settings, GError **error_loc);
gboolean uca_phantom_communicate_set_settings (UcaPhantomCommunicate *self, CaptureSettings *settings, GError **error_loc);
gboolean uca_phantom_communicate_start_readout(UcaPhantomCommunicate *self, GError **error_loc);
gboolean uca_phantom_communicate_stop_readout(UcaPhantomCommunicate *self, GError **error_loc);
gboolean uca_phantom_communicate_arm (UcaPhantomCommunicate *self, guint cine, GError **error_loc);
gboolean uca_phantom_communicate_trigger (UcaPhantomCommunicate *self, GError **error_loc);
gboolean uca_phantom_communicate_trigger_ptframes (UcaPhantomCommunicate *self, guint ptframes, GError **error_loc);
gboolean uca_phantom_communicate_request_images (UcaPhantomCommunicate *self, CaptureSettings *settings, GError **error_loc);
gboolean uca_phantom_communicate_grab_image (UcaPhantomCommunicate *self, gpointer data, GError **error_loc);


gboolean uca_phantom_communicate_get_mac_address (UcaPhantomCommunicate *self, GError **error_loc);

typedef enum _IP_SOURCE_FLAGS {
    USE_ENV,
    USE_CLASS,
    USE_BCAST,
    N_IP_FLAGS
} IP_SOURCE_FLAGS;

enum PhantomUnitIds {
    UNIT_INFO_SENSOR,
    UNIT_INFO_SNSVERSION,
    UNIT_INFO_CFA,
    UNIT_INFO_FILTER,
    UNIT_INFO_HWVER,
    UNIT_INFO_KERNEL,
    UNIT_INFO_SWVER,
    UNIT_INFO_XVER,
    UNIT_INFO_MODEL,
    UNIT_INFO_PVER,
    UNIT_INFO_SVER,
    UNIT_INFO_SERIAL,
    UNIT_INFO_NAME,
    UNIT_INFO_FEATURES,
    UNIT_INFO_IMGFORMATS,
    UNIT_INFO_VIDEOSYSTEMS,
    UNIT_INFO_MAXCINES,
    UNIT_INFO_XMAX,
    UNIT_INFO_YMAX,
    UNIT_INFO_XINC,
    UNIT_INFO_YINC,
    UNIT_INFO_WINX,
    UNIT_INFO_WINY,
    UNIT_INFO_KERNSZ,
    UNIT_INFO_MEMSZ,
    UNIT_INFO_CINEMEM,
    UNIT_INFO_MDEPTHS,
    UNIT_INFO_EXPDEAD,
    UNIT_INFO_MINEXP,
    UNIT_INFO_XBLOCK,
    UNIT_INFO_YBLOCK,
    UNIT_INFO_PIXPS,
    UNIT_INFO_ROTPS,
    UNIT_INFO_FOTPS,
    UNIT_INFO_MINFRATE,
    UNIT_INFO_MAXRATE,
    UNIT_INFO_TMODEL,
    UNIT_INFO_MAGTP,
    UNIT_INFO_RTOBYTEPS,
    UNIT_INFO_RTOPACKET,
    UNIT_INFO_RTOPACKETOVHEAD,
    UNIT_INFO_RTOFROVHEAD,
    UNIT_INFO_RTO_CHANNELS,
    UNIT_INFO_MODES,
    UNIT_META_NAME,
    UNIT_META_LENS,
    UNIT_META_FSTOP,
    UNIT_META_FLEN,
    UNIT_META_COMMENT,
    UNIT_META_XSET,
    UNIT_CAM_SYNCIMG,
    UNIT_CAM_FRDELAY,
    UNIT_CAM_RTOEN,
    UNIT_CAM_RTOTFR,
    UNIT_CAM_MEMBPP,
    UNIT_CAM_TRIGPOL,
    UNIT_CAM_TRIGFILT,
    UNIT_CAM_STARTONACQ,
    UNIT_CAM_TSFORMAT,
    UNIT_CAM_TCMODE,
    UNIT_CAM_MASTER,
    UNIT_CAM_APOFFDIS,
    UNIT_CAM_LONGREADY,
    UNIT_CAM_CINES,
    UNIT_CAM_DARK,
    UNIT_CAM_TSETSNS,
    UNIT_CAM_TSETCAM,
    // UNIT_CAM_TZ,
    UNIT_CAM_MODE,
    UNIT_ETH_IP,
    UNIT_ETH_NETMASK,
    UNIT_ETH_BROADCAST,
    UNIT_ETH_GATEWAY,
    UNIT_ETH_MTU,
    UNIT_ETH_XIP,
    UNIT_ETH_XNETMASK,
    UNIT_ETH_XBROADCAST,
    UNIT_VIDEO_SYSTEM,
    UNIT_VIDEO_OUTPUT,
    UNIT_VIDEO_FIELDS,
    UNIT_VIDEO_WIDESCREEN,
    UNIT_VIDEO_GENLOCK,
    UNIT_VIDEO_VFMODE,
    UNIT_VIDEO_UZOOM,
    UNIT_VIDEO_VOX,
    UNIT_VIDEO_VOY,
    UNIT_VIDEO_VOW,
    UNIT_VIDEO_VOH,
    UNIT_VIDEO_VW,
    UNIT_VIDEO_VH,
    //
    UNIT_VIDEO_ADJ_RED,
    UNIT_VIDEO_ADJ_GREEN,
    UNIT_VIDEO_ADJ_BLUE,
    UNIT_VIDEO_ADJ_TOE,
    UNIT_VIDEO_ADJ_GAMMA,
    UNIT_VIDEO_ADJ_RGAMMA,
    UNIT_VIDEO_ADJ_BGAMMA,
    UNIT_VIDEO_ADJ_GAIN,
    UNIT_VIDEO_ADJ_OFFSET,
    UNIT_VIDEO_ADJ_FLARE,
    UNIT_VIDEO_ADJ_HUE,
    UNIT_VIDEO_ADJ_SAT,
    UNIT_VIDEO_ADJ_RPED,
    UNIT_VIDEO_ADJ_GPED,
    UNIT_VIDEO_ADJ_BPED,
    UNIT_VIDEO_ADJ_CHROMA,
    UNIT_VIDEO_ADJ_TONE,
    UNIT_VIDEO_ADJ_MATRIX,
    UNIT_VIDEO_ADJ_LOG,
    //
    UNIT_IRIG_SEC,
    UNIT_IRIG_YEARBEGIN,
    UNIT_IRIG_FLAGS,
    UNIT_IRIG_SIGNAL,
    UNIT_IRIG_GPS,
    // UNIT_IRIG_RANGE,
    //
    UNIT_MAG_STATE,
    UNIT_MAG_PROGRESS,
    UNIT_MAG_PROTECT,
    UNIT_MAG_SIZE,
    UNIT_MAG_USED,
    UNIT_MAG_TAKES,
    UNIT_MAG_VERSION,
    UNIT_MAG_ID,
    UNIT_MAG_RUNSTOP,
    UNIT_MAG_TYPE,
    //
    UNIT_DEFC_RES,
    UNIT_DEFC_RATE,
    UNIT_DEFC_EXP,
    UNIT_DEFC_EDREXP,
    UNIT_DEFC_PTFRAMES,
    UNIT_DEFC_SHOFF,
    UNIT_DEFC_RAMP,
    UNIT_DEFC_BCOUNT,
    UNIT_DEFC_BPERIOD,
    UNIT_DEFC_HQENABLE,
    UNIT_DEFC_DECIMATION,
    UNIT_DEFC_FRCOUNT,
    UNIT_DEFC_FRSIZE,
    UNIT_DEFC_AEXPMODE,
    UNIT_DEFC_AEXPCOMP,
    UNIT_DEFC_META_OX,
    UNIT_DEFC_META_OY,
    UNIT_DEFC_META_W,
    UNIT_DEFC_META_H,
    UNIT_DEFC_META_OW,
    UNIT_DEFC_META_OH,
    UNIT_DEFC_META_CROP,
    //
    UNIT_CF_STATE,
    UNIT_CF_ACTION,
    UNIT_CF_SIZE,
    UNIT_CF_USED,
    UNIT_CF_PROGRESS,
    UNIT_CF_ERRRCODE,
    //    
    UNIT_CT_STATE,
    UNIT_CT_FRCOUNT,
    UNIT_CT_FIRSTFR,
    UNIT_CT_LASTFR,
    UNIT_CT_FORMAT,
    UNIT_CT_IN,
    UNIT_CT_OUT,
    //
    UNIT_CT_START,
    UNIT_CT_LEN,
    UNIT_CT_FRSIZE,
    UNIT_CT_FRSPACE,
    
    UNIT_CT_TRIGTIME_SECS,
    UNIT_CT_TRIGTIME_FRAC,
    UNIT_CT_CAM,
    UNIT_CT_INFO,
    UNIT_CT_ADJ,
    UNIT_CT_META_PBRATE,
    UNIT_CT_META_TCRATE,
    UNIT_CT_META_UUID,
    UNIT_CT_META_SYSTEM,
    UNIT_CT_META_TRIGTC,
    UNIT_CT_META_PAX,
    UNIT_CT_META_PAY,
    UNIT_CT_META_PAOX,
    UNIT_CT_META_PAOY,
    UNIT_CT_META_OX,
    UNIT_CT_META_OY,
    UNIT_CT_META_OW,
    UNIT_CT_META_OH,
    UNIT_CT_META_W,
    UNIT_CT_META_H,
    UNIT_CT_META_CROP,
    UNIT_CT_META_RESIZE,
    UNIT_CT_META_GPS,
    //
    UNIT_AUTO_VIDEOPLAY,
    UNIT_AUTO_FLASHSAVE,
    UNIT_AUTO_FILESAVE,
    UNIT_AUTO_ACQRESTART,
    UNIT_AUTO_BREF,
    UNIT_AUTO_FIRSTFRAME,
    UNIT_AUTO_LASTFRAME,
    UNIT_AUTO_LOOPS,
    UNIT_AUTO_SPEED,
    UNIT_AUTO_PROGRESS,
    UNIT_AUTO_BREF_PROGRESS,
    //
    UNIT_AUTO_TRIGGER_X,
    UNIT_AUTO_TRIGGER_Y,
    UNIT_AUTO_TRIGGER_W,
    UNIT_AUTO_TRIGGER_H,
    UNIT_AUTO_TRIGGER_THRESHOLD,
    UNIT_AUTO_TRIGGER_AREA,
    UNIT_AUTO_TRIGGER_SPEED,
    UNIT_AUTO_TRIGGER_MODE,
    N_UNIT_PROPERTIES
};

enum PhantomCommandIds {
    CMD_GET, //
    CMD_SET, //
    CMD_START_RECORDING_IN_A_CINE, //
    CMD_DELETE_A_CINE, //
    CMD_RELEASE_A_CINE, //
    CMD_SOFTWARE_TRIGGER, //
    CMD_GET_CINE_STATES, //
    CMD_START_DATA_CONNECTION, //
    CMD_ATTACH, //
    CMD_GET_IMAGES, //
    CMD_GET_XIMAGES, //
    CMD_GET_TIMESTAMPS, //
    CMD_SHOW_NETWORK_INTERFACE_CONFIGURATION, //
    CMD_SHOW_ROUTING_TABLE, //
    CMD_PARTITION_CINE_MEMORY, //
    CMD_PERFORM_WHITE_BALANCE, //
    CMD_PERFORM_BLACK_REFERENCE, //
    CMD_PRNU_CORRECTION_UPDATE, //
    CMD_BLACK_REFERENCE_UPDATE, //
    CMD_FLASH_ERASE, //
    CMD_FLASH_SAVE, //
    CMD_STORAGE_DEVICE_SAVE, //
    CMD_RETRIEVE_INTERNAL_CAMERA_LOG, //
    CMD_PLAY_FRAMES_ON_VIDEO_OUTPUT, //
    CMD_CHECK_FOR_VARIABLE_CHANGES, //
    CMD_ENABLE_STATUS_CHANGE_NOTIFICATIONS, //
    CMD_SAVE_FACTORY_DEFAULTS, //
    CMD_LOAD_FACTORY_DEFAULTS, //
    CMD_SAVE_USER_SETTINGS, //
    CMD_LOAD_USER_SETTINGS, //
    CMD_ERASE_USER_SETTINGS, //
    CMD_LIST_USER_SETTINGS, //
    CMD_DEBUG_CONSOLE_MODE,//
    CMD_SET_LENS_APERTURE,//
    CMD_MOVE_FOCUS,//
    CMD_ISSUE_LENS_MOUNT_COMMAND,//
    CMD_CHANGE_SERIAL_LINE_BAUDE_RATE,//
    CMD_CAMERA_CALIBRATION,//
    CMD_SYSTEM_MONITOR,//
    CMD_GENERATE_TEST_IMAGE,//
    CMD_SET_REAL_TIME_CLOCK,//
    CMD_LIST_FILES_ON_STORAGE_DEVICE,//
    CMD_REMOVE_FILE_FROM_STORAGE_DEVICE,//
    CMD_FORMATE_STORAGE_DEVICE,//
    CMD_FILE_READ_DATA_FROM_STORAGE_DEVICE,//
    CMD_USE_COLOR_PRESET,//
    CMD_SET_MULTI_MATRIX_AXIS,//
    N_UNIT_COMMANDS
};
G_END_DECLS
#endif