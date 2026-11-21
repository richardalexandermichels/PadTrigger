cmake_minimum_required(VERSION 3.22)
project(PadTrigger VERSION 2.1.0 LANGUAGES C CXX)

include(FetchContent)
set(JUCE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/JUCE" CACHE PATH "Local JUCE source tree")
if(EXISTS "${JUCE_SOURCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/juce")
    set(padtrigger_juce_checkout "${JUCE_SOURCE_DIR}")
    set(padtrigger_juce_origin "local checkout")
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 29396c22c93392d6738e021b83196283d6e4d850)
    FetchContent_MakeAvailable(JUCE)
    set(padtrigger_juce_checkout "${juce_SOURCE_DIR}")
    set(padtrigger_juce_origin "pinned checkout")
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

if(WIN32)
    find_package(Git QUIET)
    if(Git_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${padtrigger_juce_checkout}" rev-parse HEAD
                        OUTPUT_VARIABLE padtrigger_juce_revision OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(NOT padtrigger_juce_revision AND IS_DIRECTORY "${padtrigger_juce_checkout}/.git")
            execute_process(COMMAND "${GIT_EXECUTABLE}" "--git-dir=${padtrigger_juce_checkout}/.git" rev-parse HEAD
                            OUTPUT_VARIABLE padtrigger_juce_revision OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        endif()
    endif()
    if(NOT padtrigger_juce_revision)
        set(padtrigger_juce_revision "unknown")
    endif()
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/padomizer-$<CONFIG>-build.txt" CONTENT
        "Padomizer ${PROJECT_VERSION}\nConfiguration: $<CONFIG>\nGenerator: ${CMAKE_GENERATOR}\nCMake: ${CMAKE_VERSION}\nCompiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\nJUCE: ${padtrigger_juce_revision} (${padtrigger_juce_origin})\n")
    get_target_property(padtrigger_vst3 PadTrigger_VST3 JUCE_PLUGIN_ARTEFACT_FILE)
    install(DIRECTORY "${padtrigger_vst3}" DESTINATION . COMPONENT Padomizer)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/padomizer-$<CONFIG>-build.txt" README.md DESIGN.md
        DESTINATION . COMPONENT Padomizer)
    install(FILES Assets/DSEG-LICENSE.txt DESTINATION Assets COMPONENT Padomizer)
    install(FILES "Padomizer - Melodic Pluck.RTrackTemplate" "Padomizer - Bass Pulse.RTrackTemplate"
        "Padomizer - Mallet Answer.RTrackTemplate"
        "Padomizer - One-track MIDI.RTrackTemplate" "Padomizer - Single drum note.RTrackTemplate"
        "PadTrigger - One-track stereo.RTrackTemplate" "PadTrigger - sforzando Stereo Print.RTrackTemplate"
        DESTINATION TrackTemplates COMPONENT Padomizer)
    install(FILES "REAPER/PadTrigger - Record audio.lua" "REAPER/PadTrigger - Set up stereo print.lua"
        DESTINATION Scripts COMPONENT Padomizer)
endif()

option(PADTRIGGER_BUILD_TESTS "Build the Padomizer processor and editor checks" OFF)
if(PADTRIGGER_BUILD_TESTS)
    enable_testing()
    add_executable(PadTriggerTests Tests/PadTriggerTests.cpp)
    target_compile_features(PadTriggerTests PRIVATE cxx_std_17)
    target_include_directories(PadTriggerTests PRIVATE
        Source "$<TARGET_PROPERTY:PadTrigger,INCLUDE_DIRECTORIES>")
    target_compile_definitions(PadTriggerTests PRIVATE "$<TARGET_PROPERTY:PadTrigger,COMPILE_DEFINITIONS>")
    target_link_libraries(PadTriggerTests PRIVATE PadTrigger)
    add_test(NAME PadomizerProcessorAndEditor COMMAND PadTriggerTests)
endif()
