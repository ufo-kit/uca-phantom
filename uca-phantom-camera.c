#include "uca-phantom-camera.h"

#include <gio/gio.h>
#include <gmodule.h>
#include <glib-object.h>

#include <unistd.h>
#include <stdint.h>
#include <float.h>
#include <stdlib.h>

#include <time.h>


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
    PROP_SENSOR_PIXEL_WIDTH,
    PROP_SENSOR_PIXEL_HEIGHT,
    PROP_SENSOR_BITDEPTH,
    PROP_SENSOR_HORIZONTAL_BINNING,
    PROP_SENSOR_VERTICAL_BINNING,
    PROP_TRIGGER_SOURCE,
    PROP_TRIGGER_TYPE,
    PROP_EXPOSURE_TIME,
    PROP_FRAMES_PER_SECOND,
    PROP_ROI_X,
    PROP_ROI_Y,
    PROP_ROI_WIDTH,
    PROP_ROI_HEIGHT,
    PROP_ROI_WIDTH_MULTIPLIER,
    PROP_ROI_HEIGHT_MULTIPLIER,
    PROP_HAS_STREAMING,
    PROP_HAS_CAMRAM_RECORDING,
    PROP_RECORDED_FRAMES,
    PROP_BUFFERED,
    0
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
    PROP_SYNC_MODE,
    PROP_ACQUISITION_MODE,
    PROP_IMAGE_FORMAT,
    PROP_TIMESTAMP_FORMAT,
    PROP_XNETCARD,
    PROP_XENABLED,
    PROP_SENSOR_PHYSICAL_HEIGHT,
    PROP_SENSOR_PHYSICAL_WIDTH,
    PROP_MAX_SENSOR_RESOLUTION_WIDTH,
    PROP_MAX_SENSOR_RESOLUTION_HEIGHT,
    PROP_XINC,
    PROP_YINC,
    PROP_INTERNAL_MEMORY_SIZE,
    N_PHANTOM_PROPERTIES
};

// Properties
static GParamSpec *uca_phantom_camera_properties[N_PHANTOM_PROPERTIES] = { NULL, };


// Constants - found on https://www.phantomhighspeed.com/products/cameras/ultrahigh4mpx/v2640
const gdouble sensor_pixel_width = 13.5e-6, sensor_pixel_height = 13.5e-6; // micrometer 
const gdouble sensor_binned_pixel_width = 27.0e-6, sensor_binned_pixel_height = 27.0e-6; // micrometer 
const gfloat temp_min = 5.0, temp_max = 50.0; // Celsius

struct _UcaPhantomCameraPrivate {
    GError *construct_error;
    GCancellable *accept;
    gboolean constructed;

    // Read only properties
    gchar *name;
    gdouble sensor_pixel_width, sensor_pixel_height; // in meters
    guint max_sensor_resolution_width, max_sensor_resolution_height; // in pixels
    gdouble sensor_physical_width, sensor_physical_height; // in meters
    guint expdead, xinc, yinc; // in ns
    guint internal_memory_size; // in MBytes

    // Camera properties
    guint sensor_resolution_width, sensor_resolution_height; // in pixels
    guint roi_x0, roi_y0, roi_width, roi_height, roi_width_multiplier, roi_height_multiplier;
    guint numbuffers; // nb cines
    gboolean has_streaming, has_camram_recording;
    gboolean buffered;
    CaptureSettings settings; // Groups all the main writeable properties
    

    // Network properties
    gchar *xnetcard;
    gboolean xenabled;
    gboolean x_data_connected, data_connected, control_connected;

    // Hack for tracking the last cine used
    gboolean recording;
    guint *cine_tracker;
    GCond cine_cond;
    GMutex cine_mutex;
    gint *finished_recording;

    UcaPhantomCommunicate *communicator;
};


static void uca_phantom_camera_trigger (UcaCamera *camera, GError **error);

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

    gboolean result = FALSE;

    if ((priv->settings.timestamp_format != TS_NONE || !priv->buffered) && !priv->data_connected) {
        g_debug ("Connecting to the datastream\n");
        result = uca_phantom_communicate_connect_datastream (priv->communicator, &internal_error);
        g_debug ("Connected to the datastream\n");

        if (result != TRUE && internal_error != NULL) {
            g_propagate_error (error, internal_error);
            return;
        }

        priv->data_connected = TRUE;
    }

    // Connect to the x datastream
    if (priv->xenabled && !priv->x_data_connected) {
        g_debug ("Connecting to the x datastream\n");
        result = uca_phantom_communicate_connect_xdatastream (priv->communicator, &internal_error);

        if (result != TRUE && internal_error != NULL) {
            g_propagate_error (error, internal_error);
            return;
        }
        priv->x_data_connected = TRUE;
    } 

    // Launch the readout using the communicator
    g_debug ("Starting readout function\n");
    result = uca_phantom_communicate_start_readout (priv->communicator, !priv->buffered, &(priv->settings), &internal_error);
    if (result != TRUE && internal_error != NULL) {
        g_propagate_error (error, internal_error);
        return;
    }
}

static void
uca_phantom_camera_stop_readout (UcaCamera *camera,
                                 GError **error) {
    g_return_if_fail (error == NULL || *error == NULL);
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));

    GError *internal_error = NULL;
    UcaPhantomCameraPrivate *priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    // Stop the readout using the communicator
    gboolean res = uca_phantom_communicate_stop_readout (priv->communicator, &internal_error);
    if (res != TRUE && internal_error != NULL) {
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

    GError *internal_error = NULL;
    UcaPhantomCameraPrivate *priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    gboolean result = FALSE;

    // Start readout automatically
    uca_phantom_camera_start_readout (camera, &internal_error);
    if (internal_error != NULL) {
        g_propagate_error (error, internal_error);
        return;
    }    

    if (!priv->recording)
        priv->recording = TRUE;
}

static void
uca_phantom_camera_stop_recording (UcaCamera *camera,
                                   GError **error) {
    g_return_if_fail (error == NULL || *error == NULL);
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));

    UcaPhantomCameraPrivate *priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    GError *internal_error = NULL;

    // Temp read timestamps
    for (int i = 0; i < 2000; i++) {
        guint64 time;
        gboolean res = uca_phantom_communicate_grab_timestamp(priv->communicator, &time, 0, error);
        if (!res) {
            g_print ("Uh oh\n");
        }
    }

    gboolean res = uca_phantom_communicate_disarm (priv->communicator, &internal_error);
    if (res != TRUE && internal_error != NULL) {
        g_propagate_error (error, internal_error);
        return;
    }
    
    priv->recording = FALSE;

    uca_phantom_camera_stop_readout (camera, &internal_error);
    if (internal_error != NULL) {
        g_propagate_error (error, internal_error);
        return;
    }
}

/**
 *
 */
static void
uca_phantom_camera_trigger (UcaCamera *camera,
                            GError **error) {
    g_return_if_fail (error == NULL || *error == NULL);
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));
    UcaPhantomCameraPrivate *priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);
    g_return_if_fail (priv->recording);

    GError *internal_error = NULL;
    gdouble time_to_record = 0;
    gboolean res = FALSE;

    CaptureSettings settings = priv->settings;
    gboolean buffered;
    g_object_get(camera, "buffered", &buffered, NULL);

    if (settings.trigger_source == UCA_CAMERA_TRIGGER_SOURCE_AUTO){
        g_print ("AUto Trigger\n");
    }

    if (buffered) {
        settings.current_cine = priv->settings.current_cine;
    }
    else {
        settings.current_cine = -1;
    }

    // print the current cine
    time_to_record = settings.nb_pre_trigger_frames / (gdouble)settings.frames_per_second;

    // Arm the camera
    res = uca_phantom_communicate_arm (priv->communicator, settings.current_cine, &internal_error);
    if (res != TRUE && internal_error != NULL) {
        g_propagate_error (error, internal_error);
        return;
    }

    // Wait and let the camera fill in the buffer with enough pre trigger frames 
    g_usleep (time_to_record * G_USEC_PER_SEC);

    gboolean ready_to_trigger = FALSE;
    GValue flags = G_VALUE_INIT;
    const gchar *flags_str = NULL;
    gchar *found = NULL;

    while (!ready_to_trigger) {
        res = uca_phantom_communicate_get_variable (priv->communicator, UNIT_CT_STATE, settings.current_cine, &flags, &internal_error);
        if (res != TRUE && internal_error != NULL) {
            g_propagate_error (error, internal_error);
            return;
        }
        flags_str = g_value_get_string (&flags);
        if (flags_str == NULL) {
            g_warning ("Failed to get flags string");
            continue;
        }
        found = g_strrstr (flags_str, "ABL");
        if (found != NULL) {
            ready_to_trigger = TRUE;
        }
        g_value_unset (&flags);
    }
    found = NULL;

    // Trigger the camera using the communicator
    res = uca_phantom_communicate_trigger (priv->communicator, &internal_error);
    if (res != TRUE && internal_error != NULL) {
        g_propagate_error (error, internal_error);
        return;
    }
    
    // check if the cine is flagged READY !
    // Same thing, estimate the time to record the post trigger frames
    time_to_record = settings.nb_post_trigger_frames / (gdouble)settings.frames_per_second;

    g_usleep (time_to_record * G_USEC_PER_SEC);

    gboolean ready_to_request = FALSE;
    GValue flags2 = G_VALUE_INIT;

    gboolean extra_loop = FALSE;

    do {
        if (ready_to_request) {
            extra_loop = TRUE;
        }

        res = uca_phantom_communicate_get_variable (priv->communicator, UNIT_CT_STATE, settings.current_cine, &flags2, &internal_error);
        if (res != TRUE && internal_error != NULL) {
            g_propagate_error (error, internal_error);
        }
        flags_str = g_value_get_string (&flags2);
        if (flags_str == NULL) {
            g_warning ("Failed to get flags string");
            continue;
        }
        found = g_strrstr (flags_str, "STR");
        if (found != NULL) {
            ready_to_request = TRUE;
        }

        
        
        g_value_unset (&flags2);
    } while (!ready_to_request && !extra_loop);

    res = !uca_phantom_communicate_request_images (
            priv->communicator, 
            settings,
            &internal_error);
    if (res != TRUE && internal_error != NULL) {
        g_propagate_error (error, internal_error);
        return;
    }

    if (settings.trigger_source == UCA_CAMERA_TRIGGER_SOURCE_SOFTWARE){
        // // Wait until we've grabbed enough images to overwrite the new cine
        // g_mutex_lock (&priv->cine_mutex);
        // guint ni = priv->settings.nb_post_trigger_frames + priv->settings.nb_pre_trigger_frames;
        // while (priv->cine_tracker[priv->settings.current_cine] < ni ) {
        //     g_cond_wait (&priv->cine_cond, &priv->cine_mutex);
        // }
        // g_cond_wait (&priv->cine_cond, &priv->cine_mutex);
        priv->settings.current_cine += 1;
        priv->settings.current_cine %= priv->numbuffers;
    }
    // g_print ("Trigger end: Saving in cine: %d\n", priv->settings.current_cine);
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
uca_phantom_camera_grab_live (UcaCamera *camera,
                         gpointer data,
                         GError **error) {
    UcaPhantomCameraPrivate *priv;
    static gboolean first = TRUE;
    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    if (first) {
        // Arm the camera
        gboolean res = uca_phantom_communicate_arm (priv->communicator, -1, error);
        if (res != TRUE) {
            return;
        }

        // Trigger the camera using the communicator

        // Wait for the camera to be ready
        gdouble time_to_record = 1 / (gdouble)priv->settings.frames_per_second;
        g_print ("Time to record postframes: %f\n", time_to_record);

        g_usleep (time_to_record * G_USEC_PER_SEC);

        res = uca_phantom_communicate_trigger (priv->communicator, error);
        
        first = FALSE;
    }    

    return uca_phantom_communicate_grab_live_image (priv->communicator, data, priv->settings, error);
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

    UcaCameraTriggerSource trigger_source;
    g_object_get(camera, "trigger-source", &(priv->settings.trigger_source), NULL);

    if (!priv->buffered) {
        // grab a live image without saving in a cine
        g_print ("Grabbing a live image\n");
        return uca_phantom_camera_grab_live (camera, data, error);
    }

    if (priv->settings.trigger_source == UCA_CAMERA_TRIGGER_SOURCE_AUTO){
        uca_phantom_camera_trigger(camera, error);
    }

    // g_print ("ReadGrabbing an image\n");

    if (!uca_phantom_communicate_grab_image (priv->communicator, data, error)) {
        return FALSE;
    }
    priv->cine_tracker[priv->settings.current_cine] += 1;
    // if (priv->cine_tracker[priv->settings.current_cine] == priv->settings.nb_post_trigger_frames + priv->settings.nb_pre_trigger_frames) {
    //     g_cond_signal (&priv->cine_cond);
    // }
  
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

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (object);
    UcaPhantomCommunicate *communicator = priv->communicator;

    GError *internal_error = NULL;
    gchar* resolution = NULL;
    gboolean res = TRUE;

    switch (property_id) {
        // Use all properties defined in base_overrideables
        case PROP_SENSOR_PIXEL_WIDTH: // Nothing to do, this is a read-only property
        case PROP_SENSOR_PIXEL_HEIGHT: // Nothing to do, this is a read-only property
        case PROP_ROI_WIDTH_MULTIPLIER: // Nothing to do, this is a read-only property
        case PROP_ROI_HEIGHT_MULTIPLIER: // Nothing to do, this is a read-only property
        case PROP_SENSOR_PHYSICAL_WIDTH: // Nothing to do, this is a read-only property
        case PROP_SENSOR_PHYSICAL_HEIGHT: // Nothing to do, this is a read-only property
        case PROP_MAX_SENSOR_RESOLUTION_WIDTH: // Nothing to do, this is a read-only property
        case PROP_MAX_SENSOR_RESOLUTION_HEIGHT: // Nothing to do, this is a read-only property
        case PROP_XINC: // Nothing to do, this is a read-only property
        case PROP_YINC: // Nothing to do, this is a read-only property
        case PROP_INTERNAL_MEMORY_SIZE: // Nothing to do, this is a read-only property
        case PROP_EDR_EXP: // Nothing to do, this is a read-only property
        case PROP_HAS_STREAMING: // Nothing to do, this is a read-only property
        case PROP_HAS_CAMRAM_RECORDING:
            // Nothing to do, this is a read-only property
            break;
        case PROP_NAME:
            g_free (priv->name);
            priv->name = g_strdup (g_value_get_string (value));
            break;
        case PROP_SENSOR_WIDTH:
            priv->settings.sensor_width = g_value_get_uint (value);
        case PROP_SENSOR_HEIGHT:
            priv->settings.sensor_height = g_value_get_uint (value);
            resolution = g_strdup_printf (
                "%dx%d", priv->settings.sensor_width, priv->settings.sensor_height);
            if (priv->control_connected){
                res = uca_phantom_communicate_set_variable(communicator, UNIT_DEFC_RES, resolution, &internal_error);
            }
            g_free (resolution);
            break;
        case PROP_SENSOR_BITDEPTH:
            priv->settings.sensor_bit_depth = g_value_get_uint (value);
            // Image format requested on trigger
            break;
        case PROP_TRIGGER_SOURCE:
            priv->settings.trigger_source = g_value_get_enum (value);
            // TODO
            break;
        case PROP_TRIGGER_TYPE:
            priv->settings.trigger_type = g_value_get_uint (value);
            gchar* trigger_type = g_strdup_printf("%u", priv->settings.trigger_type);
            if (priv->control_connected)
                res = uca_phantom_communicate_set_variable(communicator, UNIT_CAM_TRIGPOL, trigger_type, &internal_error);
            g_free(trigger_type);
            break;
        case PROP_EXPOSURE_TIME:
            priv->settings.exposure_time = g_value_get_double (value);
            g_print ("Setting exposure to %f\n", priv->settings.exposure_time);
            gchar* exposure = g_strdup_printf("%d", (guint)(priv->settings.exposure_time));
            g_print ("Setting exposure to %s\n", exposure);
            if (priv->control_connected)
                res = uca_phantom_communicate_set_variable(communicator, UNIT_DEFC_EXP, exposure, &internal_error);
            g_free(exposure);
            break;
        case PROP_FRAMES_PER_SECOND:
            priv->settings.frames_per_second = g_value_get_double (value);
            gchar* fps = g_strdup_printf("%f", priv->settings.frames_per_second);
            if (priv->control_connected)
                res = uca_phantom_communicate_set_variable(communicator, UNIT_DEFC_RATE, fps, &internal_error);
            g_free (fps);
            break;
        case PROP_ROI_X:
            priv->settings.roi_x0 = g_value_get_int (value);
            gchar* roi_x = g_strdup_printf("%d", priv->settings.roi_x0);
            if (priv->control_connected)
                res = uca_phantom_communicate_set_variable(communicator, UNIT_DEFC_META_OX, roi_x, &internal_error);
            g_free (roi_x);
            break;
        case PROP_ROI_Y:
            priv->settings.roi_y0 = g_value_get_int (value);
            gchar* roi_y = g_strdup_printf("%d", priv->settings.roi_y0);
            if (priv->control_connected)
                res = uca_phantom_communicate_set_variable(communicator, UNIT_DEFC_META_OY, roi_y, &internal_error);
            g_free (roi_y);
            break;
        case PROP_ROI_WIDTH:
            priv->settings.roi_width = g_value_get_uint (value);
        case PROP_ROI_HEIGHT:
            priv->settings.roi_height = g_value_get_uint (value);
            resolution = g_strdup_printf (
                "%dx%d", priv->settings.roi_width, priv->settings.roi_height);
            if (priv->control_connected)
                res = uca_phantom_communicate_set_variable(communicator, UNIT_DEFC_RES, resolution, &internal_error);
            g_free (resolution);
            break;
        /* End of base_overrideables */

        case PROP_FOCAL_LENGTH:
            priv->settings.focal_length = g_value_get_float (value);
            // Currently not supported by Phantom V1610
            break;
        case PROP_APERTURE:
            priv->settings.aperture = g_value_get_float (value);
            // Currently not supported by Phantom V1610
            break;
        case PROP_SHUTTER_OFF:
            priv->settings.shutter_off = g_value_get_boolean (value);
            gchar* shutter_off = g_strdup_printf("%d", priv->settings.shutter_off);
            if (priv->control_connected)
                res = uca_phantom_communicate_set_variable(communicator, UNIT_DEFC_SHOFF, shutter_off, &internal_error);
            g_free(shutter_off);
            break;
        case PROP_AEXPMODE:
            priv->settings.aexpmode = g_value_get_uint (value);
            gchar* aexpmode = g_strdup_printf("%d", priv->settings.aexpmode);
            if (priv->control_connected)
                res = uca_phantom_communicate_set_variable(communicator, UNIT_DEFC_AEXPMODE, aexpmode, &internal_error);
            g_free(aexpmode);
            break;
        case PROP_AEXPCOMP:
            priv->settings.aexpcomp = g_value_get_float (value);
            gchar* aexpcomp = g_strdup_printf("%f", priv->settings.aexpcomp);
            if (priv->control_connected)
                res = uca_phantom_communicate_set_variable(communicator, UNIT_DEFC_AEXPCOMP, aexpcomp, &internal_error);
            g_free(aexpcomp);
            break;
        case PROP_NB_POST_TRIGGER_FRAMES:
            priv->settings.nb_post_trigger_frames = g_value_get_uint (value);
            gchar* nb_post_trigger_frames = g_strdup_printf("%d", priv->settings.nb_post_trigger_frames+1);
            if (priv->control_connected)
                res = uca_phantom_communicate_set_variable(communicator, UNIT_DEFC_PTFRAMES, nb_post_trigger_frames, &internal_error);
            g_free(nb_post_trigger_frames);
            break;
        case PROP_NB_PRE_TRIGGER_FRAMES:
            priv->settings.nb_pre_trigger_frames = g_value_get_uint (value);
            // Requested when triggered
            break;
        case PROP_SYNC_MODE:
            priv->settings.sync_mode = g_value_get_uint (value);
            gchar* sync_mode = g_strdup_printf("%d", priv->settings.sync_mode);
            if (priv->control_connected)
                res = uca_phantom_communicate_set_variable(communicator, UNIT_CAM_SYNCIMG, sync_mode, &internal_error);
            g_free(sync_mode);
            break;
        case PROP_ACQUISITION_MODE:
            priv->settings.acquisition_mode = g_value_get_uint (value);
            gchar* acquisition_mode = g_strdup_printf("%d", priv->settings.acquisition_mode);
            if (priv->control_connected)
                res = uca_phantom_communicate_set_variable(communicator, UNIT_CAM_MODE, acquisition_mode, &internal_error);
            g_free(acquisition_mode);
            break;
        case PROP_IMAGE_FORMAT:
            priv->settings.image_format = g_value_get_uint (value);
            // Requested when triggered
            break;
        case PROP_TIMESTAMP_FORMAT:
            priv->settings.timestamp_format = g_value_get_uint (value);
            if (priv->settings.timestamp_format != TS_NONE && priv->control_connected){
                gchar* timestamp_format = g_strdup_printf ("%u", priv->settings.timestamp_format);
                res = uca_phantom_communicate_set_variable (communicator, UNIT_CAM_TSFORMAT, timestamp_format, &internal_error);
                g_free (timestamp_format);
            }
            break;
        case PROP_XNETCARD:
            g_free (priv->xnetcard);
            priv->xnetcard = g_strdup (g_value_get_string (value));
            if (priv->communicator != NULL)
                g_object_set (priv->communicator, "xnetcard", priv->xnetcard, NULL);
            break;
        case PROP_XENABLED:
            priv->xenabled = g_value_get_boolean (value);
            if (priv->communicator != NULL)
                g_object_set (priv->communicator, "xenabled", priv->xenabled, NULL);
            break;
        case PROP_BUFFERED:
            priv->buffered = g_value_get_boolean (value);
            break;
        case PROP_NUM_BUFFERS:
            priv->numbuffers = g_value_get_uint (value);
            if (priv->control_connected){
                res = uca_phantom_communicate_set_nb_cines (priv->communicator, priv->numbuffers, &internal_error);
            }
            if (priv->cine_tracker != NULL)
                g_free (priv->cine_tracker);
            priv->cine_tracker = g_new0 (guint, priv->numbuffers);
            break;
        default:
            g_print ("set : Property %d not found\n", property_id);
            // Warn if the property is not defined in this class
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;       
    }

    if (res == FALSE) {
        g_warning ("Failed to set property %s: %s", g_param_spec_get_name (pspec), internal_error->message);
        g_error_free (internal_error);
    }
}

/**
 * TODO: ask phantom for the current value of the property
 */
static void
uca_phantom_camera_get_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec) {
    UcaPhantomCameraPrivate *priv;

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (object);

    GError *internal_error = NULL;


    switch (property_id) {
        // Use all properties defined in base_overrideables
        case PROP_SENSOR_PHYSICAL_WIDTH:
            g_value_set_double (value, priv->sensor_physical_width);
            break;
        case PROP_SENSOR_PHYSICAL_HEIGHT:
            g_value_set_double (value, priv->sensor_physical_height);
            break;
        case PROP_MAX_SENSOR_RESOLUTION_WIDTH:
            g_value_set_uint (value, priv->max_sensor_resolution_width);
            break;
        case PROP_MAX_SENSOR_RESOLUTION_HEIGHT:
            g_value_set_uint (value, priv->max_sensor_resolution_height);
            break;
        case PROP_XINC:
            g_value_set_uint (value, priv->xinc);
            break;
        case PROP_YINC:
            g_value_set_uint (value, priv->yinc);
            break;
        case PROP_INTERNAL_MEMORY_SIZE:
            g_value_set_uint (value, priv->internal_memory_size);
            break;
        case PROP_NAME:
            g_value_set_string (value, priv->name);
            break;
        case PROP_SENSOR_WIDTH:
            g_value_set_uint (value, priv->settings.sensor_width);
            break;
        case PROP_SENSOR_HEIGHT:
            g_value_set_uint (value, priv->settings.sensor_height);
            break;
        case PROP_SENSOR_PIXEL_WIDTH:
            g_value_set_double (value, sensor_pixel_width);
            break;
        case PROP_SENSOR_PIXEL_HEIGHT:
            g_value_set_double (value, sensor_pixel_height);
            break;
        case PROP_SENSOR_BITDEPTH:
            g_value_set_uint (value, priv->settings.sensor_bit_depth);
            break;
        case PROP_SENSOR_HORIZONTAL_BINNING:
            g_value_set_uint (value, 1); // TODO
            break;
        case PROP_SENSOR_VERTICAL_BINNING:
            g_value_set_uint (value, 1); // TODO
            break;
        case PROP_TRIGGER_SOURCE:
            g_value_set_enum (value, priv->settings.trigger_source);
            break;
        case PROP_TRIGGER_TYPE:
            g_value_set_enum (value, priv->settings.trigger_type);
            break;
        case PROP_EXPOSURE_TIME:
            g_value_set_double (value, priv->settings.exposure_time);
            break;
        case PROP_FRAMES_PER_SECOND:
            g_value_set_double (value, priv->settings.frames_per_second);
            break;
        case PROP_ROI_X:
            g_value_set_uint (value, priv->settings.roi_x0);
            break;
        case PROP_ROI_Y:
            g_value_set_uint (value, priv->settings.roi_y0);
            break;
        case PROP_ROI_WIDTH:
            g_value_set_uint (value, priv->settings.roi_width);
            break;
        case PROP_ROI_HEIGHT:
            g_value_set_uint (value, priv->settings.roi_height);
            break;
        case PROP_ROI_WIDTH_MULTIPLIER:
            g_value_set_uint (value, priv->roi_width_multiplier);
            break;
        case PROP_ROI_HEIGHT_MULTIPLIER:
            g_value_set_uint (value, priv->roi_height_multiplier);
            break;
        case PROP_HAS_STREAMING:
            g_value_set_boolean (value, priv->has_streaming);
            break;
        case PROP_HAS_CAMRAM_RECORDING:
            g_value_set_boolean (value, priv->has_camram_recording);
            break;
        case PROP_RECORDED_FRAMES:
            g_value_set_uint (value, 0); // TODO
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
            g_value_set_boolean (value, priv->settings.shutter_off);
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
        case PROP_BUFFERED:
            g_value_set_boolean (value, priv->buffered);
            break;
        case PROP_NUM_BUFFERS:
            g_value_set_uint (value, priv->numbuffers);
            break;
        default:
            g_print ("get : Property %d not found\n", property_id);
            // Warn if the property is not defined in this class
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;       
    }
}
static void
uca_phantom_camera_dispose (GObject *object) {
    UcaPhantomCameraPrivate *priv;

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (object);

    G_OBJECT_CLASS (uca_phantom_camera_parent_class)->dispose (object);

    if (priv->xnetcard) {
        g_free (priv->xnetcard);
        priv->xnetcard = NULL;
    }
    if (priv->name) {
        g_free (priv->name);
        priv->name = NULL;
    }

    if (priv->cine_tracker) {
        g_free (priv->cine_tracker);
        priv->cine_tracker = NULL;
    }

    g_cond_clear (&priv->cine_cond);
    g_mutex_clear (&priv->cine_mutex);
}

static void
uca_phantom_camera_finalize (GObject *object) {
    UcaPhantomCameraPrivate *priv;
    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (object);


    G_OBJECT_CLASS (uca_phantom_camera_parent_class)->finalize (object);

    // All network related objects are automatically destroyed by the communicator
    if (priv->communicator) {
        g_object_unref (priv->communicator);
        priv->communicator = NULL;
    }    
}

static void
uca_phantom_camera_constructed (GObject *object) {
    UcaPhantomCamera *camera;
    UcaPhantomCameraPrivate *priv;
    GError *error = NULL;

    camera = UCA_PHANTOM_CAMERA (object);
    priv = camera->priv;

    priv->constructed = TRUE;

    

    G_OBJECT_CLASS (uca_phantom_camera_parent_class)->constructed (object);
}

static gboolean
uca_phantom_camera_initable_init (GInitable *initable,
                              GCancellable *cancellable,
                              GError **error) {
    UcaPhantomCamera *camera;
    UcaPhantomCameraPrivate *priv;
    GError *internal_error = NULL;

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

    // Create a phantom communicator object
    priv->communicator = uca_phantom_communicate_new ();

    // Init base class properties
    priv->name = g_strdup ("Phantom Camera");
    priv->has_streaming = FALSE;
    priv->has_camram_recording = TRUE;
    priv->buffered = FALSE;
    priv->xenabled = FALSE;
    priv->numbuffers = 16;
    priv->cine_tracker = g_new0 (guint, priv->numbuffers);
    

    // Set the network properties
    gchar *xnetcard = getenv ("PHANTOM_XNETCARD");
    if (xnetcard == NULL) {
        g_debug ("No XNETCARD environment variable found\n");
        xnetcard = priv->xnetcard;
    }
    // g_print ("XNETCARD: %s\n", xnetcard);

    g_object_set (camera, "xnetcard", xnetcard,
                        "xenabled", priv->xenabled,
                        "buffered", priv->buffered,
                        "num-buffers", priv->numbuffers, // Number of cines. 16 cines ~ 3000 full resolution images per cine
                        NULL);

    g_object_set (priv->communicator,
                    "xnetcard", priv->xnetcard,
                    "xenabled", priv->xenabled,
                    NULL);

    // g_print ("Connecting to the camera\n");

    // Connect the control streamm to the camera
    if (!uca_phantom_communicate_connect_controlstream(priv->communicator, &internal_error)) {
        g_propagate_error (error, internal_error);
        return FALSE;
    }
    else {
        priv->control_connected = TRUE;
    }

    GValue value = G_VALUE_INIT;
    if (!uca_phantom_communicate_get_variable (priv->communicator, UNIT_INFO_XMAX, 0, &value, &internal_error)) {
        g_propagate_error (error, internal_error);
        return FALSE;
    }
    priv->max_sensor_resolution_width = g_value_get_uint (&value);
    priv->settings.sensor_width = priv->max_sensor_resolution_width;
    priv->roi_width_multiplier = 1;
    priv->settings.roi_width = priv->max_sensor_resolution_width;
    g_value_unset (&value);

    if (!uca_phantom_communicate_get_variable (priv->communicator, UNIT_INFO_YMAX, 0, &value, &internal_error)) {
        g_propagate_error (error, internal_error);
        return FALSE;
    }
    priv->max_sensor_resolution_height = g_value_get_uint (&value);
    priv->settings.sensor_height = priv->max_sensor_resolution_height;
    priv->roi_height_multiplier = 1;
    priv->settings.roi_height = priv->max_sensor_resolution_height;
    g_value_unset (&value);

    if (!uca_phantom_communicate_get_variable (priv->communicator, UNIT_INFO_EXPDEAD, 0, &value, &internal_error)) {
        g_propagate_error (error, internal_error);
        return FALSE;
    }
    priv->expdead = g_value_get_uint (&value);
    g_value_unset (&value);

    if (!uca_phantom_communicate_get_variable (priv->communicator, UNIT_INFO_XINC, 0, &value, &internal_error)) {
        g_propagate_error (error, internal_error);
        return FALSE;
    }
    priv->xinc = g_value_get_uint (&value);
    g_value_unset (&value);

    if (!uca_phantom_communicate_get_variable (priv->communicator, UNIT_INFO_YINC, 0, &value, &internal_error)) {
        g_propagate_error (error, internal_error);
        return FALSE;
    }
    priv->yinc = g_value_get_uint (&value);
    g_value_unset (&value);

    if (!uca_phantom_communicate_get_variable (priv->communicator, UNIT_INFO_CINEMEM, 0, &value, error)) {
        return FALSE;
    }
    priv->internal_memory_size = g_value_get_uint (&value);
    g_value_unset (&value);

    priv->sensor_physical_width = priv->max_sensor_resolution_width * sensor_pixel_width;
    priv->sensor_physical_height = priv->max_sensor_resolution_height * sensor_pixel_height;
    priv->sensor_pixel_width = sensor_pixel_width;
    priv->sensor_pixel_height = sensor_pixel_height;

    // Update the real time clock
    gchar *time_val = g_strdup_printf ("%ld", time(NULL));
    if (!uca_phantom_communicate_run_command (priv->communicator, CMD_SET_REAL_TIME_CLOCK, time_val, NULL, &internal_error)) {
        g_propagate_error (error, internal_error);
        return FALSE;
    }
    g_free (time_val);

    // // Experimental !
    // gchar* enable_crop = g_strdup_printf("%d", 1);
    // res = uca_phantom_communicate_set_variable(communicator, UNIT_DEFC_META_CROP, enable_crop, error);
    // g_free(enable_crop);

    return TRUE;
}

static void
uca_phantom_camera_initable_iface_init (GInitableIface *iface) {
    iface->init = uca_phantom_camera_initable_init;
}

/**
 * 
 */
static void
uca_phantom_camera_class_init (UcaPhantomCameraClass *klass) {    
    GObjectClass *oclass = G_OBJECT_CLASS (klass);
    UcaCameraClass *camera_class = UCA_CAMERA_CLASS (klass);
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
    // camera_class->grab_live = uca_phantom_camera_grab_live;
    camera_class->trigger = uca_phantom_camera_trigger;


    // Implement the base class properties
    for (guint i = 0; base_overrideables[i] != 0; i++) {
        g_object_class_override_property (oclass, base_overrideables[i], uca_camera_props[base_overrideables[i]]);
    }


    // Add the phantom specific unit variables as properties
    uca_phantom_camera_properties[PROP_FOCAL_LENGTH] =
        g_param_spec_float ("focal-length",
                             "Focal length",
                             "Focal length",
                             0, G_MAXFLOAT, 0,
                             G_PARAM_READWRITE);
    uca_phantom_camera_properties[PROP_APERTURE] =
        g_param_spec_float ("aperture",
                             "Aperture",
                             "Aperture",
                             0, G_MAXFLOAT, 0,
                             G_PARAM_READWRITE);
                             
    uca_phantom_camera_properties[PROP_EDR_EXP] =
        g_param_spec_uint ("edrexp",
                             "EDR exposure time",
                             "EDR exposure time",
                             0, G_MAXUINT, 0,
                             G_PARAM_READWRITE);
    uca_phantom_camera_properties[PROP_SHUTTER_OFF] =
        g_param_spec_boolean ("shutteroff",
                              "Shutter off",
                              "Shutter off",
                              FALSE,
                              G_PARAM_READWRITE);
    uca_phantom_camera_properties[PROP_AEXPMODE] =
        g_param_spec_uint ("aexpmode",
                            "Auto exposure mode",
                            "Auto exposure mode",
                            AUTO_EXP_MODE_OFF, AUTO_EXP_MODE_CENTER, AUTO_EXP_MODE_CENTER,
                            G_PARAM_READWRITE);

    uca_phantom_camera_properties[PROP_AEXPCOMP] =
        g_param_spec_float ("aexpcomp",
                            "Auto exposure compensation",
                            "Auto exposure compensation",
                            0, G_MAXFLOAT, 0,
                            G_PARAM_READWRITE);
    uca_phantom_camera_properties[PROP_NB_POST_TRIGGER_FRAMES] =
        g_param_spec_uint ("postframes",
                           "Number of post trigger frames",
                           "Number of post trigger frames",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE);
    uca_phantom_camera_properties[PROP_NB_PRE_TRIGGER_FRAMES] =
        g_param_spec_uint ("preframes",
                           "Number of pre trigger frames",
                           "Number of pre trigger frames",
                           0, G_MAXUINT, 1,
                           G_PARAM_READWRITE);
    uca_phantom_camera_properties[PROP_SYNC_MODE] =
        g_param_spec_uint ("syncmode",
                           "Sync mode",
                           "Sync mode",
                           SYNC_MODE_FREE_RUN,
                           SYNC_MODE_VIDEO_FRAME_RATE,
                           SYNC_MODE_VIDEO_FRAME_RATE,
                           G_PARAM_READWRITE);
    uca_phantom_camera_properties[PROP_ACQUISITION_MODE] =
        g_param_spec_uint ("acqmode",
                           "Acquisition mode",
                           "Acquisition mode",
                           ACQUISITION_MODE_STANDARD,
                           ACQUISITION_MODE_BRIGHT_FIELD,
                           ACQUISITION_MODE_STANDARD,
                           G_PARAM_READWRITE);
    uca_phantom_camera_properties[PROP_IMAGE_FORMAT] =
        g_param_spec_uint ("imgformat",
                           "Image format",
                           "Image format",
                            IMG_8, IMG_P12L, IMG_P12L,
                           G_PARAM_READWRITE);
    uca_phantom_camera_properties[PROP_TIMESTAMP_FORMAT] =
        g_param_spec_uint ("tsformat",
                             "Timestamp",
                             "Timestamp",
                             TS_SHORT, TS_NONE, TS_NONE,
                             G_PARAM_READWRITE);
    uca_phantom_camera_properties[PROP_XNETCARD] =
        g_param_spec_string ("xnetcard",
                             "10 Gb NIC",
                             "10 Gb NIC",
                             "ens21f0",
                             G_PARAM_READWRITE);
                             
    uca_phantom_camera_properties[PROP_XENABLED] =
        g_param_spec_boolean ("xenabled",
                              "X enabled",
                              "X enabled",
                              TRUE,
                              G_PARAM_READWRITE);
                            
    uca_phantom_camera_properties[PROP_BUFFERED] =
        g_param_spec_boolean ("buffered",
                           "Save images to cine first",
                           "Save images to cine first",
                           TRUE,
                           G_PARAM_READWRITE);
    uca_phantom_camera_properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Name",
                             "Phantom",
                             G_PARAM_READWRITE);
    uca_phantom_camera_properties[PROP_SENSOR_PHYSICAL_HEIGHT] =
        g_param_spec_double ("sensor-physical-height",
                            "Sensor physical height",
                            "Sensor physical height",
                            0, G_MAXDOUBLE, 0,
                            G_PARAM_READABLE);
    uca_phantom_camera_properties[PROP_SENSOR_PHYSICAL_WIDTH] =
        g_param_spec_double ("sensor-physical-width",
                            "Sensor physical width",
                            "Sensor physical width",
                            0, G_MAXDOUBLE, 0,
                            G_PARAM_READABLE);
    uca_phantom_camera_properties[PROP_MAX_SENSOR_RESOLUTION_WIDTH] =
        g_param_spec_uint ("max-sensor-resolution-width",
                            "Max sensor resolution width",
                            "Max sensor resolution width",
                            0, G_MAXUINT, 0,
                            G_PARAM_READABLE);
    uca_phantom_camera_properties[PROP_MAX_SENSOR_RESOLUTION_HEIGHT] =
        g_param_spec_uint ("max-sensor-resolution-height",
                            "Max sensor resolution height",
                            "Max sensor resolution height",
                            0, G_MAXUINT, 0,
                            G_PARAM_READABLE);
    uca_phantom_camera_properties[PROP_XINC] =
        g_param_spec_uint ("xinc",
                            "X increment",
                            "X increment",
                            0, G_MAXUINT, 0,
                            G_PARAM_READABLE);
    uca_phantom_camera_properties[PROP_YINC] =
        g_param_spec_uint ("yinc",
                            "Y increment",
                            "Y increment",
                            0, G_MAXUINT, 0,
                            G_PARAM_READABLE);
    uca_phantom_camera_properties[PROP_INTERNAL_MEMORY_SIZE] =
        g_param_spec_uint ("internal-memory",
                            "Internal memory size",
                            "Internal memory size",
                            0, G_MAXUINT, 0,
                            G_PARAM_READABLE);
    
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

    // g_print ("Initializing Phantom Camera\n");

    g_cond_init (&priv->cine_cond);
    g_mutex_init (&priv->cine_mutex);

    priv->settings = (CaptureSettings){
        .sensor_width = priv->max_sensor_resolution_width,
        .sensor_height = priv->max_sensor_resolution_height,
        .roi_x0 = 0,
        .roi_y0 = 0,
        .roi_width = priv->max_sensor_resolution_width,
        .roi_height = priv->max_sensor_resolution_height,
        .sensor_bit_depth = ImageFormatSpecs[IMG_P12L].bit_depth,
        
        .frames_per_second = 100.0,
        .exposure_time = 0.001,
        .focal_length = 0.0,
        .aperture = 0.0,
        .edr_exp = 459,
        .shutter_off = FALSE,
        .aexpcomp = 0.0,
        .nb_post_trigger_frames = 1,
        .nb_pre_trigger_frames = 0,
        .current_cine = 1,

        .aexpmode = AUTO_EXP_MODE_AVERAGE,
        .sync_mode = SYNC_MODE_VIDEO_FRAME_RATE,
        .acquisition_mode = ACQUISITION_MODE_STANDARD,
        .image_format = IMG_P12L,
        .timestamp_format = TS_NONE,
        .trigger_source = UCA_CAMERA_TRIGGER_SOURCE_SOFTWARE,
        .trigger_type = UCA_CAMERA_TRIGGER_TYPE_EDGE,
    };    
    priv->data_connected = FALSE;
    priv->x_data_connected = FALSE;
}

G_MODULE_EXPORT GType
camera_plugin_get_type (void) {
    return UCA_TYPE_PHANTOM_CAMERA;
}

UcaPhantomCamera *uca_phantom_camera_new (void) {
    return UCA_PHANTOM_CAMERA (
        g_object_new (
            UCA_TYPE_PHANTOM_CAMERA, NULL));
}