#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>

#define WIDTH 640
#define HEIGHT 480
#define CHANNELS 1
#define BITS_PER_CHANNEL 16
#define BYTES_PER_CHANNEL (BITS_PER_CHANNEL / 8)

void send_raw_image(GOutputStream *stream, guint16 *image_data) {
    gsize bytes_to_write = WIDTH * HEIGHT * CHANNELS * BYTES_PER_CHANNEL;
    g_output_stream_write_all(stream, (const void*)image_data, bytes_to_write, NULL, NULL);
}

int main(int argc, char **argv) {
    GError *error = NULL;
    GOutputStream *stream;
    GSocketConnection *connection;
    GSocket *socket;
    GSocketAddress *address;
    guint16 *image_data = (guint16*)malloc(WIDTH * HEIGHT * CHANNELS * sizeof(guint16));

    // Initialize image data with some sample values
    for (int i = 0; i < WIDTH * HEIGHT * CHANNELS; i++) {
        image_data[i] = i % (1 << BITS_PER_CHANNEL);
    }

    // Create a socket and connect to a remote host
    socket = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, &error);
    address = g_inet_socket_address_new_from_string("localhost", 1234, &error);
    connection = g_socket_connect(socket, address, NULL, &error);
    if (error != NULL) {
        g_printerr("Failed to connect: %s\n", error->message);
        return 1;
    }

    // Create a output stream and send raw image data
    stream = g_io_stream_get_output_stream(G_IO_STREAM(connection));
    for (int i = 0; i < 10; i++) {
        send_raw_image(stream, image_data);
        usleep(100000);
    }

    // Play the stream using mpv
    char *command = g_strdup_printf("mpv --demuxer=rawvideo --demuxer-rawvideo-format=rgb%c --demuxer-rawvideo-w=%d --demuxer-rawvideo-h=%d fd://0", BITS_PER_CHANNEL == 8 ? 'a' : 'b', WIDTH, HEIGHT);
    GSubprocess *subprocess = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, error, command, NULL);
    if (error != NULL) {
        g_printerr("Failed to run mpv: %s\n", error->message);
        return 1;
    }
    g_subprocess_get_stdin_pipe(subprocess);

    g_object_unref(stream);
    g_object_unref(connection);
    g_object_unref(socket);
    free(image_data);

    return 0;
}