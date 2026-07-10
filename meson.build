project('gattn', 'c', version: '0.1.0', default_options: ['c_std=c11'])

deps = [
  dependency('gtk4'),
  dependency('libadwaita-1'),
  dependency('vte-2.91-gtk4'),
]

install_data('data/org.mmxgn.gattn.desktop',
  install_dir: join_paths(get_option('datadir'), 'applications'))

executable('gattn', [
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
