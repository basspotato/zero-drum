add_executable(zd_tests
    main.cpp
    test_dsp.cpp
    test_engines.cpp
    test_sequencer.cpp
    test_engine.cpp
    test_phase1.cpp)

target_link_libraries(zd_tests PRIVATE zd_engine doctest::doctest)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(zd_tests)
