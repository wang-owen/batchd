project(
  'loopnet',
  'cpp',
  version: '0.1.0',
  license: 'MIT',
  meson_version: '>= 1.3.0',
  default_options: [
    'cpp_std=c++23',
    'warning_level=3',
    'werror=false',
    'buildtype=debugoptimized',
    'b_ndebug=if-release',
  ],
)

inc = include_directories('include')

loopnet_sources = files(
  'src/acceptor.cc',
  'src/buffer.cc',
  'src/event_loop.cc',
  'src/fd_util.cc',
  'src/poller.cc',
  'src/tcp_connection.cc',
)

loopnet_lib = static_library(
  'loopnet',
  loopnet_sources,
  include_directories: inc,
)

loopnet_dep = declare_dependency(
  include_directories: inc,
  link_with: loopnet_lib,
)

if get_option('examples')
  subdir('examples')
endif

if get_option('tests')
  subdir('tests')
endif
