desktop_file = i18n.merge_file(
  input: 'sm.puri.Chatty.desktop.in',
  output: 'sm.puri.Chatty.desktop',
  type: 'desktop',
  po_dir: '../po',
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications')
)

desktop_file = i18n.merge_file(
  input: 'sm.puri.Chatty-daemon.desktop.in',
  output: 'sm.puri.Chatty-daemon.desktop',
  type: 'desktop',
  po_dir: '../po',
  install: true,
  install_dir: join_paths(get_option('sysconfdir'), 'xdg/autostart')
)

iconsdir = join_paths(get_option('datadir'), 'icons', 'hicolor')
foreach size: ['scalable', 'symbolic']
  install_subdir('icons/hicolor/' + size, install_dir: iconsdir)
endforeach

desktop_utils = find_program('desktop-file-validate', required: false)
if desktop_utils.found()
  test('Validate desktop file', desktop_utils,
    args: [desktop_file]
  )
endif

appstream_file = i18n.merge_file(
  input: 'sm.puri.Chatty.metainfo.xml.in',
  output: 'sm.puri.Chatty.metainfo.xml',
  po_dir: '../po',
  install: true,
  install_dir: join_paths(get_option('datadir'), 'metainfo')
)

appstream_util = find_program('appstream-util', required: false)
if appstream_util.found()
  test('Validate appstream file', appstream_util,
    args: ['validate', '--nonet', appstream_file]
  )
endif

schema_src = 'sm.puri.Chatty.gschema.xml'
compiled = gnome.compile_schemas(build_by_default: true,
                                 depend_files: files(schema_src))

install_data(schema_src,
  install_dir: join_paths(get_option('datadir'), 'glib-2.0/schemas')
)

compile_schemas = find_program('glib-compile-schemas', required: false)
if compile_schemas.found()
  test('Validate schema file', compile_schemas,
    args: ['--strict', '--dry-run', meson.current_source_dir()]
  )
endif
