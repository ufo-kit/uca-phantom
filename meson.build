project('ucaphantom', 'c')

uca_dep = dependency('libuca', version: '>= 2.0')
gio_dep = dependency('gio-2.0', version: '>= 2.22')

plugindir = uca_dep.get_pkgconfig_variable('plugindir')

shared_library('ucaphantom',
    sources: ['uca-phantom-camera.c'],
    dependencies: [uca_dep, gio_dep],
    install: true,
    install_dir: plugindir,
)
