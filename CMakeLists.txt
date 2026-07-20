cmake_minimum_required(VERSION 3.25)

project(MasteringAudioSuite VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Short git SHA for product version string (0.4.0-alpha.m4b+abcdef0).
set(MASTERING_AUDIO_GIT_SHA "unknown")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE MASTERING_AUDIO_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()

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
    modules/audio-analysis/src/LoudnessMeter.cpp
    modules/audio-analysis/src/SpectralAnalysis.cpp
    modules/audio-analysis/src/StreamingAnalyzer.cpp
    modules/audio-analysis/src/Sha256.cpp
    modules/audio-analysis/src/AnalysisFingerprint.cpp
    modules/dsp/src/ProcessorChain.cpp
    modules/dsp/src/DynamicTools.cpp
    modules/dsp/src/Oversampler.cpp
    modules/dsp/src/NonlinearProcessors.cpp
    modules/dsp/src/MasterSafetyChain.cpp
    modules/dsp/src/ExportQc.cpp
    modules/dsp/src/EnvelopeDetector.cpp
    modules/dsp/src/DynamicEq.cpp
    modules/dsp/src/MixNodeChain.cpp
    modules/dsp/src/ParallelCompressor.cpp
    modules/dsp/src/StereoWidth.cpp
    modules/assistant/src/MixAdvisor.cpp
    modules/assistant/src/MetalcoreMixPass.cpp
    modules/assistant/src/MetalcoreAnalysis.cpp
    modules/assistant/src/ActionResolver.cpp
    modules/assistant/src/SectionAutomation.cpp
    modules/assistant/src/EvidenceModel.cpp
    modules/assistant/src/ActionBudget.cpp
    modules/assistant/src/LoudnessMatch.cpp
    modules/assistant/src/RenderIdentity.cpp
    modules/assistant/src/AutoApplyPolicy.cpp
    modules/assistant/src/RealtimeOfflineCompare.cpp
    modules/assistant/src/MetalcoreProfile.cpp
    modules/product-version/src/ProductVersion.cpp
    modules/benchmark/src/BenchmarkSessionManifest.cpp
    modules/benchmark/src/ImportValidator.cpp
    modules/benchmark/src/ActionGraphDiff.cpp
    modules/benchmark/src/ExperimentRunner.cpp
    modules/benchmark/src/AblationRunner.cpp
    modules/benchmark/src/ListeningPackageV2.cpp
    modules/benchmark/src/UserEditCapture.cpp
    modules/benchmark/src/ObjectiveReport.cpp
    modules/benchmark/src/GoldenRegression.cpp
    modules/benchmark/src/CalibrationWorksheet.cpp
    modules/benchmark/src/RenderRetention.cpp
    modules/reliability/src/TypedError.cpp
    modules/reliability/src/AtomicFile.cpp
    modules/reliability/src/AutosaveRecovery.cpp
    modules/reliability/src/SchemaMigration.cpp
    modules/reliability/src/AssetRelinker.cpp
    modules/reliability/src/PortablePackage.cpp
    modules/reliability/src/JobSystem.cpp
    modules/reliability/src/RenderIntegrity.cpp
    modules/reliability/src/DiskMemory.cpp
    modules/reliability/src/Diagnostics.cpp
    modules/reliability/src/ProjectLock.cpp
    modules/reliability/src/SettingsStore.cpp
    modules/reliability/src/ExportPolicy.cpp
    modules/reliability/src/IpcHardening.cpp
    modules/dsp/src/VocalRider.cpp
    modules/project-bridge/src/ProjectDocument.cpp
    modules/research-export/src/ResearchExample.cpp
    modules/ipc/src/BridgeProtocol.cpp
    modules/ipc/src/MixNodeProtocol.cpp
    modules/ipc/src/MixNodeController.cpp
)

target_include_directories(mastering_audio_core
    PUBLIC
        modules/audio-analysis/include
        modules/dsp/include
        modules/assistant/include
        modules/project-bridge/include
        modules/research-export/include
        modules/ipc/include
        modules/product-version/include
        modules/benchmark/include
        modules/reliability/include
)

target_compile_features(mastering_audio_core PUBLIC cxx_std_23)
target_link_libraries(mastering_audio_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_definitions(mastering_audio_core
    PUBLIC
        MASTERING_AUDIO_GIT_SHA="${MASTERING_AUDIO_GIT_SHA}"
        MASTERING_AUDIO_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        MASTERING_AUDIO_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        MASTERING_AUDIO_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
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
        tests/LoudnessMeterTests.cpp
        tests/TruePeakAndLoudnessHardeningTests.cpp
        tests/TruePeakFirTailTests.cpp
        tests/OfficialVectorTests.cpp
        tests/KWeightFrequencyResponseTests.cpp
        tests/AnalyzerRealtimeSafetyTests.cpp
        tests/MeteringValidationTests.cpp
        tests/ProcessorChainTests.cpp
        tests/ProjectDocumentTests.cpp
        tests/RenderPathTests.cpp
        tests/ResearchExampleTests.cpp
        tests/Milestone0RegressionTests.cpp
        tests/OversamplerTests.cpp
        tests/MasterSafetyDspTests.cpp
        tests/TruePeakLimiterM1BTests.cpp
        tests/M1BVerificationTests.cpp
        tests/DynamicEqTests.cpp
        tests/MixNodeTests.cpp
        tests/MetalcoreMixPassTests.cpp
        tests/MetalcoreEngineM3BTests.cpp
        tests/MetalcoreEngineM3CTests.cpp
        tests/MetalcoreEngineM3CVerificationTests.cpp
        tests/Milestone4ATests.cpp
        tests/Milestone4BTests.cpp
    )
    target_link_libraries(mastering_audio_tests
        PRIVATE
            mastering_audio_core
            Catch2::Catch2WithMain
    )

    add_executable(dsp_validation_render
        tools/dsp-validation-render/main.cpp
    )
    target_link_libraries(dsp_validation_render PRIVATE mastering_audio_core)

    add_executable(dynamic_processing_validation
        tools/dynamic-processing-validation/main.cpp
    )
    target_link_libraries(dynamic_processing_validation PRIVATE mastering_audio_core)

    add_executable(mix_node_validation
        tools/mix-node-validation/main.cpp
    )
    target_link_libraries(mix_node_validation PRIVATE mastering_audio_core)

    add_executable(mixpass_validation
        tools/mixpass-validation/main.cpp
    )
    target_link_libraries(mixpass_validation PRIVATE mastering_audio_core)

    add_executable(metalcore_engine_validation
        tools/metalcore-engine-validation/main.cpp
    )
    target_link_libraries(metalcore_engine_validation PRIVATE mastering_audio_core)

    add_executable(blind_test_package
        tools/blind-test-package/main.cpp
    )
    target_link_libraries(blind_test_package PRIVATE mastering_audio_core)

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
        VERSION ${PROJECT_VERSION}
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

    juce_add_plugin(MasteringAudioMixNode
        COMPANY_NAME "Mastering Audio"
        BUNDLE_ID "audio.mastering.mixnode"
        PLUGIN_MANUFACTURER_CODE MsAu
        PLUGIN_CODE MsMn
        FORMATS VST3
        VERSION ${PROJECT_VERSION}
        PRODUCT_NAME "Mastering Audio Mix Node"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        NEEDS_WEB_BROWSER TRUE
        NEEDS_WEBVIEW2 TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx
    )
    target_sources(MasteringAudioMixNode PRIVATE
        apps/mix-node-plugin/PluginProcessor.cpp
        apps/mix-node-plugin/PluginEditor.cpp
        apps/shared/WebViewComponent.cpp
        apps/shared/BridgeClient.cpp
    )
    target_include_directories(MasteringAudioMixNode PRIVATE apps)
    target_link_libraries(MasteringAudioMixNode
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
        VERSION ${PROJECT_VERSION}
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

    foreach(target MasteringAudioAnalyzer MasteringAudioMixNode MasteringAudioSuite)
        target_compile_definitions(${target} PRIVATE
            JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
            JUCE_DISPLAY_SPLASH_SCREEN=0
        )
    endforeach()

    target_compile_definitions(MasteringAudioAnalyzer PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)
    target_compile_definitions(MasteringAudioMixNode PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(MasteringAudioAnalyzer PRIVATE juce::juce_events)
    target_link_libraries(MasteringAudioMixNode PRIVATE juce::juce_events)
    target_link_libraries(MasteringAudioSuite PRIVATE juce::juce_events)

    install(TARGETS MasteringAudioSuite RUNTIME DESTINATION .)
    set(CPACK_PACKAGE_NAME "Mastering Audio Suite")
    set(CPACK_PACKAGE_VENDOR "Mastering Audio")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_GENERATOR "ZIP")
    include(CPack)
endif()
