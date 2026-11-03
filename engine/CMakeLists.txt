add_library(zd_engine STATIC
    src/Engine.cpp
    src/Sequencer.cpp
    src/Strip.cpp
    src/gen/Generator.cpp
    src/gen/StyleKit.cpp
    src/PatternIO.cpp
    src/export/Export.cpp
    src/engines/AnalogKick.cpp
    src/engines/AnalogSnare.cpp
    src/engines/SamplePlayer.cpp
    src/engines/FmPerc.cpp
    src/engines/Sub808.cpp)

target_include_directories(zd_engine PUBLIC include)
target_compile_features(zd_engine PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(zd_engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(zd_engine PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()
