juce_add_plugin(ZeroDrum
    PRODUCT_NAME "Zero Drum"
    COMPANY_NAME "Basspotato"
    BUNDLE_ID "com.basspotato.zerodrum"
    PLUGIN_MANUFACTURER_CODE Bspt
    PLUGIN_CODE ZrDm
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Drum
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    FORMATS Standalone VST3 AU)

clap_juce_extensions_plugin(TARGET ZeroDrum
    CLAP_ID "com.basspotato.zerodrum"
    CLAP_FEATURES instrument drum-machine stereo)

target_sources(ZeroDrum PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/PadGrid.cpp
    src/StepGrid.cpp)

target_compile_definitions(ZeroDrum PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ZeroDrum
    PRIVATE
        zd_engine
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Plugin-level tests (the JUCE shell, headless). Built with the engine tests.
if(ZD_BUILD_TESTS)
    juce_add_console_app(zd_plugin_tests PRODUCT_NAME "zd_plugin_tests")
    target_sources(zd_plugin_tests PRIVATE tests/test_midi_out.cpp)
    target_include_directories(zd_plugin_tests PRIVATE src)
    target_compile_definitions(zd_plugin_tests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(zd_plugin_tests PRIVATE ZeroDrum zd_engine juce::juce_audio_utils doctest::doctest)
    add_test(NAME zd_plugin_tests COMMAND zd_plugin_tests)
endif()
