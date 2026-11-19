cmake_minimum_required(VERSION 3.22)
project(OpenFilterEQ VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

option(OF_BUILD_PLUGIN "Build the plugin (needs JUCE)"        ON)
option(OF_BUILD_TESTS  "Build unit tests"                     ON)
option(OF_BUILD_AAX    "Build AAX (needs AAX_SDK_PATH)"       OFF)

if(MSVC)
  set(OF_WARNINGS /W4)
else()
  set(OF_WARNINGS -Wall -Wextra -Wconversion -Wshadow -Wno-sign-conversion)
endif()

enable_testing()

# ---- DSP core: no JUCE, no GUI -------------------------------------------------
add_library(of_dsp STATIC
  src/dsp/design/Prototype.cpp
  src/dsp/design/MatchedDesign.cpp
  src/dsp/kernel/ChannelFilter.cpp
  src/dsp/Engine.cpp)
target_include_directories(of_dsp PUBLIC src)
target_compile_options(of_dsp PRIVATE ${OF_WARNINGS})

# ---- Plugin ---------------------------------------------------------------------
if(OF_BUILD_PLUGIN)
  add_subdirectory(extern/JUCE EXCLUDE_FROM_ALL)

  set(OF_FORMATS VST3 LV2 Standalone)
  if(APPLE)
    list(APPEND OF_FORMATS AU)
  endif()
  if(OF_BUILD_AAX)
    juce_set_aax_sdk_path("${AAX_SDK_PATH}")
    list(APPEND OF_FORMATS AAX)
  endif()

  juce_add_plugin(OpenFilterEQ
    PRODUCT_NAME                "OpenFilter EQ"
    COMPANY_NAME                "OpenFilter"
    BUNDLE_ID                   "org.openfilter.eq"
    PLUGIN_MANUFACTURER_CODE    Opfl
    PLUGIN_CODE                 OfEq
    FORMATS                     ${OF_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Fx EQ
    AU_MAIN_TYPE                kAudioUnitType_Effect
    AAX_CATEGORY                AAX_ePlugInCategory_EQ
    LV2URI                      "https://github.com/OWNER/openfilter/eq"   # placeholder: set before first release, permanent afterwards
    COPY_PLUGIN_AFTER_BUILD     FALSE)

  target_sources(OpenFilterEQ PRIVATE
    src/plugin/Parameters.cpp
    src/plugin/Presets.cpp
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.cpp
    src/ui/ResponseDisplay.cpp
    src/ui/SpectrumAnalyzer.cpp
    src/ui/BandDock.cpp
    src/ui/Dial.cpp)
  target_include_directories(OpenFilterEQ PRIVATE src)
  target_compile_definitions(OpenFilterEQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
  target_link_libraries(OpenFilterEQ
    PRIVATE of_dsp juce::juce_audio_utils juce::juce_dsp
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

  # Renders the editor to a PNG without a host and round-trips the state: tools/snapshot.cpp.
  # It reuses the plugin's own compiled code, so it borrows that target's include paths and
  # definitions instead of compiling the JUCE modules a second time.
  add_executable(of_snapshot tools/snapshot.cpp)
  target_include_directories(of_snapshot PRIVATE $<TARGET_PROPERTY:OpenFilterEQ,INCLUDE_DIRECTORIES>)
  target_compile_definitions(of_snapshot PRIVATE $<TARGET_PROPERTY:OpenFilterEQ,COMPILE_DEFINITIONS>)
  target_link_libraries(of_snapshot PRIVATE OpenFilterEQ)

  # A minimal host that loads the built VST3 and LV2 and exercises them:
  # tests/integration/host_test.cpp.
  if(OF_BUILD_TESTS)
    juce_add_console_app(of_host_test PRODUCT_NAME "of_host_test")
    target_sources(of_host_test PRIVATE tests/integration/host_test.cpp)
    target_compile_definitions(of_host_test PRIVATE
      JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_LV2=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(of_host_test PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
    add_dependencies(of_host_test OpenFilterEQ_VST3 OpenFilterEQ_LV2)
    add_test(NAME of_host_test COMMAND of_host_test
      "$<GENEX_EVAL:$<TARGET_PROPERTY:OpenFilterEQ_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>"
      "$<GENEX_EVAL:$<TARGET_PROPERTY:OpenFilterEQ_LV2,JUCE_PLUGIN_ARTEFACT_FILE>>")
    add_test(NAME of_snapshot COMMAND of_snapshot "${CMAKE_BINARY_DIR}/editor.png")
  endif()
endif()

# ---- Tests ----------------------------------------------------------------------
if(OF_BUILD_TESTS)
  add_subdirectory(extern/Catch2 EXCLUDE_FROM_ALL)
  add_executable(of_tests
    tests/unit/design_tests.cpp
    tests/unit/kernel_tests.cpp
    tests/unit/engine_tests.cpp
    tests/unit/natural_tests.cpp)
  target_link_libraries(of_tests PRIVATE of_dsp Catch2::Catch2WithMain)
  add_test(NAME of_tests COMMAND of_tests)
endif()
