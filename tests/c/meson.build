scheme_inc = include_directories('../../src/include')

test_types = executable('test_types',
  'test_types.c',
  include_directories: scheme_inc,
)
test('types', test_types)

test_gc = executable('test_gc',
  'test_gc.c',
  include_directories: scheme_inc,
)
test('gc', test_gc)
