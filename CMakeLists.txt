cmake_minimum_required(VERSION 3.21)

# 发布构建由 CI 根据 tag（v1.2.3）传入版本号
set(JIMAKU_VERSION "0.1.0" CACHE STRING "Application version")
if(NOT JIMAKU_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "JIMAKU_VERSION must look like 1.2.3, got '${JIMAKU_VERSION}'")
endif()

# Qt 6.12 需要 macOS 14；必须在 project() 之前设置
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")

project(Jimaku VERSION ${JIMAKU_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.7 REQUIRED COMPONENTS Widgets Network)
qt_standard_project_setup()

if(MSVC)
    add_compile_options(/utf-8)
endif()

# --- 第三方库：全部静态编进程序，发布时不用再带动态库 ----------------------
include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# whisper.cpp：macOS 默认走 Metal（着色器编进库里），Windows 默认 CPU，可选 CUDA
option(JIMAKU_CUDA "Build whisper.cpp with CUDA (Windows/Linux, needs CUDA toolkit)" OFF)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(GGML_CUDA ${JIMAKU_CUDA} CACHE BOOL "" FORCE)
# 发布包要能在别人的电脑上跑，不针对构建机的 CPU 指令集优化
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(whisper
    GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
    GIT_TAG v1.9.5
    GIT_SHALLOW TRUE)

# qtkeychain：API key 存 macOS 钥匙串 / Windows 凭据管理器，不写进配置文件
set(BUILD_WITH_QT6 ON CACHE BOOL "" FORCE)
set(BUILD_TRANSLATIONS OFF CACHE BOOL "" FORCE)
set(BUILD_TEST_APPLICATION OFF CACHE BOOL "" FORCE)
FetchContent_Declare(qtkeychain
    GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
    GIT_TAG 0.15.0
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(whisper qtkeychain)

# --- 核心（程序和测试共用，不依赖界面）---------------------------------------
add_library(jimaku_core STATIC
    src/core/segment.h src/core/segment.cpp
    src/core/errors.h src/core/errors.cpp
    src/core/mediadecoder.h src/core/mediadecoder.cpp
    src/core/transcriber.h src/core/transcriber.cpp
    src/core/segmenter.h src/core/segmenter.cpp
    src/core/translator.h src/core/translator.cpp
    src/core/subtitlewriter.h src/core/subtitlewriter.cpp
    src/core/burner.h src/core/burner.cpp
    src/core/modelmanager.h src/core/modelmanager.cpp
    src/core/settings.h src/core/settings.cpp
    src/core/pipeline.h src/core/pipeline.cpp)
target_include_directories(jimaku_core PUBLIC src)
target_link_libraries(jimaku_core PUBLIC Qt6::Core Qt6::Network whisper qt6keychain)

# --- 程序 -------------------------------------------------------------------
set(APP_SOURCES
    src/main.cpp
    src/cli.h src/cli.cpp
    src/ui/mainwindow.h src/ui/mainwindow.cpp
    src/ui/droppage.h src/ui/droppage.cpp
    src/ui/progresspage.h src/ui/progresspage.cpp
    src/ui/editorpage.h src/ui/editorpage.cpp
    src/ui/subtitlemodel.h src/ui/subtitlemodel.cpp
    src/ui/settingsdialog.h src/ui/settingsdialog.cpp
    src/ui/modeldownloaddialog.h src/ui/modeldownloaddialog.cpp)

qt_add_executable(Jimaku WIN32 MACOSX_BUNDLE ${APP_SOURCES})
target_compile_definitions(Jimaku PRIVATE APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(Jimaku PRIVATE Qt6::Widgets jimaku_core)
set_target_properties(Jimaku PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME Jimaku
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.wangchaozhi.jimaku
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})

# --- 单元测试 ---------------------------------------------------------------
option(JIMAKU_BUILD_TESTS "Build unit tests" ON)
if(JIMAKU_BUILD_TESTS)
    enable_testing()
    foreach(name segmenter subtitlewriter translator errors transcriber)
        qt_add_executable(${name}_test tests/${name}_test.cpp)
        target_link_libraries(${name}_test PRIVATE jimaku_core)
        add_test(NAME ${name} COMMAND ${name}_test)
    endforeach()
endif()
