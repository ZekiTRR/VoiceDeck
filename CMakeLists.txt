cmake_minimum_required(VERSION 3.20)
project(VoiceDeck VERSION 1.5.0 LANGUAGES C CXX)

message(STATUS "VoiceDeck - Soundpad + VoiceLab + RVC (Windows only)")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "VoiceDeck targets Windows only (WASAPI + VB-CABLE). Linux support is not planned for now.")
endif()

# ---- vendored DSP libraries --------------------------------------------
# Only the DaisySP modules we actually use (Decimator; PitchShifter pulls in
# Phasor). Everything else in DaisySP that we touch is header-only.
add_library(daisysp STATIC
    third_party/daisysp/Effects/decimator.cpp
    third_party/daisysp/Control/phasor.cpp
)
target_include_directories(daisysp PUBLIC
    third_party/daisysp
    third_party/daisysp/Utility
    third_party/daisysp/Control
    third_party/daisysp/Effects
)

file(GLOB IIR1_SRC CONFIGURE_DEPENDS third_party/iir1/iir/*.cpp)
add_library(iir1 STATIC ${IIR1_SRC})
target_include_directories(iir1 PUBLIC third_party/iir1)

# ------------------------------------------------------------------------

add_executable(voicedeck
    src/app/main.cpp
    src/app/App.cpp
    src/ui/UiApp.cpp
    src/core/engine/AudioEngine.cpp
    src/core/dsp/VoiceProcessor.cpp
    src/core/dsp/AmbientPlayer.cpp
    src/core/ai/Ort.cpp
    src/core/ai/VoiceLibrary.cpp
    src/core/ai/RvcConverter.cpp
    src/core/ai/AiVoiceService.cpp
    src/core/ai/AiVoiceStage.cpp
    src/core/soundboard/Sound.cpp
    src/core/soundboard/SoundBank.cpp
    src/core/soundboard/VoiceMixer.cpp
    src/core/settings/Settings.cpp
    src/core/platform/HotkeyManager.cpp
    src/core/platform/AutoStart.cpp
    src/core/platform/Elevation.cpp
    src/core/platform/ThreadAffinity.cpp
    third_party/miniaudio/miniaudio.c
)

target_include_directories(voicedeck PRIVATE
    src
    third_party
    third_party/miniaudio
    third_party/signalsmith
    third_party/onnxruntime/include
    third_party/webview2/include
)

target_compile_definitions(voicedeck PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(voicedeck PRIVATE /W3 /permissive-)
    # Real-time DSP: fast FP math; /MP for quick parallel builds.
    # (Whole-program /GL+/LTCG was tried and dropped: 3x slower rebuilds for
    # no measurable audio-path gain.)
    target_compile_options(voicedeck PRIVATE /fp:fast /MP)
    # miniaudio is vendored third-party C - keep its warnings silent
    set_source_files_properties(third_party/miniaudio/miniaudio.c PROPERTIES COMPILE_OPTIONS "/W0")
else()
    target_compile_options(voicedeck PRIVATE -Wall -Wextra)
endif()

# WASAPI/COM (ole32), hotkeys/tray (user32, shell32), WebView2 UI (static loader)
target_link_libraries(voicedeck PRIVATE user32 shell32 ole32 daisysp iir1
    "${CMAKE_SOURCE_DIR}/third_party/webview2/lib/x64/WebView2LoaderStatic.lib")

# onnxruntime.dll is loaded dynamically (OrtGetApiBase); ship it next to the exe.
add_custom_command(TARGET voicedeck POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/bin/onnxruntime.dll"
        "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/bin/onnxruntime_providers_shared.dll"
        $<TARGET_FILE_DIR:voicedeck>)

# The web front-end lives in a "web" folder next to the exe.
add_custom_command(TARGET voicedeck POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/web" "$<TARGET_FILE_DIR:voicedeck>/web")

# Python tooling for the in-app RVC .pth -> ONNX converter.
add_custom_command(TARGET voicedeck POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/tools/aivoice" "$<TARGET_FILE_DIR:voicedeck>/tools/aivoice")

if(MSVC)
    set_source_files_properties(third_party/daisysp/Effects/decimator.cpp
                                third_party/daisysp/Control/phasor.cpp
                                PROPERTIES COMPILE_OPTIONS "/W0")
endif()
