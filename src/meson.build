scheme_core_sources = files(
  'pal/pal_posix.c',
  'gc/gc.c',
  'vm/vm.c',
  'vm/builtins.c',
  'reader/reader.c',
  'bootstrap/compiler.c',
  'primitives/number.c',
  'primitives/pair.c',
  'primitives/port.c',
  'primitives/symbol.c',
  'primitives/vector.c',
)

scheme_inc = include_directories('include')

scheme_core = static_library('scheme_core',
  scheme_core_sources,
  include_directories: scheme_inc,
)

executable('scheme',
  'main.c',
  include_directories: scheme_inc,
  link_with: scheme_core,
  link_args: ['-lm'],
)
