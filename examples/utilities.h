#pragma once
#include <glib.h>
#include <stdio.h>

gboolean save_image_8 (guint8 *image, guint width, guint height, const gchar *filename);
gboolean save_image_16 (guint16 *image, guint width, guint height, const gchar *filename);