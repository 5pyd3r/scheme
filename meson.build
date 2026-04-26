project('scheme', 'c',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ]
)

build_type = get_option('build_type')

if build_type == 'debug'
  add_project_arguments('-O0', '-g3', '-ggdb', language: 'c')
  add_project_arguments('-fsanitize=address,undefined', language: 'c')
  add_project_link_arguments('-fsanitize=address,undefined', language: 'c')
  add_project_arguments('-fno-omit-frame-pointer', language: 'c')
  add_project_arguments('-DSCHEME_DEBUG', language: 'c')
else
  add_project_arguments('-O2', language: 'c')
  add_project_arguments('-D_FORTIFY_SOURCE=2', language: 'c')
endif

add_project_arguments('-Wall', '-Wextra', '-Wno-unused-parameter', language: 'c')

subdir('src')
subdir('tests')
