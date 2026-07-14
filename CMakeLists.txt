cmake_minimum_required(VERSION 3.25)

project(MasteringAudioSuite VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MASTERING_AUDIO_BUILD_PRODUCTS "Build the JUCE VST3 and desktop products" ON)
option(MASTERING_AUDIO_BUILD_TESTS "Build native unit tests" ON)

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(mastering_audio_core STATIC
    modules/audio-analysis/src/AudioAnalyzer.cpp
    modules/dsp/src/ProcessorChain.cpp
    modules/dsp/src/DynamicTools.cpp
    modules/assistant/src/MixAdvisor.cpp
    modules/project-bridge/src/ProjectDocument.cpp
    modules/research-export/src/ResearchExample.cpp
    modules/ipc/src/BridgeProtocol.cpp
)

target_include_directories(mastering_audio_core
    PUBLIC
        modules/audio-analysis/include
        modules/dsp/include
        modules/assistant/include
        modules/project-bridge/include
        modules/research-export/include
        modules/ipc/include
)

target_compile_features(mastering_audio_core PUBLIC cxx_std_23)
target_link_libraries(mastering_audio_core PUBLIC nlohmann_json::nlohmann_json)
set_target_properties(mastering_audio_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(mastering_audio_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(mastering_audio_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MASTERING_AUDIO_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.15.2
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(mastering_audio_tests
        tests/AudioAnalyzerTests.cpp
        tests/ProcessorChainTests.cpp
        tests/ProjectDocumentTests.cpp
        tests/RenderPathTests.cpp
        tests/ResearchExampleTests.cpp
        tests/Milestone0RegressionTests.cpp
    )
    target_link_libraries(mastering_audio_tests
        PRIVATE
            mastering_audio_core
            Catch2::Catch2WithMain
    )

    include(CTest)
    include(Catch)
    catch_discover_tests(mastering_audio_tests)
endif()

if(MASTERING_AUDIO_BUILD_PRODUCTS)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.14
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)

    file(GLOB_RECURSE MASTERING_AUDIO_UI_ASSETS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/dist/*"
    )
    if(NOT MASTERING_AUDIO_UI_ASSETS)
        message(FATAL_ERROR "UI assets are missing. Run `npm ci && npm run build` before CMake.")
    endif()

    juce_add_binary_data(MasteringAudioUI SOURCES ${MASTERING_AUDIO_UI_ASSETS})

    juce_add_plugin(MasteringAudioAnalyzer
        COMPANY_NAME "Mastering Audio"
        BUNDLE_ID "audio.mastering.analyzer"
        PLUGIN_MANUFACTURER_CODE MsAu
        PLUGIN_CODE MsAn
        FORMATS VST3
        PRODUCT_NAME "Mastering Audio Analyzer"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        NEEDS_WEB_BROWSER TRUE
        NEEDS_WEBVIEW2 TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
    )
    target_sources(MasteringAudioAnalyzer PRIVATE
        apps/analyzer-plugin/PluginProcessor.cpp
        apps/analyzer-plugin/PluginEditor.cpp
        apps/shared/WebViewComponent.cpp
        apps/shared/BridgeClient.cpp
    )
    target_include_directories(MasteringAudioAnalyzer PRIVATE apps)
    target_link_libraries(MasteringAudioAnalyzer
        PRIVATE
            mastering_audio_core
            MasteringAudioUI
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )

    juce_add_gui_app(MasteringAudioSuite
        COMPANY_NAME "Mastering Audio"
        BUNDLE_ID "audio.mastering.suite"
        PRODUCT_NAME "Mastering Audio Suite"
        NEEDS_WEB_BROWSER TRUE
        NEEDS_WEBVIEW2 TRUE
    )
    target_sources(MasteringAudioSuite PRIVATE
        apps/mix-desktop/Main.cpp
        apps/mix-desktop/MainComponent.cpp
        apps/mix-desktop/StemEngine.cpp
        apps/mix-desktop/BridgeServer.cpp
        apps/shared/WebViewComponent.cpp
    )
    target_include_directories(MasteringAudioSuite PRIVATE apps)
    target_link_libraries(MasteringAudioSuite
        PRIVATE
            mastering_audio_core
            MasteringAudioUI
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )

    foreach(target MasteringAudioAnalyzer MasteringAudioSuite)
        target_compile_definitions(${target} PRIVATE
            JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
            JUCE_DISPLAY_SPLASH_SCREEN=0
        )
    endforeach()

    target_compile_definitions(MasteringAudioAnalyzer PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(MasteringAudioAnalyzer PRIVATE juce::juce_events)
    target_link_libraries(MasteringAudioSuite PRIVATE juce::juce_events)

    install(TARGETS MasteringAudioSuite RUNTIME DESTINATION .)
    set(CPACK_PACKAGE_NAME "Mastering Audio Suite")
    set(CPACK_PACKAGE_VENDOR "Mastering Audio")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_GENERATOR "ZIP")
    include(CPack)
endif()
