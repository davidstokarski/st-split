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

option(STSPLIT_BUILD_TESTS "Build the whole-plugin test tool" OFF)
if(STSPLIT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
