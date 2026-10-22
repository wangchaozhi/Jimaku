cmake_minimum_required(VERSION 3.21)

# 发布构建由 CI 根据 tag（v1.2.3）传入版本号
set(JIMAKU_VERSION "0.1.1" CACHE STRING "Application version")
if(NOT JIMAKU_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "JIMAKU_VERSION must look like 1.2.3, got '${JIMAKU_VERSION}'")
endif()

# Qt 6.12 需要 macOS 14；必须在 project() 之前设置
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")

project(Jimaku VERSION ${JIMAKU_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.7 REQUIRED COMPONENTS Widgets Network Multimedia MultimediaWidgets)
qt_standard_project_setup()

if(MSVC)
    # 只给 C/C++：开 JIMAKU_CUDA 时 nvcc 会把裸的 /utf-8 当成第二个输入文件
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>")
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

# MeCab：日文分词和读音（振假名用）。仓库是 autotools 的，这里只编运行库；源码里还有 register，用 C++14
FetchContent_Declare(mecab
    GIT_REPOSITORY https://github.com/taku910/mecab.git
    GIT_TAG 61b90ba6e669dc2d7d533d4a80d206f3b31d52b1)

FetchContent_MakeAvailable(whisper qtkeychain mecab)

set(MECAB_SRC ${mecab_SOURCE_DIR}/mecab/src)
# 上游还在继承 std::binary_function（C++17 起已删除，新版 MSVC 没有）：去掉这个没用的基类（可重复执行）
file(READ ${MECAB_SRC}/dictionary.cpp _mecab_dictionary)
string(REPLACE "struct pair_1st_cmp: public std::binary_function<bool, T1, T2> {" "struct pair_1st_cmp {"
       _mecab_patched "${_mecab_dictionary}")
if(NOT _mecab_patched STREQUAL _mecab_dictionary)
    file(WRITE ${MECAB_SRC}/dictionary.cpp "${_mecab_patched}")
endif()
file(GLOB MECAB_SOURCES ${MECAB_SRC}/*.cpp)
list(FILTER MECAB_SOURCES EXCLUDE REGEX "/mecab(-[a-z-]+)?\\.cpp$") # 命令行工具
add_library(mecab STATIC ${MECAB_SOURCES})
set_target_properties(mecab PROPERTIES CXX_STANDARD 14)
target_compile_definitions(mecab PRIVATE HAVE_CONFIG_H)
target_include_directories(mecab PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/mecab PUBLIC ${MECAB_SRC})
if(WIN32)
    # mecab.h 在 Windows 上默认 dllimport；静态链接时库和使用方都按 dllexport 声明即可
    target_compile_definitions(mecab PUBLIC DLL_EXPORT NOMINMAX WIN32_LEAN_AND_MEAN PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(mecab PRIVATE advapi32) # 读注册表里的 mecabrc（我们用不到，但代码里有）
endif()
if(MSVC)
    target_compile_options(mecab PRIVATE /w)
else()
    target_compile_options(mecab PRIVATE -w)
endif()

# --- 核心（程序和测试共用，不依赖界面）---------------------------------------
add_library(jimaku_core STATIC
    src/core/segment.h src/core/segment.cpp
    src/core/errors.h src/core/errors.cpp
    src/core/mediadecoder.h src/core/mediadecoder.cpp
    src/core/transcriber.h src/core/transcriber.cpp
    src/core/segmenter.h src/core/segmenter.cpp
    src/core/translator.h src/core/translator.cpp
    src/core/subtitlewriter.h src/core/subtitlewriter.cpp
    src/core/subtitlereader.h src/core/subtitlereader.cpp
    src/core/segmentedit.h src/core/segmentedit.cpp
    src/core/furigana.h src/core/furigana.cpp
    src/core/burner.h src/core/burner.cpp
    src/core/modelmanager.h src/core/modelmanager.cpp
    src/core/settings.h src/core/settings.cpp
    src/core/pipeline.h src/core/pipeline.cpp
    src/core/coverage.h src/core/coverage.cpp
    src/core/project.h src/core/project.cpp
    src/core/sleepguard.h src/core/sleepguard.cpp)
target_include_directories(jimaku_core PUBLIC src)
target_link_libraries(jimaku_core PUBLIC Qt6::Core Qt6::Network whisper qt6keychain PRIVATE mecab)
if(APPLE)
    target_link_libraries(jimaku_core PUBLIC "-framework IOKit" "-framework CoreFoundation") # 处理时阻止睡眠
endif()

# --- 程序 -------------------------------------------------------------------
set(APP_SOURCES
    src/main.cpp
    src/cli.h src/cli.cpp
    src/ui/mainwindow.h src/ui/mainwindow.cpp
    src/ui/droppage.h src/ui/droppage.cpp
    src/ui/workspacepage.h src/ui/workspacepage.cpp
    src/ui/videoview.h src/ui/videoview.cpp
    src/ui/timeline.h src/ui/timeline.cpp
    src/ui/subtitlemodel.h src/ui/subtitlemodel.cpp
    src/ui/settingsdialog.h src/ui/settingsdialog.cpp
    src/ui/modeldownloaddialog.h src/ui/modeldownloaddialog.cpp
    src/ui/updatechecker.h src/ui/updatechecker.cpp)

# 图标：resources/icons/jimaku.svg 生成的 .icns / .ico / png（生成结果已提交）
if(WIN32)
    set(APP_ICON_SOURCES resources/icons/jimaku.rc)
    set_source_files_properties(resources/icons/jimaku.rc PROPERTIES
        OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/jimaku.ico)
elseif(APPLE)
    set(APP_ICON_SOURCES resources/icons/jimaku.icns)
    set_source_files_properties(resources/icons/jimaku.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

qt_add_executable(Jimaku WIN32 MACOSX_BUNDLE ${APP_SOURCES} ${APP_ICON_SOURCES})
qt_add_resources(Jimaku "icons" PREFIX "/" FILES resources/icons/jimaku-256.png)
target_compile_definitions(Jimaku PRIVATE APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(Jimaku PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets jimaku_core)
set_target_properties(Jimaku PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME Jimaku
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.wangchaozhi.jimaku
    MACOSX_BUNDLE_ICON_FILE jimaku.icns
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})

# macOS：钥匙串的"始终允许"绑定在签名上。默认的临时签名每次编译都会变，于是每次都弹窗；
# 用固定的证书签名（钥匙串里有名为 "Jimaku Dev" 的代码签名证书时自动使用）就只需授权一次
# （也可以用 -DJIMAKU_CODESIGN_IDENTITY=... 指定别的证书）
if(APPLE)
    set(_identity "${JIMAKU_CODESIGN_IDENTITY}")
    if(NOT _identity)
        execute_process(COMMAND security find-identity -p codesigning OUTPUT_VARIABLE _identities ERROR_QUIET)
        if(_identities MATCHES "\"Jimaku Dev\"")
            set(_identity "Jimaku Dev")
        endif()
    endif()
    if(_identity)
        message(STATUS "Jimaku: signing with \"${_identity}\"")
        add_custom_command(TARGET Jimaku POST_BUILD
            COMMAND codesign --force --sign "${_identity}" "$<TARGET_BUNDLE_DIR:Jimaku>"
            VERBATIM)
    endif()
endif()

# --- 单元测试 ---------------------------------------------------------------
option(JIMAKU_BUILD_TESTS "Build unit tests" ON)
if(JIMAKU_BUILD_TESTS)
    enable_testing()
    foreach(name segmenter segmentedit subtitlewriter subtitlereader translator errors transcriber coverage furigana)
        qt_add_executable(${name}_test tests/${name}_test.cpp)
        target_link_libraries(${name}_test PRIVATE jimaku_core)
        add_test(NAME ${name} COMMAND ${name}_test)
    endforeach()
endif()
