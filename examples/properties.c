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
        "defc-rate", (float)1000,
        NULL);

    // Get the class
    GObjectClass *klass = G_OBJECT_GET_CLASS(camera);   

    // Get all properties of the camera
    guint n_properties;
    GParamSpec **properties = g_object_class_list_properties(klass, &n_properties);

    // Print the properties
    for (int i = 0; i < n_properties; i++) {
        // Get the value of the property
        GValue value = G_VALUE_INIT;
        const gchar *property_name = g_param_spec_get_name(properties[i]);
        g_object_get_property (G_OBJECT(camera), property_name, &value);

        if (G_VALUE_HOLDS_INT(&value)) {
            g_print("Property: %s, Value: %d\n", property_name, g_value_get_int(&value));
        } else if (G_VALUE_HOLDS_UINT(&value)) {
            g_print("Property: %s, Value: %u\n", property_name, g_value_get_uint(&value));
        } else if (G_VALUE_HOLDS_ENUM(&value)) {
            g_print("Property: %s, Value: %d\n", property_name, g_value_get_enum(&value));
        } else if (G_VALUE_HOLDS_FLOAT(&value)) {
            g_print("Property: %s, Value: %f\n", property_name, g_value_get_float(&value));
        } else if (G_VALUE_HOLDS_DOUBLE(&value)) {
            g_print("Property: %s, Value: %f\n", property_name, g_value_get_double(&value));
        } else if (G_VALUE_HOLDS_STRING(&value)) {
            g_print("Property: %s, Value: %s\n", property_name, g_value_get_string(&value));
        } else if (G_VALUE_HOLDS_BOOLEAN(&value)) {
            g_print("Property: %s, Value: %s\n", property_name, g_value_get_boolean(&value) ? "true" : "false");
        }

        // Print the description of the property
        g_print("Description: %s\n", g_param_spec_get_blurb(properties[i]));

    }

    g_object_unref(camera);
    g_object_unref(manager);
}