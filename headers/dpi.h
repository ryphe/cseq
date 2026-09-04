 #pragma once
#include <windows.h>
#include "config.h"

#ifndef USER_DEFAULT_SCREEN_DPI
#define USER_DEFAULT_SCREEN_DPI 96
#endif

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)(UINT_PTR)-4)
#endif

static inline void cseq_update_scales_from_dpi(UINT dpiX, UINT dpiY) {
    if (dpiX < USER_DEFAULT_SCREEN_DPI) dpiX = USER_DEFAULT_SCREEN_DPI;
    if (dpiX > 480) dpiX = 480;
    if (dpiY < USER_DEFAULT_SCREEN_DPI) dpiY = USER_DEFAULT_SCREEN_DPI;
    if (dpiY > 480) dpiY = 480;
    g_dpiScaleX = (float)dpiX / (float)USER_DEFAULT_SCREEN_DPI;
    g_dpiScaleY = (float)dpiY / (float)USER_DEFAULT_SCREEN_DPI;
}

 
static inline BOOL cseq_enable_process_dpi_awareness(void) {
    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (user32) {
        typedef BOOL (WINAPI *FnSetCtx)(HANDLE);
        FnSetCtx pfn = (FnSetCtx)(void *)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (pfn) {
            if (pfn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return TRUE;
        }
    }
    {
        HMODULE shcore = LoadLibraryA("shcore.dll");
        if (shcore) {
            typedef HRESULT (WINAPI *FnSetAwareness)(int);
            FnSetAwareness pfn = (FnSetAwareness)(void *)GetProcAddress(shcore, "SetProcessDpiAwareness");
            if (pfn && SUCCEEDED(pfn(2  ))) {
                FreeLibrary(shcore);
                return TRUE;
            }
            FreeLibrary(shcore);
        }
    }
    {
        typedef BOOL (WINAPI *FnSetAware)(void);
        HMODULE u32 = GetModuleHandleA("user32.dll");
        if (u32) {
            FnSetAware pfn = (FnSetAware)(void *)GetProcAddress(u32, "SetProcessDPIAware");
            if (pfn) return pfn();
        }
    }
    return FALSE;
}

 
static inline void cseq_query_window_dpi(HWND hwnd, UINT *outDpiX, UINT *outDpiY) {
    UINT dx = USER_DEFAULT_SCREEN_DPI, dy = USER_DEFAULT_SCREEN_DPI;
    if (hwnd && IsWindow(hwnd)) {
        HMODULE user32 = GetModuleHandleA("user32.dll");
        if (user32) {
            typedef UINT (WINAPI *FnGetDpiForWindow)(HWND);
            FnGetDpiForWindow pfn = (FnGetDpiForWindow)(void *)GetProcAddress(user32, "GetDpiForWindow");
            if (pfn) {
                dx = dy = pfn(hwnd);
            } else {
                HDC hdc = GetDC(hwnd);
                if (hdc) {
                    dx = (UINT)GetDeviceCaps(hdc, LOGPIXELSX);
                    dy = (UINT)GetDeviceCaps(hdc, LOGPIXELSY);
                    ReleaseDC(hwnd, hdc);
                }
            }
        }
    } else {
        HDC hdc = GetDC(NULL);  
        if (hdc) {
            dx = (UINT)GetDeviceCaps(hdc, LOGPIXELSX);
            dy = (UINT)GetDeviceCaps(hdc, LOGPIXELSY);
            ReleaseDC(NULL, hdc);
        }
    }
    *outDpiX = dx; *outDpiY = dy;
}

 
static inline void cseq_apply_dpi_for_window(HWND hwnd) {
    UINT dx, dy;
    cseq_query_window_dpi(hwnd, &dx, &dy);
    cseq_update_scales_from_dpi(dx, dy);
}
