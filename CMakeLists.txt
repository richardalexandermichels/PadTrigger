cmake_minimum_required(VERSION 3.22)
project(PadTrigger VERSION 2.0.0 LANGUAGES C CXX)

include(FetchContent)
set(JUCE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/JUCE" CACHE PATH "Local JUCE source tree")
if(EXISTS "${JUCE_SOURCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/juce")
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 29396c22c93392d6738e021b83196283d6e4d850)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_binary_data(PadomizerAssets HEADER_NAME PadomizerAssets.h NAMESPACE PadomizerAssets
    SOURCES Assets/DSEG14ClassicMini-Regular.ttf)

juce_add_plugin(PadTrigger
    COMPANY_NAME "MyVST"
    PLUGIN_MANUFACTURER_CODE Myvs
    PLUGIN_CODE Pdtr
    PRODUCT_NAME "Pad Trigger"
    PLUGIN_NAME "Padomizer"
    FORMATS VST3
    VST3_CATEGORIES Fx Drum
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE)

target_sources(PadTrigger PRIVATE Source/PadTrigger.cpp Source/PadEditor.cpp Source/MidiNotes.cpp Source/PoolEditor.cpp)
target_compile_features(PadTrigger PRIVATE cxx_std_17)
target_compile_definitions(PadTrigger PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(PadTrigger PRIVATE
    PadomizerAssets
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_audio_devices
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

option(PADTRIGGER_BUILD_TESTS "Build Pad Trigger's low-gain and WAV recall checks" OFF)
if(PADTRIGGER_BUILD_TESTS)
    enable_testing()
    add_executable(PadTriggerTests Tests/PadTriggerTests.cpp)
    target_compile_features(PadTriggerTests PRIVATE cxx_std_17)
    target_include_directories(PadTriggerTests PRIVATE
        Source "$<TARGET_PROPERTY:PadTrigger,INCLUDE_DIRECTORIES>")
    target_compile_definitions(PadTriggerTests PRIVATE "$<TARGET_PROPERTY:PadTrigger,COMPILE_DEFINITIONS>")
    target_link_libraries(PadTriggerTests PRIVATE PadTrigger)
    add_test(NAME PadTriggerLowGain COMMAND PadTriggerTests)
endif()
