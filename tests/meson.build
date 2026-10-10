catch2_dep = dependency(
  'catch2-with-main',
  default_options: ['tests=false', 'install=false'],
)

test_sources = files(
  # 'test_event_loop.cpp',
)

if test_sources.length() > 0
  test_exe = executable(
    'loopnet_tests',
    test_sources,
    dependencies: [loopnet_dep, catch2_dep],
  )

  test('loopnet_tests', test_exe, protocol: 'tap', args: ['--reporter', 'tap'])
endif
