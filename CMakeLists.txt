cmake_minimum_required(VERSION 3.31)

if(APPLE)
    project(BlackThrum VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)
else()
    project(BlackThrum VERSION 0.1.0 LANGUAGES C CXX)
endif()

option(BLACKTHRUM_BUILD_PLUGIN "Build the YUP Standalone/VST3/AU targets" ON)
option(BLACKTHRUM_BUILD_TESTS "Build deterministic DSP regression tests" ON)
option(BLACKTHRUM_ADHOC_SIGN "Ad-hoc sign local macOS plugin and app bundles after building" ON)
set(BLACKTHRUM_CREATE_AU OFF)
if(APPLE)
    set(BLACKTHRUM_CREATE_AU ON)
endif()
set(BLACKTHRUM_YUP_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../yup" CACHE PATH
    "Path to a local YUP checkout; fetched from upstream when unavailable")

add_library(blackthrum_engine STATIC
    source/BlackThrumEngine.cpp)
target_include_directories(blackthrum_engine PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include")
target_compile_features(blackthrum_engine PUBLIC cxx_std_20)
set_target_properties(blackthrum_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(blackthrum_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BLACKTHRUM_BUILD_TESTS)
    include(CTest)

    function(enable_test_assertions target_name)
        if(MSVC)
            target_compile_options(${target_name} PRIVATE /UNDEBUG)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${target_name} PRIVATE -UNDEBUG)
        endif()
    endfunction()

    add_executable(blackthrum_engine_tests
        tests/BlackThrumEngineTests.cpp)
    target_link_libraries(blackthrum_engine_tests PRIVATE blackthrum_engine)
    target_compile_features(blackthrum_engine_tests PRIVATE cxx_std_20)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(blackthrum_engine_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    enable_test_assertions(blackthrum_engine_tests)
    add_test(NAME blackthrum_engine_tests COMMAND blackthrum_engine_tests)
endif()

if(BLACKTHRUM_BUILD_PLUGIN)
    set(YUP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(YUP_BUILD_PLUGINS OFF CACHE BOOL "" FORCE)
    set(YUP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_PLUGINVAL OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_VST3_VALIDATOR OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_CLAP_VALIDATOR OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_AUVAL_VALIDATOR OFF CACHE BOOL "" FORCE)

    if(EXISTS "${BLACKTHRUM_YUP_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${BLACKTHRUM_YUP_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/yup")
    else()
        include(FetchContent)
        FetchContent_Declare(yup
            GIT_REPOSITORY https://github.com/kunitoki/yup.git
            GIT_TAG 9a1c9bc699b6a714f6f52486462d98a140c8bf95)
        FetchContent_MakeAvailable(yup)
    endif()

    # YUP's iconutil-based helper rejects its generated iconset on current
    # macOS. Keep the compatibility fix project-local until upstream changes.
    if(APPLE)
        include(cmake/YupMacOSIconWorkaround.cmake)
    endif()

    yup_audio_plugin(
        TARGET_NAME blackthrum
        TARGET_VERSION ${PROJECT_VERSION}
        TARGET_IDE_GROUP "BlackThrum"
        TARGET_APP_ID "audio.2bit.blackthrum"
        TARGET_APP_NAMESPACE "audio.2bit"
        TARGET_CXX_STANDARD 20
        PLUGIN_ID "audio.2bit.BlackThrum"
        PLUGIN_NAME "BlackThrum"
        PLUGIN_VENDOR "2bit"
        PLUGIN_EMAIL ""
        PLUGIN_VERSION "${PROJECT_VERSION}"
        PLUGIN_DESCRIPTION "Three-carrier detuned thrum drone instrument"
        PLUGIN_URL "https://github.com/2bbb/BlackThrum"
        PLUGIN_IS_SYNTH ON
        PLUGIN_IS_MONO OFF
        PLUGIN_CREATE_CLAP OFF
        PLUGIN_CREATE_VST3 ON
        PLUGIN_CREATE_AU ${BLACKTHRUM_CREATE_AU}
        PLUGIN_AU_SUBTYPE "BlTh"
        PLUGIN_AU_MANUFACTURER "2Bit"
        PLUGIN_AU_SANDBOX_SAFE ON
        PLUGIN_CREATE_AUv3 OFF
        PLUGIN_CREATE_LV2 OFF
        PLUGIN_CREATE_AAX OFF
        PLUGIN_CREATE_STANDALONE ON
        PLUGIN_COPY_AFTER_BUILD OFF
        MODULES
            yup::yup_gui
            yup::yup_audio_processors)

    target_sources(blackthrum_shared INTERFACE
        source/ParameterGridEditor.cpp
        source/ParameterGridEditor.h
        source/ProductState.h
        source/BlackThrumPlugin.cpp
        source/BlackThrumPlugin.h)
    target_link_libraries(blackthrum_shared INTERFACE blackthrum_engine)
    target_include_directories(blackthrum_shared INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}/include"
        "${CMAKE_CURRENT_LIST_DIR}/source")

    if(BLACKTHRUM_BUILD_TESTS)
        add_executable(blackthrum_plugin_bridge_tests
            tests/BlackThrumPluginBridgeTests.cpp
            source/BlackThrumPlugin.cpp)
        target_link_libraries(blackthrum_plugin_bridge_tests PRIVATE blackthrum_engine yup::yup_audio_processors)
        target_include_directories(blackthrum_plugin_bridge_tests PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/include"
            "${CMAKE_CURRENT_LIST_DIR}/source")
        target_compile_definitions(blackthrum_plugin_bridge_tests PRIVATE
            BLACKTHRUM_HEADLESS_TEST=1
            BLACKTHRUM_SOURCE_DIR="${CMAKE_CURRENT_LIST_DIR}")
        target_compile_features(blackthrum_plugin_bridge_tests PRIVATE cxx_std_20)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(blackthrum_plugin_bridge_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        enable_test_assertions(blackthrum_plugin_bridge_tests)
        add_test(NAME blackthrum_plugin_bridge_tests COMMAND blackthrum_plugin_bridge_tests)
    endif()

    set(blackthrum_release_bundle_targets
        blackthrum_standalone_plugin
        blackthrum_vst3_plugin)
    if(APPLE)
        list(APPEND blackthrum_release_bundle_targets blackthrum_au_plugin)
    endif()

    add_custom_target(blackthrum_release_bundles
        DEPENDS ${blackthrum_release_bundle_targets})

    if(APPLE)
        set_target_properties(blackthrum_standalone_plugin PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER "audio.2bit.blackthrum"
            MACOSX_BUNDLE_BUNDLE_NAME "BlackThrum"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "audio.2bit.blackthrum")
    endif()

    if(APPLE AND BLACKTHRUM_ADHOC_SIGN)
        find_program(BLACKTHRUM_CODESIGN_EXECUTABLE codesign REQUIRED)
        add_custom_command(TARGET blackthrum_vst3_plugin POST_BUILD
            COMMAND "${BLACKTHRUM_CODESIGN_EXECUTABLE}" --force --deep --sign -
                    "$<TARGET_BUNDLE_DIR:blackthrum_vst3_plugin>"
            COMMENT "Ad-hoc signing BlackThrum VST3 for local loading"
            VERBATIM)
        add_custom_command(TARGET blackthrum_standalone_plugin POST_BUILD
            COMMAND "${BLACKTHRUM_CODESIGN_EXECUTABLE}" --force --deep --sign -
                    "$<TARGET_BUNDLE_DIR:blackthrum_standalone_plugin>"
            COMMENT "Ad-hoc signing BlackThrum standalone app for local launch"
            VERBATIM)
    endif()
endif()
