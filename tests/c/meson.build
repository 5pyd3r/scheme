test_scheme_inc = include_directories('../../src/include')

test_types = executable('test_types',
  'test_types.c',
  include_directories: test_scheme_inc,
  link_with: scheme_core,
  link_args: ['-lm'],
)
test('types', test_types)

test_gc = executable('test_gc',
  'test_gc.c',
  include_directories: test_scheme_inc,
  link_with: scheme_core,
  link_args: ['-lm'],
)
test('gc', test_gc)

test_number = executable('test_number',
  'test_number.c',
  include_directories: test_scheme_inc,
  link_with: scheme_core,
  link_args: ['-lm'],
)
test('number', test_number)

test_listvec = executable('test_listvec',
  'test_listvec.c',
  include_directories: test_scheme_inc,
  link_with: scheme_core,
  link_args: ['-lm'],
)
test('listvec', test_listvec)

test_char = executable('test_char',
  'test_char.c',
  include_directories: test_scheme_inc,
  link_with: scheme_core,
  link_args: ['-lm'],
)
test('char', test_char)

test_string = executable('test_string',
  'test_string.c',
  include_directories: test_scheme_inc,
  link_with: scheme_core,
  link_args: ['-lm'],
)
test('string', test_string)

test_bytevector = executable('test_bytevector',
  'test_bytevector.c',
  include_directories: test_scheme_inc,
  link_with: scheme_core,
  link_args: ['-lm'],
)
test('bytevector', test_bytevector)

test_closure = executable('test_closure',
  'test_closure.c',
  include_directories: test_scheme_inc,
  link_with: scheme_core,
  link_args: ['-lm'],
)
test('closure', test_closure)
