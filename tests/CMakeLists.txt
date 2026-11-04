add_executable(zd_tests
    main.cpp
    test_dsp.cpp
    test_engines.cpp
    test_sequencer.cpp
    test_engine.cpp
    test_phase1.cpp
    test_generator.cpp
    test_export.cpp
    test_master.cpp
    test_clap.cpp
    test_crusher.cpp
    test_choke.cpp
    test_reverb.cpp
    test_melodic_kick.cpp
    test_glue.cpp)

find_package(Threads REQUIRED)
target_link_libraries(zd_tests PRIVATE zd_engine doctest::doctest Threads::Threads)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(zd_tests)
