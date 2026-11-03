# Third-party dependencies, pinned to exact versions.
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

if(ZD_BUILD_PLUGIN)
    # JUCE is used under its commercial licence terms (Zero Drum is closed source).
    # Check the JUCE licence tier before distributing builds.
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.5
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(juce)

    # JUCE has no native CLAP target yet; this adds one on top of the JUCE plugin target.
    # Pinned to a main-branch commit (2026-10-06): JUCE 9 support landed after the 0.26.0 tag.
    FetchContent_Declare(clap_juce_extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG        7adee3a1bd4684d4caa5601100e364abccff4b4d
        GIT_SUBMODULES_RECURSE TRUE)
    FetchContent_MakeAvailable(clap_juce_extensions)
endif()

if(ZD_BUILD_TESTS)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.12
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(doctest)
endif()
