tests_inc = [
  top_inc,
  src_inc,
]

env = environment()
env.set('G_TEST_SRCDIR', meson.current_source_dir())
env.set('G_TEST_BUILDDIR', meson.current_build_dir())
env.set('GSETTINGS_SCHEMA_DIR', join_paths(meson.build_root(), 'data'))
env.set('MALLOC_CHECK_', '2')

test_items = []

if purple_dep.found()
  test_items += ['account']
endif

test_items = [
  'history',
  'settings',
  'utils',
  'matrix-api',
  'matrix-db',
  'matrix-enc',
  'matrix-utils',
  'mm-account',
]

foreach item: test_items
  t = executable(
    item,
    item + '.c',
    include_directories: tests_inc,
    link_with: libchatty.get_static_lib(),
    dependencies: chatty_deps,
  )
  test(item, t, env: env, timeout: 120)
endforeach
