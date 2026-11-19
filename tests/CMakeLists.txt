# Whole-plugin tests: engine alignment, hosting (Apple's built-in AUs), state, stress + editor snapshots (PNG).
#   StSplitPluginTests [--snapshot <dir>]
# Links the plugin's shared-code library, so it exercises the real processor.
juce_add_console_app(StSplitPluginTests PRODUCT_NAME "StSplitPluginTests")
target_sources(StSplitPluginTests PRIVATE PluginTests.cpp)
target_compile_features(StSplitPluginTests PRIVATE cxx_std_20)
target_link_libraries(StSplitPluginTests PRIVATE STSplit)
target_include_directories(StSplitPluginTests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../Source")
target_include_directories(StSplitPluginTests PRIVATE $<TARGET_PROPERTY:STSplit,INCLUDE_DIRECTORIES>)
target_compile_definitions(StSplitPluginTests PRIVATE $<TARGET_PROPERTY:STSplit,COMPILE_DEFINITIONS> $<TARGET_PROPERTY:STSplit,INTERFACE_COMPILE_DEFINITIONS>)
if(APPLE)
    target_link_libraries(StSplitPluginTests PRIVATE "-framework AudioToolbox" "-framework CoreAudioKit")
endif()
add_dependencies(StSplitPluginTests STSplitScanner)
# The helper as the plug-in finds it: inside the built AU bundle (Contents/Helpers), not the build tree.
add_dependencies(StSplitPluginTests STSplit_AU)
target_compile_definitions(StSplitPluginTests PRIVATE STSPLIT_AU_BUNDLE="$<TARGET_BUNDLE_DIR:STSplit_AU>")
