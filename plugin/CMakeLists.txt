juce_add_plugin(ZeroDrum
    PRODUCT_NAME "Zero Drum"
    COMPANY_NAME "Basspotato"
    BUNDLE_ID "com.basspotato.zerodrum"
    PLUGIN_MANUFACTURER_CODE Bspt
    PLUGIN_CODE ZrDm
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
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
