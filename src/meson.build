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
  'users/chatty-item.c',
  'users/chatty-contact.c',
  'users/chatty-pp-buddy.c',
  'users/chatty-account.c',
  'users/chatty-pp-account.c',
  'chatty-list-row.c',
  'chatty-chat.c',
  'chatty-contact-provider.c',
  'chatty-settings.c',
  'chatty-icons.c',
  'chatty-history.c',
  'chatty-utils.c',
]

chatty_sources = [
  'main.c',
  'chatty-avatar.c',
  'chatty-chat-view.c',
  'chatty-manager.c',
  'chatty-application.c',
  'chatty-window.c',
  'dialogs/chatty-settings-dialog.c',
  'dialogs/chatty-new-chat-dialog.c',
  'dialogs/chatty-new-muc-dialog.c',
  'dialogs/chatty-user-info-dialog.c',
  'dialogs/chatty-muc-info-dialog.c',
  'chatty-message.c',
  'chatty-message-row.c',
  'chatty-conversation.c',
  './xeps/xeps.c',
  './xeps/chatty-xep-0184.c',
  './xeps/chatty-xep-0313.c',
  './xeps/chatty-xep-0352.c',
  'chatty-dbus.c',
  'chatty-notify.c',
  'chatty-purple-request.c',
  'chatty-purple-notify.c',
  version_h,
]

purple = dependency('purple')
purple_plugdir = purple.get_pkgconfig_variable('plugindir')
jabber = meson.get_compiler('c').find_library('jabber', dirs: purple_plugdir)
jabber_incdir = include_directories('xeps/prpl/jabber')

chatty_deps = [
  dependency('gio-2.0', version: '>= 2.50'),
  dependency('gtk+-3.0', version: '>= 3.22'),
  purple, jabber,
  dependency('libhandy-0.0'),
  dependency('sqlite3', version: '>=3.0.0'),
  dependency('libebook-contacts-1.2'),
  dependency('libebook-1.2'),
  libebook_dep,
  libfeedback_dep,
  libm_dep,
]

gnome = import('gnome')

chatty_sources += gnome.compile_resources('chatty-resources',
  'chatty.gresource.xml',
  c_name: 'chatty'
)

libchatty = static_library(
  'libchatty', libsrc,
  install: false,
  dependencies: chatty_deps,
)

executable('chatty', chatty_sources,
  include_directories: jabber_incdir,
  dependencies: chatty_deps,
  link_with: libchatty,
  install: true,
  install_rpath: purple_plugdir,
)
