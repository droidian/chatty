src_inc += include_directories('.')

libsrc += files([
  'chatty-matrix.c',
  'matrix-api.c',
  'matrix-enc.c',
  'matrix-db.c',
  'matrix-net.c',
  'matrix-utils.c',
  'chatty-ma-account.c',
  'chatty-ma-buddy.c',
  'chatty-ma-chat.c',
])

chatty_deps += [
  dependency('libgcrypt'),
  dependency('libsoup-2.4'),
  dependency('json-glib-1.0'),
  libolm_dep,
]
