cmake_minimum_required(VERSION 3.20)
project(OpenQLab VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

list(APPEND CMAKE_PREFIX_PATH "/usr/lib/cmake/Qt6")

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Multimedia MultimediaWidgets Svg Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LILV REQUIRED lilv-0)
if(UNIX AND NOT APPLE)
    pkg_check_modules(SUIL REQUIRED suil-0)
endif()

qt_add_executable(openqlab
    resources/resources.qrc
    src/main.cpp
    src/engine/Cue.h
    src/engine/Cue.cpp
    src/engine/AudioPlugin.h
    src/engine/VstPlugin.h
    src/engine/VstPlugin.cpp
    src/engine/Lv2Plugin.h
    src/engine/Lv2Plugin.cpp
    src/engine/PluginChain.h
    src/engine/PluginChain.cpp
    src/engine/AudioEngine.h
    src/engine/AudioEngine.cpp
    src/engine/miniaudio_impl.cpp
    src/engine/AudioCue.h
    src/engine/AudioCue.cpp
    src/engine/VideoCue.h
    src/engine/VideoCue.cpp
    src/engine/CueList.h
    src/engine/CueList.cpp
    src/engine/ControlCues.h
    src/engine/ControlCues.cpp
    src/engine/MicCue.h
    src/engine/MicCue.cpp
    src/engine/GroupCue.h
    src/engine/GroupCue.cpp
    src/engine/LabelCue.h
    src/engine/LabelCue.cpp
    src/engine/TextCue.h
    src/engine/TextCue.cpp
    src/engine/Workspace.h
    src/engine/Workspace.cpp
    src/engine/AppSettings.h
    src/engine/AppSettings.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/CueListModel.h
    src/ui/CueListModel.cpp
    src/ui/CueListView.h
    src/ui/CueListView.cpp
    src/ui/InspectorPanel.h
    src/ui/InspectorPanel.cpp
    src/ui/VideoOutputWindow.h
    src/ui/VideoOutputWindow.cpp
    src/ui/TextOutputWindow.h
    src/ui/TextOutputWindow.cpp
    src/ui/CueInfoBar.h
    src/ui/CueInfoBar.cpp
    src/ui/ActiveCuesPanel.h
    src/ui/ActiveCuesPanel.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.cpp
    src/ui/WaveformView.h
    src/ui/WaveformView.cpp
    src/ui/PluginChainWidget.h
    src/ui/PluginChainWidget.cpp
    src/ui/WebServer.h
    src/ui/WebServer.cpp
)

target_link_libraries(openqlab PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Svg
    Qt6::Network
    ${LILV_LIBRARIES}
)
if(UNIX AND NOT APPLE)
    target_link_libraries(openqlab PRIVATE ${SUIL_LIBRARIES})
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(openqlab PRIVATE
        dl       # dlopen/dlsym for VST2 loading
        pthread  # std::mutex/thread in AudioEngine
        X11      # XSetErrorHandler + XQueryTree (LV2/VST2 UI)
    )
elseif(APPLE)
    target_link_libraries(openqlab PRIVATE
        "-framework CoreAudio"
        "-framework CoreFoundation"
        "-framework AudioUnit"
        "-framework AudioToolbox"
    )
endif()

target_include_directories(openqlab PRIVATE
    src
    ${LILV_INCLUDE_DIRS}
    $<$<NOT:$<BOOL:${APPLE}>>:${SUIL_INCLUDE_DIRS}>
)

target_compile_options(openqlab PRIVATE
    ${LILV_CFLAGS_OTHER}
    $<$<NOT:$<BOOL:${APPLE}>>:${SUIL_CFLAGS_OTHER}>
)

# miniaudio needs these on Linux
target_compile_definitions(openqlab PRIVATE
    MA_NO_VORBIS    # use built-in vorbis if needed (stb_vorbis not included here)
)

set_target_properties(openqlab PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE   TRUE
)
if(APPLE)
    set_target_properties(openqlab PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/MacOSXBundleInfo.plist.in"
    )
endif()
