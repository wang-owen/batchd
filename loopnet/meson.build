loopnet_inc = include_directories('include')
loopnet_lib = static_library(
    'loopnet',
    files(
        'src/acceptor.cc',
        'src/buffer.cc',
        'src/event_loop.cc',
        'src/fd_util.cc',
        'src/poller.cc',
        'src/tcp_connection.cc',
        'src/tcp_server.cc',
    ),
    include_directories: loopnet_inc,
)
loopnet_dep = declare_dependency(
    link_with: loopnet_lib,
    include_directories: loopnet_inc,
)

executable('loopnet_server', 'main.cc', dependencies: [loopnet_dep])
executable('loopnet_client', 'client.cc')
