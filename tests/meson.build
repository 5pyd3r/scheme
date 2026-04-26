subdir('c')

test_ss_files = files(
  'scheme/test-char.ss',
  'scheme/test-string.ss',
  'scheme/test-bytevector.ss',
)

test('char-ss', scheme_exe,
  args: [test_ss_files[0]],
  workdir: meson.project_source_root(),
)
test('string-ss', scheme_exe,
  args: [test_ss_files[1]],
  workdir: meson.project_source_root(),
)
test('bytevector-ss', scheme_exe,
  args: [test_ss_files[2]],
  workdir: meson.project_source_root(),
)
