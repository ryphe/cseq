#pragma once
#include "config.h"
#include "font.h"
#include "dpi.h"
#include "globals.h"      
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "dwmapi.lib")

 

#ifndef VIS_MAX_FFT
#define VIS_MAX_FFT 8192
#endif

#ifndef SAMPLE_RATE
#define SAMPLE_RATE 44100
#endif

#define VIS_RING_SIZE 8192
#define VIS_MAX_BANDS 320

enum { VIS_MODE_OSC = 0, VIS_MODE_SPEC, VIS_MODE_COMBO, VIS_MODE_COUNT };
enum { VIS_CH_STEREO = 0, VIS_CH_LEFT, VIS_CH_RIGHT, VIS_CH_MIDSIDE, VIS_CH_COUNT };
enum { VIS_BTN_MODE = 0, VIS_BTN_FFT, VIS_BTN_ZOOM, VIS_BTN_CH, VIS_BTN_FREEZE, VIS_BTN_COUNT };

 
typedef struct {
    float bufferL[VIS_RING_SIZE];
    float bufferR[VIS_RING_SIZE];
    volatile LONG writePos;
} VisAudioRing;

static VisAudioRing g_visRing = { 0 };

static inline void vis_push_audio_samples(const float* pInterleaved, int frameCount) {
    if (!pInterleaved || frameCount <= 0) return;
    LONG wp = g_visRing.writePos;
    for (int i = 0; i < frameCount; ++i) {
        int idx = (wp + i) & (VIS_RING_SIZE - 1);
        g_visRing.bufferL[idx] = pInterleaved[i * 2 + 0];
        g_visRing.bufferR[idx] = pInterleaved[i * 2 + 1];
    }
    InterlockedExchange(&g_visRing.writePos, (wp + frameCount) & (VIS_RING_SIZE - 1));
}

 
static HDC     g_visCacheDC      = NULL;
static HBITMAP g_visCacheBmp     = NULL;
static HBITMAP g_visCacheOldBmp  = NULL;
static int     g_visCacheW       = 0;
static int     g_visCacheH       = 0;
static bool    g_visCacheInvalid = true;

static HDC     g_visBackDC   = NULL;
static HBITMAP g_visBackBmp  = NULL;
static HBITMAP g_visBackOld  = NULL;
static int     g_visBackW    = 0;
static int     g_visBackH    = 0;

 
static HANDLE         g_hVisSyncThread = NULL;
static volatile LONG  g_visSyncRunning = 0;

static inline void invalidate_vis_cache(void) { g_visCacheInvalid = true; }

static inline bool is_vis_cache_dirty(int w, int h) {
    static DWORD s_lastVisHash = 0;
    static int s_lastW = 0, s_lastH = 0;
    if (w != s_lastW || h != s_lastH || g_visCacheInvalid) {
        s_lastW = w; s_lastH = h; return true;
    }
    DWORD hsh = 2166136261u;
    hsh = (hsh ^ (DWORD)g_Vis.mode) * 16777619u;
    hsh = (hsh ^ (DWORD)g_Vis.fftSize) * 16777619u;
    hsh = (hsh ^ (DWORD)g_Vis.channels) * 16777619u;
    hsh = (hsh ^ (DWORD)(g_Vis.isFrozen ? 1 : 0)) * 16777619u;
    DWORD uHue = 0, uZoom = 0, uSx = 0, uSy = 0;
    memcpy(&uHue, &g_Vis.hue, sizeof(float));
    memcpy(&uZoom, &g_Vis.zoom, sizeof(float));
    memcpy(&uSx, &g_dpiScaleX, sizeof(float));
    memcpy(&uSy, &g_dpiScaleY, sizeof(float));
    hsh = (hsh ^ uHue) * 16777619u;
    hsh = (hsh ^ uZoom) * 16777619u;
    hsh = (hsh ^ uSx) * 16777619u;
    hsh = (hsh ^ uSy) * 16777619u;
    if (hsh != s_lastVisHash) { s_lastVisHash = hsh; return true; }
    return false;
}

 
static float s_visHannTable[VIS_MAX_FFT];
static int   s_visHannCachedN = 0;

static inline void ensure_hann_window(int n) {
    if (s_visHannCachedN == n) return;
    for (int i = 0; i < n; ++i) {
        s_visHannTable[i] = 0.5f * (1.0f - cosf(2.0f * 3.14159265358979323846f * (float)i / (float)(n - 1)));
    }
    s_visHannCachedN = n;
}

static inline void vis_fft(float* real, float* imag, int n) {
    int j = 0;
    for (int i = 0; i < n - 1; ++i) {
        if (i < j) {
            float tr = real[i]; real[i] = real[j]; real[j] = tr;
            float ti = imag[i]; imag[i] = imag[j]; imag[j] = ti;
        }
        int k = n >> 1;
        while (k <= j) { j -= k; k >>= 1; }
        j += k;
    }
    for (int len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * 3.14159265358979323846f / (float)len;
        float wlen_r = cosf(angle), wlen_i = sinf(angle);
        for (int i = 0; i < n; i += len) {
            float w_r = 1.0f, w_i = 0.0f;
            int half = len >> 1;
            for (int k = 0; k < half; ++k) {
                float u_r = real[i + k], u_i = imag[i + k];
                float v_r = real[i + k + half] * w_r - imag[i + k + half] * w_i;
                float v_i = real[i + k + half] * w_i + imag[i + k + half] * w_r;
                real[i + k] = u_r + v_r; imag[i + k] = u_i + v_i;
                real[i + k + half] = u_r - v_r; imag[i + k + half] = u_i - v_i;
                float next_w_r = w_r * wlen_r - w_i * wlen_i;
                float next_w_i = w_r * wlen_i + w_i * wlen_r;
                w_r = next_w_r; w_i = next_w_i;
            }
        }
    }
}

 
static inline void vis_freq_to_note(float freq, char* outBuf, size_t bufSize) {
    if (freq < 16.0f) { snprintf(outBuf, bufSize, "< 16Hz"); return; }
    static const char* kNotes[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    float midi = 69.0f + 12.0f * log2f(freq / 440.0f);
    int iMidi = (int)(midi + 0.5f);
    if (iMidi < 0) iMidi = 0;
    if (iMidi > 127) iMidi = 127;
    snprintf(outBuf, bufSize, "%s%d", kNotes[iMidi % 12], (iMidi / 12) - 1);
}

 
static inline bool get_vis_button_rect(int btnIdx, int clientW, RECT* outRc) {
    (void)clientW;
    if (btnIdx < 0 || btnIdx >= VIS_BTN_COUNT) return false;
    int startX = scale_x(10), topY = scale_y(6), btnH = scale_y(28), gap = scale_x(6);
    const int widths[VIS_BTN_COUNT] = {
        scale_x(74),  
        scale_x(74),  
        scale_x(68),  
        scale_x(78),  
        scale_x(68)   
    };
    int curX = startX;
    for (int i = 0; i < btnIdx; ++i) curX += widths[i] + gap;
    if (outRc) *outRc = (RECT){ curX, topY, curX + widths[btnIdx], topY + btnH };
    return true;
}

 
static inline bool get_vis_hue_slider_rect(int w, int h, RECT* outTrackRc, RECT* outHitRc) {
    (void)w;
    int sliderW = scale_x(150), sliderH = scale_y(7), sliderX = scale_x(52), sliderY = h - scale_y(20);
    if (outTrackRc) *outTrackRc = (RECT){ sliderX, sliderY, sliderX + sliderW, sliderY + sliderH };
    if (outHitRc)   *outHitRc   = (RECT){ sliderX - scale_x(6), sliderY - scale_y(6),
                                          sliderX + sliderW + scale_x(6), sliderY + sliderH + scale_y(6) };
    return true;
}

 
static inline void update_vis_cache(HWND hwnd, HDC hdc, int w, int h) {
    (void)hwnd;
    if (!g_visCacheDC) g_visCacheDC = CreateCompatibleDC(hdc);
    if (g_visCacheW != w || g_visCacheH != h || !g_visCacheBmp) {
        if (g_visCacheBmp) { SelectObject(g_visCacheDC, g_visCacheOldBmp); DeleteObject(g_visCacheBmp); }
        g_visCacheBmp = CreateCompatibleBitmap(hdc, w, h);
        g_visCacheOldBmp = (HBITMAP)SelectObject(g_visCacheDC, g_visCacheBmp);
        g_visCacheW = w; g_visCacheH = h;
    }

    HDC dc = g_visCacheDC;
    HFONT oldF = SELECT_UI_FONT(dc);
    RECT rc = { 0, 0, w, h };
    HBRUSH bg = CreateSolidBrush(RGB(17, 19, 23));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);

    int topH = scale_y(40), botH = scale_y(32);
    int canvasY = topH, canvasH = h - topH - botH;
    int gap = (g_Vis.mode == VIS_MODE_COMBO) ? scale_y(8) : 0;
    int oscH = (g_Vis.mode == VIS_MODE_COMBO) ? ((canvasH - gap) / 2) : canvasH;
    int specY = (g_Vis.mode == VIS_MODE_COMBO) ? (canvasY + oscH + gap) : canvasY;
    int specH = (g_Vis.mode == VIS_MODE_COMBO) ? (canvasH - oscH - gap) : canvasH;
    int specL = scale_x(54), specR = w - scale_x(12);
    int specW = specR - specL;

    COLORREF primaryCol  = hsl_to_rgb(g_Vis.hue, 0.45f, 0.58f);
    COLORREF gridLineCol = RGB(26, 30, 38);
    COLORREF gridSubCol  = RGB(20, 24, 30);
    COLORREF textDimCol  = RGB(85, 95, 112);

     
    HPEN topSepPen = CreatePen(PS_SOLID, 1, RGB(32, 36, 46));
    HGDIOBJ opOld = SelectObject(dc, topSepPen);
    MoveToEx(dc, 0, topH - 1, NULL); LineTo(dc, w, topH - 1);
    MoveToEx(dc, 0, h - botH, NULL); LineTo(dc, w, h - botH);
    if (g_Vis.mode == VIS_MODE_COMBO) {
        MoveToEx(dc, 0, canvasY + oscH + gap / 2, NULL);
        LineTo(dc, w, canvasY + oscH + gap / 2);
    }
    SelectObject(dc, opOld); DeleteObject(topSepPen);

     
    if (g_Vis.mode == VIS_MODE_OSC || g_Vis.mode == VIS_MODE_COMBO) {
        int oscY = canvasY;
        int oscMid = oscY + oscH / 2;
        int oscL = (g_Vis.mode == VIS_MODE_COMBO) ? specL : scale_x(8);
        int oscR = (g_Vis.mode == VIS_MODE_COMBO) ? specR : (w - scale_x(8));
        HPEN gPen = CreatePen(PS_SOLID, 1, gridLineCol);
        HPEN subPen = CreatePen(PS_DOT, 1, gridSubCol);
        HGDIOBJ op = SelectObject(dc, gPen);

        MoveToEx(dc, oscL, oscMid, NULL); LineTo(dc, oscR, oscMid);
        SelectObject(dc, subPen);
        MoveToEx(dc, oscL, oscMid - oscH / 4, NULL); LineTo(dc, oscR, oscMid - oscH / 4);
        MoveToEx(dc, oscL, oscMid + oscH / 4, NULL); LineTo(dc, oscR, oscMid + oscH / 4);

        for (int d = 1; d < 8; ++d) {
            int gx = oscL + (int)((float)d / 8.0f * (float)(oscR - oscL));
            MoveToEx(dc, gx, oscY, NULL); LineTo(dc, gx, oscY + oscH);
        }
        SelectObject(dc, op); DeleteObject(subPen); DeleteObject(gPen);
        SetTextColor(dc, textDimCol);
        if (g_Vis.mode == VIS_MODE_COMBO) {
             
            RECT oscRcBot = { oscL + scale_x(6), oscY + oscH - scale_y(16), oscL + scale_x(40), oscY + oscH - scale_y(2) };
            DrawTextA(dc, "-1.0", -1, &oscRcBot, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
        } else {
            TextOutA(dc, scale_x(10), oscY + oscH - scale_y(16), "-1.0", 4);
        }
    }

     
    if (g_Vis.mode == VIS_MODE_SPEC || g_Vis.mode == VIS_MODE_COMBO) {
        int lblH = scale_y(16);
        int plotH = specH - lblH;
        if (plotH < 20) plotH = 20;

        HPEN gPen = CreatePen(PS_SOLID, 1, gridLineCol);
        HPEN subPen = CreatePen(PS_DOT, 1, gridSubCol);
        HGDIOBJ op = SelectObject(dc, gPen);

        const float dbVals[] = { 6.0f, 0.0f, -12.0f, -24.0f, -48.0f, -72.0f };
        for (int i = 0; i < 6; ++i) {
            float norm = (dbVals[i] - (-72.0f)) / (6.0f - (-72.0f));
            int y = specY + plotH - (int)(norm * (float)plotH);
            MoveToEx(dc, specL, y, NULL); LineTo(dc, specR, y);
        }

         
        SetTextColor(dc, textDimCol);
        {
            int railTop = specY + scale_y(6);
            int railBot = specY + plotH - scale_y(6);
            int railSpan = railBot - railTop;
            if (railSpan < scale_y(14) * 5) railSpan = scale_y(14) * 5;
            for (int i = 0; i < 6; ++i) {
                int y = railTop + (i * railSpan) / 5;
                char dbBuf[16]; snprintf(dbBuf, sizeof(dbBuf), "%+ddB", (int)dbVals[i]);
                RECT dbRc = { scale_x(4), y - scale_y(7), specL - scale_x(6), y + scale_y(7) };
                DrawTextA(dc, dbBuf, -1, &dbRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }
        }

        const float freqs[] = { 20.f, 50.f, 100.f, 250.f, 500.f, 1000.f, 2500.f, 5000.f, 10000.f, 20000.f };
        const char* fLabels[] = { "20", "50", "100", "250", "500", "1k", "2.5k", "5k", "10k", "20k" };
        for (int i = 0; i < 10; ++i) {
            float norm = log10f(freqs[i] / 20.0f) / log10f(20000.0f / 20.0f);
            int x = specL + (int)(norm * (float)specW);
            SelectObject(dc, (i == 2 || i == 5 || i == 8) ? gPen : subPen);
            MoveToEx(dc, x, specY, NULL); LineTo(dc, x, specY + plotH);

            SetTextColor(dc, textDimCol);
            RECT fRc;
            int lblY1 = specY + plotH + scale_y(2);
            int lblY2 = specY + specH;
            if (i == 0) {
                fRc = (RECT){ x + scale_x(2), lblY1, x + scale_x(34), lblY2 };
                DrawTextA(dc, fLabels[i], -1, &fRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            } else if (i == 9) {
                fRc = (RECT){ x - scale_x(34), lblY1, x - scale_x(2), lblY2 };
                DrawTextA(dc, fLabels[i], -1, &fRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            } else {
                fRc = (RECT){ x - scale_x(18), lblY1, x + scale_x(18), lblY2 };
                DrawTextA(dc, fLabels[i], -1, &fRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
        SelectObject(dc, op); DeleteObject(subPen); DeleteObject(gPen);
    }

     
    #define DRAW_VIS_CONTROL(idx, label, valStr, active) do { \
        RECT bRc; \
        if (get_vis_button_rect(idx, w, &bRc)) { \
            HBRUSH br = CreateSolidBrush(active ? RGB(24, 34, 46) : RGB(22, 25, 32)); \
            HPEN pn = CreatePen(PS_SOLID, 1, active ? primaryCol : RGB(38, 44, 56)); \
            HGDIOBJ ob = SelectObject(dc, br); HGDIOBJ op2 = SelectObject(dc, pn); \
            RoundRect(dc, bRc.left, bRc.top, bRc.right, bRc.bottom, 4, 4); \
            SelectObject(dc, op2); SelectObject(dc, ob); DeleteObject(pn); DeleteObject(br); \
            RECT valRc = { bRc.left, bRc.top + scale_y(2), bRc.right, bRc.top + scale_y(15) }; \
            SetTextColor(dc, active ? RGB(230, 240, 255) : RGB(175, 185, 200)); \
            DrawTextA(dc, valStr, -1, &valRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX); \
            HFONT oldSubFont = (HFONT)SelectObject(dc, get_ui_small_font()); \
            RECT lblRc = { bRc.left, bRc.top + scale_y(14), bRc.right, bRc.bottom - scale_y(1) }; \
            SetTextColor(dc, active ? hsl_to_rgb(g_Vis.hue, 0.40f, 0.70f) : RGB(95, 105, 120)); \
            DrawTextA(dc, label, -1, &lblRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX); \
            SelectObject(dc, oldSubFont); \
        } \
    } while(0)

    const char* modeNames[] = { "OSC", "SPEC", "COMBO" };
    DRAW_VIS_CONTROL(VIS_BTN_MODE, "MODE", modeNames[g_Vis.mode], true);

    char fftStr[16]; snprintf(fftStr, sizeof(fftStr), "%d", g_Vis.fftSize);
    DRAW_VIS_CONTROL(VIS_BTN_FFT, "FFT \xB1", fftStr, g_Vis.mode != VIS_MODE_OSC);

    char zoomStr[16]; snprintf(zoomStr, sizeof(zoomStr), "%.1fx", g_Vis.zoom);
    DRAW_VIS_CONTROL(VIS_BTN_ZOOM, "ZOOM", zoomStr, false);

    const char* chNames[] = { "STEREO", "L", "R", "M-S" };
    DRAW_VIS_CONTROL(VIS_BTN_CH, "CHANNEL", chNames[g_Vis.channels], false);
    DRAW_VIS_CONTROL(VIS_BTN_FREEZE, "STATE", g_Vis.isFrozen ? "FREEZE" : "LIVE", g_Vis.isFrozen);

    #undef DRAW_VIS_CONTROL

     
    RECT sTrack, sHit;
    get_vis_hue_slider_rect(w, h, &sTrack, &sHit);
    bool isSliderHover = PtInRect(&sHit, (POINT){ g_Vis.mouseX, g_Vis.mouseY }) || g_Vis.isDraggingHue;

    RECT lblHue = { scale_x(10), h - scale_y(24), sTrack.left - scale_x(6), h - scale_y(4) };
    SetTextColor(dc, isSliderHover ? RGB(200, 215, 235) : textDimCol);
    DrawTextA(dc, "HUE", -1, &lblHue, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    int trkSteps = sTrack.right - sTrack.left;
    float sat = isSliderHover ? 0.65f : 0.40f;
    float lig = isSliderHover ? 0.52f : 0.40f;
    for (int px = 0; px < trkSteps; ++px) {
        float hNorm = (float)px / (float)trkSteps * 360.0f;
        COLORREF gradCol = hsl_to_rgb(hNorm, sat, lig);
        HPEN sp = CreatePen(PS_SOLID, 1, gradCol);
        HGDIOBJ op = SelectObject(dc, sp);
        MoveToEx(dc, sTrack.left + px, sTrack.top, NULL);
        LineTo(dc, sTrack.left + px, sTrack.bottom);
        SelectObject(dc, op); DeleteObject(sp);
    }

    HPEN borderPen = CreatePen(PS_SOLID, 1, isSliderHover ? RGB(75, 88, 108) : RGB(40, 48, 60));
    HGDIOBJ opB = SelectObject(dc, borderPen);
    HGDIOBJ obB = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, sTrack.left - 1, sTrack.top - 1, sTrack.right + 1, sTrack.bottom + 1, 3, 3);
    SelectObject(dc, obB); SelectObject(dc, opB); DeleteObject(borderPen);

    int thumbX = sTrack.left + (int)((g_Vis.hue / 360.0f) * (float)trkSteps);
    if (thumbX < sTrack.left) thumbX = sTrack.left;
    if (thumbX > sTrack.right) thumbX = sTrack.right;
    RECT thumbRc = { thumbX - scale_x(3), sTrack.top - scale_y(3), thumbX + scale_x(3), sTrack.bottom + scale_y(3) };
    HBRUSH tBr = CreateSolidBrush(isSliderHover ? RGB(255, 255, 255) : RGB(220, 230, 245));
    HPEN tPn = CreatePen(PS_SOLID, 1, RGB(20, 24, 30));
    opB = SelectObject(dc, tPn); obB = SelectObject(dc, tBr);
    RoundRect(dc, thumbRc.left, thumbRc.top, thumbRc.right, thumbRc.bottom, 2, 2);
    SelectObject(dc, obB); SelectObject(dc, opB); DeleteObject(tPn); DeleteObject(tBr);

     
    RECT botRc = { sTrack.right + scale_x(16), h - scale_y(24), w - scale_x(12), h - scale_y(4) };
    SetTextColor(dc, textDimCol);
    DrawTextA(dc, "[L/R-Click] FFT | [Scroll] Zoom | [Drag] Hue | [ESC] Close",
              -1, &botRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(dc, oldF);
}

 
static inline void draw_vis_dynamic_overlays(HWND hwnd, HDC dc, int w, int h) {
    (void)hwnd;
    int topH = scale_y(40), botH = scale_y(32);
    int canvasY = topH, canvasH = h - topH - botH;
    int gap = (g_Vis.mode == VIS_MODE_COMBO) ? scale_y(8) : 0;
    int oscH = (g_Vis.mode == VIS_MODE_COMBO) ? ((canvasH - gap) / 2) : canvasH;
    int specY = (g_Vis.mode == VIS_MODE_COMBO) ? (canvasY + oscH + gap) : canvasY;
    int specH = (g_Vis.mode == VIS_MODE_COMBO) ? (canvasH - oscH - gap) : canvasH;
    int specL = scale_x(54), specR = w - scale_x(12);
    int specW = specR - specL;

    LONG curWp = InterlockedCompareExchange(&g_visRing.writePos, 0, 0);
    COLORREF primaryCol = hsl_to_rgb(g_Vis.hue, 0.45f, 0.58f);
    COLORREF secCol     = hsl_to_rgb(fmodf(g_Vis.hue + 120.0f, 360.0f), 0.40f, 0.52f);

     
    if (g_Vis.mode == VIS_MODE_OSC || g_Vis.mode == VIS_MODE_COMBO) {
        int oscY = canvasY;
        int oscMid = oscY + oscH / 2;
        int oscL = (g_Vis.mode == VIS_MODE_COMBO) ? specL : scale_x(8);
        int oscR = (g_Vis.mode == VIS_MODE_COMBO) ? specR : (w - scale_x(8));
        int oscW = oscR - oscL;

        int viewFrames = (int)((float)VIS_MAX_FFT / (g_Vis.zoom > 0.05f ? g_Vis.zoom : 1.0f));
        if (viewFrames > VIS_RING_SIZE / 2) viewFrames = VIS_RING_SIZE / 2;
        if (viewFrames < 64) viewFrames = 64;

        int trigIdx = (int)curWp - viewFrames;
        if (!g_Vis.isFrozen) {
            for (int t = 0; t < 512; ++t) {
                int i0 = (curWp - viewFrames - t - 1) & (VIS_RING_SIZE - 1);
                int i1 = (curWp - viewFrames - t) & (VIS_RING_SIZE - 1);
                if (g_visRing.bufferL[i0] <= 0.0f && g_visRing.bufferL[i1] > 0.0f) {
                    trigIdx = (int)(curWp - viewFrames - t);
                    break;
                }
            }
        }

        static POINT oscPtsL[4096];
        static POINT oscPtsR[4096];
        int ptCount = (oscW < 4096) ? oscW : 4096;

         
        if (g_Vis.channels != VIS_CH_RIGHT) {
            for (int x = 0; x < ptCount; ++x) {
                int sIdx = (trigIdx + (int)((float)x / (float)ptCount * (float)viewFrames)) & (VIS_RING_SIZE - 1);
                float val = g_Vis.isFrozen ? g_Vis.freezeL[x % VIS_MAX_FFT] : g_visRing.bufferL[sIdx];
                if (g_Vis.channels == VIS_CH_MIDSIDE) {
                    float valR = g_Vis.isFrozen ? g_Vis.freezeR[x % VIS_MAX_FFT] : g_visRing.bufferR[sIdx];
                    val = (val + valR) * 0.5f;
                }
                int y = oscMid - (int)(val * (float)(oscH / 2 - 4));
                if (y < oscY + 2) y = oscY + 2;
                if (y > oscY + oscH - 2) y = oscY + oscH - 2;
                oscPtsL[x] = (POINT){ oscL + x, y };
            }
            HPEN wavePen = CreatePen(PS_SOLID, 2, primaryCol);
            HGDIOBJ op = SelectObject(dc, wavePen);
            Polyline(dc, oscPtsL, ptCount);
            SelectObject(dc, op); DeleteObject(wavePen);
        }

         
        if (g_Vis.channels == VIS_CH_STEREO || g_Vis.channels == VIS_CH_RIGHT || g_Vis.channels == VIS_CH_MIDSIDE) {
            for (int x = 0; x < ptCount; ++x) {
                int sIdx = (trigIdx + (int)((float)x / (float)ptCount * (float)viewFrames)) & (VIS_RING_SIZE - 1);
                float val = g_Vis.isFrozen ? g_Vis.freezeR[x % VIS_MAX_FFT] : g_visRing.bufferR[sIdx];
                if (g_Vis.channels == VIS_CH_MIDSIDE) {
                    float valL = g_Vis.isFrozen ? g_Vis.freezeL[x % VIS_MAX_FFT] : g_visRing.bufferL[sIdx];
                    val = (valL - val) * 0.5f;
                }
                int y = oscMid - (int)(val * (float)(oscH / 2 - 4));
                if (y < oscY + 2) y = oscY + 2;
                if (y > oscY + oscH - 2) y = oscY + oscH - 2;
                oscPtsR[x] = (POINT){ oscL + x, y };
            }
            HPEN wavePenR = CreatePen(PS_SOLID, 1, secCol);
            HGDIOBJ op = SelectObject(dc, wavePenR);
            Polyline(dc, oscPtsR, ptCount);
            SelectObject(dc, op); DeleteObject(wavePenR);
        }
    }

     
    if (g_Vis.mode == VIS_MODE_SPEC || g_Vis.mode == VIS_MODE_COMBO) {
        int lblH = scale_y(16);
        int plotH = specH - lblH;
        if (plotH < 20) plotH = 20;

         
        int N = g_Vis.fftSize;
        if (N != 256 && N != 512 && N != 1024 && N != 2048 && N != 4096 && N != 8192) N = 1024;
        int halfN = N / 2;

        ensure_hann_window(N);

        static float realL[VIS_MAX_FFT], imagL[VIS_MAX_FFT];
        static float realR[VIS_MAX_FFT], imagR[VIS_MAX_FFT];

        if (!g_Vis.isFrozen) {
            for (int i = 0; i < N; ++i) {
                int sIdx = (curWp - N + i) & (VIS_RING_SIZE - 1);
                float wnd = s_visHannTable[i];
                realL[i] = g_visRing.bufferL[sIdx] * wnd; imagL[i] = 0.0f;
                realR[i] = g_visRing.bufferR[sIdx] * wnd; imagR[i] = 0.0f;
                g_Vis.freezeL[i] = g_visRing.bufferL[sIdx];
                g_Vis.freezeR[i] = g_visRing.bufferR[sIdx];
            }
            vis_fft(realL, imagL, N);
            vis_fft(realR, imagR, N);

            for (int k = 0; k < halfN; ++k) {
                float magL = sqrtf(realL[k] * realL[k] + imagL[k] * imagL[k]) / (float)halfN;
                float magR = sqrtf(realR[k] * realR[k] + imagR[k] * imagR[k]) / (float)halfN;
                float dbL = (magL > 1e-5f) ? (20.0f * log10f(magL)) : -72.0f;
                float dbR = (magR > 1e-5f) ? (20.0f * log10f(magR)) : -72.0f;

                 
                g_Vis.specBinsL[k] = g_Vis.specBinsL[k] * 0.72f + dbL * 0.28f;
                g_Vis.specBinsR[k] = g_Vis.specBinsR[k] * 0.72f + dbR * 0.28f;
            }
        }

         
        int numSlices = specW / 2;
        if (numSlices > VIS_MAX_BANDS) numSlices = VIS_MAX_BANDS;
        if (numSlices < 64) numSlices = 64;

        static POINT contourPts[VIS_MAX_BANDS];
        int numContourPts = 0;

        for (int i = 0; i < numSlices; ++i) {
            float norm = (float)i / (float)(numSlices - 1);
            float freq = 20.0f * powf(20000.0f / 20.0f, norm);
            float binIdxF = freq / ((float)SAMPLE_RATE / (float)N);
            int b0 = (int)binIdxF;
            int b1 = b0 + 1;
            if (b0 >= halfN) b0 = halfN - 1;
            if (b1 >= halfN) b1 = halfN - 1;
            float frac = binIdxF - (float)b0;

             
            float dbL = g_Vis.specBinsL[b0] * (1.0f - frac) + g_Vis.specBinsL[b1] * frac;
            float dbR = g_Vis.specBinsR[b0] * (1.0f - frac) + g_Vis.specBinsR[b1] * frac;
            float valDb = (g_Vis.channels == VIS_CH_LEFT) ? dbL :
                          (g_Vis.channels == VIS_CH_RIGHT) ? dbR : max(dbL, dbR);

            float normY = (valDb - (-72.0f)) / (6.0f - (-72.0f));
            if (normY < 0.0f) normY = 0.0f;
            if (normY > 1.0f) normY = 1.0f;

             
            int sx = specL + (int)(norm * (float)specW);
            int nextI = (i + 1 < numSlices) ? (i + 1) : i;
            int sxNext = specL + (int)(((float)nextI / (float)(numSlices - 1)) * (float)specW);
            if (sxNext <= sx) sxNext = sx + 1;  

            int sh = (int)(normY * (float)plotH);
            int sy = specY + plotH - sh;

             
            if (sh > 0) {
                float intensity = 0.12f + 0.88f * (normY * normY);
                COLORREF sliceCol = RGB(
                    (BYTE)(GetRValue(primaryCol) * intensity),
                    (BYTE)(GetGValue(primaryCol) * intensity),
                    (BYTE)(GetBValue(primaryCol) * intensity)
                );
                RECT sliceRc = { sx, sy, sxNext, specY + plotH };
                HBRUSH sBr = CreateSolidBrush(sliceCol);
                FillRect(dc, &sliceRc, sBr);
                DeleteObject(sBr);
            }

            contourPts[numContourPts++] = (POINT){ sx, sy };
        }

         
        if (numContourPts > 1) {
            HPEN crestPen = CreatePen(PS_SOLID, 1, hsl_to_rgb(g_Vis.hue, 0.55f, 0.78f));
            HGDIOBJ op = SelectObject(dc, crestPen);
            Polyline(dc, contourPts, numContourPts);
            SelectObject(dc, op);
            DeleteObject(crestPen);
        }

         
        if (g_Vis.mouseX >= specL && g_Vis.mouseX <= specR &&
            g_Vis.mouseY >= specY && g_Vis.mouseY <= specY + plotH) {
            float normX = (float)(g_Vis.mouseX - specL) / (float)specW;
            float hovFreq = 20.0f * powf(20000.0f / 20.0f, normX);
            float normY = (float)(specY + plotH - g_Vis.mouseY) / (float)plotH;
            float hovDb = -72.0f + normY * (6.0f - (-72.0f));

            char noteBuf[16];
            vis_freq_to_note(hovFreq, noteBuf, sizeof(noteBuf));

            char hudText[64];
            if (hovFreq >= 1000.0f)
                snprintf(hudText, sizeof(hudText), "%.2f kHz | %s | %+.1f dB", hovFreq / 1000.0f, noteBuf, hovDb);
            else
                snprintf(hudText, sizeof(hudText), "%.0f Hz | %s | %+.1f dB", hovFreq, noteBuf, hovDb);

            int hudW = scale_x(168), hudH = scale_y(22);
            int hudX = g_Vis.mouseX + scale_x(12), hudY = g_Vis.mouseY - scale_y(26);
            if (hudX + hudW > specR) hudX = g_Vis.mouseX - hudW - scale_x(12);
            if (hudY < specY) hudY = g_Vis.mouseY + scale_y(12);

            RECT hudRc = { hudX, hudY, hudX + hudW, hudY + hudH };
            HBRUSH hBg = CreateSolidBrush(RGB(20, 24, 30));
            HPEN hPn = CreatePen(PS_SOLID, 1, primaryCol);
            HGDIOBJ ob = SelectObject(dc, hBg); HGDIOBJ op = SelectObject(dc, hPn);
            RoundRect(dc, hudRc.left, hudRc.top, hudRc.right, hudRc.bottom, 4, 4);
            SelectObject(dc, op); SelectObject(dc, ob);
            DeleteObject(hPn); DeleteObject(hBg);

            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(235, 242, 255));
            DrawTextA(dc, hudText, -1, &hudRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }
}

 
static LRESULT CALLBACK VisualizerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        g_Vis.mouseX = mx; g_Vis.mouseY = my;

        if (g_Vis.isDraggingHue) {
            RECT rc; GetClientRect(hwnd, &rc);
            RECT sTrack, sHit;
            get_vis_hue_slider_rect(rc.right - rc.left, rc.bottom - rc.top, &sTrack, &sHit);
            int trkW = sTrack.right - sTrack.left;
            if (trkW > 0) {
                float norm = (float)(mx - sTrack.left) / (float)trkW;
                if (norm < 0.0f) norm = 0.0f;
                if (norm > 1.0f) norm = 1.0f;
                g_Vis.hue = norm * 360.0f;
                invalidate_vis_cache();
                if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if (g_Vis.isDraggingHue) { g_Vis.isDraggingHue = false; ReleaseCapture(); }
        return 0;

     
    case WM_MOUSEWHEEL: {
        short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        g_Vis.zoom += (zDelta > 0) ? 0.5f : -0.5f;
        if (g_Vis.zoom < 0.5f) g_Vis.zoom = 0.5f;
        if (g_Vis.zoom > 8.0f) g_Vis.zoom = 8.0f;
        invalidate_vis_cache();
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;

         
        RECT sTrack, sHit;
        get_vis_hue_slider_rect(w, h, &sTrack, &sHit);
        if (PtInRect(&sHit, (POINT){mx, my})) {
            g_Vis.isDraggingHue = true;
            SetCapture(hwnd);
            int trkW = sTrack.right - sTrack.left;
            if (trkW > 0) {
                float norm = (float)(mx - sTrack.left) / (float)trkW;
                if (norm < 0.0f) norm = 0.0f;
                if (norm > 1.0f) norm = 1.0f;
                g_Vis.hue = norm * 360.0f;
                invalidate_vis_cache();
                if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        RECT rcBtn;

         
        if (get_vis_button_rect(VIS_BTN_MODE, w, &rcBtn) && PtInRect(&rcBtn, (POINT){mx, my})) {
            g_Vis.mode = (g_Vis.mode + 1) % VIS_MODE_COUNT;
            invalidate_vis_cache();
            return 0;
        }

         
        if (get_vis_button_rect(VIS_BTN_FFT, w, &rcBtn) && PtInRect(&rcBtn, (POINT){mx, my})) {
            int cur = g_Vis.fftSize;
            int next = (cur == 256)  ? 512  :
                       (cur == 512)  ? 1024 :
                       (cur == 1024) ? 2048 :
                       (cur == 2048) ? 4096 :
                       (cur == 4096) ? 8192 : 256;
            InterlockedExchange((volatile LONG*)&g_Vis.fftSize, next);
            invalidate_vis_cache();
            return 0;
        }

         
        if (get_vis_button_rect(VIS_BTN_ZOOM, w, &rcBtn) && PtInRect(&rcBtn, (POINT){mx, my})) {
            g_Vis.zoom = (g_Vis.zoom >= 8.0f) ? 0.5f : (g_Vis.zoom * 2.0f);
            invalidate_vis_cache();
            return 0;
        }

         
        if (get_vis_button_rect(VIS_BTN_CH, w, &rcBtn) && PtInRect(&rcBtn, (POINT){mx, my})) {
            g_Vis.channels = (g_Vis.channels + 1) % VIS_CH_COUNT;
            invalidate_vis_cache();
            return 0;
        }

         
        if (get_vis_button_rect(VIS_BTN_FREEZE, w, &rcBtn) && PtInRect(&rcBtn, (POINT){mx, my})) {
            g_Vis.isFrozen = !g_Vis.isFrozen;
            invalidate_vis_cache();
            return 0;
        }
        return 0;
    }

    case WM_RBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;

         
        RECT rcBtn;
        if (get_vis_button_rect(VIS_BTN_FFT, w, &rcBtn) && PtInRect(&rcBtn, (POINT){mx, my})) {
            int cur = g_Vis.fftSize;
            int next = (cur == 8192)  ? 4096 :
                       (cur == 4096)  ? 2048 :
                       (cur == 2048)  ? 1024 :
                       (cur == 1024)  ? 512  :
                       (cur == 512)   ? 256  : 8192;
            InterlockedExchange((volatile LONG*)&g_Vis.fftSize, next);
            invalidate_vis_cache();
            return 0;
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HFONT oldFontMain = SELECT_UI_FONT(hdc);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;

        if (w > 0 && h > 0) {
            if (is_vis_cache_dirty(w, h)) {
                update_vis_cache(hwnd, hdc, w, h);
                g_visCacheInvalid = false;
            }
            if (!g_visBackDC) g_visBackDC = CreateCompatibleDC(hdc);
            if (g_visBackW != w || g_visBackH != h || !g_visBackBmp) {
                if (g_visBackBmp) { SelectObject(g_visBackDC, g_visBackOld); DeleteObject(g_visBackBmp); }
                g_visBackBmp = CreateCompatibleBitmap(hdc, w, h);
                g_visBackOld = (HBITMAP)SelectObject(g_visBackDC, g_visBackBmp);
                g_visBackW = w; g_visBackH = h;
            }
            BitBlt(g_visBackDC, 0, 0, w, h, g_visCacheDC, 0, 0, SRCCOPY);
            draw_vis_dynamic_overlays(hwnd, g_visBackDC, w, h);
            BitBlt(hdc, 0, 0, w, h, g_visBackDC, 0, 0, SRCCOPY);
        }
        SelectObject(hdc, oldFontMain);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DPICHANGED: {
        UINT newDpi = HIWORD(wParam);
        cseq_update_scales_from_dpi(newDpi, newDpi);
        refresh_ui_font_for_dpi();
        invalidate_vis_cache();

        RECT* sug = (RECT*)lParam;
        SetWindowPos(hwnd, NULL,
                     sug->left, sug->top,
                     sug->right - sug->left, sug->bottom - sug->top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { ShowWindow(hwnd, SW_HIDE); return 0; }
        if (wParam == VK_SPACE)  { g_Vis.isFrozen = !g_Vis.isFrozen; invalidate_vis_cache(); return 0; }
        break;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
         
        if (g_hVisSyncThread) {
            InterlockedExchange(&g_visSyncRunning, 0);
            if (WaitForSingleObject(g_hVisSyncThread, 2000) == WAIT_OBJECT_0) {
                CloseHandle(g_hVisSyncThread);
                g_hVisSyncThread = NULL;
            }
        }
        if (g_visCacheDC) {
            if (g_visCacheBmp) { SelectObject(g_visCacheDC, g_visCacheOldBmp); DeleteObject(g_visCacheBmp); g_visCacheBmp = NULL; }
            DeleteDC(g_visCacheDC); g_visCacheDC = NULL;
        }
        if (g_visBackDC) {
            if (g_visBackBmp) { SelectObject(g_visBackDC, g_visBackOld); DeleteObject(g_visBackBmp); g_visBackBmp = NULL; }
            DeleteDC(g_visBackDC); g_visBackDC = NULL;
        }
        g_visHwnd = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

 
static DWORD WINAPI vis_vsync_thread_proc(LPVOID lpParam) {
    (void)lpParam;
    while (InterlockedCompareExchange(&g_visSyncRunning, 1, 1) == 1) {
        HWND hwnd = g_visHwnd;
        if (hwnd && IsWindow(hwnd) && IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
            HRESULT hr = DwmFlush();
            if (FAILED(hr)) Sleep(16);
            InvalidateRect(hwnd, NULL, FALSE);
        } else {
            Sleep(30);
        }
    }
    return 0;
}

static inline void open_visualizer_dialog(HWND parentHwnd) {
    if (!g_visHwnd || !IsWindow(g_visHwnd)) {
        static bool s_visRegistered = false;
        if (!s_visRegistered) {
            WNDCLASSA wc;
            memset(&wc, 0, sizeof(wc));
            wc.lpfnWndProc   = VisualizerWndProc;
            wc.hInstance     = GetModuleHandleA(NULL);
            wc.lpszClassName = "RefractVisualizerClass";
            wc.hCursor       = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
            wc.style         = CS_DBLCLKS;
            RegisterClassA(&wc);
            s_visRegistered = true;
        }

        int rw = scale_x(760), rh = scale_y(480);
        int scrW = GetSystemMetrics(SM_CXSCREEN), scrH = GetSystemMetrics(SM_CYSCREEN);
        int rx = (scrW - rw) / 2, ry = (scrH - rh) / 2;
        if (parentHwnd && IsWindow(parentHwnd)) {
            RECT prc; GetWindowRect(parentHwnd, &prc);
            rx = prc.left + ((prc.right - prc.left) - rw) / 2;
            ry = prc.top + ((prc.bottom - prc.top) - rh) / 2;
        }

        g_visHwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW, "RefractVisualizerClass", "Audio Visualizer & Spectrum Analyzer",
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE | WS_SIZEBOX,
            rx, ry, rw, rh, parentHwnd, NULL, GetModuleHandleA(NULL), NULL
        );

         
        bool visThreadLive = false;
        if (g_hVisSyncThread) {
            if (WaitForSingleObject(g_hVisSyncThread, 0) == WAIT_TIMEOUT) {
                visThreadLive = true;
            } else {
                CloseHandle(g_hVisSyncThread);
                g_hVisSyncThread = NULL;
            }
        }
        if (visThreadLive) {
            InterlockedExchange(&g_visSyncRunning, 1);
        } else if (InterlockedCompareExchange(&g_visSyncRunning, 1, 0) == 0) {
            g_hVisSyncThread = CreateThread(NULL, 0, vis_vsync_thread_proc, NULL, 0, NULL);
        }
    }

    ShowWindow(g_visHwnd, SW_SHOW);
    SetForegroundWindow(g_visHwnd);
    invalidate_vis_cache();
    InvalidateRect(g_visHwnd, NULL, FALSE);
}

static inline void toggle_visualizer_dialog(HWND parentHwnd) {
    if (g_visHwnd && IsWindow(g_visHwnd) && IsWindowVisible(g_visHwnd)) {
        ShowWindow(g_visHwnd, SW_HIDE);
    } else {
        open_visualizer_dialog(parentHwnd);
    }
}

 
