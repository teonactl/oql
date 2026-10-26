cmake_minimum_required(VERSION 3.20)
project(OQL VERSION 0.1.13 LANGUAGES CXX)

set(OQL_EDITION "full" CACHE STRING "Build edition: full or base")
set_property(CACHE OQL_EDITION PROPERTY STRINGS full base)

# Harness di test standalone per la logica di engine/ (nessuna GUI, nessun
# device audio reale toccato) — vedi tests/README.md. Di default OFF: non
# tocca né il target `oql` né la CI esistente.
option(OQL_BUILD_TESTS "Build the standalone engine test harness (tests/oql_tests)" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(UNIX AND NOT APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/usr/lib/cmake/Qt6")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Multimedia MultimediaWidgets Svg Network Qml LinguistTools Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LILV REQUIRED lilv-0)
pkg_check_modules(SOUNDTOUCH soundtouch)
if(UNIX AND NOT APPLE)
    pkg_check_modules(SUIL REQUIRED suil-0)
endif()

qt_add_executable(oql
    resources/resources.qrc
    src/main.cpp
    src/engine/Cue.h
    src/engine/Cue.cpp
    src/engine/AudioPlugin.h
    src/engine/BuiltinPlugins.h
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
    src/engine/ProjectionWarp.h
    src/engine/ProjectionWarp.cpp
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
    src/engine/ImageCue.h
    src/engine/ImageCue.cpp
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
    src/ui/CueRowDelegate.h
    src/ui/CueRowDelegate.cpp
    src/ui/InspectorPanel.h
    src/ui/InspectorPanel.cpp
    src/ui/WarpCalibrationOverlay.h
    src/ui/WarpCalibrationOverlay.cpp
    src/ui/VideoOutputWindow.h
    src/ui/VideoOutputWindow.cpp
    src/ui/TextOutputWindow.h
    src/ui/TextOutputWindow.cpp
    src/ui/ImageOutputWindow.h
    src/ui/ImageOutputWindow.cpp
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
    src/engine/ScriptEngine.h
    src/engine/ScriptEngine.cpp
    src/engine/ScriptCue.h
    src/engine/ScriptCue.cpp
    src/ui/ScriptEditorDialog.h
    src/ui/ScriptEditorDialog.cpp
    src/ui/AboutDialog.h
    src/ui/AboutDialog.cpp
    src/engine/RecordCue.h
    src/engine/RecordCue.cpp
)

if(WIN32)
    target_sources(oql PRIVATE resources/oql.rc)
endif()

target_link_libraries(oql PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Svg
    Qt6::Network
    Qt6::Qml
    ${LILV_LIBRARIES}
)
if(SOUNDTOUCH_FOUND)
    target_link_libraries(oql PRIVATE ${SOUNDTOUCH_LIBRARIES})
    target_include_directories(oql PRIVATE ${SOUNDTOUCH_INCLUDE_DIRS})
    target_compile_definitions(oql PRIVATE HAVE_SOUNDTOUCH)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(oql PRIVATE ${SUIL_LIBRARIES})
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(oql PRIVATE
        dl       # dlopen/dlsym for VST2 loading
        pthread  # std::mutex/thread in AudioEngine
        X11      # XSetErrorHandler + XQueryTree (LV2/VST2 UI)
    )
elseif(APPLE)
    target_link_libraries(oql PRIVATE
        "-framework CoreAudio"
        "-framework CoreFoundation"
        "-framework AudioUnit"
        "-framework AudioToolbox"
    )
endif()

target_include_directories(oql PRIVATE
    src
    ${LILV_INCLUDE_DIRS}
    $<$<NOT:$<BOOL:${APPLE}>>:${SUIL_INCLUDE_DIRS}>
)

target_link_directories(oql PRIVATE
    ${LILV_LIBRARY_DIRS}
    ${SOUNDTOUCH_LIBRARY_DIRS}
    $<$<NOT:$<BOOL:${APPLE}>>:${SUIL_LIBRARY_DIRS}>
)

target_compile_options(oql PRIVATE
    ${LILV_CFLAGS_OTHER}
    $<$<NOT:$<BOOL:${APPLE}>>:${SUIL_CFLAGS_OTHER}>
)

# miniaudio needs these on Linux
target_compile_definitions(oql PRIVATE
    MA_NO_VORBIS    # use built-in vorbis if needed (stb_vorbis not included here)
)
if(OQL_EDITION STREQUAL "base")
    target_compile_definitions(oql PRIVATE OQL_BASE)
endif()

if(DEFINED OQL_BETA_EXPIRY AND NOT OQL_BETA_EXPIRY STREQUAL "")
    target_compile_definitions(oql PRIVATE OQL_BETA_EXPIRY="${OQL_BETA_EXPIRY}")
endif()


qt_add_translations(oql
    TS_FILES
        translations/oql_en.ts
        translations/oql_es.ts
        translations/oql_fr.ts
    RESOURCE_PREFIX "/i18n"
)

set_target_properties(oql PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE   TRUE
)
if(APPLE)
    set_target_properties(oql PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/MacOSXBundleInfo.plist.in"
        MACOSX_BUNDLE_ICON_FILE oql
    )
    set(_MACOS_ICON "${CMAKE_SOURCE_DIR}/resources/oql.icns")
    if(EXISTS "${_MACOS_ICON}")
        set_source_files_properties("${_MACOS_ICON}" PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources)
        target_sources(oql PRIVATE "${_MACOS_ICON}")
    endif()
endif()

# ── oql_tests: harness standalone per engine/ (nessuna dipendenza da ui/) ────
if(OQL_BUILD_TESTS)
    add_executable(oql_tests
        src/engine/Cue.h
        src/engine/Cue.cpp
        src/engine/AudioPlugin.h
        src/engine/BuiltinPlugins.h
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
        src/engine/ProjectionWarp.h
        src/engine/ProjectionWarp.cpp
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
        src/engine/ImageCue.h
        src/engine/ImageCue.cpp
        src/engine/Workspace.h
        src/engine/Workspace.cpp
        src/engine/AppSettings.h
        src/engine/AppSettings.cpp
        src/engine/ScriptEngine.h
        src/engine/ScriptEngine.cpp
        src/engine/ScriptCue.h
        src/engine/ScriptCue.cpp
        src/engine/RecordCue.h
        src/engine/RecordCue.cpp
        src/ui/WebServer.h
        src/ui/WebServer.cpp
        src/ui/CueListModel.h
        src/ui/CueListModel.cpp
        tests/test_harness.h
        tests/fase1_tests.cpp
        tests/fase2_tests.cpp
        tests/fase3_tests.cpp
        tests/fase4_tests.cpp
        tests/fase5_tests.cpp
        tests/fase6_tests.cpp
        tests/fase7_tests.cpp
    )

    target_link_libraries(oql_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Multimedia
        Qt6::Network
        Qt6::Qml
        Qt6::Test
        ${LILV_LIBRARIES}
    )
    if(SOUNDTOUCH_FOUND)
        target_link_libraries(oql_tests PRIVATE ${SOUNDTOUCH_LIBRARIES})
        target_include_directories(oql_tests PRIVATE ${SOUNDTOUCH_INCLUDE_DIRS})
        target_compile_definitions(oql_tests PRIVATE HAVE_SOUNDTOUCH)
    endif()
    if(UNIX AND NOT APPLE)
        target_link_libraries(oql_tests PRIVATE ${SUIL_LIBRARIES} dl pthread X11)
    elseif(APPLE)
        target_link_libraries(oql_tests PRIVATE
            "-framework CoreAudio" "-framework CoreFoundation"
            "-framework AudioUnit" "-framework AudioToolbox")
    endif()

    target_include_directories(oql_tests PRIVATE
        src
        ${LILV_INCLUDE_DIRS}
        $<$<NOT:$<BOOL:${APPLE}>>:${SUIL_INCLUDE_DIRS}>
    )
    target_link_directories(oql_tests PRIVATE
        ${LILV_LIBRARY_DIRS}
        ${SOUNDTOUCH_LIBRARY_DIRS}
        $<$<NOT:$<BOOL:${APPLE}>>:${SUIL_LIBRARY_DIRS}>
    )
    target_compile_options(oql_tests PRIVATE
        ${LILV_CFLAGS_OTHER}
        $<$<NOT:$<BOOL:${APPLE}>>:${SUIL_CFLAGS_OTHER}>
    )
    target_compile_definitions(oql_tests PRIVATE MA_NO_VORBIS)
    if(OQL_EDITION STREQUAL "base")
        target_compile_definitions(oql_tests PRIVATE OQL_BASE)
    endif()
endif()
