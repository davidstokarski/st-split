cmake_minimum_required(VERSION 3.24)
project(STSplit VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# ST Shell: use a sibling checkout (../st-shell) when present, otherwise fetch
# the private repo. Override with -DST_SHELL_DIR=/path/to/st-shell.
# ---------------------------------------------------------------------------
set(ST_SHELL_DIR "" CACHE PATH "Path to a local st-shell checkout")
set(ST_SHELL_TAG "main" CACHE STRING "st-shell git ref to fetch")

if(ST_SHELL_DIR STREQUAL "" AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/../st-shell/CMakeLists.txt")
    set(ST_SHELL_DIR "${CMAKE_CURRENT_LIST_DIR}/../st-shell")
endif()

if(NOT ST_SHELL_DIR STREQUAL "")
    message(STATUS "Using local st-shell at ${ST_SHELL_DIR}")
    add_subdirectory("${ST_SHELL_DIR}" "${CMAKE_BINARY_DIR}/_deps/st-shell-build")
else()
    include(FetchContent)
    FetchContent_Declare(st_shell
        GIT_REPOSITORY https://github.com/davidstokarski/st-shell.git
        GIT_TAG        ${ST_SHELL_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(st_shell)
endif()

file(GLOB_RECURSE STSPLIT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Source/*.h")

# JUCE's sign + copy-to-~/Library step is added below, after the scanner helper has been embedded in the bundles.
set(STSPLIT_COPY_AFTER_BUILD ${ST_COPY_AFTER_BUILD})
set(ST_COPY_AFTER_BUILD OFF)

st_add_plugin(STSplit
    PRODUCT_NAME  "ST Split"
    PLUGIN_CODE   Stsp
    VERSION       ${PROJECT_VERSION}
    BUNDLE_SUFFIX stsplit
    SOURCES       ${STSPLIT_SOURCES})

target_include_directories(STSplit PRIVATE "${CMAKE_CURRENT_LIST_DIR}/Source")

# ST Split hosts the user's own Audio Units and VST3s inside each band.
target_compile_definitions(STSplit PUBLIC
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_LADSPA=0)

if(APPLE)
    target_link_libraries(STSplit PRIVATE "-framework AudioToolbox" "-framework CoreAudioKit")
endif()

# ---------------------------------------------------------------------------
# VST3 scanner helper. Listing a VST3 means loading it; doing that inside the DAW lets one broken plug-in crash
# the DAW. ST Split runs this helper (one process per bundle) instead, from <bundle>/Contents/Helpers.
# Not built with the hardened runtime: it must be able to load any third-party plug-in.
# ---------------------------------------------------------------------------
juce_add_console_app(STSplitScanner PRODUCT_NAME "ST Split Scanner")
set_target_properties(STSplitScanner PROPERTIES OUTPUT_NAME "ST Split Scanner")   # the name PluginCatalog looks for
target_sources(STSplitScanner PRIVATE scanner/Main.cpp)
target_compile_features(STSplitScanner PRIVATE cxx_std_20)
target_compile_definitions(STSplitScanner PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_PLUGINHOST_VST=0 JUCE_PLUGINHOST_LV2=0 JUCE_PLUGINHOST_LADSPA=0
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0)
target_link_libraries(STSplitScanner
    PRIVATE juce::juce_audio_processors_headless
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

foreach(format AU VST3 Standalone)
    if(TARGET STSplit_${format})
        add_dependencies(STSplit_${format} STSplitScanner)
        add_custom_command(TARGET STSplit_${format} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_BUNDLE_DIR:STSplit_${format}>/Contents/Helpers"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:STSplit_${format}>/Contents/Helpers"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:STSplitScanner>" "$<TARGET_BUNDLE_DIR:STSplit_${format}>/Contents/Helpers/"
            VERBATIM)
    endif()
endforeach()

if(STSPLIT_COPY_AFTER_BUILD)
    juce_enable_copy_plugin_step(STSplit)   # re-signs the bundles (the helper changed them), then installs
endif()

option(STSPLIT_BUILD_TESTS "Build the whole-plugin test tool" OFF)
if(STSPLIT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
