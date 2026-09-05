 #pragma once
#include "config.h"
#include <windows.h>
#include <windowsx.h>
#include <string.h>
#include <math.h>

static inline void update_scrollbar(HWND hwnd);

typedef enum {
    CSEQ_SB_NONE = 0,
    CSEQ_SB_THUMB,
    CSEQ_SB_TRACK_UP,
    CSEQ_SB_TRACK_DOWN
} RefractSbPart;

typedef struct {
    bool dragging;
    bool hoverThumb;
    bool hoverTrack;
    int  grabOffsetPx;    

     
    bool visible;
    int  totalContent;
    int  visibleHeight;
    int  scrollPos;
} RefractSbState;

static RefractSbState g_sbState = { 0 };

typedef struct {
    bool visible;
    int  left, right;         
    int  trackTop, trackBottom;
    int  thumbTop, thumbBottom;
    int  pagePx;
    int  scrollMin;
    int  scrollRange;
} RefractSbGeom;

static inline int cseq_sb_width(void) { return scale_x(13); }

 
static inline bool cseq_sb_get_geom(HWND hwnd, RefractSbGeom *geo) {
    memset(geo, 0, sizeof(*geo));
    if (!hwnd || !IsWindow(hwnd)) return false;

    RECT rc;
    if (!GetClientRect(hwnd, &rc) || rc.right <= 0 || rc.bottom <= 0)
        return false;

    int trackTop = get_header_height();
    int trackBottom = rc.bottom - get_bottom_dock_height();
    int trackLen = trackBottom - trackTop;
    if (trackLen < cseq_sb_width()) return false;   

    geo->visible = true;
    geo->scrollMin = 0;
    geo->scrollRange = g_sbState.totalContent;      
    geo->pagePx = g_sbState.visibleHeight;          
    geo->left = rc.right - cseq_sb_width();
    geo->right = rc.right;
    geo->trackTop = trackTop;
    geo->trackBottom = trackBottom;

    int total = g_sbState.totalContent;
    int visible = g_sbState.visibleHeight;
    if (total <= 0) total = g_Seq.trackCount * get_track_height();
    if (visible <= 0) visible = trackLen;

    
    int thumbLen = (total > visible)
                   ? (int)((float)trackLen * ((float)visible / (float)total))
                   : trackLen;   
    if (thumbLen < scale_y(28)) thumbLen = scale_y(28);
    if (thumbLen > trackLen) thumbLen = trackLen;

    
    float frac = (total > visible)
                 ? (float)g_Seq.scrollY / (float)(total - visible)
                 : 0.0f;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    int thumbTop = trackTop + (int)(frac * (float)(trackLen - thumbLen));
    geo->thumbTop = thumbTop;
    geo->thumbBottom = thumbTop + thumbLen;
    return true;
}

static inline RefractSbPart cseq_sb_hit_test(const RefractSbGeom *geo, int mx, int my) {
    if (!geo || !geo->visible) return CSEQ_SB_NONE;
    if (mx < geo->left || mx >= geo->right) return CSEQ_SB_NONE;
    if (my < geo->trackTop || my >= geo->trackBottom) return CSEQ_SB_NONE;
    if (my >= geo->thumbTop && my < geo->thumbBottom) return CSEQ_SB_THUMB;
    return (my < geo->thumbTop) ? CSEQ_SB_TRACK_UP : CSEQ_SB_TRACK_DOWN;
}


 
static inline void cseq_sb_commit_scroll(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int visibleH = (rc.bottom - rc.top) - get_header_height() - get_bottom_dock_height();
    int maxScroll = g_Seq.trackCount * get_track_height() - visibleH;
    if (maxScroll < 0) maxScroll = 0;
    if (g_Seq.scrollY > maxScroll) g_Seq.scrollY = maxScroll;
    if (g_Seq.scrollY < 0) g_Seq.scrollY = 0;
    update_scrollbar(hwnd);
    if (g_hWnd) InvalidateRect(hwnd, NULL, FALSE);
}

 
static inline void cseq_sb_page_scroll(HWND hwnd, bool up) {
    SCROLLINFO si = { 0 };
    si.cbSize = sizeof(si);
    si.fMask = SIF_PAGE;
    int page = get_track_height();    
    if (GetScrollInfo(hwnd, SB_VERT, &si) && si.nPage > 0) page = (int)si.nPage;
    g_Seq.scrollY += up ? -page : page;
    cseq_sb_commit_scroll(hwnd);
}

static inline void cseq_sb_begin_drag(HWND hwnd, int mouseY, const RefractSbGeom *geo) {
    g_sbState.dragging = true;
    g_sbState.grabOffsetPx = mouseY - geo->thumbTop;
    SetCapture(hwnd);
}

 
static inline void cseq_sb_update_drag(HWND hwnd, int mouseY) {
    RefractSbGeom geo;
    if (!cseq_sb_get_geom(hwnd, &geo)) { g_sbState.dragging = false; ReleaseCapture(); return; }

    int trackLen = geo.trackBottom - geo.trackTop;
    int thumbLen = geo.thumbBottom - geo.thumbTop;
    int denom = trackLen - thumbLen;
    if (denom <= 0) return;

    float frac = (float)(mouseY - g_sbState.grabOffsetPx - geo.trackTop) / (float)denom;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    int span = geo.scrollRange - geo.pagePx;    
    g_Seq.scrollY = geo.scrollMin + (int)(frac * (float)span + 0.5f);
    cseq_sb_commit_scroll(hwnd);
}

static inline void cseq_sb_end_drag(HWND hwnd) {
    if (g_sbState.dragging) {
        g_sbState.dragging = false;
        if (GetCapture() == hwnd) ReleaseCapture();
        if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
    }
}

static inline void cseq_sb_cancel_drag(void) {
    g_sbState.dragging = false;
    g_sbState.grabOffsetPx = 0;
}

 
static inline bool cseq_sb_set_hover(HWND hwnd, bool thumbHover, bool trackHover) {
    if (g_sbState.hoverThumb != thumbHover || g_sbState.hoverTrack != trackHover) {
        g_sbState.hoverThumb = thumbHover;
        g_sbState.hoverTrack = trackHover;
        if (g_hWnd && !g_Seq.isDraggingClip && !g_Seq.isMarqueeSelecting) {
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return true;
    }
    return false;
}

 
static inline void cseq_sb_draw_aa_thumb(HDC hdc, int x0, int y0, int w, int h, float radius, COLORREF color) {
    if (w <= 0 || h <= 0 || w > 256 || h > 4096) return;

    HDC memDC = CreateCompatibleDC(hdc);
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* pBits = NULL;
    HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hBmp || !pBits) { if (memDC) DeleteDC(memDC); return; }

    HGDIOBJ oldBmp = SelectObject(memDC, hBmp);
    DWORD* pPix = (DWORD*)pBits;
    float cR = (float)GetRValue(color), cG = (float)GetGValue(color), cB = (float)GetBValue(color);

    const int SAMPLES = 4;
    const float step = 1.0f / (float)SAMPLES;
    const float offset = step * 0.5f;
    const float invTotal = 1.0f / (float)(SAMPLES * SAMPLES);

    if (radius > (float)w * 0.5f) radius = (float)w * 0.5f;
    if (radius > (float)h * 0.5f) radius = (float)h * 0.5f;
    if (radius < 1.0f) radius = 1.0f;

    float innerW = (float)w - 2.0f * radius;
    float innerH = (float)h - 2.0f * radius;
    float r2 = radius * radius;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float sumA = 0.0f;
            for (int sy = 0; sy < SAMPLES; ++sy) {
                float py = (float)y + offset + (float)sy * step;
                for (int sx = 0; sx < SAMPLES; ++sx) {
                    float px = (float)x + offset + (float)sx * step;

                    float dx = 0.0f;
                    if (px < radius) dx = radius - px;
                    else if (px > radius + innerW) dx = px - (radius + innerW);

                    float dy = 0.0f;
                    if (py < radius) dy = radius - py;
                    else if (py > radius + innerH) dy = py - (radius + innerH);

                    if (dx * dx + dy * dy <= r2) {
                        sumA += 1.0f;
                    }
                }
            }
            if (sumA <= 0.001f) {
                pPix[y * w + x] = 0;
            } else {
                BYTE a  = (BYTE)(sumA * invTotal * 255.0f + 0.5f);
                BYTE pr = (BYTE)(cR * (sumA * invTotal) + 0.5f);
                BYTE pg = (BYTE)(cG * (sumA * invTotal) + 0.5f);
                BYTE pb = (BYTE)(cB * (sumA * invTotal) + 0.5f);
                pPix[y * w + x] = ((DWORD)a << 24) | ((DWORD)pr << 16) | ((DWORD)pg << 8) | (DWORD)pb;
            }
        }
    }

    GdiFlush();
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(hdc, x0, y0, w, h, memDC, 0, 0, w, h, bf);
    SelectObject(memDC, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(memDC);
}

 
static inline void cseq_sb_draw(HDC hdc, HWND hwnd) {
    RefractSbGeom geo;
    if (!cseq_sb_get_geom(hwnd, &geo)) {
        if (g_sbState.hoverThumb || g_sbState.hoverTrack) {
            g_sbState.hoverThumb = false;
            g_sbState.hoverTrack = false;
        }
        return;
    }

     
    RECT trackRt = { geo.left, geo.trackTop, geo.right, geo.trackBottom };
    HBRUSH trackBrush = CreateSolidBrush(RGB(28, 33, 42));
    FillRect(hdc, &trackRt, trackBrush);
    DeleteObject(trackBrush);

     
    bool hot = (g_sbState.dragging || g_sbState.hoverThumb);
    COLORREF thumbCol = hot ? RGB(80, 100, 120) : RGB(60, 75, 90);

    int marginX = scale_x(2);
    int thumbX = geo.left + marginX;
    int thumbW = (geo.right - geo.left) - marginX * 2;
    int thumbY = geo.thumbTop;
    int thumbH = geo.thumbBottom - geo.thumbTop;

    if (thumbW > 0 && thumbH > 0) {
        float radius = (float)thumbW * 0.5f;  
        cseq_sb_draw_aa_thumb(hdc, thumbX, thumbY, thumbW, thumbH, radius, thumbCol);
    }
}