batchd_inc = include_directories('include')
batchd_deps = [allocator_dep, threadpool_dep, loopnet_dep]
batchd_lib = static_library(
    'batchd',
    files('src/batching_queue.cc', 'src/inference_engine.cc'),
    include_directories: batchd_inc,
    dependencies: batchd_deps,
)
batchd_dep = declare_dependency(
    link_with: batchd_lib,
    include_directories: batchd_inc,
    dependencies: batchd_deps,
)

executable(
    'batchd',
    'main.cc',
    dependencies: [batchd_dep, loopnet_dep],
)
executable('batchd_client', 'client.cc')
executable(
    'load_sweep',
    'benchmarks/load_sweep.cc',
    dependencies: [batchd_dep],
)

if build_tests
    test(
        'batchd',
        executable(
            'batchd_tests',
            files(
                'tests/batching_queue_test.cc',
                'tests/inference_engine_test.cc',
            ),
            dependencies: [batchd_dep, catch2_dep],
        ),
    )
endif

if build_benchmarks
    benchmark(
        'batchd',
        executable(
            'batchd_bench',
            'benchmarks/batching_bench.cc',
            dependencies: [batchd_dep, catch2_dep],
        ),
        args: ['[!benchmark]'],
        timeout: 1200,
    )
endif
