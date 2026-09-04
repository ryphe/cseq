// --- Synth module interface popup: Quadrum (drum) + Halo (poly) ------------
// A companion tool window to the piano-roll editor. It is opened together with
// the piano roll for synth clips, and can be closed/reopened independently:
//   - Opening a synth clip's piano roll also opens this window.
//   - Closing the piano roll also closes this window.
//   - Closing this window alone does NOT close the piano roll.
//   - A "SYNTH" button in the piano-roll toolbar reopens it.
//
// The controls read/write the patch stored on the currently-open clip
// (g_Seq.clips[g_midiEdit.clipIdx]) under the seq lock. The audio thread reads
// the same fields under the seq lock, so edits are picked up live; the Quadrum
// engine re-renders a voice's transient on the next trigger when its patch
// signature changes.
//
// Rendering: a knob-only rack. Each knob is rendered at 3x supersampling into a
// DIB section and downscaled with HALFTONE (the same anti-aliased dial used by
// the source quadrum/halo UIs). The whole rack is drawn into a persistent,
// size-cached compatible bitmap that is only rebuilt when the patch hash
// changes (mirrors the MIDI editor's g_midiCacheDC double-buffer pattern), so
// repaints are a cheap BitBlt with no flicker.

#ifndef CSEQ_SYNTHUI_H
#define CSEQ_SYNTHUI_H

#include "globals.h"
#include "synth.h"

#define SYNTH_PRESET_ROW_HALO 22   // Adjust this number to make rows thinner or taller in synth preset list (Halo)

// Global window handle + current clip binding (defined in main.c).
extern HWND g_synthHwnd;

// --- Knob control ----------------------------------------------------------
typedef struct {
    const char* label;
    const char* unit;
    double* param;        // pointer into the clip's patch
    double  min, max;
    int     isInt;
    int     decimals;     // digits shown in the value readout
    int     curve;        // 0 = linear, 1 = log (Hz), 2 = cubic (times)
    int     disabled;     // 1 = greyed out / not adjustable (Clap's osc knobs)
    RECT    rect;
} SynthKnob;

// The per-clip patch the UI edits. Returns NULL if no valid synth clip is open.
static inline Clip* synth_ui_target_clip(void) {
    if (g_midiEdit.clipIdx < 0 || g_midiEdit.clipIdx >= g_Seq.clipCount) return NULL;
    Clip* c = &g_Seq.clips[g_midiEdit.clipIdx];
    if (!c->isMidi) return NULL;
    if (c->clipKind != CLIP_KIND_QUADRUM && c->clipKind != CLIP_KIND_HALO) return NULL;
    return c;
}

static inline bool synth_ui_is_open(void) {
    return g_synthHwnd && IsWindow(g_synthHwnd);
}

// Single synth UI window, so a single static interaction state is safe.
static int    s_synthDragKnob   = -1;   // knob index being dragged (-1 = none)
static int    s_synthDragStartY = 0;
static double s_synthDragStartVal = 0.0;
static int    s_synthQuadVoice  = 0;    // Quadrum voice whose params the knobs edit
// Halo preset dropdown modal state.
static int    s_synthPresetOpen  = 0;   // modal list visible
static int    s_synthPresetSel   = 0;   // current preset index (0 = Obsidian Pad)
static int    s_synthPresetHover = -1;  // hovered row in the open list

// Single source of truth for window dimensions.
// Halo height is compacted to 400px (tight around the 4 knob rows and bottom
// notice) because the preset selector now opens as a top-to-bottom list modal
// within the panel instead of dropping below the window boundary.
#define SYNTH_UI_WIDTH 750

static inline int synth_ui_window_width(void) {
    return SYNTH_UI_WIDTH;
}

static inline int synth_ui_window_height(bool quadrum) {
    return quadrum ? 270 : 400;
}

// Push the current patch into the engine's runtime state so a live playback
// edit is heard immediately. For Halo this is a no-op (the audio thread reads
// the clip patch directly). For Quadrum the audio thread only plays back
// pre-rendered transients, so a parameter edit must re-render them OFF the
// audio thread — quadrum_render is far too slow to run on the realtime thread.
// During a knob drag we re-render just the active voice (cheap); the full
// clip is re-rendered when the selected voice changes.
static inline void synth_ui_push_patch(int clipIdx) {
    if (clipIdx < 0 || clipIdx >= MAX_CLIPS) return;
    Clip* c = synth_ui_target_clip();
    if (!c || c->clipKind != CLIP_KIND_QUADRUM) return;
    // Throttle the transient re-render during a fast knob drag so a flood of
    // mouse-moves doesn't hog the UI thread (which would stall the piano-roll
    // playhead). The param value is already written live; re-rendering at
    // ~60 Hz is plenty for audible feedback.
    static DWORD s_lastPushT = 0;
    DWORD now = GetTickCount();
    if (now - s_lastPushT < 16) return;
    s_lastPushT = now;
    synth_quadrum_rerender_voice(clipIdx, s_synthQuadVoice);
}

// --- Knob value mapping (mirrors the source halo knob_curve_for_unit) ------
static inline int synth_knob_curve_for_unit(const char* unit) {
    if (unit && strcmp(unit, "Hz") == 0) return 1;   // log frequency
    if (unit && strcmp(unit, "s") == 0)  return 2;   // cubic time
    return 0;
}

static inline double synth_knob_norm_to_value(const SynthKnob* k, double norm) {
    if (norm < 0.0) norm = 0.0;
    if (norm > 1.0) norm = 1.0;
    if (k->curve == 1) {                       // logarithmic
        double lo = (k->min < 1.0) ? 1.0 : k->min;
        return lo * pow(k->max / lo, norm);
    }
    if (k->curve == 2) {                       // cubic
        return k->min + norm * norm * norm * (k->max - k->min);
    }
    return k->min + norm * (k->max - k->min);
}

static inline double synth_knob_value_to_norm(const SynthKnob* k, double value) {
    double norm;
    if (k->curve == 1) {                       // logarithmic
        double lo = (k->min < 1.0) ? 1.0 : k->min;
        double v = (value < lo) ? lo : value;
        norm = log(v / lo) / log(k->max / lo);
    } else if (k->curve == 2) {                // cubic
        double span = k->max - k->min;
        double t = (span > 1e-9) ? ((value - k->min) / span) : 0.0;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        norm = pow(t, 1.0 / 3.0);
    } else {
        norm = (value - k->min) / (k->max - k->min);
    }
    if (norm < 0.0) norm = 0.0;
    if (norm > 1.0) norm = 1.0;
    return norm;
}

// --- Knob geometry ---------------------------------------------------------
// A knob-only rack: cells laid out in a grid. 72px cells (label / dial / value).
#define SYNTH_KNOB_CELL_W 72
#define SYNTH_KNOB_CELL_H 76
#define SYNTH_KNOB_GAP    6
#define SYNTH_KNOB_COLS   8

static inline void synth_knob_layout(SynthKnob* knobs, int count, int clientW, int startY) {
    int cols = SYNTH_KNOB_COLS;
    if (clientW < cols * (SYNTH_KNOB_CELL_W + SYNTH_KNOB_GAP)) cols = 6;
    if (clientW < cols * (SYNTH_KNOB_CELL_W + SYNTH_KNOB_GAP)) cols = 4;
    int gridW = cols * SYNTH_KNOB_CELL_W + (cols - 1) * SYNTH_KNOB_GAP;
    int x0 = (clientW > gridW) ? (clientW - gridW) / 2 : 10;
    int y0 = startY;
    for (int i = 0; i < count; ++i) {
        int col = i % cols;
        int row = i / cols;
        int x = x0 + col * (SYNTH_KNOB_CELL_W + SYNTH_KNOB_GAP);
        int y = y0 + row * (SYNTH_KNOB_CELL_H + SYNTH_KNOB_GAP);
        SetRect(&knobs[i].rect, x, y, x + SYNTH_KNOB_CELL_W, y + SYNTH_KNOB_CELL_H);
    }
}

// --- Quadrum Voice Pads geometry -------------------------------------------
// Width is 82px so titles like "Closed Hat" and "Open Hat" fit comfortably
// without text truncation.
#define SYNTH_PAD_W   82
#define SYNTH_PAD_H   30
#define SYNTH_PAD_GAP 6

static inline void synth_quad_pad_rect(int v, int clientW, RECT* outRc) {
    int padRowW = 8 * SYNTH_PAD_W + 7 * SYNTH_PAD_GAP;
    int x0 = (clientW > padRowW) ? (clientW - padRowW) / 2 : 10;
    int y0 = 8;
    int x = x0 + v * (SYNTH_PAD_W + SYNTH_PAD_GAP);
    SetRect(outRc, x, y0, x + SYNTH_PAD_W, y0 + SYNTH_PAD_H);
}

// --- Halo Preset Modal & Button geometry -----------------------------------
#define SYNTH_PRESET_BTN_W   188
#define SYNTH_PRESET_BTN_H   24
#define SYNTH_PRESET_RESET_W 56
#define SYNTH_PRESET_GAP     8
#define SYNTH_PRESET_XGAP    14   // gap between vibrato knob and buttons

static inline void synth_preset_geometry(int clientW, int clientH,
                                         RECT* presetBtn, RECT* resetBtn,
                                         RECT* listRc) {
    int cols = SYNTH_KNOB_COLS;
    int gridW = cols * SYNTH_KNOB_CELL_W + (cols - 1) * SYNTH_KNOB_GAP;
    int x0 = (clientW > gridW) ? (clientW - gridW) / 2 : 10;
    int vibratoCol = 28 % cols;
    int vibratoRow = 28 / cols;
    int vibratoLeft = x0 + vibratoCol * (SYNTH_KNOB_CELL_W + SYNTH_KNOB_GAP);
    int btnX = vibratoLeft + SYNTH_KNOB_CELL_W + SYNTH_PRESET_XGAP;

    int rowTop = 10 + vibratoRow * (SYNTH_KNOB_CELL_H + SYNTH_KNOB_GAP);
    int btnY = rowTop + (SYNTH_KNOB_CELL_H - SYNTH_PRESET_BTN_H) / 2;

    if (presetBtn)
        SetRect(presetBtn, btnX, btnY, btnX + SYNTH_PRESET_BTN_W, btnY + SYNTH_PRESET_BTN_H);
    if (resetBtn)
        SetRect(resetBtn, btnX + SYNTH_PRESET_BTN_W + SYNTH_PRESET_GAP, btnY,
                btnX + SYNTH_PRESET_BTN_W + SYNTH_PRESET_GAP + SYNTH_PRESET_RESET_W,
                btnY + SYNTH_PRESET_BTN_H);

    if (listRc) {
        // Calculate the total list height from the compact row height
        int totalListH = HALO_PRESET_COUNT * SYNTH_PRESET_ROW_HALO;
        int maxBottom = rowTop + SYNTH_KNOB_CELL_H;
        int listBottom = maxBottom;
        int listTop = listBottom - totalListH;
        if (listTop < 10) listTop = 10;  // Stay within panel top

        SetRect(listRc, btnX, listTop, btnX + SYNTH_PRESET_BTN_W, listBottom);
    }
    (void)clientH;
}

static inline void synth_preset_row_rect(const RECT* listRc, int r, RECT* rowRc) {
    int y0 = listRc->top + r * SYNTH_PRESET_ROW_HALO;
    int y1 = y0 + SYNTH_PRESET_ROW_HALO;
    SetRect(rowRc, listRc->left + 1, y0, listRc->right - 1, y1);
}

static inline int synth_preset_row_hit(const RECT* listRc, int my) {
    if (my < listRc->top || my >= listRc->bottom) return -1;
    int r = (my - listRc->top) / SYNTH_PRESET_ROW_HALO;
    if (r < 0 || r >= HALO_PRESET_COUNT) return -1;
    return r;
}

// --- Supersampled knob renderer --------------------------------------------
// Renders the dial at 3x into a DIB section, then StretchBlt down with
// HALFTONE for a smooth anti-aliased arc/needle (the source quadrum/halo look).
static inline void synth_draw_knob(HDC hdc, const SynthKnob* k, int is_active,
                                   COLORREF accent, COLORREF accentDim,
                                   COLORREF text, COLORREF textDim) {
    int kw = k->rect.right - k->rect.left;
    int kh = k->rect.bottom - k->rect.top;
    if (kw <= 0 || kh <= 0) return;

    // Disabled knobs (Clap's fixed osc knobs) render greyed out — dimmed dial
    // and muted label/value — and are not adjustable.
    bool dis = (k->disabled != 0);
    COLORREF arcColor   = dis ? RGB(60, 66, 76)   : (is_active ? accent : accentDim);
    COLORREF capFill    = dis ? RGB(24, 28, 34)   : (is_active ? RGB(36, 44, 56) : RGB(28, 33, 42));
    COLORREF capBorder  = dis ? RGB(50, 56, 66)   : (is_active ? accent : RGB(38, 46, 58));
    COLORREF needleCol  = dis ? RGB(90, 98, 110)  : RGB(245, 248, 252);
    COLORREF labelCol   = dis ? RGB(100, 108, 120): text;
    COLORREF valueCol   = dis ? RGB(100, 108, 120): (is_active ? accent : textDim);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, labelCol);
    HFONT oldFont = SELECT_UI_FONT(hdc);
    RECT lr = { k->rect.left, k->rect.top, k->rect.right, k->rect.top + 16 };
    DrawTextA(hdc, k->label, -1, &lr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    int cx = (k->rect.left + k->rect.right) / 2;
    int cy = k->rect.top + 38;
    int r = 16;
    int dial_box = 40;

    double norm = synth_knob_value_to_norm(k, *k->param);

    int ss = 3;
    int ss_dim = dial_box * ss;
    int ss_cx = ss_dim / 2;
    int ss_cy = ss_dim / 2;
    int ss_r = r * ss;

    HDC memDC = CreateCompatibleDC(hdc);
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = ss_dim;
    bmi.bmiHeader.biHeight = -ss_dim;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hBmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (hBmp && memDC) {
        HGDIOBJ oldBmp = SelectObject(memDC, hBmp);

        RECT bg_rc = { 0, 0, ss_dim, ss_dim };
        HBRUSH bg_br = CreateSolidBrush(RGB(22, 26, 33));   // panel bg
        FillRect(memDC, &bg_rc, bg_br);
        DeleteObject(bg_br);

        HBRUSH track_br = CreateSolidBrush(RGB(14, 16, 21));
        HPEN track_pen = CreatePen(PS_SOLID, 1 * ss, RGB(38, 46, 58));
        HGDIOBJ old_br = SelectObject(memDC, track_br);
        HGDIOBJ old_pen = SelectObject(memDC, track_pen);
        Ellipse(memDC, ss_cx - ss_r, ss_cy - ss_r, ss_cx + ss_r, ss_cy + ss_r);
        SelectObject(memDC, old_br);
        SelectObject(memDC, old_pen);
        DeleteObject(track_br);
        DeleteObject(track_pen);

        double start_ang = 3.92699;    // 225 deg
        double sweep_total = 4.71239;  // 270 deg sweep
        double cur_ang = start_ang - norm * sweep_total;

        COLORREF arc_color = arcColor;
        HPEN arc_pen = CreatePen(PS_SOLID, 3 * ss, arc_color);
        old_pen = SelectObject(memDC, arc_pen);
        for (double a = start_ang; a >= cur_ang; a -= 0.05) {
            int ax = ss_cx + (int)((ss_r - 2 * ss) * cos(a));
            int ay = ss_cy - (int)((ss_r - 2 * ss) * sin(a));
            if (a == start_ang) MoveToEx(memDC, ax, ay, NULL);
            else LineTo(memDC, ax, ay);
        }
        SelectObject(memDC, old_pen);
        DeleteObject(arc_pen);

        COLORREF cap_fill = capFill;
        COLORREF cap_border = capBorder;
        HBRUSH cap_br = CreateSolidBrush(cap_fill);
        HPEN cap_pen = CreatePen(PS_SOLID, 1 * ss, cap_border);
        old_br = SelectObject(memDC, cap_br);
        old_pen = SelectObject(memDC, cap_pen);
        int cap_r = ss_r - 5 * ss;
        Ellipse(memDC, ss_cx - cap_r, ss_cy - cap_r, ss_cx + cap_r, ss_cy + cap_r);
        SelectObject(memDC, old_br);
        SelectObject(memDC, old_pen);
        DeleteObject(cap_br);
        DeleteObject(cap_pen);

        HPEN needle_pen = CreatePen(PS_SOLID, 2 * ss, needleCol);
        old_pen = SelectObject(memDC, needle_pen);
        MoveToEx(memDC, ss_cx, ss_cy, NULL);
        LineTo(memDC, ss_cx + (int)((ss_r - 6 * ss) * cos(cur_ang)),
                      ss_cy - (int)((ss_r - 6 * ss) * sin(cur_ang)));
        SelectObject(memDC, old_pen);
        DeleteObject(needle_pen);

        SetStretchBltMode(hdc, HALFTONE);
        SetBrushOrgEx(hdc, 0, 0, NULL);
        StretchBlt(hdc, cx - dial_box / 2, cy - dial_box / 2, dial_box, dial_box,
                   memDC, 0, 0, ss_dim, ss_dim, SRCCOPY);

        SelectObject(memDC, oldBmp);
        DeleteObject(hBmp);
        DeleteDC(memDC);
    }

    char vbuf[32];
    if (k->isInt) {
        snprintf(vbuf, sizeof(vbuf), "%d %s", (int)(*k->param + 0.5), k->unit ? k->unit : "");
    } else if (k->unit && strcmp(k->unit, "%") == 0) {
        snprintf(vbuf, sizeof(vbuf), "%.0f%%", *k->param * 100.0);
    } else if (k->max >= 1000.0) {
        snprintf(vbuf, sizeof(vbuf), "%.0f %s", *k->param, k->unit ? k->unit : "");
    } else {
        int dec = (k->decimals > 0) ? k->decimals : 2;
        snprintf(vbuf, sizeof(vbuf), "%.*f %s", dec, *k->param, k->unit ? k->unit : "");
    }

    SetTextColor(hdc, valueCol);
    RECT vr = { k->rect.left, k->rect.top + 54, k->rect.right, k->rect.top + 72 };
    DrawTextA(hdc, vbuf, -1, &vr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, oldFont);
}

// --- Rack builders ---------------------------------------------------------
// Quadrum: 16 knobs for the currently-selected voice's params.
static inline int synth_quadrum_init_knobs(SynthKnob* knobs, QuadrumParams* p, int voice) {
    int i = 0;
    #define QK(lbl, unt, ptr, mn, mx, isi, dec) do { \
        knobs[i].label = lbl; knobs[i].unit = unt; knobs[i].param = ptr; \
        knobs[i].min = mn; knobs[i].max = mx; knobs[i].isInt = isi; \
        knobs[i].decimals = dec; knobs[i].curve = synth_knob_curve_for_unit(unt); \
        knobs[i].disabled = 0; i++; \
    } while (0)
    QK("pitch",       "Hz",  &p->pitch,        20.0, 1200.0, 0, 0);
    QK("sweep",       "%",   &p->pitch_env,    0.0,   1.0,   0, 2);
    QK("p-decay",     "s",   &p->pitch_decay,  0.002, 0.35,  0, 3);
    QK("fm depth",    "x",   &p->fm_depth,     0.0,   8.0,   0, 2);
    QK("noise mix",   "%",   &p->noise_mix,    0.0,   1.0,   0, 2);
    QK("n-decay",     "s",   &p->noise_decay,  0.005, 1.2,   0, 3);
    QK("n-cutoff",    "Hz",  &p->noise_cutoff, 100.0, 16000.0,0, 0);
    QK("click",       "%",   &p->click,        0.0,   1.0,   0, 2);
    QK("cutoff",      "Hz",  &p->filter_cutoff,100.0,18000.0,0, 0);
    QK("resonance",   "Q",   &p->filter_q,     0.3,   8.0,   0, 2);
    QK("drive",       "x",   &p->drive,        1.0,   5.0,   0, 2);
    QK("fm ratio",    "x",   &p->fm_ratio,     0.5,   6.0,   0, 2);
    QK("amp-decay",   "s",   &p->decay,        0.01,  1.8,   0, 3);
    QK("clap taps",   "",    &p->clap_taps,    1.0,   5.0,   1, 0);
    QK("tap spread",  "s",   &p->clap_spread,  0.005, 0.035, 0, 4);
    QK("filter type", "",    &p->filter_type,  0.0,   2.0,   1, 0);
    #undef QK
    // Clap has no oscillator — its osc/fm knobs (pitch, sweep, pitch decay,
    // fm depth) are fixed in the engine and greyed out for consistency.
    if (voice == VOICE_CLAP) {
        for (int d = 0; d < 4; ++d) knobs[d].disabled = 1;
    }
    return i;
}

// Halo: 29 knobs.
static inline int synth_halo_init_knobs(SynthKnob* knobs, HaloPatch* p) {
    int i = 0;
    #define HK(lbl, unt, ptr, mn, mx, isi, dec) do { \
        knobs[i].label = lbl; knobs[i].unit = unt; knobs[i].param = ptr; \
        knobs[i].min = mn; knobs[i].max = mx; knobs[i].isInt = isi; \
        knobs[i].decimals = dec; knobs[i].curve = synth_knob_curve_for_unit(unt); \
        knobs[i].disabled = 0; i++; \
    } while (0)
    HK("pitch",     "st",  &p->pitch_semi,      -24, 24,    1, 1);
    HK("wave",      "",    &p->waveform,          0,  3,    1, 1);
    HK("fm ratio",  "x",   &p->fm_ratio,        0.5,  8,    0, 2);
    HK("fm depth",  "",    &p->fm_depth,          0,  6,    0, 2);
    HK("fm feedbk", "%",   &p->fm_feedback,       0,  1,    0, 2);
    HK("osc mix",   "%",   &p->osc_mix,           0,  1,    0, 2);
    HK("unison",    "",    &p->unison_voices,     1,  8,    1, 1);
    HK("u spread",  "ct",  &p->unison_spread,     0, 50,    0, 1);
    HK("detune",    "ct",  &p->detune,            0, 50,    0, 1);
    HK("partials",  "",    &p->partial_count,     1, 12,    1, 1);
    HK("harm tilt", "",    &p->partial_tilt,     -2,  2,    0, 2);
    HK("noise mix", "%",   &p->noise_mix,         0,  1,    0, 2);
    HK("noise col", "Hz",  &p->noise_cutoff,    100,16000,  0, 0);
    HK("harm dec",  "%",   &p->harm_decay,        0,  1,    0, 2);
    HK("inharm",    "%",   &p->inharm,            0,  1,    0, 2);
    HK("cutoff",    "Hz",  &p->filter_cutoff,    50,18000,  0, 0);
    HK("resonance", "Q",   &p->filter_q,        0.5, 20,    0, 2);
    HK("f drive",   "x",   &p->filter_drive,      1,  8,    0, 2);
    HK("drive",     "x",   &p->drive,             1,  6,    0, 2);
    HK("filt type", "",    &p->filter_type,       0,  3,    1, 1);
    HK("lfo filt",  "Hz",  &p->lfo_filt_depth,    0,2000,   0, 0);
    HK("key track", "%",   &p->key_track,         0,  1,    0, 2);
    HK("amp att",   "s",   &p->amp_attack,    0.002,  2,    0, 3);
    HK("amp dec",   "s",   &p->amp_decay,     0.05,   4,    0, 3);
    HK("release",   "s",   &p->amp_release,   0.02,   6,    0, 3);
    HK("sustain",   "%",   &p->amp_sustain,       0,  1,    0, 2);
    HK("env filt",  "%",   &p->filter_env_depth, -1,  1,    0, 2);
    HK("lfo rate",  "Hz",  &p->lfo_rate,       0.1,  20,    0, 2);
    HK("vibrato",   "ct",  &p->vibrato,           0,100,    1, 1);
    #undef HK
    return i;
}

// --- Hashed double-buffer cache --------------------------------------------
static HDC     s_synthCacheDC      = NULL;
static HBITMAP s_synthCacheBmp     = NULL;
static HBITMAP s_synthCacheOldBmp  = NULL;
static int     s_synthCacheW       = 0;
static int     s_synthCacheH       = 0;
static bool    s_synthCacheInvalid = true;
// When >= 0, the next cache rebuild redraws only this knob over the existing
// cache instead of clearing and redrawing the whole rack. Used during a knob
// drag so a live edit doesn't rebuild all 29 supersampled knobs on every
// mouse-move (which starves the UI thread and stalls the piano-roll playhead).
static int     s_synthRedrawOnly  = -1;

static inline void invalidate_synth_cache(void) {
    s_synthCacheInvalid = true;
    s_synthRedrawOnly = -1;
}

// Request a targeted rebuild that redraws just one knob into the persistent
// cache (leaving the rest of the rack intact).
static inline void synth_ui_redraw_knob(int idx) {
    s_synthCacheInvalid = true;
    s_synthRedrawOnly = idx;
}

static inline bool synth_ui_is_dirty(int w, int h) {
    static DWORD s_lastHash = 0;
    static int   s_lastW = 0, s_lastH = 0;

    bool force = (w != s_lastW || h != s_lastH || s_synthCacheInvalid);
    s_lastW = w; s_lastH = h;
    Clip* c = synth_ui_target_clip();
    if (!c) return force;

    DWORD hsh = 2166136261u;
    hsh = hash_dword(hsh, (DWORD)c->clipKind);
    hsh = hash_dword(hsh, (DWORD)s_synthQuadVoice);
    hsh = hash_dword(hsh, (DWORD)s_synthPresetOpen);
    hsh = hash_dword(hsh, (DWORD)s_synthPresetSel);
    hsh = hash_dword(hsh, (DWORD)s_synthPresetHover);
    hsh = hash_dword(hsh, (DWORD)s_synthDragKnob);   // active highlight on click
    if (c->clipKind == CLIP_KIND_QUADRUM) {
        QuadrumParams* p = &c->quadrumParams[s_synthQuadVoice];
        hsh = hash_float(hsh, (float)p->pitch);
        hsh = hash_float(hsh, (float)p->pitch_env);
        hsh = hash_float(hsh, (float)p->pitch_decay);
        hsh = hash_float(hsh, (float)p->fm_ratio);
        hsh = hash_float(hsh, (float)p->fm_depth);
        hsh = hash_float(hsh, (float)p->noise_mix);
        hsh = hash_float(hsh, (float)p->noise_decay);
        hsh = hash_float(hsh, (float)p->noise_cutoff);
        hsh = hash_float(hsh, (float)p->click);
        hsh = hash_float(hsh, (float)p->filter_cutoff);
        hsh = hash_float(hsh, (float)p->filter_q);
        hsh = hash_float(hsh, (float)p->filter_type);
        hsh = hash_float(hsh, (float)p->drive);
        hsh = hash_float(hsh, (float)p->decay);
        hsh = hash_float(hsh, (float)p->clap_taps);
        hsh = hash_float(hsh, (float)p->clap_spread);
    } else if (c->clipKind == CLIP_KIND_HALO) {
        HaloPatch* p = &c->haloPatch;
        hsh = hash_float(hsh, (float)p->pitch_semi);
        hsh = hash_float(hsh, (float)p->waveform);
        hsh = hash_float(hsh, (float)p->fm_ratio);
        hsh = hash_float(hsh, (float)p->fm_depth);
        hsh = hash_float(hsh, (float)p->fm_feedback);
        hsh = hash_float(hsh, (float)p->osc_mix);
        hsh = hash_float(hsh, (float)p->unison_voices);
        hsh = hash_float(hsh, (float)p->unison_spread);
        hsh = hash_float(hsh, (float)p->detune);
        hsh = hash_float(hsh, (float)p->partial_count);
        hsh = hash_float(hsh, (float)p->partial_tilt);
        hsh = hash_float(hsh, (float)p->noise_mix);
        hsh = hash_float(hsh, (float)p->noise_cutoff);
        hsh = hash_float(hsh, (float)p->harm_decay);
        hsh = hash_float(hsh, (float)p->inharm);
        hsh = hash_float(hsh, (float)p->filter_cutoff);
        hsh = hash_float(hsh, (float)p->filter_q);
        hsh = hash_float(hsh, (float)p->filter_drive);
        hsh = hash_float(hsh, (float)p->drive);
        hsh = hash_float(hsh, (float)p->filter_type);
        hsh = hash_float(hsh, (float)p->lfo_filt_depth);
        hsh = hash_float(hsh, (float)p->key_track);
        hsh = hash_float(hsh, (float)p->amp_attack);
        hsh = hash_float(hsh, (float)p->amp_decay);
        hsh = hash_float(hsh, (float)p->amp_release);
        hsh = hash_float(hsh, (float)p->amp_sustain);
        hsh = hash_float(hsh, (float)p->filter_env_depth);
        hsh = hash_float(hsh, (float)p->lfo_rate);
        hsh = hash_float(hsh, (float)p->vibrato);
    }

    if (hsh != s_lastHash || force) {
        s_lastHash = hsh;
        return true;
    }
    return false;
}

// Rebuild the rack into the persistent cache bitmap.
static inline void synth_ui_update_cache(HDC hdc, int w, int h) {
    if (!s_synthCacheDC) s_synthCacheDC = CreateCompatibleDC(hdc);
    if (s_synthCacheW != w || s_synthCacheH != h || !s_synthCacheBmp) {
        if (s_synthCacheBmp) {
            SelectObject(s_synthCacheDC, s_synthCacheOldBmp);
            DeleteObject(s_synthCacheBmp);
            s_synthCacheBmp = NULL;
        }
        s_synthCacheBmp = CreateCompatibleBitmap(hdc, w, h);
        s_synthCacheOldBmp = (HBITMAP)SelectObject(s_synthCacheDC, s_synthCacheBmp);
        s_synthCacheW = w;
        s_synthCacheH = h;
    }

    HDC dc = s_synthCacheDC;
    bool partial = (s_synthRedrawOnly >= 0);

    // A targeted knob redraw leaves the rest of the cache intact: skip the
    // background clear and the voice-pad row, and redraw only the one knob.
    if (!partial) {
        RECT rc = { 0, 0, w, h };
        HBRUSH bg = CreateSolidBrush(RGB(22, 26, 33));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
    }

    Clip* c = synth_ui_target_clip();
    if (!c) return;

    bool quadrum = (c->clipKind == CLIP_KIND_QUADRUM);
    COLORREF accent = quadrum ? RGB(56, 194, 224) : RGB(255, 140, 25);
    COLORREF accentDim = quadrum ? RGB(28, 92, 112) : RGB(140, 65, 12);
    COLORREF text = quadrum ? RGB(230, 237, 243) : RGB(240, 244, 250);
    COLORREF textDim = quadrum ? RGB(138, 150, 166) : RGB(145, 155, 170);

    if (quadrum) {
        // Voice pads row (selectable; knobs edit the selected voice). Centered
        // above the knob rack, drawn in the Inter UI font.
        if (!partial) {
            HFONT oldPadFont = SELECT_UI_FONT(dc);
            // TRANSPARENT so the pad label text doesn't get a white OPAQUE
            // background painted over the pad fill.
            SetBkMode(dc, TRANSPARENT);
            for (int v = 0; v < 8; ++v) {
                RECT pr;
                synth_quad_pad_rect(v, w, &pr);
                bool sel = (v == s_synthQuadVoice);
                HBRUSH vb = CreateSolidBrush(sel ? RGB(30, 48, 60) : RGB(20, 24, 31));
                HPEN vp = CreatePen(PS_SOLID, sel ? 2 : 1, sel ? RGB(140, 235, 255) : RGB(56, 194, 224));
                HGDIOBJ ob = SelectObject(dc, vb);
                HGDIOBJ op = SelectObject(dc, vp);
                RoundRect(dc, pr.left, pr.top, pr.right, pr.bottom, 4, 4);
                SelectObject(dc, op); SelectObject(dc, ob);
                DeleteObject(vp); DeleteObject(vb);
                SetTextColor(dc, sel ? RGB(190, 245, 255) : RGB(140, 235, 255));
                DrawTextA(dc, VOICE_NAMES[v], -1, &pr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            SelectObject(dc, oldPadFont);
        }

        // 16 knobs for the selected voice (start below the voice pads).
        QuadrumParams* p = &c->quadrumParams[s_synthQuadVoice];
        SynthKnob knobs[16] = { 0 };
        int count = synth_quadrum_init_knobs(knobs, p, s_synthQuadVoice);
        synth_knob_layout(knobs, count, w, 46);
        for (int i = 0; i < count; ++i) {
            if (partial && i != s_synthRedrawOnly) continue;
            // A partial redraw keeps the rest of the cache, so wipe this knob's
            // whole cell first — otherwise the transparent label/value text
            // blits stack on the previous frame and become unreadable.
            if (partial) {
                HBRUSH cb = CreateSolidBrush(RGB(22, 26, 33));
                FillRect(dc, &knobs[i].rect, cb);
                DeleteObject(cb);
            }
            synth_draw_knob(dc, &knobs[i], (i == s_synthDragKnob), accent, accentDim, text, textDim);
        }
    } else {
        // Halo: 29 knobs.
        HaloPatch* p = &c->haloPatch;
        SynthKnob knobs[29] = { 0 };
        int count = synth_halo_init_knobs(knobs, p);
        synth_knob_layout(knobs, count, w, 10);
        for (int i = 0; i < count; ++i) {
            if (partial && i != s_synthRedrawOnly) continue;
            if (partial) {
                HBRUSH cb = CreateSolidBrush(RGB(22, 26, 33));
                FillRect(dc, &knobs[i].rect, cb);
                DeleteObject(cb);
            }
            synth_draw_knob(dc, &knobs[i], (i == s_synthDragKnob), accent, accentDim, text, textDim);
        }

        // Preset-name and [Reset] buttons in the 4th row to the right of vibrato.
        RECT presetBtn, resetBtn, listRc;
        synth_preset_geometry(w, h, &presetBtn, &resetBtn, &listRc);
        HFONT oldPresetFont = SELECT_UI_FONT(dc);
        SetBkMode(dc, TRANSPARENT);

        // Preset button.
        HBRUSH pb = CreateSolidBrush(s_synthPresetOpen ? RGB(38, 48, 62) : RGB(28, 33, 42));
        HPEN pp = CreatePen(PS_SOLID, 1, accent);
        HGDIOBJ ob = SelectObject(dc, pb);
        HGDIOBJ op = SelectObject(dc, pp);
        RoundRect(dc, presetBtn.left, presetBtn.top, presetBtn.right, presetBtn.bottom, 3, 3);
        SelectObject(dc, op); SelectObject(dc, ob);
        DeleteObject(pp); DeleteObject(pb);
        int idx = (s_synthPresetSel >= 0 && s_synthPresetSel < HALO_PRESET_COUNT) ? s_synthPresetSel : 0;
        SetTextColor(dc, accent);
        DrawTextA(dc, HALO_PRESET_NAMES[idx], -1, &presetBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // [Reset] button beside it.
        HBRUSH rb = CreateSolidBrush(RGB(28, 33, 42));
        HPEN rp = CreatePen(PS_SOLID, 1, RGB(90, 105, 115));
        ob = SelectObject(dc, rb);
        op = SelectObject(dc, rp);
        RoundRect(dc, resetBtn.left, resetBtn.top, resetBtn.right, resetBtn.bottom, 3, 3);
        SelectObject(dc, op); SelectObject(dc, ob);
        DeleteObject(rp); DeleteObject(rb);
        SetTextColor(dc, RGB(140, 155, 175));
        DrawTextA(dc, "Reset", -1, &resetBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Preset dropdown modal list when open (extends top to bottom within
        // the knob panel boundary so it never spills past the compacted window).
        if (s_synthPresetOpen) {
            HBRUSH lb = CreateSolidBrush(RGB(18, 22, 28));
            HPEN lp = CreatePen(PS_SOLID, 1, accent);
            ob = SelectObject(dc, lb);
            op = SelectObject(dc, lp);
            RoundRect(dc, listRc.left, listRc.top, listRc.right, listRc.bottom, 4, 4);
            SelectObject(dc, op); SelectObject(dc, ob);
            DeleteObject(lp); DeleteObject(lb);

            for (int r = 0; r < HALO_PRESET_COUNT; ++r) {
                RECT row;
                synth_preset_row_rect(&listRc, r, &row);

                bool isHover = (r == s_synthPresetHover);
                bool isSel = (r == s_synthPresetSel);

                if (isHover || isSel) {
                    COLORREF rowBg = isHover ? (isSel ? RGB(46, 58, 76) : RGB(34, 44, 58))
                                             : RGB(28, 36, 48);
                    HBRUSH hb = CreateSolidBrush(rowBg);
                    FillRect(dc, &row, hb);
                    DeleteObject(hb);
                }

                if (isSel) {
                    // Small active indicator bar on the left edge
                    RECT indRc = { row.left + 2, row.top + 3, row.left + 5, row.bottom - 3 };
                    HBRUSH ib = CreateSolidBrush(accent);
                    FillRect(dc, &indRc, ib);
                    DeleteObject(ib);
                }

                // Row separator
                if (r < HALO_PRESET_COUNT - 1) {
                    HPEN sp = CreatePen(PS_SOLID, 1, RGB(28, 34, 44));
                    HGDIOBJ osp = SelectObject(dc, sp);
                    MoveToEx(dc, row.left + 6, row.bottom - 1, NULL);
                    LineTo(dc, row.right - 6, row.bottom - 1);
                    SelectObject(dc, osp);
                    DeleteObject(sp);
                }

                SetTextColor(dc, isSel ? accent : (isHover ? RGB(245, 248, 252) : textDim));
                RECT textRc = { row.left + 12, row.top, row.right - 8, row.bottom };
                DrawTextA(dc, HALO_PRESET_NAMES[r], -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        }
        SelectObject(dc, oldPresetFont);
    }

    s_synthRedrawOnly = -1;
}

// --- Window proc -----------------------------------------------------------
static LRESULT CALLBACK SynthUIWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static inline void synth_ui_open(HWND parentHwnd) {
    Clip* target = synth_ui_target_clip();
    if (!target) return;
    bool quadrum = (target->clipKind == CLIP_KIND_QUADRUM);

    if (!g_synthHwnd || !IsWindow(g_synthHwnd)) {
        static bool s_registered = false;
        if (!s_registered) {
            WNDCLASSA wc = { 0 };
            wc.lpfnWndProc   = SynthUIWndProc;
            wc.hInstance     = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractSynthUIClass";
            wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_registered = true;
        }
        int rw = synth_ui_window_width();
        int rh = synth_ui_window_height(quadrum);
        int scrW = GetSystemMetrics(SM_CXSCREEN), scrH = GetSystemMetrics(SM_CYSCREEN);
        int rx = (scrW - rw) / 2, ry = (scrH - rh) / 2;
        if (parentHwnd && IsWindow(parentHwnd)) {
            RECT prc; GetWindowRect(parentHwnd, &prc);
            rx = prc.left + ((prc.right - prc.left) - rw) / 2;
            ry = prc.bottom + 8;
        }
        g_synthHwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            "RefractSynthUIClass", "Synth",
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE,
            rx, ry, rw, rh,
            parentHwnd, NULL, GetModuleHandle(NULL), NULL
        );
    }

    Clip* c = synth_ui_target_clip();
    char title[64];
    if (c && c->clipKind == CLIP_KIND_QUADRUM)
        snprintf(title, sizeof(title), "Quadrum - Clip %d", g_midiEdit.clipIdx + 1);
    else
        snprintf(title, sizeof(title), "Halo - Clip %d", g_midiEdit.clipIdx + 1);
    SetWindowTextA(g_synthHwnd, title);

    // Ensure width and height match current mode
    {
        RECT wrc; GetWindowRect(g_synthHwnd, &wrc);
        int wantW = synth_ui_window_width();
        int wantH = synth_ui_window_height(quadrum);
        if ((wrc.bottom - wrc.top) != wantH || (wrc.right - wrc.left) != wantW) {
            SetWindowPos(g_synthHwnd, NULL, wrc.left, wrc.top, wantW, wantH, SWP_NOZORDER);
        }
    }

    invalidate_synth_cache();
    ShowWindow(g_synthHwnd, SW_SHOW);
    SetForegroundWindow(g_synthHwnd);
    InvalidateRect(g_synthHwnd, NULL, FALSE);
}

static inline void synth_ui_close(void) {
    if (g_synthHwnd && IsWindow(g_synthHwnd)) {
        ShowWindow(g_synthHwnd, SW_HIDE);
    }
}

static inline void synth_ui_destroy(void) {
    if (g_synthHwnd && IsWindow(g_synthHwnd)) {
        DestroyWindow(g_synthHwnd);
        g_synthHwnd = NULL;
    }
}

static LRESULT CALLBACK SynthUIWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        // The whole client is repainted from the double-buffered cache in
        // WM_PAINT; suppress the default background erase to avoid flicker.
        return 1;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        if (s_synthCacheDC) {
            if (s_synthCacheBmp) {
                SelectObject(s_synthCacheDC, s_synthCacheOldBmp);
                DeleteObject(s_synthCacheBmp);
                s_synthCacheBmp = NULL;
            }
            DeleteDC(s_synthCacheDC);
            s_synthCacheDC = NULL;
        }
        s_synthCacheW = 0; s_synthCacheH = 0;
        s_synthCacheInvalid = true;
        g_synthHwnd = NULL;
        s_synthDragKnob = -1;
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

        if (synth_ui_is_dirty(w, h)) {
            synth_ui_update_cache(hdc, w, h);
            s_synthCacheInvalid = false;
        }
        if (s_synthCacheDC) {
            BitBlt(memDC, 0, 0, w, h, s_synthCacheDC, 0, 0, SRCCOPY);
        } else {
            RECT fillRc = { 0, 0, w, h };
            HBRUSH bg = CreateSolidBrush(RGB(22, 26, 33));
            FillRect(memDC, &fillRc, bg);
            DeleteObject(bg);
        }

        // Bottom footer hints
        {
            HFONT oldFontHint = SELECT_UI_FONT(memDC);
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, RGB(140, 155, 175));

            Clip* cTarget = synth_ui_target_clip();
            bool isQuad = (cTarget && cTarget->clipKind == CLIP_KIND_QUADRUM);

            // Left hint: "Right-click knob to reset" (shows for both Quadrum and Halo)
            RECT resetHintRc = { 16, h - 26, 320, h - 4 };
            const char* resetText = isQuad 
                ? "Right-click knob or pad to reset"
                : "Right-click knob to reset";
            DrawTextA(memDC, resetText, -1, &resetHintRc,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Right hint: ESC / ENTER to close
            RECT escHintRc = { w - 340, h - 26, w - 16, h - 4 };
            DrawTextA(memDC, "Press [ESC] or [ENTER] to close", -1, &escHintRc,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(memDC, oldFontHint);
        }

        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (s_synthPresetOpen) {
                s_synthPresetOpen = 0;
                s_synthPresetHover = -1;
                invalidate_synth_cache();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        if (wParam == VK_RETURN) {
            if (s_synthPresetOpen) {
                Clip* c = synth_ui_target_clip();
                if (c && c->clipKind == CLIP_KIND_HALO &&
                    s_synthPresetHover >= 0 && s_synthPresetHover < HALO_PRESET_COUNT) {
                    seq_lock();
                    halo_get_preset(s_synthPresetHover, &c->haloPatch);
                    seq_unlock();
                    s_synthPresetSel = s_synthPresetHover;
                }
                s_synthPresetOpen = 0;
                s_synthPresetHover = -1;
                invalidate_synth_cache();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        return 0;

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        SetFocus(hwnd);
        Clip* c = synth_ui_target_clip();
        if (!c) return 0;
        RECT rc; GetClientRect(hwnd, &rc);
        int clientW = rc.right - rc.left;
        int clientH = rc.bottom - rc.top;
        bool quadrum = (c->clipKind == CLIP_KIND_QUADRUM);

        // Quadrum: click a voice pad to select which voice the knobs edit
        if (quadrum) {
            for (int v = 0; v < 8; ++v) {
                RECT pr;
                synth_quad_pad_rect(v, clientW, &pr);
                if (mx >= pr.left && mx <= pr.right && my >= pr.top && my <= pr.bottom) {
                    s_synthQuadVoice = v;
                    invalidate_synth_cache();
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
        }

        // Halo: if preset modal is open, handle clicks on it or dismiss it
        if (!quadrum && s_synthPresetOpen) {
            RECT presetBtn, resetBtn, listRc;
            synth_preset_geometry(clientW, clientH, &presetBtn, &resetBtn, &listRc);
            POINT pt = { mx, my };

            // Selection inside the list
            if (PtInRect(&listRc, pt)) {
                int r = synth_preset_row_hit(&listRc, my);
                if (r >= 0 && r < HALO_PRESET_COUNT) {
                    seq_lock();
                    halo_get_preset(r, &c->haloPatch);
                    seq_unlock();
                    s_synthPresetSel = r;
                }
                s_synthPresetOpen = 0;
                s_synthPresetHover = -1;
                invalidate_synth_cache();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            // [Reset] button
            if (PtInRect(&resetBtn, pt)) {
                seq_lock();
                halo_get_preset(0, &c->haloPatch);
                seq_unlock();
                s_synthPresetSel = 0;
                s_synthPresetOpen = 0;
                s_synthPresetHover = -1;
                invalidate_synth_cache();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            // Clicked outside modal: dismiss modal cleanly without triggering controls underneath
            s_synthPresetOpen = 0;
            s_synthPresetHover = -1;
            invalidate_synth_cache();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        // Halo: preset button + reset button when closed
        if (!quadrum) {
            RECT presetBtn, resetBtn, listRc;
            synth_preset_geometry(clientW, clientH, &presetBtn, &resetBtn, &listRc);
            POINT pt = { mx, my };
            if (PtInRect(&presetBtn, pt)) {
                s_synthPresetOpen = 1;
                s_synthPresetHover = s_synthPresetSel;
                invalidate_synth_cache();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (PtInRect(&resetBtn, pt)) {
                seq_lock();
                halo_get_preset(0, &c->haloPatch);
                seq_unlock();
                s_synthPresetSel = 0;
                s_synthPresetOpen = 0;
                invalidate_synth_cache();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }

        // Knob drag hit-testing
        int count = quadrum ? 16 : 29;
        SynthKnob knobs[29] = { 0 };
        if (quadrum) { QuadrumParams* p = &c->quadrumParams[s_synthQuadVoice]; count = synth_quadrum_init_knobs(knobs, p, s_synthQuadVoice); }
        else         { HaloPatch* p = &c->haloPatch; count = synth_halo_init_knobs(knobs, p); }
        synth_knob_layout(knobs, count, clientW, quadrum ? 46 : 10);
        for (int i = 0; i < count; ++i) {
            if (knobs[i].disabled) continue;
            if (mx >= knobs[i].rect.left && mx <= knobs[i].rect.right &&
                my >= knobs[i].rect.top  && my <= knobs[i].rect.bottom) {
                seq_lock();
                s_synthDragStartVal = *knobs[i].param;
                seq_unlock();
                s_synthDragKnob = i;
                s_synthDragStartY = my;
                SetCapture(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }
        return 0;
    }

    case WM_RBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        Clip* c = synth_ui_target_clip();
        if (!c) return 0;
        RECT rc; GetClientRect(hwnd, &rc);
        int clientW = rc.right - rc.left;
        bool quadrum = (c->clipKind == CLIP_KIND_QUADRUM);

        // Quadrum: right-click a voice pad to reset that voice to factory default
        if (quadrum) {
            for (int v = 0; v < 8; ++v) {
                RECT pr;
                synth_quad_pad_rect(v, clientW, &pr);
                if (mx >= pr.left && mx <= pr.right && my >= pr.top && my <= pr.bottom) {
                    seq_lock();
                    quadrum_get_preset((VoiceType)v, &c->quadrumParams[v]);
                    seq_unlock();
                    synth_quadrum_rerender_voice(g_midiEdit.clipIdx, v);
                    invalidate_synth_cache();
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
        }

        // Right-click a knob resets ONLY that knob to its individual factory
        // default (not the whole patch). The default struct is built once and
        // the right-clicked field is copied over by its offset within the
        // all-double patch struct.
        int count = quadrum ? 16 : 29;
        SynthKnob knobs[29] = { 0 };
        if (quadrum) { QuadrumParams* p = &c->quadrumParams[s_synthQuadVoice]; count = synth_quadrum_init_knobs(knobs, p, s_synthQuadVoice); }
        else         { HaloPatch* p = &c->haloPatch; count = synth_halo_init_knobs(knobs, p); }
        synth_knob_layout(knobs, count, clientW, quadrum ? 46 : 10);
        for (int i = 0; i < count; ++i) {
            if (knobs[i].disabled) continue;
            if (mx >= knobs[i].rect.left && mx <= knobs[i].rect.right &&
                my >= knobs[i].rect.top  && my <= knobs[i].rect.bottom) {
                seq_lock();
                if (quadrum) {
                    QuadrumParams def;
                    quadrum_get_preset((VoiceType)s_synthQuadVoice, &def);
                    double* liveBase = (double*)&c->quadrumParams[s_synthQuadVoice];
                    double* defBase  = (double*)&def;
                    ptrdiff_t off = knobs[i].param - liveBase;
                    if (off >= 0 && off < (ptrdiff_t)(sizeof(QuadrumParams) / sizeof(double)))
                        liveBase[off] = defBase[off];
                } else {
                    HaloPatch def;
                    halo_get_preset(0, &def);
                    double* liveBase = (double*)&c->haloPatch;
                    double* defBase  = (double*)&def;
                    ptrdiff_t off = knobs[i].param - liveBase;
                    if (off >= 0 && off < (ptrdiff_t)(sizeof(HaloPatch) / sizeof(double)))
                        liveBase[off] = defBase[off];
                }
                seq_unlock();
                if (quadrum) synth_quadrum_rerender_voice(g_midiEdit.clipIdx, s_synthQuadVoice);
                invalidate_synth_cache();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        // Update preset dropdown hover for highlight
        Clip* hoverC = synth_ui_target_clip();
        if (hoverC && hoverC->clipKind != CLIP_KIND_QUADRUM && s_synthPresetOpen) {
            RECT rc; GetClientRect(hwnd, &rc);
            RECT presetBtn, resetBtn, listRc;
            synth_preset_geometry(rc.right - rc.left, rc.bottom - rc.top, &presetBtn, &resetBtn, &listRc);
            POINT pt = { mx, my };
            int hoverRow = PtInRect(&listRc, pt) ? synth_preset_row_hit(&listRc, my) : -1;
            if (hoverRow != s_synthPresetHover) {
                s_synthPresetHover = hoverRow;
                invalidate_synth_cache();
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        if (s_synthDragKnob >= 0 && (wParam & MK_LBUTTON)) {
            Clip* c = synth_ui_target_clip();
            if (!c) return 0;
            RECT rc; GetClientRect(hwnd, &rc);
            bool quadrum = (c->clipKind == CLIP_KIND_QUADRUM);
            int count = quadrum ? 16 : 29;
            SynthKnob knobs[29] = { 0 };
            if (quadrum) { QuadrumParams* p = &c->quadrumParams[s_synthQuadVoice]; count = synth_quadrum_init_knobs(knobs, p, s_synthQuadVoice); }
            else         { HaloPatch* p = &c->haloPatch; count = synth_halo_init_knobs(knobs, p); }
            synth_knob_layout(knobs, count, rc.right - rc.left, quadrum ? 46 : 10);
            int idx = s_synthDragKnob;
            if (idx >= 0 && idx < count) {
                SynthKnob* k = &knobs[idx];
                double range = k->max - k->min;
                double delta = -(double)(my - s_synthDragStartY) * 0.004 * range;
                double startNorm = synth_knob_value_to_norm(k, s_synthDragStartVal);
                double norm = startNorm + (delta / range);
                if (norm < 0.0) norm = 0.0;
                if (norm > 1.0) norm = 1.0;
                double val = synth_knob_norm_to_value(k, norm);
                if (k->isInt) val = (double)lround(val);
                seq_lock();
                *k->param = val;
                seq_unlock();
                synth_ui_push_patch(g_midiEdit.clipIdx);
                // Redraw only the dragged knob into the cache (not all 29) so
                // the UI thread stays free to keep the piano-roll playhead
                // scrolling during a live edit.
                synth_ui_redraw_knob(idx);
                InvalidateRect(hwnd, &knobs[idx].rect, FALSE);
            }
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        int mx = pt.x, my = pt.y;
        short zDelta = (short)HIWORD(wParam);
        bool fine = (wParam & MK_SHIFT) != 0;
        Clip* c = synth_ui_target_clip();
        if (!c) return 0;
        RECT rc; GetClientRect(hwnd, &rc);
        int clientW = rc.right - rc.left;
        int clientH = rc.bottom - rc.top;
        bool quadrum = (c->clipKind == CLIP_KIND_QUADRUM);

        // If preset list modal is open and mouse is over it, wheel cycles through presets
        if (!quadrum && s_synthPresetOpen) {
            RECT presetBtn, resetBtn, listRc;
            synth_preset_geometry(clientW, clientH, &presetBtn, &resetBtn, &listRc);
            if (PtInRect(&listRc, (POINT){ mx, my })) {
                int nextSel = s_synthPresetSel + (zDelta > 0 ? -1 : 1);
                if (nextSel < 0) nextSel = 0;
                if (nextSel >= HALO_PRESET_COUNT) nextSel = HALO_PRESET_COUNT - 1;
                if (nextSel != s_synthPresetSel) {
                    seq_lock();
                    halo_get_preset(nextSel, &c->haloPatch);
                    seq_unlock();
                    s_synthPresetSel = nextSel;
                    s_synthPresetHover = nextSel;
                    invalidate_synth_cache();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            }
        }

        int count = quadrum ? 16 : 29;
        SynthKnob knobs[29] = { 0 };
        if (quadrum) { QuadrumParams* p = &c->quadrumParams[s_synthQuadVoice]; count = synth_quadrum_init_knobs(knobs, p, s_synthQuadVoice); }
        else         { HaloPatch* p = &c->haloPatch; count = synth_halo_init_knobs(knobs, p); }
        synth_knob_layout(knobs, count, clientW, quadrum ? 46 : 10);
        for (int i = 0; i < count; ++i) {
            if (knobs[i].disabled) continue;
            if (mx >= knobs[i].rect.left && mx <= knobs[i].rect.right &&
                my >= knobs[i].rect.top  && my <= knobs[i].rect.bottom) {
                SynthKnob* k = &knobs[i];
                double range = k->max - k->min;
                double step = fine ? range * 0.004 : range * 0.02;
                if (k->isInt) step = fine ? 2.0 : 4.0;
                
                double val = *k->param + (zDelta > 0 ? step : -step);
                if (k->isInt) val = (double)lround(val);
                if (val < k->min) val = k->min;
                if (val > k->max) val = k->max;
                seq_lock();
                *k->param = val;
                seq_unlock();
                synth_ui_push_patch(g_midiEdit.clipIdx);
                invalidate_synth_cache();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if (GetCapture() == hwnd) ReleaseCapture();
        s_synthDragKnob = -1;
        invalidate_synth_cache();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

#endif /* CSEQ_SYNTHUI_H */