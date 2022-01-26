src_inc += include_directories('.')

libsrc += files([
  'chatty-mm-account.c',
  'chatty-mm-buddy.c',
  'chatty-mm-buddy.c',
  'chatty-mm-chat.c',
  'chatty-mmsd.c',
  'chatty-sms-uri.c',
])

chatty_deps += [
  dependency('mm-glib', version: '>= 1.12.0'),
]
