cmake_minimum_required(VERSION 3.20)
project(OpenQLab VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

list(APPEND CMAKE_PREFIX_PATH "/usr/lib/cmake/Qt6")

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Multimedia MultimediaWidgets)

qt_add_executable(openqlab
    src/main.cpp
    src/engine/Cue.h
    src/engine/Cue.cpp
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
)

target_link_libraries(openqlab PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
)

target_include_directories(openqlab PRIVATE src)

set_target_properties(openqlab PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE   TRUE
)
