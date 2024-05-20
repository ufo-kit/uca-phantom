project(
    'ucaphantom', 
    'c',
    default_options:['c_std=c99' ])

c_args = ['-O2', '-D_GNU_SOURCE','-Wall', '-Wextra', '-fPIC', '-msse4', '-maes']

gnome = import('gnome')

uca_dep = dependency('libuca', version: '>= 2.0')
gio_dep = dependency('gio-2.0', version: '>= 2.22')
glib_dep = dependency('glib-2.0', version: '>= 2.38')
gobject_dep = dependency('gobject-2.0', version: '>= 2.64.6')
gmodule_dep = dependency('gmodule-2.0', version: '>= 2.38')
libpcap = dependency('libpcap', version: '>= 1.0')
# hdf5_lib = dependency('hdf5')
# cc = meson.get_compiler('c')

# # Manually specify the PF_RING include directories and libraries
# pfring_inc = include_directories('/usr/include')
# pfring_lib = cc.find_library('pfring', dirs: '/usr/lib')

# Generate the enum types
enum_sources = ['uca-phantom-communicate.h']
enum_files = gnome.mkenums('phantom-enums',
    sources: enum_sources,
    h_template: 'phantom-enums.h.template',
    c_template: 'phantom-enums.c.template',
)

enums_c = enum_files[0]
enums_h = enum_files[1]

deps = [uca_dep, glib_dep, gio_dep, gobject_dep, libpcap]
# deps += [pfring_lib, hdf5_lib]

headers = include_directories('.')
uca_headers = include_directories('@0@/@1@/uca'.format(get_option('prefix'), 'include'))

plugindir = uca_dep.get_pkgconfig_variable('plugindir')

sources = files(
    'uca-phantom-variables.c',
    'uca-phantom-communicate.c',
    'uca-phantom-camera.c',
    'ringbuf.c',
) + enum_files

phantom = library('ucaphantom',
    c_args: c_args,
    sources: sources,
    include_directories: headers,
    dependencies: deps,
    install: true,
    install_dir: plugindir,
)

# subdir('test')
