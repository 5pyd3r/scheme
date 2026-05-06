subdir('c')

test_ss_files = files(
  'scheme/test-char.ss',
  'scheme/test-string.ss',
  'scheme/test-bytevector.ss',
  'scheme/test-closure.ss',
  'scheme/test-features.ss',
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
test('closure-ss', scheme_exe,
  args: [test_ss_files[3]],
  workdir: meson.project_source_root(),
)
test('features-ss', scheme_exe,
  args: [test_ss_files[4]],
  workdir: meson.project_source_root(),
)
