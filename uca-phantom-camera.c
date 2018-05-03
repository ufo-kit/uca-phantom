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
#include <gio/gio.h>
#include <gmodule.h>
#include <string.h>

#include <uca/uca-camera.h>
#include "uca-phantom-camera.h"
#include "config.h"


#define UCA_PHANTOM_CAMERA_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UCA_TYPE_PHANTOM_CAMERA, UcaPhantomCameraPrivate))

static void uca_phantom_camera_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (UcaPhantomCamera, uca_phantom_camera, UCA_TYPE_CAMERA,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                uca_phantom_camera_initable_iface_init))

GQuark uca_phantom_camera_error_quark ()
{
    return g_quark_from_static_string("uca-net-camera-error-quark");
}

enum {
    /* info structure */
    PROP_SENSOR_TYPE = N_BASE_PROPERTIES,
    PROP_SENSOR_VERSION,
    PROP_HARDWARE_VERSION,
    PROP_KERNEL_VERSION,
    PROP_FIRMWARE_VERSION,
    PROP_FPGA_VERSION,
    N_PROPERTIES
};

static gint base_overrideables[] = {
    PROP_NAME,
    PROP_SENSOR_WIDTH,
    PROP_SENSOR_HEIGHT,
    PROP_SENSOR_BITDEPTH,
    PROP_ROI_X,
    PROP_ROI_Y,
    PROP_ROI_WIDTH,
    PROP_ROI_HEIGHT,
    PROP_EXPOSURE_TIME,
    PROP_HAS_STREAMING,
    PROP_HAS_CAMRAM_RECORDING,
    0
};

static GParamSpec *phantom_properties[N_PROPERTIES] = { NULL, };

struct _UcaPhantomCameraPrivate {
    GError          *construct_error;
    gchar           *host;
    GSocketClient   *client;
    gsize            size;
};

typedef struct  {
    const gchar *name;
    GType        type;
    GParamFlags  flags;
    gint         property_id;
    gboolean     handle_automatically;
} UnitVariable;

static UnitVariable variables[] = {
    { "info.sensor",     G_TYPE_UINT, G_PARAM_READABLE,  PROP_SENSOR_TYPE,      TRUE },
    { "info.snsversion", G_TYPE_UINT, G_PARAM_READABLE,  PROP_SENSOR_VERSION,   TRUE },
    { "info.hwver",      G_TYPE_UINT, G_PARAM_READABLE,  PROP_HARDWARE_VERSION, TRUE },
    { "info.kernel",     G_TYPE_UINT, G_PARAM_READABLE,  PROP_KERNEL_VERSION,   TRUE },
    { "info.swver",      G_TYPE_UINT, G_PARAM_READABLE,  PROP_FIRMWARE_VERSION, TRUE },
    { "info.xver",       G_TYPE_UINT, G_PARAM_READABLE,  PROP_FPGA_VERSION,     TRUE },
    { "info.name",       G_TYPE_UINT, G_PARAM_READABLE,  PROP_NAME,             TRUE },
    { "info.xmax",       G_TYPE_UINT, G_PARAM_READABLE,  PROP_SENSOR_WIDTH,     TRUE },
    { "info.ymax",       G_TYPE_UINT, G_PARAM_READABLE,  PROP_SENSOR_HEIGHT,    TRUE },
    { "video.paox",      G_TYPE_INT,  G_PARAM_READWRITE, PROP_ROI_X,            TRUE },
    { "video.paoy",      G_TYPE_INT,  G_PARAM_READWRITE, PROP_ROI_Y,            TRUE },
    { "defc.exp",        G_TYPE_UINT, G_PARAM_READWRITE, PROP_EXPOSURE_TIME,    FALSE },
    /* { "video.pax",          G_TYPE_UINT, G_PARAM_READWRITE, PROP_ROI_WIDTH }, */
    /* { "video.pay",          G_TYPE_UINT, G_PARAM_READWRITE, PROP_ROI_HEIGHT }, */
    { NULL, }
};

static UnitVariable *
phantom_lookup_by_id (gint property_id)
{
    for (guint i = 0; variables[i].name != NULL; i++) {
        if (variables[i].property_id == property_id)
            return &variables[i];
    }

    return NULL;
}

static gchar *
phantom_get_string (UnitVariable *var)
{
    gchar *request;
    gchar *reply = NULL;

    request = g_strdup_printf ("get %s", var->name);
    g_debug ("send request `%s'", request);
    g_free (request);
    return reply;
}

static void
phantom_get (UnitVariable *var, GValue *value)
{
    gchar *reply;
    GValue reply_value = {0,};

    reply = phantom_get_string (var);
    g_value_init (&reply_value, G_TYPE_STRING);
    g_value_set_string (&reply_value, reply);
    g_value_transform (&reply_value, value);

    g_free (reply);
    g_value_unset (&reply_value);
}

static void
phantom_set (UnitVariable *var, const GValue *value)
{
    gchar *request;
    GValue request_value = {0,};

    if (!(var->flags & G_PARAM_WRITABLE))
        return;

    g_value_init (&request_value, G_TYPE_STRING);
    g_value_transform (value, &request_value);
    request = g_strdup_printf ("set %s %s", var->name, g_value_get_string (&request_value));
    g_value_unset (&request_value);
}

static void
uca_phantom_camera_start_recording (UcaCamera *camera,
                                    GError **error)
{
    /* send command */
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));

}

static void
uca_phantom_camera_stop_recording (UcaCamera *camera,
                                   GError **error)
{
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));
}

static void
uca_phantom_camera_start_readout (UcaCamera *camera,
                                  GError **error)
{
    /* set up listener */

    /* send startdata command */
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));
}

static void
uca_phantom_camera_stop_readout (UcaCamera *camera,
                                 GError **error)
{
    /* stop listener */
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));
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

static gboolean
uca_phantom_camera_grab (UcaCamera *camera,
                         gpointer data,
                         GError **error)
{
    return TRUE;
}

static void
uca_phantom_camera_trigger (UcaCamera *camera,
                        GError **error)
{
    g_return_if_fail (UCA_IS_PHANTOM_CAMERA (camera));
}

static void
uca_phantom_camera_set_property (GObject *object,
                                 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
    UnitVariable *var;

    var = phantom_lookup_by_id (property_id);

    if (var != NULL) {
        phantom_set (var, value);
        return;
    }
}

static void
uca_phantom_camera_get_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
    UnitVariable *var;

    var = phantom_lookup_by_id (property_id);

    if (var != NULL && var->handle_automatically) {
        phantom_get (var, value);
        return;
    }

    switch (property_id) {
        case PROP_SENSOR_BITDEPTH:
            g_value_set_uint (value, 12);
            break;
        case PROP_ROI_WIDTH:
            g_value_set_uint (value, 2048);
            break;
        case PROP_ROI_HEIGHT:
            g_value_set_uint (value, 1952);
            break;
        case PROP_EXPOSURE_TIME:
            {
                gchar *s;
                gdouble time;
                s = phantom_get_string (var);
                /* FIXME: remove this */
                s = "30000000";
                time = atoi (s) / 1000.0 / 1000.0 / 1000.0;
                g_value_set_double (value, time);
                /* g_free (s); */
            }
            break;
        case PROP_HAS_STREAMING:
            g_value_set_boolean (value, FALSE);
            break;
        case PROP_HAS_CAMRAM_RECORDING:
            g_value_set_boolean (value, TRUE);
            break;
    };
}

static void
uca_phantom_camera_dispose (GObject *object)
{
    G_OBJECT_CLASS (uca_phantom_camera_parent_class)->dispose (object);
}

static void
uca_phantom_camera_finalize (GObject *object)
{
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

static gboolean
phantom_discover (GSocketAddress **remote_addr, GError **error)
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
    gboolean result = TRUE;

    bcast_addr = g_inet_address_new_from_string ("255.255.255.255");
    bcast_socket_addr = g_inet_socket_address_new (bcast_addr, 7380);
    socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, error);

    if (socket == NULL) {
        result = FALSE;
        goto cleanup_discovery_addr;
    }

    g_socket_set_broadcast (socket, TRUE);

    if (g_socket_send_to (socket, bcast_socket_addr, request, sizeof (request), NULL, error) < 0) {
        result = FALSE;
        goto cleanup_discovery_socket;
    }

    if (g_socket_receive_from (socket, &remote_socket_addr, reply, sizeof (reply), NULL, error) < 0) {
        result = FALSE;
        goto cleanup_discovery_socket;
    }

    g_debug ("Phantom UDP discovery reply: `%s'", reply);
    regex = g_regex_new ("PH16 (\\d+) (\\d+) (\\d+)", 0, 0, error);

    if (!g_regex_match (regex, reply, 0, &info)) {
        g_set_error (error, UCA_CAMERA_ERROR, UCA_CAMERA_ERROR_NOT_FOUND,
                     "`%s' does not match expected reply", reply);
        goto cleanup_discovery_addr;
    }

    port_string = g_match_info_fetch (info, 1);
    port = atoi (port_string);
    g_free (port_string);
    *remote_addr = g_inet_socket_address_new (g_inet_socket_address_get_address ((GInetSocketAddress *) remote_socket_addr), port);

cleanup_discovery_addr:
    g_regex_unref (regex);
    g_object_unref (bcast_addr);
    g_object_unref (bcast_socket_addr);

cleanup_discovery_socket:
    g_object_unref (socket);
    
    return result;
}

static void
uca_phantom_camera_constructed (GObject *object)
{
    UcaPhantomCameraPrivate *priv;
    GSocketAddress *addr;

    priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (object);

    phantom_discover (&addr, &priv->construct_error);

    g_object_unref (addr);
}

static void
uca_phantom_camera_initable_iface_init (GInitableIface *iface)
{
    iface->init = ufo_net_camera_initable_init;
}

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

    for (guint i = 0; i < base_overrideables[i]; i++)
        g_object_class_override_property (oclass, base_overrideables[i], uca_camera_props[base_overrideables[i]]);

    for (guint id = N_BASE_PROPERTIES; id < N_PROPERTIES; id++)
        g_object_class_install_property (oclass, id, phantom_properties[id]);

    g_type_class_add_private (klass, sizeof(UcaPhantomCameraPrivate));
}

static void
uca_phantom_camera_init (UcaPhantomCamera *self)
{
    UcaPhantomCameraPrivate *priv;

    self->priv = priv = UCA_PHANTOM_CAMERA_GET_PRIVATE (self);

    priv->construct_error = NULL;
    priv->client = g_socket_client_new ();
}

G_MODULE_EXPORT GType
camera_plugin_get_type (void)
{
    return UCA_TYPE_PHANTOM_CAMERA;
}
