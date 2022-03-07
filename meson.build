project(
  'chatty', 'c', 'cpp',
  version: '0.6.2',
  meson_version: '>= 0.53.0',
)

i18n = import('i18n')
gnome = import('gnome')
cc = meson.get_compiler('c')

top_inc = include_directories('.')
src_inc = []

purple_dep = dependency('purple', required: get_option('purple'))

config_h = configuration_data()
config_h.set10('HAVE_EXPLICIT_BZERO', cc.has_function('explicit_bzero'))
config_h.set('PURPLE_ENABLED', purple_dep.found())
config_h.set_quoted('GETTEXT_PACKAGE', 'purism-chatty')
config_h.set_quoted('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))
config_h.set_quoted('PACKAGE_NAME', meson.project_name())
config_h.set_quoted('PACKAGE_VERSION', meson.project_version())

# TODO: Use has_headers: 'olm/olm.h' when we bump meson requirement
libolm_dep = cc.find_library('olm', required: true)

if (cc.has_function('olm_account_unpublished_fallback_key', dependencies: libolm_dep))
  config_h.set('OLM_ACCOUNT_PICKLE_V4', true)
else
  config_h.set('OLM_ACCOUNT_PICKLE_V4', false)
endif

if (cc.has_function('olm_pk_key_from_private', dependencies: libolm_dep))
  config_h.set('HAVE_OLM3', true)
else
  config_h.set('HAVE_OLM2', true)
endif

configure_file(
  output: 'config.h',
  configuration: config_h,
)
add_project_arguments([
  '-I' + meson.build_root(),
  '-DHAVE_CONFIG_H',
  '-DGLIB_DISABLE_DEPRECATION_WARNINGS',
  '-DG_LOG_USE_STRUCTURED',
], language: 'c')

global_c_args = []
test_c_args = [
  '-Wcast-align',
  '-Wdate-time',
  '-Wdeclaration-after-statement',
  ['-Werror=format-security', '-Werror=-format-nonliteral'],
  '-Wendif-labels',
  '-Werror=incompatible-pointer-types',
  '-Werror=missing-declarations',
  '-Werror=overflow',
  '-Werror=return-type',
  '-Werror=shift-count-overflow',
  '-Werror=shift-overflow=1',
  '-Werror=implicit-fallthrough=3',
  '-Wfloat-equal',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Winit-self',
  '-Wmaybe-uninitialized',
  '-Wmissing-field-initializers',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wnested-externs',
  '-Wno-missing-field-initializers',
  '-Wno-sign-compare',
  '-Wno-strict-aliasing',
  '-Wno-unused-parameter',
  '-Wold-style-definition',
  '-Wpointer-arith',
  '-Wredundant-decls',
  '-Wshadow',
  '-Wstrict-prototypes',
  '-Wswitch-default',
  '-Wswitch-enum',
  '-Wtype-limits',
  '-Wundef',
  '-Wunused-function',
]
if get_option('buildtype') != 'plain'
  test_c_args += '-fstack-protector-strong'
endif

foreach arg: test_c_args
  if cc.has_multi_arguments(arg)
    global_c_args += arg
  endif
endforeach
add_project_arguments(
  global_c_args,
  language: 'c'
)

run_data = configuration_data()
run_data.set('ABS_BUILDDIR', meson.current_build_dir())
run_data.set('ABS_SRCDIR', meson.current_source_dir())
configure_file(
  input: 'run.in',
  output: 'run',
  configuration: run_data)

libebook_dep = dependency('libebook-contacts-1.2')
libfeedback_dep = dependency('libfeedback-0.0')
libm_dep = cc.find_library('m')
libgd = subproject('libgd',
                   default_options: [
                     'with-tagged-entry=true',
                     'static=true'
                   ])

libgd_dep = libgd.get_variable('libgd_dep')

subdir('completion')
subdir('data')
subdir('help')
subdir('src')
subdir('tests')
subdir('po')

summary({
  'Enable libpurple': purple_dep.found(),
})

meson.add_install_script('build-aux/meson/postinstall.py')
