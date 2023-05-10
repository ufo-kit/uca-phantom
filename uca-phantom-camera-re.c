#include <gio/gio.h>
#include <gmodule.h>
#include <glib-object.h>

#include <uca/uca-camera.h>

#include "uca-phantom-communicate.h"
#include "uca-phantom-camera-re.h"
#include "uca-phantom-variables.h"

// SSE(128) instructions AVX(256); library intrisincs 
// UCA UFO SSE 

#include <unistd.h>
#include <stdint.h>


#define UCA_PHANTOM_CAMERA_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UCA_TYPE_PHANTOM_CAMERA, UcaPhantomCameraPrivate))


static void uca_phantom_camera_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (UcaPhantomCamera, uca_phantom_camera, UCA_TYPE_CAMERA,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                uca_phantom_camera_initable_iface_init))

GQuark uca_phantom_camera_error_quark ()
{
    return g_quark_from_static_string("uca-net-camera-error-quark");
}

// Properties
static GParamSpec *uca_phantom_camera_properties[N_UNIT_PROPERTIES] = { NULL, };

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
} AcquisitionMode;

static GEnumValue sync_mode_values[] = {
    { SYNC_MODE_FREE_RUN,           "SYNC_MODE_FREE_RUN",           "sync_mode_free_run" },
    { SYNC_MODE_FSYNC,              "SYNC_MODE_FSYNC",              "sync_mode_fsync" },
    { SYNC_MODE_IRIG,               "SYNC_MODE_IRIG",               "sync_mode_irig" },
    { SYNC_MODE_VIDEO_FRAME_RATE,   "SYNC_MODE_VIDEO_FRAME_RATE",   "sync_mode_video_frame_rate" },
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
    GError *construct_error;
    GCancellable *accept;

    // Base class properties
    gchar *name;
    guint sensor_width, sensor_height;
    gfloat sensor_pixel_width, sensor_pixel_height;
    guint sensor_bitdepth;
    guint sensor_horizontal_binning, sensor_vertical_binning;
    UcaCameraTriggerSource trigger_source;
    UcaCameraTriggerType trigger_type;
    gfloat exposure_time;
    gfloat frames_per_second;
    guint roi_x, roi_y, roi_width, roi_height;
    gfloat roi_width_multiplier, roi_height_multiplier;
    gboolean has_streaming;
    gboolean has_camram_recording;
    guint recorded_frames;

    // Phantom specific properties
    guint edr_exp; // EDR exposure time
    guint shutter_off, aexpmode; // Shutter off: always use maximum exposure time and minimum straddle time, aexpmode: auto exposure mode
    gfloat aexpcomp; // Auto exposure compensation
    guint nb_post_trigger_frames, nb_pre_trigger_frames;
    guint current_cine; // Current cine number in which the camera is recording

    SyncMode sync_mode;
    AcquisitionMode acquisition_mode;
    ImageFormat image_format;
    TimestampFormat timestamp_format;

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
                                  GError **error)
{
    g_return_if_fail (error == NULL || *error == NULL);
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));

    GError *internal_error = NULL;
    UcaPhantomCameraPrivate *priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    // Launch the readout using the communicator
    if (!uca_phantom_communicate_start_readout (priv->communicator, &internal_error)) {
        g_propagate_error (error, internal_error);
        return;
    }
}

static void
uca_phantom_camera_stop_readout (UcaCamera *camera,
                                 GError **error)
{
    g_return_if_fail (error == NULL || *error == NULL);
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));

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

    GError *internal_error = NULL;
    UcaPhantomCameraPrivate *priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    gboolean result = FALSE;
    
    // Connect to the datastream(s)
    if (priv->xenabled) {
        result = uca_phantom_communicate_connect_xdatastream (priv->communicator, &internal_error);

        if (result != TRUE && internal_error != NULL) {
            g_propagate_error (error, internal_error);
            return FALSE;
        }
    } 

    if (self->timestamping || !self->xenabled) {
        result = uca_phantom_communicate_connect_datastream (priv->communicator, self->data_port, &internal_error);

        if (result != TRUE && internal_error != NULL) {
            g_propagate_error (error, internal_error);
            return FALSE;
        }
    }

    // Arm the camera
    result = uca_phantom_communicate_arm (priv->communicator, priv->current_cine, &internal_error);
    if (result != TRUE && internal_error != NULL) {
        g_propagate_error (error, internal_error);
        return FALSE;
    }
}

static void
uca_phantom_camera_stop_recording (UcaCamera *camera,
                                   GError **error)
{
    g_return_if_fail (error == NULL || *error == NULL);
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));
    /*
        Nothing to do, everything is handled automatically by the class destructor
    */
}

/**
 *
 */
static void
uca_phantom_camera_trigger (UcaCamera *camera,
                            GError **error)
{
    static guint prev_nb_post_trigger_frames = -1;
    g_return_if_fail (error == NULL || *error == NULL);
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));

    GError *internal_error = NULL;
    PhantomReply reply;
    UcaPhantomCameraPrivate *priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (camera);

    // Check if the number of post trigger frames has changed
    if (priv->nb_post_trigger_frames != prev_nb_post_trigger_frames) {
        prev_nb_post_trigger_frames = priv->nb_post_trigger_frames;
        // Set the number of post trigger frames
        gchar *pt_frames_str = g_strdup_printf("%d", priv->nb_post_trigger_frames);
        if (!uca_phantom_communicate_set_variable (priv->communicator, PROP_DEFC_PTFRAMES, pt_frames_str, &internal_error)) {
            g_propagate_error (error, internal_error);
            g_free(pt_frames_str);
            return FALSE;
        }
        g_free(pt_frames_str);
    }

    // Trigger the camera using the communicator
    if (!uca_phantom_communicate_trigger (priv->communicator, priv->nb_post_trigger_frames, &internal_error)) {
        g_propagate_error (error, internal_error);
        return;
    }

    // Immediately request and store the images
    gint start = -priv->nb_pre_trigger_frames;
    if (!uca_phantom_communicate_request_images (
            priv->communicator, 
            priv->current_cine, 
            start,
            priv->nb_post_trigger_frames,
            priv->image_format,
            priv->timestamp_format,
            &internal_error)) {
        g_propagate_error (error, internal_error);
        return;
    }
}

static void
uca_phantom_camera_write (UcaCamera *camera,
                          const gchar *name,
                          gpointer data,
                          gsize size,
                          GError **error)
{
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
                         GError **error)
{   
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
                                 GValue *value,
                                 GParamSpec *pspec)
{
    UcaPhantomCameraPrivate *priv;

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (object);

    switch (property_id) {
    case PROP_NAME:
        // Do nothing
        break;
    cade PROP_
    
    default:
        break;
    }
}

/**
 *
 */
static void
uca_phantom_camera_get_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
    UcaPhantomCameraPrivate *priv;

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (object);

}

static void
uca_phantom_camera_dispose (GObject *object)
{
    UcaPhantomCameraPrivate *priv;

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (object);


    G_OBJECT_CLASS (uca_phantom_camera_parent_class)->dispose (object);
}

static void
uca_phantom_camera_finalize (GObject *object)
{
    UcaPhantomCameraPrivate *priv;
    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (object);


    G_OBJECT_CLASS (uca_phantom_camera_parent_class)->finalize (object);

    // All network related objects are automatically destroyed by the communicator
    if (G_IS_UCA_PHANTOM_COMMUNICATOR (priv->communicator)) {
        g_object_unref (priv->communicator);
        priv->communicator = NULL;
    }
}

static gboolean
ufo_net_camera_initable_init (GInitable *initable,
                              GCancellable *cancellable,
                              GError **error)
{
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

    // Connect the control streamm to the camera
    if (!uca_phantom_communicate_connect_controlstream(priv->communicator, &internal_error)) {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Failed to connect to the camera");
        return FALSE;
    }
    else {
        priv->connected = TRUE;
    }

    return TRUE;
}

static void
uca_phantom_camera_constructed (GObject *object)
{

}

static void
uca_phantom_camera_initable_iface_init (GInitableIface *iface)
{
    iface->init = ufo_net_camera_initable_init;
}

/**
 * 
 */
static void
uca_phantom_camera_class_init (UcaPhantomCameraClass *klass)
{    
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
    camera_class->trigger = uca_phantom_camera_trigger;

    // Add the properties
    for (guint i = N_BASE_PROPERTIES; i < N_UNIT_PROPERTIES; i++) {
        GType type = Variables[i].type;
        switch (type) {
        case G_TYPE_BOOLEAN:
            uca_phantom_camera_properties[i] = g_param_spec_boolean (
                Variables[i].name,
                NULL,
                NULL,
                Variables[i].default_value,
                Variables[i].flags);
            break;
        
        default:
            break;
        }  
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

    priv->xenabled = FALSE;
    priv->current_cine = 0;
    priv->xres, yres = 0;
    priv->nb_post_trigger_frames = 100;
    priv->nb_pre_trigger_frames = 100;
    priv->current_cine = 1;

    priv->image_format = IMG_P10;
    priv->timestamp_format = TS_NONE;

    gboolean xenabled;

}

G_MODULE_EXPORT GType
camera_plugin_get_type (void)
{
    return UCA_TYPE_PHANTOM_CAMERA;
}

UcaPhantomCamera *uca_phantom_camera_new (void) {
    return UCA_PHANTOM_CAMERA (g_object_new (UCA_TYPE_PHANTOM_CAMERA, NULL));
}