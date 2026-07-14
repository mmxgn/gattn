project('gattn', 'c', version: '0.1.6', default_options: ['c_std=c11'])

gnome = import('gnome')

deps = [
  dependency('gtk4'),
  dependency('libadwaita-1'),
  dependency('vte-2.91-gtk4'),
  dependency('gtksourceview-5'),
]

install_data('data/org.mmxgn.gattn.desktop',
  install_dir: join_paths(get_option('datadir'), 'applications'))

install_data('data/icons/hicolor/scalable/apps/org.mmxgn.gattn.svg',
  install_dir: join_paths(get_option('datadir'), 'icons', 'hicolor', 'scalable', 'apps'))

resources = gnome.compile_resources('gattn-resources',
  'data/gattn.gresource.xml', source_dir: 'data', c_name: 'gattn_resources')

executable('gattn', [
  resources,
  'src/main.c',
  'src/session.c',
  'src/state_detector.c',
  'src/notify.c',
  'src/recents.c',
  'src/prefs_store.c',
  'src/sessions_store.c',
  'src/ui/sidebar.c',
  'src/ui/prefs.c',
  'src/ui/grid.c',
  'src/ui/session_picker.c',
], dependencies: deps, install: true)
