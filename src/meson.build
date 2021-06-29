version_h = vcs_tag(input: 'version.h.in',
		    output: 'version.h')

libsrc = [
  'contrib/gtktypebuiltins.c',
  'contrib/gtkflattenlistmodel.c',
  'contrib/gtkrbtree.c',
  'contrib/gtkfilter.c',
  'contrib/gtkcustomfilter.c',
  'contrib/gtkfilterlistmodel.c',
  'contrib/gtksorter.c',
  'contrib/gtkcustomsorter.c',
  'contrib/gtksortlistmodel.c',
  'contrib/gtkslicelistmodel.c',
  'dialogs/chatty-pp-account-details.c',
  'matrix/matrix-api.c',
  'matrix/matrix-enc.c',
  'matrix/matrix-db.c',
  'matrix/matrix-utils.c',
  'matrix/chatty-ma-account.c',
  'matrix/chatty-ma-buddy.c',
  'matrix/chatty-ma-chat.c',
  'users/chatty-item.c',
  'users/chatty-contact.c',
  'users/chatty-pp-buddy.c',
  'users/chatty-account.c',
  'users/chatty-pp-account.c',
  'chatty-fp-row.c',
  'chatty-list-row.c',
  'chatty-chat-view.c',
  'chatty-message-row.c',
  'chatty-text-item.c',
  'chatty-image-item.c',
  'chatty-log.c',
  'chatty-avatar.c',
  'chatty-chat.c',
  'chatty-pp-chat.c',
  'chatty-contact-provider.c',
  'chatty-message.c',
  'chatty-settings.c',
  'chatty-history.c',
  'chatty-notification.c',
  'chatty-secret-store.c',
  'chatty-utils.c',
  'chatty-phone-utils.cpp',
]

ui_files = files (
  'ui/chatty-chat-view.ui',
  'ui/chatty-contact-row.ui',
  # FIXME: Testing this fails in CI,
  # but works fine locally
  # 'ui/chatty-info-dialog.ui',
  'ui/chatty-dialog-join-muc.ui',
  'ui/chatty-dialog-new-chat.ui',
  'ui/chatty-fp-row.ui',
  'ui/chatty-list-row.ui',
  'ui/chatty-message-row.ui',
  # FIXME
  # 'ui/chatty-pp-account-details.ui',
  'ui/chatty-settings-dialog.ui',
  'ui/chatty-window.ui',
)

chatty_sources = [
  'main.c',
  'chatty-manager.c',
  'chatty-application.c',
  'chatty-window.c',
  'dialogs/chatty-info-dialog.c',
  'dialogs/chatty-settings-dialog.c',
  'dialogs/chatty-new-chat-dialog.c',
  'dialogs/chatty-new-muc-dialog.c',
  './xeps/xeps.c',
  './xeps/chatty-xep-0184.c',
  './xeps/chatty-xep-0313.c',
  './xeps/chatty-xep-0352.c',
  'chatty-purple-request.c',
  'chatty-purple-notify.c',
  version_h,
]

libphonenumber_dep = cc.find_library('phonenumber', required: true)
purple = dependency('purple')
purple_plugdir = purple.get_pkgconfig_variable('plugindir')
jabber = meson.get_compiler('c').find_library('jabber', dirs: purple_plugdir)
jabber_incdir = include_directories('xeps/prpl/jabber')

chatty_deps = [
  dependency('gio-2.0', version: '>= 2.50'),
  dependency('gtk+-3.0', version: '>= 3.22'),
  purple, jabber,
  dependency('libsecret-1'),
  dependency('libhandy-1', version: '>= 1.1.90'),
  dependency('sqlite3', version: '>=3.0.0'),
  dependency('libebook-contacts-1.2'),
  dependency('libebook-1.2'),
  dependency('gsettings-desktop-schemas'),
  dependency('libgcrypt'),
  dependency('libsoup-2.4'),
  dependency('json-glib-1.0'),
  libolm_dep,
  libebook_dep,
  libfeedback_dep,
  libm_dep,
  libphonenumber_dep,
]

gnome = import('gnome')

resources = gnome.compile_resources('chatty-resources',
  'chatty.gresource.xml',
  c_name: 'chatty'
)

libchatty = both_libraries(
  'chatty', libsrc, resources,
  install: false,
  dependencies: chatty_deps,
)

gtk_builder_tool = find_program('gtk-builder-tool', required: false)
if gtk_builder_tool.found()
  preload_env = 'LD_PRELOAD=@0@:libhandy-1.so'.format(libchatty.get_shared_lib().full_path())
  foreach file: ui_files
    test('Validate @0@'.format(file), gtk_builder_tool,
         env: [preload_env],
         args: ['validate', file])
  endforeach
endif

executable('chatty', chatty_sources, resources,
  include_directories: jabber_incdir,
  dependencies: chatty_deps,
  link_with: libchatty.get_static_lib(),
  install: true,
  install_rpath: purple_plugdir,
)
