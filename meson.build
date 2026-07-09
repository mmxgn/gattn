project('gattn', 'c', version: '0.1.0', default_options: ['c_std=c11'])

deps = [
  dependency('gtk4'),
  dependency('libadwaita-1'),
  dependency('vte-2.91-gtk4'),
]

executable('gattn', ['src/main.c', 'src/session.c'], dependencies: deps)
