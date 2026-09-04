 
#pragma once
#include <windows.h>
#include <math.h>
#include "config.h"   
#include "dpi.h"

extern HFONT g_hFontUI;

 
static inline HFONT cseq_create_face_font(const char *face, int pxHeight) {
    return CreateFontA(-pxHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
}

 
static inline HFONT cseq_build_ui_font_locked(void) {
    int px = (int)(14.0f * scale_font(1.0f) + 0.5f);
    if (px < 8) px = 8;
    HFONT f = cseq_create_face_font("Inter", px);
    if (!f) f = cseq_create_face_font("Segoe UI", px);
    return f;
}

static inline void refresh_ui_font_for_dpi(void) {
    HFONT old = g_hFontUI;
    HFONT nu = cseq_build_ui_font_locked();
    if (nu) {
        g_hFontUI = nu;
        if (old && old != (HFONT)GetStockObject(DEFAULT_GUI_FONT)) DeleteObject(old);
    }
}

 
static inline bool init_ui_font(void) {
    if (g_dpiScaleX == 1.0f && g_dpiScaleY == 1.0f) {
         
        UINT dx, dy;
        HDC hdc = GetDC(NULL);
        if (hdc) {
            dx = (UINT)GetDeviceCaps(hdc, LOGPIXELSX);
            dy = (UINT)GetDeviceCaps(hdc, LOGPIXELSY);
            ReleaseDC(NULL, hdc);
            cseq_update_scales_from_dpi(dx, dy);
        }
    }

    HRSRC hRes = FindResourceA(NULL, MAKEINTRESOURCEA(IDR_INTER_FONT), RT_RCDATA);
    if (hRes) {
        HGLOBAL hData = LoadResource(NULL, hRes);
        if (hData) {
            void* pData = LockResource(hData);
            DWORD size = SizeofResource(NULL, hRes);
            if (pData && size > 0) {
                DWORD nFonts = 0;
                HANDLE hFontResource = AddFontMemResourceEx(pData, size, NULL, &nFonts);
                if (hFontResource) {
                    g_hFontUI = cseq_build_ui_font_locked();
                    if (g_hFontUI) return true;
                }
            }
        }
    }

    
    g_hFontUI = cseq_build_ui_font_locked();
    if (g_hFontUI) return true;

    
    g_hFontUI = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    return (g_hFontUI != NULL);
}

 
#define SELECT_UI_FONT(hdc)   (HFONT)SelectObject((hdc), g_hFontUI)

 
static inline HFONT get_ui_small_font(void) {
    static HFONT s_hFontSmall = NULL;
    static int   s_lastPx = 0;
    int px = (int)(10.0f * scale_font(1.0f) + 0.5f);
    if (px < 7) px = 7;
    if (!s_hFontSmall || px != s_lastPx) {
        if (s_hFontSmall) DeleteObject(s_hFontSmall);
        s_hFontSmall = CreateFontA(-px, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Inter");
        if (!s_hFontSmall) {
            s_hFontSmall = CreateFontA(-px, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
        }
        s_lastPx = px;
    }
    return s_hFontSmall;
}

static inline COLORREF hsl_to_rgb(float h, float s, float l) {
    float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = l - c / 2.0f;
    float r = 0.0f, g = 0.0f, b = 0.0f;

    if (h < 60.0f) {
        r = c; g = x; b = 0.0f;
    } else if (h < 120.0f) {
        r = x; g = c; b = 0.0f;
    } else if (h < 180.0f) {
        r = 0.0f; g = c; b = x;
    } else if (h < 240.0f) {
        r = 0.0f; g = x; b = c;
    } else if (h < 300.0f) {
        r = x; g = 0.0f; b = c;
    } else {
        r = c; g = 0.0f; b = x;
    }

    return RGB((BYTE)((r + m) * 255.0f), (BYTE)((g + m) * 255.0f), (BYTE)((b + m) * 255.0f));
}
// ---------------------------------------------------------------------------
// Central error reporting. All project code should raise user-facing errors
// through cseq_report_error() so a headless/logging variant only needs to
// override this one hook. owner may be NULL; safe to call from worker threads
// (a message box owned by g_hWnd is still modal-free for the worker).
static inline void cseq_report_error(HWND owner, const char* title, const char* message) {
    MessageBoxA(owner, message, title, MB_ICONERROR);
}
