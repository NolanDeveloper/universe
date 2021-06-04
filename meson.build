project(
  'universe',
  'c',
  version: '0.1',
  default_options: ['warning_level=3']
)

cc = meson.get_compiler('c')

exe = executable(
  'universe', [
    'src/main.c',
  ],
  install: true,
  dependencies: [
    dependency('sdl2'),
    dependency('gl'),
    dependency('glew'),
    dependency('cglm'),
    dependency('libpng'),
    cc.find_library('m', required : false),
  ]
)

test('basic', exe)
