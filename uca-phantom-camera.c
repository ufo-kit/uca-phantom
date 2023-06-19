#include <gio/gio.h>
#include <gmodule.h>
#include <glib-object.h>

#include <uca/uca-camera.h>

#include "uca-phantom-communicate.h"
#include "uca-phantom-camera-re.h"

// SSE(128) instructions AVX(256); library intrisincs 
// UCA UFO SSE 

#include <unistd.h>
#include <stdint.h>
#include <float.h>


#define UCA_PHANTOM_CAMERA_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UCA_TYPE_PHANTOM_CAMERA, UcaPhantomCameraPrivate))


static void uca_phantom_camera_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (UcaPhantomCamera, uca_phantom_camera, UCA_TYPE_CAMERA,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                uca_phantom_camera_initable_iface_init))

GQuark uca_phantom_camera_error_quark () {
    return g_quark_from_static_string("uca-net-camera-error-quark");
}

static gint base_overrideables[] = {
    PROP_NAME,
    PROP_SENSOR_WIDTH,
    PROP_SENSOR_HEIGHT,
    PROP_SENSOR_PIXEL_WIDTH, // PROP_INFO_XMAX
    PROP_SENSOR_PIXEL_HEIGHT, // PROP_INFO_YMAX
    PROP_SENSOR_BITDEPTH, // described by ImageFormatString
    PROP_TRIGGER_SOURCE, // UcaCameraTriggerSource
    PROP_TRIGGER_TYPE, // UcaCameraTriggerType
    PROP_EXPOSURE_TIME, // PROP_DEFC_EXP
    PROP_FRAMES_PER_SECOND, // PROP_DEFC_RATE
    PROP_ROI_X, // PROP_DEFC_META_OX
    PROP_ROI_Y, // PROP_DEFC_META_OY
    PROP_ROI_WIDTH, // PROP_DEFC_META_W
    PROP_ROI_HEIGHT, // PROP_DEFC_META_H
    PROP_ROI_WIDTH_MULTIPLIER, // PROP_INFO_XINC
    PROP_ROI_HEIGHT_MULTIPLIER, // PROP_INFO_YINC
    PROP_HAS_STREAMING,
    PROP_HAS_CAMRAM_RECORDING,
    0,
};

enum {
    PROP_FOCAL_LENGTH = N_BASE_PROPERTIES,
    PROP_APERTURE,
    PROP_EDR_EXP,
    PROP_SHUTTER_OFF,
    PROP_AEXPMODE,
    PROP_AEXPCOMP,
    PROP_NB_POST_TRIGGER_FRAMES,
    PROP_NB_PRE_TRIGGER_FRAMES,
    PROP_CURRENT_CINE,
    PROP_SYNC_MODE,
    PROP_ACQUISITION_MODE,
    PROP_IMAGE_FORMAT,
    PROP_TIMESTAMP_FORMAT,
    PROP_XNETCARD,
    PROP_XENABLED,
    N_PHANTOM_PROPERTIES
};

// Properties
static GParamSpec *uca_phantom_camera_properties[N_PHANTOM_PROPERTIES] = { NULL, };


// Constants - found on https://www.phantomhighspeed.com/products/cameras/ultrahigh4mpx/v2640
const guint sensor_resolution_width = 2048, sensor_resolution_height = 1952;
const gfloat sensor_pixel_width = 13.5, sensor_pixel_height = 13.5; // micrometer 
const gfloat sensor_binned_pixel_width = 27.0, sensor_binned_pixel_height = 27.0; // micrometer 
const gfloat sensor_width = sensor_resolution_width * sensor_pixel_width, 
             sensor_height = sensor_resolution_height * sensor_pixel_height; // micrometer 
const gfloat temp_min = 5.0, temp_max = 50.0; // Celsius

struct _UcaPhantomCameraPrivate {
    GError *construct_error;
    GCancellable *accept;

    // Base class properties + Phantom specific properties
    gchar *name;
    gfloat sensor_width, sensor_height;
    guint max_sensor_resolution_width, max_sensor_resolution_height;
    guint expdead, xinc, yinc;
    gboolean has_streaming, has_camram_recording;
    CaptureSettings settings; // Groups all the main writeable properties

    // Network properties
    gchar *xnetcard;
    gboolean xenabled;
    gboolean connected;


    UcaPhantomCommunicate *communicator;
};

/**
 *
 */
static void
uca_phantom_camera_start_readout (UcaCamera *camera,
                                  GError **error) {
    g_return_if_fail (error == NULL || *error == NULL);
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));

    GError *internal_error = NULL;
    UcaPhantomCameraPrivate *priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    g_print ("Starting readout\n");
    // Launch the readout using the communicator
    if (!uca_phantom_communicate_start_readout (priv->communicator, &internal_error)) {
        g_propagate_error (error, internal_error);
        return;
    }
}

static void
uca_phantom_camera_stop_readout (UcaCamera *camera,
                                 GError **error) {
    g_return_if_fail (error == NULL || *error == NULL);
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));

    g_print ("Stopping readout\n");

    GError *internal_error = NULL;
    UcaPhantomCameraPrivate *priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    // Stop the readout using the communicator
    if (!uca_phantom_communicate_stop_readout (priv->communicator, &internal_error)) {
        g_propagate_error (error, internal_error);
        return;
    }
}


/**
 *
 */
static void
uca_phantom_camera_start_recording (UcaCamera *camera,
                                    GError **error) {
    g_return_if_fail (error == NULL || *error == NULL);
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));

    g_print ("Starting recording\n");

    GError *internal_error = NULL;
    UcaPhantomCameraPrivate *priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    gboolean result = FALSE;
    
    // Connect to the datastream(s)
    if (priv->xenabled) {
        g_print ("Connecting to xdatastream\n");
        result = uca_phantom_communicate_connect_xdatastream (priv->communicator, &internal_error);

        if (result != TRUE && internal_error != NULL) {
            g_propagate_error (error, internal_error);
            return;
        }
    } 

    if (priv->settings.timestamp_format != TS_NONE || !priv->xenabled) {
        result = uca_phantom_communicate_connect_datastream (priv->communicator, &internal_error);

        if (result != TRUE && internal_error != NULL) {
            g_propagate_error (error, internal_error);
            return;
        }
    }
    guint cine = priv->settings.current_cine;
    // Arm the camera
    result = uca_phantom_communicate_arm (priv->communicator, cine, &internal_error);
    if (result != TRUE && internal_error != NULL) {
        g_propagate_error (error, internal_error);
        return;
    }

    gdouble time_to_record = priv->settings.nb_pre_trigger_frames / (gdouble)priv->settings.frames_per_second;
    g_print ("Time to record : %lf\n", time_to_record);
    usleep (time_to_record * 1000000);
}

static void
uca_phantom_camera_stop_recording (UcaCamera *camera,
                                   GError **error) {
    g_return_if_fail (error == NULL || *error == NULL);
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));

    g_print ("Stoping recording\n");

    /*
        Nothing to do, everything is handled automatically by the class destructor
    */
}

/**
 *
 */
static void
uca_phantom_camera_trigger (UcaCamera *camera,
                            GError **error) {
    g_return_if_fail (error == NULL || *error == NULL);
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));

    g_print ("Triggering\n");

    GError *internal_error = NULL;
    UcaPhantomCameraPrivate *priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    gboolean res = uca_phantom_communicate_set_settings (priv->communicator, &priv->settings, &internal_error);
    if (res != TRUE && internal_error != NULL) {
        g_propagate_error (error, internal_error);
        return;
    }

    // Trigger the camera using the communicator
    if (!uca_phantom_communicate_trigger (priv->communicator, &internal_error)) {
        g_propagate_error (error, internal_error);
        return;
    }
    
    // check if the cine is flagged READY !
    // todo : use notify command
    // Current method: estimate the time needed to record ptframes
    // and wait an extra 100ms to be sure :D 
    gdouble time_to_record = priv->settings.nb_post_trigger_frames / (gdouble)priv->settings.frames_per_second;
    g_print ("Time to record : %lf\n", time_to_record);
    usleep (time_to_record * 1000000 + 100000);

    g_print ("img_format : %s\n", ImageFormatSpecs[priv->settings.image_format].format_string);

    guint nb_images = priv->settings.nb_post_trigger_frames + priv->settings.nb_pre_trigger_frames;
    if (!uca_phantom_communicate_request_images (
            priv->communicator, 
            priv->settings.current_cine, 
            nb_images,
            priv->settings.image_format,
            priv->settings.timestamp_format,
            &internal_error)) {
        g_propagate_error (error, internal_error);
        return;
    }
    if (internal_error != NULL) {
        g_print ("Error : %s\n", internal_error->message);
        return;
    }
}

static void
uca_phantom_camera_write (UcaCamera *camera,
                          const gchar *name,
                          gpointer data,
                          gsize size,
                          GError **error) {
    g_return_if_fail (error == NULL || *error == NULL);
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));

    /*
        Not implemented
    */
}

/**
 *
 */
static gboolean
uca_phantom_camera_grab (UcaCamera *camera,
                         gpointer data,
                         GError **error) {   
    UcaPhantomCameraPrivate *priv;
    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    // First request the images using the communicator
    GError *internal_error = NULL;

    // Grab single image!
    if (!uca_phantom_communicate_grab_image (priv->communicator, data, &internal_error)) {
        g_propagate_error (error, internal_error);
        return FALSE;
    }

    return TRUE;
}

/**
 *
 */
static void
uca_phantom_camera_set_property (GObject *object,
                                 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec) {
    UcaPhantomCameraPrivate *priv;
    g_print ("Setting property %d\n", property_id);

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (object);
    UcaPhantomCommunicate *communicator = priv->communicator;

    GError *internal_error = NULL;


    switch (property_id) {
        // Use all properties defined in base_overrideables
        case PROP_NAME:
            g_free (priv->name);
            priv->name = g_strdup (g_value_get_string (value));
            break;
        case PROP_SENSOR_WIDTH:
            // Nothing to do, this is a read-only property
            break;
        case PROP_SENSOR_HEIGHT:
            // Nothing to do, this is a read-only property
            break;
        case PROP_SENSOR_PIXEL_WIDTH:
            // Nothing to do, this is a read-only property
            break;
        case PROP_SENSOR_PIXEL_HEIGHT:
            // Nothing to do, this is a read-only property
            break;
        case PROP_SENSOR_BITDEPTH:
            priv->settings.sensor_bit_depth = g_value_get_uint (value);
            break;
        case PROP_TRIGGER_SOURCE:
            priv->settings.trigger_source = g_value_get_uint (value);
            break;
        case PROP_TRIGGER_TYPE:
            priv->settings.trigger_type = g_value_get_uint (value);
            break;
        case PROP_EXPOSURE_TIME:
            priv->settings.exposure_time = g_value_get_uint (value);
            break;
        case PROP_FRAMES_PER_SECOND:
            g_print ("Setting FPS to %f\n", g_value_get_double (value));
            priv->settings.frames_per_second = g_value_get_double (value);
            break;
        case PROP_ROI_X:
            priv->settings.roi_pixel_x = g_value_get_int (value);
            break;
        case PROP_ROI_Y:
            priv->settings.roi_pixel_y = g_value_get_int (value);
            break;
        case PROP_ROI_WIDTH:
            priv->settings.roi_pixel_width = g_value_get_uint (value);
            break;
        case PROP_ROI_HEIGHT:
            priv->settings.roi_pixel_height = g_value_get_uint (value);
            break;
        case PROP_ROI_WIDTH_MULTIPLIER:
            priv->settings.roi_width_multiplier = g_value_get_uint (value);
            break;
        case PROP_ROI_HEIGHT_MULTIPLIER:
            priv->settings.roi_height_multiplier = g_value_get_uint (value);
            break;
        case PROP_HAS_STREAMING:
            priv->has_streaming = g_value_get_boolean (value);
            break;
        case PROP_HAS_CAMRAM_RECORDING:
            priv->has_camram_recording = g_value_get_boolean (value);
            break;
        // End of base_overrideables
        case PROP_FOCAL_LENGTH:
            priv->settings.focal_length = g_value_get_float (value);
            break;
        case PROP_APERTURE:
            priv->settings.aperture = g_value_get_float (value);
            break;
        case PROP_EDR_EXP:
            priv->settings.edr_exp = g_value_get_uint (value);
            break;
        case PROP_SHUTTER_OFF:
            priv->settings.shutter_off = g_value_get_uint (value);
            break;
        case PROP_AEXPMODE:
            priv->settings.aexpmode = g_value_get_uint (value);
            break;
        case PROP_AEXPCOMP:
            priv->settings.aexpcomp = g_value_get_float (value);
            break;
        case PROP_NB_POST_TRIGGER_FRAMES:
            g_print ("Setting nb_post_trigger_frames to %d\n", g_value_get_uint (value));
            priv->settings.nb_post_trigger_frames = g_value_get_uint (value);
            break;
        case PROP_NB_PRE_TRIGGER_FRAMES:
            g_print ("Setting nb_pre_trigger_frames to %d\n", g_value_get_uint (value));
            priv->settings.nb_pre_trigger_frames = g_value_get_uint (value);
            break;
        case PROP_CURRENT_CINE:
            priv->settings.current_cine = g_value_get_uint (value);
            break;
        case PROP_SYNC_MODE:
            priv->settings.sync_mode = g_value_get_uint (value);
            break;
        case PROP_ACQUISITION_MODE:
            priv->settings.acquisition_mode = g_value_get_uint (value);
            break;
        case PROP_IMAGE_FORMAT:
            priv->settings.image_format = g_value_get_uint (value);
            break;
        case PROP_TIMESTAMP_FORMAT:
            priv->settings.timestamp_format = g_value_get_uint (value);
            break;
        case PROP_XNETCARD:
            g_print ("Setting xnetcard to %s\n", g_value_get_string (value));
            g_free (priv->xnetcard);
            priv->xnetcard = g_strdup (g_value_get_string (value));
            g_object_set (priv->communicator, "xnetcard", priv->xnetcard, NULL);
            break;
        case PROP_XENABLED:
            priv->xenabled = g_value_get_boolean (value);
            g_object_set (priv->communicator, "xenabled", priv->xenabled, NULL);

            break;
        default:
            // Warn if the property is not defined in this class
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;       
    }

    // Send the settings to the phantom
    gboolean res = uca_phantom_communicate_set_settings (priv->communicator, &(priv->settings), &internal_error);
    if (!res || internal_error != NULL) {
        g_warning ("Failed to set capture settings: %s", internal_error->message);
        g_error_free (internal_error);
    }
}

/**
 *
 */
static void
uca_phantom_camera_get_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec) {
    UcaPhantomCameraPrivate *priv;
    g_print ("Getting property %d\n", property_id);

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (object);

    GError *internal_error = NULL;


    switch (property_id) {
        // Use all properties defined in base_overrideables
        case PROP_NAME:
            g_value_set_string (value, priv->name);
            break;
        case PROP_SENSOR_WIDTH:
            g_value_set_uint (value, priv->sensor_width);
            break;
        case PROP_SENSOR_HEIGHT:
            g_value_set_uint (value, priv->sensor_height);
            break;
        case PROP_SENSOR_PIXEL_WIDTH:
            g_value_set_uint (value, priv->settings.sensor_pixel_width);
            break;
        case PROP_SENSOR_PIXEL_HEIGHT:
            g_value_set_uint (value, priv->settings.sensor_pixel_height);
            break;
        case PROP_SENSOR_BITDEPTH:
            g_value_set_uint (value, priv->settings.sensor_bit_depth);
            break;
        case PROP_TRIGGER_SOURCE:
            g_value_set_uint (value, priv->settings.trigger_source);
            break;
        case PROP_TRIGGER_TYPE:
            g_value_set_uint (value, priv->settings.trigger_type);
            break;
        case PROP_EXPOSURE_TIME:
            g_value_set_uint (value, priv->settings.exposure_time);
            break;
        case PROP_FRAMES_PER_SECOND:
            g_print ("Getting FPS to %f\n", priv->settings.frames_per_second);
            g_value_set_double (value, priv->settings.frames_per_second);

            break;
        case PROP_ROI_X:
            g_value_set_int (value, priv->settings.roi_pixel_x);
            break;
        case PROP_ROI_Y:
            g_value_set_int (value, priv->settings.roi_pixel_y);
            break;
        case PROP_ROI_WIDTH:
            g_value_set_uint (value, priv->settings.roi_pixel_width);
            break;
        case PROP_ROI_HEIGHT:
            g_value_set_uint (value, priv->settings.roi_pixel_height);
            break;
        case PROP_ROI_WIDTH_MULTIPLIER:
            g_value_set_uint (value, priv->settings.roi_width_multiplier);
            break;
        case PROP_ROI_HEIGHT_MULTIPLIER:
            g_value_set_uint (value, priv->settings.roi_height_multiplier);
            break;
        case PROP_HAS_STREAMING:
            g_value_set_boolean (value, priv->has_streaming);
            break;
        case PROP_HAS_CAMRAM_RECORDING:
            g_value_set_boolean (value, priv->has_camram_recording);
            break;
        // End of base_overrideables
        case PROP_FOCAL_LENGTH:
            g_value_set_float (value, priv->settings.focal_length);
            break;
        case PROP_APERTURE:
            g_value_set_float (value, priv->settings.aperture);
            break;
        case PROP_EDR_EXP:
            g_value_set_uint (value, priv->settings.edr_exp);
            break;
        case PROP_SHUTTER_OFF:
            g_value_set_uint (value, priv->settings.shutter_off);
            break;
        case PROP_AEXPMODE:
            g_value_set_uint (value, priv->settings.aexpmode);
            break;
        case PROP_AEXPCOMP:
            g_value_set_float (value, priv->settings.aexpcomp);
            break;
        case PROP_NB_POST_TRIGGER_FRAMES:
            g_value_set_uint (value, priv->settings.nb_post_trigger_frames);
            break;
        case PROP_NB_PRE_TRIGGER_FRAMES:
            g_value_set_uint (value, priv->settings.nb_pre_trigger_frames);
            break;
        case PROP_CURRENT_CINE:
            g_value_set_uint (value, priv->settings.current_cine);
            break;
        case PROP_SYNC_MODE:
            g_value_set_uint (value, priv->settings.sync_mode);
            break;
        case PROP_ACQUISITION_MODE:
            g_value_set_uint (value, priv->settings.acquisition_mode);
            break;
        case PROP_IMAGE_FORMAT:
            g_value_set_uint (value, priv->settings.image_format);
            break;
        case PROP_TIMESTAMP_FORMAT:
            g_value_set_uint (value, priv->settings.timestamp_format);
            break;
        case PROP_XNETCARD:
            g_value_set_string (value, priv->xnetcard);
            break;
        case PROP_XENABLED:
            g_value_set_boolean (value, priv->xenabled);
            break;
        default:
            // Warn if the property is not defined in this class
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;       
    }
}
static void
uca_phantom_camera_dispose (GObject *object) {
    UcaPhantomCameraPrivate *priv;

    g_print ("Disposing phantom camera\n") ;

    // g_free (priv->name);

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (object);

    G_OBJECT_CLASS (uca_phantom_camera_parent_class)->dispose (object);
}

static void
uca_phantom_camera_finalize (GObject *object) {
    UcaPhantomCameraPrivate *priv;
    g_print ("Finalizing phantom camera\n") ;
    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (object);


    G_OBJECT_CLASS (uca_phantom_camera_parent_class)->finalize (object);

    // All network related objects are automatically destroyed by the communicator
    if (priv->communicator) {
        g_object_unref (priv->communicator);
        priv->communicator = NULL;
    }
}

static gboolean
ufo_net_camera_initable_init (GInitable *initable,
                              GCancellable *cancellable,
                              GError **error) {
    UcaPhantomCamera *camera;
    UcaPhantomCameraPrivate *priv;
    GError *internal_error = NULL;

    g_return_val_if_fail (UCA_IS_PHANTOM_CAMERA (initable), FALSE);

    g_print ("Initializing phantom camera (INITABLE)\n") ;

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

    // Create a phantom communicator object
    priv->communicator = uca_phantom_communicate_new ();

    g_print ("Setting communicator properties\n") ;

    g_object_set (priv->communicator,
                    "xnetcard", priv->xnetcard,
                    "xenabled", priv->xenabled,
                    NULL);

    // Connect the control streamm to the camera
    if (!uca_phantom_communicate_connect_controlstream(priv->communicator, &internal_error)) {
        g_propagate_error (error, internal_error);
        return FALSE;
    }
    else {
        priv->connected = TRUE;
    }


    GValue value = G_VALUE_INIT;
    if (!uca_phantom_communicate_get_variable (priv->communicator, UNIT_INFO_XMAX, &value, &internal_error)) {
        g_propagate_error (error, internal_error);
        return FALSE;
    }
    priv->max_sensor_resolution_width = g_value_get_uint (&value);
    g_value_unset (&value);

    if (!uca_phantom_communicate_get_variable (priv->communicator, UNIT_INFO_YMAX, &value, &internal_error)) {
        g_propagate_error (error, internal_error);
        return FALSE;
    }
    priv->max_sensor_resolution_height = g_value_get_uint (&value);
    g_value_unset (&value);

    if (!uca_phantom_communicate_get_variable (priv->communicator, UNIT_INFO_EXPDEAD, &value, &internal_error)) {
        g_propagate_error (error, internal_error);
        return FALSE;
    }
    priv->expdead = g_value_get_uint (&value);
    g_value_unset (&value);

    if (!uca_phantom_communicate_get_variable (priv->communicator, UNIT_INFO_XINC, &value, &internal_error)) {
        g_propagate_error (error, internal_error);
        return FALSE;
    }
    priv->xinc = g_value_get_uint (&value);
    g_value_unset (&value);

    if (!uca_phantom_communicate_get_variable (priv->communicator, UNIT_INFO_YINC, &value, &internal_error)) {
        g_propagate_error (error, internal_error);
        return FALSE;
    }
    priv->yinc = g_value_get_uint (&value);
    g_value_unset (&value);


    // Init base class properties
    priv->name = g_strdup ("Phantom Camera");
    priv->sensor_width = sensor_width;
    priv->sensor_height = sensor_height;
    priv->has_streaming = FALSE;
    priv->has_camram_recording = FALSE;
    priv->settings = (CaptureSettings){
        .sync_mode = SYNC_MODE_VIDEO_FRAME_RATE,
        .acquisition_mode = ACQUISITION_MODE_HS_BINNED,
        .image_format = IMG_P12L,
        .timestamp_format = TS_NONE,
        .sensor_pixel_width = sensor_pixel_width,
        .sensor_pixel_height = sensor_pixel_height,
        .sensor_bit_depth = ImageFormatSpecs[IMG_P12L].bit_depth,
        .trigger_source = UCA_CAMERA_TRIGGER_SOURCE_SOFTWARE,
        .trigger_type = UCA_CAMERA_TRIGGER_TYPE_EDGE,
        .frames_per_second = 1000.0,
        .exposure_time = 459,
        .roi_pixel_x = 0,
        .roi_pixel_y = 0,
        .roi_pixel_width = priv->max_sensor_resolution_width,
        .roi_pixel_height = priv->max_sensor_resolution_height,
        .roi_width_multiplier = priv->xinc ,
        .roi_height_multiplier = priv->yinc ,
        .focal_length = 0.0,
        .aperture = 0.0,
        .edr_exp = 459,
        .shutter_off = 1,
        .aexpmode = AUTO_EXP_MODE_AVERAGE,
        .aexpcomp = 0.0,
        .nb_post_trigger_frames = 0,
        .nb_pre_trigger_frames = 0,
        .current_cine = 1,
    };

    g_print ("Phantom camera initialized\n") ;

    return TRUE;
}

static void
uca_phantom_camera_constructed (GObject *object) {

}

static void
uca_phantom_camera_initable_iface_init (GInitableIface *iface) {
    iface->init = ufo_net_camera_initable_init;
}

/**
 * 
 */
static void
uca_phantom_camera_class_init (UcaPhantomCameraClass *klass) {    
    GObjectClass *oclass = G_OBJECT_CLASS (klass);
    UcaCameraClass *camera_class = UCA_CAMERA_CLASS (klass);

    g_print ("Initializing phantom camera class\n") ;

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


    // Add the phantom specific unit variables as properties
    uca_phantom_camera_properties[PROP_FOCAL_LENGTH] =
        g_param_spec_double ("focal-length",
                             "Focal length",
                             "Focal length",
                             0, G_MAXDOUBLE, 0,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    uca_phantom_camera_properties[PROP_APERTURE] =
        g_param_spec_double ("aperture",
                             "Aperture",
                             "Aperture",
                             0, G_MAXDOUBLE, 0,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
                             
    uca_phantom_camera_properties[PROP_EDR_EXP] =
        g_param_spec_double ("edrexp",
                             "EDR exposure time",
                             "EDR exposure time",
                             0, G_MAXDOUBLE, 0,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    uca_phantom_camera_properties[PROP_SHUTTER_OFF] =
        g_param_spec_boolean ("shutteroff",
                              "Shutter off",
                              "Shutter off",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    uca_phantom_camera_properties[PROP_AEXPMODE] =
        g_param_spec_uint ("aexpmode",
                            "Auto exposure mode",
                            "Auto exposure mode",
                            AUTO_EXP_MODE_OFF, AUTO_EXP_MODE_CENTER, AUTO_EXP_MODE_CENTER,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    uca_phantom_camera_properties[PROP_AEXPCOMP] =
        g_param_spec_double ("aexpcomp",
                            "Auto exposure compensation",
                            "Auto exposure compensation",
                            0, G_MAXDOUBLE, 0,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    uca_phantom_camera_properties[PROP_NB_POST_TRIGGER_FRAMES] =
        g_param_spec_uint ("postframes",
                           "Number of post trigger frames",
                           "Number of post trigger frames",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    uca_phantom_camera_properties[PROP_NB_PRE_TRIGGER_FRAMES] =
        g_param_spec_uint ("preframes",
                           "Number of pre trigger frames",
                           "Number of pre trigger frames",
                           0, G_MAXUINT, 1,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    uca_phantom_camera_properties[PROP_CURRENT_CINE] =
        g_param_spec_uint ("cine",
                           "Current cine number",
                           "Current cine number",
                           0, G_MAXUINT, 1,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    uca_phantom_camera_properties[PROP_SYNC_MODE] =
        g_param_spec_uint ("syncmode",
                           "Sync mode",
                           "Sync mode",
                           SYNC_MODE_FREE_RUN,
                           SYNC_MODE_VIDEO_FRAME_RATE,
                           SYNC_MODE_VIDEO_FRAME_RATE,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    uca_phantom_camera_properties[PROP_ACQUISITION_MODE] =
        g_param_spec_uint ("acqmode",
                           "Acquisition mode",
                           "Acquisition mode",
                           ACQUISITION_MODE_STANDARD,
                           ACQUISITION_MODE_BRIGHT_FIELD,
                           ACQUISITION_MODE_STANDARD,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    uca_phantom_camera_properties[PROP_IMAGE_FORMAT] =
        g_param_spec_uint ("imgformat",
                           "Image format",
                           "Image format",
                            IMG_8, IMG_P12L, IMG_P12L,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    uca_phantom_camera_properties[PROP_TIMESTAMP_FORMAT] =
        g_param_spec_uint ("tsformat",
                             "Timestamp",
                             "Timestamp",
                             TS_SHORT, TS_NONE, TS_NONE,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    uca_phantom_camera_properties[PROP_XNETCARD] =
        g_param_spec_string ("xnetcard",
                             "10 Gb NIC",
                             "10 Gb NIC",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    uca_phantom_camera_properties[PROP_XENABLED] =
        g_param_spec_boolean ("xenabled",
                              "X enabled",
                              "X enabled",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    // Implement the base class properties
    for (guint i = 0; base_overrideables[i] != 0; i++) {
        g_object_class_override_property (oclass, base_overrideables[i], uca_camera_props[base_overrideables[i]]);
    }
    for (guint id = N_BASE_PROPERTIES; id < N_PHANTOM_PROPERTIES; id++) {
        g_object_class_install_property (oclass, id, uca_phantom_camera_properties[id]);
    }

    g_type_class_add_private (klass, sizeof(UcaPhantomCameraPrivate));
}



/**
 *
 */
static void
uca_phantom_camera_init (UcaPhantomCamera *self) {
    
    UcaPhantomCameraPrivate *priv;
    self->priv = priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (self);

    g_print ("Initializing phantom camera instance\n") ;

}

G_MODULE_EXPORT GType
camera_plugin_get_type (void) {
    return UCA_TYPE_PHANTOM_CAMERA;
}

UcaPhantomCamera *uca_phantom_camera_new (void) {
    g_print ("Creating phantom camera instance\n") ;
    return UCA_PHANTOM_CAMERA (
        g_object_new (
            UCA_TYPE_PHANTOM_CAMERA, NULL));
}