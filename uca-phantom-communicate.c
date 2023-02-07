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

// /*
//  * Class definition
// */

// #define 


/*
 * Phantom-specific data types
 * TODO: For the time being, they will remain as strings...
*/
#define PHANTOM_TYPE_HEX G_TYPE_STRING
#define PHANTOM_TYPE_RES G_TYPE_STRING

typedef struct {
    const gchar *name;
    GType        type;
    GParamFlags  flags;
    gint         property_id;
    gboolean     handle_automatically;
} Unit;

typedef struct {
    gint x, y;
    guint w, h, threshold, area, speed, mode;
} Trigger;

static Unit variables[] = {
    // Sensor information
    {"info.sensor",     G_TYPE_UINT,    G_PARAM_READABLE,  PROP_INFO_SENSOR,   TRUE},
    {"info.snsversion", G_TYPE_UINT,    G_PARAM_READABLE,  PROP_INFO_SNSVERSION,   TRUE},
    {"info.cfa",        G_TYPE_UINT,    G_PARAM_READABLE,  PROP_INFO_CFA,  TRUE},
    {"info.filter",     G_TYPE_UINT,    G_PARAM_READABLE,  PROP_INFO_FILTER,   TRUE},
    {"info.hwver",      G_TYPE_UINT,    G_PARAM_READABLE,  PROP_INFO_HWVER,    TRUE},
    {"info.kernel",     G_TYPE_UINT,    G_PARAM_READABLE,  PROP_INFO_KERNEL,   TRUE},
    {"info.swver",      G_TYPE_UINT,    G_PARAM_READABLE,  PROP_INFO_SWVER,    TRUE},
    {"info.xver",       G_TYPE_UINT,    G_PARAM_READABLE,  PROP_INFO_XVER, TRUE},
    {"info.model",      G_TYPE_STRING,  G_PARAM_READABLE,    PROP_INFO_MODEL,    TRUE},
    {"info.pver",       G_TYPE_UINT,    G_PARAM_READABLE,  PROP_INFO_PVER, TRUE},
    {"info.sver",       G_TYPE_UINT,    G_PARAM_READABLE,  PROP_INFO_SVER, TRUE},
    {"info.serial",     G_TYPE_UINT,    G_PARAM_READABLE,  PROP_INFO_SERIAL,   TRUE},
    {"info.name",       G_TYPE_STRING,  G_PARAM_READWRITE,   PROP_INFO_NAME, TRUE},
    // Capabilities
    {"info.features",        G_TYPE_STRING,     G_PARAM_READABLE,   PROP_INFO_FEATURES, TRUE},
    {"info.imgformats",      G_TYPE_STRING,     G_PARAM_READABLE,   PROP_INFO_IMGFORMATS,   TRUE},
    {"info.videosystems",    G_TYPE_STRING,     G_PARAM_READABLE,   PROP_INFO_VIDEOSYSTEMS, TRUE},
    {"info.maxcines",        G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_MAXCINES, TRUE},
    {"info.xmax",            G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_XMAX, TRUE},
    {"info.ymax",            G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_YMAX, TRUE},
    {"info.xinc",            G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_XINC, TRUE},
    {"info.yinc",            G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_YINC, TRUE},
    {"info.winx",            G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_WINX, TRUE},
    {"info.winy",            G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_WINY, TRUE},
    {"info.kernsz",          G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_KERNSZ,   TRUE},
    {"info.memsz",           G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_MEMSZ,    TRUE},
    {"info.cinemem",         G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_CINEMEM,  TRUE},
    {"info.mdepths",         PHANTOM_TYPE_HEX,  G_PARAM_READABLE,    PROP_INFO_MDEPTHS,  TRUE},
    {"info.expdead",         G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_EXPDEAD,  TRUE},
    {"info.minexp",          G_TYPE_UINT,   G_PARAM_READWRITE,    PROP_INFO_MINEXP,   TRUE},
    {"info.xblock",          G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_XBLOCK,   TRUE},
    {"info.yblock",          G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_YBLOCK,   TRUE},
    {"info.pixps",           G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_PIXPS,    TRUE},
    {"info.rotps",           G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_ROTPS,    TRUE},
    {"info.fotps",           G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_FOTPS,    TRUE},
    {"info.minfrate",        G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_MINFRATE, TRUE},
    {"info.maxrate",         G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_MAXRATE,  TRUE},
    {"info.tmodel",          G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_TMODEL,   TRUE},
    {"info.magtp",           G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_MAGTP,    TRUE},
    {"info.rtobyteps",       G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_RTOBYTEPS,    TRUE},
    {"info.rtopacket",       G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_RTOPACKET,    TRUE},
    {"info.rtopacketovhead", G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_RTOPACKETOVHEAD,  TRUE},
    {"info.rtofrovhead",     G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_RTOFROVHEAD,  TRUE},
    {"info.rto_channels",    G_TYPE_UINT,   G_PARAM_READABLE,     PROP_INFO_RTO_CHANNELS, TRUE},
    // Color correction (non concerned)
    // Camera status monitoring
    {"info.modes", G_TYPE_UINT,     G_PARAM_READABLE,   PROP_INFO_MODES,    TRUE},
    // meta
    {"meta.name",    G_TYPE_STRING,     G_PARAM_READWRITE,  PROP_META_NAME, TRUE},
    {"meta.lens",    G_TYPE_STRING,     G_PARAM_READABLE,   PROP_META_LENS, TRUE},
    {"meta.fstop",   G_TYPE_FLOAT,  G_PARAM_READABLE,    PROP_META_FSTOP,    TRUE},
    {"meta.flen",    G_TYPE_FLOAT,  G_PARAM_READABLE,    PROP_META_FLEN, TRUE},
    {"meta.comment", G_TYPE_STRING,     G_PARAM_READWRITE,  PROP_META_COMMENT,  TRUE},
    {"meta.xset",    G_TYPE_STRING,     G_PARAM_READWRITE,  PROP_META_XSET, TRUE},
    // cam
    {"cam.syncimg", G_TYPE_UINT,    G_PARAM_READWRITE,     PROP_CAM_SYNCIMG,   TRUE},
    {"cam.frdelay", G_TYPE_UINT,    G_PARAM_READWRITE,     PROP_CAM_FRDELAY,   TRUE},
    {"cam.rtoen",   G_TYPE_UINT,    G_PARAM_READWRITE,     PROP_CAM_RTOEN, TRUE},
    {"cam.rtotfr",  G_TYPE_UINT,    G_PARAM_READWRITE,     PROP_CAM_RTOTFR,    TRUE},
    {"cam.membpp",  G_TYPE_UINT,    G_PARAM_READWRITE,     PROP_CAM_MEMBPP,    TRUE},
    // Global camera options
    {"cam.trigpol",    G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_CAM_TRIGPOL,   TRUE},
    {"cam.trigfilt",   G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_CAM_TRIGFILT,  TRUE},
    {"cam.startonacq", G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_CAM_STARTONACQ,    TRUE},
    {"cam.tsformat",   G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_CAM_TSFORMAT,  TRUE},
    {"cam.tcmode",     G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_CAM_TCMODE,    TRUE},
    {"cam.master",     G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_CAM_MASTER,    TRUE},
    {"cam.apoffdis",   G_TYPE_INT,  G_PARAM_READWRITE,   PROP_CAM_APOFFDIS,  TRUE},
    {"cam.longready",  G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CAM_LONGREADY, TRUE},
    {"cam.cines",      G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CAM_CINES, TRUE},
    {"cam.dark",       G_TYPE_INT,  G_PARAM_READWRITE,   PROP_CAM_DARK,  TRUE},
    {"cam.tsetsns",    G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CAM_TSETSNS,   TRUE},
    {"cam.tsetcam",    G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CAM_TSETCAM,   TRUE},
    {"cam.tz",         G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_CAM_TZ,    TRUE},
    {"cam.mode",       G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CAM_MODE,  TRUE},
    // ethernet
    {"eth.ip",         G_TYPE_STRING,   G_PARAM_READWRITE,    PROP_ETH_IP,    TRUE},
    {"eth.netmask",    G_TYPE_STRING,   G_PARAM_READWRITE,    PROP_ETH_NETMASK,   TRUE},
    {"eth.broadcast",  G_TYPE_STRING,   G_PARAM_READWRITE,    PROP_ETH_BROADCAST, TRUE},
    {"eth.gateway",    G_TYPE_STRING,   G_PARAM_READWRITE,    PROP_ETH_GATEWAY,   TRUE},
    {"eth.mtu",        G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_ETH_MTU,   TRUE},
    {"eth.xip",        G_TYPE_STRING,   G_PARAM_READWRITE,    PROP_ETH_XIP,   TRUE},
    {"eth.xnetmask",   G_TYPE_STRING,   G_PARAM_READWRITE,    PROP_ETH_XNETMASK,  TRUE},
    {"eth.xbroadcast", G_TYPE_STRING,   G_PARAM_READWRITE,    PROP_ETH_XBROADCAST,    TRUE},
    // video
    {"video.system",     G_TYPE_UINT,   G_PARAM_READWRITE,    PROP_VIDEO_SYSTEM,  TRUE},
    {"video.output",     G_TYPE_UINT,   G_PARAM_READWRITE,    PROP_VIDEO_OUTPUT,  TRUE},
    {"video.fields",     G_TYPE_UINT,   G_PARAM_READWRITE,    PROP_VIDEO_FIELDS,  TRUE},
    {"video.widescreen", G_TYPE_FLOAT,  G_PARAM_READWRITE,   PROP_VIDEO_WIDESCREEN,  TRUE},
    {"video.genlock",    G_TYPE_UINT,   G_PARAM_READWRITE,    PROP_VIDEO_GENLOCK, TRUE},
    {"video.vfmode",     G_TYPE_UINT,   G_PARAM_READWRITE,    PROP_VIDEO_VFMODE,  TRUE},
    {"video.uzoom", G_TYPE_UINT,    G_PARAM_READABLE,  PROP_VIDEO_UZOOM,   TRUE},
    {"video.vox",   G_TYPE_UINT,    G_PARAM_READABLE,  PROP_VIDEO_VOX, TRUE},
    {"video.voy",   G_TYPE_UINT,    G_PARAM_READABLE,  PROP_VIDEO_VOY, TRUE},
    {"video.vow",   G_TYPE_UINT,    G_PARAM_READABLE,  PROP_VIDEO_VOW, TRUE},
    {"video.voh",   G_TYPE_UINT,    G_PARAM_READABLE,  PROP_VIDEO_VOH, TRUE},
    {"video.vw",    G_TYPE_UINT,    G_PARAM_READABLE,  PROP_VIDEO_VW,  TRUE},
    {"video.vh",    G_TYPE_UINT,    G_PARAM_READABLE,  PROP_VIDEO_VH,  TRUE},
    // Video image adjustments (TODO: check if is relevant)
    {"video.adj.red",    G_TYPE_FLOAT,  G_PARAM_READABLE,    PROP_VIDEO_ADJ_RED, TRUE},
    {"video.adj.green",  G_TYPE_FLOAT,  G_PARAM_READABLE,    PROP_VIDEO_ADJ_GREEN,   TRUE},
    {"video.adj.blue",   G_TYPE_FLOAT,  G_PARAM_READABLE,    PROP_VIDEO_ADJ_BLUE,    TRUE},
    {"video.adj.toe",    G_TYPE_FLOAT,  G_PARAM_READABLE,    PROP_VIDEO_ADJ_TOE, TRUE},
    {"video.adj.gamma",  G_TYPE_FLOAT,  G_PARAM_READABLE,    PROP_VIDEO_ADJ_GAMMA,   TRUE},
    {"video.adj.rgamma", G_TYPE_FLOAT,  G_PARAM_READABLE,    PROP_VIDEO_ADJ_RGAMMA,  TRUE},
    {"video.adj.bgamma", G_TYPE_FLOAT,  G_PARAM_READABLE,    PROP_VIDEO_ADJ_BGAMMA,  TRUE},
    {"video.adj.gain",   G_TYPE_FLOAT,  G_PARAM_READABLE,    PROP_VIDEO_ADJ_GAIN,    TRUE},
    {"video.adj.offset", G_TYPE_FLOAT,  G_PARAM_READABLE,    PROP_VIDEO_ADJ_OFFSET,  TRUE},
    {"video.adj.flare",  G_TYPE_FLOAT,  G_PARAM_READABLE,    PROP_VIDEO_ADJ_FLARE,   TRUE},
    {"video.adj.hue",    G_TYPE_FLOAT,  G_PARAM_READABLE,    PROP_VIDEO_ADJ_HUE, TRUE},
    {"video.adj.sat",    G_TYPE_FLOAT,  G_PARAM_READABLE,    PROP_VIDEO_ADJ_SAT, TRUE},
    {"video.adj.rped",   G_TYPE_FLOAT,  G_PARAM_READABLE,    PROP_VIDEO_ADJ_RPED,    TRUE},
    {"video.adj.gped",   G_TYPE_FLOAT,  G_PARAM_READABLE,    PROP_VIDEO_ADJ_GPED,    TRUE},
    {"video.adj.bped",   G_TYPE_FLOAT,  G_PARAM_READABLE,    PROP_VIDEO_ADJ_BPED,    TRUE},
    {"video.adj.chroma", G_TYPE_FLOAT,  G_PARAM_READABLE,    PROP_VIDEO_ADJ_CHROMA,  TRUE},
    {"video.adj.tone",   G_TYPE_STRING,     G_PARAM_READABLE,   PROP_VIDEO_ADJ_TONE,    TRUE},
    {"video.adj.matrix", G_TYPE_UINT,   G_PARAM_READABLE,     PROP_VIDEO_ADJ_MATRIX,  TRUE},
    {"video.adj.log",    G_TYPE_UINT,   G_PARAM_READABLE,     PROP_VIDEO_ADJ_LOG, TRUE},
    // [...] other params, maybe not important
    // irig // Inter-Range Instrumentation Group
    {"irig.sec",       G_TYPE_UINT,     G_PARAM_READABLE,   PROP_IRIG_SEC,  TRUE},
    {"irig.yearbegin", G_TYPE_UINT,     G_PARAM_READABLE,   PROP_IRIG_YEARBEGIN,    TRUE},
    {"irig.flags",     G_TYPE_FLAGS,    G_PARAM_READABLE,  PROP_IRIG_FLAGS,    TRUE},
    {"irig.signal",    G_TYPE_STRING,   G_PARAM_READABLE,     PROP_IRIG_SIGNAL,   TRUE},
    {"irig.gps",       G_TYPE_STRING,   G_PARAM_READABLE,     PROP_IRIG_GPS,  TRUE},
    {"irig.range",     G_TYPE_STRING,   G_PARAM_READABLE,     PROP_IRIG_RANGE,    TRUE},
    // cinemag
    {"mag.state",    G_TYPE_UINT,   G_PARAM_READABLE,     PROP_MAG_STATE, TRUE},
    {"mag.progress", G_TYPE_UINT,   G_PARAM_READABLE,     PROP_MAG_PROGRESS,  TRUE},
    {"mag.protect",  G_TYPE_UINT,   G_PARAM_READABLE,     PROP_MAG_PROTECT,   TRUE},
    {"mag.size",     G_TYPE_UINT,   G_PARAM_READABLE,     PROP_MAG_SIZE,  TRUE},
    {"mag.used",     G_TYPE_UINT,   G_PARAM_READABLE,     PROP_MAG_USED,  TRUE},
    {"mag.takes",    G_TYPE_UINT,   G_PARAM_READABLE,     PROP_MAG_TAKES, TRUE},
    {"mag.version",  G_TYPE_UINT,   G_PARAM_READABLE,     PROP_MAG_VERSION,   TRUE},
    {"mag.id",       G_TYPE_STRING,     G_PARAM_READABLE,   PROP_MAG_ID,    TRUE},
    {"mag.runstop",  G_TYPE_INT,    G_PARAM_READWRITE,     PROP_MAG_RUNSTOP,   TRUE},
    {"mag.type",     G_TYPE_UINT,   G_PARAM_READABLE,     PROP_MAG_TYPE,  TRUE},
    // default_cine
    {"defc.res",        PHANTOM_TYPE_RES,   G_PARAM_READWRITE,    PROP_DEFC_RES,  TRUE},
    {"defc.rate",       G_TYPE_FLOAT,   G_PARAM_READWRITE,    PROP_DEFC_RATE, TRUE},
    {"defc.exp",        G_TYPE_UINT,    G_PARAM_READWRITE,     PROP_DEFC_EXP,  TRUE},
    {"defc.edrexp",     G_TYPE_UINT,    G_PARAM_READWRITE,     PROP_DEFC_EDREXP,   TRUE},
    {"defc.ptframes",   G_TYPE_UINT,    G_PARAM_READWRITE,     PROP_DEFC_PTFRAMES, TRUE},
    {"defc.shoff",      G_TYPE_UINT,    G_PARAM_READWRITE,     PROP_DEFC_SHOFF,    TRUE},
    {"defc.ramp",       G_TYPE_UINT,    G_PARAM_READWRITE,     PROP_DEFC_RAMP, TRUE},
    {"defc.bcount",     G_TYPE_UINT,    G_PARAM_READWRITE,     PROP_DEFC_BCOUNT,   TRUE},
    {"defc.bperiod",    G_TYPE_UINT,    G_PARAM_READWRITE,     PROP_DEFC_BPERIOD,  TRUE},
    {"defc.hqenable",   G_TYPE_UINT,    G_PARAM_READWRITE,     PROP_DEFC_HQENABLE, TRUE},
    {"defc.decimation", G_TYPE_UINT,    G_PARAM_READWRITE,     PROP_DEFC_DECIMATION,   TRUE},
    {"defc.frcount",    G_TYPE_UINT,    G_PARAM_READABLE,  PROP_DEFC_FRCOUNT,  TRUE},
    {"defc.frsize",     G_TYPE_UINT,    G_PARAM_READABLE,  PROP_DEFC_FRSIZE,   TRUE},
    {"defc.aexpmode",   G_TYPE_UINT,    G_PARAM_READWRITE,     PROP_DEFC_AEXPMODE, TRUE},
    {"defc.aexpcomp",   G_TYPE_FLOAT,   G_PARAM_READWRITE,    PROP_DEFC_AEXPCOMP, TRUE},
    {"defc.meta.ox",    G_TYPE_INT,     G_PARAM_READWRITE,  PROP_DEFC_META_OX,  TRUE},
    {"defc.meta.oy",    G_TYPE_INT,     G_PARAM_READWRITE,  PROP_DEFC_META_OY,  TRUE},
    {"defc.meta.w",     G_TYPE_INT,     G_PARAM_READWRITE,  PROP_DEFC_META_W,   TRUE},
    {"defc.meta.h",     G_TYPE_INT,     G_PARAM_READWRITE,  PROP_DEFC_META_H,   TRUE},
    {"defc.meta.ow",    G_TYPE_INT,     G_PARAM_READWRITE,  PROP_DEFC_META_OW,  TRUE},
    {"defc.meta.oh",    G_TYPE_INT,     G_PARAM_READWRITE,  PROP_DEFC_META_OH,  TRUE},
    {"defc.meta.crop",  G_TYPE_INT,     G_PARAM_READWRITE,  PROP_DEFC_META_CROP,    TRUE},
    //{ defc.meta.?,            G_TYPE_UINT, G_PARAM_READABLE, , TRUE},
    // storage_device
    {"cf.state",    G_TYPE_INT,     G_PARAM_READABLE,   PROP_CF_STATE,  TRUE},
    {"cf.action",   G_TYPE_UINT,    G_PARAM_READABLE,  PROP_CF_ACTION, TRUE},
    {"cf.size",     G_TYPE_UINT,    G_PARAM_READABLE,  PROP_CF_SIZE,   TRUE},
    {"cf.used",     G_TYPE_UINT,    G_PARAM_READABLE,  PROP_CF_USED,   TRUE},
    {"cf.progress", G_TYPE_UINT,    G_PARAM_READABLE,  PROP_CF_PROGRESS,   TRUE},
    {"cf.errrcode", G_TYPE_UINT,    G_PARAM_READABLE,  PROP_CF_ERRRCODE,   TRUE},
    // cine_tables
    {"c#.state",   G_TYPE_FLAGS,   G_PARAM_READABLE,     PROP_CT_STATE,  TRUE},
    {"c#.frcount", G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CT_FRCOUNT,    TRUE},
    {"c#.firstfr", G_TYPE_INT,  G_PARAM_READABLE,    PROP_CT_FIRSTFR,    TRUE},
    {"c#.lastfr",  G_TYPE_INT,  G_PARAM_READABLE,    PROP_CT_LASTFR, TRUE},
    {"c#.format",  G_TYPE_INT,  G_PARAM_READABLE,    PROP_CT_FORMAT, TRUE},
    {"c#.in",      G_TYPE_INT,  G_PARAM_READABLE,    PROP_CT_IN, TRUE},
    {"c#.out",     G_TYPE_INT,  G_PARAM_READABLE,    PROP_CT_OUT,    TRUE},
    // memory allocation
    {"c#.start",   PHANTOM_TYPE_HEX,    G_PARAM_READABLE,  PROP_CT_START,  TRUE},
    {"c#.len",     PHANTOM_TYPE_HEX,    G_PARAM_READABLE,  PROP_CT_LEN,    TRUE},
    {"c#.frsize",  G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CT_FRSIZE, TRUE},
    {"c#.frspace", G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CT_FRSPACE,    TRUE},
    // trigger time
    {"c#.trigtime.secs", G_TYPE_UINT,   G_PARAM_READABLE,     PROP_CT_TRIGTIME_SECS,  TRUE},
    {"c#.trigtime.frac", G_TYPE_UINT,   G_PARAM_READABLE,     PROP_CT_TRIGTIME_FRAC,  TRUE},
    //cam/info/adj/meta substructures
    {"c#.cam",         G_TYPE_UINT,  G_PARAM_READABLE,    PROP_CT_CAM,    TRUE},
    {"c#.info",        G_TYPE_UINT,  G_PARAM_READABLE,    PROP_CT_INFO,   TRUE},
    {"c#.adj",         G_TYPE_UINT,  G_PARAM_READABLE,    PROP_CT_ADJ,    TRUE},
    {"c#.meta.pbrate", G_TYPE_FLOAT,    G_PARAM_READABLE,  PROP_CT_META_PBRATE,    TRUE},
    {"c#.meta.tcrate", G_TYPE_FLOAT,    G_PARAM_READABLE,  PROP_CT_META_TCRATE,    TRUE},
    {"c#.meta.uuid",   G_TYPE_STRING,   G_PARAM_READABLE,     PROP_CT_META_UUID,  TRUE},
    {"c#.meta.system", G_TYPE_STRING,   G_PARAM_READABLE,     PROP_CT_META_SYSTEM,    TRUE},
    {"c#.meta.trigtc", G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CT_META_TRIGTC,    TRUE},
    {"c#.meta.pax",    G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CT_META_PAX,   TRUE},
    {"c#.meta.pay",    G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CT_META_PAY,   TRUE},
    {"c#.meta.paox",   G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CT_META_PAOX,  TRUE},
    {"c#.meta.paoy",   G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CT_META_PAOY,  TRUE},
    {"c#.meta.ox",     G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CT_META_OX,    TRUE},
    {"c#.meta.oy",     G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CT_META_OY,    TRUE},
    {"c#.meta.ow",     G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CT_META_OW,    TRUE},
    {"c#.meta.oh",     G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CT_META_OH,    TRUE},
    {"c#.meta.w",      G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CT_META_W, TRUE},
    {"c#.meta.h",      G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CT_META_H, TRUE},
    {"c#.meta.crop",   G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CT_META_CROP,  TRUE},
    {"c#.meta.resize", G_TYPE_UINT,     G_PARAM_READABLE,   PROP_CT_META_RESIZE,    TRUE},
    {"c#.meta.gps",    G_TYPE_STRING,   G_PARAM_READABLE,     PROP_CT_META_GPS,   TRUE},
    // automatic
    {"auto.videoplay",         G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_AUTO_VIDEOPLAY,    TRUE},
    {"auto.flashsave",         G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_AUTO_FLASHSAVE,    TRUE},
    {"auto.filesave",          G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_AUTO_FILESAVE, TRUE},
    {"auto.acqrestart",        G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_AUTO_ACQRESTART,   TRUE},
    {"auto.bref",              G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_AUTO_BREF, TRUE},
    {"auto.firstframe",        G_TYPE_INT,  G_PARAM_READWRITE,   PROP_AUTO_FIRSTFRAME,   TRUE},
    {"auto.lastframe",         G_TYPE_INT,  G_PARAM_READWRITE,   PROP_AUTO_LASTFRAME,    TRUE},
    {"auto.loops",             G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_AUTO_LOOPS,    TRUE},
    {"auto.speed",             G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_AUTO_SPEED,    TRUE},
    {"auto.progress",          G_TYPE_UINT,     G_PARAM_READABLE,   PROP_AUTO_PROGRESS, TRUE},
    {"auto.bref_progress",     G_TYPE_UINT,     G_PARAM_READABLE,   PROP_AUTO_BREF_PROGRESS,    TRUE},
    {"auto.trigger.x",         G_TYPE_INT,  G_PARAM_READWRITE,   PROP_AUTO_TRIGGER_X,    TRUE},
    {"auto.trigger.y",         G_TYPE_INT,  G_PARAM_READWRITE,   PROP_AUTO_TRIGGER_Y,    TRUE},
    {"auto.trigger.w",         G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_AUTO_TRIGGER_W,    TRUE},
    {"auto.trigger.h",         G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_AUTO_TRIGGER_H,    TRUE},
    {"auto.trigger.threshold", G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_AUTO_TRIGGER_THRESHOLD,    TRUE},
    {"auto.trigger.area",      G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_AUTO_TRIGGER_AREA, TRUE},
    {"auto.trigger.speed",     G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_AUTO_TRIGGER_SPEED,    TRUE},
    {"auto.trigger.mode",      G_TYPE_UINT,     G_PARAM_READWRITE,  PROP_AUTO_TRIGGER_MODE, TRUE},
    {NULL, }
};

enum TerminatePhantomDiscover {
    ALL,
    REGEX,
    RECEIVE,
    SOCKET,
    BCAST
};

#define UCA_PHANTOM_CAMERA_ERROR uca_phantom_camera_error_quark()
typedef enum {
    UCA_PHANTOM_CAMERA_ERROR_INIT,
    UCA_PHANTOM_CAMERA_ERROR_DISCOVER,
    UCA_PHANTOM_CAMERA_ERROR_STOP_RECORDING,
} UcaPhantomNetworkError;

/*
 * Phantom Request data structure
 * TODO: Doc
 * Note: owner free's data
*/
struct _PhantomRequest {
    Unit variable;
    gchar* raw;
    gsize size, write_size;
    // gssize write_size;
};
typedef struct _PhantomRequest PhantomRequest;

/*
 * Phantom Reply data structure
 * TODO: Doc
 * Note: owner free's data
*/
struct _PhantomReply {
    gchar* raw;
    GValue value;
    gsize size;
    gssize read_size;
};
typedef struct _PhantomReply PhantomReply;

static GSocketAddress *
phantom_discover (gboolean x_enabled, GError **error) {
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

    gchar *bcast_address = (x_enabled==TRUE) ? "172.16.255.255" : "100.100.255.255";
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
uca_phantom_communicate (GSocketConnection *connection, PhantomRequest *request, PhantomReply *reply, GError **error) {
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
    g_return_val_if_fail (request != NULL || reply != NULL, FALSE);

    GError *sub_error = NULL;

    // TODO: check that the streams are succesfully fetched
    GOutputStream * ostream = g_io_stream_get_output_stream (G_IO_STREAM (connection));
    GInputStream * istream = g_io_stream_get_input_stream (G_IO_STREAM (connection));

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
 * Get unit variable
 * TODO: Doc
 * Note: All parameter data belongs to the user!
*/
gboolean uca_phantom_get_variable (GSocketConnection *connection, guint variable_flag, GValue *return_value, GError **error) {
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
    gboolean communicated = uca_phantom_communicate (connection, &request, &reply, &sub_error);

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

int main() {
    GError *error = NULL;
    GValue val = G_VALUE_INIT;

    /* create a new connection */
    GSocketConnection *connection = NULL;
    GSocketClient *client = g_socket_client_new();

    GSocketAddress *addr = phantom_discover(TRUE, &error);

    /* connect to the host */
    connection = g_socket_client_connect (
        client,
        G_SOCKET_CONNECTABLE(addr),
        NULL,
        &error);

    /* don't forget to check for errors */
    if (error != NULL) {
        g_print ("Houston theres a problem: %s\n", error->message);
        return FALSE;
    }
    
    g_print ("Connection successful!\n");
    // for (int i=0; i < N_UNIT_PROPERTIES; i++) {
    //     gboolean result = uca_phantom_get_variable (connection, i, &error);
    //     if (!result) {
    //         g_print ("Houston theres a problem: %s\n", error->message);
    //         g_error_free (error);
    //         return FALSE;
    //     }
    // }   
    gboolean result = uca_phantom_get_variable (connection, PROP_INFO_MODEL, &val, &error);
    gboolean result2 = uca_phantom_get_variable (connection, PROP_INFO_MODEL, &val, &error);

    if (!result || !result2) {
        g_print ("Houston theres a problem: %s\n", error->message);
        g_error_free (error);
        return FALSE;
    }

    g_object_unref (addr);
    g_object_unref (connection);
    g_object_unref (client);
    g_value_unset (&val);


    return TRUE;
}