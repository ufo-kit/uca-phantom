#ifndef UCA_PHANTOM_COMMUNICATE_H
#define UCA_PHANTOM_COMMUNICATE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define UCA_TYPE_PHANTOM_COMMUNICATE (uca_phantom_communicate_get_type ())
G_DECLARE_FINAL_TYPE (UcaPhantomCommunicate, uca_phantom_communicate, UCA, PHANTOM_COMMUNICATE, GObject)


typedef struct _PhantomRequest PhantomRequest;
typedef struct _PhantomReply PhantomReply;

typedef struct _CaptureSettings CaptureSettings;
struct _CaptureSettings {
    guint16 width, height;
    gfloat fps;
    guint exposure;
    gfloat focal_length;
    gfloat aperture;
    guint post_trigger;
    guint8 aquisition_mode;
    guint8 trigger_mode;
};

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
    UCA_PHANTOM_COMMUNICATE_ERROR_GET_MAC_ADDRESS,
    // Phantom communication error codes
    UCA_PHANTOM_COMMUNICATE_ERROR_GET_VARIABLE,
    UCA_PHANTOM_COMMUNICATE_ERROR_SET_VARIABLE,
    UCA_PHANTOM_COMMUNICATE_ERROR_RUN_COMMAND,
    UCA_PHANTOM_COMMUNICATE_ERROR_GET_CAPTURE_SETTINGS,
    UCA_PHANTOM_COMMUNICATE_ERROR_SET_CAPTURE_SETTINGS,
    UCA_PHANTOM_COMMUNICATE_ERROR_GET_RESOLUTION,
    UCA_PHANTOM_COMMUNICATE_ERROR_START_RECORDING,
    UCA_PHANTOM_COMMUNICATE_ERROR_STOP_RECORDING,
    UCA_PHANTOM_COMMUNICATE_ERROR_TRIGGER,
    UCA_PHANTOM_COMMUNICATE_ERROR_UNPACK_IMAGE,
    UCA_PHANTOM_COMMUNICATE_ERROR_GRAB_IMAGE,
    UCA_PHANTOM_COMMUNICATE_ERROR_DISCONNECT_DATASTREAM,
    UCA_PHANTOM_COMMUNICATE_ERROR_NEXT_EVENT,
    UCA_PHANTOM_COMMUNICATE_ERROR_NO_DATA,
    UCA_PHANTOM_COMMUNICATE_ERROR_INVALID_ARGUMENT,
    UCA_PHANTOM_COMMUNICATE_ERROR_MAYBE_CORRUPTED
} UcaPhantomCommunicateError;

/*
 * Public methods
*/
UcaPhantomCommunicate *uca_phantom_communicate_new (void);
gboolean uca_phantom_communicate_attempt_connect (UcaPhantomCommunicate *self, GError **error_loc);
gboolean uca_phantom_communicate_run_command (UcaPhantomCommunicate *self, guint command_flag, PhantomReply *reply, GError **error_loc, ...);
gboolean uca_phantom_communicate_get_variable (UcaPhantomCommunicate *self, guint variable_flag, GValue *return_value, GError **error);
gboolean uca_phantom_communicate_set_variable (UcaPhantomCommunicate *self, guint variable_flag, const char *set_value, GError **error_loc);
void uca_phantom_communicate_print_capture_settings (UcaPhantomCommunicate *self);
gboolean uca_phantom_communicate_get_capture_settings (UcaPhantomCommunicate *self, CaptureSettings *settings, GError **error_loc);
gboolean uca_phantom_communicate_set_capture_settings (UcaPhantomCommunicate *self, CaptureSettings *settings, GError **error_loc);
gboolean uca_phantom_communicate_start_readout(UcaPhantomCommunicate *self, GError **error_loc);
gboolean uca_phantom_communicate_stop_readout(UcaPhantomCommunicate *self, GError **error_loc);
gboolean uca_phantom_communicate_arm(UcaPhantomCommunicate *self, gchar *cine, GError **error_loc);
gboolean uca_phantom_communicate_trigger (UcaPhantomCommunicate *self, GError **error_loc);
gboolean uca_phantom_communicate_request_images (UcaPhantomCommunicate *self, gint cine, guint nb_images, guint img_format, guint ts_format, GError **error_loc);
gboolean uca_phantom_communicate_request_ximages (UcaPhantomCommunicate *self, guint cine, guint nb_images, guint img_format, guint ts_format, GError **error_loc);
gboolean uca_phantom_communicate_grab_image (UcaPhantomCommunicate *self, gpointer data, GError **error_loc);


gboolean uca_phantom_communicate_get_mac_address (UcaPhantomCommunicate *self, GError **error_loc);

typedef enum _IP_SOURCE_FLAGS {
    USE_ENV,
    USE_CLASS,
    USE_BCAST,
    N_IP_FLAGS
} IP_SOURCE_FLAGS;

typedef enum _ImageFormat {
    IMG_8,
    IMG_8R,
    IMG_P16,
    IMG_P16R,
    IMG_P10,
    IMG_P12L
} ImageFormat;

typedef enum _TimestampFormat {
    TS_SHORT,
    TS_SHORT32,
    TS_LONG,
    TS_LONG32,
    TS_NONE // No timestamp is requested
} TimestampFormat;

enum PhantomUnitIds {
    PROP_INFO_SENSOR,
    PROP_INFO_SNSVERSION,
    PROP_INFO_CFA,
    PROP_INFO_FILTER,
    PROP_INFO_HWVER,
    PROP_INFO_KERNEL,
    PROP_INFO_SWVER,
    PROP_INFO_XVER,
    PROP_INFO_MODEL,
    PROP_INFO_PVER,
    PROP_INFO_SVER,
    PROP_INFO_SERIAL,
    PROP_INFO_NAME,
    PROP_INFO_FEATURES,
    PROP_INFO_IMGFORMATS,
    PROP_INFO_VIDEOSYSTEMS,
    PROP_INFO_MAXCINES,
    PROP_INFO_XMAX,
    PROP_INFO_YMAX,
    PROP_INFO_XINC,
    PROP_INFO_YINC,
    PROP_INFO_WINX,
    PROP_INFO_WINY,
    PROP_INFO_KERNSZ,
    PROP_INFO_MEMSZ,
    PROP_INFO_CINEMEM,
    PROP_INFO_MDEPTHS,
    PROP_INFO_EXPDEAD,
    PROP_INFO_MINEXP,
    PROP_INFO_XBLOCK,
    PROP_INFO_YBLOCK,
    PROP_INFO_PIXPS,
    PROP_INFO_ROTPS,
    PROP_INFO_FOTPS,
    PROP_INFO_MINFRATE,
    PROP_INFO_MAXRATE,
    PROP_INFO_TMODEL,
    PROP_INFO_MAGTP,
    PROP_INFO_RTOBYTEPS,
    PROP_INFO_RTOPACKET,
    PROP_INFO_RTOPACKETOVHEAD,
    PROP_INFO_RTOFROVHEAD,
    PROP_INFO_RTO_CHANNELS,
    PROP_INFO_MODES,
    PROP_META_NAME,
    PROP_META_LENS,
    PROP_META_FSTOP,
    PROP_META_FLEN,
    PROP_META_COMMENT,
    PROP_META_XSET,
    PROP_CAM_SYNCIMG,
    PROP_CAM_FRDELAY,
    PROP_CAM_RTOEN,
    PROP_CAM_RTOTFR,
    PROP_CAM_MEMBPP,
    PROP_CAM_TRIGPOL,
    PROP_CAM_TRIGFILT,
    PROP_CAM_STARTONACQ,
    PROP_CAM_TSFORMAT,
    PROP_CAM_TCMODE,
    PROP_CAM_MASTER,
    PROP_CAM_APOFFDIS,
    PROP_CAM_LONGREADY,
    PROP_CAM_CINES,
    PROP_CAM_DARK,
    PROP_CAM_TSETSNS,
    PROP_CAM_TSETCAM,
    // PROP_CAM_TZ,
    PROP_CAM_MODE,
    PROP_ETH_IP,
    PROP_ETH_NETMASK,
    PROP_ETH_BROADCAST,
    PROP_ETH_GATEWAY,
    PROP_ETH_MTU,
    PROP_ETH_XIP,
    PROP_ETH_XNETMASK,
    PROP_ETH_XBROADCAST,
    PROP_VIDEO_SYSTEM,
    PROP_VIDEO_OUTPUT,
    PROP_VIDEO_FIELDS,
    PROP_VIDEO_WIDESCREEN,
    PROP_VIDEO_GENLOCK,
    PROP_VIDEO_VFMODE,
    PROP_VIDEO_UZOOM,
    PROP_VIDEO_VOX,
    PROP_VIDEO_VOY,
    PROP_VIDEO_VOW,
    PROP_VIDEO_VOH,
    PROP_VIDEO_VW,
    PROP_VIDEO_VH,
    //
    PROP_VIDEO_ADJ_RED,
    PROP_VIDEO_ADJ_GREEN,
    PROP_VIDEO_ADJ_BLUE,
    PROP_VIDEO_ADJ_TOE,
    PROP_VIDEO_ADJ_GAMMA,
    PROP_VIDEO_ADJ_RGAMMA,
    PROP_VIDEO_ADJ_BGAMMA,
    PROP_VIDEO_ADJ_GAIN,
    PROP_VIDEO_ADJ_OFFSET,
    PROP_VIDEO_ADJ_FLARE,
    PROP_VIDEO_ADJ_HUE,
    PROP_VIDEO_ADJ_SAT,
    PROP_VIDEO_ADJ_RPED,
    PROP_VIDEO_ADJ_GPED,
    PROP_VIDEO_ADJ_BPED,
    PROP_VIDEO_ADJ_CHROMA,
    PROP_VIDEO_ADJ_TONE,
    PROP_VIDEO_ADJ_MATRIX,
    PROP_VIDEO_ADJ_LOG,
    //
    PROP_IRIG_SEC,
    PROP_IRIG_YEARBEGIN,
    PROP_IRIG_FLAGS,
    PROP_IRIG_SIGNAL,
    PROP_IRIG_GPS,
    // PROP_IRIG_RANGE,
    //
    PROP_MAG_STATE,
    PROP_MAG_PROGRESS,
    PROP_MAG_PROTECT,
    PROP_MAG_SIZE,
    PROP_MAG_USED,
    PROP_MAG_TAKES,
    PROP_MAG_VERSION,
    PROP_MAG_ID,
    PROP_MAG_RUNSTOP,
    PROP_MAG_TYPE,
    //
    PROP_DEFC_RES,
    PROP_DEFC_RATE,
    PROP_DEFC_EXP,
    PROP_DEFC_EDREXP,
    PROP_DEFC_PTFRAMES,
    PROP_DEFC_SHOFF,
    PROP_DEFC_RAMP,
    PROP_DEFC_BCOUNT,
    PROP_DEFC_BPERIOD,
    PROP_DEFC_HQENABLE,
    PROP_DEFC_DECIMATION,
    PROP_DEFC_FRCOUNT,
    PROP_DEFC_FRSIZE,
    PROP_DEFC_AEXPMODE,
    PROP_DEFC_AEXPCOMP,
    PROP_DEFC_META_OX,
    PROP_DEFC_META_OY,
    PROP_DEFC_META_W,
    PROP_DEFC_META_H,
    PROP_DEFC_META_OW,
    PROP_DEFC_META_OH,
    PROP_DEFC_META_CROP,
    //
    PROP_CF_STATE,
    PROP_CF_ACTION,
    PROP_CF_SIZE,
    PROP_CF_USED,
    PROP_CF_PROGRESS,
    PROP_CF_ERRRCODE,
    //    
    // PROP_CT_STATE,
    // PROP_CT_FRCOUNT,
    // PROP_CT_FIRSTFR,
    // PROP_CT_LASTFR,
    // PROP_CT_FORMAT,
    // PROP_CT_IN,
    // PROP_CT_OUT,
    // PROP_CT_START,
    // PROP_CT_LEN,
    // PROP_CT_FRSIZE,
    // PROP_CT_FRSPACE,
    // PROP_CT_TRIGTIME_SECS,
    // PROP_CT_TRIGTIME_FRAC,
    // PROP_CT_CAM,
    // PROP_CT_INFO,
    // PROP_CT_ADJ,
    // PROP_CT_META_PBRATE,
    // PROP_CT_META_TCRATE,
    // PROP_CT_META_UUID,
    // PROP_CT_META_SYSTEM,
    // PROP_CT_META_TRIGTC,
    // PROP_CT_META_PAX,
    // PROP_CT_META_PAY,
    // PROP_CT_META_PAOX,
    // PROP_CT_META_PAOY,
    // PROP_CT_META_OX,
    // PROP_CT_META_OY,
    // PROP_CT_META_OW,
    // PROP_CT_META_OH,
    // PROP_CT_META_W,
    // PROP_CT_META_H,
    // PROP_CT_META_CROP,
    // PROP_CT_META_RESIZE,
    // PROP_CT_META_GPS,
    //
    PROP_AUTO_VIDEOPLAY,
    PROP_AUTO_FLASHSAVE,
    PROP_AUTO_FILESAVE,
    PROP_AUTO_ACQRESTART,
    PROP_AUTO_BREF,
    PROP_AUTO_FIRSTFRAME,
    PROP_AUTO_LASTFRAME,
    PROP_AUTO_LOOPS,
    PROP_AUTO_SPEED,
    PROP_AUTO_PROGRESS,
    PROP_AUTO_BREF_PROGRESS,
    //
    PROP_AUTO_TRIGGER_X,
    PROP_AUTO_TRIGGER_Y,
    PROP_AUTO_TRIGGER_W,
    PROP_AUTO_TRIGGER_H,
    PROP_AUTO_TRIGGER_THRESHOLD,
    PROP_AUTO_TRIGGER_AREA,
    PROP_AUTO_TRIGGER_SPEED,
    PROP_AUTO_TRIGGER_MODE,
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