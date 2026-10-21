// MeCab 用 autotools 生成 config.h；这里按 Jimaku 的用法手写：
// 只用运行时（分词），字典和输入都是 UTF-8，不需要 iconv
#pragma once
#define PACKAGE "mecab"
#define VERSION "0.996"
#define DIC_VERSION 102
#define MECAB_DEFAULT_RC ""
#define HAVE_STDINT_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_FCNTL_H 1
#if defined(_WIN32) && !defined(__GNUC__)
// 上游 common.h 只在 MinGW 下定义 WPATH_FORCE，MSVC 用到却没定义：在这里补上（路径按 UTF-8 转宽字符）
#define WPATH_FORCE(path) (MeCab::Utf8ToWide(path).c_str())
#endif
#if defined(_WIN32)
#define HAVE_WINDOWS_H 1
#else
#define HAVE_UNISTD_H 1
#define HAVE_SYS_MMAN_H 1
#define HAVE_DIRENT_H 1
#define HAVE_PTHREAD_H 1
#define HAVE_GETENV 1
#endif
