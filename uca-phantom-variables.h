#ifndef UCA_PHANTOM_VARIABLES_H
#define UCA_PHANTOM_VARIABLES_H

#include <gio/gio.h>
#include <gmodule.h>
#include <glib-object.h>

/*
 * Phantom-specific data types
 * TODO: For the time being, they will remain as strings...
*/
#define PHANTOM_TYPE_HEX G_TYPE_STRING
#define PHANTOM_TYPE_RES G_TYPE_STRING
#define PHANTOM_TYPE_FLAGS G_TYPE_STRING
typedef struct {
    const gchar *name;
    GType        type;
    GParamFlags  flags;
    gint         property_id;
    gboolean     handle_automatically;
} Unit;

Unit variables[] = {
    // Sensor information
    {"info.sensor",     G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_INFO_SENSOR,   TRUE},
    {"info.snsversion", G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_INFO_SNSVERSION,   TRUE},
    {"info.cfa",        G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_INFO_CFA,  TRUE},
    {"info.filter",     G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_INFO_FILTER,   TRUE},
    {"info.hwver",      G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_INFO_HWVER,    TRUE},
    {"info.kernel",     G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_INFO_KERNEL,   TRUE},
    {"info.swver",      G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_INFO_SWVER,    TRUE},
    {"info.xver",       G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_INFO_XVER, TRUE},
    {"info.model",      G_TYPE_STRING,  G_PARAM_READABLE,    UNIT_INFO_MODEL,    TRUE},
    {"info.pver",       G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_INFO_PVER, TRUE},
    {"info.sver",       G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_INFO_SVER, TRUE},
    {"info.serial",     G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_INFO_SERIAL,   TRUE},
    {"info.name",       G_TYPE_STRING,  G_PARAM_READWRITE,   UNIT_INFO_NAME, TRUE},
    // Capabilities
    {"info.features",        G_TYPE_STRING,     G_PARAM_READABLE,   UNIT_INFO_FEATURES, TRUE},
    {"info.imgformats",      G_TYPE_STRING,     G_PARAM_READABLE,   UNIT_INFO_IMGFORMATS,   TRUE},
    {"info.videosystems",    G_TYPE_STRING,     G_PARAM_READABLE,   UNIT_INFO_VIDEOSYSTEMS, TRUE},
    {"info.maxcines",        G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_MAXCINES, TRUE},
    {"info.xmax",            G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_XMAX, TRUE},
    {"info.ymax",            G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_YMAX, TRUE},
    {"info.xinc",            G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_XINC, TRUE},
    {"info.yinc",            G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_YINC, TRUE},
    {"info.winx",            G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_WINX, TRUE},
    {"info.winy",            G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_WINY, TRUE},
    {"info.kernsz",          G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_KERNSZ,   TRUE},
    {"info.memsz",           G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_MEMSZ,    TRUE},
    {"info.cinemem",         G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_CINEMEM,  TRUE},
    {"info.mdepths",         PHANTOM_TYPE_HEX,  G_PARAM_READABLE,    UNIT_INFO_MDEPTHS,  TRUE},
    {"info.expdead",         G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_EXPDEAD,  TRUE},
    {"info.minexp",          G_TYPE_UINT,   G_PARAM_READWRITE,    UNIT_INFO_MINEXP,   TRUE},
    {"info.xblock",          G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_XBLOCK,   TRUE},
    {"info.yblock",          G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_YBLOCK,   TRUE},
    {"info.pixps",           G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_PIXPS,    TRUE},
    {"info.rotps",           G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_ROTPS,    TRUE},
    {"info.fotps",           G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_FOTPS,    TRUE},
    {"info.minfrate",        G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_MINFRATE, TRUE},
    {"info.maxrate",         G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_MAXRATE,  TRUE},
    {"info.tmodel",          G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_TMODEL,   TRUE},
    {"info.magtp",           G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_MAGTP,    TRUE},
    {"info.rtobyteps",       G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_RTOBYTEPS,    TRUE},
    {"info.rtopacket",       G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_RTOPACKET,    TRUE},
    {"info.rtopacketovhead", G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_RTOPACKETOVHEAD,  TRUE},
    {"info.rtofrovhead",     G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_RTOFROVHEAD,  TRUE},
    {"info.rto_channels",    G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_INFO_RTO_CHANNELS, TRUE},
    // Color correction (non concerned)
    // Camera status monitoring
    {"info.modes", G_TYPE_UINT,     G_PARAM_READABLE,   UNIT_INFO_MODES,    TRUE},
    // meta
    {"meta.name",    G_TYPE_STRING,     G_PARAM_READWRITE,  UNIT_META_NAME, TRUE},
    {"meta.lens",    G_TYPE_STRING,     G_PARAM_READABLE,   UNIT_META_LENS, TRUE},
    {"meta.fstop",   G_TYPE_FLOAT,  G_PARAM_READABLE,    UNIT_META_FSTOP,    TRUE},
    {"meta.flen",    G_TYPE_FLOAT,  G_PARAM_READABLE,    UNIT_META_FLEN, TRUE},
    {"meta.comment", G_TYPE_STRING,     G_PARAM_READWRITE,  UNIT_META_COMMENT,  TRUE},  // note: these are 4096chars long !
    {"meta.xset",    G_TYPE_STRING,     G_PARAM_READWRITE,  UNIT_META_XSET, TRUE}, // note: these are 4096chars long !
    // cam
    {"cam.syncimg", G_TYPE_UINT,    G_PARAM_READWRITE,     UNIT_CAM_SYNCIMG,   TRUE},
    {"cam.frdelay", G_TYPE_UINT,    G_PARAM_READWRITE,     UNIT_CAM_FRDELAY,   TRUE},
    {"cam.rtoen",   G_TYPE_UINT,    G_PARAM_READWRITE,     UNIT_CAM_RTOEN, TRUE},
    {"cam.rtotfr",  G_TYPE_UINT,    G_PARAM_READWRITE,     UNIT_CAM_RTOTFR,    TRUE},
    {"cam.membpp",  G_TYPE_UINT,    G_PARAM_READWRITE,     UNIT_CAM_MEMBPP,    TRUE},
    // Global camera options
    {"cam.trigpol",    G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_CAM_TRIGPOL,   TRUE},
    {"cam.trigfilt",   G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_CAM_TRIGFILT,  TRUE},
    {"cam.startonacq", G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_CAM_STARTONACQ,    TRUE},
    {"cam.tsformat",   G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_CAM_TSFORMAT,  TRUE},
    {"cam.tcmode",     G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_CAM_TCMODE,    TRUE},
    {"cam.master",     G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_CAM_MASTER,    TRUE},
    {"cam.apoffdis",   G_TYPE_INT,  G_PARAM_READWRITE,   UNIT_CAM_APOFFDIS,  TRUE},
    {"cam.longready",  G_TYPE_UINT,     G_PARAM_READABLE,   UNIT_CAM_LONGREADY, TRUE},
    {"cam.cines",      G_TYPE_UINT,     G_PARAM_READABLE,   UNIT_CAM_CINES, TRUE},
    {"cam.dark",       G_TYPE_INT,  G_PARAM_READWRITE,   UNIT_CAM_DARK,  TRUE},
    {"cam.tsetsns",    G_TYPE_UINT,     G_PARAM_READABLE,   UNIT_CAM_TSETSNS,   TRUE},
    {"cam.tsetcam",    G_TYPE_UINT,     G_PARAM_READABLE,   UNIT_CAM_TSETCAM,   TRUE},
    // {"cam.tz",         G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_CAM_TZ,    TRUE},
    {"cam.mode",       G_TYPE_UINT,     G_PARAM_READABLE,   UNIT_CAM_MODE,  TRUE},
    // ethernet
    {"eth.ip",         G_TYPE_STRING,   G_PARAM_READWRITE,    UNIT_ETH_IP,    TRUE},
    {"eth.netmask",    G_TYPE_STRING,   G_PARAM_READWRITE,    UNIT_ETH_NETMASK,   TRUE},
    {"eth.broadcast",  G_TYPE_STRING,   G_PARAM_READWRITE,    UNIT_ETH_BROADCAST, TRUE},
    {"eth.gateway",    G_TYPE_STRING,   G_PARAM_READWRITE,    UNIT_ETH_GATEWAY,   TRUE},
    {"eth.mtu",        G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_ETH_MTU,   TRUE},
    {"eth.xip",        G_TYPE_STRING,   G_PARAM_READWRITE,    UNIT_ETH_XIP,   TRUE},
    {"eth.xnetmask",   G_TYPE_STRING,   G_PARAM_READWRITE,    UNIT_ETH_XNETMASK,  TRUE},
    {"eth.xbroadcast", G_TYPE_STRING,   G_PARAM_READWRITE,    UNIT_ETH_XBROADCAST,    TRUE},
    // video
    {"video.system",     G_TYPE_UINT,   G_PARAM_READWRITE,    UNIT_VIDEO_SYSTEM,  TRUE},
    {"video.output",     G_TYPE_UINT,   G_PARAM_READWRITE,    UNIT_VIDEO_OUTPUT,  TRUE},
    {"video.fields",     G_TYPE_UINT,   G_PARAM_READWRITE,    UNIT_VIDEO_FIELDS,  TRUE},
    {"video.widescreen", G_TYPE_FLOAT,  G_PARAM_READWRITE,   UNIT_VIDEO_WIDESCREEN,  TRUE},
    {"video.genlock",    G_TYPE_UINT,   G_PARAM_READWRITE,    UNIT_VIDEO_GENLOCK, TRUE},
    {"video.vfmode",     G_TYPE_UINT,   G_PARAM_READWRITE,    UNIT_VIDEO_VFMODE,  TRUE},
    {"video.uzoom", G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_VIDEO_UZOOM,   TRUE},
    {"video.vox",   G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_VIDEO_VOX, TRUE},
    {"video.voy",   G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_VIDEO_VOY, TRUE},
    {"video.vow",   G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_VIDEO_VOW, TRUE},
    {"video.voh",   G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_VIDEO_VOH, TRUE},
    {"video.vw",    G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_VIDEO_VW,  TRUE},
    {"video.vh",    G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_VIDEO_VH,  TRUE},
    // Video image adjustments (TODO: check if is relevant)
    {"video.adj.red",    G_TYPE_FLOAT,  G_PARAM_READABLE,    UNIT_VIDEO_ADJ_RED, TRUE},
    {"video.adj.green",  G_TYPE_FLOAT,  G_PARAM_READABLE,    UNIT_VIDEO_ADJ_GREEN,   TRUE},
    {"video.adj.blue",   G_TYPE_FLOAT,  G_PARAM_READABLE,    UNIT_VIDEO_ADJ_BLUE,    TRUE},
    {"video.adj.toe",    G_TYPE_FLOAT,  G_PARAM_READABLE,    UNIT_VIDEO_ADJ_TOE, TRUE},
    {"video.adj.gamma",  G_TYPE_FLOAT,  G_PARAM_READABLE,    UNIT_VIDEO_ADJ_GAMMA,   TRUE},
    {"video.adj.rgamma", G_TYPE_FLOAT,  G_PARAM_READABLE,    UNIT_VIDEO_ADJ_RGAMMA,  TRUE},
    {"video.adj.bgamma", G_TYPE_FLOAT,  G_PARAM_READABLE,    UNIT_VIDEO_ADJ_BGAMMA,  TRUE},
    {"video.adj.gain",   G_TYPE_FLOAT,  G_PARAM_READABLE,    UNIT_VIDEO_ADJ_GAIN,    TRUE},
    {"video.adj.offset", G_TYPE_FLOAT,  G_PARAM_READABLE,    UNIT_VIDEO_ADJ_OFFSET,  TRUE},
    {"video.adj.flare",  G_TYPE_FLOAT,  G_PARAM_READABLE,    UNIT_VIDEO_ADJ_FLARE,   TRUE},
    {"video.adj.hue",    G_TYPE_FLOAT,  G_PARAM_READABLE,    UNIT_VIDEO_ADJ_HUE, TRUE},
    {"video.adj.sat",    G_TYPE_FLOAT,  G_PARAM_READABLE,    UNIT_VIDEO_ADJ_SAT, TRUE},
    {"video.adj.rped",   G_TYPE_FLOAT,  G_PARAM_READABLE,    UNIT_VIDEO_ADJ_RPED,    TRUE},
    {"video.adj.gped",   G_TYPE_FLOAT,  G_PARAM_READABLE,    UNIT_VIDEO_ADJ_GPED,    TRUE},
    {"video.adj.bped",   G_TYPE_FLOAT,  G_PARAM_READABLE,    UNIT_VIDEO_ADJ_BPED,    TRUE},
    {"video.adj.chroma", G_TYPE_FLOAT,  G_PARAM_READABLE,    UNIT_VIDEO_ADJ_CHROMA,  TRUE},
    {"video.adj.tone",   G_TYPE_STRING,     G_PARAM_READABLE,   UNIT_VIDEO_ADJ_TONE,    TRUE},
    {"video.adj.matrix", G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_VIDEO_ADJ_MATRIX,  TRUE},
    {"video.adj.log",    G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_VIDEO_ADJ_LOG, TRUE},
    // [...] other params, maybe not important
    // irig // Inter-Range Instrumentation Group
    {"irig.sec",       G_TYPE_UINT,     G_PARAM_READABLE,   UNIT_IRIG_SEC,  TRUE},
    {"irig.yearbegin", G_TYPE_UINT,     G_PARAM_READABLE,   UNIT_IRIG_YEARBEGIN,    TRUE},
    {"irig.flags",     PHANTOM_TYPE_FLAGS,    G_PARAM_READABLE,  UNIT_IRIG_FLAGS,    TRUE},
    {"irig.signal",    G_TYPE_STRING,   G_PARAM_READABLE,     UNIT_IRIG_SIGNAL,   TRUE},
    {"irig.gps",       G_TYPE_STRING,   G_PARAM_READABLE,     UNIT_IRIG_GPS,  TRUE},
    // {"irig.range",     G_TYPE_STRING,   G_PARAM_READABLE,     UNIT_IRIG_RANGE,    TRUE},
    // cinemag
    {"mag.state",    G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_MAG_STATE, TRUE},
    {"mag.progress", G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_MAG_PROGRESS,  TRUE},
    {"mag.protect",  G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_MAG_PROTECT,   TRUE},
    {"mag.size",     G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_MAG_SIZE,  TRUE},
    {"mag.used",     G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_MAG_USED,  TRUE},
    {"mag.takes",    G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_MAG_TAKES, TRUE},
    {"mag.version",  G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_MAG_VERSION,   TRUE},
    {"mag.id",       G_TYPE_STRING,     G_PARAM_READABLE,   UNIT_MAG_ID,    TRUE},
    {"mag.runstop",  G_TYPE_INT,    G_PARAM_READWRITE,     UNIT_MAG_RUNSTOP,   TRUE},
    {"mag.type",     G_TYPE_UINT,   G_PARAM_READABLE,     UNIT_MAG_TYPE,  TRUE},
    // default_cine
    {"defc.res",        PHANTOM_TYPE_RES,   G_PARAM_READWRITE,    UNIT_DEFC_RES,  TRUE},
    {"defc.rate",       G_TYPE_FLOAT,   G_PARAM_READWRITE,    UNIT_DEFC_RATE, TRUE},
    {"defc.exp",        G_TYPE_UINT,    G_PARAM_READWRITE,     UNIT_DEFC_EXP,  TRUE},
    {"defc.edrexp",     G_TYPE_UINT,    G_PARAM_READWRITE,     UNIT_DEFC_EDREXP,   TRUE},
    {"defc.ptframes",   G_TYPE_UINT,    G_PARAM_READWRITE,     UNIT_DEFC_PTFRAMES, TRUE},
    {"defc.shoff",      G_TYPE_UINT,    G_PARAM_READWRITE,     UNIT_DEFC_SHOFF,    TRUE},
    {"defc.ramp",       G_TYPE_UINT,    G_PARAM_READWRITE,     UNIT_DEFC_RAMP, TRUE},
    {"defc.bcount",     G_TYPE_UINT,    G_PARAM_READWRITE,     UNIT_DEFC_BCOUNT,   TRUE},
    {"defc.bperiod",    G_TYPE_UINT,    G_PARAM_READWRITE,     UNIT_DEFC_BPERIOD,  TRUE},
    {"defc.hqenable",   G_TYPE_UINT,    G_PARAM_READWRITE,     UNIT_DEFC_HQENABLE, TRUE},
    {"defc.decimation", G_TYPE_UINT,    G_PARAM_READWRITE,     UNIT_DEFC_DECIMATION,   TRUE},
    {"defc.frcount",    G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_DEFC_FRCOUNT,  TRUE},
    {"defc.frsize",     G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_DEFC_FRSIZE,   TRUE},
    {"defc.aexpmode",   G_TYPE_UINT,    G_PARAM_READWRITE,     UNIT_DEFC_AEXPMODE, TRUE},
    {"defc.aexpcomp",   G_TYPE_FLOAT,   G_PARAM_READWRITE,    UNIT_DEFC_AEXPCOMP, TRUE},
    {"defc.meta.ox",    G_TYPE_INT,     G_PARAM_READWRITE,  UNIT_DEFC_META_OX,  TRUE},
    {"defc.meta.oy",    G_TYPE_INT,     G_PARAM_READWRITE,  UNIT_DEFC_META_OY,  TRUE},
    {"defc.meta.w",     G_TYPE_INT,     G_PARAM_READWRITE,  UNIT_DEFC_META_W,   TRUE},
    {"defc.meta.h",     G_TYPE_INT,     G_PARAM_READWRITE,  UNIT_DEFC_META_H,   TRUE},
    {"defc.meta.ow",    G_TYPE_INT,     G_PARAM_READWRITE,  UNIT_DEFC_META_OW,  TRUE},
    {"defc.meta.oh",    G_TYPE_INT,     G_PARAM_READWRITE,  UNIT_DEFC_META_OH,  TRUE},
    {"defc.meta.crop",  G_TYPE_INT,     G_PARAM_READWRITE,  UNIT_DEFC_META_CROP,    TRUE},
    //{ defc.meta.?,            G_TYPE_UINT, G_PARAM_READABLE, , TRUE},
    // storage_device
    {"cf.state",    G_TYPE_INT,     G_PARAM_READABLE,   UNIT_CF_STATE,  TRUE},
    {"cf.action",   G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_CF_ACTION, TRUE},
    {"cf.size",     G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_CF_SIZE,   TRUE},
    {"cf.used",     G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_CF_USED,   TRUE},
    {"cf.progress", G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_CF_PROGRESS,   TRUE},
    {"cf.errcode", G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_CF_ERRRCODE,   TRUE},
    // Cine status 
    /*
    Name Type Acces c#.state flag list r/o c#.frcount uint r/o c#.firstfr int r/o c#.lastfr int r/o c#.format int r/o c#.in int r/o c#.out int r/o
    */
    {"c%d.state",  G_TYPE_INT,     G_PARAM_READABLE,  UNIT_CT_STATE,    TRUE},
    {"c%d.frcount",    G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_CT_FRCOUNT,  TRUE},
    {"c%d.firstfr",    G_TYPE_INT,     G_PARAM_READABLE,  UNIT_CT_FIRSTFR,  TRUE},
    {"c%d.lastfr", G_TYPE_INT,     G_PARAM_READABLE,  UNIT_CT_LASTFR,   TRUE},
    {"c%d.format",    G_TYPE_INT,     G_PARAM_READABLE,  UNIT_CT_FORMAT,   TRUE},
    {"c%d.in",    G_TYPE_INT,     G_PARAM_READABLE,  UNIT_CT_IN,   TRUE},
    {"c%d.out",   G_TYPE_INT,     G_PARAM_READABLE,  UNIT_CT_OUT,  TRUE},
    // Cine memory
    {"c%d.start",  PHANTOM_TYPE_HEX,    G_PARAM_READABLE,  UNIT_CT_STATE,    TRUE},
    {"c%d.len",    PHANTOM_TYPE_HEX,    G_PARAM_READABLE,  UNIT_CT_LEN,  TRUE},
    {"c%d.frsize", G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_CT_FRSIZE,   TRUE},
    {"c%d.frspace",    G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_CT_FRSPACE,  TRUE},
    // Trigger times substructure
    {"c%d.trigtime.secs",    G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_CT_TRIGTIME_SECS,    TRUE},
    {"c%d.trigtime.frac",    G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_CT_TRIGTIME_FRAC,    TRUE},
    // Cam substructure
    {"c%d.cam",    G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_CT_CAM,    TRUE},
    // Info substructure
    {"c%d.info",    G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_CT_INFO,    TRUE},
    // adl substructure
    {"c%d.adj",    G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_CT_ADJ,    TRUE},
    // Meta substructure
    {"c%d.meta.pbrate",  G_TYPE_FLOAT,     G_PARAM_READABLE,  UNIT_CT_META_PBRATE,    TRUE},
    {"c%d.meta.tcrate",    G_TYPE_FLOAT,    G_PARAM_READABLE,  UNIT_CT_META_TCRATE,  TRUE},
    {"c%d.meta.uuid",    G_TYPE_STRING,     G_PARAM_READABLE,  UNIT_CT_META_UUID,  TRUE},
    {"c%d.meta.system", G_TYPE_INT,     G_PARAM_READABLE,  UNIT_CT_META_SYSTEM,   TRUE},
    {"c%d.meta.trigtc",    G_TYPE_STRING,     G_PARAM_READABLE,  UNIT_CT_META_TRIGTC,   TRUE},
    {"c%d.meta.pax",    G_TYPE_INT,     G_PARAM_READABLE,  UNIT_CT_META_PAX,   TRUE},
    {"c%d.meta.pay",   G_TYPE_INT,     G_PARAM_READABLE,  UNIT_CT_META_PAY,  TRUE},
    {"c%d.meta.paox",  G_TYPE_INT,     G_PARAM_READABLE,  UNIT_CT_META_PAOX,    TRUE},
    {"c%d.meta.paoy",    G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_CT_META_PAOY,  TRUE},
    {"c%d.meta.ox",    G_TYPE_UINT,    G_PARAM_READABLE,  UNIT_CT_META_OX,  TRUE},
    {"c%d.meta.oy",    G_TYPE_INT,     G_PARAM_READABLE,  UNIT_CT_META_OY,  TRUE},
    {"c%d.meta.ow", G_TYPE_INT,     G_PARAM_READABLE,  UNIT_CT_META_OW,   TRUE},
    {"c%d.meta.oh",    G_TYPE_INT,     G_PARAM_READABLE,  UNIT_CT_META_OH,   TRUE},
    {"c%d.meta.w",    G_TYPE_INT,     G_PARAM_READABLE,  UNIT_CT_META_W,   TRUE},
    {"c%d.meta.h",   G_TYPE_INT,     G_PARAM_READABLE,  UNIT_CT_META_H,  TRUE},
    {"c%d.meta.crop", G_TYPE_INT,     G_PARAM_READABLE,  UNIT_CT_META_CROP,   TRUE},
    {"c%d.meta.resize",    G_TYPE_INT,     G_PARAM_READABLE,  UNIT_CT_META_RESIZE,   TRUE},
    {"c%d.meta.gps",   G_TYPE_STRING,     G_PARAM_READABLE,  UNIT_CT_META_GPS,  TRUE},

    // automatic
    {"auto.videoplay",         G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_AUTO_VIDEOPLAY,    TRUE},
    {"auto.flashsave",         G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_AUTO_FLASHSAVE,    TRUE},
    {"auto.filesave",          G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_AUTO_FILESAVE, TRUE},
    {"auto.acqrestart",        G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_AUTO_ACQRESTART,   TRUE},
    {"auto.bref",              G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_AUTO_BREF, TRUE},
    {"auto.firstframe",        G_TYPE_INT,  G_PARAM_READWRITE,   UNIT_AUTO_FIRSTFRAME,   TRUE},
    {"auto.lastframe",         G_TYPE_INT,  G_PARAM_READWRITE,   UNIT_AUTO_LASTFRAME,    TRUE},
    {"auto.loops",             G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_AUTO_LOOPS,    TRUE},
    {"auto.speed",             G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_AUTO_SPEED,    TRUE},
    {"auto.progress",          G_TYPE_UINT,     G_PARAM_READABLE,   UNIT_AUTO_PROGRESS, TRUE},
    {"auto.bref_progress",     G_TYPE_UINT,     G_PARAM_READABLE,   UNIT_AUTO_BREF_PROGRESS,    TRUE},
    {"auto.trigger.x",         G_TYPE_INT,  G_PARAM_READWRITE,   UNIT_AUTO_TRIGGER_X,    TRUE},
    {"auto.trigger.y",         G_TYPE_INT,  G_PARAM_READWRITE,   UNIT_AUTO_TRIGGER_Y,    TRUE},
    {"auto.trigger.w",         G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_AUTO_TRIGGER_W,    TRUE},
    {"auto.trigger.h",         G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_AUTO_TRIGGER_H,    TRUE},
    {"auto.trigger.threshold", G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_AUTO_TRIGGER_THRESHOLD,    TRUE},
    {"auto.trigger.area",      G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_AUTO_TRIGGER_AREA, TRUE},
    {"auto.trigger.speed",     G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_AUTO_TRIGGER_SPEED,    TRUE},
    {"auto.trigger.mode",      G_TYPE_UINT,     G_PARAM_READWRITE,  UNIT_AUTO_TRIGGER_MODE, TRUE},
    {NULL, }
};

#endif