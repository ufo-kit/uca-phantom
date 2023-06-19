# Python file to read binary image
import matplotlib.pyplot as plt
import numpy as np
import matplotlib.animation as animation
from matplotlib.widgets import Button

def show_8_bit(filename):

    # Read image
    raw_image = open(filename, 'rb')

    # Convert flat image to 2D array row by row
    image = np.fromfile(raw_image, dtype=np.uint8).reshape(1952, 2048)

    # # Display image
    plt.imshow(image, cmap='gray')
    plt.show()

def show_16_bit(filename):

    # Read image
    raw_image = open(filename, 'rb')

    # Convert flat image to 2D array row by row
    image = np.fromfile(raw_image, dtype=np.uint16).reshape(1952, 2048)

    # # Display image
    plt.imshow(image, cmap='gray')
    plt.show()

def animate_8bit(filenames):
    files = []

    for filename in filenames:
        files.append(open(filename, 'rb'))
    
    images = []
    for file in files:
        images.append(np.fromfile(file, dtype=np.uint8).reshape(1952, 2048))
    
    fig = plt.figure()
    ims = []
    for image in images:
        ims.append([plt.imshow(image, cmap='gray', animated=True)])
    
    ani = animation.ArtistAnimation(fig, ims, interval=1, blit=True, repeat_delay=1000)

    plt.show()

def animate_16bit(filenames):
    files = []

    for filename in filenames:
        files.append(open(filename, 'rb'))
    
    images = []
    for file in files:
        images.append(np.fromfile(file, dtype=np.uint16).reshape(1952, 2048))
    
    fig = plt.figure()
    ims = []
    for image in images:
        ims.append([plt.imshow(image, cmap='gray', animated=True)])
    
    ani = animation.ArtistAnimation(fig, ims, interval=50, blit=True, repeat_delay=1000)

    # Set up the writer for saving the animation as an MP4 file
    Writer = animation.writers['ffmpeg']
    writer = Writer(fps=15, metadata=dict(artist='Me'), bitrate=1800)

    # Save the animation as an MP4 file
    ani.save('sample.mp4', writer=writer)

    # Define the pause and resume functions
    def pause_animation(event):
        ani.event_source.stop()

    def resume_animation(event):
        ani.event_source.start()

    # Create the pause and resume buttons
    pause_button = Button(plt.axes([0.8, 0.05, 0.1, 0.075]), 'Pause')
    pause_button.on_clicked(pause_animation)

    resume_button = Button(plt.axes([0.9, 0.05, 0.1, 0.075]), 'Resume')
    resume_button.on_clicked(resume_animation)

    plt.show()

if __name__ == '__main__':
    filenames = []
    for i in range(0, 50):
        filenames.append('build/image_' + str(i) + '.dat')
    animate_16bit(filenames)