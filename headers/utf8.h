#pragma once
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

 
static inline int utf8_to_wide_buf(const char* src, wchar_t* dst, int dstChars) {
    if (!dst || dstChars <= 0) return 0;
    dst[0] = L'\0';
    if (!src || !src[0]) return 0;
    int need = MultiByteToWideChar(CP_UTF8, 0, src, -1, NULL, 0);
    if (need <= 0 || need > dstChars) return 0;
    int wrote = MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, need);
    return (wrote > 1) ? (wrote - 1) : 0;
}

 
static inline int wide_to_utf8_buf(const wchar_t* src, char* dst, int dstBytes) {
    if (!dst || dstBytes <= 0) return 0;
    dst[0] = '\0';
    if (!src || !src[0]) return 0;
    int need = WideCharToMultiByte(CP_UTF8, 0, src, -1, NULL, 0, NULL, NULL);
    if (need <= 0 || need > dstBytes) return 0;
    int wrote = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, need, NULL, NULL);
    return (wrote > 1) ? (wrote - 1) : 0;
}

 
static inline wchar_t* utf8_to_wide_heap(const char* src) {
    if (!src || !src[0]) return NULL;
    int need = MultiByteToWideChar(CP_UTF8, 0, src, -1, NULL, 0);
    if (need <= 0) return NULL;
    wchar_t* w = (wchar_t*)malloc((size_t)need * sizeof(wchar_t));
    if (!w) return NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, src, -1, w, need) <= 0) {
        free(w);
        return NULL;
    }
    return w;
}

 
static inline char* wide_to_utf8_heap(const wchar_t* src) {
    if (!src || !src[0]) return NULL;
    int need = WideCharToMultiByte(CP_UTF8, 0, src, -1, NULL, 0, NULL, NULL);
    if (need <= 0) return NULL;
    char* u8 = (char*)malloc((size_t)need);
    if (!u8) return NULL;
    if (WideCharToMultiByte(CP_UTF8, 0, src, -1, u8, need, NULL, NULL) <= 0) {
        free(u8);
        return NULL;
    }
    return u8;
}

 
static inline FILE* fopen_utf8(const char* pathUtf8, const wchar_t* mode) {
    wchar_t wpath[MAX_PATH * 2];
    if (utf8_to_wide_buf(pathUtf8, wpath, (int)(sizeof(wpath) / sizeof(wpath[0]))) <= 0)
        return NULL;
    return _wfopen(wpath, mode);
}
