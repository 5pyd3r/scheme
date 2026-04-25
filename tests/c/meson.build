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
