scheme_sources = files(
  'main.c',
  'pal/pal_posix.c',
  'gc/gc.c',
  'vm/vm.c',
  'vm/builtins.c',
  'reader/reader.c',
  'bootstrap/compiler.c',
  'primitives/arith.c',
  'primitives/pair.c',
  'primitives/port.c',
)

scheme_inc = include_directories('include')

executable('scheme',
  scheme_sources,
  include_directories: scheme_inc,
)
