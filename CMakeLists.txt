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
option(OF_BUILD_CLAP   "Build CLAP (via clap-juce-extensions)" ON)
option(OF_INSTALL_AFTER_BUILD "Copy the built plugins to the user plugin folders" ON)

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
  src/dsp/lanes/FirLane.cpp
  src/dsp/lanes/SpectralLane.cpp
  src/match/CurveFitter.cpp
  src/dsp/Engine.cpp)
target_include_directories(of_dsp PUBLIC src)
target_compile_options(of_dsp PRIVATE ${OF_WARNINGS})
find_package(Threads REQUIRED)
target_link_libraries(of_dsp PUBLIC Threads::Threads)

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
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Fx EQ
    AU_MAIN_TYPE                kAudioUnitType_Effect
    AAX_CATEGORY                AAX_ePlugInCategory_EQ
    LV2URI                      "https://github.com/OWNER/openfilter/eq"   # placeholder: set before first release, permanent afterwards
    COPY_PLUGIN_AFTER_BUILD     ${OF_INSTALL_AFTER_BUILD})

  # Fonts (SIL Open Font License; the licence texts sit next to them in assets/fonts).
  juce_add_binary_data(OpenFilterAssets SOURCES
    assets/fonts/Inter-Regular.ttf
    assets/fonts/Inter-SemiBold.ttf
    assets/fonts/JetBrainsMono-Regular.ttf)
  set_target_properties(OpenFilterAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

  target_sources(OpenFilterEQ PRIVATE
    src/plugin/Parameters.cpp
    src/plugin/Presets.cpp
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.cpp
    src/ui/ResponseDisplay.cpp
    src/ui/SpectrumAnalyzer.cpp
    src/ui/AdvancedPanel.cpp
    src/ui/BandDock.cpp
    src/ui/BandTable.cpp
    src/ui/MatchPanel.cpp
    src/ui/InstancePanel.cpp
    src/hub/InstanceHub.cpp
    src/ui/Dial.cpp)
  target_include_directories(OpenFilterEQ PRIVATE src)
  target_compile_definitions(OpenFilterEQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
  target_link_libraries(OpenFilterEQ
    PRIVATE of_dsp OpenFilterAssets juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

  if(OF_BUILD_CLAP)
    add_subdirectory(extern/clap-juce-extensions EXCLUDE_FROM_ALL)
    clap_juce_extensions_plugin(TARGET OpenFilterEQ
      CLAP_ID "org.openfilter.eq"
      CLAP_FEATURES audio-effect equalizer stereo mono)
    # Like the other formats, copy the result to the user's plugin folder (Linux path).
    if(OF_INSTALL_AFTER_BUILD AND UNIX AND NOT APPLE)
      add_custom_command(TARGET OpenFilterEQ_CLAP POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{HOME}/.clap"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:OpenFilterEQ_CLAP>" "$ENV{HOME}/.clap/"
        VERBATIM)
    endif()
  endif()

  # Renders the editor to a PNG without a host and round-trips the state: tools/snapshot.cpp.
  # It reuses the plugin's own compiled code, so it borrows that target's include paths and
  # definitions instead of compiling the JUCE modules a second time.
  add_executable(of_snapshot tools/snapshot.cpp)
  target_include_directories(of_snapshot PRIVATE $<TARGET_PROPERTY:OpenFilterEQ,INCLUDE_DIRECTORIES>
                                                 $<TARGET_PROPERTY:OpenFilterAssets,INTERFACE_INCLUDE_DIRECTORIES>)
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

    # A minimal CLAP host for the CLAP build: tests/integration/clap_test.cpp. POSIX only.
    if(OF_BUILD_CLAP AND UNIX)
      add_executable(of_clap_test tests/integration/clap_test.cpp)
      target_include_directories(of_clap_test PRIVATE extern/clap-juce-extensions/clap-libs/clap/include)
      target_link_libraries(of_clap_test PRIVATE ${CMAKE_DL_LIBS})
      add_dependencies(of_clap_test OpenFilterEQ_CLAP)
      add_test(NAME of_clap_test COMMAND of_clap_test "$<TARGET_FILE:OpenFilterEQ_CLAP>")
    endif()
  endif()
endif()

# ---- Tests ----------------------------------------------------------------------
if(OF_BUILD_TESTS)
  add_subdirectory(extern/Catch2 EXCLUDE_FROM_ALL)
  add_executable(of_tests
    tests/unit/design_tests.cpp
    tests/unit/kernel_tests.cpp
    tests/unit/engine_tests.cpp
    tests/unit/natural_tests.cpp
    tests/unit/dynamics_tests.cpp
    tests/unit/linear_tests.cpp
    tests/unit/spectral_tests.cpp
    tests/unit/character_tests.cpp
    tests/unit/fit_tests.cpp)
  target_link_libraries(of_tests PRIVATE of_dsp Catch2::Catch2WithMain)
  add_test(NAME of_tests COMMAND of_tests)

  # CPU cost of the engine in typical situations: tests/bench/bench.cpp. Not part of ctest:
  # timings depend on the machine. Run ./of_bench, or ./of_bench --check against the budgets.
  add_executable(of_bench tests/bench/bench.cpp)
  target_link_libraries(of_bench PRIVATE of_dsp)
endif()
