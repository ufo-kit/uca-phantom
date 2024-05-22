#include <uca/uca-plugin-manager.h>
#include <uca/uca-camera.h>

#include <stdio.h>
#include <glib.h>

/**
 * There are a lot of properties that can be set in the camera.
 * They are all described in the uca-phantom/uca-phantom-variables.h file and
 * likewise in the gobject property description.
*/

int main (int argc, char *argv[]) {
    // Declaring the variables to be used
    UcaPluginManager *manager;
    UcaCamera *camera;
    GError *error = NULL;

    // Creating the camera object
    manager = uca_plugin_manager_new();

    camera = uca_plugin_manager_get_camera(manager, 
        "phantom", &error, NULL);

    if (error != NULL) {
        g_print("Error: %s\n", error->message);
        return 1;
    }

    // Set properties of the camera
    g_object_set(
        G_OBJECT(camera),
        "defc-rate", 1000,
        NULL);

    // Get the class
    GObjectClass *klass = G_OBJECT_GET_CLASS(camera);   

    // Get all properties of the camera
    guint n_properties;
    GParamSpec **properties = g_object_class_list_properties(klass, &n_properties);

    // Print the properties
    for (int i = 0; i < n_properties; i++) {
        // Get the type of the property
        GType type = G_PARAM_SPEC_VALUE_TYPE(properties[i]);
        // Get the value of the property
        GValue value = G_VALUE_INIT;
        g_value_init(&value, type);
        g_object_get_property (G_OBJECT(camera), properties[i]->name, &value);

        if (type == G_TYPE_INT) {
            g_print("Property: %s, Value: %d\n", properties[i]->name, g_value_get_int(&value));
        } else if (type == G_TYPE_FLOAT) {
            g_print("Property: %s, Value: %f\n", properties[i]->name, g_value_get_float(&value));
        } else if (type == G_TYPE_BOOLEAN) {
            g_print("Property: %s, Value: %s\n", properties[i]->name, g_value_get_boolean(&value) ? "TRUE" : "FALSE");
        } else {
            g_print("Property: %s, Value: %s\n", properties[i]->name, g_value_get_string(&value));
        }

        // Print the description of the property
        g_print("Description: %s\n", g_param_spec_get_blurb(properties[i]));

    }

    g_object_unref(camera);
    g_object_unref(manager);
}