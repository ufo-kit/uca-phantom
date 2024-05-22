/* Copyright (C) 2011, 2012 Matthias Vogelgesang <matthias.vogelgesang@kit.edu>
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

#ifndef __UCA_PHANTOM_CAMERA_H
#define __UCA_PHANTOM_CAMERA_H

#include <glib-object.h>
#include <uca/uca-camera.h>
#include "uca-phantom-communicate.h"
#include "phantom-enums.h"

G_BEGIN_DECLS

#define UCA_TYPE_PHANTOM_CAMERA             (uca_phantom_camera_get_type())
#define UCA_PHANTOM_CAMERA(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UCA_TYPE_PHANTOM_CAMERA, UcaPhantomCamera))
#define UCA_IS_PHANTOM_CAMERA(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UCA_TYPE_PHANTOM_CAMERA))
#define UCA_PHANTOM_CAMERA_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UCA_TYPE_PHANTOM_CAMERA, UcaPhantomCameraClass))
#define UCA_IS_PHANTOM_CAMERA_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UCA_TYPE_PHANTOM_CAMERA))
#define UCA_PHANTOM_CAMERA_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UCA_TYPE_PHANTOM_CAMERA, UcaPhantomCameraClass))

#define UCA_PHANTOM_CAMERA_ERROR uca_phantom_camera_error_quark()
typedef enum {
    UCA_PHANTOM_CAMERA_ERROR_INIT,
    UCA_PHANTOM_CAMERA_ERROR_START_RECORDING,
    UCA_PHANTOM_CAMERA_ERROR_STOP_RECORDING,
    UCA_PHANTOM_CAMERA_ERROR_TRIGGER,
    UCA_PHANTOM_CAMERA_ERROR_NEXT_EVENT,
    UCA_PHANTOM_CAMERA_ERROR_NO_DATA,
    UCA_PHANTOM_CAMERA_ERROR_MAYBE_CORRUPTED
} UcaPhantomCameraError;

typedef struct _UcaPhantomCamera           UcaPhantomCamera;
typedef struct _UcaPhantomCameraClass      UcaPhantomCameraClass;
typedef struct _UcaPhantomCameraPrivate    UcaPhantomCameraPrivate;

/**
 * UcaPhantomCamera:
 *
 * Creates #UcaPhantomCamera instances by loading corresponding shared objects. The
 * contents of the #UcaPhantomCamera structure are private and should only be
 * accessed via the provided API.
 */
struct _UcaPhantomCamera {
    /*< private >*/
    UcaCamera parent;

    UcaPhantomCameraPrivate *priv;
};

/**
 * UcaPhantomCameraClass:
 *
 * #UcaPhantomCamera class
 */
struct _UcaPhantomCameraClass {
    /*< private >*/
    UcaCameraClass parent;
};

GType uca_phantom_camera_get_type(void);

UcaPhantomCamera *uca_phantom_camera_new(void);

/*
#if GLIB_CHECK_VERSION(2, 74, 0)

#else
    #define G_DEFINE_ENUM_VALUE(EnumValue, EnumNick) \
    { EnumValue, #EnumValue, EnumNick }

    #define G_DEFINE_ENUM_TYPE(TypeName, type_name, ...) \
    GType \
    type_name ## _get_type (void) { \
    static gsize g_define_type__static = 0; \
    if (g_once_init_enter (&g_define_type__static)) { \
        static const GEnumValue enum_values[] = { \
        __VA_ARGS__ , \
        { 0, NULL, NULL }, \
        }; \
        GType g_define_type = g_enum_register_static (g_intern_static_string (#TypeName), enum_values); \
        g_once_init_leave (&g_define_type__static, g_define_type); \
    } \
    return g_define_type__static; \
    }
#endif

G_DEFINE_ENUM_TYPE (SyncMode, sync_mode,
    G_DEFINE_ENUM_VALUE (SYNC_MODE_FREE_RUN, "internal"),
    G_DEFINE_ENUM_VALUE (SYNC_MODE_FSYNC, "external"),
    G_DEFINE_ENUM_VALUE (SYNC_MODE_IRIG, "irig"),
    G_DEFINE_ENUM_VALUE (SYNC_MODE_VIDEO_FRAME_RATE, "video"),
    G_DEFINE_ENUM_VALUE (SYNC_MODE_TRIGGER, "trigger"))
G_DEFINE_ENUM_TYPE (AcquisitionMode, acquisition_mode,
    G_DEFINE_ENUM_VALUE (ACQUISITION_MODE_STANDARD, "standard"),
    G_DEFINE_ENUM_VALUE (ACQUISITION_MODE_STANDARD_BINNED, "standard-binned"),
    G_DEFINE_ENUM_VALUE (ACQUISITION_MODE_HS, "hs"),
    G_DEFINE_ENUM_VALUE (ACQUISITION_MODE_HS_BINNED, "hs-binned"),
    G_DEFINE_ENUM_VALUE (ACQUISITION_MODE_BRIGHT_FIELD, "bright-field"))
G_DEFINE_ENUM_TYPE (AutoExpMode, aexp_mode,
    G_DEFINE_ENUM_VALUE (AUTO_EXP_MODE_OFF, "off"),
    G_DEFINE_ENUM_VALUE (AUTO_EXP_MODE_AVERAGE, "average"),
    G_DEFINE_ENUM_VALUE (AUTO_EXP_MODE_SPOT, "spot"),
    G_DEFINE_ENUM_VALUE (AUTO_EXP_MODE_CENTER, "center"))
G_DEFINE_ENUM_TYPE (ImageFormat, image_format,
    G_DEFINE_ENUM_VALUE (IMG_8, "8bit"),
    G_DEFINE_ENUM_VALUE (IMG_8R, "8bit-raw"),
    G_DEFINE_ENUM_VALUE (IMG_P16, "16bit"),
    G_DEFINE_ENUM_VALUE (IMG_P16R, "16bit-raw"),
    G_DEFINE_ENUM_VALUE (IMG_P10, "10bit"),
    G_DEFINE_ENUM_VALUE (IMG_P12L, "12bit"))
G_DEFINE_ENUM_TYPE (TimestampFormat, timestamp_format,
    G_DEFINE_ENUM_VALUE (TS_SHORT, "short"),
    G_DEFINE_ENUM_VALUE (TS_SHORT32, "short32"),
    G_DEFINE_ENUM_VALUE (TS_LONG, "long"),
    G_DEFINE_ENUM_VALUE (TS_LONG32, "long32"),
    G_DEFINE_ENUM_VALUE (TS_NONE, "none"))

*/

G_END_DECLS

#endif
