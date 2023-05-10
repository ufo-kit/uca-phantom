project(
    'ucaphantom', 
    'c',
    default_options:['c_std=c99' ])

c_args = ['-O2', '-D_GNU_SOURCE','-Wall', '-Wextra', '-fPIC', '-msse4', '-maes']

uca_dep = dependency('libuca', version: '>= 2.0')
gio_dep = dependency('gio-2.0', version: '>= 2.22')
glib_dep = dependency('glib-2.0', version: '>= 2.38')
gobject_dep = dependency('gobject-2.0', version: '>= 2.64.6')
gmodule_dep = dependency('gmodule-2.0', version: '>= 2.38')
libpcap = dependency('libpcap', version: '>= 1.0')

deps = [uca_dep, glib_dep, gio_dep, gobject_dep, libpcap]

headers = include_directories('.')
uca_headers = include_directories('@0@/@1@/uca'.format(get_option('prefix'), 'include'))

plugindir = uca_dep.get_pkgconfig_variable('plugindir')

phantom = library('ucaphantom',
    sources: ['uca-phantom-camera-re.c', 'uca-phantom-communicate.c'],
    dependencies: deps,
    include_directories: headers,
    install: true,
    c_args: c_args,
    install_dir: plugindir,
)

# test_network = executable(
#     'test_network', 
#     sources: ['test_network.c', 'uca-phantom-communicate.c'],
#     dependencies: deps,
#     # link_with: phantom,
#     c_args: c_args,
#     include_directories: [headers, uca_headers]
# )

test_timestamps = executable(
    'test_timestamps', 
    sources: ['test_timestamps.c', 'uca-phantom-communicate.c'],
    dependencies: deps,
    # link_with: phantom,
    c_args: c_args,
    include_directories: [headers, uca_headers]
)

# test_default = executable(
#     'test_default', 
#     sources: ['test_default.c', 'uca-phantom-camera-re.c'],
#     dependencies: deps
# )

# subdir('test')
