executables = [
    'grab_images_ucad.c',
    'grab_images_auto.c',
    'grab_images_software.c',
    'grab_live_images.c',
    'grab_ximages_software.c',
    'properties.c',
]

foreach filename : executables
    executable(
        filename.split('.')[0],
        sources: [filename, 'utilities.c'],
        dependencies: deps,
        c_args: c_args,
        include_directories: [headers, uca_headers]
    ) 
endforeach