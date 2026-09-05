#pragma once
#include "globals.h"
#include "dsp.h"
#include "fx.h"
#include "scrollbar.h"
#include "slicing.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>

#pragma comment(lib, "msimg32.lib")

 
#ifdef CSEQ_PROFILE
static int    cseq_prof_depth = 0;       
static double cseq_prof_stripMs = 0.0;   
static int    cseq_prof_stripCount = 0;
static int    cseq_prof_stripClips = 0;

static inline double cseq_prof_now_ms(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

static inline DWORD cseq_prof_gdi_handles(void) {
    return GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
}

static inline void cseq_prof_report(const char* tag, double t0, DWORD gdi0,
                                    int clips, int stripCount, double stripMs) {
    char buf[192];
    if (stripCount > 0) {
        snprintf(buf, sizeof(buf),
                 "[CSEQ_PROFILE] %-14s %9.3f ms  clips=%d  strips=%d (%.3f ms)  GDI %lu -> %lu\n",
                 tag, cseq_prof_now_ms() - t0, clips, stripCount, stripMs,
                 (unsigned long)gdi0, (unsigned long)cseq_prof_gdi_handles());
    } else {
        snprintf(buf, sizeof(buf),
                 "[CSEQ_PROFILE] %-14s %9.3f ms  clips=%d  GDI %lu -> %lu\n",
                 tag, cseq_prof_now_ms() - t0, clips,
                 (unsigned long)gdi0, (unsigned long)cseq_prof_gdi_handles());
    }
    OutputDebugStringA(buf);
}
#endif

#define TOPBAR_SLOT_COUNT 13
#define TOPBAR_START_X 12
#define TOPBAR_SLOT_GAP 6


 
static inline void draw_supersampled_badge_glyph(HDC hdc, int cx, int cy, int gw, int gh, bool isOpen, COLORREF col, float alphaMultiplier) {
    if (gw <= 0 || gh <= 0) return;

    int ssW = gw * 2;
    int ssH = gh * 2;

    HDC memDC = CreateCompatibleDC(hdc);
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = ssW;
    bmi.bmiHeader.biHeight = -ssH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hBmp || !pBits) {
        if (memDC) DeleteDC(memDC);
        return;
    }

    HGDIOBJ oldBmp = SelectObject(memDC, hBmp);
    memset(pBits, 0, (size_t)ssW * (size_t)ssH * 4);

    HPEN linePen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    HGDIOBJ oldPen = SelectObject(memDC, linePen);

    if (!isOpen) {
         
         
        float t = (float)((double)GetTickCount64() * 0.00115);
        float scale = (float)ssH * 0.42f;
        int midX = ssW / 2;
        int midY = ssH / 2;

        POINT ssPts[FAKE_ATTRACTOR_POINTS];
        for (int i = 0; i < FAKE_ATTRACTOR_POINTS; ++i) {
            float p = (float)i / (float)(FAKE_ATTRACTOR_POINTS - 1);
            float a = t * 0.62f + p * 6.283185f * 1.75f;
            float b = t * 0.91f + p * 6.283185f * 2.35f;

            float x = sinf(a) * (1.05f + 0.32f * cosf(b * 1.35f));
            float y = cosf(a * 0.88f) * sinf(b) * 0.92f + 0.22f * sinf(a * 2.05f + b * 0.7f);

            ssPts[i].x = midX + (int)(x * scale);
            ssPts[i].y = midY + (int)(y * scale * 0.72f);
        }
        Polyline(memDC, ssPts, FAKE_ATTRACTOR_POINTS);
        MoveToEx(memDC, ssPts[FAKE_ATTRACTOR_POINTS - 1].x, ssPts[FAKE_ATTRACTOR_POINTS - 1].y, NULL);
        LineTo(memDC, ssPts[0].x, ssPts[0].y);
    } else {
         
        int midY = ssH / 2;
        LONG wp = InterlockedCompareExchange(&g_visRing.writePos, 0, 0);
        float t = (float)((double)GetTickCount64() * 0.004);

        POINT oscPts[256];
        int numPts = (ssW < 256) ? ssW : 256;
        for (int x = 0; x < numPts; ++x) {
            float normX = (float)x / (float)numPts;
            int sIdx = (wp - 256 + (int)(normX * 256.0f)) & (VIS_RING_SIZE - 1);
            float audioSample = g_visRing.bufferL[sIdx];
            float synthSine = sinf(normX * 6.283185f * 2.0f + t) * 0.35f;
            float combined = audioSample * 0.75f + synthSine * 0.25f;

            int y = midY - (int)(combined * (float)(ssH / 2 - 4));
            if (y < 2) y = 2;
            if (y > ssH - 3) y = ssH - 3;
            oscPts[x] = (POINT){ x, y };
        }
        Polyline(memDC, oscPts, numPts);
    }

    SelectObject(memDC, oldPen);
    DeleteObject(linePen);
    GdiFlush();    

     
    HDC downDC = CreateCompatibleDC(hdc);
    BITMAPINFO downBmi = { 0 };
    downBmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    downBmi.bmiHeader.biWidth = gw;
    downBmi.bmiHeader.biHeight = -gh;
    downBmi.bmiHeader.biPlanes = 1;
    downBmi.bmiHeader.biBitCount = 32;
    downBmi.bmiHeader.biCompression = BI_RGB;

    void* pDownBits = NULL;
    HBITMAP hDownBmp = CreateDIBSection(downDC, &downBmi, DIB_RGB_COLORS, &pDownBits, NULL, 0);
    if (hDownBmp && pDownBits) {
        HGDIOBJ oldDownBmp = SelectObject(downDC, hDownBmp);
        DWORD* srcPix = (DWORD*)pBits;
        DWORD* dstPix = (DWORD*)pDownBits;

        float cR = (float)GetRValue(col);
        float cG = (float)GetGValue(col);
        float cB = (float)GetBValue(col);

        for (int y = 0; y < gh; ++y) {
            for (int x = 0; x < gw; ++x) {
                int s00 = (srcPix[(y * 2 + 0) * ssW + (x * 2 + 0)] & 0xFF) ? 1 : 0;
                int s01 = (srcPix[(y * 2 + 0) * ssW + (x * 2 + 1)] & 0xFF) ? 1 : 0;
                int s10 = (srcPix[(y * 2 + 1) * ssW + (x * 2 + 0)] & 0xFF) ? 1 : 0;
                int s11 = (srcPix[(y * 2 + 1) * ssW + (x * 2 + 1)] & 0xFF) ? 1 : 0;
                float cov = (float)(s00 + s01 + s10 + s11) * 0.25f;

                if (cov <= 0.001f) {
                    dstPix[y * gw + x] = 0;
                } else {
                    float aF = cov * alphaMultiplier;
                    if (aF > 1.0f) aF = 1.0f;
                    BYTE a = (BYTE)(aF * 255.0f);
                    BYTE pr = (BYTE)(cR * aF);
                    BYTE pg = (BYTE)(cG * aF);
                    BYTE pb = (BYTE)(cB * aF);
                    dstPix[y * gw + x] = ((DWORD)a << 24) | ((DWORD)pr << 16) | ((DWORD)pg << 8) | (DWORD)pb;
                }
            }
        }
        GdiFlush();    

        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        AlphaBlend(hdc, cx - gw / 2, cy - gh / 2, gw, gh, downDC, 0, 0, gw, gh, bf);

        SelectObject(downDC, oldDownBmp);
        DeleteObject(hDownBmp);
    }
    DeleteDC(downDC);

    SelectObject(memDC, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(memDC);
}

static inline bool granular_is_clip_enabled(int clipIdx) {
    if (clipIdx < 0 || clipIdx >= g_Seq.clipCount) return false;
    return g_Seq.clips[clipIdx].isGranular;
}

static inline bool granular_is_track_enabled(int trackIdx) {
    if (trackIdx < 0 || trackIdx >= MAX_TRACKS) return false;
    return g_TrackGran[trackIdx].enabled;
}

static inline COLORREF get_volume_gradient_color(float vol) {
    float norm = vol / 1.5f;
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    float hue = norm * 135.0f;
    return hsl_to_rgb(hue, 0.85f, 0.52f);
}


static inline float track_theme_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}


#define TRACK_THEME_HUE_STEP  30.0f  
#define TRACK_THEME_SAT       0.50f  

static inline void init_track_theme(int trackIdx) {
    if (trackIdx < 0 || trackIdx >= MAX_TRACKS) return;

    static bool s_seeded = false;
    static uint32_t s_globalSeed = 0;
    if (!s_seeded) {
        // Fixed seed (no rand()): the value IS the starting hue (hueOffset =
        // s_globalSeed % 360 below). 270° = purple, so the track palette
        // begins around purple and steps through the hue wheel.
        s_globalSeed = 270;
        s_seeded = true;
    }

    
    float hueOffset = (float)(s_globalSeed % 360);
    float hue = fmodf(hueOffset + 360.0f - fmodf((float)trackIdx * TRACK_THEME_HUE_STEP, 360.0f), 360.0f);

    
    uint32_t h = s_globalSeed ^ (uint32_t)trackIdx;
    h ^= h >> 16;
    h *= 0x7feb352dUL;
    h ^= h >> 15;
    h *= 0x846ca68bUL;
    h ^= h >> 16;

    float sat = TRACK_THEME_SAT;
    float lig = 0.60f; 

    TrackTheme* t = &g_Seq.trackThemes[trackIdx];

    t->waveColor       = hsl_to_rgb(hue, sat, lig);
    t->selectWaveColor = hsl_to_rgb(hue, sat, track_theme_clampf(lig * 1.2f, 0.f, 1.f));
    t->bgColor         = hsl_to_rgb(hue, sat, track_theme_clampf(lig * 0.3f, 0.f, 1.f));
    t->selectBgColor   = hsl_to_rgb(hue, sat, track_theme_clampf(lig * 0.45f, 0.f, 1.f));
    t->borderColor     = hsl_to_rgb(hue, sat, track_theme_clampf(lig * 0.65f, 0.f, 1.f));
}


static const char *kSlotMaxLabels[TOPBAR_SLOT_COUNT] = {
    "PAUSE",
    "300.00 BPM",
    "1024 BARS",
    "SWING 100%",
    "SNAP 1/32T",
    "FROM CURSOR",
    "LO-FI OFF",
    "IMPORT",
    "EXPORT",
    "SAVE",
    "LOAD",
    "KEYBINDS",
    " ~~~ "  
};

static inline void get_topbar_slot_bounds(HDC hdc, int slotIdx, int *outX, int *outW) {
    HDC useDC = hdc;
    bool releaseDC = false;
    if (!useDC) {
        useDC = GetDC(NULL);
        releaseDC = true;
    }

    HFONT oldFont = SELECT_UI_FONT(useDC);
    (void)oldFont;

    SIZE szBracket;
    GetTextExtentPoint32A(useDC, "[", 1, &szBracket);

    int curX = scale_x(TOPBAR_START_X);
    for (int i = 0; i < TOPBAR_SLOT_COUNT; ++i) {
        SIZE szText;
        GetTextExtentPoint32A(useDC, kSlotMaxLabels[i], (int)strlen(kSlotMaxLabels[i]), &szText);
        int slotWidth = szText.cx + (szBracket.cx * 2) + scale_x(10);

        if (i == slotIdx) {
            if (outX) *outX = curX;
            if (outW) *outW = slotWidth;
            break;
        }
        curX += slotWidth + scale_x(TOPBAR_SLOT_GAP);
    }

    if (releaseDC) {
        ReleaseDC(NULL, useDC);
    }
}

static inline void draw_fixed_badge(HDC hdc, int slotIdx, int y, const char *text, COLORREF textCol) {
    HFONT oldFont = SELECT_UI_FONT(hdc);
    int x = 0, w = 0;
    get_topbar_slot_bounds(hdc, slotIdx, &x, &w);

    int yOff1 = scale_y(1);
    int badgeH = scale_y(21);

    SetBkMode(hdc, TRANSPARENT);
    SetTextCharacterExtra(hdc, 0);

    SIZE szBracket;
    GetTextExtentPoint32A(hdc, "[", 1, &szBracket);

    SetTextColor(hdc, RGB(70, 78, 92));
    TextOutA(hdc, x, y + yOff1, "[", 1);
    TextOutA(hdc, x + w - szBracket.cx, y + yOff1, "]", 1);

    SetTextColor(hdc, textCol);
    RECT textRect = {
        x + szBracket.cx,
        y + yOff1,
        x + w - szBracket.cx,
        y + badgeH
    };
    DrawTextA(hdc, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, oldFont);
}

inline void draw_alpha_box(HDC hdc, int x, int y, int w, int h, COLORREF fillColor, BYTE alpha, COLORREF borderColor) {
    if (w <= 0 || h <= 0) return;
    HDC memDC = CreateCompatibleDC(hdc);
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *pBits = NULL;
    HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (hBmp && pBits) {
        HGDIOBJ oldBmp = SelectObject(memDC, hBmp);
        BYTE r = GetRValue(fillColor);
        BYTE g = GetGValue(fillColor);
        BYTE b = GetBValue(fillColor);
        BYTE pr = (BYTE)((r * alpha) / 255);
        BYTE pg = (BYTE)((g * alpha) / 255);
        BYTE pb = (BYTE)((b * alpha) / 255);
        DWORD pixel = ((DWORD)alpha << 24) | ((DWORD)pr << 16) | ((DWORD)pg << 8) | (DWORD)pb;
        DWORD *pPix = (DWORD *)pBits;
        int count = w * h;
        for (int i = 0; i < count; ++i) pPix[i] = pixel;
        GdiFlush();    

        BLENDFUNCTION bf = {AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
        AlphaBlend(hdc, x, y, w, h, memDC, 0, 0, w, h, bf);

        SelectObject(memDC, oldBmp);
        DeleteObject(hBmp);
    }
    DeleteDC(memDC);

    HPEN borderPen = CreatePen(PS_DOT, 1, borderColor);
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, x, y, x + w, y + h);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);
}

 

 
inline void draw_aa_circle(HDC hdc, int cx, int cy, float radius, COLORREF fillColor, COLORREF borderColor, float borderWidth) {
    int rCeil = (int)ceilf(radius + borderWidth) + 2;
    int size = rCeil * 2 + 1;
    if (size <= 0) return;

    HDC memDC = CreateCompatibleDC(hdc);
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hBmp || !pBits) {
        if (memDC) DeleteDC(memDC);
        return;
    }

    HGDIOBJ oldBmp = SelectObject(memDC, hBmp);
    DWORD* pPix = (DWORD*)pBits;
    float center = (float)rCeil;

    float fR = (float)GetRValue(fillColor), fG = (float)GetGValue(fillColor), fB = (float)GetBValue(fillColor);
    float bR = (float)GetRValue(borderColor), bG = (float)GetGValue(borderColor), bB = (float)GetBValue(borderColor);

    const int SAMPLES = 4;
    const float step = 1.0f / (float)SAMPLES;
    const float offset = step * 0.5f;
    const float invTotalSamples = 1.0f / (float)(SAMPLES * SAMPLES);
    const float innerRadius = (borderWidth > 0.0f) ? (radius - borderWidth) : radius;
    const float rSq = radius * radius;
    const float inRSq = innerRadius * innerRadius;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float sumR = 0.0f, sumG = 0.0f, sumB = 0.0f, sumA = 0.0f;

            for (int sy = 0; sy < SAMPLES; ++sy) {
                float py = ((float)y + offset + (float)sy * step) - center;
                for (int sx = 0; sx < SAMPLES; ++sx) {
                    float px = ((float)x + offset + (float)sx * step) - center;
                    float distSq = px * px + py * py;

                    if (distSq <= inRSq) {
                        sumR += fR; sumG += fG; sumB += fB; sumA += 1.0f;
                    } else if (distSq <= rSq) {
                        if (borderWidth > 0.0f) {
                            sumR += bR; sumG += bG; sumB += bB; sumA += 1.0f;
                        } else {
                            sumR += fR; sumG += fG; sumB += fB; sumA += 1.0f;
                        }
                    }
                }
            }

            if (sumA <= 0.001f) {
                pPix[y * size + x] = 0;
            } else {
                BYTE a = (BYTE)(sumA * invTotalSamples * 255.0f + 0.5f);
                BYTE pr = (BYTE)(sumR * invTotalSamples + 0.5f);
                BYTE pg = (BYTE)(sumG * invTotalSamples + 0.5f);
                BYTE pb = (BYTE)(sumB * invTotalSamples + 0.5f);

                pPix[y * size + x] = ((DWORD)a << 24) | ((DWORD)pr << 16) | ((DWORD)pg << 8) | (DWORD)pb;
            }
        }
    }

    GdiFlush();    
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(hdc, cx - rCeil, cy - rCeil, size, size, memDC, 0, 0, size, size, bf);

    SelectObject(memDC, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(memDC);
}

 
static inline void draw_aa_playhead_triangle(HDC hdc, int tipX, int topY, int halfW, int height, COLORREF color) {
    int w = halfW * 2 + 3;
    int h = height + 2;

    HDC memDC = CreateCompatibleDC(hdc);
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hBmp || !pBits) {
        if (memDC) DeleteDC(memDC);
        return;
    }

    HGDIOBJ oldBmp = SelectObject(memDC, hBmp);
    DWORD* pPix = (DWORD*)pBits;
    float cR = (float)GetRValue(color), cG = (float)GetGValue(color), cB = (float)GetBValue(color);
    float centerX = (float)halfW + 1.0f;

    for (int y = 0; y < h; ++y) {
        float fy = (float)y;
        float prog = (height > 0) ? (fy / (float)height) : 1.0f;
        if (prog > 1.0f) prog = 1.0f;
        float allowedHalfW = (float)halfW * (1.0f - prog);

        for (int x = 0; x < w; ++x) {
            float distFromCenter = fabsf((float)x - centerX);
            float alphaX = 0.5f - (distFromCenter - allowedHalfW);
            float alphaY = (y <= height) ? 1.0f : (0.5f - ((float)y - (float)height));
            float alpha = alphaX * alphaY;

            if (alpha < 0.0f) alpha = 0.0f;
            if (alpha > 1.0f) alpha = 1.0f;

            BYTE a = (BYTE)(alpha * 255.0f);
            BYTE pr = (BYTE)(cR * alpha);
            BYTE pg = (BYTE)(cG * alpha);
            BYTE pb = (BYTE)(cB * alpha);

            pPix[y * w + x] = ((DWORD)a << 24) | ((DWORD)pr << 16) | ((DWORD)pg << 8) | (DWORD)pb;
        }
    }

    GdiFlush();    
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(hdc, tipX - halfW - 1, topY, w, h, memDC, 0, 0, w, h, bf);

    SelectObject(memDC, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(memDC);
}

 
static inline void draw_aa_triangle(HDC hdc, int cx, int topY, int halfW, int height, COLORREF color, bool apexUp) {
    int w = halfW * 2 + 3;
    int h = height + 2;

    HDC memDC = CreateCompatibleDC(hdc);
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hBmp || !pBits) {
        if (memDC) DeleteDC(memDC);
        return;
    }

    HGDIOBJ oldBmp = SelectObject(memDC, hBmp);
    DWORD* pPix = (DWORD*)pBits;
    float cR = (float)GetRValue(color), cG = (float)GetGValue(color), cB = (float)GetBValue(color);
    float centerX = (float)halfW + 1.0f;

    for (int y = 0; y < h; ++y) {
        float fy = (float)y;
        float prog = (height > 0) ? (fy / (float)height) : 1.0f;
        if (prog > 1.0f) prog = 1.0f;
        float allowedHalfW = apexUp ? ((float)halfW * prog) : ((float)halfW * (1.0f - prog));

        for (int x = 0; x < w; ++x) {
            float distFromCenter = fabsf((float)x - centerX);
            float alphaX = 0.5f - (distFromCenter - allowedHalfW);
            float alphaY = (y <= height) ? 1.0f : (0.5f - ((float)y - (float)height));
            float alpha = alphaX * alphaY;

            if (alpha < 0.0f) alpha = 0.0f;
            if (alpha > 1.0f) alpha = 1.0f;

            BYTE a = (BYTE)(alpha * 255.0f);
            BYTE pr = (BYTE)(cR * alpha);
            BYTE pg = (BYTE)(cG * alpha);
            BYTE pb = (BYTE)(cB * alpha);

            pPix[y * w + x] = ((DWORD)a << 24) | ((DWORD)pr << 16) | ((DWORD)pg << 8) | (DWORD)pb;
        }
    }

    GdiFlush();    
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(hdc, cx - halfW - 1, topY, w, h, memDC, 0, 0, w, h, bf);

    SelectObject(memDC, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(memDC);
}

 
static inline void draw_vis_hybrid_badge(HDC hdc, int slotIdx, int y) {
    int bx = 0, bw = 0;
    get_topbar_slot_bounds(hdc, slotIdx, &bx, &bw);

    int badgeH = scale_y(21);
    int badgeY = y + scale_y(1);
    RECT pillRc = { bx, badgeY, bx + bw, badgeY + badgeH };

    bool isOpen = (g_visHwnd && IsWindow(g_visHwnd) && IsWindowVisible(g_visHwnd));

     
    bool isHover = false;
    {
        POINT curPt;
        if (GetCursorPos(&curPt)) {
            isHover = (WindowFromPoint(curPt) == g_hWnd);
            if (isHover) {
                ScreenToClient(g_hWnd, &curPt);
            isHover = (curPt.x >= bx && curPt.x <= bx + bw &&
                       curPt.y >= badgeY && curPt.y <= badgeY + badgeH);
            }
        }
    }
    InterlockedExchange(&g_visBadgeHover, isHover ? 1 : 0);

    COLORREF tintCol = hsl_to_rgb(g_Vis.hue, 0.35f, 0.60f);
    COLORREF bgCol = isOpen ? RGB(24, 34, 48) : (isHover ? RGB(26, 32, 42) : RGB(20, 24, 30));
    COLORREF borderCol = isHover ? hsl_to_rgb(g_Vis.hue, 0.15f, 0.68f)
                       : (isOpen ? tintCol : RGB(35, 41, 51));

     
    HBRUSH br = CreateSolidBrush(bgCol);
    HPEN pn = CreatePen(PS_SOLID, 1, borderCol);
    HGDIOBJ ob = SelectObject(hdc, br);
    HGDIOBJ op = SelectObject(hdc, pn);
    RoundRect(hdc, pillRc.left, pillRc.top, pillRc.right, pillRc.bottom, scale_x(8), scale_y(8));
    SelectObject(hdc, op);
    SelectObject(hdc, ob);
    DeleteObject(pn);
    DeleteObject(br);

     
    static float s_attractLevel = 0.0f;
    {
        LONG wp = InterlockedCompareExchange(&g_visRing.writePos, 0, 0);
        float peak = 0.0f;
        for (int i = 0; i < 128; ++i) {
            float v = fabsf(g_visRing.bufferL[(wp - 1 - i * 8) & (VIS_RING_SIZE - 1)]);
            if (v > peak) peak = v;
        }
        float target = peak * 1.75f;
        if (target > 1.0f) target = 1.0f;
        s_attractLevel += (target - s_attractLevel) * ((target > s_attractLevel) ? 0.3f : 0.05f);
    }

    float glyphAlpha = isOpen ? 0.95f : (0.20f + 0.80f * s_attractLevel);
    COLORREF glyphBase = isOpen ? tintCol : (isHover ? tintCol : RGB(170, 185, 205));

     
    int glyphW = bw - scale_x(8);
    int glyphH = badgeH - scale_y(4);
    draw_supersampled_badge_glyph(hdc, bx + bw / 2, badgeY + badgeH / 2, glyphW, glyphH, isOpen, glyphBase, glyphAlpha);
}

 
static inline bool get_pager_button_rects(int clientW, int clientH, RECT *upRc, RECT *downRc) {
    (void)clientW;
    int viewportH = clientH - get_header_height() - get_bottom_dock_height();
    if (viewportH <= 0 || g_Seq.trackCount * get_track_height() <= viewportH) return false;

    int bw = scale_x(16), bh = scale_y(18);
    int gap = scale_x(4);
    int pairW = 2 * bw + gap;
     
    int x = scale_x(8);                             
    int y = get_header_height() - bh - scale_y(2);  
    if (upRc)   SetRect(upRc,   x,           y, x + bw,           y + bh);
    if (downRc) SetRect(downRc, x + bw + gap, y, x + pairW,      y + bh);
    return true;
}

 
static inline void get_timesig_badge_rect(int clientW, int clientH, RECT* rc) {
    (void)clientW; (void)clientH;
    int bh = scale_y(18);
    int y = get_header_height() - bh - scale_y(2);
    int bw = scale_x(36);
    // Right-aligned inside the track-header column, hugging its right edge
    // so the badge sits just left of the ruler's "Bar N" labels without
    // overlapping them. The scale_x(2) is the gap to the bar area — raise
    // it to move the badge away, lower it to pull it closer.
    int x = get_track_header_width() - bw - scale_x(2);
    SetRect(rc, x, y, x + bw, y + bh);
}

 
static inline bool track_is_dimmed(int trackIdx) {
    if (trackIdx < 0 || trackIdx >= MAX_TRACKS) return true;
    if (g_Seq.trackMuted[trackIdx]) return true;
    bool anySolo = false;
    for (int t = 0; t < g_Seq.trackCount && t < MAX_TRACKS; ++t) {
        if (g_Seq.trackSolo[t]) { anySolo = true; break; }
    }
    return anySolo && !g_Seq.trackSolo[trackIdx];
}

 

 
#define GDI_CACHE_SLOTS 64

static COLORREF g_brushCacheCol[GDI_CACHE_SLOTS];
static HBRUSH   g_brushCacheHnd[GDI_CACHE_SLOTS];
static COLORREF g_penCacheCol[GDI_CACHE_SLOTS];
static HPEN     g_penCacheHnd[GDI_CACHE_SLOTS];
static int    g_gdiCacheNext = 0;

static inline HBRUSH cached_solid_brush(COLORREF col) {
    for (int i = 0; i < GDI_CACHE_SLOTS; ++i) {
        if (g_brushCacheHnd[i] && g_brushCacheCol[i] == col) return g_brushCacheHnd[i];
    }
    HBRUSH br = CreateSolidBrush(col);
    if (!br) return NULL;
    int slot = g_gdiCacheNext;
    g_gdiCacheNext = (g_gdiCacheNext + 1) % GDI_CACHE_SLOTS;
    if (g_brushCacheHnd[slot]) DeleteObject(g_brushCacheHnd[slot]);
    g_brushCacheHnd[slot] = br;
    g_brushCacheCol[slot] = col;
    return br;
}

static inline HPEN cached_solid_pen(COLORREF col) {
    for (int i = 0; i < GDI_CACHE_SLOTS; ++i) {
        if (g_penCacheHnd[i] && g_penCacheCol[i] == col) return g_penCacheHnd[i];
    }
    HPEN pn = CreatePen(PS_SOLID, 1, col);
    if (!pn) return NULL;
    int slot = g_gdiCacheNext;
    g_gdiCacheNext = (g_gdiCacheNext + 1) % GDI_CACHE_SLOTS;
    if (g_penCacheHnd[slot]) DeleteObject(g_penCacheHnd[slot]);
    g_penCacheHnd[slot] = pn;
    g_penCacheCol[slot] = col;
    return pn;
}

static inline void release_cached_gdi(void) {
    for (int i = 0; i < GDI_CACHE_SLOTS; ++i) {
        if (g_brushCacheHnd[i]) { DeleteObject(g_brushCacheHnd[i]); g_brushCacheHnd[i] = NULL; }
        if (g_penCacheHnd[i])   { DeleteObject(g_penCacheHnd[i]);   g_penCacheHnd[i] = NULL; }
    }
    g_gdiCacheNext = 0;
}

 
static HDC     g_waveScratchDC   = NULL;
static HBITMAP g_waveScratchBmp  = NULL;
static HBITMAP g_waveScratchOld  = NULL;
static DWORD*  g_waveScratchBits = NULL;
static int     g_waveScratchCapW = 0;
static int     g_waveScratchCapH = 0;

 
static float*  g_spanTop  = NULL;    
static int     g_spanTopCap = 0;
static float*  g_spanBot  = NULL;    
static int     g_spanBotCap = 0;
static float*  g_spanFrac = NULL;    
static int     g_spanFracCap = 0;
static float*  g_spanAlpha = NULL;   
static int     g_spanAlphaCap = 0;

 
static float*  g_gradLut = NULL;
static int     g_gradCap = 0;

static inline bool wave_scratch_ensure(HDC hdc, int w, int h) {
    if (g_waveScratchBits && w <= g_waveScratchCapW && h <= g_waveScratchCapH &&
        g_spanTopCap >= w && g_spanBotCap >= w && g_spanFracCap >= w &&
        g_spanAlphaCap >= w && g_gradCap >= h) {
        return true;
    }

    int newW = (w > g_waveScratchCapW) ? ((w + 255) & ~255) : g_waveScratchCapW;
    int newH = (h > g_waveScratchCapH) ? ((h +  63) &  ~63) : g_waveScratchCapH;

     
    if (g_spanTopCap < newW) {
        float* p = (float*)realloc(g_spanTop, (size_t)newW * sizeof(float));
        if (!p) return false;
        g_spanTop = p;
        g_spanTopCap = newW;
    }
    if (g_spanBotCap < newW) {
        float* p = (float*)realloc(g_spanBot, (size_t)newW * sizeof(float));
        if (!p) return false;
        g_spanBot = p;
        g_spanBotCap = newW;
    }
    if (g_spanFracCap < newW) {
        float* p = (float*)realloc(g_spanFrac, (size_t)newW * sizeof(float));
        if (!p) return false;
        g_spanFrac = p;
        g_spanFracCap = newW;
    }
    if (g_spanAlphaCap < newW) {
        float* p = (float*)realloc(g_spanAlpha, (size_t)newW * sizeof(float));
        if (!p) return false;
        g_spanAlpha = p;
        g_spanAlphaCap = newW;
    }
    if (g_gradCap < newH) {
        float* p = (float*)realloc(g_gradLut, (size_t)newH * sizeof(float));
        if (!p) return false;
        g_gradLut = p;
        g_gradCap = newH;
    }

    if (!g_waveScratchDC) {
        g_waveScratchDC = CreateCompatibleDC(hdc);
        if (!g_waveScratchDC) return false;
    }

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = newW;
    bmi.bmiHeader.biHeight = -newH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hBmp = CreateDIBSection(g_waveScratchDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hBmp || !pBits) {
        if (hBmp) DeleteObject(hBmp);
        return false;
    }
    if (g_waveScratchBmp) {
        SelectObject(g_waveScratchDC, g_waveScratchOld);
        DeleteObject(g_waveScratchBmp);
    }
    g_waveScratchBmp = hBmp;
    g_waveScratchOld = (HBITMAP)SelectObject(g_waveScratchDC, hBmp);
    g_waveScratchBits = (DWORD*)pBits;
    g_waveScratchCapW = newW;
    g_waveScratchCapH = newH;
    memset(g_waveScratchBits, 0, (size_t)newW * (size_t)newH * 4);
    return true;
}

 
static inline void wave_build_grad_lut(int h) {
    const float center = (float)(h - 1) * 0.5f;
    for (int row = 0; row < h; ++row) {
        const float dist = (center > 0.5f) ? fabsf((float)row - center) / center : 0.0f;
        g_gradLut[row] = 1.0f - 0.72f * powf(dist, 1.25f);
    }
}

 
static inline float compute_fade_dim_alpha(float gain) {
    if (gain <= 0.0f) return 0.0f;
    if (gain >= 1.0f) return 1.0f;
     
    const float gClamped = gain;
    return log10f(1.0f + 9.0f * gClamped);
}

static inline void wave_compute_spans(int w, int h,
                                      const AudioSample* s,
                                      double frameAtX0, double framesPerPx,
                                      float volume, float maxAmpPx,
                                      double playableLen, bool loopWaveform,
                                      float clipLengthBeats,
                                      float fadeInBeats, float fadeOutBeats,
                                      uint8_t fadeInType, uint8_t fadeOutType,
                                      float ppb, int startXRel,
                                      int* outTop, int* outBot) {
    const double invCount  = (s->frameCount > 0) ? 1.0 / (double)s->frameCount : 0.0;
    const float  center    = (float)(h - 1) * 0.5f;
    const float  amp       = (maxAmpPx < 2.0f) ? 2.0f : maxAmpPx;

     
    const float rectWpx    = clipLengthBeats * ppb;
    const float clipRightX = rectWpx - (float)startXRel;
    const float inApexX    = fadeInBeats * ppb - (float)startXRel;
    const float outApexX   = clipRightX - fadeOutBeats * ppb;
     
    const float volNorm    = (volume < 1.0f) ? volume : 1.0f;
    float       volK       = clamp(volNorm * 2.0f, 0.0f, 1.0f);
    volK = volK * volK * (3.0f - 2.0f * volK);    
    const float volAlpha   = 0.35f + 0.65f * volK;
    const bool  hasIn   = (fadeInBeats > 0.001f) && (inApexX > 0.0f);
    const bool  hasOut  = (fadeOutBeats > 0.001f) && (outApexX < clipRightX);
    const float invIn   = hasIn ? 1.0f / inApexX : 0.0f;
    const float invOut  = hasOut ? 1.0f / (clipRightX - outApexX) : 0.0f;

    // Mirror the audio renderer's loop policy so the waveform shows the same
    // behavior that is heard: loop only when the clip length is at least twice
    // the playable sample length; otherwise the region past the sample's end
    // is silence, not a repeat.
    const bool   loop = loopWaveform && (playableLen > 0.0);

    int gT = h, gB = 0;

    // --- Peak source selection (multi-resolution) ---------------------------
    // Deep zoom: sample PCM directly per pixel (Audacity-style true min/max).
    // Overview: pick the highest LOD level whose bin span fits under a pixel.
    // Fallback (no cache built, e.g. granular own-samples): flat line.
    // When the waveform loops we always read PCM directly so the repeat is
    // drawn accurately; the peak cache cannot wrap the playable region.
    const bool  deepZoom = s->pFrames && s->frameCount > 0 &&
                           framesPerPx > 0.0 && framesPerPx <= 4096.0;
    const bool  readPcm = deepZoom || loop;
    const Peak* pk = NULL;
    int   levelEntries = 0;
    double levelBinFrames = 0.0;
    float binsPerPx = 0.0f;
    double binPos = 0.0;
    bool havePeaks = (s->peaks && s->peakTotal > 0 && s->lodCount > 0 && invCount > 0.0);

    if (!readPcm && havePeaks) {
        // Choose the finest level whose bin is <= one pixel wide; fall back
        // to the coarsest level when zoomed far out beyond level 0.
        int chosen = s->lodCount - 1;
        for (int l = 0; l < s->lodCount; ++l) {
            double binFrames = (double)PEAK_BASE_BIN_FRAMES * pow((double)PEAK_LOD_RATIO, l);
            if (binFrames <= framesPerPx) { chosen = l; break; }
        }
        pk = s->peaks + s->lodOffset[chosen];
        levelEntries = s->lodEntries[chosen];
        levelBinFrames = (double)PEAK_BASE_BIN_FRAMES * pow((double)PEAK_LOD_RATIO, chosen);
        binsPerPx = (float)(framesPerPx * invCount * (double)levelEntries);
        binPos = frameAtX0 / levelBinFrames;
    }

    for (int x = 0; x < w; ++x, binPos += (double)binsPerPx) {
        g_spanTop[x] = 0.0f;
        g_spanBot[x] = 0.0f;
        g_spanFrac[x] = 0.0f;

        float colAlpha = volAlpha;
        if (hasIn && (float)x < inApexX) {
            const float t = (float)x * invIn;
            colAlpha *= compute_fade_dim_alpha(compute_fade_gain(t, fadeInType, true));
        }
        if (hasOut && (float)x > outApexX) {
            const float t = ((float)x - outApexX) * invOut;
            colAlpha *= compute_fade_dim_alpha(compute_fade_gain(t, fadeOutType, false));
        }
        g_spanAlpha[x] = colAlpha;

        const double f0 = frameAtX0 + (double)x * framesPerPx;
        const double f1 = f0 + framesPerPx;
        if (!(f1 > 0.0) || !(framesPerPx > 0.0)) continue;

         
        const float frac = 1.0f;

        float vMin = 0.0f, vMax = 0.0f;
        if (readPcm) {
            // True sample-accurate min/max under this pixel column. When the
            // waveform loops, wrap the interval back into the playable region
            // so the repeat is shown; otherwise clamp to the sample's end
            // (the region beyond it is silence).
            vMin = 0.0f; vMax = 0.0f;
            const ma_uint64 frameCount = s->frameCount;
            ma_uint64 fi = (f0 > 0.0) ? (ma_uint64)f0 : 0;
            ma_uint64 fe = (ma_uint64)f1;
            if (fe <= fi) fe = fi + 1;
            if (loop) {
                // Iterate across the pixel's interval, wrapping each frame back
                // to the sample's actual start (frame 0) so the repeat shows
                // the full 100% sample, matching the audio renderer. The
                // alt-slip offset only sets the entry point, not the repeats.
                for (ma_uint64 f = fi; f < fe; ++f) {
                    ma_uint64 idx = f % frameCount;
                    float mono = (s->pFrames[idx * 2 + 0] + s->pFrames[idx * 2 + 1]) * 0.5f;
                    if (mono < vMin) vMin = mono;
                    if (mono > vMax) vMax = mono;
                }
            } else {
                if (fi > frameCount) fi = frameCount;
                if (fe > frameCount) fe = frameCount;
                if (fe <= fi) fe = (fi < frameCount) ? fi + 1 : fi;
                for (ma_uint64 f = fi; f < fe; ++f) {
                    float mono = (s->pFrames[f * 2 + 0] + s->pFrames[f * 2 + 1]) * 0.5f;
                    if (mono < vMin) vMin = mono;
                    if (mono > vMax) vMax = mono;
                }
            }
            vMin *= volume;
            vMax *= volume;
        } else if (pk) {
            const ma_uint64 entryFrames = (ma_uint64)(levelBinFrames + 0.5);
            // When the clip does not loop, columns beyond the sample's end are
            // silence (flat center line), matching what the audio renderer
            // outputs for that region.
            if (!loop && f0 >= (double)s->frameCount) {
                vMin = 0.0f; vMax = 0.0f;
            } else if (binsPerPx >= 1.0f) {
                 
                double bp = binPos - floor(binPos / (double)levelEntries) * (double)levelEntries;
                int i0 = (int)floorf((float)bp);
                int i1 = (int)ceilf((float)bp + binsPerPx);
                if (i0 < 0) i0 = 0;
                if (i1 > levelEntries) i1 = levelEntries;
                if (i1 <= i0) i1 = i0 + 1;
                if (i1 > levelEntries) i1 = levelEntries;
                vMin = pk[i0].min;
                vMax = pk[i0].max;
                for (int i = i0 + 1; i < i1; ++i) {
                    if (pk[i].min < vMin) vMin = pk[i].min;
                    if (pk[i].max > vMax) vMax = pk[i].max;
                }
                vMin *= volume;
                vMax *= volume;
            } else {
                 
                float cp = (float)binPos + binsPerPx * 0.5f;
                cp -= floorf(cp / (float)levelEntries) * (float)levelEntries;
                if (cp < 0.0f) cp += (float)levelEntries;
                int i0 = (int)cp;
                int i1 = i0 + 1;
                if (i1 >= levelEntries) i1 = 0;
                const float t = cp - (float)i0;
                vMin = (pk[i0].min + (pk[i1].min - pk[i0].min) * t) * volume;
                vMax = (pk[i0].max + (pk[i1].max - pk[i0].max) * t) * volume;
            }
            (void)entryFrames;
        } else {
            // No peak cache: flat center line (same visual as silence).
            vMin = 0.0f; vMax = 0.0f;
        }
        if (vMin < -1.0f) vMin = -1.0f;
        if (vMax >  1.0f) vMax =  1.0f;
        if (vMin >  vMax) { float tv = vMin; vMin = vMax; vMax = tv; }

        float yTop = center - vMax * amp;
        float yBot = center - vMin * amp;
        if (yBot - yTop < 1.0f) {
             
            const float mid = 0.5f * (yTop + yBot);
            yTop = floorf(mid);
            yBot = yTop + 1.0f;
        }
        if (yTop < 0.0f) yTop = 0.0f;
        if (yBot > (float)h) yBot = (float)h;

        g_spanTop[x] = yTop;
        g_spanBot[x] = yBot;
        g_spanFrac[x] = frac;

        int r0 = (int)floorf(yTop);
        int r1 = (int)ceilf(yBot);
        if (r0 < gT) gT = r0;
        if (r1 > gB) gB = r1;
    }

    *outTop = gT;
    *outBot = gB;    
}

static inline void wave_raster_spans(DWORD* pPix, int stride, int w, int h,
                                     COLORREF color, int rT, int rB) {
    if (rB <= rT) return;
    const float cR = (float)GetRValue(color);
    const float cG = (float)GetGValue(color);
    const float cB = (float)GetBValue(color);

    for (int x = 0; x < w; ++x) {
        const float yTop = g_spanTop[x];
        const float yBot = g_spanBot[x];
        int r0 = (int)floorf(yTop);
        int r1 = (int)ceilf(yBot);
        if (r0 < 0) r0 = 0;
        if (r1 > h) r1 = h;
        if (r0 >= r1) continue;

        const float frac = g_spanFrac[x];
        const float colAlpha = g_spanAlpha[x];
        DWORD* col = pPix + x;
        for (int row = r0; row < r1; ++row) {
            const float rowF = (float)row;
            const float cTop = (yTop > rowF) ? yTop : rowF;
            const float cBot = (yBot < rowF + 1.0f) ? yBot : rowF + 1.0f;
            const float cov = (cBot - cTop) * frac;
            if (cov <= 0.0f) continue;

            const float aF = cov * g_gradLut[row] * colAlpha;
            BYTE a = (BYTE)(aF * 255.0f + 0.5f);
            if (a == 0) continue;

            BYTE pr = (BYTE)(cR * aF + 0.5f);
            BYTE pg = (BYTE)(cG * aF + 0.5f);
            BYTE pb = (BYTE)(cB * aF + 0.5f);
            col[(size_t)row * stride] = ((DWORD)a << 24) | ((DWORD)pr << 16) | ((DWORD)pg << 8) | (DWORD)pb;
        }
    }
}

 
static inline void draw_smooth_waveform(HDC hdc, int dstX, int dstY, int w, int h,
                                        const AudioSample* s,
                                        double frameAtX0, double framesPerPx,
                                        float volume,
                                        COLORREF color,
                                        float maxAmpPx,
                                        double playableLen, bool loopWaveform,
                                        float clipLengthBeats,
                                        float fadeInBeats, float fadeOutBeats,
                                        uint8_t fadeInType, uint8_t fadeOutType,
                                        float ppb, int startXRel) {
    if (w <= 0 || h <= 0 || !s->loaded || !s->pFrames || s->frameCount <= 0) return;
    if (!wave_scratch_ensure(hdc, w, h)) return;

    wave_build_grad_lut(h);
    int rT, rB;
    wave_compute_spans(w, h, s, frameAtX0, framesPerPx, volume, maxAmpPx,
                       playableLen, loopWaveform,
                       clipLengthBeats, fadeInBeats, fadeOutBeats,
                       fadeInType, fadeOutType, ppb, startXRel, &rT, &rB);
    if (rB <= rT) return;

    wave_raster_spans(g_waveScratchBits, g_waveScratchCapW, w, h, color, rT, rB);
    GdiFlush();

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(hdc, dstX, dstY, w, h, g_waveScratchDC, 0, 0, w, h, bf);

     
    memset(g_waveScratchBits + (size_t)rT * g_waveScratchCapW, 0,
           (size_t)(rB - rT) * g_waveScratchCapW * 4);
}

 
static inline DWORD hash_dword(DWORD hash, DWORD val);    

#define WAVE_CACHE_MAX_SLOTS 256
#define WAVE_CACHE_MAX_BYTES (64u * 1024u * 1024u)

typedef struct {
    bool     used;
    DWORD    lastUse;
    int      w, h;
    HDC      dc;
    HBITMAP  bmp, oldBmp;
    DWORD*   bits;
    size_t   bytes;
     
    DWORD    hash;
    int      sampleIndex;
    double   frameAtX0, framesPerPx;
    float    maxAmpPx;
    COLORREF waveColor;
    float    volume;
    float    fadeInBeats, fadeOutBeats;
    uint8_t  fadeInType, fadeOutType;
    bool     isMuted, isSelected;
    bool     loopWaveform;
} WaveCacheEntry;

static WaveCacheEntry g_waveCache[WAVE_CACHE_MAX_SLOTS];
static DWORD  g_waveCacheClock = 0;
static size_t g_waveCacheBytes = 0;


static inline DWORD wave_cache_make_hash(int sampleIndex, COLORREF waveColor,
                                         int w, int h,
                                         double frameAtX0, double framesPerPx,
                                         float maxAmpPx, float volume,
                                         float fadeInBeats, float fadeOutBeats,
                                         uint8_t fadeInType, uint8_t fadeOutType,
                                         bool isMuted, bool isSelected,
                                         bool loopWaveform) {
    DWORD d[2];
    DWORD hsh = 2166136261u;
    hsh = hash_dword(hsh, (DWORD)sampleIndex);
    hsh = hash_dword(hsh, (DWORD)waveColor);
    hsh = hash_dword(hsh, (DWORD)w);
    hsh = hash_dword(hsh, (DWORD)h);
    memcpy(d, &frameAtX0, sizeof(double)); hsh = hash_dword(hsh, d[0]); hsh = hash_dword(hsh, d[1]);
    memcpy(d, &framesPerPx, sizeof(double)); hsh = hash_dword(hsh, d[0]); hsh = hash_dword(hsh, d[1]);
    memcpy(d, &maxAmpPx, sizeof(float)); hsh = hash_dword(hsh, d[0]);
    memcpy(d, &volume, sizeof(float)); hsh = hash_dword(hsh, d[0]);
    memcpy(d, &fadeInBeats, sizeof(float)); hsh = hash_dword(hsh, d[0]);
    memcpy(d, &fadeOutBeats, sizeof(float)); hsh = hash_dword(hsh, d[0]);
    hsh = hash_dword(hsh, (DWORD)fadeInType);
    hsh = hash_dword(hsh, (DWORD)fadeOutType);
    hsh = hash_dword(hsh, isMuted ? 1 : 0);
    hsh = hash_dword(hsh, isSelected ? 1 : 0);
    hsh = hash_dword(hsh, loopWaveform ? 1 : 0);
    return hsh;
}

static inline void wave_cache_free_entry(WaveCacheEntry* e) {
    if (!e->used) return;
    if (e->dc) {
        if (e->bmp) {
            SelectObject(e->dc, e->oldBmp);
            DeleteObject(e->bmp);
        }
        DeleteDC(e->dc);
        e->dc = NULL;
    }
    g_waveCacheBytes -= e->bytes;
    e->used = false;
    e->bmp = NULL;
    e->bits = NULL;
    e->bytes = 0;
}

static inline void wave_cache_flush(void) {
    for (int i = 0; i < WAVE_CACHE_MAX_SLOTS; ++i) wave_cache_free_entry(&g_waveCache[i]);
    g_waveCacheBytes = 0;
}

 
static inline WaveCacheEntry* wave_cache_acquire(HDC hdc, bool* outHit,
                                                 int sampleIndex, COLORREF waveColor,
                                                 int w, int h,
                                                 double frameAtX0, double framesPerPx,
                                                 float volume, float maxAmpPx,
                                                 float fadeInBeats, float fadeOutBeats,
                                                 uint8_t fadeInType, uint8_t fadeOutType,
                                                 bool isMuted, bool isSelected,
                                                 bool loopWaveform) {
    *outHit = false;
    const DWORD hsh = wave_cache_make_hash(sampleIndex, waveColor, w, h,
                                           frameAtX0, framesPerPx, maxAmpPx, volume,
                                           fadeInBeats, fadeOutBeats, fadeInType, fadeOutType,
                                           isMuted, isSelected, loopWaveform);
    const DWORD now = ++g_waveCacheClock;

    WaveCacheEntry* freeSlot = NULL;
    WaveCacheEntry* lru = NULL;
    for (int i = 0; i < WAVE_CACHE_MAX_SLOTS; ++i) {
        WaveCacheEntry* e = &g_waveCache[i];
        if (!e->used) {
            if (!freeSlot) freeSlot = e;
            continue;
        }
        if (e->hash == hsh &&
            e->sampleIndex == sampleIndex && e->w == w && e->h == h &&
            e->waveColor == waveColor &&
            e->volume == volume &&
            e->fadeInBeats == fadeInBeats && e->fadeOutBeats == fadeOutBeats &&
            e->fadeInType == fadeInType && e->fadeOutType == fadeOutType &&
            e->isMuted == isMuted && e->isSelected == isSelected &&
            e->loopWaveform == loopWaveform &&
            e->frameAtX0 == frameAtX0 && e->framesPerPx == framesPerPx &&
            e->maxAmpPx == maxAmpPx) {
            e->lastUse = now;
            *outHit = true;
            return e;
        }
        if (!lru || e->lastUse < lru->lastUse) lru = e;
    }

    if (!freeSlot) freeSlot = lru;
    if (!freeSlot) return NULL;

     
    while (g_waveCacheBytes - freeSlot->bytes + (size_t)w * (size_t)h * 4 > WAVE_CACHE_MAX_BYTES) {
        WaveCacheEntry* victim = NULL;
        for (int i = 0; i < WAVE_CACHE_MAX_SLOTS; ++i) {
            WaveCacheEntry* e = &g_waveCache[i];
            if (e->used && e != freeSlot && (!victim || e->lastUse < victim->lastUse)) victim = e;
        }
        if (!victim) break;
        wave_cache_free_entry(victim);
    }

    if (!freeSlot->dc) {
        freeSlot->dc = CreateCompatibleDC(hdc);
        if (!freeSlot->dc) return NULL;
    }
    if (!freeSlot->used || freeSlot->w != w || freeSlot->h != h) {
        wave_cache_free_entry(freeSlot);
        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* pBits = NULL;
        HBITMAP hBmp = CreateDIBSection(freeSlot->dc, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
        if (!hBmp || !pBits) {
            if (hBmp) DeleteObject(hBmp);
            return NULL;
        }
        if (freeSlot->bmp) {
            SelectObject(freeSlot->dc, freeSlot->oldBmp);
            DeleteObject(freeSlot->bmp);
        }
        freeSlot->bmp = hBmp;
        freeSlot->oldBmp = (HBITMAP)SelectObject(freeSlot->dc, hBmp);
        freeSlot->bits = (DWORD*)pBits;
        freeSlot->w = w;
        freeSlot->h = h;
        freeSlot->bytes = (size_t)w * (size_t)h * 4;
        g_waveCacheBytes += freeSlot->bytes;
        memset(freeSlot->bits, 0, freeSlot->bytes);
    } else {
         
        memset(freeSlot->bits, 0, freeSlot->bytes);
    }

    freeSlot->used = true;
    freeSlot->lastUse = now;
    freeSlot->hash = hsh;
    freeSlot->sampleIndex = sampleIndex;
    freeSlot->waveColor = waveColor;
    freeSlot->frameAtX0 = frameAtX0;
    freeSlot->framesPerPx = framesPerPx;
    freeSlot->maxAmpPx = maxAmpPx;
    freeSlot->volume = volume;
    freeSlot->fadeInBeats = fadeInBeats;
    freeSlot->fadeOutBeats = fadeOutBeats;
    freeSlot->fadeInType = fadeInType;
    freeSlot->fadeOutType = fadeOutType;
    freeSlot->isMuted = isMuted;
    freeSlot->isSelected = isSelected;
    freeSlot->loopWaveform = loopWaveform;
    return freeSlot;
}

static inline void wave_render_shutdown(void) {
    wave_cache_flush();
    if (g_waveScratchDC) {
        if (g_waveScratchBmp) {
            SelectObject(g_waveScratchDC, g_waveScratchOld);
            DeleteObject(g_waveScratchBmp);
            g_waveScratchBmp = NULL;
        }
        DeleteDC(g_waveScratchDC);
        g_waveScratchDC = NULL;
    }
    g_waveScratchBits = NULL;
    g_waveScratchCapW = 0;
    g_waveScratchCapH = 0;
    free(g_spanTop);   g_spanTop = NULL;   g_spanTopCap = 0;
    free(g_spanBot);   g_spanBot = NULL;   g_spanBotCap = 0;
    free(g_spanFrac);  g_spanFrac = NULL;  g_spanFracCap = 0;
    free(g_spanAlpha); g_spanAlpha = NULL; g_spanAlphaCap = 0;
    free(g_gradLut);   g_gradLut = NULL;   g_gradCap = 0;
}

 
static HDC     g_scrimDC      = NULL;
static HBITMAP g_scrimBmp     = NULL;
static HBITMAP g_scrimOld     = NULL;
static DWORD*  g_scrimBits    = NULL;
static int     g_scrimCapW    = 0;
static int     g_scrimCapH    = 0;
static BYTE*   g_scrimAlpha   = NULL;    
static int     g_scrimTplW    = 0;
static int     g_scrimTplH    = 0;
static int     g_scrimTplFeather = -1;

static inline bool scrim_ensure(HDC hdc, int w, int h) {
    if (g_scrimBits && w <= g_scrimCapW && h <= g_scrimCapH) return true;

    int newW = (w > g_scrimCapW) ? ((w + 63) & ~63) : g_scrimCapW;
    int newH = (h > g_scrimCapH) ? ((h + 15) & ~15) : g_scrimCapH;

    if (!g_scrimDC) {
        g_scrimDC = CreateCompatibleDC(hdc);
        if (!g_scrimDC) return false;
    }

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = newW;
    bmi.bmiHeader.biHeight = -newH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hBmp = CreateDIBSection(g_scrimDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hBmp || !pBits) {
        if (hBmp) DeleteObject(hBmp);
        return false;
    }
    if (g_scrimBmp) {
        SelectObject(g_scrimDC, g_scrimOld);
        DeleteObject(g_scrimBmp);
    }
    g_scrimBmp = hBmp;
    g_scrimOld = (HBITMAP)SelectObject(g_scrimDC, hBmp);
    g_scrimBits = (DWORD*)pBits;
    g_scrimCapW = newW;
    g_scrimCapH = newH;
    return true;
}

static inline void draw_title_scrim(HDC hdc, int x, int y, int w, int h, COLORREF color) {
    if (w <= 0 || h <= 0) return;
    if (!scrim_ensure(hdc, w, h)) return;

    const int feather = (scale_x(4) > 0) ? scale_x(4) : 1;
    if (w != g_scrimTplW || h != g_scrimTplH || feather != g_scrimTplFeather) {
        BYTE* tpl = (BYTE*)malloc((size_t)w * (size_t)h);
        if (tpl) {
            for (int row = 0; row < h; ++row) {
                BYTE* tRow = tpl + (size_t)row * w;
                for (int cx = 0; cx < w; ++cx) {
                    float eL = (cx < feather) ? (float)cx / (float)feather : 1.0f;
                    float eR = (w - 1 - cx < feather) ? (float)(w - 1 - cx) / (float)feather : 1.0f;
                    float edge = (eL < eR) ? eL : eR;
                    tRow[cx] = (BYTE)(115.0f * edge + 0.5f);
                }
            }
            free(g_scrimAlpha);
            g_scrimAlpha = tpl;
            g_scrimTplW = w;
            g_scrimTplH = h;
            g_scrimTplFeather = feather;
        }
    }
    if (!g_scrimAlpha || g_scrimTplW != w || g_scrimTplH != h) return;

    const float cR = (float)GetRValue(color);
    const float cG = (float)GetGValue(color);
    const float cB = (float)GetBValue(color);
    const BYTE* tpl = g_scrimAlpha;
    int i = 0;
    for (int row = 0; row < h; ++row) {
        DWORD* dst = g_scrimBits + (size_t)row * g_scrimCapW;
        for (int cx = 0; cx < w; ++cx, ++i) {
            const float a = (float)tpl[i];
            BYTE pr = (BYTE)(cR * a / 255.0f + 0.5f);
            BYTE pg = (BYTE)(cG * a / 255.0f + 0.5f);
            BYTE pb = (BYTE)(cB * a / 255.0f + 0.5f);
            dst[cx] = ((DWORD)tpl[i] << 24) | ((DWORD)pr << 16) | ((DWORD)pg << 8) | (DWORD)pb;
        }
    }
    GdiFlush();    

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(hdc, x, y, w, h, g_scrimDC, 0, 0, w, h, bf);
}

 
#define CSEQ_FADE_CURVE_ALPHA 0.85f
#define CSEQ_FADE_WEDGE_ALPHA 0.12f
#define CSEQ_FADE_INDICATOR_ALPHA 0.55f    

static HDC     g_fadeSSDC   = NULL;    
static HBITMAP g_fadeSSBmp  = NULL;
static HBITMAP g_fadeSSOld  = NULL;
static DWORD*  g_fadeSSBits = NULL;
static int     g_fadeSSCapW = 0;
static int     g_fadeSSCapH = 0;

static HDC     g_fadeDC     = NULL;    
static HBITMAP g_fadeBmp    = NULL;
static HBITMAP g_fadeOld    = NULL;
static DWORD*  g_fadeBits   = NULL;
static int     g_fadeCapW   = 0;
static int     g_fadeCapH   = 0;

typedef struct {
    BYTE*   alpha;       
    int     w, h;
    uint8_t curveType;
    bool    isFadeIn;
} FadeCurveTpl;
static FadeCurveTpl g_fadeTpls[FADE_CURVE_COUNT * 2];
 
static FadeCurveTpl g_fadeMiniTpls[FADE_CURVE_COUNT * 2];

static inline HBRUSH fade_white_brush(void) {
    static HBRUSH b = NULL;
    if (!b) b = CreateSolidBrush(RGB(255, 255, 255));
    return b;
}

static inline HPEN fade_curve_pen(void) {
    static HPEN p = NULL;
    if (!p) p = CreatePen(PS_SOLID, 4, RGB(255, 255, 255));    
    return p;
}

static inline bool fade_dib_ensure(HDC hdc, HDC* pDC, HBITMAP* pBmp, HBITMAP* pOld,
                                   DWORD** pBits, int* pCapW, int* pCapH,
                                   int needW, int needH, int alignW, int alignH) {
    if (*pBits && needW <= *pCapW && needH <= *pCapH) return true;

    int newW = (needW > *pCapW) ? ((needW + alignW - 1) & ~(alignW - 1)) : *pCapW;
    int newH = (needH > *pCapH) ? ((needH + alignH - 1) & ~(alignH - 1)) : *pCapH;

    if (!*pDC) {
        *pDC = CreateCompatibleDC(hdc);
        if (!*pDC) return false;
    }

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = newW;
    bmi.bmiHeader.biHeight = -newH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pNewBits = NULL;
    HBITMAP hBmp = CreateDIBSection(*pDC, &bmi, DIB_RGB_COLORS, &pNewBits, NULL, 0);
    if (!hBmp || !pNewBits) {
        if (hBmp) DeleteObject(hBmp);
        return false;
    }
    if (*pBmp) {
        SelectObject(*pDC, *pOld);
        DeleteObject(*pBmp);
    }
    *pBmp = hBmp;
    *pOld = (HBITMAP)SelectObject(*pDC, hBmp);
    *pBits = (DWORD*)pNewBits;
    *pCapW = newW;
    *pCapH = newH;
    return true;
}

static inline bool fade_scratch_ensure(HDC hdc, int w, int h) {
    if (!fade_dib_ensure(hdc, &g_fadeSSDC, &g_fadeSSBmp, &g_fadeSSOld, &g_fadeSSBits,
                         &g_fadeSSCapW, &g_fadeSSCapH, w * 2, h * 2, 64, 64)) return false;
    return fade_dib_ensure(hdc, &g_fadeDC, &g_fadeBmp, &g_fadeOld, &g_fadeBits,
                           &g_fadeCapW, &g_fadeCapH, w, h, 64, 16);
}

 
static inline void fade_clear_ss(int w, int h) {
    const int rowBytes = w * 2 * 4;
    for (int y = 0; y < h * 2; ++y)
        memset(g_fadeSSBits + (size_t)y * g_fadeSSCapW, 0, (size_t)rowBytes);
}

 
static inline void fade_downsample_cov(BYTE* dst, int w, int h, float scale) {
    const int ssStride = g_fadeSSCapW;
    for (int y = 0; y < h; ++y) {
        const DWORD* r0 = g_fadeSSBits + (size_t)(y * 2) * ssStride;
        const DWORD* r1 = r0 + ssStride;
        BYTE* d = dst + (size_t)y * w;
        for (int x = 0; x < w; ++x) {
            int c = ((r0[x * 2]     & 0xFF) ? 1 : 0) +
                    ((r0[x * 2 + 1] & 0xFF) ? 1 : 0) +
                    ((r1[x * 2]     & 0xFF) ? 1 : 0) +
                    ((r1[x * 2 + 1] & 0xFF) ? 1 : 0);
            BYTE v = (BYTE)((float)c * 0.25f * scale * 255.0f + 0.5f);
            if (v > d[x]) d[x] = v;
        }
    }
}

 
static inline void fade_blend_alpha(HDC hdc, int x, int y, int w, int h,
                                    COLORREF col, float alpha, const BYTE* tpl) {
    if (!fade_dib_ensure(hdc, &g_fadeDC, &g_fadeBmp, &g_fadeOld, &g_fadeBits,
                         &g_fadeCapW, &g_fadeCapH, w, h, 64, 16)) return;

    const float cR = (float)GetRValue(col);
    const float cG = (float)GetGValue(col);
    const float cB = (float)GetBValue(col);
    int i = 0;
    for (int row = 0; row < h; ++row) {
        DWORD* dst = g_fadeBits + (size_t)row * g_fadeCapW;
        for (int cx = 0; cx < w; ++cx, ++i) {
            float aF = (float)tpl[i] / 255.0f * alpha;
            if (aF > 1.0f) aF = 1.0f;
            BYTE a = (BYTE)(aF * 255.0f + 0.5f);
            BYTE pr = (BYTE)(cR * aF + 0.5f);
            BYTE pg = (BYTE)(cG * aF + 0.5f);
            BYTE pb = (BYTE)(cB * aF + 0.5f);
            dst[cx] = ((DWORD)a << 24) | ((DWORD)pr << 16) | ((DWORD)pg << 8) | (DWORD)pb;
        }
    }
    GdiFlush();    

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(hdc, x, y, w, h, g_fadeDC, 0, 0, w, h, bf);
}

static inline bool fade_downsample_blend(HDC hdc, int x, int y, int w, int h,
                                         COLORREF col, float alpha) {
    static BYTE* s_cov = NULL;
    static int   s_cap = 0;
    if (s_cap < w * h) {
        BYTE* p = (BYTE*)realloc(s_cov, (size_t)w * (size_t)h);
        if (!p) return false;
        s_cov = p;
        s_cap = w * h;
    }
    memset(s_cov, 0, (size_t)w * (size_t)h);
    fade_downsample_cov(s_cov, w, h, 1.0f);
    fade_blend_alpha(hdc, x, y, w, h, col, alpha, s_cov);
    return true;
}

static inline void fade_raster_wedge_2x(int ssW, int ssH, uint8_t curveType, bool isFadeIn) {
    HGDIOBJ oldBr = SelectObject(g_fadeSSDC, fade_white_brush());
    HGDIOBJ oldPen = SelectObject(g_fadeSSDC, GetStockObject(NULL_PEN));
    // The wedge's top boundary must track the same envelope the curve line
    // draws, so the low-opacity shape hugs non-linear fades with no gap.
    // Sampled with the same rounding as fade_raster_curve_2x; gain is
    // monotonic, so the polygon boundary never self-intersects.
    POINT pts[513];
    int numPts = (ssW < 512) ? ssW : 512;
    if (numPts < 2) { SelectObject(g_fadeSSDC, oldPen); SelectObject(g_fadeSSDC, oldBr); return; }
    for (int i = 0; i < numPts; ++i) {
        float t = (float)i / (float)(numPts - 1);
        float gain = compute_fade_gain(t, curveType, isFadeIn);
        pts[i] = (POINT){ (int)(t * (float)(ssW - 1) + 0.5f),
                          (int)((1.0f - gain) * (float)(ssH - 1) + 0.5f) };
    }
    if (isFadeIn) {
        pts[numPts] = (POINT){ ssW - 1, ssH - 1 };    // close down the right edge, along the bottom
    } else {
        pts[numPts] = (POINT){ 0, ssH - 1 };          // close along the bottom, up the left edge
    }
    Polygon(g_fadeSSDC, pts, numPts + 1);
    SelectObject(g_fadeSSDC, oldPen);
    SelectObject(g_fadeSSDC, oldBr);
}

static inline void fade_raster_curve_2x(int ssW, int ssH, uint8_t curveType, bool isFadeIn) {
    HGDIOBJ oldPen = SelectObject(g_fadeSSDC, fade_curve_pen());
    POINT pts[512];
    int numPts = (ssW < 512) ? ssW : 512;
    for (int i = 0; i < numPts; ++i) {
        float t = (float)i / (float)(numPts - 1);
        
        
        float gain = compute_fade_gain(t, curveType, isFadeIn);
        pts[i] = (POINT){ (int)(t * (float)(ssW - 1) + 0.5f),
                          (int)((1.0f - gain) * (float)(ssH - 1) + 0.5f) };
    }
    Polyline(g_fadeSSDC, pts, numPts);
    SelectObject(g_fadeSSDC, oldPen);
}

static inline bool fade_template_build(HDC hdc, FadeCurveTpl* t, int w, int h,
                                       uint8_t curveType, bool isFadeIn) {
    BYTE* buf = (BYTE*)malloc((size_t)w * (size_t)h);
    if (!buf) return false;
    if (!fade_scratch_ensure(hdc, w, h)) {
        free(buf);
        return false;
    }
    memset(buf, 0, (size_t)w * (size_t)h);

    const int ssW = w * 2, ssH = h * 2;

    fade_clear_ss(w, h);
    fade_raster_wedge_2x(ssW, ssH, curveType, isFadeIn);
    fade_downsample_cov(buf, w, h, CSEQ_FADE_WEDGE_ALPHA / CSEQ_FADE_CURVE_ALPHA);

    fade_clear_ss(w, h);
    fade_raster_curve_2x(ssW, ssH, curveType, isFadeIn);
    fade_downsample_cov(buf, w, h, 1.0f);

    free(t->alpha);
    t->alpha = buf;
    t->w = w;
    t->h = h;
    t->curveType = curveType;
    t->isFadeIn = isFadeIn;
    return true;
}

static inline const FadeCurveTpl* fade_template_get(HDC hdc, int w, int h,
                                                    uint8_t curveType, bool isFadeIn) {
    if (curveType >= FADE_CURVE_COUNT) curveType = FADE_CURVE_LINEAR;
    FadeCurveTpl* t = &g_fadeTpls[curveType * 2 + (isFadeIn ? 0 : 1)];
    if (t->alpha && t->w == w && t->h == h) return t;
    return fade_template_build(hdc, t, w, h, curveType, isFadeIn) ? t : NULL;
}

 
static inline const FadeCurveTpl* fade_mini_template_get(HDC hdc, int w, int h,
                                                         uint8_t curveType, bool isFadeIn) {
    if (curveType >= FADE_CURVE_COUNT) curveType = FADE_CURVE_LINEAR;
    FadeCurveTpl* t = &g_fadeMiniTpls[curveType * 2 + (isFadeIn ? 0 : 1)];
    if (t->alpha && t->w == w && t->h == h) return t;
    return fade_template_build(hdc, t, w, h, curveType, isFadeIn) ? t : NULL;
}

static inline void fade_templates_flush(void) {
    for (int i = 0; i < FADE_CURVE_COUNT * 2; ++i) {
        free(g_fadeTpls[i].alpha);
        g_fadeTpls[i].alpha = NULL;
        g_fadeTpls[i].w = 0;
        g_fadeTpls[i].h = 0;
    }
    for (int i = 0; i < FADE_CURVE_COUNT * 2; ++i) {
        free(g_fadeMiniTpls[i].alpha);
        g_fadeMiniTpls[i].alpha = NULL;
        g_fadeMiniTpls[i].w = 0;
        g_fadeMiniTpls[i].h = 0;
    }
}

static inline void fade_render_shutdown(void) {
    fade_templates_flush();
    if (g_fadeSSDC) {
        if (g_fadeSSBmp) {
            SelectObject(g_fadeSSDC, g_fadeSSOld);
            DeleteObject(g_fadeSSBmp);
            g_fadeSSBmp = NULL;
        }
        DeleteDC(g_fadeSSDC);
        g_fadeSSDC = NULL;
    }
    g_fadeSSBits = NULL;
    g_fadeSSCapW = 0;
    g_fadeSSCapH = 0;
    if (g_fadeDC) {
        if (g_fadeBmp) {
            SelectObject(g_fadeDC, g_fadeOld);
            DeleteObject(g_fadeBmp);
            g_fadeBmp = NULL;
        }
        DeleteDC(g_fadeDC);
        g_fadeDC = NULL;
    }
    g_fadeBits = NULL;
    g_fadeCapW = 0;
    g_fadeCapH = 0;
}

 
static inline void draw_aa_line(HDC hdc, int x0, int y0, int x1, int y1, COLORREF color, float alpha) {
    int bx0 = ((x0 < x1) ? x0 : x1) - 2;
    int by0 = ((y0 < y1) ? y0 : y1) - 2;
    int bx1 = ((x0 < x1) ? x1 : x0) + 2;
    int by1 = ((y0 < y1) ? y1 : y0) + 2;
    int w = bx1 - bx0;
    int h = by1 - by0;
    if (w <= 0 || h <= 0) return;
    if (!fade_scratch_ensure(hdc, w, h)) return;

    const int ssW = w * 2, ssH = h * 2;
    (void)ssW; (void)ssH;
    fade_clear_ss(w, h);
    HGDIOBJ oldPen = SelectObject(g_fadeSSDC, fade_curve_pen());
    MoveToEx(g_fadeSSDC, (x0 - bx0) * 2, (y0 - by0) * 2, NULL);
    LineTo(g_fadeSSDC, (x1 - bx0) * 2, (y1 - by0) * 2);
    SelectObject(g_fadeSSDC, oldPen);

    fade_downsample_blend(hdc, bx0, by0, w, h, color, alpha);
}

// Draw the transient-slice preview overlay on the timeline canvas. Called from
// render_ui while the Slice dialog is open (g_slicePreview.active): dashed
// bright vertical lines mark each detected slice boundary (k >= 1) over the
// selected clips' waveforms. Uses the same beat math as the commit pipeline.
static inline void draw_slice_preview_overlay(HDC hdc, int w, int h) {
    if (!g_slicePreview.active || g_slicePreview.clipCount <= 0) return;
    const float ppb = get_pixels_per_beat();
    int viewportTop = get_header_height();
    int viewportBottom = h - get_bottom_dock_height();

    seq_lock();
    for (int ti = 0; ti < g_slicePreview.clipCount; ++ti) {
        int idx = g_slicePreview.clipIdx[ti];
        if (idx < 0 || idx >= g_Seq.clipCount) continue;
        const Clip* c = &g_Seq.clips[idx];
        if (c->isMidi) continue;
        const TransientSliceMap* m = &g_slicePreview.maps[ti];
        if (m->count < 2) continue;

        float pRate = (c->playbackRate > 0.01f) ? c->playbackRate : 1.0f;
        double beatsPerFrame = (double)g_Seq.bpm / (60.0 * (double)SAMPLE_RATE * (double)pRate);

        int clipX1 = get_track_header_width() - g_Seq.scrollX + (int)(c->startBeat * ppb);
        int cY1 = get_header_height() - g_Seq.scrollY + c->track * get_track_height();
        int cY2 = cY1 + get_track_height();
        if (cY2 <= viewportTop || cY1 >= viewportBottom) continue;

        // Dashed preview line at each slice boundary (skip slice 0 = clip start).
        for (size_t k = 1; k < m->count; ++k) {
            double beatOff = (double)m->frame_indices[k] * beatsPerFrame;
            int x = clipX1 + (int)(beatOff * (double)ppb);
            if (x < get_track_header_width() || x >= w) continue;
            // Dashed: draw a short bright dash, then skip a gap.
            int dash = scale_y(6), gap = scale_y(4);
            for (int y = cY1 + scale_y(2); y < cY2 - scale_y(2); y += dash + gap) {
                int yEnd = y + dash;
                if (yEnd > cY2 - scale_y(2)) yEnd = cY2 - scale_y(2);
                draw_aa_line(hdc, x, y, x, yEnd, RGB(255, 200, 90), 0.9f);
            }
        }
    }
    seq_unlock();
}

 
static inline void draw_aa_wedge(HDC hdc, int anchorX, int anchorY, int apexX, int apexY,
                                 COLORREF color, float alpha) {
    int bx0 = (anchorX < apexX) ? anchorX : apexX;
    int bx1 = (anchorX < apexX) ? apexX : anchorX;
    int w = bx1 - bx0;
    int h = anchorY - apexY;
    if (w <= 0 || h <= 0) return;
    if (!fade_scratch_ensure(hdc, w, h)) return;

    const int ssW = w * 2, ssH = h * 2;
    fade_clear_ss(w, h);
    HGDIOBJ oldBr = SelectObject(g_fadeSSDC, fade_white_brush());
    HGDIOBJ oldPen = SelectObject(g_fadeSSDC, GetStockObject(NULL_PEN));
    int lax = (anchorX - bx0) * 2;   if (lax > ssW - 1) lax = ssW - 1;
    int lpx = (apexX - bx0) * 2;     if (lpx > ssW - 1) lpx = ssW - 1;
    int lay = (anchorY - apexY) * 2 - 1; if (lay > ssH - 1) lay = ssH - 1;
    POINT tri[3] = { { lax, lay }, { lpx, 0 }, { lpx, lay } };
    Polygon(g_fadeSSDC, tri, 3);
    SelectObject(g_fadeSSDC, oldPen);
    SelectObject(g_fadeSSDC, oldBr);

    fade_downsample_blend(hdc, bx0, apexY, w, h, color, alpha);
}

 
static inline void draw_supersampled_fade_curve(HDC hdc, int anchorX, int anchorY, int apexX, int apexY,
                                                uint8_t curveType, bool isFadeIn,
                                                COLORREF baseCol, float alpha) {
    if (alpha <= 0.0f) return;
    if (anchorY <= apexY) return;
    int x0 = (anchorX < apexX) ? anchorX : apexX;
    int x1 = (anchorX < apexX) ? apexX : anchorX;
    int w = x1 - x0;
    int h = anchorY - apexY;
    if (w <= 1 || h <= 1) return;
    if (curveType >= FADE_CURVE_COUNT) curveType = FADE_CURVE_LINEAR;

    if (curveType == FADE_CURVE_LINEAR) {
        draw_aa_wedge(hdc, anchorX, anchorY, apexX, apexY, baseCol,
                      alpha * (CSEQ_FADE_WEDGE_ALPHA / CSEQ_FADE_CURVE_ALPHA));
        draw_aa_line(hdc, anchorX, anchorY, apexX, apexY, baseCol, alpha);
        return;
    }

    const FadeCurveTpl* t = fade_template_get(hdc, w, h, curveType, isFadeIn);
    if (t) fade_blend_alpha(hdc, x0, apexY, w, h, baseCol, alpha, t->alpha);
}

 
static bool g_waveWinOn = false;
static int  g_waveWinL = 0;
static int  g_waveWinR = 0;

static inline int get_clip_stack_count(const Clip *c) {
    if (!c) return 1;
    int count = 1;
    for (int j = 0; j < g_Seq.clipCount; ++j) {
        const Clip *other = &g_Seq.clips[j];
        if (other == c) continue;
        if (other->track == c->track && other->startBeat < total_beats()) {
            if (fabsf(c->startBeat - other->startBeat) < 0.01f &&
                fabsf(c->lengthBeats - other->lengthBeats) < 0.01f) {
                count++;
            }
        }
    }
    return count;
}

static inline void draw_waveform_clip(HDC hdc, const Clip *clip, const RECT *rect, bool isHovered, int stackCount) {
    HFONT oldFont = SELECT_UI_FONT(hdc);
    if (!clip) return;

    int clipWidth = rect->right - rect->left;
    int clipHeight = rect->bottom - rect->top;
    if (clipWidth <= 0 || clipHeight <= 0) return;

    int tIdx = clip->track >= 0 && clip->track < MAX_TRACKS ? clip->track : 0;
    TrackTheme *theme = &g_Seq.trackThemes[tIdx];
    bool isMuted = clip->isMuted ||
                   ((clip->track >= 0 && clip->track < g_Seq.trackCount) ? track_is_dimmed(clip->track) : false);

    const float ppb = get_pixels_per_beat();
    const float vol = clip->volume;

     
    if (clip->isMidi) {
        COLORREF fillCol = isMuted ? RGB(30, 33, 40) : (clip->isSelected ? theme->selectBgColor : theme->bgColor);
        COLORREF borderCol = isMuted ? RGB(58, 64, 76) : (clip->isSelected ? RGB(255, 255, 255) : theme->borderColor);
        if (isHovered && !clip->isSelected) borderCol = isMuted ? RGB(80, 88, 104) : theme->selectWaveColor;

        float edgeAlpha = 1.0f;
        if (rect->left < get_track_header_width()) {
            const int kEdgeFadeWidth = scale_x(32);
            int visibleWidth = rect->right - get_track_header_width();
            if (visibleWidth < kEdgeFadeWidth && visibleWidth > 0) {
                edgeAlpha = (float)visibleWidth / (float)kEdgeFadeWidth;
                if (edgeAlpha < 0.3f) edgeAlpha = 0.3f;
            }
        }
        if (edgeAlpha < 0.99f) {
            fillCol = RGB((BYTE)(GetRValue(fillCol) * edgeAlpha), (BYTE)(GetGValue(fillCol) * edgeAlpha), (BYTE)(GetBValue(fillCol) * edgeAlpha));
            borderCol = RGB((BYTE)(GetRValue(borderCol) * edgeAlpha), (BYTE)(GetGValue(borderCol) * edgeAlpha), (BYTE)(GetBValue(borderCol) * edgeAlpha));
        }

        HBRUSH bgBrush = cached_solid_brush(fillCol);
        HPEN borderPen = cached_solid_pen(borderCol);
        HGDIOBJ oldBrush = SelectObject(hdc, bgBrush);
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        Rectangle(hdc, rect->left, rect->top, rect->right, rect->bottom);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);

        char midiTitleBuf[128];
        if (clip->clipKind == CLIP_KIND_QUADRUM) {
            snprintf(midiTitleBuf, sizeof(midiTitleBuf), "quadrum (%d note%s)",
                     clip->midiNoteCount, clip->midiNoteCount == 1 ? "" : "s");
        } else if (clip->clipKind == CLIP_KIND_HALO) {
            snprintf(midiTitleBuf, sizeof(midiTitleBuf), "halo (%d note%s)",
                     clip->midiNoteCount, clip->midiNoteCount == 1 ? "" : "s");
        } else if (clip->sampleIndex >= 0 && clip->sampleIndex < g_Seq.sampleCount &&
            g_Seq.samples[clip->sampleIndex].loaded) {
            const char* sampleName = g_Seq.samples[clip->sampleIndex].name;
            if (clip->midiNoteCount > 0) {
                snprintf(midiTitleBuf, sizeof(midiTitleBuf), "%s (%d note%s)",
                         sampleName, clip->midiNoteCount,
                         clip->midiNoteCount == 1 ? "" : "s");
            } else {
                strncpy(midiTitleBuf, sampleName, sizeof(midiTitleBuf) - 1);
                midiTitleBuf[sizeof(midiTitleBuf) - 1] = '\0';
            }
        } else if (clip->sampleIndex < 0 && sfont_is_loaded()) {
            // No sample attached: the clip plays through the loaded SoundFont,
            // so name it from the font + the currently selected instrument.
            const char* sfName = sfont_name();
            const char* instName = sfont_preset_name(sfont_active_preset_slot());
            if (sfName && sfName[0] && instName && instName[0]) {
                if (clip->midiNoteCount > 0) {
                    snprintf(midiTitleBuf, sizeof(midiTitleBuf), "%s / %s (%d note%s)",
                             sfName, instName, clip->midiNoteCount,
                             clip->midiNoteCount == 1 ? "" : "s");
                } else {
                    snprintf(midiTitleBuf, sizeof(midiTitleBuf), "%s / %s", sfName, instName);
                }
            } else {
                snprintf(midiTitleBuf, sizeof(midiTitleBuf), "MIDI (%d note%s)",
                         clip->midiNoteCount,
                         clip->midiNoteCount == 1 ? "" : "s");
            }
        } else {
            snprintf(midiTitleBuf, sizeof(midiTitleBuf), "MIDI (%d note%s)",
                     clip->midiNoteCount,
                     clip->midiNoteCount == 1 ? "" : "s");
        }

        // Synth modules carry their brand accent through the whole clip:
        // badge bg/border/text plus the note-body color.
        COLORREF synBd = RGB(0, 0, 0), synTx = RGB(0, 0, 0), synNote = RGB(0, 0, 0);
        const char* synBadgeText = NULL;
        if (clip->clipKind == CLIP_KIND_QUADRUM) {
            synBd = RGB(56, 194, 224);  synTx = RGB(140, 235, 255);  synNote = RGB(56, 194, 224);
            synBadgeText = "DRUM";
        } else if (clip->clipKind == CLIP_KIND_HALO) {
            synBd = RGB(255, 140, 25);  synTx = RGB(255, 200, 140);  synNote = RGB(255, 140, 25);
            synBadgeText = "SYNTH";
        }
        bool isSynthClip = (synBadgeText != NULL);

        int textStartX = max(rect->left + scale_x(6), get_track_header_width() + scale_x(6));
        
         
        // Halo's "SYNTH" is 5 chars and clips at the 44px badge width; give it
        // a wider badge. Other labels (DRUM/MIDI) keep their existing width.
        int badgeW = (clip->clipKind == CLIP_KIND_HALO) ? scale_x(52)
                   : isSynthClip ? scale_x(44) : scale_x(36);
        int badgeMargin = scale_x(5);
        RECT mBadge = { rect->right - badgeW - badgeMargin, rect->top + scale_y(3), rect->right - badgeMargin, rect->top + scale_y(19) };


        bool showBadge = (mBadge.left > textStartX + scale_x(8));
        int textEndX = showBadge ? (mBadge.left - scale_x(4)) : (rect->right - scale_x(6));

        COLORREF textBase = clip->isSelected ? RGB(255, 255, 255)
                           : (isMuted ? RGB(135, 145, 160) : RGB(215, 225, 240));
        SetTextColor(hdc, RGB((BYTE)(GetRValue(textBase) * edgeAlpha),
                              (BYTE)(GetGValue(textBase) * edgeAlpha),
                              (BYTE)(GetBValue(textBase) * edgeAlpha)));

        // Only draw the title when there is room for more than a stray
        // letter or two; otherwise just show the MIDI badge.
        if (textEndX > textStartX + scale_x(46)) {
            RECT textRect = { textStartX, rect->top + scale_y(2), textEndX, rect->top + scale_y(22) };

            wchar_t midiTitleW[256];
            if (utf8_to_wide_buf(midiTitleBuf, midiTitleW, 256) > 0)
                DrawTextW(hdc, midiTitleW, -1, &textRect, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            else
                DrawTextA(hdc, midiTitleBuf, -1, &textRect, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        }

        if (showBadge) {
            HBRUSH mBg = cached_solid_brush(
                clip->clipKind == CLIP_KIND_QUADRUM ? RGB(18, 52, 64) :
                clip->clipKind == CLIP_KIND_HALO    ? RGB(64, 40, 14) :
                                                      RGB(40, 26, 66));
            HPEN mPn = cached_solid_pen(isSynthClip ? synBd : RGB(180, 140, 255));
            HGDIOBJ ob = SelectObject(hdc, mBg);
            HGDIOBJ op = SelectObject(hdc, mPn);
            RoundRect(hdc, mBadge.left, mBadge.top, mBadge.right, mBadge.bottom, 3, 3);
            SelectObject(hdc, op);
            SelectObject(hdc, ob);
            // Smaller, centered label so it sits inside the pill instead of
            // crowding the border.
            HFONT oldBadgeF = (HFONT)SelectObject(hdc, get_ui_small_font());
            SetTextColor(hdc, isSynthClip ? synTx : RGB(200, 165, 255));
            DrawTextA(hdc, isSynthClip ? synBadgeText : "MIDI", -1, &mBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdc, oldBadgeF);
        }

        if (clip->midiNoteCount > 0) {
            float lo = 127.0f, hi = 0.0f;
            for (int i = 0; i < clip->midiNoteCount && i < MIDI_MAX_NOTES; ++i) {
                float p = (float)clip->midiNotes[i].pitch;
                if (p < lo) lo = p;
                if (p > hi) hi = p;
            }
            if (hi - lo < 12.0f) { lo -= 6.0f; hi += 6.0f; }
            float range = hi - lo;
            if (range < 1.0f) range = 1.0f;

            int noteTop = rect->top + scale_y(24);
            int noteBot = rect->bottom - scale_y(4);
            if (noteBot > noteTop + 3) {
                for (int i = 0; i < clip->midiNoteCount && i < MIDI_MAX_NOTES; ++i) {
                    const MidiNote *n = &clip->midiNotes[i];
                    int nx = rect->left + (int)(n->startBeat * ppb);
                    int nw = (int)(n->lengthBeats * ppb);
                    if (nw < 4) nw = 4;
                    if (nx + nw > rect->right - scale_x(2)) nw = rect->right - scale_x(2) - nx;
                    if (nx < rect->left + scale_x(2)) { nw -= (rect->left + scale_x(2)) - nx; nx = rect->left + scale_x(2); }
                    if (nw <= 0) continue;
                    float rel = ((float)hi - (float)n->pitch) / range;
                    int ny = noteTop + (int)(rel * (float)(noteBot - noteTop));
                    int nh = max(3, (noteBot - noteTop) / max(1, (int)(hi - lo) + 1) - 1);
                    if (ny + nh > noteBot) nh = noteBot - ny;
                    if (nh <= 0) continue;

                    COLORREF nCol = isSynthClip ? synNote
                                  : (isMuted ? RGB(85, 95, 110)
                                  : (clip->isSelected ? theme->selectWaveColor : theme->waveColor));
                    if (!isSynthClip) nCol = desaturate_color_by_volume(nCol, vol);
                    if (edgeAlpha < 0.99f) {
                        nCol = RGB((BYTE)(GetRValue(nCol) * edgeAlpha), (BYTE)(GetGValue(nCol) * edgeAlpha), (BYTE)(GetBValue(nCol) * edgeAlpha));
                    }
                    HBRUSH nBr = cached_solid_brush(nCol);
                    HGDIOBJ onb = SelectObject(hdc, nBr);
                    HGDIOBJ onp = SelectObject(hdc, GetStockObject(NULL_PEN));
                    Rectangle(hdc, nx, ny, nx + nw, ny + nh);
                    SelectObject(hdc, onp);
                    SelectObject(hdc, onb);
                }
            }
        }

        
        if (stackCount > 1) {
            char stackBuf[16];
            snprintf(stackBuf, sizeof(stackBuf), "x%d", stackCount);
            int bW = scale_x(24), bH = scale_y(15);
            RECT stackRc = { rect->right - bW - scale_x(4), rect->bottom - bH - scale_y(4),
                             rect->right - scale_x(4), rect->bottom - scale_y(4) };
            if (stackRc.left > textStartX + scale_x(16)) {
                HBRUSH sBg = cached_solid_brush(RGB(38, 28, 54));
                HPEN   sPn = cached_solid_pen(RGB(170, 130, 240));
                HGDIOBJ ob = SelectObject(hdc, sBg);
                HGDIOBJ op = SelectObject(hdc, sPn);
                RoundRect(hdc, stackRc.left, stackRc.top, stackRc.right, stackRc.bottom, 4, 4);
                SelectObject(hdc, op);
                SelectObject(hdc, ob);
                HFONT oldSmallF = (HFONT)SelectObject(hdc, get_ui_small_font());
                SetTextColor(hdc, RGB(225, 205, 255));
                DrawTextA(hdc, stackBuf, -1, &stackRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(hdc, oldSmallF);
            }
        }

        SelectObject(hdc, oldFont);
        return;
    }

     
    COLORREF fillCol = isMuted ? RGB(30, 33, 40) : (clip->isSelected ? theme->selectBgColor : theme->bgColor);
    COLORREF borderCol = isMuted ? RGB(58, 64, 76) : (clip->isSelected ? RGB(255, 255, 255) : theme->borderColor);
    if (isHovered && !clip->isSelected) borderCol = isMuted ? RGB(80, 88, 104) : theme->selectWaveColor;

    fillCol = desaturate_color_by_volume(fillCol, vol);
    borderCol = desaturate_color_by_volume(borderCol, vol);

    float edgeAlpha = 1.0f;
    if (rect->left < get_track_header_width()) {
        const int kEdgeFadeWidth = scale_x(32);
        int visibleWidth = rect->right - get_track_header_width();
        if (visibleWidth < kEdgeFadeWidth && visibleWidth > 0) {
            edgeAlpha = (float)visibleWidth / (float)kEdgeFadeWidth;
            if (edgeAlpha < 0.3f) edgeAlpha = 0.3f;
        }
    }
    if (edgeAlpha < 0.99f) {
        fillCol = RGB((BYTE)(GetRValue(fillCol) * edgeAlpha), (BYTE)(GetGValue(fillCol) * edgeAlpha), (BYTE)(GetBValue(fillCol) * edgeAlpha));
        borderCol = RGB((BYTE)(GetRValue(borderCol) * edgeAlpha), (BYTE)(GetGValue(borderCol) * edgeAlpha), (BYTE)(GetBValue(borderCol) * edgeAlpha));
    }

    HBRUSH bgBrush = cached_solid_brush(fillCol);
    HPEN borderPen = cached_solid_pen(borderCol);
    HGDIOBJ oldBrush = SelectObject(hdc, bgBrush);
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    Rectangle(hdc, rect->left, rect->top, rect->right, rect->bottom);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);

     
    HRGN clipBodyRgn = CreateRectRgn(rect->left, rect->top, rect->right, rect->bottom);
    HRGN clipBodyOld = CreateRectRgn(0, 0, 0, 0);
    BOOL clipBodyHad = GetClipRgn(hdc, clipBodyOld);
    if (clipBodyHad) {
        CombineRgn(clipBodyRgn, clipBodyRgn, clipBodyOld, RGN_AND);
    }
    SelectClipRgn(hdc, clipBodyRgn);

    
    const char* sampleName = "Sample";
    if (clip->sampleIndex >= 0 && clip->sampleIndex < g_Seq.sampleCount &&
        g_Seq.samples[clip->sampleIndex].loaded) {
        sampleName = g_Seq.samples[clip->sampleIndex].name;
    }

    // Title splits into a left-aligned (ellipsized) sample name plus a
    // right-aligned rate badge, so a small clip still shows its playback
    // rate - the name is what gets truncated, never the "(1.25x)".
    char nameBuf[112];
    strncpy(nameBuf, sampleName, sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = '\0';
    char rateBuf[16];
    rateBuf[0] = '\0';
    if (fabsf(clip->playbackRate - 1.0f) > 0.01f) {
        snprintf(rateBuf, sizeof(rateBuf), "(%.2fx)", clip->playbackRate);
    }

    int textStartX = max(rect->left + scale_x(6), get_track_header_width() + scale_x(6));
    int textEndX = rect->right - scale_x(6);
    if (clip->isGranular) textEndX -= scale_x(46);

    if (clip->sampleIndex >= 0 && clip->sampleIndex < g_Seq.sampleCount) {
        AudioSample* s = &g_Seq.samples[clip->sampleIndex];
        if (s->loaded && s->pFrames && s->frameCount > 0) {
            COLORREF waveColor = isMuted ? RGB(85, 95, 110) : (clip->isSelected ? theme->selectWaveColor : theme->waveColor);
            waveColor = desaturate_color_by_volume(waveColor, vol);
            if (edgeAlpha < 0.99f) {
                waveColor = RGB((BYTE)(GetRValue(waveColor) * edgeAlpha),
                                (BYTE)(GetGValue(waveColor) * edgeAlpha),
                                (BYTE)(GetBValue(waveColor) * edgeAlpha));
            }

            float fpb = frames_per_beat(g_Seq.bpm);
            float pRate = (clip->playbackRate > 0.01f) ? clip->playbackRate : 1.0f;
            int maxAmpPx = (clipHeight / 2) - scale_y(4);
            if (maxAmpPx < 2) maxAmpPx = 2;

            int startX = max(rect->left, get_track_header_width());
            int endX = rect->right;
            if (g_waveWinOn) {
                if (startX < g_waveWinL) startX = g_waveWinL;
                if (endX > g_waveWinR)  endX = g_waveWinR;
            }

            int waveH = clipHeight - 4;
            if (waveH > 0 && endX > startX) {
                double frameAtX0 = (double)clip->sampleOffsetFrames
                                 + ((double)(startX - rect->left) / (double)ppb) * (double)fpb * (double)pRate;
                double framesPerPx = (double)fpb * (double)pRate / (double)ppb;
                int waveW = endX - startX;

                // Loop policy mirrors the audio renderer: loop only when the
                // clip length is at least twice the FULL sample length. The
                // decision is independent of the alt-slip offset so slipping
                // never flips the loop state and the waveform stays stable.
                double playableLen = (double)s->frameCount - (double)clip->sampleOffsetFrames;
                double clipLenFrames = (double)clip->lengthBeats * (double)fpb * (double)pRate;
                bool loopWaveform = ((double)s->frameCount > 0.0) &&
                                    (clipLenFrames >= (double)s->frameCount * 2.0);

                bool canCache = (!g_waveWinOn && startX == rect->left && endX == rect->right);
                bool waveAudible = (framesPerPx > 0.0);
                bool cacheHit = false;
                WaveCacheEntry* ce = NULL;
                if (canCache && waveAudible) {
                    ce = wave_cache_acquire(hdc, &cacheHit,
                                            clip->sampleIndex, waveColor, waveW, waveH,
                                            frameAtX0, framesPerPx,
                                            clip->volume, (float)maxAmpPx,
                                            clip->fadeInBeats, clip->fadeOutBeats,
                                            clip->fadeInType, clip->fadeOutType,
                                            isMuted, clip->isSelected, loopWaveform);
                }

                BLENDFUNCTION waveBf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                if (cacheHit) {
                    AlphaBlend(hdc, startX, rect->top + 2, waveW, waveH, ce->dc, 0, 0, waveW, waveH, waveBf);
                } else if (ce) {
                    if (wave_scratch_ensure(hdc, waveW, waveH)) {
                        wave_build_grad_lut(waveH);
                        int rT, rB;
                        wave_compute_spans(waveW, waveH, s, frameAtX0, framesPerPx,
                                           clip->volume, (float)maxAmpPx,
                                           playableLen, loopWaveform,
                                           clip->lengthBeats,
                                           clip->fadeInBeats, clip->fadeOutBeats,
                                           clip->fadeInType, clip->fadeOutType,
                                           ppb, startX - rect->left, &rT, &rB);
                        if (rB > rT) {
                            wave_raster_spans(ce->bits, waveW, waveW, waveH, waveColor, rT, rB);
                            GdiFlush();
                            AlphaBlend(hdc, startX, rect->top + 2, waveW, waveH, ce->dc, 0, 0, waveW, waveH, waveBf);
                        }
                    }
                } else if (waveAudible) {
                    draw_smooth_waveform(hdc, startX, rect->top + 2, waveW, waveH,
                                         s, frameAtX0, framesPerPx,
                                         clip->volume,
                                         waveColor,
                                         (float)maxAmpPx,
                                         playableLen, loopWaveform,
                                         clip->lengthBeats,
                                         clip->fadeInBeats, clip->fadeOutBeats,
                                         clip->fadeInType, clip->fadeOutType,
                                         ppb, startX - rect->left);
                }

                 
                // Loop marker: show where the sample ends and looping (or
                // silence) begins, whenever the clip is longer than the
                // playable sample — not only when the clip was alt-slipped.
                if (clipLenFrames > playableLen) {
                    double fc = (double)s->frameCount;
                    double k0 = ceil(frameAtX0 / fc);
                    double loopFrame = k0 * fc;
                    double dx = (loopFrame - frameAtX0) / framesPerPx;
                    int loopX = startX + (int)(dx + 0.5);
                    if (loopX >= startX && loopX <= endX) {
                        int yBottom = rect->bottom - scale_y(1);
                        int yTop = rect->bottom - scale_y(12);
                        draw_aa_line(hdc, loopX, yTop, loopX, yBottom, RGB(255, 255, 255), 0.35f);
                    }
                }
            }
        }
    }

    {
        // Scrim covers the visible title: ellipsized name portion + rate badge.
        SIZE szName = { 0 }, szRate = { 0 };
        wchar_t nameW[112], rateW[16];
        if (utf8_to_wide_buf(nameBuf, nameW, 112) > 0)
            GetTextExtentPoint32W(hdc, nameW, (int)wcslen(nameW), &szName);
        else
            GetTextExtentPoint32A(hdc, nameBuf, (int)strlen(nameBuf), &szName);
        if (rateBuf[0]) {
            if (utf8_to_wide_buf(rateBuf, rateW, 16) > 0)
                GetTextExtentPoint32W(hdc, rateW, (int)wcslen(rateW), &szRate);
            else
                GetTextExtentPoint32A(hdc, rateBuf, (int)strlen(rateBuf), &szRate);
        }
        int availW = textEndX - textStartX;
        int nameWanted = szName.cx;
        int nameVisible = nameWanted;
        if (nameVisible > availW) nameVisible = (availW > 0) ? availW : 0;
        int scrimW = nameVisible + szRate.cx + scale_x(6);
        int maxW = textEndX - textStartX + scale_x(6);
        if (scrimW > maxW) scrimW = maxW;
        draw_title_scrim(hdc, textStartX - scale_x(3), rect->top + scale_y(1), scrimW, scale_y(24), fillCol);
    }

    
    COLORREF trackBaseCol = isMuted ? RGB(90, 100, 115) : (clip->isSelected ? theme->selectWaveColor : theme->waveColor);
    trackBaseCol = desaturate_color_by_volume(trackBaseCol, vol);
    BYTE fadeR = (BYTE)(GetRValue(trackBaseCol) + (255 - GetRValue(trackBaseCol)) * 0.55f);
    BYTE fadeG = (BYTE)(GetGValue(trackBaseCol) + (255 - GetGValue(trackBaseCol)) * 0.55f);
    BYTE fadeB = (BYTE)(GetBValue(trackBaseCol) + (255 - GetBValue(trackBaseCol)) * 0.55f);
    if (edgeAlpha < 0.99f) {
        fadeR = (BYTE)(fadeR * edgeAlpha);
        fadeG = (BYTE)(fadeG * edgeAlpha);
        fadeB = (BYTE)(fadeB * edgeAlpha);
    }
    COLORREF fadeColor = RGB(fadeR, fadeG, fadeB);

    int inApexX = rect->left + (int)(clip->fadeInBeats * ppb);
    int outApexX = rect->right - (int)(clip->fadeOutBeats * ppb);
    if (inApexX > rect->right) inApexX = rect->right;
    if (outApexX < rect->left) outApexX = rect->left;
    float fadeAlpha = CSEQ_FADE_CURVE_ALPHA * edgeAlpha;

    if (clip->fadeInBeats > 0.001f) {
        if (inApexX - rect->left > 1 && clipHeight > 1) {
            draw_supersampled_fade_curve(hdc, rect->left, rect->bottom, inApexX, rect->top,
                                         clip->fadeInType, true, fadeColor, fadeAlpha);
        }
    }
    if (clip->fadeOutBeats > 0.001f) {
        if (rect->right - outApexX > 1 && clipHeight > 1) {
            draw_supersampled_fade_curve(hdc, rect->right, rect->bottom, outApexX, rect->top,
                                         clip->fadeOutType, false, fadeColor, fadeAlpha);
        }
    }

     
    {
        const int indW = scale_x(9);
        const int indH = scale_y(8);
        if (clip->fadeInBeats > 0.001f && (inApexX - rect->left) > indW + 4 && clipHeight > indH + 4) {
            const FadeCurveTpl* tpl = fade_mini_template_get(hdc, indW, indH, clip->fadeInType, true);
            if (tpl) fade_blend_alpha(hdc, rect->left + 2, rect->top + 2, indW, indH,
                                      fadeColor, CSEQ_FADE_INDICATOR_ALPHA * edgeAlpha, tpl->alpha);
        }
        if (clip->fadeOutBeats > 0.001f && (rect->right - outApexX) > indW + 4 && clipHeight > indH + 4) {
            const FadeCurveTpl* tpl = fade_mini_template_get(hdc, indW, indH, clip->fadeOutType, false);
            if (tpl) fade_blend_alpha(hdc, rect->right - indW - 2, rect->top + 2, indW, indH,
                                      fadeColor, CSEQ_FADE_INDICATOR_ALPHA * edgeAlpha, tpl->alpha);
        }
    }

    
    COLORREF textBase = clip->isSelected ? RGB(255, 255, 255)
                       : (isMuted ? RGB(135, 145, 160) : RGB(215, 225, 240));
    SetTextColor(hdc, RGB((BYTE)(GetRValue(textBase) * edgeAlpha),
                          (BYTE)(GetGValue(textBase) * edgeAlpha),
                          (BYTE)(GetBValue(textBase) * edgeAlpha)));

    if (textEndX > textStartX + scale_x(10)) {
        SIZE szRate = { 0, 0 };
        wchar_t rateW[16];
        bool haveRateW = false;
        if (rateBuf[0]) {
            haveRateW = (utf8_to_wide_buf(rateBuf, rateW, 16) > 0);
            if (haveRateW)
                GetTextExtentPoint32W(hdc, rateW, (int)wcslen(rateW), &szRate);
            else
                GetTextExtentPoint32A(hdc, rateBuf, (int)strlen(rateBuf), &szRate);
        }

        // Rate badge pinned to the right edge; the name yields the space.
        if (szRate.cx > 0) {
            COLORREF rateCol = clip->isSelected ? RGB(255, 235, 170) : RGB(255, 210, 120);
            SetTextColor(hdc, RGB((BYTE)(GetRValue(rateCol) * edgeAlpha),
                                  (BYTE)(GetGValue(rateCol) * edgeAlpha),
                                  (BYTE)(GetBValue(rateCol) * edgeAlpha)));
            RECT rateRect = { textEndX - szRate.cx, rect->top + scale_y(2), textEndX, rect->top + scale_y(22) };
            if (haveRateW)
                DrawTextW(hdc, rateW, -1, &rateRect, DT_SINGLELINE | DT_RIGHT | DT_VCENTER | DT_NOPREFIX);
            else
                DrawTextA(hdc, rateBuf, -1, &rateRect, DT_SINGLELINE | DT_RIGHT | DT_VCENTER | DT_NOPREFIX);
            SetTextColor(hdc, RGB((BYTE)(GetRValue(textBase) * edgeAlpha),
                                  (BYTE)(GetGValue(textBase) * edgeAlpha),
                                  (BYTE)(GetBValue(textBase) * edgeAlpha)));
        }

        int nameEndX = textEndX - szRate.cx - ((szRate.cx > 0) ? scale_x(4) : 0);
        if (nameEndX > textStartX) {
            RECT textRect = { textStartX, rect->top + scale_y(2), nameEndX, rect->top + scale_y(22) };
            wchar_t nameW[112];
            if (utf8_to_wide_buf(nameBuf, nameW, 112) > 0)
                DrawTextW(hdc, nameW, -1, &textRect, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            else
                DrawTextA(hdc, nameBuf, -1, &textRect, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
    }

    
    if (clip->isGranular) {
        RECT gBadge = { rect->right - scale_x(46), rect->top + scale_y(4), rect->right - scale_x(6), rect->top + scale_y(20) };
        if (gBadge.left > textStartX + scale_x(20)) {
            HBRUSH gBg = cached_solid_brush(RGB(22, 60, 40));
            HPEN gPn = cached_solid_pen(RGB(80, 240, 180));
            HGDIOBJ ob = SelectObject(hdc, gBg);
            HGDIOBJ op = SelectObject(hdc, gPn);
            RoundRect(hdc, gBadge.left, gBadge.top, gBadge.right, gBadge.bottom, 3, 3);
            SelectObject(hdc, op);
            SelectObject(hdc, ob);
            // Match the DRUM/SYNTH/MIDI badges: smaller, centered label.
            HFONT oldGranF = (HFONT)SelectObject(hdc, get_ui_small_font());
            SetTextColor(hdc, RGB(160, 255, 205));
            DrawTextA(hdc, "GRAN", -1, &gBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdc, oldGranF);
        }
    }

    
    if (stackCount > 1) {
        char stackBuf[16];
        snprintf(stackBuf, sizeof(stackBuf), "x%d", stackCount);
        int bW = scale_x(24), bH = scale_y(15);
        RECT stackRc = { rect->right - bW - scale_x(4), rect->bottom - bH - scale_y(4),
                         rect->right - scale_x(4), rect->bottom - scale_y(4) };
        if (stackRc.left > textStartX + scale_x(16)) {
            HBRUSH sBg = cached_solid_brush(RGB(28, 36, 48));
            HPEN   sPn = cached_solid_pen(RGB(90, 140, 210));
            HGDIOBJ ob = SelectObject(hdc, sBg);
            HGDIOBJ op = SelectObject(hdc, sPn);
            RoundRect(hdc, stackRc.left, stackRc.top, stackRc.right, stackRc.bottom, 4, 4);
            SelectObject(hdc, op);
            SelectObject(hdc, ob);
            HFONT oldSmallF = (HFONT)SelectObject(hdc, get_ui_small_font());
            SetTextColor(hdc, RGB(190, 220, 255));
            DrawTextA(hdc, stackBuf, -1, &stackRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdc, oldSmallF);
        }
    }

     
    if (clipBodyHad) SelectClipRgn(hdc, clipBodyOld);
    else             SelectClipRgn(hdc, NULL);
    DeleteObject(clipBodyOld);
    DeleteObject(clipBodyRgn);

    SelectObject(hdc, oldFont);
}

 
static HDC g_cacheDC = NULL;
static HBITMAP g_cacheBmp = NULL;
static HBITMAP g_cacheOldBmp = NULL;
static int g_cacheW = 0;
static int g_cacheH = 0;
static bool g_timelineDirty = true;

 
static int g_cacheScrollX = 0;
static int g_cacheScrollY = 0;

 
static HDC     g_mainBackDC = NULL;
static HBITMAP g_mainBackBmp = NULL;
static HBITMAP g_mainBackOldBmp = NULL;
static int     g_mainBackW = 0;
static int     g_mainBackH = 0;

 
static inline void shutdown_render_surfaces(void) {
    if (g_mainBackDC) {
        if (g_mainBackBmp) {
            SelectObject(g_mainBackDC, g_mainBackOldBmp);
            DeleteObject(g_mainBackBmp);
            g_mainBackBmp = NULL;
        }
        DeleteDC(g_mainBackDC);
        g_mainBackDC = NULL;
    }
    g_mainBackW = 0;
    g_mainBackH = 0;

    if (g_cacheDC) {
        SelectObject(g_cacheDC, g_cacheOldBmp);
        DeleteObject(g_cacheBmp);
        DeleteDC(g_cacheDC);
        g_cacheDC = NULL;
    }
    g_cacheW = 0;
    g_cacheH = 0;
    g_timelineDirty = true;

     
    wave_render_shutdown();
    fade_render_shutdown();
    if (g_scrimDC) {
        if (g_scrimBmp) {
            SelectObject(g_scrimDC, g_scrimOld);
            DeleteObject(g_scrimBmp);
            g_scrimBmp = NULL;
        }
        DeleteDC(g_scrimDC);
        g_scrimDC = NULL;
    }
    g_scrimBits = NULL;
    g_scrimCapW = 0;
    g_scrimCapH = 0;
    free(g_scrimAlpha);
    g_scrimAlpha = NULL;
    g_scrimTplW = 0;
    g_scrimTplH = 0;
    g_scrimTplFeather = -1;
    release_cached_gdi();
}

 
static inline void invalidate_timeline_cache(void) {
    g_timelineDirty = true;
    wave_cache_flush();
    fade_templates_flush();
}

static inline DWORD hash_dword(DWORD hash, DWORD val) {
    return (hash ^ val) * 16777619u;
}

static inline DWORD hash_float(DWORD hash, float val) {
    DWORD u = 0;
    memcpy(&u, &val, sizeof(float));
    return (hash ^ u) * 16777619u;
}

 
#define TIMELINE_HASH_CHUNK_BARS 32
#define TIMELINE_HASH_CHUNK_COUNT (MAX_BARS / TIMELINE_HASH_CHUNK_BARS)    

static DWORD g_chunkHash[MAX_TRACKS][TIMELINE_HASH_CHUNK_COUNT];
static int   g_hashClipCount = -1;
static DWORD g_lastParamHash = 0;
static DWORD g_lastContentHash = 0;

 
static inline bool hash_chunk_is_stale(int chunk) {
    uint64_t w = g_Seq.barDirty[chunk >> 1];
    uint64_t mask = (chunk & 1) ? 0xFFFFFFFF00000000ULL : 0x00000000FFFFFFFFULL;
    return (w & mask) != 0ULL;
}

 
static inline DWORD hash_track_chunk(int t, int chunk) {
    DWORD h = 2166136261u ^ ((DWORD)t * 2654435761u) ^ ((DWORD)chunk * 40503u);
    const float b0 = (float)chunk * TIMELINE_HASH_CHUNK_BARS * beats_per_bar();
    const float b1 = b0 + (float)TIMELINE_HASH_CHUNK_BARS * beats_per_bar();
    for (int i = 0; i < g_Seq.clipCount && i < MAX_CLIPS; ++i) {
        const Clip *c = &g_Seq.clips[i];
        if (c->track != t) continue;
        if (c->startBeat >= b1 || c->startBeat + c->lengthBeats <= b0) continue;
        h = hash_dword(h, (DWORD)c->sampleIndex);
        h = hash_dword(h, (DWORD)c->track);
        h = hash_float(h, c->startBeat);
        h = hash_float(h, c->lengthBeats);
        h = hash_dword(h, (DWORD)c->sampleOffsetFrames);
        h = hash_float(h, c->volume);
        h = hash_float(h, c->playbackRate);
        h = hash_float(h, c->fadeInBeats);
        h = hash_float(h, c->fadeOutBeats);
        h = hash_dword(h, (DWORD)c->fadeInType);
        h = hash_dword(h, (DWORD)c->fadeOutType);
        h = hash_dword(h, c->isSelected ? 1 : 0);
        h = hash_dword(h, c->isGranular ? 1 : 0);
        h = hash_dword(h, c->isMuted ? 1 : 0);
        h = hash_dword(h, c->isMidi ? 1 : 0);
        h = hash_dword(h, (DWORD)c->midiNoteCount);
    }
    return h;
}

 
static inline DWORD compute_timeline_param_hash(void) {
    DWORD hsh = 2166136261u;
    seq_lock();
    hsh = hash_dword(hsh, (DWORD)g_Seq.zoom);
    hsh = hash_float(hsh, g_Seq.bpm);    
    hsh = hash_dword(hsh, (DWORD)g_Seq.visibleBarCount);
    hsh = hash_dword(hsh, (DWORD)g_Seq.gridDivision);
    hsh = hash_dword(hsh, (DWORD)g_Seq.trackCount);
    hsh = hash_dword(hsh, (DWORD)g_Seq.clipCount);

    for (int t = 0; t < g_Seq.trackCount && t < MAX_TRACKS; ++t) {
        hsh = hash_dword(hsh, g_Seq.trackMuted[t] ? 1 : 0);
        hsh = hash_dword(hsh, g_Seq.trackSolo[t] ? 1 : 0);
        hsh = hash_float(hsh, g_Seq.trackVolume[t]);
        hsh = hash_dword(hsh, granular_is_track_enabled(t) ? 1 : 0);
        hsh = hash_dword(hsh, (g_Seq.trackFilter[t].enabled && g_Seq.trackFilter[t].typeMask != 0) ? 1 : 0);
    }
    seq_unlock();
    return hsh;
}

 
static inline DWORD compute_timeline_content_hash(void) {
    DWORD hsh = 2166136261u;
    seq_lock();
    const bool allStale = (g_allChunksStale != 0) || (g_hashClipCount != g_Seq.clipCount);
    for (int t = 0; t < g_Seq.trackCount && t < MAX_TRACKS; ++t) {
        for (int c = 0; c < TIMELINE_HASH_CHUNK_COUNT; ++c) {
            if (allStale || hash_chunk_is_stale(c)) {
                g_chunkHash[t][c] = hash_track_chunk(t, c);
            }
            hsh = hash_dword(hsh, g_chunkHash[t][c]);
        }
    }
    g_hashClipCount = g_Seq.clipCount;
    InterlockedExchange(&g_allChunksStale, 0);
    seq_unlock();
    return hsh;
}
 
static inline bool is_timeline_dirty(int w, int h) {
    static int s_lastW = 0, s_lastH = 0;

    if (w != s_lastW || h != s_lastH || g_timelineDirty) {
        s_lastW = w;
        s_lastH = h;
        return true;
    }

    if (compute_timeline_param_hash() != g_lastParamHash) return true;
    return compute_timeline_content_hash() != g_lastContentHash;
}
 
 
static inline void scroll_shift_timeline_cache(HDC hdc, int w, int h, int dx, int dy);

 
static inline bool timeline_redraw_dirty_bars(int w, int h) {
    if (!bar_bitfield_any(&g_Seq.barDirty)) return false;

    float ppb = get_pixels_per_beat();
    const float pxPerBar = beats_per_bar() * ppb;
    const int headerW = get_track_header_width();
    const int viewportTop = get_header_height();
    const int viewportBottom = h - get_bottom_dock_height();
    const int trackAreaEndY = viewportTop - g_Seq.scrollY + g_Seq.trackCount * get_track_height();

    HFONT oldFont = SELECT_UI_FONT(g_cacheDC);

    seq_lock();
    int b = 0;
    while (b < MAX_BARS) {
        if (!bar_bit_test(&g_Seq.barDirty, b)) { ++b; continue; }

        int runEnd = b + 1;
        while (runEnd < MAX_BARS && bar_bit_test(&g_Seq.barDirty, runEnd)) ++runEnd;

        const int drawEnd = min(runEnd, g_Seq.visibleBarCount);
        const int x1 = headerW - g_Seq.scrollX + (int)((float)b * pxPerBar);
        const int x2 = headerW - g_Seq.scrollX + (int)((float)drawEnd * pxPerBar);
        const int bx1 = max(headerW, x1);
        const int bx2 = min(w, x2);
        const bool visible = (b < g_Seq.visibleBarCount) && (bx2 > bx1);
        RECT band = { bx1, viewportTop, bx2, viewportBottom };
        if (band.bottom > trackAreaEndY) band.bottom = trackAreaEndY;

        if (visible && band.right > band.left && band.bottom > band.top) {
             
            FillRect(g_cacheDC, &band, cached_solid_brush(RGB(17, 19, 23)));

             
            const float gridFrac = grid_division_beat_fraction(g_Seq.gridDivision);
            const int stepsPerBeat = (int)(1.0f / gridFrac + 0.5f);
            const int stepsInBar = (int)(beats_per_bar() / gridFrac + 0.5f);
            const int gridTop = viewportTop;
            const int gridBottom = min(viewportBottom, trackAreaEndY);

            HPEN sixteenthPen = cached_solid_pen(RGB(30, 34, 42));    
            HPEN beatPen = cached_solid_pen(RGB(45, 50, 62));         
            HPEN barPen = cached_solid_pen(RGB(68, 76, 95));          
            HGDIOBJ origPen = SelectObject(g_cacheDC, sixteenthPen);
            for (int bb = b; bb < drawEnd; ++bb) {
                float barStart = (float)bb * beats_per_bar();
                int barX = headerW - g_Seq.scrollX + (int)(barStart * ppb);
                if (barX >= band.left && barX < band.right) {
                    SelectObject(g_cacheDC, barPen);
                    MoveToEx(g_cacheDC, barX, gridTop, NULL);
                    LineTo(g_cacheDC, barX, gridBottom);
                }
                for (int k = 1; k < stepsInBar; ++k) {
                    float beat = barStart + (float)k * gridFrac;
                    int gridX = headerW - g_Seq.scrollX + (int)(beat * ppb);
                    if (gridX < band.left || gridX >= band.right) continue;
                    if (k % stepsPerBeat == 0) SelectObject(g_cacheDC, beatPen);
                    else SelectObject(g_cacheDC, sixteenthPen);
                    MoveToEx(g_cacheDC, gridX, gridTop, NULL);
                    LineTo(g_cacheDC, gridX, gridBottom);
                }
            }
            // Terminating bar boundary at the end of the last bar, drawn when
            // the dirty band covers the final bar (the loop above draws bars
            // up to visibleBarCount-1, never the right edge of the last one).
            int endBarX = headerW - g_Seq.scrollX + (int)(total_beats() * ppb);
            if (endBarX >= band.left && endBarX < band.right) {
                SelectObject(g_cacheDC, barPen);
                MoveToEx(g_cacheDC, endBarX, gridTop, NULL);
                LineTo(g_cacheDC, endBarX, gridBottom);
            }
            SelectObject(g_cacheDC, origPen);

             
            g_waveWinOn = true;    
            g_waveWinL = band.left;
            g_waveWinR = band.right;

            HRGN bandClip = CreateRectRgnIndirect(&band);
            SelectClipRgn(g_cacheDC, bandClip);

                
                for (int t = 0; t < g_Seq.trackCount && t < MAX_TRACKS; ++t) {
                const int trackY1 = viewportTop - g_Seq.scrollY + t * get_track_height();
                const int trackY2 = trackY1 + get_track_height();
                if (trackY2 <= viewportTop || trackY1 >= viewportBottom) continue;

                for (int i = 0; i < g_Seq.clipCount && i < MAX_CLIPS; ++i) {
                    Clip *c = &g_Seq.clips[i];
                    if (c->track != t) continue;
                    if (c->startBeat >= total_beats()) continue;
                    if (!c->isMidi && (c->sampleIndex < 0 || c->sampleIndex >= g_Seq.sampleCount)) continue;

                    int clipX1 = headerW - g_Seq.scrollX + (int)(c->startBeat * ppb);
                    float visibleLen = c->lengthBeats;
                    if (c->startBeat + visibleLen > total_beats()) visibleLen = total_beats() - c->startBeat;
                    if (visibleLen <= 0.0f) continue;
                    int clipX2 = clipX1 + (int)(visibleLen * ppb);
                    if (clipX2 <= band.left || clipX1 >= band.right) continue;

                    int clipY1 = viewportTop - g_Seq.scrollY + c->track * get_track_height();
                    int clipY2 = clipY1 + get_track_height();
                    if (clipY2 <= viewportTop || clipY1 >= viewportBottom) continue;

                    int stackCount = get_clip_stack_count(c);
                    RECT clipRect = { clipX1, clipY1, clipX2, clipY2 };
                    draw_waveform_clip(g_cacheDC, c, &clipRect, false, stackCount);
                }
            }

            SelectClipRgn(g_cacheDC, NULL);
            DeleteObject(bandClip);
            g_waveWinOn = false;

             
            HPEN trackDivPen = cached_solid_pen(RGB(24, 27, 34));
            HGDIOBJ oldDivPen = SelectObject(g_cacheDC, trackDivPen);
            for (int t = 0; t < g_Seq.trackCount && t < MAX_TRACKS; ++t) {
                int trackY = viewportTop - g_Seq.scrollY + t * get_track_height();
                if (trackY + get_track_height() <= viewportTop || trackY >= viewportBottom) continue;
                if (trackY + get_track_height() <= band.top || trackY >= band.bottom) continue;
                MoveToEx(g_cacheDC, band.left, trackY + get_track_height(), NULL);
                LineTo(g_cacheDC, band.right, trackY + get_track_height());
            }
            SelectObject(g_cacheDC, oldDivPen);
        }

         
        for (int i = b; i < runEnd; ++i) {
            bar_bit_clear(&g_Seq.barDirty, i);
            if (!visible) {
                bar_bit_clear(&g_Seq.barValid, i);
            }
        }
        b = runEnd;
    }
    seq_unlock();

    SelectObject(g_cacheDC, oldFont);
    return true;
}

 
static inline void bars_mark_rendered(int w, const RECT *clipWin) {
    float ppb = get_pixels_per_beat();
    const float pxPerBar = beats_per_bar() * ppb;
    const int headerW = get_track_header_width();
    const int x0 = clipWin ? clipWin->left : headerW;
    const int x1 = clipWin ? clipWin->right : w;

    int b0 = (int)(((float)(x0 - headerW) + g_Seq.scrollX) / pxPerBar);
    int b1 = (int)(((float)(x1 - headerW) + g_Seq.scrollX) / pxPerBar) + 1;
    if (b0 < 0) b0 = 0;
    if (b1 >= MAX_BARS) b1 = MAX_BARS - 1;
    for (int b = b0; b <= b1; ++b) {
        bar_bit_clear(&g_Seq.barDirty, b);
        bar_bit_set(&g_Seq.barValid, b);
    }
}

 
static inline bool viewport_has_invalid_bars(int w) {
    float ppb = get_pixels_per_beat();
    const float pxPerBar = beats_per_bar() * ppb;
    const int headerW = get_track_header_width();
    if (pxPerBar < 1.0f) return true;

    int b0 = (int)((float)g_Seq.scrollX / pxPerBar);
    int b1 = (int)(((float)(w - headerW) + (float)g_Seq.scrollX) / pxPerBar) + 1;
    if (b0 < 0) b0 = 0;
    if (b1 >= g_Seq.visibleBarCount) b1 = g_Seq.visibleBarCount - 1;
    if (b1 >= MAX_BARS) b1 = MAX_BARS - 1;    
    if (b0 > b1) return false;

    for (int b = b0; b <= b1; ++b) {
        if (bar_bit_test(&g_Seq.barDirty, b) || !bar_bit_test(&g_Seq.barValid, b))
            return true;
    }
    return false;
}


static inline void update_timeline_cache(HDC hdc, int w, int h, const RECT *win) {
#ifdef CSEQ_PROFILE
     
    const double cseqT0 = cseq_prof_now_ms();
    DWORD cseqGdi0 = 0;
    int cseqClips = 0;
    bool cseqTopLevel = false;
    if (cseq_prof_depth == 0) {
        cseqGdi0 = cseq_prof_gdi_handles();
        cseq_prof_depth = 1;
        cseqTopLevel = true;
    }
#endif
     
    if (!win && !g_timelineDirty && g_cacheBmp && !is_timeline_dirty(w, h) &&
        !viewport_has_invalid_bars(w)) {
        int sdx = g_cacheScrollX - g_Seq.scrollX;
        int sdy = g_cacheScrollY - g_Seq.scrollY;
        int bandW = w - get_track_header_width();
        int bandH = h - get_header_height() - get_bottom_dock_height();
        if ((sdx == 0 || sdy == 0) && (sdx != 0 || sdy != 0) &&
            bandW > 0 && bandH > 0 &&
            abs(sdx) < bandW && abs(sdy) < bandH) {
            scroll_shift_timeline_cache(hdc, w, h, sdx, sdy);
            g_cacheScrollX = g_Seq.scrollX;
            g_cacheScrollY = g_Seq.scrollY;
#ifdef CSEQ_PROFILE
            cseq_prof_depth = 0;    
#endif
            return;
        }
    }

     
    if (!win && !g_timelineDirty && g_cacheBmp &&
        g_Seq.scrollX == g_cacheScrollX && g_Seq.scrollY == g_cacheScrollY &&
        compute_timeline_param_hash() == g_lastParamHash) {
        if (timeline_redraw_dirty_bars(w, h)) {
            seq_lock();
            bars_mark_rendered(w, NULL);
            g_lastParamHash = compute_timeline_param_hash();
            g_lastContentHash = compute_timeline_content_hash();
            seq_unlock();
#ifdef CSEQ_PROFILE
            cseq_prof_depth = 0;
#endif
            return;
        }
    }

    if (!g_cacheDC) {
        g_cacheDC = CreateCompatibleDC(hdc);
    }

    if (g_cacheW != w || g_cacheH != h || !g_cacheBmp) {
        if (g_cacheBmp) {
            SelectObject(g_cacheDC, g_cacheOldBmp);
            DeleteObject(g_cacheBmp);
        }
        g_cacheBmp = CreateCompatibleBitmap(hdc, w, h);
        g_cacheOldBmp = (HBITMAP)SelectObject(g_cacheDC, g_cacheBmp);
        g_cacheW = w;
        g_cacheH = h;
    }

    HFONT oldFont = SELECT_UI_FONT(g_cacheDC);

    RECT fullRc = {0, 0, w, h};
    const RECT *clipWin = win ? win : &fullRc;

     

    FillRect(g_cacheDC, clipWin, cached_solid_brush(RGB(17, 19, 23)));

    HRGN winClipRgn = NULL;
    if (win) {
        winClipRgn = CreateRectRgnIndirect(win);
        SelectClipRgn(g_cacheDC, winClipRgn);
    }

    int viewportTop = get_header_height();
    int viewportBottom = h - get_bottom_dock_height();
    int trackAreaEndY = get_header_height() - g_Seq.scrollY + g_Seq.trackCount * get_track_height();
    float ppb = get_pixels_per_beat();

    HRGN timelineClip = CreateRectRgn(get_track_header_width(), viewportTop, w, viewportBottom);
    if (win) {
        HRGN winRgn = CreateRectRgnIndirect(win);
        CombineRgn(timelineClip, timelineClip, winRgn, RGN_AND);
        DeleteObject(winRgn);
    }
    SelectClipRgn(g_cacheDC, timelineClip);

    HPEN sixteenthPen = cached_solid_pen(RGB(30, 34, 42));    
    HPEN beatPen = cached_solid_pen(RGB(45, 50, 62));         
    HPEN barPen = cached_solid_pen(RGB(68, 76, 95));          
    HGDIOBJ origPen = SelectObject(g_cacheDC, sixteenthPen);

    float gridFrac      = grid_division_beat_fraction(g_Seq.gridDivision);
    int   stepsPerBeat  = (int)(1.0f / gridFrac + 0.5f);
    int   stepsInBar    = (int)(beats_per_bar() / gridFrac + 0.5f);
    // Grid exists only for the project's bars — no phantom bar past the end.
    int   totalBars     = g_Seq.visibleBarCount;
    int gridTop = viewportTop;
    int gridBottom = min(viewportBottom, trackAreaEndY);

    if (gridBottom > gridTop) {
        for (int bb = 0; bb < totalBars; ++bb) {
            float barStart = (float)bb * beats_per_bar();
            int barX = get_track_header_width() - g_Seq.scrollX + (int)(barStart * ppb);
            // Gate only the bar line on its own position: a bar whose left
            // edge is scrolled off (or outside the redraw strip) still has
            // visible division lines that must be drawn.
            if (barX >= get_track_header_width() && barX <= w &&
                barX >= clipWin->left && barX <= clipWin->right) {
                SelectObject(g_cacheDC, barPen);
                MoveToEx(g_cacheDC, barX, gridTop, NULL);
                LineTo(g_cacheDC, barX, gridBottom);
            }

            for (int k = 1; k < stepsInBar; ++k) {
                float beat = barStart + (float)k * gridFrac;
                int gridX = get_track_header_width() - g_Seq.scrollX + (int)(beat * ppb);
                if (gridX < get_track_header_width() || gridX > w) continue;
                if (gridX < clipWin->left || gridX > clipWin->right) continue;
                if (k % stepsPerBeat == 0) SelectObject(g_cacheDC, beatPen);
                else SelectObject(g_cacheDC, sixteenthPen);
                MoveToEx(g_cacheDC, gridX, gridTop, NULL);
                LineTo(g_cacheDC, gridX, gridBottom);
            }
        }

        // Terminating bar boundary at the end of the last bar: the loop above
        // draws bars 0..totalBars-1, so the line at total_beats() (the right
        // edge of the final bar) is never rendered without this explicit pass.
        int endBarX = get_track_header_width() - g_Seq.scrollX + (int)(total_beats() * ppb);
        if (endBarX >= get_track_header_width() && endBarX <= w &&
            endBarX >= clipWin->left && endBarX <= clipWin->right) {
            SelectObject(g_cacheDC, barPen);
            MoveToEx(g_cacheDC, endBarX, gridTop, NULL);
            LineTo(g_cacheDC, endBarX, gridBottom);
        }
    }

    SelectObject(g_cacheDC, origPen);

    g_waveWinOn = (win != NULL);
    g_waveWinL = clipWin->left;
    g_waveWinR = clipWin->right;

    seq_lock();
    for (int i = 0; i < g_Seq.clipCount; ++i) {
        Clip *c = &g_Seq.clips[i];
        if (c->track >= g_Seq.trackCount || c->startBeat >= total_beats()) continue;
        if (!c->isMidi && (c->sampleIndex < 0 || c->sampleIndex >= g_Seq.sampleCount)) continue;

        bool isHovered = false;
        int stackCount = get_clip_stack_count(c);

        int clipX1 = get_track_header_width() - g_Seq.scrollX + (int)(c->startBeat * ppb);
        float visibleLen = c->lengthBeats;
        if (c->startBeat + visibleLen > total_beats()) visibleLen = total_beats() - c->startBeat;
        if (visibleLen <= 0.0f) continue;
        int clipX2 = clipX1 + (int)(visibleLen * ppb);

        int clipY1 = get_header_height() - g_Seq.scrollY + c->track * get_track_height();
        int clipY2 = clipY1 + get_track_height();

        if (clipY2 <= viewportTop || clipY1 >= viewportBottom) continue;
        if (clipX2 <= get_track_header_width() || clipX1 >= w) continue;
        if (clipX2 <= clipWin->left || clipX1 >= clipWin->right) continue;
        if (clipY2 <= clipWin->top || clipY1 >= clipWin->bottom) continue;

        RECT clipRect = {clipX1, clipY1, clipX2, clipY2};
        draw_waveform_clip(g_cacheDC, c, &clipRect, isHovered, stackCount);

#ifdef CSEQ_PROFILE
        cseqClips++;
#endif
    }

    seq_unlock();

    g_waveWinOn = false;

    SelectClipRgn(g_cacheDC, winClipRgn);
    DeleteObject(timelineClip);

    HPEN trackDivPen = cached_solid_pen(RGB(24, 27, 34));
    HGDIOBJ oldDivPen = SelectObject(g_cacheDC, trackDivPen);

    for (int t = 0; t < g_Seq.trackCount && t < MAX_TRACKS; ++t) {
        int trackY = get_header_height() - g_Seq.scrollY + t * get_track_height();
        if (trackY + get_track_height() <= viewportTop || trackY >= viewportBottom) continue;
        if (trackY + get_track_height() <= clipWin->top || trackY >= clipWin->bottom) continue;

        bool isMuted = g_Seq.trackMuted[t];
        bool isSolo = g_Seq.trackSolo[t];
        bool isGran = granular_is_track_enabled(t);
        bool isDim = track_is_dimmed(t);

        RECT thRect = {0, trackY, get_track_header_width(), trackY + get_track_height()};
        FillRect(g_cacheDC, &thRect, cached_solid_brush(isDim ? RGB(36, 22, 22) : RGB(22, 25, 30)));

        float tVol = (g_Seq.trackVolume[t] > 1.0f) ? 1.0f : g_Seq.trackVolume[t];
        if (tVol < 0.0f) tVol = 0.0f;
        int barHeight = (int)(tVol * (float)get_track_height());

        RECT colBar = {0, trackY + (get_track_height() - barHeight), scale_x(5), trackY + get_track_height()};
        FillRect(g_cacheDC, &colBar, cached_solid_brush(isDim ? RGB(200, 50, 50) : g_Seq.trackThemes[t % MAX_TRACKS].waveColor));

        char trackName[32];
        snprintf(trackName, sizeof(trackName), "Track %d", t + 1);
        SetBkMode(g_cacheDC, TRANSPARENT);
        SetTextColor(g_cacheDC, isDim ? RGB(220, 90, 90) : RGB(165, 175, 190));

        RECT nameRect = { scale_x(10), trackY + scale_y(5), get_track_header_width() - scale_x(6), trackY + scale_y(23) };
        DrawTextA(g_cacheDC, trackName, -1, &nameRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        
        // fx indicator
        if (g_TrackFx[t].count > 0) {
            RECT fxRect = { get_track_header_width() - scale_x(34), trackY + scale_y(5),
                            get_track_header_width() - scale_x(6), trackY + scale_y(23) };
            SetTextColor(g_cacheDC, RGB(80, 210, 240));
            DrawTextA(g_cacheDC, "FX", -1, &fxRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        // filter indicator

        int curBadgeY = trackY + scale_y(26);
        int badgeH = scale_y(13);
        int badgeStep = scale_y(15);

        if (isSolo) {
            RECT soloRect = { scale_x(10), curBadgeY, get_track_header_width() - scale_x(10), curBadgeY + badgeH };
            FillRect(g_cacheDC, &soloRect, cached_solid_brush(RGB(150, 95, 20)));
            SetTextColor(g_cacheDC, RGB(255, 215, 120));
            DrawTextA(g_cacheDC, "SOLO", -1, &soloRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            curBadgeY += badgeStep;
        }

        if (isMuted) {
            RECT muteRect = { scale_x(10), curBadgeY, get_track_header_width() - scale_x(10), curBadgeY + badgeH };
            FillRect(g_cacheDC, &muteRect, cached_solid_brush(RGB(180, 40, 40)));
            SetTextColor(g_cacheDC, RGB(255, 255, 255));
            DrawTextA(g_cacheDC, "MUTED", -1, &muteRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            curBadgeY += badgeStep;
        }

        if (isGran) {
            RECT granRect = { scale_x(10), curBadgeY, get_track_header_width() - scale_x(10), curBadgeY + badgeH };
            FillRect(g_cacheDC, &granRect, cached_solid_brush(RGB(22, 90, 55)));
            SetTextColor(g_cacheDC, RGB(160, 255, 205));
            DrawTextA(g_cacheDC, "GRAN", -1, &granRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            curBadgeY += badgeStep;
        }

        if (!isMuted && !isGran && !isSolo) {
            char volBuf[16];
            snprintf(volBuf, sizeof(volBuf), "%d%%", (int)(g_Seq.trackVolume[t] * 100.0f + 0.5f));
            SetTextColor(g_cacheDC, RGB(95, 105, 120));
            RECT volRect = { scale_x(10), trackY + scale_y(28), get_track_header_width() - scale_x(10), trackY + scale_y(46) };
            DrawTextA(g_cacheDC, volBuf, -1, &volRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        // filter indicator: white "flt" label docked at bottom-right of track header
        if (g_Seq.trackFilter[t].enabled && g_Seq.trackFilter[t].typeMask != 0) {
            RECT fltRect = { get_track_header_width() - scale_x(34),
                             trackY + get_track_height() - scale_y(24),
                             get_track_header_width() - scale_x(6),
                             trackY + get_track_height() - scale_y(4) };
            SetTextColor(g_cacheDC, isDim ? RGB(160, 160, 160) : RGB(255, 255, 255));
            DrawTextA(g_cacheDC, "flt", -1, &fltRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        MoveToEx(g_cacheDC, 0, trackY + get_track_height(), NULL);
        LineTo(g_cacheDC, w, trackY + get_track_height());
    }

    SelectObject(g_cacheDC, oldDivPen);

     
    if (clipWin->top < get_header_height()) {
        RECT headerRect = {clipWin->left, 0, clipWin->right, get_header_height()};
        FillRect(g_cacheDC, &headerRect, cached_solid_brush(RGB(24, 27, 34)));

        int headerLeft = get_track_header_width();

        if (clipWin->bottom > get_header_height() - 1) {
            HPEN headerSepPen = cached_solid_pen(RGB(42, 48, 60));
            HGDIOBJ oldHdrPen = SelectObject(g_cacheDC, headerSepPen);
            MoveToEx(g_cacheDC, clipWin->left, get_header_height() - 1, NULL);
            LineTo(g_cacheDC, clipWin->right, get_header_height() - 1);
            SelectObject(g_cacheDC, oldHdrPen);
        }

         
        HRGN rulerClip = CreateRectRgn(headerLeft, 0, clipWin->right, get_header_height() - 1);
        if (winClipRgn) {
            CombineRgn(rulerClip, rulerClip, winClipRgn, RGN_AND);
        }
        SelectClipRgn(g_cacheDC, rulerClip);

        float secondsPerBeat = 60.0f / (g_Seq.bpm > 1.0f ? g_Seq.bpm : 1.0f);

        for (int b = 0; b <= g_Seq.visibleBarCount; ++b) {
            int barX = headerLeft - g_Seq.scrollX + (int)((float)b * beats_per_bar() * ppb);
            if (barX > w) break;
            if (barX > clipWin->right) break;

            if (b < g_Seq.visibleBarCount) {
                float barWidthPx = beats_per_bar() * ppb;

                if (barX + (int)barWidthPx <= headerLeft) continue;
                if (barX + (int)barWidthPx < clipWin->left) continue;

                float barAlpha = 1.0f;
                if (barX < headerLeft) {
                    int visibleWidth = (int)(barX + barWidthPx) - headerLeft;
                    const float kFadeZone = (float)scale_x(32);
                    barAlpha = (float)visibleWidth / kFadeZone;
                    if (barAlpha > 1.0f) barAlpha = 1.0f;
                    if (barAlpha < 0.0f) barAlpha = 0.0f;
                }

                if (barAlpha <= 0.01f) continue;

                int barLeft = barX + scale_x(2);
                int barRight = barX + (int)barWidthPx - scale_x(2);
                if (barRight <= barLeft + scale_x(6)) continue;

                COLORREF bgCol = RGB(24, 27, 34);
                COLORREF barTextBase = RGB(165, 175, 190);
                COLORREF barTextColor = RGB(
                    (BYTE)(GetRValue(bgCol) + (GetRValue(barTextBase) - GetRValue(bgCol)) * barAlpha),
                    (BYTE)(GetGValue(bgCol) + (GetGValue(barTextBase) - GetGValue(bgCol)) * barAlpha),
                    (BYTE)(GetBValue(bgCol) + (GetBValue(barTextBase) - GetBValue(bgCol)) * barAlpha)
                );

                char barText[32];
                snprintf(barText, sizeof(barText), "Bar %d", b + 1);
                SetBkMode(g_cacheDC, TRANSPARENT);
                SetTextColor(g_cacheDC, barTextColor);

                RECT barRc = { barLeft, get_header_height() - scale_y(20), barRight, get_header_height() - scale_y(2) };
                DrawTextA(g_cacheDC, barText, -1, &barRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

                if (barWidthPx >= 100.0f * g_dpiScaleX) {
                    char secText[32];
                    float barSeconds = (float)b * beats_per_bar() * secondsPerBeat;
                    snprintf(secText, sizeof(secText), "%.1fs", barSeconds);

                    COLORREF secBase = RGB(90, 100, 115);
                    COLORREF secColor = RGB(
                        (BYTE)(GetRValue(bgCol) + (GetRValue(secBase) - GetRValue(bgCol)) * barAlpha),
                        (BYTE)(GetGValue(bgCol) + (GetGValue(secBase) - GetGValue(bgCol)) * barAlpha),
                        (BYTE)(GetBValue(bgCol) + (GetBValue(secBase) - GetBValue(bgCol)) * barAlpha)
                    );
                    SetTextColor(g_cacheDC, secColor);

                    int secLeft = barX + (int)(barWidthPx * 0.50f);
                    int secRight = barX + (int)barWidthPx - scale_x(4);
                    if (secRight > secLeft + scale_x(8)) {
                        RECT secRc = { secLeft, get_header_height() - scale_y(20), secRight, get_header_height() - scale_y(2) };
                        DrawTextA(g_cacheDC, secText, -1, &secRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                    }
                }
            }
        }

        SelectClipRgn(g_cacheDC, winClipRgn);
        DeleteObject(rulerClip);

         
        RECT gutterCleanRc = { 0, get_header_height() - scale_y(22), headerLeft, get_header_height() - 1 };
        FillRect(g_cacheDC, &gutterCleanRc, cached_solid_brush(RGB(24, 27, 34)));
    }  

    if (winClipRgn) {
        SelectClipRgn(g_cacheDC, NULL);
        DeleteObject(winClipRgn);
    }

    g_timelineDirty = false;
     
    seq_lock();
    bars_mark_rendered(w, win ? clipWin : NULL);
    g_lastParamHash = compute_timeline_param_hash();
    g_lastContentHash = compute_timeline_content_hash();
    seq_unlock();
    g_cacheScrollX = g_Seq.scrollX;
    g_cacheScrollY = g_Seq.scrollY;
    SelectObject(g_cacheDC, oldFont);

#ifdef CSEQ_PROFILE
    if (win) {
         
        cseq_prof_stripMs += cseq_prof_now_ms() - cseqT0;
        cseq_prof_stripCount++;
        cseq_prof_stripClips += cseqClips;
    } else if (cseqTopLevel) {
        cseq_prof_report("full-rebuild", cseqT0, cseqGdi0, cseqClips, 0, 0.0);
        cseq_prof_depth = 0;
    }
#endif
}

 
static inline void scroll_shift_timeline_cache(HDC hdc, int w, int h, int dx, int dy) {
#ifdef CSEQ_PROFILE
    const double cseqSsT0 = cseq_prof_now_ms();
    const DWORD cseqSsGdi0 = cseq_prof_gdi_handles();
    cseq_prof_stripMs = 0.0;
    cseq_prof_stripCount = 0;
    cseq_prof_stripClips = 0;
#endif
     
    if (dx != 0 && dy != 0) {
        update_timeline_cache(hdc, w, h, NULL);
#ifdef CSEQ_PROFILE
        cseq_prof_report("scroll-fallback", cseqSsT0, cseqSsGdi0, 0, 0, 0.0);
#endif
        return;
    }

    int headerW = get_track_header_width();
    int headerH = get_header_height();
    int dockTop = h - get_bottom_dock_height();
    const int kGutterFadeW = scale_x(32);    

    if (dx != 0) {
         
        RECT bandX = { headerW, 0, w, h };
        ScrollDC(g_cacheDC, dx, 0, &bandX, &bandX, NULL, NULL);

         
        RECT stripX;
        if (dx > 0) SetRect(&stripX, headerW, 0, min(w, headerW + dx), h);
        else        SetRect(&stripX, max(headerW, w + dx), 0, w, h);
        if (stripX.right > stripX.left)
            update_timeline_cache(hdc, w, h, &stripX);

         
        RECT fadeZone = { headerW, 0, min(w, headerW + kGutterFadeW), h };
        if (fadeZone.right > fadeZone.left)
            update_timeline_cache(hdc, w, h, &fadeZone);
    }

    if (dy != 0) {
         
        RECT bandY = { 0, headerH, w, dockTop };
        ScrollDC(g_cacheDC, 0, dy, &bandY, &bandY, NULL, NULL);

         
        RECT stripY;
        if (dy > 0) SetRect(&stripY, 0, headerH, w, min(dockTop, headerH + dy));
        else        SetRect(&stripY, 0, max(headerH, dockTop + dy), w, dockTop);
        if (stripY.bottom > stripY.top)
            update_timeline_cache(hdc, w, h, &stripY);
    }

#ifdef CSEQ_PROFILE
    cseq_prof_report("scroll-shift", cseqSsT0, cseqSsGdi0,
                      cseq_prof_stripClips, cseq_prof_stripCount, cseq_prof_stripMs);
#endif
}

// --- Synth module launcher glyphs --------------------------------------------
// Small vector marks for the dock buttons, tinted per brand accent (quadrum
// cyan / halo orange, matching the synthsource design language). Each mark is
// rendered at 4x resolution into a DIB and box-downsampled back, so the curved
// and diagonal strokes come out antialiased instead of jagged at small sizes.

// Box-filter downsample + premultiplied-alpha composite shared by both logos.
// The ring/body is drawn in the caller's currently selected pen color (the
// button macro selects accent or the dim hover variant before calling), and
// `dotCol` tints the halo's orbit dot. `drawBody` paints into the supersampled
// DC centered at (sw/2, sh/2) with radius r*SS.
static inline void draw_synth_logo_ss(HDC dc, int cx, int cy, int r,
                                      COLORREF dotCol,
                                      void (*drawBody)(HDC, int, int, int, COLORREF)) {
    // The ring color comes from the pen the caller has selected (accent or dim).
    COLORREF ringCol = RGB(255, 255, 255);
    {
        HGDIOBJ curPen = GetCurrentObject(dc, OBJ_PEN);
        if (curPen) {
            LOGPEN lp;
            if (GetObject(curPen, sizeof(lp), &lp) == sizeof(lp))
                ringCol = lp.lopnColor;
        }
    }

    const int SS = 4;
    int w = r * 2 + 4, h = r * 2 + 4;
    if (w <= 0 || h <= 0) return;
    int sw = w * SS, sh = h * SS;

    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = sw;
    bi.bmiHeader.biHeight = -sh;          // top-down row order
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC mem = CreateCompatibleDC(dc);
    void* srcBits = NULL;
    HBITMAP srcBmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &srcBits, NULL, 0);
    if (!srcBmp || !srcBits) { if (mem) DeleteDC(mem); return; }
    HGDIOBJ oldSrc = SelectObject(mem, srcBmp);
    memset(srcBits, 0, (size_t)sw * sh * 4);

    HPEN pn = CreatePen(PS_SOLID, SS, ringCol);
    HBRUSH br = CreateSolidBrush(ringCol);
    HGDIOBJ op = SelectObject(mem, pn);
    HGDIOBJ ob = SelectObject(mem, br);
    drawBody(mem, sw / 2, sh / 2, r * SS, dotCol);
    SelectObject(mem, ob);
    SelectObject(mem, op);
    DeleteObject(pn); DeleteObject(br);
    GdiFlush();

    // Box-downsample into a destination DIB (premultiplied alpha) and
    // composite over the button. Non-black coverage becomes alpha so the
    // transparent canvas never shows as a box.
    BITMAPINFO dbi = { 0 };
    dbi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dbi.bmiHeader.biWidth = w;
    dbi.bmiHeader.biHeight = -h;
    dbi.bmiHeader.biPlanes = 1;
    dbi.bmiHeader.biBitCount = 32;
    dbi.bmiHeader.biCompression = BI_RGB;

    HDC downDC = CreateCompatibleDC(dc);
    void* outBits = NULL;
    HBITMAP outBmp = CreateDIBSection(downDC, &dbi, DIB_RGB_COLORS, &outBits, NULL, 0);
    if (!outBmp || !outBits) {
        if (downDC) DeleteDC(downDC);
        SelectObject(mem, oldSrc); DeleteObject(srcBmp); DeleteDC(mem);
        return;
    }
    HGDIOBJ oldOut = SelectObject(downDC, outBmp);
    DWORD* srcPix = (DWORD*)srcBits;
    DWORD* dstPix = (DWORD*)outBits;

    for (int y = 0; y < h; ++y) {
        int sy0 = y * sh / h, sy1 = (y + 1) * sh / h;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < w; ++x) {
            int sx0 = x * sw / w, sx1 = (x + 1) * sw / w;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            unsigned int rSum = 0, gSum = 0, bSum = 0, covered = 0;
            unsigned int total = (unsigned int)((sy1 - sy0) * (sx1 - sx0));
            for (int yy = sy0; yy < sy1; ++yy) {
                DWORD* row = srcPix + (size_t)yy * sw;
                for (int xx = sx0; xx < sx1; ++xx) {
                    DWORD c = row[xx];
                    if (c & 0x00FFFFFF) {
                        // 32-bit DIB (BI_RGB): byte 0 = blue, byte 2 = red.
                        bSum += (c >> 0) & 0xFF;
                        gSum += (c >> 8) & 0xFF;
                        rSum += (c >> 16) & 0xFF;
                        covered++;
                    }
                }
            }
            if (covered == 0 || total == 0) {
                dstPix[y * w + x] = 0;   // fully transparent
            } else {
                float cov = (float)covered / (float)total;
                if (cov > 1.0f) cov = 1.0f;
                float rAvg = (float)rSum / (float)covered;
                float gAvg = (float)gSum / (float)covered;
                float bAvg = (float)bSum / (float)covered;
                BYTE a  = (BYTE)(cov * 255.0f + 0.5f);
                BYTE pr = (BYTE)(rAvg * cov + 0.5f);
                BYTE pg = (BYTE)(gAvg * cov + 0.5f);
                BYTE pb = (BYTE)(bAvg * cov + 0.5f);
                dstPix[y * w + x] = ((DWORD)a << 24) | ((DWORD)pr << 16) | ((DWORD)pg << 8) | pb;
            }
        }
    }

    GdiFlush();
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(dc, cx - w / 2, cy - h / 2, w, h, downDC, 0, 0, w, h, bf);

    SelectObject(downDC, oldOut);
    DeleteObject(outBmp);
    DeleteDC(downDC);
    SelectObject(mem, oldSrc);
    DeleteObject(srcBmp);
    DeleteDC(mem);
}

// Quadrum "Q": ring with a tail stroke (cyan). No secondary dot.
static inline void draw_synth_logo_q_body(HDC mem, int cx, int cy, int r, COLORREF dotCol) {
    (void)dotCol;
    HGDIOBJ oldBrush = SelectObject(mem, GetStockObject(NULL_BRUSH));
    Ellipse(mem, cx - r, cy - r, cx + r, cy + r);
    // Tail: from lower-right of the ring outward.
    MoveToEx(mem, cx + r / 2, cy + r / 2, NULL);
    LineTo(mem, cx + r + r / 2, cy + r + r / 2);
    SelectObject(mem, oldBrush);
}
static inline void draw_synth_logo_q(HDC dc, int cx, int cy, int r, COLORREF col) {
    draw_synth_logo_ss(dc, cx, cy, r, col, draw_synth_logo_q_body);
}

// Halo mark: neon ring with a small orbit dot at upper right (orange).
static inline void draw_synth_logo_halo_body(HDC mem, int cx, int cy, int r, COLORREF dotCol) {
    HGDIOBJ oldBrush = SelectObject(mem, GetStockObject(NULL_BRUSH));
    Ellipse(mem, cx - r, cy - r, cx + r, cy + r);
    SelectObject(mem, oldBrush);
    HBRUSH dotBr = CreateSolidBrush(dotCol);
    HGDIOBJ oldDot = SelectObject(mem, dotBr);
    int dr = (r > 3) ? r / 3 : 1;
    Ellipse(mem, cx + r - dr, cy - r - dr, cx + r + dr, cy - r + dr);
    SelectObject(mem, oldDot);
    DeleteObject(dotBr);
}
static inline void draw_synth_logo_halo(HDC dc, int cx, int cy, int r, COLORREF col) {
    draw_synth_logo_ss(dc, cx, cy, r, col, draw_synth_logo_halo_body);
}
 
static inline void render_ui(HDC hdc, const RECT *clientRect) {
    HFONT oldFontMain = SELECT_UI_FONT(hdc);
    int w = clientRect->right - clientRect->left;
    int h = clientRect->bottom - clientRect->top;
    if (w <= 0 || h <= 0) return;

     
    if (!g_mainBackDC) {
        g_mainBackDC = CreateCompatibleDC(hdc);
    }
    if (g_mainBackW != w || g_mainBackH != h || !g_mainBackBmp) {
        if (g_mainBackBmp) {
            SelectObject(g_mainBackDC, g_mainBackOldBmp);
            DeleteObject(g_mainBackBmp);
        }
        g_mainBackBmp = CreateCompatibleBitmap(hdc, w, h);
        g_mainBackOldBmp = (HBITMAP)SelectObject(g_mainBackDC, g_mainBackBmp);
        g_mainBackW = w;
        g_mainBackH = h;
        g_timelineDirty = true;
    }

     
    if (is_timeline_dirty(w, h) ||
        g_Seq.scrollX != g_cacheScrollX ||
        g_Seq.scrollY != g_cacheScrollY ||
        viewport_has_invalid_bars(w)) {
        update_timeline_cache(hdc, w, h, NULL);
    }
    HDC memDC = g_mainBackDC;
    HFONT oldFontMem = SELECT_UI_FONT(memDC);

    BitBlt(memDC, 0, 0, w, h, g_cacheDC, 0, 0, SRCCOPY);

    int viewportTop = get_header_height();
    int viewportBottom = h - get_bottom_dock_height();
    int trackAreaEndY = get_header_height() - g_Seq.scrollY + g_Seq.trackCount * get_track_height();
    float ppb = get_pixels_per_beat();

     
    HRGN overlayClip = CreateRectRgn(get_track_header_width(), viewportTop, w, viewportBottom);
    SelectClipRgn(memDC, overlayClip);

    seq_lock();
    int hov = g_Seq.hoveredClip;
    if (hov >= 0 && hov < g_Seq.clipCount && !g_Seq.isDraggingClip && !g_Seq.isMarqueeSelecting) {
        Clip *c = &g_Seq.clips[hov];
        if (c->track < g_Seq.trackCount && c->startBeat < total_beats()) {
            int cX1 = get_track_header_width() - g_Seq.scrollX + (int)(c->startBeat * ppb);
            float vLen = c->lengthBeats;
            if (c->startBeat + vLen > total_beats()) vLen = total_beats() - c->startBeat;
            int cX2 = cX1 + (int)(vLen * ppb);
            int cY1 = get_header_height() - g_Seq.scrollY + c->track * get_track_height();
            int cY2 = cY1 + get_track_height();

            if (cY2 > viewportTop && cY1 < viewportBottom && cX2 > get_track_header_width() && cX1 < w) {
                if (!c->isSelected) {
                    COLORREF hovCol = (c->clipKind == CLIP_KIND_QUADRUM) ? RGB(56, 194, 224)
                                    : (c->clipKind == CLIP_KIND_HALO)    ? RGB(255, 140, 25)
                                    : (c->isMidi ? RGB(180, 140, 255) : RGB(140, 185, 225));
                    HPEN hovPen = CreatePen(PS_SOLID, 1, hovCol);
                    HGDIOBJ oldP = SelectObject(memDC, hovPen);
                    HGDIOBJ oldB = SelectObject(memDC, GetStockObject(NULL_BRUSH));
                    Rectangle(memDC, cX1, cY1, cX2, cY2);
                    SelectObject(memDC, oldB);
                    SelectObject(memDC, oldP);
                    DeleteObject(hovPen);
                }

            
            if (!c->isMidi) {
                    int inApexX = cX1 + (int)(c->fadeInBeats * ppb);
                    int outApexX = cX2 - (int)(c->fadeOutBeats * ppb);
                    int apexY = cY1 + scale_y(3);
                    const float range = (float)scale_x(24);  

                     
                    bool inTopZone = (g_Seq.mouseY <= cY1 + scale_y(22));

                    float pin = 0.0f, pout = 0.0f;
                    if (inTopZone || g_Seq.isFadeInDragging || g_Seq.isFadeOutDragging) {
                        float din = sqrtf((float)((g_Seq.mouseX - inApexX) * (g_Seq.mouseX - inApexX) +
                                                  (g_Seq.mouseY - apexY) * (g_Seq.mouseY - apexY)));
                        float dout = sqrtf((float)((g_Seq.mouseX - outApexX) * (g_Seq.mouseX - outApexX) +
                                                   (g_Seq.mouseY - apexY) * (g_Seq.mouseY - apexY)));
                        pin = 1.0f - din / range;
                        pout = 1.0f - dout / range;
                    }
                    if (g_Seq.isFadeInDragging)  pin = 1.0f;
                    if (g_Seq.isFadeOutDragging) pout = 1.0f;

                    if (pin > 0.05f && inApexX >= get_track_header_width() && inApexX <= w) {
                        BYTE a = (BYTE)(pin * 255.0f + 0.5f);
                        draw_aa_circle(memDC, inApexX, apexY, (3.5f + pin) * g_dpiScaleX,
                                       RGB(a, a, a),
                                       RGB((BYTE)(35.0f * pin + 0.5f), (BYTE)(42.0f * pin + 0.5f), (BYTE)(54.0f * pin + 0.5f)),
                                       1.0f);
                    }
                    if (pout > 0.05f && outApexX >= get_track_header_width() && outApexX <= w) {
                        BYTE a = (BYTE)(pout * 255.0f + 0.5f);
                        draw_aa_circle(memDC, outApexX, apexY, (3.5f + pout) * g_dpiScaleX,
                                       RGB(a, a, a),
                                       RGB((BYTE)(35.0f * pout + 0.5f), (BYTE)(42.0f * pout + 0.5f), (BYTE)(54.0f * pout + 0.5f)),
                                       1.0f);
                    }
                }
            }
        }
    }
    seq_unlock();

    // Feature 2: transient-slice preview lines (drawn inside the overlay clip).
    draw_slice_preview_overlay(memDC, w, h);

    
    seq_lock();
    if (g_Seq.volumePopupClip >= 0 && g_Seq.volumePopupClip < g_Seq.clipCount && GetTickCount64() < g_Seq.volumePopupExpiry) {
        Clip *c = &g_Seq.clips[g_Seq.volumePopupClip];
        int clipX1 = get_track_header_width() - g_Seq.scrollX + (int)(c->startBeat * ppb);
        int clipY2 = get_header_height() - g_Seq.scrollY + (c->track + 1) * get_track_height();
        float volVal = c->volume;
        seq_unlock();

        RECT vPopRect = {clipX1 + scale_x(4), clipY2 - scale_y(20), clipX1 + scale_x(72), clipY2 - scale_y(2)};
        HBRUSH vBg = CreateSolidBrush(RGB(15, 18, 24));
        HPEN vBorder = CreatePen(PS_SOLID, 1, get_volume_gradient_color(volVal));
        HGDIOBJ oldPopB = SelectObject(memDC, vBg);
        HGDIOBJ oldPopP = SelectObject(memDC, vBorder);
        RoundRect(memDC, vPopRect.left, vPopRect.top, vPopRect.right, vPopRect.bottom, 4, 4);

        char vText[32];
        snprintf(vText, sizeof(vText), "Vol: %d%%", (int)(volVal * 100.0f + 0.5f));
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, get_volume_gradient_color(volVal));
        DrawTextA(memDC, vText, -1, &vPopRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(memDC, oldPopP);
        SelectObject(memDC, oldPopB);
        DeleteObject(vBorder);
        DeleteObject(vBg);
    } else {
        seq_unlock();
    }

    
    LONG pFrame = InterlockedCompareExchange(&g_Seq.playbackFrame, 0, 0);
    float currentBeat = frame_to_beat((ma_uint64)pFrame, g_Seq.bpm, g_Seq.swing);
    int playheadX = get_track_header_width() - g_Seq.scrollX + (int)(currentBeat * ppb);
    int playheadTop = viewportTop;
    int playheadBottom = min(viewportBottom, trackAreaEndY);

    if (playheadBottom > playheadTop && playheadX >= get_track_header_width() && playheadX <= w) {
        HPEN playheadPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
        HGDIOBJ oldPhPen = SelectObject(memDC, playheadPen);
        MoveToEx(memDC, playheadX, playheadTop + scale_y(7), NULL);
        LineTo(memDC, playheadX, playheadBottom);
        SelectObject(memDC, oldPhPen);
        DeleteObject(playheadPen);

        draw_aa_playhead_triangle(memDC, playheadX, get_header_height(), scale_x(4), scale_y(7), RGB(255, 255, 255));
    }

    
    if (g_Seq.isMarqueeSelecting && g_Seq.hasMovedPastThreshold) {
        int mX1 = max(get_track_header_width(), min(g_Seq.marqueeStartX, g_Seq.marqueeCurX));
        int mX2 = max(get_track_header_width(), max(g_Seq.marqueeStartX, g_Seq.marqueeCurX));
        int mY1 = min(g_Seq.marqueeStartY, g_Seq.marqueeCurY);
        int mY2 = max(g_Seq.marqueeStartY, g_Seq.marqueeCurY);

        if (mX2 > mX1 && mY2 > mY1) {
            draw_alpha_box(memDC, mX1, mY1, mX2 - mX1, mY2 - mY1, RGB(60, 140, 240), 65, RGB(110, 190, 255));
        }
    }

    SelectClipRgn(memDC, NULL);
    DeleteObject(overlayClip);

    // Track-header drag (Shift+drag): subtly dim the canvas except the row of
    // the track being dragged, so the target position reads without a heavy
    // overlay box.
    if (g_Seq.isTrackHeaderDragging) {
        int dragY = get_header_height() - g_Seq.scrollY + g_Seq.dragTrackCur * get_track_height();
        int dragBottom = dragY + get_track_height();
        int viewTop = get_header_height();
        int viewBottom = h - get_bottom_dock_height();

        // Dim everything above the dragged row (light overlay).
        if (dragY > viewTop)
            draw_alpha_box(memDC, 0, viewTop, w, dragY - viewTop, RGB(0, 0, 0), 40, RGB(0, 0, 0));
        // Dim everything below the dragged row.
        if (dragBottom < viewBottom)
            draw_alpha_box(memDC, 0, dragBottom, w, viewBottom - dragBottom, RGB(0, 0, 0), 40, RGB(0, 0, 0));

        // A thin accent line marks the dragged row's boundaries so the target
        // is still identifiable without a filled box.
        HPEN linePn = CreatePen(PS_SOLID, 1, RGB(90, 130, 180));
        HGDIOBJ oldLn = SelectObject(memDC, linePn);
        MoveToEx(memDC, 0, dragY, NULL);
        LineTo(memDC, w, dragY);
        MoveToEx(memDC, 0, dragBottom, NULL);
        LineTo(memDC, w, dragBottom);
        SelectObject(memDC, oldLn);
        DeleteObject(linePn);
    }

    
    if (seq_is_playing()) draw_fixed_badge(memDC, 0, 10, "PLAY", RGB(95, 220, 160));
    else draw_fixed_badge(memDC, 0, 10, "PAUSE", RGB(240, 80, 80));

    char bpmBuf[32];
    if (fabsf(g_Seq.bpm - floorf(g_Seq.bpm + 0.5f)) < 0.005f)
        snprintf(bpmBuf, sizeof(bpmBuf), "%.0f BPM", g_Seq.bpm);
    else
        snprintf(bpmBuf, sizeof(bpmBuf), "%.2f BPM", g_Seq.bpm);
    draw_fixed_badge(memDC, 1, 10, bpmBuf, RGB(215, 220, 230));

    char barBuf[32];
    if (g_Seq.visibleBarCount == 1)
        snprintf(barBuf, sizeof(barBuf), "1 BAR");
    else
        snprintf(barBuf, sizeof(barBuf), "%d BARS", g_Seq.visibleBarCount);
    draw_fixed_badge(memDC, 2, 10, barBuf, RGB(215, 220, 230));

    char swingBuf[32];
    snprintf(swingBuf, sizeof(swingBuf), "SWING %d%%", (int)(g_Seq.swing * 100.0f + 0.5f));
    draw_fixed_badge(memDC, 3, 10, swingBuf, g_Seq.swing > 0.001f ? RGB(255, 205, 110) : RGB(170, 178, 190));

    char snapBuf[32];
    if (g_Seq.quantizeEnabled)
        snprintf(snapBuf, sizeof(snapBuf), "SNAP %s", grid_division_label(g_Seq.gridDivision));
    else
        snprintf(snapBuf, sizeof(snapBuf), "SNAP OFF");
    draw_fixed_badge(memDC, 4, 10, snapBuf, g_Seq.quantizeEnabled ? RGB(110, 220, 240) : RGB(130, 140, 155));

    draw_fixed_badge(memDC, 5, 10,
                     g_Seq.playFromStartOnPlay ? "FROM START" : "FROM CURSOR",
                     g_Seq.playFromStartOnPlay ? RGB(110, 200, 240) : RGB(130, 140, 155));

    char lofiBuf[32];
    snprintf(lofiBuf, sizeof(lofiBuf), "LO-FI %s", g_Seq.isLofi ? "ON" : "OFF");
    draw_fixed_badge(memDC, 6, 10, lofiBuf, g_Seq.isLofi ? RGB(255, 175, 80) : RGB(130, 140, 155));

    draw_fixed_badge(memDC, 7, 10, "IMPORT", RGB(140, 230, 210));
    draw_fixed_badge(memDC, 8, 10, "EXPORT", RGB(160, 215, 255));
    draw_fixed_badge(memDC, 9, 10, "SAVE", RGB(120, 230, 180));
    draw_fixed_badge(memDC, 10, 10, "LOAD", RGB(245, 190, 100));
    draw_fixed_badge(memDC, 11, 10, "KEYBINDS", RGB(200, 180, 240));
    draw_vis_hybrid_badge(memDC, 12, 10);

     
    {
        RECT gutterRc = { 0, get_header_height() - scale_y(22), get_track_header_width(), get_header_height() - 1 };
        HBRUSH gtrBrush = CreateSolidBrush(RGB(24, 27, 34));
        FillRect(memDC, &gutterRc, gtrBrush);
        DeleteObject(gtrBrush);

        RECT pgUpRc, pgDnRc;
        if (get_pager_button_rects(w, h, &pgUpRc, &pgDnRc)) {
            bool hovUp = (g_Seq.mouseX >= pgUpRc.left && g_Seq.mouseX <= pgUpRc.right &&
                          g_Seq.mouseY >= pgUpRc.top && g_Seq.mouseY <= pgUpRc.bottom);
            bool hovDn = (g_Seq.mouseX >= pgDnRc.left && g_Seq.mouseX <= pgDnRc.right &&
                          g_Seq.mouseY >= pgDnRc.top && g_Seq.mouseY <= pgDnRc.bottom);

            COLORREF base = RGB(150, 162, 178);
            COLORREF bgCol = RGB(24, 27, 34);
            float dimK = 0.35f;
            COLORREF upCol = hovUp ? RGB(215, 230, 255) : RGB((BYTE)(GetRValue(bgCol) + (GetRValue(base) - GetRValue(bgCol)) * dimK),
                                                              (BYTE)(GetGValue(bgCol) + (GetGValue(base) - GetGValue(bgCol)) * dimK),
                                                              (BYTE)(GetBValue(bgCol) + (GetBValue(base) - GetBValue(bgCol)) * dimK));
            COLORREF dnCol = hovDn ? RGB(215, 230, 255) : RGB((BYTE)(GetRValue(bgCol) + (GetRValue(base) - GetRValue(bgCol)) * dimK),
                                                              (BYTE)(GetGValue(bgCol) + (GetGValue(base) - GetGValue(bgCol)) * dimK),
                                                              (BYTE)(GetBValue(bgCol) + (GetBValue(base) - GetBValue(bgCol)) * dimK));

            int upCx = (pgUpRc.left + pgUpRc.right) / 2;
            int dnCx = (pgDnRc.left + pgDnRc.right) / 2;
            int triTop = pgUpRc.top + (pgUpRc.bottom - pgUpRc.top) / 2 - scale_y(6);
            int triH = scale_y(11);
            int triHalfW = scale_x(6);

            draw_aa_triangle(memDC, upCx, triTop, triHalfW, triH, upCol, true);
            draw_aa_triangle(memDC, dnCx, triTop, triHalfW, triH, dnCol, false);
        }

         
        {
            RECT tsRc;
            get_timesig_badge_rect(w, h, &tsRc);
            HBRUSH tsBg = CreateSolidBrush(RGB(24, 27, 34));
            HGDIOBJ oldTsB = SelectObject(memDC, tsBg);
            HGDIOBJ oldTsP = SelectObject(memDC, GetStockObject(NULL_PEN));
            RoundRect(memDC, tsRc.left, tsRc.top, tsRc.right, tsRc.bottom, 4, 4);
            SelectObject(memDC, oldTsP);
            SelectObject(memDC, oldTsB);
            DeleteObject(tsBg);

            char tsBuf[16];
            snprintf(tsBuf, sizeof(tsBuf), "%d/%d", g_Seq.timeSigNum, g_Seq.timeSigDen);
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, RGB(130, 138, 150));
            HFONT oldTsF = SELECT_UI_FONT(memDC);
            DrawTextA(memDC, tsBuf, -1, &tsRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(memDC, oldTsF);
        }
    }

    
    RECT bottomDockRect = {0, h - get_bottom_dock_height(), w, h};
    HBRUSH bottomDockBrush = CreateSolidBrush(RGB(22, 25, 30));
    FillRect(memDC, &bottomDockRect, bottomDockBrush);
    DeleteObject(bottomDockBrush);

    HPEN bottomDockDivPen = CreatePen(PS_SOLID, 1, RGB(32, 36, 45));
    HGDIOBJ oldDockPen = SelectObject(memDC, bottomDockDivPen);
    MoveToEx(memDC, 0, h - get_bottom_dock_height(), NULL);
    LineTo(memDC, w, h - get_bottom_dock_height());
    MoveToEx(memDC, get_track_header_width(), h - get_bottom_dock_height(), NULL);
    LineTo(memDC, get_track_header_width(), h);
    SelectObject(memDC, oldDockPen);
    DeleteObject(bottomDockDivPen);

     
    int btnY = h - scale_y(38);
    int availW = get_track_header_width();
    int btnMargin = scale_x(6);
    int btnGap = scale_x(4);
    int btnW = (availW - btnMargin * 2 - btnGap) / 2, btnH = scale_y(20);
    int totalBtnsW = btnW * 2 + btnGap;
    int btnStartX = btnMargin;

    bool isAddHover = (g_Seq.mouseX >= btnStartX && g_Seq.mouseX <= btnStartX + btnW &&
                       g_Seq.mouseY >= btnY && g_Seq.mouseY <= btnY + btnH);
    bool isDelHover = (g_Seq.mouseX >= btnStartX + btnW + btnGap && g_Seq.mouseX <= btnStartX + totalBtnsW &&
                       g_Seq.mouseY >= btnY && g_Seq.mouseY <= btnY + btnH);

    RECT addBtn = {btnStartX, btnY, btnStartX + btnW, btnY + btnH};
    HBRUSH addBrush = CreateSolidBrush(isAddHover ? RGB(26, 48, 36) : RGB(28, 34, 42));
    HPEN addPen = CreatePen(PS_SOLID, 1, isAddHover ? RGB(90, 230, 160) : RGB(58, 120, 88));
    HGDIOBJ oldAddB = SelectObject(memDC, addBrush);
    HGDIOBJ oldAddP = SelectObject(memDC, addPen);
    RoundRect(memDC, addBtn.left, addBtn.top, addBtn.right, addBtn.bottom, 4, 4);
    SetTextColor(memDC, isAddHover ? RGB(175, 255, 215) : RGB(115, 215, 165));
    {
        HFONT oldBtnFont = (HFONT)SelectObject(memDC, get_ui_small_font());
        DrawTextA(memDC, "ADD", -1, &addBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(memDC, oldBtnFont);
    }
    SelectObject(memDC, oldAddP);
    SelectObject(memDC, oldAddB);
    DeleteObject(addPen);
    DeleteObject(addBrush);

    RECT delBtn = {btnStartX + btnW + btnGap, btnY, btnStartX + totalBtnsW, btnY + btnH};
    bool canDelete = (g_Seq.trackCount > MIN_TRACKS);
    HBRUSH delBrush = CreateSolidBrush(isDelHover && canDelete ? RGB(52, 26, 28) : RGB(28, 34, 42));
    HPEN delPen = CreatePen(PS_SOLID, 1, isDelHover && canDelete ? RGB(235, 90, 90) : RGB(140, 55, 55));
    HGDIOBJ oldDelB = SelectObject(memDC, delBrush);
    HGDIOBJ oldDelP = SelectObject(memDC, delPen);
    RoundRect(memDC, delBtn.left, delBtn.top, delBtn.right, delBtn.bottom, 4, 4);
    SetTextColor(memDC, canDelete ? (isDelHover ? RGB(255, 155, 155) : RGB(225, 105, 105)) : RGB(75, 80, 90));
    {
        HFONT oldBtnFont = (HFONT)SelectObject(memDC, get_ui_small_font());
        DrawTextA(memDC, "REM", -1, &delBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(memDC, oldBtnFont);
    }
    SelectObject(memDC, oldDelP);
    SelectObject(memDC, oldDelB);
    DeleteObject(delPen);
    DeleteObject(delBrush);

     
    int dockY = h - scale_y(38);
    int dockH = scale_y(30);
    int ctrlW = scale_x(56);
    int gap = scale_x(5);
    int ctrlStartX = get_track_header_width() + scale_x(8);

    #define DRAW_CTRL(x, label, value, isHover, bgCol, bdCol, txCol, lblCol, isActive) \
    do { \
        RECT cRc = { x, dockY, x + ctrlW, dockY + dockH }; \
        HBRUSH cBg = CreateSolidBrush(isHover ? bgCol##_HOVER : bgCol##_NORM); \
        HPEN cPn = CreatePen(PS_SOLID, 1, isHover ? bdCol##_HOVER : bdCol##_NORM); \
        HGDIOBJ oB = SelectObject(memDC, cBg); \
        HGDIOBJ oP = SelectObject(memDC, cPn); \
        RoundRect(memDC, cRc.left, cRc.top, cRc.right, cRc.bottom, 4, 4); \
        SetBkMode(memDC, TRANSPARENT); \
        RECT valRc = { x, dockY + scale_y(2), x + ctrlW, dockY + scale_y(16) }; \
        SetTextColor(memDC, isHover ? txCol##_HOVER : txCol##_NORM); \
        DrawTextA(memDC, value, -1, &valRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX); \
        HFONT oldF = (HFONT)SelectObject(memDC, get_ui_small_font()); \
        RECT lblRc = { x, dockY + scale_y(16), x + ctrlW, dockY + dockH }; \
        SetTextColor(memDC, isHover ? lblCol##_HOVER : lblCol##_NORM); \
        DrawTextA(memDC, label, -1, &lblRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX); \
        SelectObject(memDC, oldF); \
        SelectObject(memDC, oP); SelectObject(memDC, oB); \
        DeleteObject(cPn); DeleteObject(cBg); \
    } while(0)

     
    COLORREF cBg_NORM  = RGB(22, 25, 32);
    COLORREF cBg_HOVER = RGB(30, 34, 42);
    COLORREF cBd_NORM  = RGB(40, 46, 56);
    COLORREF cBd_HOVER = RGB(70, 90, 110);
    COLORREF cTx_NORM  = RGB(110, 125, 145);
    COLORREF cTx_HOVER = RGB(180, 200, 220);
    COLORREF cLb_NORM  = RGB(75, 88, 105);
    COLORREF cLb_HOVER = RGB(120, 140, 160);
    COLORREF mTx_NORM  = cTx_NORM;
    COLORREF mTx_HOVER = cTx_HOVER;
    bool isLimit = (g_Seq.masterMode == 1);    
    COLORREF lTx_NORM  = isLimit ? RGB(100, 190, 150) : RGB(190, 170, 100);
    COLORREF lTx_HOVER = isLimit ? RGB(160, 240, 200) : RGB(240, 220, 160);

     
    int zX = ctrlStartX;
    bool isZH = (g_Seq.mouseX >= zX && g_Seq.mouseX <= zX + ctrlW &&
                 g_Seq.mouseY >= dockY && g_Seq.mouseY <= dockY + dockH);
    char zVal[8]; snprintf(zVal, sizeof(zVal), "%d%%", (int)(g_Seq.zoom * 100.0f + 0.5f));
    DRAW_CTRL(zX, "ZOOM", zVal, isZH, cBg, cBd, cTx, cLb, false);

     
    int mX = zX + ctrlW + gap;
    g_masterVolBtnRect.left   = mX;
    g_masterVolBtnRect.top    = dockY;
    g_masterVolBtnRect.right  = mX + ctrlW;
    g_masterVolBtnRect.bottom = dockY + dockH;
    bool isMH = (g_Seq.mouseX >= mX && g_Seq.mouseX <= mX + ctrlW &&
                 g_Seq.mouseY >= dockY && g_Seq.mouseY <= dockY + dockH);
    char mVal[8]; snprintf(mVal, sizeof(mVal), "%d", (int)(g_Seq.masterVolume * 100.0f + 0.5f));
    DRAW_CTRL(mX, "MASTER", mVal, isMH, cBg, cBd, mTx, cLb, false);

     
    int lX = mX + ctrlW + gap;
    bool isLH = (g_Seq.mouseX >= lX && g_Seq.mouseX <= lX + ctrlW &&
                 g_Seq.mouseY >= dockY && g_Seq.mouseY <= dockY + dockH);
    DRAW_CTRL(lX, "MODE", isLimit ? "LIMIT" : "CLIP", isLH, cBg, cBd, lTx, cLb, false);

    // --- Synth module launchers (Quadrum / Halo) -----------------------------
    // Same pill geometry as the control buttons but tinted in each synth's
    // brand accent (quadrum cyan / halo orange, from the synthsource palettes).
    // Value band = drawn logo glyph, label band = DRUM / SYNTH in caps.
    #define DRAW_SYNTH_BTN(x, accent, accentDim, label, drawLogo) \
    do { \
        RECT sRc = { x, dockY, x + ctrlW, dockY + dockH }; \
        bool isSH = (g_Seq.mouseX >= (x) && g_Seq.mouseX <= (x) + ctrlW && \
                     g_Seq.mouseY >= dockY && g_Seq.mouseY <= dockY + dockH); \
        COLORREF sBg = isSH ? RGB(30, 36, 46) : RGB(22, 25, 32); \
        COLORREF sBd = isSH ? (accent) : RGB(40, 46, 56); \
        HBRUSH shBr = CreateSolidBrush(sBg); \
        HPEN shPn = CreatePen(PS_SOLID, 1, sBd); \
        HGDIOBJ soB = SelectObject(memDC, shBr); \
        HGDIOBJ soP = SelectObject(memDC, shPn); \
        RoundRect(memDC, sRc.left, sRc.top, sRc.right, sRc.bottom, 4, 4); \
        SelectObject(memDC, soP); SelectObject(memDC, soB); \
        DeleteObject(shPn); DeleteObject(shBr); \
        /* logo glyph centered in the value band */ \
        int gcx = (x) + ctrlW / 2; \
        int gcy = dockY + scale_y(9); \
        int gr  = scale_y(5); \
        HPEN gPen = CreatePen(PS_SOLID, scale_x(1) > 0 ? scale_x(1) : 1, isSH ? (accent) : (accentDim)); \
        HGDIOBJ goP = SelectObject(memDC, gPen); \
        HBRUSH gBr  = CreateSolidBrush(isSH ? (accent) : (accentDim)); \
        HGDIOBJ goB = SelectObject(memDC, gBr); \
        drawLogo(memDC, gcx, gcy, gr, accent); \
        SelectObject(memDC, goB); SelectObject(memDC, goP); \
        DeleteObject(gBr); DeleteObject(gPen); \
        HFONT soF = (HFONT)SelectObject(memDC, get_ui_small_font()); \
        RECT sLblRc = { x, dockY + scale_y(16), x + ctrlW, dockY + dockH }; \
        SetBkMode(memDC, TRANSPARENT); \
        SetTextColor(memDC, isSH ? (accent) : (accentDim)); \
        DrawTextA(memDC, label, -1, &sLblRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX); \
        SelectObject(memDC, soF); \
    } while(0)

    // Quadrum "Q": ring + tail stroke, cyan.
    // Halo mark: neon ring + orbit dot, orange.
    // Media Explorer "≡": monochrome three-bar glyph + MEDIA label, immediately
    // left of Q/DRUM (mirrors the synth-launcher pill geometry but neutral).
    int mediaX = lX + ctrlW + gap;
    {
        RECT mRc = { mediaX, dockY, mediaX + ctrlW, dockY + dockH };
        bool isMediaH = (g_Seq.mouseX >= mediaX && g_Seq.mouseX <= mediaX + ctrlW &&
                         g_Seq.mouseY >= dockY && g_Seq.mouseY <= dockY + dockH);
        COLORREF acc = RGB(150, 165, 185);        // neutral grey accent
        COLORREF accDim = RGB(80, 92, 108);
        COLORREF mBg = isMediaH ? RGB(30, 36, 46) : RGB(22, 25, 32);
        COLORREF mBd = isMediaH ? acc : RGB(40, 46, 56);
        HBRUSH mBr = CreateSolidBrush(mBg);
        HPEN mPn = CreatePen(PS_SOLID, 1, mBd);
        HGDIOBJ moB = SelectObject(memDC, mBr);
        HGDIOBJ moP = SelectObject(memDC, mPn);
        RoundRect(memDC, mRc.left, mRc.top, mRc.right, mRc.bottom, 4, 4);
        SelectObject(memDC, moP); SelectObject(memDC, moB);
        DeleteObject(mPn); DeleteObject(mBr);

        // Three-bar "≡" glyph centered in the value band.
        int gcx = mediaX + ctrlW / 2;
        int gcy = dockY + scale_y(9);
        int barW = scale_x(7), barH = scale_y(2), barGap = scale_y(3);
        HPEN gPen = CreatePen(PS_SOLID, 1, isMediaH ? acc : accDim);
        HGDIOBJ goP = SelectObject(memDC, gPen);
        HBRUSH gBr = CreateSolidBrush(isMediaH ? acc : accDim);
        HGDIOBJ goB = SelectObject(memDC, gBr);
        for (int b = 0; b < 3; ++b) {
            RECT barRc = { gcx - barW / 2, gcy - barH - barGap + b * (barH + barGap),
                           gcx + barW / 2, gcy - barH - barGap + b * (barH + barGap) + barH };
            FillRect(memDC, &barRc, gBr);
        }
        SelectObject(memDC, goB); SelectObject(memDC, goP);
        DeleteObject(gBr); DeleteObject(gPen);

        HFONT mF = (HFONT)SelectObject(memDC, get_ui_small_font());
        RECT mLblRc = { mediaX, dockY + scale_y(16), mediaX + ctrlW, dockY + dockH };
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, isMediaH ? acc : accDim);
        DrawTextA(memDC, "MEDIA", -1, &mLblRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(memDC, mF);
    }

    int qX = mediaX + ctrlW + gap;
    DRAW_SYNTH_BTN(qX, RGB(56, 194, 224), RGB(30, 100, 120), "DRUM", draw_synth_logo_q);
    int hX = qX + ctrlW + gap;
    DRAW_SYNTH_BTN(hX, RGB(255, 140, 25), RGB(130, 70, 15), "SYNTH", draw_synth_logo_halo);

    #undef DRAW_SYNTH_BTN

    #undef DRAW_CTRL

    
    float beatFrac = fmodf(currentBeat, 1.0f);
    float bpb = beats_per_bar();
    if (bpb < 1.0f) bpb = 1.0f;
    if (bpb > 16.0f) bpb = 16.0f;
    int pulseCount = (int)(bpb + 0.5f);
    int activeBeat = (int)fmodf(currentBeat, bpb);
    bool playing = seq_is_playing();
    float pulse = playing ? (1.0f - beatFrac * 0.65f) : 0.0f;
    int pulseGap = scale_x(4);
    int blockW = (totalBtnsW - (pulseCount - 1) * pulseGap) / pulseCount;
    int blockH = scale_y(5);
    int lineY = h - scale_y(13);

    for (int b = 0; b < pulseCount; ++b) {
        int sqX = btnStartX + b * (blockW + pulseGap);
        RECT sqRect = {sqX, lineY, sqX + blockW, lineY + blockH};

        COLORREF sqCol = RGB(28, 33, 42);
        if (playing && b == activeBeat) {
            COLORREF baseCol = (b == 0) ? RGB(255, 200, 70) : RGB(80, 240, 180);
            sqCol = RGB((BYTE)(GetRValue(baseCol) * pulse),
                        (BYTE)(GetGValue(baseCol) * pulse),
                        (BYTE)(GetBValue(baseCol) * pulse));
        }

        HBRUSH sqBrush = CreateSolidBrush(sqCol);
        HPEN sqPen = CreatePen(PS_SOLID, 1, (b == activeBeat && playing) ? RGB(160, 255, 220) : RGB(42, 48, 60));
        HGDIOBJ oldSqB = SelectObject(memDC, sqBrush);
        HGDIOBJ oldSqP = SelectObject(memDC, sqPen);
        RoundRect(memDC, sqRect.left, sqRect.top, sqRect.right, sqRect.bottom, 2, 2);
        SelectObject(memDC, oldSqP);
        SelectObject(memDC, oldSqB);
        DeleteObject(sqPen);
        DeleteObject(sqBrush);
    }

    
    {
        SetBkMode(memDC, TRANSPARENT);
        RECT msgRc = { w - scale_x(360), h - scale_y(34), w - scale_x(16), h - scale_y(10) };

        if (g_Seq.isBusy) {
            const char* kind =
                (g_Seq.jobKind == 1) ? "SAVING" :
                (g_Seq.jobKind == 2) ? "LOADING" :
                (g_Seq.jobKind == 3) ? "EXPORTING" :
                (g_Seq.jobKind == 5) ? "SOUNDFONT" : "WORKING";
            char progBuf[64];
            snprintf(progBuf, sizeof(progBuf), "%s  %d%%", kind, g_Seq.jobProgress);
            SetTextColor(memDC, RGB(120, 225, 255));
            DrawTextA(memDC, progBuf, -1, &msgRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        else if (g_Seq.exportMsgActive) {
            if (GetTickCount64() >= g_Seq.exportMsgExpiry) {
                g_Seq.exportMsgActive = false;
            } else {
                SetTextColor(memDC, RGB(180, 205, 235));
                DrawTextA(memDC, g_Seq.exportMsg, -1, &msgRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        }
    }

    
    cseq_sb_draw(memDC, g_hWnd);

    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldFontMem);
    SelectObject(hdc, oldFontMain);
}