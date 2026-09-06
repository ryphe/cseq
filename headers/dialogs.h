#pragma once
#include "globals.h"
#include "dsp.h"
#include "fx.h"
#include "ui.h"
#include "actions.h"
#include "granular.h"
#include "synthui.h"
#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

// FX rack UI interaction state (defined at the bottom of this file where the
// rack is implemented; type is hoisted here so reset_to_init_state can clear
// its selection without a forward mess).
typedef struct {
    bool  dragging;
    bool  fromChain;
    int   index;
    int   startX, startY;
    int   dragX, dragY;
    bool  moved;
    bool  overRemove;
    int   dropSlot;
    int   selectedSlot;
    int   dragParam;
    bool  paramIsKnob;
    bool  knobIsLog;
    int   knobStartX, knobStartY;
    float knobStartVal;
} FxRackUiState;
extern FxRackUiState g_fxRackUi;

 
static inline void save_project_to_csq(const char* path);
static inline void save_project_dialog(HWND hwnd);
static inline void load_project_from_csq(const char* path);
static inline void load_project_dialog(HWND hwnd);
static inline void open_midi_editor(HWND parentHwnd, int clipIdx);
static inline void open_seq_dialog(HWND parentHwnd);
static inline void open_humanize_dialog(HWND parentHwnd);

// Defined later in this file; used by the editor toolbar drawing/clicks.
static inline void seq_blit_supersampled(HDC dc, int dx, int dy, int dw, int dh,
                                         HBITMAP bmp, int sw, int sh, int ss);
static inline void sfont_open_inst_selector(HWND parent);

 
static char g_pendingCsqPath[MAX_PATH] = "";

 
static bool g_loadDialogPending = false;

 
static char g_confirmLoadPath[MAX_PATH] = "";

 

static inline void update_window_title(void) {
    if (!g_hWnd) return;
    if (g_Seq.currentProjectName[0]) {
         
        wchar_t nameW[MAX_PATH];
        if (utf8_to_wide_buf(g_Seq.currentProjectName, nameW, MAX_PATH) > 0) {
            wchar_t titleW[MAX_PATH + 64];
            _snwprintf(titleW, sizeof(titleW) / sizeof(titleW[0]), L"cseq - %s", nameW);
            titleW[(sizeof(titleW) / sizeof(titleW[0])) - 1] = L'\0';
            SetWindowTextW(g_hWnd, titleW);
            return;
        }
        char title[MAX_PATH + 64];
        snprintf(title, sizeof(title), "cseq - %s",
                 g_Seq.currentProjectName);
        SetWindowTextA(g_hWnd, title);
    }
    else {
        char title[64];
        snprintf(title, sizeof(title), "cseq");
        SetWindowTextA(g_hWnd, title);
    }
}

static inline void reset_to_init_state(void) {
     
    if (g_Seq.deviceInitialized) {
        ma_device_stop(&g_Seq.device);
    }

    
    seq_set_playing(false);
    set_playback_frame(0);
    granular_stop_all();
    synth_stop_all();

     
    seq_lock();

     
    clear_clipboard();    
    push_undo_state();

    
    for (int t = 0; t < MAX_TRACKS; ++t) {
        if (g_TrackGran[t].ownFrames) {
            free(g_TrackGran[t].ownFrames);
            g_TrackGran[t].ownFrames = NULL;
        }
        g_TrackGran[t].ownLoaded = false;
        g_TrackGran[t].ownFrameCount = 0;
        memset(&g_TrackGran[t], 0, sizeof(GranularEngine));
        g_TrackGran[t].trackIdx = t;
        g_TrackGran[t].clipIdx = -1;
        g_TrackGran[t].sampleIndex = -1;
        g_TrackGran[t].volume = 0.85f;
    }

    
    for (int c = 0; c < MAX_CLIPS; ++c) {
        if (g_ClipGran[c].ownFrames) {
            free(g_ClipGran[c].ownFrames);
            g_ClipGran[c].ownFrames = NULL;
        }
        g_ClipGran[c].ownLoaded = false;
        g_ClipGran[c].ownFrameCount = 0;
        memset(&g_ClipGran[c], 0, sizeof(GranularEngine));
        g_ClipGran[c].clipIdx = c;
        g_ClipGran[c].sampleIndex = -1;
        g_ClipGran[c].volume = 0.85f;
    }

    // Reset per-clip synth engine runtime state (frees any quadrum transient
    // buffers and re-inits halo voice managers).
    synth_state_reset_all();

    // Clear any open transient-slice preview so it can't reference clips that
    // are about to be cleared.
    g_slicePreview.active = false;
    g_slicePreview.clipCount = 0;

    
    g_Seq.clipCount = 0;

    // Reset the SoundFont engine and its note cache so returning to a blank
    // canvas unloads the previous project's font. (Not restored by undo.)
    sfont_clear();

    
    g_Seq.bpm = 120.0f;
    g_Seq.swing = 0.0f;
    g_Seq.visibleBarCount = 4;
    g_Seq.trackCount = 4;
    g_Seq.zoom = 1.0f;
    g_Seq.timeSigNum = 4;
    g_Seq.timeSigDen = 4;
    g_Seq.isLofi = false;
    g_Seq.quantizeEnabled = true;
    g_Seq.playFromStartOnPlay = false;
    g_Seq.currentProjectName[0] = '\0';
    g_Seq.currentProjectFile[0] = '\0';
    g_Seq.isModified = false;
    
    g_Seq.lofiBitDepth   = 12;
    g_Seq.lofiSampleRate = 14700.0f;
    
    g_Seq.masterVolume  = 1.0f;
    g_Seq.masterMode    = 1;

    
    for (int t = 0; t < MAX_TRACKS; ++t) {
        init_track_theme(t);
        g_Seq.trackVolume[t] = 1.0f;
        g_Seq.trackMuted[t] = false;
        g_Seq.trackSolo[t] = false;
        g_Seq.trackPan[t] = 0.0f;
        g_Seq.trackWidth[t] = 1.0f;
        g_Seq.trackTriggerProb[t] = 1.0f;
        g_Seq.trackRngState[t] = (uint32_t)(t * 1337) + 1;
        g_Seq.trackEqLow[t] = 0.5f;
        g_Seq.trackEqMid[t] = 0.5f;
        g_Seq.trackEqHigh[t] = 0.5f;
        g_Seq.trackEqActive[t] = false;
        smooth_eq3_set_params(&g_Seq.trackEQ[t], 0.5f, 0.5f, 0.5f);
        for (int b = 0; b < 3; ++b) {
            peak_biquad_clear(&g_Seq.trackPeak[t][b]);
            peak_biquad_set(&g_Seq.trackPeak[t][b], 1000.0f, 0.7f, 0.0f, (float)SAMPLE_RATE);
        }
    }

    // Reset per-track filter plotters to default
    for (int t = 0; t < MAX_TRACKS; ++t) {
        track_filter_init_defaults(&g_Seq.trackFilter[t]);
    }

    // Wipe all track FX chains (the pre-reset state survives in the undo
    // snapshot pushed above, so Ctrl+Z restores it).
    fx_init_all();

    g_Seq.rateUndoDebounceTimer = 0;


    seq_unlock();

    g_fxRackUi.selectedSlot = -1;
    if (g_fxRackHwnd && IsWindow(g_fxRackHwnd)) {
        InvalidateRect(g_fxRackHwnd, NULL, FALSE);
    }

    cseq_clip_structure_changed();
    mark_all_bars_dirty();

    
    if (g_Seq.deviceInitialized) {
        ma_device_start(&g_Seq.device);
    }

    
    update_window_title();
    update_scrollbar(g_hWnd);
    InvalidateRect(g_hWnd, NULL, FALSE);
}

 

#define CONFIRM_INIT 1
#define CONFIRM_QUIT 2
#define CONFIRM_LOAD 3

static HWND g_confirmHwnd = NULL;
static int  g_confirmType = 0;

static inline const char* confirm_dialog_title(int type) {
    switch (type) {
        case CONFIRM_INIT: return "Confirm Project Reset";
        case CONFIRM_QUIT: return "Confirm Quit";
        case CONFIRM_LOAD: return "Load Project";
        default:           return "Confirm";
    }
}

 
static inline void confirm_apply(HWND hwnd, bool yes) {
    int act = g_confirmType;
    ShowWindow(hwnd, SW_HIDE);

    if (act == CONFIRM_INIT) {
        if (yes) reset_to_init_state();
        return;
    }

    if (act == CONFIRM_QUIT) {
        if (yes) {
            g_Seq.isModified = false;
            HWND target = g_hWnd;
            if (!target || !IsWindow(target)) target = GetWindow(hwnd, GW_OWNER);
            if (!target || !IsWindow(target)) target = GetParent(hwnd);
            if (target && IsWindow(target)) {
                DestroyWindow(target);
            } else {
                PostQuitMessage(0);
            }
        }
        return;
    }

    if (act == CONFIRM_LOAD) {
        char loadPath[MAX_PATH];
        strncpy(loadPath, g_confirmLoadPath, MAX_PATH - 1);
        loadPath[MAX_PATH - 1] = '\0';
        g_confirmLoadPath[0] = '\0';

        if (yes) {
            
            if (g_Seq.currentProjectFile[0]) {
                save_project_to_csq(g_Seq.currentProjectFile);
            } else {
                save_project_dialog(hwnd);
            }

            
            while (g_Seq.isBusy) {
                MSG msg;
                while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessageA(&msg);
                }
                Sleep(10); 
            }

            
            if (loadPath[0]) {
                
                load_project_from_csq(loadPath);
            } else {
                
                load_project_dialog(hwnd);
            }
        } else {
            
            if (loadPath[0]) {
                if (!job_is_busy()) load_project_from_csq(loadPath);
            } else {
                load_project_dialog(hwnd);
            }
        }
    }
}

static LRESULT CALLBACK ConfirmWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HFONT oldFontMain = SELECT_UI_FONT(hdc);

            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left, h = rc.bottom - rc.top;

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
            HFONT oldFontMem = SELECT_UI_FONT(memDC);

            HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
            FillRect(memDC, &rc, bg);
            DeleteObject(bg);

            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, RGB(225, 235, 245));

            const char* mainText;
            const char* subText;
            if (g_confirmType == CONFIRM_INIT) {
                mainText = "Reset project to initial state?";
                subText  = "All elements will be cleared (Undoable with Ctrl+Z).";
            }
            else if (g_confirmType == CONFIRM_LOAD) {
                mainText = "Save changes before loading?";
                subText  = "[YES] Save && Load project  |  [NO] Discard Changes";
            }
            else {
                mainText = "Quit with unsaved changes?";
                subText  = "Any unsaved changes will be permanently lost.";
            }

            RECT tRc = { 10, 16, w - 10, 42 };
            DrawTextA(memDC, mainText, -1, &tRc, DT_CENTER | DT_SINGLELINE);
            SetTextColor(memDC, RGB(130, 145, 165));
            RECT sRc = { 10, 44, w - 10, 68 };
            DrawTextA(memDC, subText, -1, &sRc, DT_CENTER | DT_SINGLELINE);

            
            RECT yBtn = { 85, 85, 175, 115 };
            HBRUSH yBg = CreateSolidBrush(RGB(22, 90, 55));
            HPEN yPn = CreatePen(PS_SOLID, 1, RGB(80, 240, 180));
            HGDIOBJ ob = SelectObject(memDC, yBg);
            HGDIOBJ op = SelectObject(memDC, yPn);
            RoundRect(memDC, yBtn.left, yBtn.top, yBtn.right, yBtn.bottom, 4, 4);
            SetTextColor(memDC, RGB(160, 255, 205));
            DrawTextA(memDC, "YES [Y]", -1, &yBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            
            RECT nBtn = { 225, 85, 315, 115 };
            HBRUSH nBg = CreateSolidBrush(RGB(140, 35, 35));
            HPEN nPn = CreatePen(PS_SOLID, 1, RGB(240, 90, 90));
            SelectObject(memDC, nBg);
            SelectObject(memDC, nPn);
            RoundRect(memDC, nBtn.left, nBtn.top, nBtn.right, nBtn.bottom, 4, 4);
            SetTextColor(memDC, RGB(255, 210, 210));
            DrawTextA(memDC, "NO [N]", -1, &nBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(memDC, op);
            SelectObject(memDC, ob);
            DeleteObject(yPn); DeleteObject(yBg);
            DeleteObject(nPn); DeleteObject(nBg);

            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldFontMem);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            SelectObject(hdc, oldFontMain);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            if (my >= 85 && my <= 115) {
                if (mx >= 85 && mx <= 175) {  
                    confirm_apply(hwnd, true);
                    return 0;
                }
                if (mx >= 225 && mx <= 315) {  
                    confirm_apply(hwnd, false);
                    return 0;
                }
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == 'Y' || wParam == VK_RETURN) {
                confirm_apply(hwnd, true);
            }
            else if (wParam == 'N' || wParam == VK_ESCAPE) {
                g_confirmLoadPath[0] = '\0';    
                ShowWindow(hwnd, SW_HIDE);
            }
            return 0;

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            g_confirmHwnd = NULL;
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static inline void open_confirm_dialog(HWND parentHwnd, int confirmType) {
    g_confirmType = confirmType;
    int rw = 400, rh = 180;
    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);

    int rx = (scrW - rw) / 2;
    int ry = (scrH - rh) / 2;

    if (parentHwnd && IsWindow(parentHwnd) && !IsIconic(parentHwnd)) {
        RECT rcParent;
        GetWindowRect(parentHwnd, &rcParent);
        if (rcParent.right > rcParent.left && rcParent.bottom > rcParent.top) {
            rx = rcParent.left + ((rcParent.right - rcParent.left) - rw) / 2;
            ry = rcParent.top + ((rcParent.bottom - rcParent.top) - rh) / 2;
        }
    }

    
    if (rx < 0 || rx + rw > scrW) rx = (scrW - rw) / 2;
    if (ry < 0 || ry + rh > scrH) ry = (scrH - rh) / 2;

    if (!g_confirmHwnd || !IsWindow(g_confirmHwnd)) {
        static bool s_confRegistered = false;
        if (!s_confRegistered) {
            WNDCLASSA wc = { 0 };
            wc.lpfnWndProc = ConfirmWndProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractConfirmClass";
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_confRegistered = true;
        }

        g_confirmHwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            "RefractConfirmClass",
            confirm_dialog_title(confirmType),
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE,
            rx, ry, rw, rh,
            parentHwnd, NULL, GetModuleHandle(NULL), NULL
        );
    } else {
        SetWindowPos(g_confirmHwnd, HWND_TOPMOST, rx, ry, rw, rh, SWP_SHOWWINDOW);
    }

    SetWindowTextA(g_confirmHwnd, confirm_dialog_title(confirmType));
    ShowWindow(g_confirmHwnd, SW_SHOW);
    SetForegroundWindow(g_confirmHwnd);
    InvalidateRect(g_confirmHwnd, NULL, FALSE);
}


 

static inline void show_export_context_menu(HWND hwnd, int screenX, int screenY) {
    HMENU hMenu = CreatePopupMenu();
    int curDepth = (g_Seq.exportBitDepth > 0) ? g_Seq.exportBitDepth : 32;

    char buf16[64], buf24[64], buf32[64];
    snprintf(buf16, sizeof(buf16), "16-bit PCM (CD Quality)\t%s", (curDepth == 16) ? "[ON]" : "[  ]");
    snprintf(buf24, sizeof(buf24), "24-bit PCM (Studio Quality)\t%s", (curDepth == 24) ? "[ON]" : "[  ]");
    snprintf(buf32, sizeof(buf32), "32-bit Float (Best Quality)\t%s", (curDepth == 32) ? "[ON]" : "[  ]");

    AppendMenuA(hMenu, MF_STRING, ID_EXPORT_DEPTH_16, buf16);
    AppendMenuA(hMenu, MF_STRING, ID_EXPORT_DEPTH_24, buf24);
    AppendMenuA(hMenu, MF_STRING, ID_EXPORT_DEPTH_32, buf32);

    MENUINFO miNoCheck = { sizeof(MENUINFO), MIM_STYLE, MNS_NOCHECK, 0, 0, 0, 0 };
    SetMenuInfo(hMenu, &miNoCheck);

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenX, screenY, 0, hwnd, NULL);
    DestroyMenu(hMenu);

    if (cmd == ID_EXPORT_DEPTH_16) g_Seq.exportBitDepth = 16;
    else if (cmd == ID_EXPORT_DEPTH_24) g_Seq.exportBitDepth = 24;
    else if (cmd == ID_EXPORT_DEPTH_32) g_Seq.exportBitDepth = 32;

    if (cmd > 0) {
        snprintf(g_Seq.exportMsg, sizeof(g_Seq.exportMsg), "Export format set to %d-bit.", g_Seq.exportBitDepth);
        g_Seq.exportMsgActive = true;
        g_Seq.exportMsgExpiry = GetTickCount64() + 2500;
        InvalidateRect(hwnd, NULL, FALSE);
    }
}


 

static inline void show_grid_context_menu(HWND hwnd, int screenX, int screenY) {
    HMENU hMenu = CreatePopupMenu();
    int curGrid = g_Seq.gridDivision;

    char buf16[64], buf16t[64], buf32[64], buf32t[64];
    snprintf(buf16,  sizeof(buf16),  "1/16 Notes\t%s",    (curGrid == GRID_1_16)  ? "[ON]" : "[  ]");
    snprintf(buf16t, sizeof(buf16t), "1/16 Triplets\t%s", (curGrid == GRID_1_16T) ? "[ON]" : "[  ]");
    snprintf(buf32,  sizeof(buf32),  "1/32 Notes\t%s",    (curGrid == GRID_1_32)  ? "[ON]" : "[  ]");
    snprintf(buf32t, sizeof(buf32t), "1/32 Triplets\t%s", (curGrid == GRID_1_32T) ? "[ON]" : "[  ]");

    AppendMenuA(hMenu, MF_STRING, ID_GRID_1_16,  buf16);
    AppendMenuA(hMenu, MF_STRING, ID_GRID_1_16T, buf16t);
    AppendMenuA(hMenu, MF_STRING, ID_GRID_1_32,  buf32);
    AppendMenuA(hMenu, MF_STRING, ID_GRID_1_32T, buf32t);

    MENUINFO miNoCheck = { sizeof(MENUINFO), MIM_STYLE, MNS_NOCHECK, 0, 0, 0, 0 };
    SetMenuInfo(hMenu, &miNoCheck);

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenX, screenY, 0, hwnd, NULL);
    DestroyMenu(hMenu);

    int newGrid = g_Seq.gridDivision;
    if (cmd == ID_GRID_1_16)       newGrid = GRID_1_16;
    else if (cmd == ID_GRID_1_16T) newGrid = GRID_1_16T;
    else if (cmd == ID_GRID_1_32)  newGrid = GRID_1_32;
    else if (cmd == ID_GRID_1_32T) newGrid = GRID_1_32T;

    if (cmd > 0) {
        g_Seq.gridDivision = newGrid;
        g_Seq.quantizeEnabled = true;  
        snprintf(g_Seq.exportMsg, sizeof(g_Seq.exportMsg), "Grid snap set to %s.", grid_division_label(newGrid));
        g_Seq.exportMsgActive = true;
        g_Seq.exportMsgExpiry = GetTickCount64() + 2500;
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

 

static HWND g_keybindsHwnd = NULL;

typedef struct {
    const char* key;
    const char* desc;
} KeybindRow;

static const KeybindRow kKeybindList[] = {
    { "/",                      "Open media explorer" },
    { "K",                      "Open keybinds window" },
    { "E",                      "Export timeline to WAV" },
    { "G",                      "Toggle Granular mode on clip" },
    { "L",                      "Toggle Lo-Fi 12-bit filter" },
    { "N",                      "Open MIDI editor (clip hover)" },
    { "Q",                      "Toggle grid snap" },
    { "S",                      "Split clip at mouse / playhead" },
    { "H",                      "Open note humanizer (piano roll)" },
    { "V",                      "Randomize note velocities (piano roll)" },
    { "M",                      "Toggle Mute on clip / track" },
    { "Shift + S",              "Toggle Solo on hovered track" },
    { "Shift + M",              "Insert 1/4-bar MIDI clip" },
    { "Shift + < / >",          "Change Bar Count (+/- 4)" },
    { "Alt + < / >",            "Pan timeline 8 bars (left or right)" },
    { "+  /  -",                "BPM adjust (+/- 1)" },
    { "[  /  ]",                "Swing adjust (+/- 5%)" },
    { "Home",                   "Jump playhead to start" },
    { "Shift + Home",           "Toggle playhead jump to start/cursor" },
    { "End",                    "Stop & jump to start" },
    { "Insert",                 "Import audio sample at cursor" },
    { "Delete",                 "Delete selected object(s)" },
    { "Space / Enter",          "Play / Pause toggle" },
    { "Page Up / Down",         "Zoom Timeline in / out" },
    { "Shift + Wheel",          "Adjust clip playback rate" },
    { "Shift + Middle Drag",    "Faster timeline viewport pan" },
    { "Alt + Drag Clip",        "Slip edit clip audio frames" },
    { "Shift + Drag Clip",      "Adjust clip volume/rate (edge)" },
    { "Shift + Drag Track",     "Adjust track ordering" },
    { "Ctrl + Scroll",          "Zoom timeline viewport" },
    { "Ctrl + S / O",           "Save / Load project file" },
    { "Ctrl + Z / Y",           "Undo / Redo action" },
    { "Ctrl + A / D",           "Select all / Deselect all clips" },
    { "Ctrl + C / V / X",       "Copy / Paste / Cut selected" },
    { "Ctrl + T / Shift + T",   "Add / Remove audio track" },
    { "Dbl-Click MIDI Clip",    "Open MIDI editor" },
    { "Middle-Click Drag",      "Pan timeline viewport" },
    { "Right-Click Object",     "Open context menu" },
};
#define KEYBIND_COUNT ((int)(sizeof(kKeybindList) / sizeof(kKeybindList[0])))

static LRESULT CALLBACK KeybindsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HFONT oldFontMain = SELECT_UI_FONT(hdc);

            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            if (w <= 0 || h <= 0) {
                SelectObject(hdc, oldFontMain);
                EndPaint(hwnd, &ps);
                return 0;
            }

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
            HFONT oldFontMem = SELECT_UI_FONT(memDC);

            HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
            FillRect(memDC, &rc, bg);
            DeleteObject(bg);

            SetBkMode(memDC, TRANSPARENT);

            int midX = w / 2;
            int startY = 16;
            int rowH = 21;
            int halfCount = (KEYBIND_COUNT + 1) / 2;

            
            HPEN divPen = CreatePen(PS_SOLID, 1, RGB(32, 38, 48));
            HGDIOBJ oldPen = SelectObject(memDC, divPen);
            MoveToEx(memDC, midX, startY - 2, NULL);
            LineTo(memDC, midX, startY + halfCount * rowH + 2);
            SelectObject(memDC, oldPen);
            DeleteObject(divPen);

            for (int i = 0; i < KEYBIND_COUNT; ++i) {
                bool isRightCol = (i >= halfCount);
                int row = isRightCol ? (i - halfCount) : i;
                int y = startY + row * rowH;

                RECT keyRc, descRc;
                 
                if (!isRightCol) {
                    
                    keyRc = (RECT){ 10, y, 140, y + rowH };
                    descRc = (RECT){ 150, y, midX - 10, y + rowH };
                } else {
                    
                    keyRc = (RECT){ midX + 10, y, midX + 140, y + rowH };
                    descRc = (RECT){ midX + 150, y, w - 10, y + rowH };
                }

                
                SetTextColor(memDC, RGB(110, 210, 240));
                DrawTextA(memDC, kKeybindList[i].key, -1, &keyRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                
                SetTextColor(memDC, RGB(170, 180, 195));
                DrawTextA(memDC, kKeybindList[i].desc, -1, &descRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            }

            
            SetTextColor(memDC, RGB(140, 155, 175));
            RECT hintRc = { 0, h - 26, w, h - 4 }; 
            DrawTextA(memDC, "Press [ESC] or [ENTER] to close", -1, &hintRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldFontMem);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            SelectObject(hdc, oldFontMain);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE || wParam == VK_RETURN) {
                ShowWindow(hwnd, SW_HIDE);
            }
            return 0;

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            g_keybindsHwnd = NULL;
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static inline void open_keybinds_dialog(HWND parentHwnd) {
    if (!g_keybindsHwnd) {
        static bool s_kbRegistered = false;
        if (!s_kbRegistered) {
            WNDCLASSA wc = { 0 };
            wc.lpfnWndProc = KeybindsWndProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractKeybindsClass";
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_kbRegistered = true;
        }

        
        // keybinds window width/height
        
        int rw = 840, rh = 480;
        int scrW = GetSystemMetrics(SM_CXSCREEN);
        int scrH = GetSystemMetrics(SM_CYSCREEN);
        int rx = (scrW - rw) / 2;
        int ry = (scrH - rh) / 2;

        g_keybindsHwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            "RefractKeybindsClass",
            "Keybinds & Shortcuts",
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE,
            rx, ry, rw, rh,
            parentHwnd, NULL, GetModuleHandle(NULL), NULL
        );
    }

    ShowWindow(g_keybindsHwnd, SW_SHOW);
    SetForegroundWindow(g_keybindsHwnd);
    InvalidateRect(g_keybindsHwnd, NULL, FALSE);
}

 

typedef struct {
    HWND hwnd;
    int trackIdx;
    int dragSlider; 
} PanWidthWindowContext;

static PanWidthWindowContext g_PanWidthWin = { 0 };

static LRESULT CALLBACK PanWidthWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            RECT rc; GetClientRect(hwnd, &rc);
            int trackLeft = 24, trackRight = rc.right - 24, trackW = trackRight - trackLeft;

            if (trackW > 0 && g_PanWidthWin.trackIdx >= 0 && g_PanWidthWin.trackIdx < g_Seq.trackCount) {
                int t = g_PanWidthWin.trackIdx;
                if (my >= 35 && my <= 65 && mx >= trackLeft && mx <= trackRight) {
                    float norm = (float)(mx - trackLeft) / (float)trackW;
                    seq_lock();
                    g_Seq.trackPan[t] = norm * 2.0f - 1.0f;
                    seq_unlock();
                    g_PanWidthWin.dragSlider = 1;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                else if (my >= 95 && my <= 125 && mx >= trackLeft && mx <= trackRight) {
                    float norm = (float)(mx - trackLeft) / (float)trackW;
                    seq_lock();
                    g_Seq.trackWidth[t] = norm * 2.0f;
                    seq_unlock();
                    g_PanWidthWin.dragSlider = 2;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (g_PanWidthWin.dragSlider > 0 && g_PanWidthWin.trackIdx >= 0 && g_PanWidthWin.trackIdx < g_Seq.trackCount) {
                int mx = GET_X_LPARAM(lParam);
                RECT rc; GetClientRect(hwnd, &rc);
                int trackLeft = 24, trackRight = rc.right - 24, trackW = trackRight - trackLeft;
                if (trackW > 0) {
                    float norm = (float)(mx - trackLeft) / (float)trackW;
                    if (norm < 0.0f) norm = 0.0f;
                    if (norm > 1.0f) norm = 1.0f;
                    int t = g_PanWidthWin.trackIdx;
                    seq_lock();
                    if (g_PanWidthWin.dragSlider == 1) {
                        g_Seq.trackPan[t] = norm * 2.0f - 1.0f;
                    }
                    else if (g_PanWidthWin.dragSlider == 2) {
                        g_Seq.trackWidth[t] = norm * 2.0f;
                    }
    seq_unlock();
    cseq_clip_structure_changed();
    InvalidateRect(hwnd, NULL, FALSE);
}
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            if (g_PanWidthWin.trackIdx < 0 || g_PanWidthWin.trackIdx >= g_Seq.trackCount)
                return 0;

            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);

            RECT rc;
            GetClientRect(hwnd, &rc);
            int trackLeft = 24, trackRight = rc.right - 24;
            if (pt.x < trackLeft || pt.x > trackRight) return 0;

            short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            int t = g_PanWidthWin.trackIdx;
            float step = (zDelta > 0) ? 0.05f : -0.05f;

            seq_lock();
            if (pt.y >= 35 && pt.y <= 65) {
                g_Seq.trackPan[t] += step;
                if (g_Seq.trackPan[t] < -1.0f) g_Seq.trackPan[t] = -1.0f;
                if (g_Seq.trackPan[t] > 1.0f) g_Seq.trackPan[t] = 1.0f;
            }
            else if (pt.y >= 95 && pt.y <= 125) {
                g_Seq.trackWidth[t] += step;
                if (g_Seq.trackWidth[t] < 0.0f) g_Seq.trackWidth[t] = 0.0f;
                if (g_Seq.trackWidth[t] > 2.0f) g_Seq.trackWidth[t] = 2.0f;
            }
            seq_unlock();

            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_LBUTTONUP:
            if (g_PanWidthWin.dragSlider > 0) {
                g_PanWidthWin.dragSlider = 0;
                ReleaseCapture();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_RBUTTONDOWN: {
            int my = GET_Y_LPARAM(lParam);
            if (g_PanWidthWin.trackIdx >= 0 && g_PanWidthWin.trackIdx < g_Seq.trackCount) {
                int t = g_PanWidthWin.trackIdx;
                seq_lock();
                if (my < 80) g_Seq.trackPan[t] = 0.0f;
                else g_Seq.trackWidth[t] = 1.0f;
                seq_unlock();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HFONT oldFontMain = SELECT_UI_FONT(hdc);

            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left, h = rc.bottom - rc.top;
            if (w <= 0 || h <= 0) {
                SelectObject(hdc, oldFontMain);
                EndPaint(hwnd, &ps);
                return 0;
            }

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
            HFONT oldFontMem = SELECT_UI_FONT(memDC);

            HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
            FillRect(memDC, &rc, bg);
            DeleteObject(bg);

            int t = g_PanWidthWin.trackIdx;
            float pan = (t >= 0 && t < g_Seq.trackCount) ? g_Seq.trackPan[t] : 0.0f;
            float width = (t >= 0 && t < g_Seq.trackCount) ? g_Seq.trackWidth[t] : 1.0f;

            int trackLeft = 24, trackRight = w - 24, trackW = trackRight - trackLeft;

            HGDIOBJ oldPPen = GetCurrentObject(memDC, OBJ_PEN);
            HGDIOBJ oldPBrush = GetCurrentObject(memDC, OBJ_BRUSH);

             
            int panY = 50;
            HPEN railPen = CreatePen(PS_SOLID, 4, RGB(28, 33, 42));
            SelectObject(memDC, railPen);
            MoveToEx(memDC, trackLeft, panY, NULL); LineTo(memDC, trackRight, panY);
            SelectObject(memDC, oldPPen);
            DeleteObject(railPen);

            int centerPanX = trackLeft + trackW / 2;
            HPEN notchPen = CreatePen(PS_SOLID, 2, RGB(60, 72, 90));
            SelectObject(memDC, notchPen);
            MoveToEx(memDC, centerPanX, panY - 6, NULL); LineTo(memDC, centerPanX, panY + 7);
            SelectObject(memDC, oldPPen);
            DeleteObject(notchPen);

            float panNorm = (pan + 1.0f) * 0.5f;
            if (panNorm < 0.0f) panNorm = 0.0f;
            if (panNorm > 1.0f) panNorm = 1.0f;
            int panThumbX = trackLeft + (int)(panNorm * (float)trackW);

            HPEN fillPen1 = CreatePen(PS_SOLID, 4, RGB(80, 210, 240));
            SelectObject(memDC, fillPen1);
            MoveToEx(memDC, centerPanX, panY, NULL); LineTo(memDC, panThumbX, panY);
            SelectObject(memDC, oldPPen);
            DeleteObject(fillPen1);

            HBRUSH thumbB1 = CreateSolidBrush(RGB(80, 240, 180));
            HPEN thumbP1 = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
            SelectObject(memDC, thumbB1); SelectObject(memDC, thumbP1);
            draw_aa_circle(memDC, panThumbX, panY, 6.5f, RGB(80, 240, 180), RGB(255, 255, 255), 1.8f); 
            SelectObject(memDC, oldPPen); SelectObject(memDC, oldPBrush);
            DeleteObject(thumbP1); DeleteObject(thumbB1);

            SetBkMode(memDC, TRANSPARENT);
            char panTxt[64];
            if (fabsf(pan) < 0.01f) snprintf(panTxt, sizeof(panTxt), "PAN: CENTER (0%%)");
            else if (pan < 0.0f) snprintf(panTxt, sizeof(panTxt), "PAN: LEFT %d%%", (int)(-pan * 100.0f + 0.5f));
            else snprintf(panTxt, sizeof(panTxt), "PAN: RIGHT %d%%", (int)(pan * 100.0f + 0.5f));
            SetTextColor(memDC, RGB(180, 220, 245));
            RECT pRc = { 0, 16, w, 36 };
            DrawTextA(memDC, panTxt, -1, &pRc, DT_CENTER | DT_SINGLELINE);

             
            int widthY = 110;
            HPEN railPen2 = CreatePen(PS_SOLID, 4, RGB(28, 33, 42));
            SelectObject(memDC, railPen2);
            MoveToEx(memDC, trackLeft, widthY, NULL); LineTo(memDC, trackRight, widthY);
            SelectObject(memDC, oldPPen);
            DeleteObject(railPen2);

            int normWidthX = trackLeft + (int)(0.5f * (float)trackW);
            HPEN notchPen2 = CreatePen(PS_SOLID, 2, RGB(60, 72, 90));
            SelectObject(memDC, notchPen2);
            MoveToEx(memDC, normWidthX, widthY - 6, NULL); LineTo(memDC, normWidthX, widthY + 7);
            SelectObject(memDC, oldPPen);
            DeleteObject(notchPen2);

            float widthNorm = width * 0.5f;
            if (widthNorm < 0.0f) widthNorm = 0.0f;
            if (widthNorm > 1.0f) widthNorm = 1.0f;
            int widthThumbX = trackLeft + (int)(widthNorm * (float)trackW);

            HPEN fillPen2 = CreatePen(PS_SOLID, 4, RGB(255, 185, 80));
            SelectObject(memDC, fillPen2);
            MoveToEx(memDC, trackLeft, widthY, NULL); LineTo(memDC, widthThumbX, widthY);
            SelectObject(memDC, oldPPen);
            DeleteObject(fillPen2);

            HBRUSH thumbB2 = CreateSolidBrush(RGB(255, 205, 110));
            HPEN thumbP2 = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
            SelectObject(memDC, thumbB2); SelectObject(memDC, thumbP2);
            draw_aa_circle(memDC, widthThumbX, widthY, 6.5f, RGB(255, 205, 110), RGB(255, 255, 255), 1.8f); 
            SelectObject(memDC, oldPPen); SelectObject(memDC, oldPBrush);
            DeleteObject(thumbP2); DeleteObject(thumbB2);

            char widthTxt[64];
            snprintf(widthTxt, sizeof(widthTxt), "STEREO WIDTH: %d%%", (int)(width * 100.0f + 0.5f));
            SetTextColor(memDC, RGB(255, 210, 140));
            RECT wRc = { 0, 76, w, 96 };
            DrawTextA(memDC, widthTxt, -1, &wRc, DT_CENTER | DT_SINGLELINE);

            SetTextColor(memDC, RGB(140, 155, 175));
            RECT hintRc = { 0, h - 24, w, h - 4 };
            DrawTextA(memDC, "Right-Click slider to reset | [ESC] to close", -1, &hintRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldFontMem);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            SelectObject(hdc, oldFontMain);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE || wParam == VK_RETURN) ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static inline void open_track_pan_width_dialog(HWND parentHwnd, int trackIdx) {
    if (trackIdx < 0 || trackIdx >= g_Seq.trackCount) return;
    if (!g_PanWidthWin.hwnd) {
        static bool s_registered = false;
        if (!s_registered) {
            WNDCLASSA wc = { 0 };
            wc.lpfnWndProc = PanWidthWndProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractPanWidthClass";
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_registered = true;
        }

int rw = 420, rh = 200;
        int scrW = GetSystemMetrics(SM_CXSCREEN), scrH = GetSystemMetrics(SM_CYSCREEN);
        int rx = (scrW - rw) / 2, ry = (scrH - rh) / 2;

        g_PanWidthWin.hwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            "RefractPanWidthClass", "Track Pan & Width",
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE,
            rx, ry, rw, rh,
            parentHwnd, NULL, GetModuleHandle(NULL), NULL
        );
    }

    g_PanWidthWin.trackIdx = trackIdx;
    char titleBuf[64];
    snprintf(titleBuf, sizeof(titleBuf), "Track %d - Pan & Width", trackIdx + 1);
    SetWindowTextA(g_PanWidthWin.hwnd, titleBuf);

    ShowWindow(g_PanWidthWin.hwnd, SW_SHOW);
    SetForegroundWindow(g_PanWidthWin.hwnd);
    InvalidateRect(g_PanWidthWin.hwnd, NULL, FALSE);
}

 

typedef struct {
    HWND hwnd;
    int  dragSlider;    
} LofiWindowContext;

static LofiWindowContext g_LofiWin = { 0 };

 
static const int kLofiBitSteps[4] = { 8, 9, 10, 12 };

static inline int lofi_bit_to_step(int bits) {
    for (int i = 0; i < 4; ++i)
        if (kLofiBitSteps[i] == bits) return i;
    return 3;  
}

static LRESULT CALLBACK LofiWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            RECT rc; GetClientRect(hwnd, &rc);
            int trackLeft = 24, trackRight = rc.right - 24, trackW = trackRight - trackLeft;
            if (trackW <= 0) return 0;

            if (my >= 38 && my <= 68 && mx >= trackLeft && mx <= trackRight) {
                 
                float norm = (float)(mx - trackLeft) / (float)trackW;
                int step = (int)(norm * 3.0f + 0.5f);
                if (step < 0) step = 0;
                if (step > 3) step = 3;
                g_Seq.lofiBitDepth = kLofiBitSteps[step];
                g_LofiWin.dragSlider = 1;
                SetCapture(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            else if (my >= 100 && my <= 130 && mx >= trackLeft && mx <= trackRight) {
                float norm = (float)(mx - trackLeft) / (float)trackW;
                if (norm < 0.0f) norm = 0.0f;
                if (norm > 1.0f) norm = 1.0f;
                g_Seq.lofiSampleRate = 8000.0f + norm * (32000.0f - 8000.0f);
                g_LofiWin.dragSlider = 2;
                SetCapture(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);

            RECT rc;
            GetClientRect(hwnd, &rc);
            int trackLeft = 24, trackRight = rc.right - 24;

            if (pt.x < trackLeft || pt.x > trackRight) return 0;

            short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            int steps = (zDelta > 0) ? 1 : -1;

            if (pt.y >= 38 && pt.y <= 68) {
                
                int curStep = lofi_bit_to_step(g_Seq.lofiBitDepth);
                int newStep = curStep + steps;
                if (newStep < 0) newStep = 0;
                if (newStep > 3) newStep = 3;
                g_Seq.lofiBitDepth = kLofiBitSteps[newStep];
                InvalidateRect(hwnd, NULL, FALSE);
            }
            else if (pt.y >= 100 && pt.y <= 130) {
                
                float newRate = g_Seq.lofiSampleRate + steps * 1000.0f;
                if (newRate < 8000.0f) newRate = 8000.0f;
                if (newRate > 32000.0f) newRate = 32000.0f;
                g_Seq.lofiSampleRate = newRate;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (g_LofiWin.dragSlider == 0) return 0;
            if (!(wParam & MK_LBUTTON)) {
                g_LofiWin.dragSlider = 0;
                ReleaseCapture();
                return 0;
            }
            int mx = GET_X_LPARAM(lParam);
            RECT rc; GetClientRect(hwnd, &rc);
            int trackLeft = 24, trackRight = rc.right - 24, trackW = trackRight - trackLeft;
            if (trackW <= 0) return 0;

            float norm = (float)(mx - trackLeft) / (float)trackW;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;

            if (g_LofiWin.dragSlider == 1) {
                int step = (int)(norm * 3.0f + 0.5f);
                if (step < 0) step = 0;
                if (step > 3) step = 3;
                g_Seq.lofiBitDepth = kLofiBitSteps[step];
            }
            else if (g_LofiWin.dragSlider == 2) {
                g_Seq.lofiSampleRate = 8000.0f + norm * (32000.0f - 8000.0f);
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_LBUTTONUP:
            if (g_LofiWin.dragSlider) {
                g_LofiWin.dragSlider = 0;
                ReleaseCapture();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_RBUTTONDOWN: {
             
            int my = GET_Y_LPARAM(lParam);
            if (my >= 38 && my <= 68) {
                g_Seq.lofiBitDepth = 12;
            } else if (my >= 100 && my <= 130) {
                g_Seq.lofiSampleRate = 14700.0f;
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HFONT oldFontMain = SELECT_UI_FONT(hdc);

            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left, h = rc.bottom - rc.top;

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
            HFONT oldFontMem = SELECT_UI_FONT(memDC);

            HBRUSH bg = CreateSolidBrush(RGB(18, 16, 26));    
            FillRect(memDC, &rc, bg);
            DeleteObject(bg);

            SetBkMode(memDC, TRANSPARENT);
            int trackLeft = 24, trackRight = w - 24, trackW = trackRight - trackLeft;

             
            int bitY = 52;
            HPEN railPen = CreatePen(PS_SOLID, 4, RGB(40, 36, 55));
            HGDIOBJ oldP = SelectObject(memDC, railPen);
            MoveToEx(memDC, trackLeft, bitY, NULL); LineTo(memDC, trackRight, bitY);
            SelectObject(memDC, oldP);
            DeleteObject(railPen);

             
            HPEN notchPen = CreatePen(PS_SOLID, 2, RGB(70, 60, 95));
            SelectObject(memDC, notchPen);
            for (int i = 0; i < 4; ++i) {
                int nx = trackLeft + (int)((float)i / 3.0f * (float)trackW);
                MoveToEx(memDC, nx, bitY - 6, NULL); LineTo(memDC, nx, bitY + 6);
            }
            SelectObject(memDC, oldP);
            DeleteObject(notchPen);

            int step = lofi_bit_to_step(g_Seq.lofiBitDepth);
            float bitNorm = (float)step / 3.0f;
            int bitThumbX = trackLeft + (int)(bitNorm * (float)trackW);

            HPEN fillPen = CreatePen(PS_SOLID, 4, RGB(160, 90, 255));
            SelectObject(memDC, fillPen);
            MoveToEx(memDC, trackLeft, bitY, NULL); LineTo(memDC, bitThumbX, bitY);
            SelectObject(memDC, oldP);
            DeleteObject(fillPen);

            draw_aa_circle(memDC, bitThumbX, bitY, 6.5f, RGB(190, 130, 255), RGB(255, 255, 255), 1.8f);

            char bitTxt[64];
            snprintf(bitTxt, sizeof(bitTxt), "BIT DEPTH: %d-bit", g_Seq.lofiBitDepth);
            SetTextColor(memDC, RGB(200, 160, 255));
            RECT bRc = { 0, 20, w, 40 };
            DrawTextA(memDC, bitTxt, -1, &bRc, DT_CENTER | DT_SINGLELINE);

             
            int rateY = 115;
            railPen = CreatePen(PS_SOLID, 4, RGB(40, 36, 55));
            SelectObject(memDC, railPen);
            MoveToEx(memDC, trackLeft, rateY, NULL); LineTo(memDC, trackRight, rateY);
            SelectObject(memDC, oldP);
            DeleteObject(railPen);

            float rateNorm = (g_Seq.lofiSampleRate - 8000.0f) / (32000.0f - 8000.0f);
            if (rateNorm < 0.0f) rateNorm = 0.0f;
            if (rateNorm > 1.0f) rateNorm = 1.0f;
            int rateThumbX = trackLeft + (int)(rateNorm * (float)trackW);

            fillPen = CreatePen(PS_SOLID, 4, RGB(80, 210, 220));
            SelectObject(memDC, fillPen);
            MoveToEx(memDC, trackLeft, rateY, NULL); LineTo(memDC, rateThumbX, rateY);
            SelectObject(memDC, oldP);
            DeleteObject(fillPen);

            draw_aa_circle(memDC, rateThumbX, rateY, 6.5f, RGB(120, 230, 240), RGB(255, 255, 255), 1.8f);

            char rateTxt[64];
            snprintf(rateTxt, sizeof(rateTxt), "SAMPLE RATE: %.0f Hz", g_Seq.lofiSampleRate);
            SetTextColor(memDC, RGB(140, 230, 240));
            RECT rRc = { 0, 82, w, 102 };
            DrawTextA(memDC, rateTxt, -1, &rRc, DT_CENTER | DT_SINGLELINE);

            SetTextColor(memDC, RGB(140, 155, 175));
            RECT hintRc = { 0, h - 24, w, h - 4 };
            DrawTextA(memDC, "Right-Click slider to reset | [ESC] to close", -1, &hintRc, DT_CENTER | DT_SINGLELINE);

            SelectObject(memDC, oldP);
            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldFontMem);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            SelectObject(hdc, oldFontMain);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE || wParam == VK_RETURN)
                ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            g_LofiWin.hwnd = NULL;
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static inline void open_lofi_dialog(HWND parentHwnd) {
    if (!g_LofiWin.hwnd) {
        static bool s_registered = false;
        if (!s_registered) {
            WNDCLASSA wc = { 0 };
            wc.lpfnWndProc   = LofiWndProc;
            wc.hInstance     = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractLofiClass";
            wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_registered = true;
        }

        
        int rw = 350, rh = 200;
        int scrW = GetSystemMetrics(SM_CXSCREEN);
        int scrH = GetSystemMetrics(SM_CYSCREEN);
        int rx = (scrW - rw) / 2;
        int ry = (scrH - rh) / 2;

        g_LofiWin.hwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            "RefractLofiClass",
            "Lo-Fi Settings",
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE,
            rx, ry, rw, rh,
            parentHwnd, NULL, GetModuleHandle(NULL), NULL
        );
    }

    ShowWindow(g_LofiWin.hwnd, SW_SHOW);
    SetForegroundWindow(g_LofiWin.hwnd);
    InvalidateRect(g_LofiWin.hwnd, NULL, FALSE);
}

 

typedef struct {
    HWND hwnd;
    int clipIdx;
    int trackIdx;
    bool isDragging;
} RateWindowContext;

static RateWindowContext g_RateWin = { 0 };


static inline float get_custom_rate_value(void) {
    seq_lock();
    if (g_RateWin.clipIdx >= 0 && g_RateWin.clipIdx < g_Seq.clipCount) {
        float r = g_Seq.clips[g_RateWin.clipIdx].playbackRate;
        seq_unlock();
        return r;
    }
    if (g_RateWin.trackIdx >= 0 && g_RateWin.trackIdx < g_Seq.trackCount) {
        bool hasSelectionOnTrack = false;
        for (int i = 0; i < g_Seq.clipCount; ++i) {
            if (g_Seq.clips[i].track == g_RateWin.trackIdx && g_Seq.clips[i].isSelected) {
                hasSelectionOnTrack = true;
                break;
            }
        }
        for (int i = 0; i < g_Seq.clipCount; ++i) {
            if (g_Seq.clips[i].track != g_RateWin.trackIdx) continue;
            if (hasSelectionOnTrack && !g_Seq.clips[i].isSelected) continue;
            float r = g_Seq.clips[i].playbackRate;
            seq_unlock();
            return r;
        }
    }
    seq_unlock();
    return 1.0f;
}

static inline void set_custom_rate_value(float rate) {
    if (rate < 0.01f) rate = 0.01f;
    if (rate > 2.00f) rate = 2.00f;

    seq_lock();
    if (g_RateWin.clipIdx >= 0 && g_RateWin.clipIdx < g_Seq.clipCount) {
        if (g_Seq.clips[g_RateWin.clipIdx].isSelected) {
            for (int i = 0; i < g_Seq.clipCount; ++i) {
                if (g_Seq.clips[i].isSelected) {
                    g_Seq.clips[i].playbackRate = rate;
                    mark_clip_bars_dirty(&g_Seq.clips[i]);
                }
            }
        }
        else {
            g_Seq.clips[g_RateWin.clipIdx].playbackRate = rate;
            mark_clip_bars_dirty(&g_Seq.clips[g_RateWin.clipIdx]);
        }
    }
    else if (g_RateWin.trackIdx >= 0 && g_RateWin.trackIdx < g_Seq.trackCount) {
        bool hasSelectionOnTrack = false;
        for (int i = 0; i < g_Seq.clipCount; ++i) {
            if (g_Seq.clips[i].track == g_RateWin.trackIdx && g_Seq.clips[i].isSelected) {
                hasSelectionOnTrack = true;
                break;
            }
        }
        for (int i = 0; i < g_Seq.clipCount; ++i) {
            if (g_Seq.clips[i].track != g_RateWin.trackIdx) continue;
            if (hasSelectionOnTrack && !g_Seq.clips[i].isSelected) continue;
            g_Seq.clips[i].playbackRate = rate;
            mark_clip_bars_dirty(&g_Seq.clips[i]);
        }
    }
    seq_unlock();
}

static LRESULT CALLBACK RateWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_LBUTTONDOWN: {
        
        bool hasClips = false;
        if (g_RateWin.clipIdx >= 0 && g_RateWin.clipIdx < g_Seq.clipCount) {
            hasClips = true;
        } else if (g_RateWin.trackIdx >= 0 && g_RateWin.trackIdx < g_Seq.trackCount) {
            seq_lock();
            for (int i = 0; i < g_Seq.clipCount; ++i) {
                if (g_Seq.clips[i].track == g_RateWin.trackIdx) {
                    hasClips = true;
                    break;
                }
            }
            seq_unlock();
        }
        if (!hasClips) return 0;  

        int mx = GET_X_LPARAM(lParam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int trackLeft = 24, trackRight = rc.right - 24;
        int trackW = trackRight - trackLeft;

        if (mx >= trackLeft && mx <= trackRight && trackW > 0) {
            float norm = (float)(mx - trackLeft) / (float)trackW;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;

            float rate = 0.01f + norm * (2.00f - 0.01f);
            set_custom_rate_value(rate);
            g_RateWin.isDragging = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (g_RateWin.isDragging) {
            
            int mx = GET_X_LPARAM(lParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int trackLeft = 24, trackRight = rc.right - 24;
            int trackW = trackRight - trackLeft;
            if (trackW > 0) {
                float norm = (float)(mx - trackLeft) / (float)trackW;
                if (norm < 0.0f) norm = 0.0f;
                if (norm > 1.0f) norm = 1.0f;
                float rate = 0.01f + norm * (2.00f - 0.01f);
                set_custom_rate_value(rate);
                InvalidateRect(hwnd, NULL, FALSE);
                if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        
        bool hasClips = false;
        if (g_RateWin.clipIdx >= 0 && g_RateWin.clipIdx < g_Seq.clipCount) {
            hasClips = true;
        } else if (g_RateWin.trackIdx >= 0 && g_RateWin.trackIdx < g_Seq.trackCount) {
            seq_lock();
            for (int i = 0; i < g_Seq.clipCount; ++i) {
                if (g_Seq.clips[i].track == g_RateWin.trackIdx) {
                    hasClips = true;
                    break;
                }
            }
            seq_unlock();
        }
        if (!hasClips) return 0;

        short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        float currentRate = get_custom_rate_value();
        float delta = (zDelta > 0) ? 0.05f : -0.05f;
        float newRate = currentRate + delta;
        if (newRate < 0.01f) newRate = 0.01f;
        if (newRate > 2.00f) newRate = 2.00f;
        set_custom_rate_value(newRate);

        if (g_hWnd) {
            snprintf(g_Seq.exportMsg, sizeof(g_Seq.exportMsg), "Rate: %.2fx", newRate);
            g_Seq.exportMsgActive = true;
            g_Seq.exportMsgExpiry = GetTickCount64() + 1500;
            InvalidateRect(g_hWnd, NULL, FALSE);
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_RateWin.isDragging) {
            g_RateWin.isDragging = false;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
            if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_RBUTTONDOWN: {
        
        bool hasClips = false;
        if (g_RateWin.clipIdx >= 0 && g_RateWin.clipIdx < g_Seq.clipCount) {
            hasClips = true;
        } else if (g_RateWin.trackIdx >= 0 && g_RateWin.trackIdx < g_Seq.trackCount) {
            seq_lock();
            for (int i = 0; i < g_Seq.clipCount; ++i) {
                if (g_Seq.clips[i].track == g_RateWin.trackIdx) {
                    hasClips = true;
                    break;
                }
            }
            seq_unlock();
        }
        if (!hasClips) return 0;

        set_custom_rate_value(1.0f);
        InvalidateRect(hwnd, NULL, FALSE);
        if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE || wParam == VK_RETURN) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HFONT oldFontMain = SELECT_UI_FONT(hdc);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0) {
            SelectObject(hdc, oldFontMain);
            EndPaint(hwnd, &ps);
            return 0;
        }

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
        HFONT oldFontMem = SELECT_UI_FONT(memDC);
        HGDIOBJ origPen = GetCurrentObject(memDC, OBJ_PEN);
        HGDIOBJ origBrush = GetCurrentObject(memDC, OBJ_BRUSH);

        HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
        FillRect(memDC, &rc, bg);
        DeleteObject(bg);

        bool hasClips = false;
        if (g_RateWin.clipIdx >= 0 && g_RateWin.clipIdx < g_Seq.clipCount) {
            hasClips = true;
        } else if (g_RateWin.trackIdx >= 0 && g_RateWin.trackIdx < g_Seq.trackCount) {
            seq_lock();
            for (int i = 0; i < g_Seq.clipCount; ++i) {
                if (g_Seq.clips[i].track == g_RateWin.trackIdx) {
                    hasClips = true;
                    break;
                }
            }
            seq_unlock();
        }

        int trackLeft = 24, trackRight = w - 24;
        int trackY = 56;
        int trackW = trackRight - trackLeft;

        
        HPEN railPen = CreatePen(PS_SOLID, 4, RGB(28, 33, 42));
        SelectObject(memDC, railPen);
        MoveToEx(memDC, trackLeft, trackY, NULL);
        LineTo(memDC, trackRight, trackY);
        SelectObject(memDC, origPen);
        DeleteObject(railPen);

        
        int centerNormX = trackLeft + (int)(((1.0f - 0.01f) / (2.00f - 0.01f)) * (float)trackW);
        HPEN notchPen = CreatePen(PS_SOLID, 2, RGB(60, 72, 90));
        SelectObject(memDC, notchPen);
        MoveToEx(memDC, centerNormX, trackY - 6, NULL);
        LineTo(memDC, centerNormX, trackY + 7);
        SelectObject(memDC, origPen);
        DeleteObject(notchPen);

        float currentRate = hasClips ? get_custom_rate_value() : 1.0f;
        float norm = (currentRate - 0.01f) / (2.00f - 0.01f);
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        int thumbX = trackLeft + (int)(norm * (float)trackW);

        
        COLORREF fillColor = hasClips ? RGB(80, 210, 240) : RGB(60, 70, 80);
        HPEN fillPen = CreatePen(PS_SOLID, 4, fillColor);
        SelectObject(memDC, fillPen);
        MoveToEx(memDC, trackLeft, trackY, NULL);
        LineTo(memDC, thumbX, trackY);
        SelectObject(memDC, origPen);
        DeleteObject(fillPen);

        
        COLORREF thumbColor = hasClips ? RGB(80, 240, 180) : RGB(80, 90, 100);
        HBRUSH thumbBrush = CreateSolidBrush(thumbColor);
        HPEN thumbBorder = CreatePen(PS_SOLID, 2, hasClips ? RGB(255, 255, 255) : RGB(100, 110, 120));
        SelectObject(memDC, thumbBrush);
        SelectObject(memDC, thumbBorder);
        draw_aa_circle(memDC, thumbX, trackY, 6.5f, thumbColor, hasClips ? RGB(255, 255, 255) : RGB(100, 110, 120), 1.8f);
        SelectObject(memDC, origPen);
        SelectObject(memDC, origBrush);
        DeleteObject(thumbBorder);
        DeleteObject(thumbBrush);

        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, hasClips ? RGB(215, 225, 240) : RGB(100, 110, 120));
        char valBuf[32];
        if (hasClips) {
            snprintf(valBuf, sizeof(valBuf), "%.2fx", currentRate);
        } else {
            snprintf(valBuf, sizeof(valBuf), "No clips");
        }
        RECT valRc = { 0, 12, w, 34 };
        DrawTextA(memDC, valBuf, -1, &valRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SetTextColor(memDC, hasClips ? RGB(80, 95, 115) : RGB(60, 70, 80));
        TextOutA(memDC, trackLeft, trackY + 12, "0.01x", 5);
        TextOutA(memDC, centerNormX - 14, trackY + 12, "1.00x", 5);
        TextOutA(memDC, trackRight - 28, trackY + 12, "2.00x", 5);

        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldFontMem);
        SelectObject(memDC, origPen);
        SelectObject(memDC, origBrush);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        SelectObject(hdc, oldFontMain);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static inline void open_custom_rate_dialog(HWND parentHwnd, int clipIdx, int trackIdx) {
    if (!g_RateWin.hwnd) {
        static bool s_rateRegistered = false;
        if (!s_rateRegistered) {
            WNDCLASSA wc = { 0 };
            wc.lpfnWndProc = RateWndProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractRateWindowClass";
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_rateRegistered = true;
        }

        int rw = 420, rh = 150;
        int scrW = GetSystemMetrics(SM_CXSCREEN);
        int scrH = GetSystemMetrics(SM_CYSCREEN);
        int rx = (scrW - rw) / 2;
        int ry = (scrH - rh) / 2;

        g_RateWin.hwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            "RefractRateWindowClass",
            "Custom Playback Rate",
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE,
            rx, ry, rw, rh,
            parentHwnd, NULL, GetModuleHandle(NULL), NULL
        );
    }

    g_RateWin.clipIdx = clipIdx;
    g_RateWin.trackIdx = trackIdx;

    char titleBuf[64];
    if (clipIdx >= 0) {
        snprintf(titleBuf, sizeof(titleBuf), "Clip Playback Rate");
    }
    else {
        snprintf(titleBuf, sizeof(titleBuf), "Track %d - Playback Rate", trackIdx + 1);
    }
    SetWindowTextA(g_RateWin.hwnd, titleBuf);

    ShowWindow(g_RateWin.hwnd, SW_SHOW);
    SetForegroundWindow(g_RateWin.hwnd);
    InvalidateRect(g_RateWin.hwnd, NULL, FALSE);
}


 

static int  g_eqTrack = 0;
static HWND g_eqHwnd = NULL;

static inline float get_eq_band_freq_hz(int trackIdx, int band) {
    if (trackIdx < 0 || trackIdx >= MAX_TRACKS || band < 0 || band >= 3) return 1000.0f;
    float param = g_Seq.trackEqFreq[trackIdx][band];
    if (param < 0.0f) param = 0.0f;
    if (param > 1.0f) param = 1.0f;
    return 20.0f * powf(1000.0f, param);
}

static inline float get_eq_band_q_factor(int trackIdx, int band) {
    if (trackIdx < 0 || trackIdx >= MAX_TRACKS || band < 0 || band >= 3) return 0.7f;
    float q = 0.35f + g_Seq.trackEqQ[trackIdx][band] * 4.65f;
    if (q < 0.20f) q = 0.20f;
    if (q > 8.00f) q = 8.00f;
    return q;
}

static inline void update_track_eq_params(int trackIdx) {
    if (trackIdx < 0 || trackIdx >= g_Seq.trackCount || trackIdx >= MAX_TRACKS) return;

    seq_lock();
    smooth_eq3_set_params(&g_Seq.trackEQ[trackIdx],
        g_Seq.trackEqHigh[trackIdx],
        g_Seq.trackEqMid[trackIdx],
        g_Seq.trackEqLow[trackIdx]);

    float gains[3] = {
        g_Seq.trackEqLow[trackIdx],
        g_Seq.trackEqMid[trackIdx],
        g_Seq.trackEqHigh[trackIdx]
    };

    for (int b = 0; b < 3; ++b) {
        float f = get_eq_band_freq_hz(trackIdx, b);
        float q = get_eq_band_q_factor(trackIdx, b);
        float gainDb = (gains[b] - 0.5f) * 24.0f;
        peak_biquad_set(&g_Seq.trackPeak[trackIdx][b], f, q, gainDb, (float)SAMPLE_RATE);
    }

    g_Seq.trackEqActive[trackIdx] = true;
    seq_unlock();
}

// --- Decoupled EQ curve rendering -------------------------------------------
// The supersampled response-curve rasterization (DIB allocation, ~500 expf
// evaluations, Polygon fill, 2x box-filter downsample) runs on a background
// worker thread; the UI thread only composites the published surface with a
// single AlphaBlend inside WM_PAINT. This keeps band drags at full mouse-
// polling rate without stalling the shared main thread (timeline, meters,
// visualizer all live there).

typedef struct {
    int   trackIdx;
    int   graphL, graphR, graphT, graphB;
    float gains[3];
    float freqs[3];
    float q[3];
} EqRenderJob;

static EqRenderJob   g_EqJob = { 0 };
static HANDLE        g_hEqWorkerThread = NULL;
static HANDLE        g_hEqWorkerEvent  = NULL;
static volatile LONG g_eqWorkerRunning = 0;

// Front surface the UI thread reads in WM_PAINT (guarded by g_eqCurveLock).
static HDC              g_eqCurveDC       = NULL;
static HBITMAP          g_eqCurveBmp      = NULL;
static HBITMAP          g_eqCurveOldBmp   = NULL;
static int              g_eqCurveW        = 0;
static int              g_eqCurveH        = 0;
static CRITICAL_SECTION g_eqCurveLock;
static bool             g_eqCurveLockInit = false;

static inline void eq_init_lock(void) {
    if (!g_eqCurveLockInit) {
        // Spin count is advisory; failure to pre-allocate the spin event is
        // non-fatal, the lock still works.
        (void)InitializeCriticalSectionAndSpinCount(&g_eqCurveLock, 4000);
        g_eqCurveLockInit = true;
    }
}

// Snapshot the current EQ params + graph geometry into the shared job, then
// wake the worker. Cheap (a struct copy) and safe to call at mouse-polling
// rate: the worker renders at most one job at a time and coalesces drags
// via the wake event.
static inline void request_eq_curve_render(HWND hwnd) {
    if (!g_hEqWorkerEvent) return;

    RECT rc;
    if (!GetClientRect(hwnd, &rc)) return;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    eq_init_lock();
    EnterCriticalSection(&g_eqCurveLock);
    g_EqJob.trackIdx = g_eqTrack;
    g_EqJob.graphL   = 52;
    g_EqJob.graphR   = w - 16;
    g_EqJob.graphT   = 16;
    g_EqJob.graphB   = h - 76;
    g_EqJob.gains[0] = g_Seq.trackEqLow[g_eqTrack];
    g_EqJob.gains[1] = g_Seq.trackEqMid[g_eqTrack];
    g_EqJob.gains[2] = g_Seq.trackEqHigh[g_eqTrack];
    for (int b = 0; b < 3; ++b) {
        g_EqJob.freqs[b] = g_Seq.trackEqFreq[g_eqTrack][b];
        g_EqJob.q[b]     = g_Seq.trackEqQ[g_eqTrack][b];
    }
    LeaveCriticalSection(&g_eqCurveLock);

    SetEvent(g_hEqWorkerEvent);
}

// Heavy curve rasterization, off the main thread. Scratch DIBs are created
// once and reused (only grown when the graph gets bigger), so steady-state
// drags do zero GDI allocations. Snapshot-then-clear ordering guarantees the
// last drag position always gets rendered.
static DWORD WINAPI EqCurveWorkerThreadProc(LPVOID lpParam) {
    (void)lpParam;

    HDC  scratchSSDC = NULL, scratchDownDC = NULL;
    HBITMAP scratchSSBmp = NULL, scratchDownBmp = NULL;
    HGDIOBJ scratchSSOld = NULL, scratchDownOld = NULL;
    DWORD *pSSBits = NULL, *pDownBits = NULL;
    int capSSW = 0, capSSH = 0, capDownW = 0, capDownH = 0;

    while (InterlockedCompareExchange(&g_eqWorkerRunning, 1, 1) == 1) {
        WaitForSingleObject(g_hEqWorkerEvent, INFINITE);
        if (InterlockedCompareExchange(&g_eqWorkerRunning, 1, 1) != 1) break;

        bool dirty = true;
        while (dirty && InterlockedCompareExchange(&g_eqWorkerRunning, 1, 1) == 1) {
            // Snapshot under the lock FIRST, then clear: if the UI thread
            // submits a newer job during our render, the event is already
            // set and we loop again with the fresh params.
            EqRenderJob job;
            eq_init_lock();
            EnterCriticalSection(&g_eqCurveLock);
            job = g_EqJob;
            LeaveCriticalSection(&g_eqCurveLock);
            dirty = false;

            int graphW = job.graphR - job.graphL;
            int graphH = job.graphB - job.graphT;
            if (graphW <= 0 || graphH <= 0) break;

            int ssW = graphW * 2;
            int ssH = graphH * 2;

            if (!scratchSSDC) {
                scratchSSDC = CreateCompatibleDC(NULL);
                scratchSSOld = GetCurrentObject(scratchSSDC, OBJ_BITMAP);
            }
            if (ssW > capSSW || ssH > capSSH || !scratchSSBmp) {
                if (scratchSSBmp) DeleteObject(scratchSSBmp);
                BITMAPINFO bmi = { 0 };
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = ssW;
                bmi.bmiHeader.biHeight = -ssH;
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                scratchSSBmp = CreateDIBSection(scratchSSDC, &bmi, DIB_RGB_COLORS, (void**)&pSSBits, NULL, 0);
                SelectObject(scratchSSDC, scratchSSBmp);
                capSSW = ssW; capSSH = ssH;
            }

            if (!scratchDownDC) {
                scratchDownDC = CreateCompatibleDC(NULL);
                scratchDownOld = GetCurrentObject(scratchDownDC, OBJ_BITMAP);
            }
            if (graphW > capDownW || graphH > capDownH || !scratchDownBmp) {
                if (scratchDownBmp) DeleteObject(scratchDownBmp);
                BITMAPINFO bmi = { 0 };
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = graphW;
                bmi.bmiHeader.biHeight = -graphH;
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                scratchDownBmp = CreateDIBSection(scratchDownDC, &bmi, DIB_RGB_COLORS, (void**)&pDownBits, NULL, 0);
                SelectObject(scratchDownDC, scratchDownBmp);
                capDownW = graphW; capDownH = graphH;
            }

            if (!pSSBits || !pDownBits) break;

            memset(pSSBits, 0, (size_t)ssW * (size_t)ssH * 4);

            int ssMidY = ssH / 2;
            float pxPerDb = (float)(graphH / 2 - 6) / 12.0f;
            float ssPxPerDb = pxPerDb * 2.0f;

            const COLORREF bandColors[3] = { RGB(255, 160, 60), RGB(90, 245, 180), RGB(135, 195, 255) };
            int nOrder[3] = { 0, 1, 2 };
            for (int i = 0; i < 2; ++i) {
                for (int j = i + 1; j < 3; ++j) {
                    if (job.freqs[nOrder[i]] > job.freqs[nOrder[j]]) {
                        int tmp = nOrder[i]; nOrder[i] = nOrder[j]; nOrder[j] = tmp;
                    }
                }
            }

            float stopX[3];
            COLORREF stopCol[3];
            for (int i = 0; i < 3; ++i) {
                int idx = nOrder[i];
                stopX[i] = job.freqs[idx] * (float)(ssW - 1);
                stopCol[i] = bandColors[idx];
            }

            // Build curve polygon (sum of three gaussians, 2px steps in SS space)
            POINT ssFillPts[1024];
            int ssPtCount = 0;
            ssFillPts[ssPtCount++] = (POINT){ 0, ssMidY };

            for (int x = 0; x < ssW; x += 2) {
                float normX = (float)x / (float)(ssW - 1);
                float totalDeltaY = 0.0f;

                for (int b = 0; b < 3; ++b) {
                    float bNormX = job.freqs[b];
                    float bGainPx = (job.gains[b] - 0.5f) * 24.0f * ssPxPerDb;
                    float bQ = 0.35f + job.q[b] * 4.65f;
                    float sigma = 0.18f / sqrtf(bQ);
                    float dist = (normX - bNormX);
                    totalDeltaY += bGainPx * expf(-(dist * dist) / (2.0f * sigma * sigma));
                }

                int cy = ssMidY - (int)(totalDeltaY + 0.5f);
                if (cy < 0) cy = 0;
                if (cy > ssH - 1) cy = ssH - 1;

                if (ssPtCount < 1020) ssFillPts[ssPtCount++] = (POINT){ x, cy };
            }
            ssFillPts[ssPtCount++] = (POINT){ ssW - 1, ssMidY };

            // Shaded polygon body
            HBRUSH ssFillBrush = CreateSolidBrush(RGB(18, 32, 50));
            HGDIOBJ oldSSBr = SelectObject(scratchSSDC, ssFillBrush);
            HGDIOBJ oldSSPen = SelectObject(scratchSSDC, GetStockObject(NULL_PEN));
            Polygon(scratchSSDC, ssFillPts, ssPtCount);
            SelectObject(scratchSSDC, oldSSPen);
            SelectObject(scratchSSDC, oldSSBr);
            DeleteObject(ssFillBrush);

            // Colored response stroke, gradient-blended between band centers
            POINT prevPt = ssFillPts[1];
            for (int i = 2; i < ssPtCount - 1; ++i) {
                POINT curPt = ssFillPts[i];
                float midX = (float)(prevPt.x + curPt.x) * 0.5f;

                COLORREF segCol;
                if (midX <= stopX[0]) segCol = stopCol[0];
                else if (midX >= stopX[2]) segCol = stopCol[2];
                else if (midX <= stopX[1]) {
                    float span = stopX[1] - stopX[0];
                    float f = (span > 0.001f) ? (midX - stopX[0]) / span : 0.0f;
                    segCol = RGB((BYTE)(GetRValue(stopCol[0]) + (GetRValue(stopCol[1]) - GetRValue(stopCol[0])) * f),
                                 (BYTE)(GetGValue(stopCol[0]) + (GetGValue(stopCol[1]) - GetGValue(stopCol[0])) * f),
                                 (BYTE)(GetBValue(stopCol[0]) + (GetBValue(stopCol[1]) - GetBValue(stopCol[0])) * f));
                } else {
                    float span = stopX[2] - stopX[1];
                    float f = (span > 0.001f) ? (midX - stopX[1]) / span : 0.0f;
                    segCol = RGB((BYTE)(GetRValue(stopCol[1]) + (GetRValue(stopCol[2]) - GetRValue(stopCol[1])) * f),
                                 (BYTE)(GetGValue(stopCol[1]) + (GetGValue(stopCol[2]) - GetGValue(stopCol[1])) * f),
                                 (BYTE)(GetBValue(stopCol[1]) + (GetBValue(stopCol[2]) - GetBValue(stopCol[1])) * f));
                }

                HPEN segPen = CreatePen(PS_SOLID, 4, segCol);
                HGDIOBJ oldP = SelectObject(scratchSSDC, segPen);
                MoveToEx(scratchSSDC, prevPt.x, prevPt.y, NULL);
                LineTo(scratchSSDC, curPt.x, curPt.y);
                SelectObject(scratchSSDC, oldP);
                DeleteObject(segPen);
                prevPt = curPt;
            }

            // Downsample 2x -> 1x box filter
            for (int y = 0; y < graphH; ++y) {
                for (int x = 0; x < graphW; ++x) {
                    DWORD p00 = pSSBits[(y * 2 + 0) * ssW + (x * 2 + 0)];
                    DWORD p01 = pSSBits[(y * 2 + 0) * ssW + (x * 2 + 1)];
                    DWORD p10 = pSSBits[(y * 2 + 1) * ssW + (x * 2 + 0)];
                    DWORD p11 = pSSBits[(y * 2 + 1) * ssW + (x * 2 + 1)];

                    int bSum = (p00 & 0xFF) + (p01 & 0xFF) + (p10 & 0xFF) + (p11 & 0xFF);
                    int gSum = ((p00 >> 8) & 0xFF) + ((p01 >> 8) & 0xFF) + ((p10 >> 8) & 0xFF) + ((p11 >> 8) & 0xFF);
                    int rSum = ((p00 >> 16) & 0xFF) + ((p01 >> 16) & 0xFF) + ((p10 >> 16) & 0xFF) + ((p11 >> 16) & 0xFF);
                    int cCount = ((p00 != 0) ? 1 : 0) + ((p01 != 0) ? 1 : 0) +
                                 ((p10 != 0) ? 1 : 0) + ((p11 != 0) ? 1 : 0);

                    if (cCount == 0) {
                        pDownBits[y * graphW + x] = 0;
                    } else {
                        float aF = (float)cCount * 0.25f;
                        BYTE a  = (BYTE)(aF * 255.0f + 0.5f);
                        BYTE pr = (BYTE)((float)rSum * 0.25f * aF + 0.5f);
                        BYTE pg = (BYTE)((float)gSum * 0.25f * aF + 0.5f);
                        BYTE pb = (BYTE)((float)bSum * 0.25f * aF + 0.5f);
                        pDownBits[y * graphW + x] = ((DWORD)a << 24) | ((DWORD)pr << 16) | ((DWORD)pg << 8) | (DWORD)pb;
                    }
                }
            }

            // Publish to the front surface under the lock
            eq_init_lock();
            EnterCriticalSection(&g_eqCurveLock);
            if (!g_eqCurveDC) {
                g_eqCurveDC = CreateCompatibleDC(NULL);
                g_eqCurveOldBmp = GetCurrentObject(g_eqCurveDC, OBJ_BITMAP);
            }
            if (graphW != g_eqCurveW || graphH != g_eqCurveH || !g_eqCurveBmp) {
                if (g_eqCurveBmp) DeleteObject(g_eqCurveBmp);
                g_eqCurveBmp = CreateCompatibleBitmap(scratchDownDC, graphW, graphH);
                SelectObject(g_eqCurveDC, g_eqCurveBmp);
                g_eqCurveW = graphW;
                g_eqCurveH = graphH;
            }
            BitBlt(g_eqCurveDC, 0, 0, graphW, graphH, scratchDownDC, 0, 0, SRCCOPY);
            LeaveCriticalSection(&g_eqCurveLock);

            // Nudge the EQ window: without this, a curve published between
            // paints (e.g. right after reopening the panel) stays invisible
            // until the next mousemove/resize forces a repaint.
            if (g_eqHwnd && IsWindow(g_eqHwnd)) {
                InvalidateRect(g_eqHwnd, NULL, FALSE);
            }

            // A newer job may have arrived while we rendered: if the wake
            // event is already set, loop immediately with the fresh params
            // instead of showing a stale frame. If it fires after this poll,
            // the outer WaitForSingleObject returns right away — no lost
            // wakeups either way.
            dirty = (WaitForSingleObject(g_hEqWorkerEvent, 0) == WAIT_OBJECT_0);
        }
    }

    if (scratchSSBmp) {
        SelectObject(scratchSSDC, scratchSSOld);
        DeleteObject(scratchSSBmp);
    }
    if (scratchDownBmp) {
        SelectObject(scratchDownDC, scratchDownOld);
        DeleteObject(scratchDownBmp);
    }
    if (scratchSSDC) DeleteDC(scratchSSDC);
    if (scratchDownDC) DeleteDC(scratchDownDC);
    return 0;
}

static LRESULT CALLBACK EqWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static int dragBand = -1;

    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

    case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HFONT oldFontMain = SELECT_UI_FONT(hdc);

            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            if (w <= 0 || h <= 0) {
                SelectObject(hdc, oldFontMain);
                EndPaint(hwnd, &ps);
                return 0;
            }

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
            HFONT oldFontMem = SELECT_UI_FONT(memDC);
            HGDIOBJ origPen = GetCurrentObject(memDC, OBJ_PEN);
            HGDIOBJ origBrush = GetCurrentObject(memDC, OBJ_BRUSH);

            HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
            FillRect(memDC, &rc, bg);
            DeleteObject(bg);

            
            int graphL = 52, graphR = w - 16, graphT = 16, graphB = h - 76;
            int graphW = graphR - graphL, graphH = graphB - graphT;
            int graphMidY = graphT + graphH / 2;
            float pxPerDb = (float)(graphH / 2 - 6) / 12.0f;

            int yPlus12 = graphMidY - (int)(12.0f * pxPerDb);
            int yMinus12 = graphMidY + (int)(12.0f * pxPerDb);

            HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(27, 32, 42));
            SelectObject(memDC, gridPen);
            MoveToEx(memDC, graphL, yPlus12, NULL);  LineTo(memDC, graphR, yPlus12);
            MoveToEx(memDC, graphL, graphMidY, NULL); LineTo(memDC, graphR, graphMidY);
            MoveToEx(memDC, graphL, yMinus12, NULL); LineTo(memDC, graphR, yMinus12);
            SelectObject(memDC, origPen);
            DeleteObject(gridPen);

            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, RGB(85, 96, 114));
            TextOutA(memDC, 10, yPlus12 - 7, "+12 dB", 6);
            TextOutA(memDC, 18, graphMidY - 7, "0", 1);
            TextOutA(memDC, 10, yMinus12 - 7, "-12 dB", 6);

            float gains[3] = {
                g_Seq.trackEqLow[g_eqTrack],
                g_Seq.trackEqMid[g_eqTrack],
                g_Seq.trackEqHigh[g_eqTrack]
            };

            const COLORREF bandColors[3] = { RGB(255, 160, 60), RGB(90, 245, 180), RGB(135, 195, 255) };
            const COLORREF dimDottedCol[3] = { RGB(140, 85, 40),  RGB(45, 125, 95),   RGB(55, 105, 145) };

            POINT nodePts[3];
            for (int i = 0; i < 3; ++i) {
                float normX = g_Seq.trackEqFreq[g_eqTrack][i];
                if (normX < 0.0f) normX = 0.0f;
                if (normX > 1.0f) normX = 1.0f;

                float gainDb = (gains[i] - 0.5f) * 24.0f;
                nodePts[i].x = graphL + (int)(normX * (float)graphW);
                nodePts[i].y = graphMidY - (int)(gainDb * pxPerDb);

                if (nodePts[i].y < graphT + 2) nodePts[i].y = graphT + 2;
                if (nodePts[i].y > graphB - 2) nodePts[i].y = graphB - 2;
            }

            
            for (int b = 0; b < 3; ++b) {
                float gainDb = (gains[b] - 0.5f) * 24.0f;
                if (fabsf(gainDb) < 0.9f) continue;

                float bNormX = g_Seq.trackEqFreq[g_eqTrack][b];
                float bGainPx = gainDb * pxPerDb;
                float bQ = get_eq_band_q_factor(g_eqTrack, b);
                float sigma = 0.18f / sqrtf(bQ);
                const float minDeviationPx = 2.2f;

                HPEN dotPen = CreatePen(PS_DOT, 1, dimDottedCol[b]);
                HGDIOBJ oldDotP = SelectObject(memDC, dotPen);
                bool started = false;

                int startX = max(graphL, (int)(graphL + (bNormX - 2.8f * sigma) * graphW));
                int endX   = min(graphR, (int)(graphL + (bNormX + 2.8f * sigma) * graphW));

                for (int x = startX; x <= endX; x += 2) {
                    float normX = (float)(x - graphL) / (float)graphW;
                    float dist = (normX - bNormX);
                    float val = bGainPx * expf(-(dist * dist) / (2.0f * sigma * sigma));

                    if (fabsf(val) < minDeviationPx) {
                        started = false;
                        continue;
                    }

                    int cy = graphMidY - (int)(val + (val > 0.0f ? 0.5f : -0.5f));
                    if (cy < graphT) cy = graphT;
                    if (cy > graphB) cy = graphB;

                    if (!started) {
                        MoveToEx(memDC, x, cy, NULL);
                        started = true;
                    } else {
                        LineTo(memDC, x, cy);
                    }
                }
                SelectObject(memDC, oldDotP);
                DeleteObject(dotPen);
            }


            // Composite the worker-rendered response curve (single AlphaBlend).
            // When the worker hasn't published yet (first paint after open or
            // resize), the dotted per-band guides drawn above remain as the
            // instant feedback layer.
            if (g_eqCurveDC && g_eqCurveW == graphW && g_eqCurveH == graphH) {
                BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                eq_init_lock();
                EnterCriticalSection(&g_eqCurveLock);
                if (g_eqCurveDC && g_eqCurveW == graphW && g_eqCurveH == graphH) {
                    AlphaBlend(memDC, graphL, graphT, graphW, graphH, g_eqCurveDC, 0, 0, graphW, graphH, bf);
                }
                LeaveCriticalSection(&g_eqCurveLock);
            }

             
            for (int i = 0; i < 3; ++i) {
                int nx = nodePts[i].x;
                int ny = nodePts[i].y;
                bool isDragged = (dragBand == i);
                COLORREF col = bandColors[i];
                float gainDb = (gains[i] - 0.5f) * 24.0f;
                float coreRadius = isDragged ? 9.5f : 8.5f;

                
                if (isDragged || fabsf(gainDb) >= 0.8f) {
                    int stalkStartY = graphMidY;
                    int stalkEndY = (ny > graphMidY) ? (ny - (int)coreRadius) : (ny + (int)coreRadius);
                    if (abs(stalkEndY - stalkStartY) >= 4) {
                        HPEN stalkPen = CreatePen(PS_DOT, 1, isDragged ? col : dimDottedCol[i]);
                        HGDIOBJ oldStalkP = SelectObject(memDC, stalkPen);
                        MoveToEx(memDC, nx, stalkStartY, NULL);
                        LineTo(memDC, nx, stalkEndY);
                        SelectObject(memDC, oldStalkP);
                        DeleteObject(stalkPen);
                    }
                }

                
                if (isDragged) {
                    draw_aa_circle(memDC, nx, ny, 14.0f, RGB(22, 32, 44), col, 1.5f);
                } else {
                    draw_aa_circle(memDC, nx, ny + 1, 10.5f, RGB(10, 12, 16), RGB(10, 12, 16), 0.0f);
                }

                
                draw_aa_circle(memDC, nx, ny, coreRadius, col, RGB(255, 255, 255), 2.0f);

                
                HFONT oldF = (HFONT)SelectObject(memDC, get_ui_small_font());
                SetBkMode(memDC, TRANSPARENT);
                SetTextColor(memDC, RGB(18, 22, 28)); 
                char numStr[2] = { (char)('1' + i), '\0' };
                RECT numRc = { nx - 10, ny - 9, nx + 10, ny + 10 };
                DrawTextA(memDC, numStr, -1, &numRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(memDC, oldF);

                
                if (isDragged) {
                    float curGainDb = gainDb;
                    float curF = get_eq_band_freq_hz(g_eqTrack, i);
                    char hudBuf[48];
                    if (curF >= 1000.0f)
                        snprintf(hudBuf, sizeof(hudBuf), "%.1fkHz  %+.1fdB", curF / 1000.0f, curGainDb);
                    else
                        snprintf(hudBuf, sizeof(hudBuf), "%.0fHz  %+.1fdB", curF, curGainDb);

                    int hudW = 104, hudH = 20;
                    int hudX = nx - hudW / 2;
                    int hudY = (ny - 30 >= graphT) ? (ny - 26) : (ny + 16);
                    if (hudX < graphL) hudX = graphL;
                    if (hudX + hudW > graphR) hudX = graphR - hudW;

                    RECT hudRc = { hudX, hudY, hudX + hudW, hudY + hudH };
                    HBRUSH hBg = CreateSolidBrush(RGB(15, 18, 24));
                    HPEN hBorder = CreatePen(PS_SOLID, 1, col);
                    HGDIOBJ ob = SelectObject(memDC, hBg);
                    HGDIOBJ op = SelectObject(memDC, hBorder);
                    RoundRect(memDC, hudRc.left, hudRc.top, hudRc.right, hudRc.bottom, 4, 4);
                    SelectObject(memDC, op);
                    SelectObject(memDC, ob);
                    DeleteObject(hBorder);
                    DeleteObject(hBg);

                    HFONT oldHF = (HFONT)SelectObject(memDC, get_ui_small_font());
                    SetTextColor(memDC, RGB(255, 255, 255));
                    DrawTextA(memDC, hudBuf, -1, &hudRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    SelectObject(memDC, oldHF);
                }
            }

            
            const char* bandNames[3] = { "LOW", "MID", "HIGH" };
            int colCenters[3] = { (int)(w * 0.22f), (int)(w * 0.50f), (int)(w * 0.78f) };

            for (int i = 0; i < 3; ++i) {
                float gainDb = (gains[i] - 0.5f) * 24.0f;
                char mainBuf[32], freqBuf[32], qBuf[32];
                snprintf(mainBuf, sizeof(mainBuf), "%s: %+.1fdB", bandNames[i], gainDb);

                float curF = get_eq_band_freq_hz(g_eqTrack, i);
                float curQ = get_eq_band_q_factor(g_eqTrack, i);
                if (curF >= 1000.0f)
                    snprintf(freqBuf, sizeof(freqBuf), "%.1fkHz", curF / 1000.0f);
                else
                    snprintf(freqBuf, sizeof(freqBuf), "%.0fHz", curF);
                snprintf(qBuf, sizeof(qBuf), "Q = %.2f", curQ);

                SetTextColor(memDC, bandColors[i]);
                RECT mainRc = { colCenters[i] - 65, h - 68, colCenters[i] + 65, h - 50 };
                DrawTextA(memDC, mainBuf, -1, &mainRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                SetTextColor(memDC, RGB(130, 145, 165));
                RECT freqRc = { colCenters[i] - 65, h - 48, colCenters[i] + 65, h - 30 };
                DrawTextA(memDC, freqBuf, -1, &freqRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                SetTextColor(memDC, RGB(210, 220, 235));
                RECT qRc = { colCenters[i] - 65, h - 28, colCenters[i] + 65, h - 10 };
                DrawTextA(memDC, qBuf, -1, &qRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }

            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldFontMem);
            SelectObject(memDC, origBrush);
            SelectObject(memDC, origPen);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            SelectObject(hdc, oldFontMain);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;

            
            int graphL = 52, graphR = w - 16, graphT = 16, graphB = h - 76;
            int graphW = graphR - graphL, graphH = graphB - graphT;
            int graphMidY = graphT + graphH / 2;
            float pxPerDb = (float)(graphH / 2 - 6) / 12.0f;

            float gains[3] = {
                g_Seq.trackEqLow[g_eqTrack],
                g_Seq.trackEqMid[g_eqTrack],
                g_Seq.trackEqHigh[g_eqTrack]
            };

            int bestBand = -1;
            float bestDistSq = 1e9f;

            for (int i = 0; i < 3; ++i) {
                float normX = g_Seq.trackEqFreq[g_eqTrack][i];
                int nx = graphL + (int)(normX * (float)graphW);
                int ny = graphMidY - (int)((gains[i] - 0.5f) * 24.0f * pxPerDb);

                float dx = (float)(mx - nx);
                float dy = (float)(my - ny);
                float dSq = dx * dx + dy * dy;

                if (dSq < bestDistSq) {
                    bestDistSq = dSq;
                    bestBand = i;
                }
            }

            if (bestBand >= 0 && (bestDistSq <= 600.0f || my < graphB)) {
                dragBand = bestBand;
            } else {
                if (mx < w * 0.35f) dragBand = 0;
                else if (mx < w * 0.65f) dragBand = 1;
                else dragBand = 2;
            }

            float* pGains[3] = { &g_Seq.trackEqLow[g_eqTrack], &g_Seq.trackEqMid[g_eqTrack], &g_Seq.trackEqHigh[g_eqTrack] };

            float newGain = 0.5f + (float)(graphMidY - my) / (24.0f * pxPerDb);
            if (newGain < 0.0f) newGain = 0.0f;
            if (newGain > 1.0f) newGain = 1.0f;
            *pGains[dragBand] = newGain;

            float newNormX = (float)(mx - graphL) / (float)graphW;
            if (newNormX < 0.0f) newNormX = 0.0f;
            if (newNormX > 1.0f) newNormX = 1.0f;
            g_Seq.trackEqFreq[g_eqTrack][dragBand] = newNormX;

            update_track_eq_params(g_eqTrack);
            request_eq_curve_render(hwnd);
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (dragBand >= 0 && dragBand <= 2) {
                int mx = GET_X_LPARAM(lParam);
                int my = GET_Y_LPARAM(lParam);

                RECT rc;
                GetClientRect(hwnd, &rc);
                int w = rc.right - rc.left;
                int h = rc.bottom - rc.top;

                
                int graphL = 52, graphR = w - 16, graphT = 16, graphB = h - 76;
                int graphW = graphR - graphL, graphH = graphB - graphT;
                int graphMidY = graphT + graphH / 2;
                float pxPerDb = (float)(graphH / 2 - 6) / 12.0f;

                float* pGains[3] = {
                    &g_Seq.trackEqLow[g_eqTrack],
                    &g_Seq.trackEqMid[g_eqTrack],
                    &g_Seq.trackEqHigh[g_eqTrack]
                };

                float newGain = 0.5f + (float)(graphMidY - my) / (24.0f * pxPerDb);
                if (newGain < 0.0f) newGain = 0.0f;
                if (newGain > 1.0f) newGain = 1.0f;
                *pGains[dragBand] = newGain;

                float newNormX = (float)(mx - graphL) / (float)graphW;
                if (newNormX < 0.0f) newNormX = 0.0f;
                if (newNormX > 1.0f) newNormX = 1.0f;
                g_Seq.trackEqFreq[g_eqTrack][dragBand] = newNormX;

                update_track_eq_params(g_eqTrack);
                request_eq_curve_render(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);

            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;

            int targetBand = -1;
            if (pt.x < w * 0.35f) targetBand = 0;
            else if (pt.x < w * 0.65f) targetBand = 1;
            else targetBand = 2;

            bool overQLabel = (pt.y >= h - 32 && pt.y <= h - 8);

            if (overQLabel) {
                float step = (zDelta > 0) ? 0.05f : -0.05f;
                g_Seq.trackEqQ[g_eqTrack][targetBand] += step;
                if (g_Seq.trackEqQ[g_eqTrack][targetBand] < 0.0f)
                    g_Seq.trackEqQ[g_eqTrack][targetBand] = 0.0f;
                if (g_Seq.trackEqQ[g_eqTrack][targetBand] > 1.0f)
                    g_Seq.trackEqQ[g_eqTrack][targetBand] = 1.0f;
                update_track_eq_params(g_eqTrack);
                request_eq_curve_render(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            int graphL = 52, graphR = w - 16, graphT = 16, graphB = h - 76;
            int graphW = graphR - graphL, graphH = graphB - graphT;
            int graphMidY = graphT + graphH / 2;
            float pxPerDb = (float)(graphH / 2 - 6) / 12.0f;

            float gains[3] = {
                g_Seq.trackEqLow[g_eqTrack],
                g_Seq.trackEqMid[g_eqTrack],
                g_Seq.trackEqHigh[g_eqTrack]
            };

            int nearBand = -1;
            for (int i = 0; i < 3; ++i) {
                float normX = g_Seq.trackEqFreq[g_eqTrack][i];
                int nx = graphL + (int)(normX * (float)graphW);
                int ny = graphMidY - (int)((gains[i] - 0.5f) * 24.0f * pxPerDb);
                if (abs(pt.x - nx) < 30 && abs(pt.y - ny) < 30) {
                    nearBand = i;
                    break;
                }
            }
            if (nearBand >= 0) targetBand = nearBand;

            float step = (zDelta > 0) ? 0.05f : -0.05f;
            float* pGains[3] = {
                &g_Seq.trackEqLow[g_eqTrack],
                &g_Seq.trackEqMid[g_eqTrack],
                &g_Seq.trackEqHigh[g_eqTrack]
            };
            *pGains[targetBand] += step;
            if (*pGains[targetBand] < 0.0f) *pGains[targetBand] = 0.0f;
            if (*pGains[targetBand] > 1.0f) *pGains[targetBand] = 1.0f;

            update_track_eq_params(g_eqTrack);
            request_eq_curve_render(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_RBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;

            
            int graphL = 52, graphR = w - 16, graphT = 16, graphB = h - 76;
            int graphW = graphR - graphL, graphH = graphB - graphT;
            int graphMidY = graphT + graphH / 2;
            float pxPerDb = (float)(graphH / 2 - 6) / 12.0f;

            float gains[3] = {
                g_Seq.trackEqLow[g_eqTrack],
                g_Seq.trackEqMid[g_eqTrack],
                g_Seq.trackEqHigh[g_eqTrack]
            };

            
            int targetBand = -1;
            float bestDistSq = 1e9f;

            for (int i = 0; i < 3; ++i) {
                float normX = g_Seq.trackEqFreq[g_eqTrack][i];
                int nx = graphL + (int)(normX * (float)graphW);
                int ny = graphMidY - (int)((gains[i] - 0.5f) * 24.0f * pxPerDb);
                
                if (ny < graphT) ny = graphT;
                if (ny > graphB) ny = graphB;

                float dx = (float)(mx - nx);
                float dy = (float)(my - ny);
                float dSq = dx * dx + dy * dy;
                if (dSq < bestDistSq) {
                    bestDistSq = dSq;
                    targetBand = i;
                }
            }

            
            if (targetBand >= 0) {
                float* pGains[3] = {
                    &g_Seq.trackEqLow[g_eqTrack],
                    &g_Seq.trackEqMid[g_eqTrack],
                    &g_Seq.trackEqHigh[g_eqTrack]
                };
                *pGains[targetBand] = 0.5f;

                
                const float defFreqs[3] = { 0.20f, 0.50f, 0.80f };
                g_Seq.trackEqFreq[g_eqTrack][targetBand] = defFreqs[targetBand];
                g_Seq.trackEqQ[g_eqTrack][targetBand] = 0.70f;

                update_track_eq_params(g_eqTrack);
                request_eq_curve_render(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONUP:
            if (dragBand >= 0) {
                dragBand = -1;
                if (GetCapture() == hwnd) ReleaseCapture();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_CAPTURECHANGED: 
        case WM_KILLFOCUS:
            if (dragBand >= 0) {
                dragBand = -1;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE || wParam == VK_RETURN) {
                ShowWindow(hwnd, SW_HIDE);
            }
            return 0;

        case WM_SIZE:
            // Graph geometry changed: drop the stale-sized front surface so
            // WM_PAINT skips compositing until the worker publishes at the
            // new size, then queue a fresh render.
            eq_init_lock();
            EnterCriticalSection(&g_eqCurveLock);
            if (g_eqCurveBmp) {
                SelectObject(g_eqCurveDC, g_eqCurveOldBmp);
                DeleteObject(g_eqCurveBmp);
                g_eqCurveBmp = NULL;
                g_eqCurveW = 0;
                g_eqCurveH = 0;
            }
            LeaveCriticalSection(&g_eqCurveLock);
            request_eq_curve_render(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case WM_DESTROY:
            // Stop the worker before tearing down shared surfaces.
            if (InterlockedExchange(&g_eqWorkerRunning, 0) == 1) {
                if (g_hEqWorkerEvent) SetEvent(g_hEqWorkerEvent);
                if (g_hEqWorkerThread) {
                    WaitForSingleObject(g_hEqWorkerThread, 1000);
                    CloseHandle(g_hEqWorkerThread);
                    g_hEqWorkerThread = NULL;
                }
                if (g_hEqWorkerEvent) {
                    CloseHandle(g_hEqWorkerEvent);
                    g_hEqWorkerEvent = NULL;
                }
            }
            eq_init_lock();
            EnterCriticalSection(&g_eqCurveLock);
            if (g_eqCurveDC) {
                if (g_eqCurveBmp) {
                    SelectObject(g_eqCurveDC, g_eqCurveOldBmp);
                    DeleteObject(g_eqCurveBmp);
                    g_eqCurveBmp = NULL;
                }
                DeleteDC(g_eqCurveDC);
                g_eqCurveDC = NULL;
                g_eqCurveW = 0;
                g_eqCurveH = 0;
            }
            LeaveCriticalSection(&g_eqCurveLock);
            g_eqHwnd = NULL;
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static inline void open_track_eq_dialog(HWND parentHwnd, int trackIdx) {
    if (trackIdx < 0 || trackIdx >= g_Seq.trackCount || trackIdx >= MAX_TRACKS) return;
    g_eqTrack = trackIdx;

    if (!g_eqHwnd) {
        static bool s_eqRegistered = false;
        if (!s_eqRegistered) {
            WNDCLASSA wc = { 0 };
            wc.lpfnWndProc = EqWndProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractEqClass";
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_eqRegistered = true;
        }

        int rw = 520, rh = 380;
        int scrW = GetSystemMetrics(SM_CXSCREEN);
        int scrH = GetSystemMetrics(SM_CYSCREEN);
        int rx = (scrW - rw) / 2;
        int ry = (scrH - rh) / 2;

        g_eqHwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            "RefractEqClass",
            "Track EQ",
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE,
            rx, ry, rw, rh,
            parentHwnd, NULL, GetModuleHandle(NULL), NULL
        );
    }

    char titleBuf[64];
    snprintf(titleBuf, sizeof(titleBuf), "Track %d - Parametric EQ", trackIdx + 1);
    SetWindowTextA(g_eqHwnd, titleBuf);

    // Start the curve worker once; re-request on every open so a track
    // switch re-renders with the new track's band params.
    eq_init_lock();
    if (InterlockedCompareExchange(&g_eqWorkerRunning, 1, 0) == 0) {
        g_hEqWorkerEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (g_hEqWorkerEvent) {
            g_hEqWorkerThread = CreateThread(NULL, 0, EqCurveWorkerThreadProc, NULL, 0, NULL);
            if (!g_hEqWorkerThread) {
                InterlockedExchange(&g_eqWorkerRunning, 0);
                CloseHandle(g_hEqWorkerEvent);
                g_hEqWorkerEvent = NULL;
            }
        } else {
            InterlockedExchange(&g_eqWorkerRunning, 0);
        }
    }
    if (g_hEqWorkerThread) {
        request_eq_curve_render(g_eqHwnd);
    }

    ShowWindow(g_eqHwnd, SW_SHOW);
    SetForegroundWindow(g_eqHwnd);
    InvalidateRect(g_eqHwnd, NULL, FALSE);
}



 

// --- Track Filter Plotter ----------------------------------------------------
// Modeless per-track tool for the stackable track filter: up to four RBJ
// biquads (LP/HP/BP/Notch) in series, applied to the track before its FX
// chain. The graph draws the combined magnitude response from the stored
// double-precision curve (evaluating per-pixel in float makes the notch/LP
// denominators stripe, which is why the stored curve is authoritative here).
// Horizontal drag sets the cutoff, the wheel sets Q, and the buttons stack:
// any combination of bands can be on, gated by the master ON/OFF toggle.
// Parameter updates take the seq lock so the audio thread always sees
// consistent coefficients (same pattern as update_track_eq_params).

static int  g_filterTrack = 0;
static HWND g_filterHwnd = NULL;
static bool g_filterDragging = false;

static inline void request_filter_curve_render(HWND hwnd);   // defined below

// Graph geometry (mirrors the Parametric EQ dialog's layout).
static inline void filter_plotter_graph_rect(HWND hwnd, RECT* rc, int* graphL, int* graphR,
                                             int* graphT, int* graphB) {
    GetClientRect(hwnd, rc);
    int w = rc->right - rc->left;
    int h = rc->bottom - rc->top;
    *graphL = 52;
    *graphR = w - 16;
    *graphT = 16;
    *graphB = h - 110;
}

// Log frequency <-> normalized x over the 20 Hz..20 kHz probe range.
static inline float filter_plotter_norm_x(float freqHz) {
    return logf(freqHz / TRACK_FILTER_FMIN) / logf(TRACK_FILTER_FMAX / TRACK_FILTER_FMIN);
}

static inline float filter_plotter_freq_at(float normX) {
    if (normX < 0.0f) normX = 0.0f;
    if (normX > 1.0f) normX = 1.0f;
    return TRACK_FILTER_FMIN * powf(TRACK_FILTER_FMAX / TRACK_FILTER_FMIN, normX);
}

// Push the dialog's parameter values into the shared state. Lock discipline:
// seq_lock() guards ONLY the tiny target write; the expensive 512-point
// magnitude curve is computed outside the lock on the same (UI-owned) struct
// so the audio callback never waits behind curve math during a drag.
// Coefficients themselves are rebuilt by the audio thread as it slews toward
// the targets - no instantaneous coefficient swap, no state clear, no click.
static inline void filter_plotter_apply(HWND hwnd) {
    (void)hwnd;
    int t = g_filterTrack;
    if (t < 0 || t >= g_Seq.trackCount || t >= MAX_TRACKS) return;
    seq_lock();
    TrackFilter* f = &g_Seq.trackFilter[t];
    track_filter_set_target(f, f->frequency, f->q);
    seq_unlock();
    track_filter_update(f, (float)SAMPLE_RATE);   // UI-render data, off-lock
    request_filter_curve_render(hwnd);
    if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, FALSE);
}

static inline void filter_plotter_toggle_enable(HWND hwnd) {
    int t = g_filterTrack;
    if (t < 0 || t >= g_Seq.trackCount || t >= MAX_TRACKS) return;
    seq_lock();
    TrackFilter* f = &g_Seq.trackFilter[t];
    f->enabled = !f->enabled;
    seq_unlock();
    request_filter_curve_render(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
    // Invalidate main timeline so the "flt" badge appears/disappears immediately
    invalidate_timeline_cache();
    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
}

static inline void filter_plotter_toggle_band(HWND hwnd, int band) {
    if (band < 0 || band >= TRACK_FILTER_TYPE_COUNT) return;
    int t = g_filterTrack;
    if (t < 0 || t >= g_Seq.trackCount || t >= MAX_TRACKS) return;
    seq_lock();
    TrackFilter* f = &g_Seq.trackFilter[t];
    f->typeMask ^= TRACK_FILTER_BIT(band);
    f->typeMask &= TRACK_FILTER_MASK_ALL;
    seq_unlock();
    track_filter_update(f, (float)SAMPLE_RATE);
    request_filter_curve_render(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
    // Invalidate main timeline so the "flt" badge appears/disappears immediately
    invalidate_timeline_cache();
    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
}

// --- Decoupled supersampled curve rendering (mirrors the EQ panel) ----------
// The 2x supersampled response raster (DIB rasterization + box-filter
// downsample) runs on a background worker thread; the UI thread only
// composites the published surface with one AlphaBlend in WM_PAINT. Drag
// events just submit a cheap job snapshot and wake the worker, so the
// graph never rasterizes on the shared main thread.

typedef struct {
    int      trackIdx;
    int      graphL, graphR, graphT, graphB;
    uint32_t typeMask;
    float    frequency;
    float    q;
    bool     enabled;
} FilterRenderJob;

static FilterRenderJob   g_filterJob = { 0 };
static HANDLE            g_hFilterWorkerThread = NULL;
static HANDLE            g_hFilterWorkerEvent  = NULL;
static volatile LONG     g_filterWorkerRunning = 0;

// Front surface the UI thread reads in WM_PAINT (guarded by g_filterCurveLock).
static HDC              g_fltCurveDC       = NULL;
static HBITMAP          g_fltCurveBmp      = NULL;
static HBITMAP          g_fltCurveOldBmp   = NULL;
static int              g_fltCurveW        = 0;
static int              g_fltCurveH        = 0;
static CRITICAL_SECTION g_filterCurveLock;
static bool             g_filterCurveLockInit = false;

static inline void filter_init_lock(void) {
    if (!g_filterCurveLockInit) {
        (void)InitializeCriticalSectionAndSpinCount(&g_filterCurveLock, 4000);
        g_filterCurveLockInit = true;
    }
}

// Snapshot the current filter params + graph geometry into the shared job and
// wake the worker. Cheap (struct copy); the worker coalesces drags via the
// wake event, so this is safe at mouse-polling rate.
static inline void request_filter_curve_render(HWND hwnd) {
    if (!g_hFilterWorkerEvent) return;

    RECT rc;
    if (!GetClientRect(hwnd, &rc)) return;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    int graphL, graphR, graphT, graphB;
    RECT rc2;
    filter_plotter_graph_rect(hwnd, &rc2, &graphL, &graphR, &graphT, &graphB);
    (void)w; (void)h;

    int t = g_filterTrack;
    filter_init_lock();
    EnterCriticalSection(&g_filterCurveLock);
    g_filterJob.trackIdx = t;
    g_filterJob.graphL = graphL;
    g_filterJob.graphR = graphR;
    g_filterJob.graphT = graphT;
    g_filterJob.graphB = graphB;
    if (t >= 0 && t < g_Seq.trackCount && t < MAX_TRACKS) {
        g_filterJob.typeMask  = g_Seq.trackFilter[t].typeMask;
        g_filterJob.frequency = g_Seq.trackFilter[t].frequency;
        g_filterJob.q         = g_Seq.trackFilter[t].q;
        g_filterJob.enabled   = g_Seq.trackFilter[t].enabled;
    }
    LeaveCriticalSection(&g_filterCurveLock);

    SetEvent(g_hFilterWorkerEvent);
}

// Heavy supersampled curve rasterization, off the main thread. Scratch DIBs
// are created once and reused (only grown when the graph gets bigger), so
// steady-state drags do zero GDI allocations.
static DWORD WINAPI FilterCurveWorkerThreadProc(LPVOID lpParam) {
    (void)lpParam;

    HDC  scratchSSDC = NULL, scratchDownDC = NULL;
    HBITMAP scratchSSBmp = NULL, scratchDownBmp = NULL;
    HGDIOBJ scratchSSOld = NULL, scratchDownOld = NULL;
    DWORD *pSSBits = NULL, *pDownBits = NULL;
    int capSSW = 0, capSSH = 0, capDownW = 0, capDownH = 0;

    while (InterlockedCompareExchange(&g_filterWorkerRunning, 1, 1) == 1) {
        WaitForSingleObject(g_hFilterWorkerEvent, INFINITE);
        if (InterlockedCompareExchange(&g_filterWorkerRunning, 1, 1) != 1) break;

        bool dirty = true;
        while (dirty && InterlockedCompareExchange(&g_filterWorkerRunning, 1, 1) == 1) {
            // Snapshot under the lock FIRST, then clear: if the UI thread
            // submits a newer job during our render, the event is already
            // set and we loop again with the fresh params.
            FilterRenderJob job;
            filter_init_lock();
            EnterCriticalSection(&g_filterCurveLock);
            job = g_filterJob;
            LeaveCriticalSection(&g_filterCurveLock);
            dirty = false;

            int graphW = job.graphR - job.graphL;
            int graphH = job.graphB - job.graphT;
            if (graphW <= 0 || graphH <= 0) break;

            int ssW = graphW * 2;
            int ssH = graphH * 2;

            if (!scratchSSDC) {
                scratchSSDC = CreateCompatibleDC(NULL);
                scratchSSOld = GetCurrentObject(scratchSSDC, OBJ_BITMAP);
            }
            if (ssW > capSSW || ssH > capSSH || !scratchSSBmp) {
                if (scratchSSBmp) DeleteObject(scratchSSBmp);
                BITMAPINFO bmi = { 0 };
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = ssW;
                bmi.bmiHeader.biHeight = -ssH;
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                scratchSSBmp = CreateDIBSection(scratchSSDC, &bmi, DIB_RGB_COLORS, (void**)&pSSBits, NULL, 0);
                SelectObject(scratchSSDC, scratchSSBmp);
                capSSW = ssW; capSSH = ssH;
            }

            if (!scratchDownDC) {
                scratchDownDC = CreateCompatibleDC(NULL);
                scratchDownOld = GetCurrentObject(scratchDownDC, OBJ_BITMAP);
            }
            if (graphW > capDownW || graphH > capDownH || !scratchDownBmp) {
                if (scratchDownBmp) DeleteObject(scratchDownBmp);
                BITMAPINFO bmi = { 0 };
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = graphW;
                bmi.bmiHeader.biHeight = -graphH;
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                scratchDownBmp = CreateDIBSection(scratchDownDC, &bmi, DIB_RGB_COLORS, (void**)&pDownBits, NULL, 0);
                SelectObject(scratchDownDC, scratchDownBmp);
                capDownW = graphW; capDownH = graphH;
            }

            if (!pSSBits || !pDownBits) break;

            memset(pSSBits, 0, (size_t)ssW * (size_t)ssH * 4);

            int ssMidY = ssH / 2;
            float pxPerDb = (float)(graphH / 2 - 6) / 24.0f;
            float ssPxPerDb = pxPerDb * 2.0f;

            // Coefficients from the job snapshot (same math as the audio path).
            TrackFilter fLocal;
            memset(&fLocal, 0, sizeof(fLocal));
            fLocal.typeMask  = job.typeMask & TRACK_FILTER_MASK_ALL;
            fLocal.frequency = job.frequency;
            fLocal.q         = job.q;
            fLocal.enabled   = job.enabled;
            track_filter_update(&fLocal, (float)SAMPLE_RATE);

            const COLORREF curveCol = job.enabled && fLocal.typeMask != 0
                                    ? RGB(80, 210, 240) : RGB(60, 72, 88);

            // Build the response polygon at 2x resolution: magnitude evaluated
            // per SS column from the double-precision coefficients (the stored
            // 512-point curve is too coarse to rasterize supersampled).
            POINT ssFillPts[1100];
            int ssPtCount = 0;
            ssFillPts[ssPtCount++] = (POINT){ 0, ssMidY };
            int ssCurveTop = ssH, ssCurveBot = 0;
            for (int x = 0; x < ssW; x += 2) {
                float normX = (float)x / (float)(ssW - 1);
                float probe = filter_plotter_freq_at(normX);
                if (probe > (float)SAMPLE_RATE * 0.495f) probe = (float)SAMPLE_RATE * 0.495f;
                double w = TRACK_FILTER_TWO_PI * probe / (double)SAMPLE_RATE;
                double db = 0.0;
                for (int tt = 0; tt < TRACK_FILTER_TYPE_COUNT; ++tt) {
                    if (fLocal.typeMask & (1u << tt))
                        db += track_filter_band_db(&fLocal.band[tt], cos(w), cos(2.0 * w));
                }
                int cy = ssMidY - (int)(db * ssPxPerDb + (db > 0.0f ? 0.5f : -0.5f));
                if (cy < 0) cy = 0;
                if (cy > ssH - 1) cy = ssH - 1;
                if (cy < ssCurveTop) ssCurveTop = cy;
                if (cy > ssCurveBot) ssCurveBot = cy;
                if (ssPtCount < 1096) ssFillPts[ssPtCount++] = (POINT){ x, cy };
            }
            ssFillPts[ssPtCount++] = (POINT){ ssW - 1, ssMidY };

            // Shaded polygon body under the curve.
            HBRUSH ssFillBrush = CreateSolidBrush(RGB(18, 32, 50));
            HGDIOBJ oldSSBr = SelectObject(scratchSSDC, ssFillBrush);
            HGDIOBJ oldSSPen = SelectObject(scratchSSDC, GetStockObject(NULL_PEN));
            Polygon(scratchSSDC, ssFillPts, ssPtCount);
            SelectObject(scratchSSDC, oldSSPen);
            SelectObject(scratchSSDC, oldSSBr);
            DeleteObject(ssFillBrush);

            // Single-colored response stroke (4px wide at 2x = 2px at 1x).
            HPEN strokePen = CreatePen(PS_SOLID, 4, curveCol);
            HGDIOBJ oldStroke = SelectObject(scratchSSDC, strokePen);
            bool started = false;
            int lastX = -1;
            for (int i = 1; i < ssPtCount - 1; ++i) {
                POINT pt = ssFillPts[i];
                if (pt.x == lastX) continue;
                if (!started) {
                    MoveToEx(scratchSSDC, pt.x, pt.y, NULL);
                    started = true;
                } else {
                    LineTo(scratchSSDC, pt.x, pt.y);
                }
                lastX = pt.x;
            }
            SelectObject(scratchSSDC, oldStroke);
            DeleteObject(strokePen);

            // Downsample 2x -> 1x box filter.
            for (int y = 0; y < graphH; ++y) {
                for (int x = 0; x < graphW; ++x) {
                    DWORD p00 = pSSBits[(y * 2 + 0) * ssW + (x * 2 + 0)];
                    DWORD p01 = pSSBits[(y * 2 + 0) * ssW + (x * 2 + 1)];
                    DWORD p10 = pSSBits[(y * 2 + 1) * ssW + (x * 2 + 0)];
                    DWORD p11 = pSSBits[(y * 2 + 1) * ssW + (x * 2 + 1)];

                    int bSum = (p00 & 0xFF) + (p01 & 0xFF) + (p10 & 0xFF) + (p11 & 0xFF);
                    int gSum = ((p00 >> 8) & 0xFF) + ((p01 >> 8) & 0xFF) + ((p10 >> 8) & 0xFF) + ((p11 >> 8) & 0xFF);
                    int rSum = ((p00 >> 16) & 0xFF) + ((p01 >> 16) & 0xFF) + ((p10 >> 16) & 0xFF) + ((p11 >> 16) & 0xFF);
                    int cCount = ((p00 != 0) ? 1 : 0) + ((p01 != 0) ? 1 : 0) +
                                 ((p10 != 0) ? 1 : 0) + ((p11 != 0) ? 1 : 0);

                    if (cCount == 0) {
                        pDownBits[y * graphW + x] = 0;
                    } else {
                        float aF = (float)cCount * 0.25f;
                        BYTE a  = (BYTE)(aF * 255.0f + 0.5f);
                        BYTE pr = (BYTE)((float)rSum * 0.25f * aF + 0.5f);
                        BYTE pg = (BYTE)((float)gSum * 0.25f * aF + 0.5f);
                        BYTE pb = (BYTE)((float)bSum * 0.25f * aF + 0.5f);
                        pDownBits[y * graphW + x] = ((DWORD)a << 24) | ((DWORD)pr << 16) | ((DWORD)pg << 8) | (DWORD)pb;
                    }
                }
            }

            // Publish to the front surface under the lock.
            filter_init_lock();
            EnterCriticalSection(&g_filterCurveLock);
            if (!g_fltCurveDC) {
                g_fltCurveDC = CreateCompatibleDC(NULL);
                g_fltCurveOldBmp = GetCurrentObject(g_fltCurveDC, OBJ_BITMAP);
            }
            if (graphW != g_fltCurveW || graphH != g_fltCurveH || !g_fltCurveBmp) {
                if (g_fltCurveBmp) DeleteObject(g_fltCurveBmp);
                g_fltCurveBmp = CreateCompatibleBitmap(scratchDownDC, graphW, graphH);
                SelectObject(g_fltCurveDC, g_fltCurveBmp);
                g_fltCurveW = graphW;
                g_fltCurveH = graphH;
            }
            BitBlt(g_fltCurveDC, 0, 0, graphW, graphH, scratchDownDC, 0, 0, SRCCOPY);
            LeaveCriticalSection(&g_filterCurveLock);

            // Nudge the plotter window so a frame published between paints
            // becomes visible without waiting for the next mouse event.
            if (g_filterHwnd && IsWindow(g_filterHwnd)) {
                InvalidateRect(g_filterHwnd, NULL, FALSE);
            }

            // A newer job may have arrived while we rendered: if the wake
            // event is already set, loop immediately with the fresh params.
            dirty = (WaitForSingleObject(g_hFilterWorkerEvent, 0) == WAIT_OBJECT_0);
        }
    }

    if (scratchSSBmp) {
        SelectObject(scratchSSDC, scratchSSOld);
        DeleteObject(scratchSSBmp);
    }
    if (scratchDownBmp) {
        SelectObject(scratchDownDC, scratchDownOld);
        DeleteObject(scratchDownBmp);
    }
    if (scratchSSDC) DeleteDC(scratchSSDC);
    if (scratchDownDC) DeleteDC(scratchDownDC);
    return 0;
}

// Start the worker once; re-request on every open so a track switch re-renders
// with the new track's band params (same lifecycle as the EQ worker).
static inline void filter_worker_ensure_started(HWND hwnd) {
    filter_init_lock();
    if (InterlockedCompareExchange(&g_filterWorkerRunning, 1, 0) == 0) {
        g_hFilterWorkerEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (g_hFilterWorkerEvent) {
            g_hFilterWorkerThread = CreateThread(NULL, 0, FilterCurveWorkerThreadProc, NULL, 0, NULL);
            if (!g_hFilterWorkerThread) {
                InterlockedExchange(&g_filterWorkerRunning, 0);
                CloseHandle(g_hFilterWorkerEvent);
                g_hFilterWorkerEvent = NULL;
            }
        } else {
            InterlockedExchange(&g_filterWorkerRunning, 0);
        }
    }
    if (g_hFilterWorkerThread) {
        request_filter_curve_render(hwnd);
    }
}

// Bottom control strip layout: the five buttons centered as one row, the
// status readout centered on its own line below them.
typedef struct {
    RECT bandBtns[TRACK_FILTER_TYPE_COUNT];
    RECT enableBtn;
} FilterStripLayout;

static inline void filter_plotter_strip_layout(int winW, int graphL, int graphR,
                                               int stripTop, FilterStripLayout* lay) {
    (void)winW;
    const int btnW = scale_x(64), btnH = scale_y(26), gap = scale_x(8);
    int totalW = btnW * (TRACK_FILTER_TYPE_COUNT + 1) + gap * TRACK_FILTER_TYPE_COUNT;
    int rowStart = graphL + ((graphR - graphL) - totalW) / 2;
    if (rowStart < graphL) rowStart = graphL;
    int bx = rowStart;
    for (int i = 0; i < TRACK_FILTER_TYPE_COUNT; ++i) {
        SetRect(&lay->bandBtns[i], bx, stripTop, bx + btnW, stripTop + btnH);
        bx += btnW + gap;
    }
    SetRect(&lay->enableBtn, bx, stripTop, bx + btnW, stripTop + btnH);
}

static inline void filter_plotter_draw_button(HDC dc, const RECT* b, const char* label,
                                              bool active, bool lit) {
    // active: band in the mask / master on. lit: extra emphasis while dragging.
    HBRUSH br = CreateSolidBrush(active ? (lit ? RGB(32, 62, 74) : RGB(26, 50, 60))
                                        : RGB(30, 34, 42));
    HPEN pen = CreatePen(PS_SOLID, 1, active ? (lit ? RGB(120, 235, 255) : RGB(80, 210, 240))
                                             : RGB(55, 60, 72));
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pen);
    RoundRect(dc, b->left, b->top, b->right, b->bottom, 4, 4);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(br);
    DeleteObject(pen);
    SetTextColor(dc, active ? (lit ? RGB(160, 245, 255) : RGB(120, 235, 255))
                            : RGB(150, 160, 175));
    DrawTextA(dc, label, -1, (RECT*)b, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static LRESULT CALLBACK FilterPlotterWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HFONT oldFontMain = SELECT_UI_FONT(hdc);

        RECT rc;
        int graphL, graphR, graphT, graphB;
        filter_plotter_graph_rect(hwnd, &rc, &graphL, &graphR, &graphT, &graphB);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0) {
            SelectObject(hdc, oldFontMain);
            EndPaint(hwnd, &ps);
            return 0;
        }

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
        HFONT oldFontMem = SELECT_UI_FONT(memDC);

        HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
        FillRect(memDC, &rc, bg);
        DeleteObject(bg);

        int graphW = graphR - graphL, graphH = graphB - graphT;
        int graphMidY = graphT + graphH / 2;
        const float dbRange = 24.0f;
        float pxPerDb = (float)(graphH / 2 - 6) / dbRange;

        SetBkMode(memDC, TRANSPARENT);

        // Read the live filter state under the seq lock.
        TrackFilter fLocal;
        bool anyActive = false;
        int t = g_filterTrack;
        if (t >= 0 && t < g_Seq.trackCount && t < MAX_TRACKS) {
            seq_lock();
            fLocal = g_Seq.trackFilter[t];
            anyActive = track_filter_any_active(&fLocal);
            seq_unlock();
        } else {
            track_filter_init_defaults(&fLocal);
        }

        // Frequency grid at the standard decade stops.
        static const struct { float f; const char* label; } kFreqStops[] = {
            { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" },
            { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" },
            { 10000.0f, "10k" }
        };
        HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(27, 32, 42));
        HGDIOBJ oldPen = SelectObject(memDC, gridPen);
        for (int i = 0; i < (int)(sizeof(kFreqStops) / sizeof(kFreqStops[0])); ++i) {
            int gx = graphL + (int)(filter_plotter_norm_x(kFreqStops[i].f) * (float)graphW);
            MoveToEx(memDC, gx, graphT, NULL);
            LineTo(memDC, gx, graphB);
        }
        for (int d = -18; d <= 18; d += 6) {
            if (d == 0) continue;
            int gy = graphMidY - (int)(d * pxPerDb);
            MoveToEx(memDC, graphL, gy, NULL);
            LineTo(memDC, graphR, gy);
        }
        SelectObject(memDC, oldPen);
        DeleteObject(gridPen);

        // Axis labels.
        SetTextColor(memDC, RGB(85, 96, 114));
        for (int i = 0; i < (int)(sizeof(kFreqStops) / sizeof(kFreqStops[0])); ++i) {
            int gx = graphL + (int)(filter_plotter_norm_x(kFreqStops[i].f) * (float)graphW);
            TextOutA(memDC, gx - 8, graphB + 4, kFreqStops[i].label, (int)strlen(kFreqStops[i].label));
        }
        for (int d = -24; d <= 24; d += 12) {
            char lbl[8];
            int gy = graphMidY - (int)(d * pxPerDb);
            snprintf(lbl, sizeof(lbl), "%+d", d);
            TextOutA(memDC, 10, gy - 7, lbl, (int)strlen(lbl));
        }

        // Magnitude response: composite the worker-rendered supersampled
        // curve (single AlphaBlend, same pattern as the EQ panel). While the
        // worker hasn't published yet (first paint / fresh open), an instant
        // dotted polyline from the stored curve stays visible as feedback.
        if (g_fltCurveDC && g_fltCurveW == graphW && g_fltCurveH == graphH) {
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            filter_init_lock();
            EnterCriticalSection(&g_filterCurveLock);
            if (g_fltCurveDC && g_fltCurveW == graphW && g_fltCurveH == graphH) {
                AlphaBlend(memDC, graphL, graphT, graphW, graphH, g_fltCurveDC, 0, 0, graphW, graphH, bf);
            }
            LeaveCriticalSection(&g_filterCurveLock);
        } else {
            COLORREF fbCol = anyActive ? RGB(70, 105, 125) : RGB(60, 72, 88);
            HPEN fbPen = CreatePen(PS_DOT, 1, fbCol);
            oldPen = SelectObject(memDC, fbPen);
            bool fbStarted = false;
            int fbLastPx = -1;
            for (int i = 0; i < TRACK_FILTER_POINTS; ++i) {
                float normX = (float)i / (float)(TRACK_FILTER_POINTS - 1);
                int px = graphL + (int)(normX * (float)graphW + 0.5f);
                float db = fLocal.magnitude[i];
                int py = graphMidY - (int)(db * pxPerDb + (db > 0.0f ? 0.5f : -0.5f));
                if (py < graphT) py = graphT;
                if (py > graphB) py = graphB;
                if (!fbStarted) {
                    MoveToEx(memDC, px, py, NULL);
                    fbStarted = true;
                } else if (px != fbLastPx) {
                    LineTo(memDC, px, py);
                }
                fbLastPx = px;
            }
            SelectObject(memDC, oldPen);
            DeleteObject(fbPen);
        }

        // Cutoff marker line + handle (always drawn: the cutoff is shared
        // state even while bypassed, so the handle shows where it will land).
        {
            int cx = graphL + (int)(filter_plotter_norm_x(fLocal.frequency) * (float)graphW);
            HPEN mkPen = CreatePen(PS_DOT, 1, anyActive ? RGB(90, 120, 145) : RGB(55, 64, 76));
            oldPen = SelectObject(memDC, mkPen);
            MoveToEx(memDC, cx, graphT, NULL);
            LineTo(memDC, cx, graphB);
            SelectObject(memDC, oldPen);
            DeleteObject(mkPen);
            draw_aa_circle(memDC, cx, graphMidY, g_filterDragging ? 9.5f : 7.5f,
                           RGB(22, 32, 44),
                           anyActive ? RGB(80, 210, 240) : RGB(80, 92, 108), 1.5f);
        }

        // Bottom strip: buttons centered as one row, readout centered below.
        int stripTop = graphB + 30;
        FilterStripLayout lay;
        filter_plotter_strip_layout(w, graphL, graphR, stripTop, &lay);

        static const char* kTypeNames[TRACK_FILTER_TYPE_COUNT] = { "LP", "HP", "BP", "NOTCH" };
        for (int i = 0; i < TRACK_FILTER_TYPE_COUNT; ++i) {
            filter_plotter_draw_button(memDC, &lay.bandBtns[i], kTypeNames[i],
                                       (fLocal.typeMask & TRACK_FILTER_BIT(i)) != 0,
                                       g_filterDragging);
        }
        filter_plotter_draw_button(memDC, &lay.enableBtn,
                                   fLocal.enabled ? "ON" : "OFF",
                                   fLocal.enabled, false);

        // Status line centered in the space below the button row.
        char readout[96];
        if (fLocal.frequency >= 1000.0f)
            snprintf(readout, sizeof(readout), "%s   %.2fkHz   Q = %.2f",
                     fLocal.enabled ? (anyActive ? "On" : "On (no bands)") : "Bypassed",
                     fLocal.frequency / 1000.0f, fLocal.q);
        else
            snprintf(readout, sizeof(readout), "%s   %.0fHz   Q = %.2f",
                     fLocal.enabled ? (anyActive ? "On" : "On (no bands)") : "Bypassed",
                     fLocal.frequency, fLocal.q);

        int btnBottom = lay.enableBtn.bottom;
        RECT roRc = { graphL, btnBottom + scale_y(8), graphR, btnBottom + scale_y(30) };
        SetTextColor(memDC, anyActive ? RGB(180, 195, 215) : RGB(110, 122, 138));
        DrawTextA(memDC, readout, -1, &roRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Drag HUD.
        if (g_filterDragging) {
            char hudBuf[48];
            if (fLocal.frequency >= 1000.0f)
                snprintf(hudBuf, sizeof(hudBuf), "%.2fkHz  Q=%.2f", fLocal.frequency / 1000.0f, fLocal.q);
            else
                snprintf(hudBuf, sizeof(hudBuf), "%.0fHz  Q=%.2f", fLocal.frequency, fLocal.q);
            int cx = graphL + (int)(filter_plotter_norm_x(fLocal.frequency) * (float)graphW);
            int hudW = 110, hudH = 20;
            int hudX = cx - hudW / 2;
            if (hudX < graphL) hudX = graphL;
            if (hudX + hudW > graphR) hudX = graphR - hudW;
            int hudY = (graphT + 8 < graphMidY - 26) ? graphT + 8 : graphMidY + 16;
            RECT hudRc = { hudX, hudY, hudX + hudW, hudY + hudH };
            HBRUSH hBg = CreateSolidBrush(RGB(15, 18, 24));
            HPEN hBorder = CreatePen(PS_SOLID, 1, RGB(80, 210, 240));
            HGDIOBJ ob2 = SelectObject(memDC, hBg);
            HGDIOBJ op2 = SelectObject(memDC, hBorder);
            RoundRect(memDC, hudRc.left, hudRc.top, hudRc.right, hudRc.bottom, 4, 4);
            SelectObject(memDC, op2);
            SelectObject(memDC, ob2);
            DeleteObject(hBorder);
            DeleteObject(hBg);
            HFONT oldHF = (HFONT)SelectObject(memDC, get_ui_small_font());
            SetTextColor(memDC, RGB(255, 255, 255));
            DrawTextA(memDC, hudBuf, -1, &hudRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(memDC, oldHF);
        }

        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldFontMem);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        SelectObject(hdc, oldFontMain);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        RECT rc;
        int graphL, graphR, graphT, graphB;
        filter_plotter_graph_rect(hwnd, &rc, &graphL, &graphR, &graphT, &graphB);
        int graphW = graphR - graphL;
        int stripTop = graphB + 30;
        FilterStripLayout lay;
        filter_plotter_strip_layout(rc.right - rc.left, graphL, graphR, stripTop, &lay);

        // Band toggles (stack) + master enable.
        for (int i = 0; i < TRACK_FILTER_TYPE_COUNT; ++i) {
            if (mx >= lay.bandBtns[i].left && mx <= lay.bandBtns[i].right &&
                my >= lay.bandBtns[i].top && my <= lay.bandBtns[i].bottom) {
                filter_plotter_toggle_band(hwnd, i);
                return 0;
            }
        }
        if (mx >= lay.enableBtn.left && mx <= lay.enableBtn.right &&
            my >= lay.enableBtn.top && my <= lay.enableBtn.bottom) {
            filter_plotter_toggle_enable(hwnd);
            return 0;
        }

        // Graph drag: horizontal position = cutoff frequency.
        if (mx >= graphL && mx <= graphR && my >= graphT && my <= graphB) {
            float normX = (float)(mx - graphL) / (float)graphW;
            int t = g_filterTrack;
            if (t >= 0 && t < g_Seq.trackCount && t < MAX_TRACKS) {
                seq_lock();
                g_Seq.trackFilter[t].frequency = filter_plotter_freq_at(normX);
                seq_unlock();
                filter_plotter_apply(hwnd);
            }
            g_filterDragging = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!g_filterDragging) return 0;
        int mx = GET_X_LPARAM(lParam);
        RECT rc;
        int graphL, graphR, graphT, graphB;
        filter_plotter_graph_rect(hwnd, &rc, &graphL, &graphR, &graphT, &graphB);
        int graphW = graphR - graphL;
        float normX = (float)(mx - graphL) / (float)graphW;
        int t = g_filterTrack;
        if (t >= 0 && t < g_Seq.trackCount && t < MAX_TRACKS) {
            seq_lock();
            g_Seq.trackFilter[t].frequency = filter_plotter_freq_at(normX);
            seq_unlock();
            filter_plotter_apply(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_filterDragging) {
            g_filterDragging = false;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        int t = g_filterTrack;
        if (t < 0 || t >= g_Seq.trackCount || t >= MAX_TRACKS) return 0;
        float notches = (float)zDelta / (float)WHEEL_DELTA;
        seq_lock();
        float q = g_Seq.trackFilter[t].q;
        // Multiplicative steps keep the Q sweep perceptually even.
        q *= powf(1.12f, notches);
        if (q < 0.05f) q = 0.05f;
        if (q > 100.0f) q = 100.0f;
        g_Seq.trackFilter[t].q = q;
        seq_unlock();
        filter_plotter_apply(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE || wParam == VK_RETURN) {
            ShowWindow(hwnd, SW_HIDE);
        }
        return 0;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY: {
        // Stop the curve worker and release the published surface (same
        // teardown as the EQ panel).
        if (InterlockedCompareExchange(&g_filterWorkerRunning, 0, 1) == 1) {
            if (g_hFilterWorkerEvent) SetEvent(g_hFilterWorkerEvent);
            if (g_hFilterWorkerThread) {
                WaitForSingleObject(g_hFilterWorkerThread, 2000);
                CloseHandle(g_hFilterWorkerThread);
                g_hFilterWorkerThread = NULL;
            }
            if (g_hFilterWorkerEvent) {
                CloseHandle(g_hFilterWorkerEvent);
                g_hFilterWorkerEvent = NULL;
            }
        }
        filter_init_lock();
        EnterCriticalSection(&g_filterCurveLock);
        if (g_fltCurveDC) {
            if (g_fltCurveBmp) {
                SelectObject(g_fltCurveDC, g_fltCurveOldBmp);
                DeleteObject(g_fltCurveBmp);
                g_fltCurveBmp = NULL;
            }
            DeleteDC(g_fltCurveDC);
            g_fltCurveDC = NULL;
        }
        g_fltCurveW = 0;
        g_fltCurveH = 0;
        LeaveCriticalSection(&g_filterCurveLock);
        g_filterHwnd = NULL;
        return 0;
    }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static inline void open_track_filter_dialog(HWND parentHwnd, int trackIdx) {
    if (trackIdx < 0 || trackIdx >= g_Seq.trackCount || trackIdx >= MAX_TRACKS) return;
    g_filterTrack = trackIdx;

    if (!g_filterHwnd) {
        static bool s_filterRegistered = false;
        if (!s_filterRegistered) {
            WNDCLASSA wc = { 0 };
            wc.lpfnWndProc = FilterPlotterWndProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractFilterClass";
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_filterRegistered = true;
        }

        int rw = scale_x(520), rh = scale_y(380);
        int scrW = GetSystemMetrics(SM_CXSCREEN);
        int scrH = GetSystemMetrics(SM_CYSCREEN);
        int rx = (scrW - rw) / 2;
        int ry = (scrH - rh) / 2;

        g_filterHwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            "RefractFilterClass",
            "Track Filter Plotter",
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE,
            rx, ry, rw, rh,
            parentHwnd, NULL, GetModuleHandle(NULL), NULL
        );
    }

    char titleBuf[64];
    snprintf(titleBuf, sizeof(titleBuf), "Track %d - Filter Plotter", trackIdx + 1);
    SetWindowTextA(g_filterHwnd, titleBuf);

    // Start the supersampled-curve worker once; re-request on every open so a
    // track switch re-renders with the new track's filter params.
    filter_worker_ensure_started(g_filterHwnd);

    ShowWindow(g_filterHwnd, SW_SHOW);
    SetForegroundWindow(g_filterHwnd);
    InvalidateRect(g_filterHwnd, NULL, FALSE);
}

static HWND    g_bpmHwnd = NULL;
static HWND    g_bpmEdit = NULL;
static WNDPROC g_bpmEditDefaultProc = NULL;
static float   g_bpmOriginal = 120.0f;

static inline void bpm_edit_commit(HWND hwnd) {
    char buf[32] = { 0 };
    if (g_bpmEdit && IsWindow(g_bpmEdit)) {
        GetWindowTextA(g_bpmEdit, buf, sizeof(buf));
    }
    float v = (float)atof(buf);
    if (v < 40.0f) v = 40.0f;
    if (v > 300.0f) v = 300.0f;
    g_Seq.bpm = v;
    char tbuf[32];
    snprintf(tbuf, sizeof(tbuf), "%.2f", g_Seq.bpm);
    if (g_bpmEdit && IsWindow(g_bpmEdit)) SetWindowTextA(g_bpmEdit, tbuf);
    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
    ShowWindow(hwnd, SW_HIDE);
}

static LRESULT CALLBACK BpmEditSubProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) { SendMessageA(GetParent(h), WM_APP + 1, 1, 0); return 0; }
        if (wParam == VK_ESCAPE) { SendMessageA(GetParent(h), WM_APP + 1, 0, 0); return 0; }
    }
    return CallWindowProcA(g_bpmEditDefaultProc, h, msg, wParam, lParam);
}

static LRESULT CALLBACK BpmWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HBRUSH s_hbrEditBg = NULL;

    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC hdcEdit = (HDC)wParam;
            HWND hwndCtrl = (HWND)lParam;
            if (hwndCtrl == g_bpmEdit) {
                SetTextColor(hdcEdit, RGB(225, 235, 245));
                SetBkColor(hdcEdit, RGB(24, 28, 38));
                if (!s_hbrEditBg) s_hbrEditBg = CreateSolidBrush(RGB(24, 28, 38));
                return (LRESULT)s_hbrEditBg;
            }
            break;
        }

        case WM_COMMAND:
             
            if (HIWORD(wParam) == EN_UPDATE && g_bpmEdit) {
                char buf[32] = { 0 };
                GetWindowTextA(g_bpmEdit, buf, sizeof(buf));
                float v = (float)atof(buf);
                if (v >= 40.0f && v <= 300.0f) {
                    g_Seq.bpm = v;
                    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                }
            }
            return 0;

        case WM_APP + 1:
            if (wParam) {
                bpm_edit_commit(hwnd);
            } else {
                g_Seq.bpm = g_bpmOriginal;    
                if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                ShowWindow(hwnd, SW_HIDE);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HFONT oldFontMain = SELECT_UI_FONT(hdc);

            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left, h = rc.bottom - rc.top;

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
            HFONT oldFontMem = SELECT_UI_FONT(memDC);

            HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
            FillRect(memDC, &rc, bg);
            DeleteObject(bg);

            SetBkMode(memDC, TRANSPARENT);

             
            SetTextColor(memDC, RGB(180, 220, 245));
            RECT lblRc = { 0, 14, w, 34 };
            DrawTextA(memDC, "SET TEMPO (BPM)", -1, &lblRc, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

             
            int editW = 180, editH = 24;
            int editX = (w - editW) / 2;
            int editY = 44;
            RECT editFrameRc = { editX - 6, editY - 6, editX + editW + 6, editY + editH + 9 };
            HBRUSH fBg = CreateSolidBrush(RGB(24, 28, 38));
            HPEN fPn = CreatePen(PS_SOLID, 1, RGB(55, 68, 88));
            HGDIOBJ ob = SelectObject(memDC, fBg);
            HGDIOBJ op = SelectObject(memDC, fPn);
            RoundRect(memDC, editFrameRc.left, editFrameRc.top, editFrameRc.right, editFrameRc.bottom, 4, 4);
            SelectObject(memDC, op);
            SelectObject(memDC, ob);
            DeleteObject(fPn);
            DeleteObject(fBg);

             
            SetTextColor(memDC, RGB(130, 145, 165));
            RECT rangeRc = { 0, 84, w, 108 };
            DrawTextA(memDC, "Range: 40.00 - 300.00 BPM", -1, &rangeRc, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

             
            int btnW = 120, btnH = 28;
            int btnY = h - 42;
            int gap = 14;
            int okX = w / 2 - btnW - gap / 2;
            int noX = w / 2 + gap / 2;

             
            RECT okRc = { okX, btnY, okX + btnW, btnY + btnH };
            HBRUSH okBg = CreateSolidBrush(RGB(22, 90, 55));
            HPEN okPn = CreatePen(PS_SOLID, 1, RGB(80, 240, 180));
            ob = SelectObject(memDC, okBg);
            op = SelectObject(memDC, okPn);
            RoundRect(memDC, okRc.left, okRc.top, okRc.right, okRc.bottom, 4, 4);
            SelectObject(memDC, op);
            SelectObject(memDC, ob);
            DeleteObject(okPn); DeleteObject(okBg);
            SetTextColor(memDC, RGB(160, 255, 205));
            DrawTextA(memDC, "APPLY [ENTER]", -1, &okRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

             
            RECT noRc = { noX, btnY, noX + btnW, btnY + btnH };
            HBRUSH noBg = CreateSolidBrush(RGB(60, 32, 32));
            HPEN noPn = CreatePen(PS_SOLID, 1, RGB(220, 100, 100));
            ob = SelectObject(memDC, noBg);
            op = SelectObject(memDC, noPn);
            RoundRect(memDC, noRc.left, noRc.top, noRc.right, noRc.bottom, 4, 4);
            SelectObject(memDC, op);
            SelectObject(memDC, ob);
            DeleteObject(noPn); DeleteObject(noBg);
            SetTextColor(memDC, RGB(255, 190, 190));
            DrawTextA(memDC, "CANCEL [ESC]", -1, &noRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldFontMem);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            SelectObject(hdc, oldFontMain);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left, h = rc.bottom - rc.top;
            int btnW = 120, btnH = 28;
            int btnY = h - 42;
            int gap = 14;
            int okX = w / 2 - btnW - gap / 2;
            int noX = w / 2 + gap / 2;

            if (my >= btnY && my <= btnY + btnH) {
                if (mx >= okX && mx <= okX + btnW) {
                    bpm_edit_commit(hwnd);
                    return 0;
                }
                if (mx >= noX && mx <= noX + btnW) {
                    g_Seq.bpm = g_bpmOriginal;
                    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                    ShowWindow(hwnd, SW_HIDE);
                    return 0;
                }
            }
            return 0;
        }

        case WM_CLOSE:
            g_Seq.bpm = g_bpmOriginal;
            if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            if (s_hbrEditBg) {
                DeleteObject(s_hbrEditBg);
                s_hbrEditBg = NULL;
            }
            g_bpmHwnd = NULL;
            g_bpmEdit = NULL;
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static inline void open_bpm_dialog(HWND parentHwnd) {
    g_bpmOriginal = g_Seq.bpm;

     
    int rw = 400, rh = 190;
    int scrW = GetSystemMetrics(SM_CXSCREEN), scrH = GetSystemMetrics(SM_CYSCREEN);
    int rx = (scrW - rw) / 2, ry = (scrH - rh) / 2;
    if (parentHwnd && IsWindow(parentHwnd)) {
        RECT prc;
        GetWindowRect(parentHwnd, &prc);
        rx = prc.left + ((prc.right - prc.left) - rw) / 2;
        ry = prc.top + ((prc.bottom - prc.top) - rh) / 2;
    }

    if (!g_bpmHwnd || !IsWindow(g_bpmHwnd)) {
        static bool s_registered = false;
        if (!s_registered) {
            WNDCLASSA wc = { 0 };
            wc.lpfnWndProc = BpmWndProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractBpmClass";
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_registered = true;
        }

        g_bpmHwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            "RefractBpmClass", "Set Tempo",
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE,
            rx, ry, rw, rh,
            parentHwnd, NULL, GetModuleHandle(NULL), NULL
        );

        g_bpmEdit = CreateWindowExA(
            0, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_AUTOHSCROLL,
            85, 44, 180, 24,
            g_bpmHwnd, (HMENU)(INT_PTR)1001, GetModuleHandle(NULL), NULL
        );
        SendMessageA(g_bpmEdit, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
        g_bpmEditDefaultProc = (WNDPROC)SetWindowLongPtrA(g_bpmEdit, GWLP_WNDPROC, (LONG_PTR)BpmEditSubProc);
        SendMessageA(g_bpmEdit, EM_SETLIMITTEXT, 12, 0);
    } else {
        SetWindowPos(g_bpmHwnd, HWND_TOPMOST, rx, ry, rw, rh, SWP_SHOWWINDOW);
    }

    RECT crc;
    GetClientRect(g_bpmHwnd, &crc);
    int editW = 180, editH = 30;
    int editX = ((crc.right - crc.left) - editW) / 2;
    int editY = 44;
    SetWindowPos(g_bpmEdit, NULL, editX, editY, editW, editH, SWP_NOZORDER | SWP_SHOWWINDOW);

    char valBuf[32];
    snprintf(valBuf, sizeof(valBuf), "%.2f", g_Seq.bpm);
    SetWindowTextA(g_bpmEdit, valBuf);
    ShowWindow(g_bpmHwnd, SW_SHOW);
    SetForegroundWindow(g_bpmHwnd);
    SetFocus(g_bpmEdit);
    SendMessageA(g_bpmEdit, EM_SETSEL, 0, -1);
    InvalidateRect(g_bpmHwnd, NULL, FALSE);
}

 

static HWND    g_swingHwnd = NULL;
static HWND    g_swingEdit = NULL;
static WNDPROC g_swingEditDefaultProc = NULL;
static float   g_swingOriginal = 0.0f;

static inline void swing_edit_commit(HWND hwnd) {
    char buf[32] = { 0 };
    if (g_swingEdit && IsWindow(g_swingEdit)) {
        GetWindowTextA(g_swingEdit, buf, sizeof(buf));
    }
    int v = atoi(buf);
    if (v < 0) v = 0;
    if (v > 95) v = 95;
    g_Seq.swing = (float)v * 0.01f;
    char tbuf[32];
    snprintf(tbuf, sizeof(tbuf), "%d", v);
    if (g_swingEdit && IsWindow(g_swingEdit)) SetWindowTextA(g_swingEdit, tbuf);
    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
    ShowWindow(hwnd, SW_HIDE);
}

static LRESULT CALLBACK SwingEditSubProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) { SendMessageA(GetParent(h), WM_APP + 1, 1, 0); return 0; }
        if (wParam == VK_ESCAPE) { SendMessageA(GetParent(h), WM_APP + 1, 0, 0); return 0; }
    }
    return CallWindowProcA(g_swingEditDefaultProc, h, msg, wParam, lParam);
}

static LRESULT CALLBACK SwingWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HBRUSH s_hbrEditBg = NULL;

    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC hdcEdit = (HDC)wParam;
            HWND hwndCtrl = (HWND)lParam;
            if (hwndCtrl == g_swingEdit) {
                SetTextColor(hdcEdit, RGB(225, 235, 245));
                SetBkColor(hdcEdit, RGB(24, 28, 38));
                if (!s_hbrEditBg) s_hbrEditBg = CreateSolidBrush(RGB(24, 28, 38));
                return (LRESULT)s_hbrEditBg;
            }
            break;
        }

        case WM_COMMAND:
            if (HIWORD(wParam) == EN_UPDATE && g_swingEdit) {
                char buf[32] = { 0 };
                GetWindowTextA(g_swingEdit, buf, sizeof(buf));
                int v = atoi(buf);
                if (v >= 0 && v <= 95) {
                    g_Seq.swing = (float)v * 0.01f;
                    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                }
            }
            return 0;

        case WM_APP + 1:
            if (wParam) {
                swing_edit_commit(hwnd);
            } else {
                g_Seq.swing = g_swingOriginal;
                if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                ShowWindow(hwnd, SW_HIDE);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HFONT oldFontMain = SELECT_UI_FONT(hdc);

            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left, h = rc.bottom - rc.top;

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
            HFONT oldFontMem = SELECT_UI_FONT(memDC);

            HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
            FillRect(memDC, &rc, bg);
            DeleteObject(bg);

            SetBkMode(memDC, TRANSPARENT);

             
            SetTextColor(memDC, RGB(180, 220, 245));
            RECT lblRc = { 0, 14, w, 34 };
            DrawTextA(memDC, "SET SWING (%)", -1, &lblRc, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

             
            int editW = 180, editH = 24;
            int editX = (w - editW) / 2;
            int editY = 44;
            RECT editFrameRc = { editX - 6, editY - 6, editX + editW + 6, editY + editH + 9 };
            HBRUSH fBg = CreateSolidBrush(RGB(24, 28, 38));
            HPEN fPn = CreatePen(PS_SOLID, 1, RGB(55, 68, 88));
            HGDIOBJ ob = SelectObject(memDC, fBg);
            HGDIOBJ op = SelectObject(memDC, fPn);
            RoundRect(memDC, editFrameRc.left, editFrameRc.top, editFrameRc.right, editFrameRc.bottom, 4, 4);
            SelectObject(memDC, op);
            SelectObject(memDC, ob);
            DeleteObject(fPn);
            DeleteObject(fBg);

             
            SetTextColor(memDC, RGB(130, 145, 165));
            RECT rangeRc = { 0, 84, w, 108 };
            DrawTextA(memDC, "Range: 0 - 95%", -1, &rangeRc, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

             
            int btnW = 120, btnH = 28;
            int btnY = h - 42;
            int gap = 14;
            int okX = w / 2 - btnW - gap / 2;
            int noX = w / 2 + gap / 2;

             
            RECT okRc = { okX, btnY, okX + btnW, btnY + btnH };
            HBRUSH okBg = CreateSolidBrush(RGB(22, 90, 55));
            HPEN okPn = CreatePen(PS_SOLID, 1, RGB(80, 240, 180));
            ob = SelectObject(memDC, okBg);
            op = SelectObject(memDC, okPn);
            RoundRect(memDC, okRc.left, okRc.top, okRc.right, okRc.bottom, 4, 4);
            SelectObject(memDC, op);
            SelectObject(memDC, ob);
            DeleteObject(okPn); DeleteObject(okBg);
            SetTextColor(memDC, RGB(160, 255, 205));
            DrawTextA(memDC, "APPLY [ENTER]", -1, &okRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

             
            RECT noRc = { noX, btnY, noX + btnW, btnY + btnH };
            HBRUSH noBg = CreateSolidBrush(RGB(60, 32, 32));
            HPEN noPn = CreatePen(PS_SOLID, 1, RGB(220, 100, 100));
            ob = SelectObject(memDC, noBg);
            op = SelectObject(memDC, noPn);
            RoundRect(memDC, noRc.left, noRc.top, noRc.right, noRc.bottom, 4, 4);
            SelectObject(memDC, op);
            SelectObject(memDC, ob);
            DeleteObject(noPn); DeleteObject(noBg);
            SetTextColor(memDC, RGB(255, 190, 190));
            DrawTextA(memDC, "CANCEL [ESC]", -1, &noRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldFontMem);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            SelectObject(hdc, oldFontMain);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left, h = rc.bottom - rc.top;
            int btnW = 120, btnH = 28;
            int btnY = h - 42;
            int gap = 14;
            int okX = w / 2 - btnW - gap / 2;
            int noX = w / 2 + gap / 2;

            if (my >= btnY && my <= btnY + btnH) {
                if (mx >= okX && mx <= okX + btnW) {
                    swing_edit_commit(hwnd);
                    return 0;
                }
                if (mx >= noX && mx <= noX + btnW) {
                    g_Seq.swing = g_swingOriginal;
                    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                    ShowWindow(hwnd, SW_HIDE);
                    return 0;
                }
            }
            return 0;
        }

        case WM_CLOSE:
            g_Seq.swing = g_swingOriginal;
            if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            if (s_hbrEditBg) {
                DeleteObject(s_hbrEditBg);
                s_hbrEditBg = NULL;
            }
            g_swingHwnd = NULL;
            g_swingEdit = NULL;
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static inline void open_swing_dialog(HWND parentHwnd) {
    g_swingOriginal = g_Seq.swing;

     
    int rw = 400, rh = 190;
    int scrW = GetSystemMetrics(SM_CXSCREEN), scrH = GetSystemMetrics(SM_CYSCREEN);
    int rx = (scrW - rw) / 2, ry = (scrH - rh) / 2;
    if (parentHwnd && IsWindow(parentHwnd)) {
        RECT prc;
        GetWindowRect(parentHwnd, &prc);
        rx = prc.left + ((prc.right - prc.left) - rw) / 2;
        ry = prc.top + ((prc.bottom - prc.top) - rh) / 2;
    }

    if (!g_swingHwnd || !IsWindow(g_swingHwnd)) {
        static bool s_registered = false;
        if (!s_registered) {
            WNDCLASSA wc = { 0 };
            wc.lpfnWndProc = SwingWndProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractSwingClass";
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_registered = true;
        }

        g_swingHwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            "RefractSwingClass", "Set Swing",
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE,
            rx, ry, rw, rh,
            parentHwnd, NULL, GetModuleHandle(NULL), NULL
        );

        g_swingEdit = CreateWindowExA(
            0, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_NUMBER | ES_AUTOHSCROLL,
            85, 44, 180, 24,
            g_swingHwnd, (HMENU)(INT_PTR)1003, GetModuleHandle(NULL), NULL
        );
        SendMessageA(g_swingEdit, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
        g_swingEditDefaultProc = (WNDPROC)SetWindowLongPtrA(g_swingEdit, GWLP_WNDPROC, (LONG_PTR)SwingEditSubProc);
        SendMessageA(g_swingEdit, EM_SETLIMITTEXT, 3, 0);
    } else {
        SetWindowPos(g_swingHwnd, HWND_TOPMOST, rx, ry, rw, rh, SWP_SHOWWINDOW);
    }

    RECT crc;
    GetClientRect(g_swingHwnd, &crc);
    int editW = 180, editH = 24;
    int editX = ((crc.right - crc.left) - editW) / 2;
    int editY = 44;
    SetWindowPos(g_swingEdit, NULL, editX, editY, editW, editH, SWP_NOZORDER | SWP_SHOWWINDOW);

    char valBuf[32];
    snprintf(valBuf, sizeof(valBuf), "%d", (int)(g_Seq.swing * 100.0f + 0.5f));
    SetWindowTextA(g_swingEdit, valBuf);
    ShowWindow(g_swingHwnd, SW_SHOW);
    SetForegroundWindow(g_swingHwnd);
    SetFocus(g_swingEdit);
    SendMessageA(g_swingEdit, EM_SETSEL, 0, -1);
    InvalidateRect(g_swingHwnd, NULL, FALSE);
}

 
static HWND    g_tsHwnd = NULL;
static HWND    g_tsNumEdit = NULL;
static HWND    g_tsDenEdit = NULL;
static WNDPROC g_tsNumEditDefaultProc = NULL;
static WNDPROC g_tsDenEditDefaultProc = NULL;
static int     g_tsOriginalNum = 4;
static int     g_tsOriginalDen = 4;

static inline void timesig_apply_rebuild(void) {
    cseq_clip_structure_changed();
    mark_all_bars_dirty();
    invalidate_timeline_cache();

    double beats = total_beats();
    double fpb = frames_per_beat(g_Seq.bpm);
    if (beats > 0.0 && fpb > 0.0) {
        double total = beats * fpb;
        if (total > (double)UINT64_MAX) total = (double)UINT64_MAX;
        ma_uint64 loopTotalFrames = (ma_uint64)total;
        if (loopTotalFrames > 0) {
            LONG cur = InterlockedCompareExchange(&g_Seq.playbackFrame, 0, 0);
            InterlockedExchange(&g_Seq.playbackFrame, (LONG)((ma_uint64)cur % loopTotalFrames));
        }
    }
    if (g_hWnd) {
        update_scrollbar(g_hWnd);
        InvalidateRect(g_hWnd, NULL, FALSE);
    }
}

static inline void timesig_edit_commit(HWND hwnd) {
    char numBuf[16] = { 0 }, denBuf[16] = { 0 };
    if (g_tsNumEdit && IsWindow(g_tsNumEdit)) GetWindowTextA(g_tsNumEdit, numBuf, sizeof(numBuf));
    if (g_tsDenEdit && IsWindow(g_tsDenEdit)) GetWindowTextA(g_tsDenEdit, denBuf, sizeof(denBuf));
    int n = atoi(numBuf);
    int d = atoi(denBuf);
    if (n < 1) n = 1;
    if (d < 1) d = 1;
    if (n > 32) n = 32;
    if (d > 32) d = 32;
    g_Seq.timeSigNum = n;
    g_Seq.timeSigDen = d;
    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%d", n);
    if (g_tsNumEdit && IsWindow(g_tsNumEdit)) SetWindowTextA(g_tsNumEdit, tbuf);
    snprintf(tbuf, sizeof(tbuf), "%d", d);
    if (g_tsDenEdit && IsWindow(g_tsDenEdit)) SetWindowTextA(g_tsDenEdit, tbuf);
    timesig_apply_rebuild();
    ShowWindow(hwnd, SW_HIDE);
}

static LRESULT CALLBACK TimesigNumEditSubProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) { SendMessageA(GetParent(h), WM_APP + 1, 1, 0); return 0; }
        if (wParam == VK_ESCAPE) { SendMessageA(GetParent(h), WM_APP + 1, 0, 0); return 0; }
    }
    return CallWindowProcA(g_tsNumEditDefaultProc, h, msg, wParam, lParam);
}

static LRESULT CALLBACK TimesigDenEditSubProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) { SendMessageA(GetParent(h), WM_APP + 1, 1, 0); return 0; }
        if (wParam == VK_ESCAPE) { SendMessageA(GetParent(h), WM_APP + 1, 0, 0); return 0; }
    }
    return CallWindowProcA(g_tsDenEditDefaultProc, h, msg, wParam, lParam);
}

static LRESULT CALLBACK TimesigWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HBRUSH s_hbrEditBg = NULL;

    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC hdcEdit = (HDC)wParam;
            HWND hwndCtrl = (HWND)lParam;
            if (hwndCtrl == g_tsNumEdit || hwndCtrl == g_tsDenEdit) {
                SetTextColor(hdcEdit, RGB(225, 235, 245));
                SetBkColor(hdcEdit, RGB(24, 28, 38));
                if (!s_hbrEditBg) s_hbrEditBg = CreateSolidBrush(RGB(24, 28, 38));
                return (LRESULT)s_hbrEditBg;
            }
            break;
        }

        case WM_COMMAND:
            if (HIWORD(wParam) == EN_UPDATE && (g_tsNumEdit || g_tsDenEdit)) {
                char numBuf[16] = { 0 }, denBuf[16] = { 0 };
                if (g_tsNumEdit && IsWindow(g_tsNumEdit)) GetWindowTextA(g_tsNumEdit, numBuf, sizeof(numBuf));
                if (g_tsDenEdit && IsWindow(g_tsDenEdit)) GetWindowTextA(g_tsDenEdit, denBuf, sizeof(denBuf));
                int n = atoi(numBuf);
                int d = atoi(denBuf);
                if (n >= 1 && d >= 1) {
                    g_Seq.timeSigNum = n;
                    g_Seq.timeSigDen = d;
                    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                }
            }
            return 0;

        case WM_APP + 1:
            if (wParam) {
                timesig_edit_commit(hwnd);
            } else {
                g_Seq.timeSigNum = g_tsOriginalNum;
                g_Seq.timeSigDen = g_tsOriginalDen;
                timesig_apply_rebuild();
                ShowWindow(hwnd, SW_HIDE);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HFONT oldFontMain = SELECT_UI_FONT(hdc);

            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left, h = rc.bottom - rc.top;

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
            HFONT oldFontMem = SELECT_UI_FONT(memDC);

            HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
            FillRect(memDC, &rc, bg);
            DeleteObject(bg);

            SetBkMode(memDC, TRANSPARENT);

             
            SetTextColor(memDC, RGB(180, 220, 245));
            RECT lblRc = { 0, 14, w, 34 };
            DrawTextA(memDC, "SET TIME SIGNATURE", -1, &lblRc, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);


            // Same rhythm as the tempo dialog, with room for the title: entry
            // row at 62 (labels in the 36..52 band below the title), range
            // line under the frames, buttons pinned to h-42.
            int editW = 120, editH = 24;
            int gapX = scale_x(16);
            int totalW = editW * 2 + gapX;
            int editX0 = (w - totalW) / 2;
            int editX1 = editX0 + editW + gapX;
            int editY = 62;

            RECT frame0 = { editX0 - 6, editY - 6, editX0 + editW + 6, editY + editH + 9 };
            RECT frame1 = { editX1 - 6, editY - 6, editX1 + editW + 6, editY + editH + 9 };
            HBRUSH fBg = CreateSolidBrush(RGB(24, 28, 38));
            HPEN fPn = CreatePen(PS_SOLID, 1, RGB(55, 68, 88));
            HGDIOBJ ob = SelectObject(memDC, fBg);
            HGDIOBJ op = SelectObject(memDC, fPn);
            RoundRect(memDC, frame0.left, frame0.top, frame0.right, frame0.bottom, 4, 4);
            RoundRect(memDC, frame1.left, frame1.top, frame1.right, frame1.bottom, 4, 4);
            SelectObject(memDC, op);
            SelectObject(memDC, ob);
            DeleteObject(fPn);
            DeleteObject(fBg);

            // Labels in a band fully above the entry frames (no overlap).
            SetTextColor(memDC, RGB(130, 145, 165));
            RECT numLbl = { editX0, editY - 26, editX0 + editW, editY - 10 };
            DrawTextA(memDC, "Beats/Bar", -1, &numLbl, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
            RECT denLbl = { editX1, editY - 26, editX1 + editW, editY - 10 };
            DrawTextA(memDC, "Note Value", -1, &denLbl, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Range line sits just below the entry frames.
            SetTextColor(memDC, RGB(130, 145, 165));
            RECT rangeRc = { 0, editY + editH + 18, w, editY + editH + 42 };
            DrawTextA(memDC, "Range: 1 - 32", -1, &rangeRc, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

             
            int btnW = 120, btnH = 28;
            int btnY = h - 42;
            int gap = 14;
            int okX = w / 2 - btnW - gap / 2;
            int noX = w / 2 + gap / 2;

             
            RECT okRc = { okX, btnY, okX + btnW, btnY + btnH };
            HBRUSH okBg = CreateSolidBrush(RGB(22, 90, 55));
            HPEN okPn = CreatePen(PS_SOLID, 1, RGB(80, 240, 180));
            ob = SelectObject(memDC, okBg);
            op = SelectObject(memDC, okPn);
            RoundRect(memDC, okRc.left, okRc.top, okRc.right, okRc.bottom, 4, 4);
            SelectObject(memDC, op);
            SelectObject(memDC, ob);
            DeleteObject(okPn); DeleteObject(okBg);
            SetTextColor(memDC, RGB(160, 255, 205));
            DrawTextA(memDC, "APPLY [ENTER]", -1, &okRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

             
            RECT noRc = { noX, btnY, noX + btnW, btnY + btnH };
            HBRUSH noBg = CreateSolidBrush(RGB(60, 32, 32));
            HPEN noPn = CreatePen(PS_SOLID, 1, RGB(220, 100, 100));
            ob = SelectObject(memDC, noBg);
            op = SelectObject(memDC, noPn);
            RoundRect(memDC, noRc.left, noRc.top, noRc.right, noRc.bottom, 4, 4);
            SelectObject(memDC, op);
            SelectObject(memDC, ob);
            DeleteObject(noPn); DeleteObject(noBg);
            SetTextColor(memDC, RGB(255, 190, 190));
            DrawTextA(memDC, "CANCEL [ESC]", -1, &noRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldFontMem);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            SelectObject(hdc, oldFontMain);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left, h = rc.bottom - rc.top;
            int btnW = 120, btnH = 28;
            int btnY = h - 42;
            int gap = 14;
            int okX = w / 2 - btnW - gap / 2;
            int noX = w / 2 + gap / 2;

            if (my >= btnY && my <= btnY + btnH) {
                if (mx >= okX && mx <= okX + btnW) {
                    timesig_edit_commit(hwnd);
                    return 0;
                }
                if (mx >= noX && mx <= noX + btnW) {
                    g_Seq.timeSigNum = g_tsOriginalNum;
                    g_Seq.timeSigDen = g_tsOriginalDen;
                    timesig_apply_rebuild();
                    ShowWindow(hwnd, SW_HIDE);
                    return 0;
                }
            }
            return 0;
        }

        case WM_CLOSE:
            g_Seq.timeSigNum = g_tsOriginalNum;
            g_Seq.timeSigDen = g_tsOriginalDen;
            timesig_apply_rebuild();
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            if (s_hbrEditBg) {
                DeleteObject(s_hbrEditBg);
                s_hbrEditBg = NULL;
            }
            g_tsHwnd = NULL;
            g_tsNumEdit = NULL;
            g_tsDenEdit = NULL;
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static inline void open_timesig_dialog(HWND parentHwnd) {
    g_tsOriginalNum = g_Seq.timeSigNum > 0 ? g_Seq.timeSigNum : 4;
    g_tsOriginalDen = g_Seq.timeSigDen > 0 ? g_Seq.timeSigDen : 4;

    // Same proportions as the tempo modal, +18px for the title band.
    int rw = 400, rh = 208;
    int scrW = GetSystemMetrics(SM_CXSCREEN), scrH = GetSystemMetrics(SM_CYSCREEN);
    int rx = (scrW - rw) / 2, ry = (scrH - rh) / 2;
    if (parentHwnd && IsWindow(parentHwnd)) {
        RECT prc;
        GetWindowRect(parentHwnd, &prc);
        rx = prc.left + ((prc.right - prc.left) - rw) / 2;
        ry = prc.top + ((prc.bottom - prc.top) - rh) / 2;
    }

    if (!g_tsHwnd || !IsWindow(g_tsHwnd)) {
        static bool s_registered = false;
        if (!s_registered) {
            WNDCLASSA wc = { 0 };
            wc.lpfnWndProc = TimesigWndProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractTimesigClass";
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_registered = true;
        }

        g_tsHwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            "RefractTimesigClass", "Set Time Signature",
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE,
            rx, ry, rw, rh,
            parentHwnd, NULL, GetModuleHandle(NULL), NULL
        );

        int editW = 120, editH = 24;
        int gapX = scale_x(16);
        int totalW = editW * 2 + gapX;
        int editX0 = (rw - totalW) / 2;
        int editX1 = editX0 + editW + gapX;

        g_tsNumEdit = CreateWindowExA(
            0, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_NUMBER | ES_AUTOHSCROLL,
            editX0, 62, editW, editH,
            g_tsHwnd, (HMENU)(INT_PTR)1004, GetModuleHandle(NULL), NULL
        );
        SendMessageA(g_tsNumEdit, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
        g_tsNumEditDefaultProc = (WNDPROC)SetWindowLongPtrA(g_tsNumEdit, GWLP_WNDPROC, (LONG_PTR)TimesigNumEditSubProc);
        SendMessageA(g_tsNumEdit, EM_SETLIMITTEXT, 3, 0);

        g_tsDenEdit = CreateWindowExA(
            0, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_NUMBER | ES_AUTOHSCROLL,
            editX1, 62, editW, editH,
            g_tsHwnd, (HMENU)(INT_PTR)1005, GetModuleHandle(NULL), NULL
        );
        SendMessageA(g_tsDenEdit, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
        g_tsDenEditDefaultProc = (WNDPROC)SetWindowLongPtrA(g_tsDenEdit, GWLP_WNDPROC, (LONG_PTR)TimesigDenEditSubProc);
        SendMessageA(g_tsDenEdit, EM_SETLIMITTEXT, 3, 0);
    } else {
        SetWindowPos(g_tsHwnd, HWND_TOPMOST, rx, ry, rw, rh, SWP_SHOWWINDOW);
    }

    RECT crc;
    GetClientRect(g_tsHwnd, &crc);
    int editW = 120, editH = 24;
    int gapX = scale_x(16);
    int totalW = editW * 2 + gapX;
    int editX0 = ((crc.right - crc.left) - totalW) / 2;
    int editX1 = editX0 + editW + gapX;
    SetWindowPos(g_tsNumEdit, NULL, editX0, 62, editW, editH, SWP_NOZORDER | SWP_SHOWWINDOW);
    SetWindowPos(g_tsDenEdit, NULL, editX1, 62, editW, editH, SWP_NOZORDER | SWP_SHOWWINDOW);

    char valBuf[16];
    snprintf(valBuf, sizeof(valBuf), "%d", g_Seq.timeSigNum);
    SetWindowTextA(g_tsNumEdit, valBuf);
    snprintf(valBuf, sizeof(valBuf), "%d", g_Seq.timeSigDen);
    SetWindowTextA(g_tsDenEdit, valBuf);
    ShowWindow(g_tsHwnd, SW_SHOW);
    SetForegroundWindow(g_tsHwnd);
    SetFocus(g_tsNumEdit);
    SendMessageA(g_tsNumEdit, EM_SETSEL, 0, -1);
    InvalidateRect(g_tsHwnd, NULL, FALSE);
}

static HWND    g_barsHwnd = NULL;
static HWND    g_barsEdit = NULL;
static WNDPROC g_barsEditDefaultProc = NULL;
static int     g_barsOriginal = 4;

static inline void set_clamped_bar_count(int newBars) {
    if (newBars < MIN_BARS) newBars = MIN_BARS;
    if (newBars > MAX_BARS) newBars = MAX_BARS;
     
    change_bar_count(newBars - g_Seq.visibleBarCount);
}

static inline void bars_edit_commit(HWND hwnd) {
    char buf[32] = { 0 };
    if (g_barsEdit && IsWindow(g_barsEdit)) {
        GetWindowTextA(g_barsEdit, buf, sizeof(buf));
    }
    int v = atoi(buf);
    if (v < 1) v = 1;
    if (v > MAX_BARS) v = MAX_BARS;
    set_clamped_bar_count(v);

    char tbuf[32];
    snprintf(tbuf, sizeof(tbuf), "%d", g_Seq.visibleBarCount);
    if (g_barsEdit && IsWindow(g_barsEdit)) SetWindowTextA(g_barsEdit, tbuf);
    ShowWindow(hwnd, SW_HIDE);
}

static LRESULT CALLBACK BarsEditSubProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) { SendMessageA(GetParent(h), WM_APP + 1, 1, 0); return 0; }
        if (wParam == VK_ESCAPE) { SendMessageA(GetParent(h), WM_APP + 1, 0, 0); return 0; }
    }
    return CallWindowProcA(g_barsEditDefaultProc, h, msg, wParam, lParam);
}

static LRESULT CALLBACK BarsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HBRUSH s_hbrBarsEditBg = NULL;

    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC hdcEdit = (HDC)wParam;
            HWND hwndCtrl = (HWND)lParam;
            if (hwndCtrl == g_barsEdit) {
                SetTextColor(hdcEdit, RGB(225, 235, 245));
                SetBkColor(hdcEdit, RGB(24, 28, 38));
                if (!s_hbrBarsEditBg) s_hbrBarsEditBg = CreateSolidBrush(RGB(24, 28, 38));
                return (LRESULT)s_hbrBarsEditBg;
            }
            break;
        }

        case WM_COMMAND:
            if (HIWORD(wParam) == EN_UPDATE && g_barsEdit) {
                char buf[32] = { 0 };
                GetWindowTextA(g_barsEdit, buf, sizeof(buf));
                if (buf[0] != '\0') {
                    int v = atoi(buf);
                    if (v >= 1 && v <= 1024) {
                        set_clamped_bar_count(v);
                    }
                }
            }
            return 0;

        case WM_APP + 1:
            if (wParam) {
                bars_edit_commit(hwnd);
            } else {
                set_clamped_bar_count(g_barsOriginal);
                ShowWindow(hwnd, SW_HIDE);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HFONT oldFontMain = SELECT_UI_FONT(hdc);

            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left, h = rc.bottom - rc.top;

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
            HFONT oldFontMem = SELECT_UI_FONT(memDC);

            HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
            FillRect(memDC, &rc, bg);
            DeleteObject(bg);

            SetBkMode(memDC, TRANSPARENT);

             
            SetTextColor(memDC, RGB(180, 220, 245));
            RECT lblRc = { 0, 14, w, 34 };
            DrawTextA(memDC, "SET BAR COUNT", -1, &lblRc, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

             
            int editW = 180, editH = 24;
            int editX = (w - editW) / 2;
            int editY = 44;
            RECT editFrameRc = { editX - 6, editY - 6, editX + editW + 6, editY + editH + 9 };
            HBRUSH fBg = CreateSolidBrush(RGB(24, 28, 38));
            HPEN fPn = CreatePen(PS_SOLID, 1, RGB(55, 68, 88));
            HGDIOBJ ob = SelectObject(memDC, fBg);
            HGDIOBJ op = SelectObject(memDC, fPn);
            RoundRect(memDC, editFrameRc.left, editFrameRc.top, editFrameRc.right, editFrameRc.bottom, 4, 4);
            SelectObject(memDC, op);
            SelectObject(memDC, ob);
            DeleteObject(fPn);
            DeleteObject(fBg);

             
            SetTextColor(memDC, RGB(130, 145, 165));
            RECT rangeRc = { 0, 84, w, 108 };
            DrawTextA(memDC, "Range: 1 - 1024 Bars", -1, &rangeRc, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

             
            int btnW = 120, btnH = 28;
            int btnY = h - 42;
            int gap = 14;
            int okX = w / 2 - btnW - gap / 2;
            int noX = w / 2 + gap / 2;

             
            RECT okRc = { okX, btnY, okX + btnW, btnY + btnH };
            HBRUSH okBg = CreateSolidBrush(RGB(22, 90, 55));
            HPEN okPn = CreatePen(PS_SOLID, 1, RGB(80, 240, 180));
            ob = SelectObject(memDC, okBg);
            op = SelectObject(memDC, okPn);
            RoundRect(memDC, okRc.left, okRc.top, okRc.right, okRc.bottom, 4, 4);
            SelectObject(memDC, op); SelectObject(memDC, ob);
            DeleteObject(okPn); DeleteObject(okBg);
            SetTextColor(memDC, RGB(160, 255, 205));
            DrawTextA(memDC, "APPLY [ENTER]", -1, &okRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

             
            RECT noRc = { noX, btnY, noX + btnW, btnY + btnH };
            HBRUSH noBg = CreateSolidBrush(RGB(60, 32, 32));
            HPEN noPn = CreatePen(PS_SOLID, 1, RGB(220, 100, 100));
            ob = SelectObject(memDC, noBg);
            op = SelectObject(memDC, noPn);
            RoundRect(memDC, noRc.left, noRc.top, noRc.right, noRc.bottom, 4, 4);
            SelectObject(memDC, op); SelectObject(memDC, ob);
            DeleteObject(noPn); DeleteObject(noBg);
            SetTextColor(memDC, RGB(255, 190, 190));
            DrawTextA(memDC, "CANCEL [ESC]", -1, &noRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldFontMem);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            SelectObject(hdc, oldFontMain);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left, h = rc.bottom - rc.top;
            int btnW = 120, btnH = 28;
            int btnY = h - 42;
            int gap = 14;
            int okX = w / 2 - btnW - gap / 2;
            int noX = w / 2 + gap / 2;

            if (my >= btnY && my <= btnY + btnH) {
                if (mx >= okX && mx <= okX + btnW) {
                    bars_edit_commit(hwnd);
                    return 0;
                }
                if (mx >= noX && mx <= noX + btnW) {
                    set_clamped_bar_count(g_barsOriginal);
                    ShowWindow(hwnd, SW_HIDE);
                    return 0;
                }
            }
            return 0;
        }

        case WM_CLOSE:
            set_clamped_bar_count(g_barsOriginal);
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            if (s_hbrBarsEditBg) {
                DeleteObject(s_hbrBarsEditBg);
                s_hbrBarsEditBg = NULL;
            }
            g_barsHwnd = NULL;
            g_barsEdit = NULL;
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static inline void open_bars_dialog(HWND parentHwnd) {
    g_barsOriginal = g_Seq.visibleBarCount;

     
    int rw = 400, rh = 190;
    int scrW = GetSystemMetrics(SM_CXSCREEN), scrH = GetSystemMetrics(SM_CYSCREEN);
    int rx = (scrW - rw) / 2, ry = (scrH - rh) / 2;
    if (parentHwnd && IsWindow(parentHwnd)) {
        RECT prc;
        GetWindowRect(parentHwnd, &prc);
        rx = prc.left + ((prc.right - prc.left) - rw) / 2;
        ry = prc.top + ((prc.bottom - prc.top) - rh) / 2;
    }

    if (!g_barsHwnd || !IsWindow(g_barsHwnd)) {
        static bool s_registered = false;
        if (!s_registered) {
            WNDCLASSA wc = { 0 };
            wc.lpfnWndProc = BarsWndProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractBarsClass";
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_registered = true;
        }

        g_barsHwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            "RefractBarsClass", "Set Bar Count",
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE,
            rx, ry, rw, rh,
            parentHwnd, NULL, GetModuleHandle(NULL), NULL
        );

        g_barsEdit = CreateWindowExA(
            0, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_NUMBER | ES_AUTOHSCROLL,
            85, 44, 180, 24,
            g_barsHwnd, (HMENU)(INT_PTR)1002, GetModuleHandle(NULL), NULL
        );
        SendMessageA(g_barsEdit, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
        g_barsEditDefaultProc = (WNDPROC)SetWindowLongPtrA(g_barsEdit, GWLP_WNDPROC, (LONG_PTR)BarsEditSubProc);
        SendMessageA(g_barsEdit, EM_SETLIMITTEXT, 6, 0);
    } else {
        SetWindowPos(g_barsHwnd, HWND_TOPMOST, rx, ry, rw, rh, SWP_SHOWWINDOW);
    }

    RECT crc;
    GetClientRect(g_barsHwnd, &crc);
    int editW = 180, editH = 30;
    int editX = ((crc.right - crc.left) - editW) / 2;
    int editY = 44;
    SetWindowPos(g_barsEdit, NULL, editX, editY, editW, editH, SWP_NOZORDER | SWP_SHOWWINDOW);

    char valBuf[32];
    snprintf(valBuf, sizeof(valBuf), "%d", g_Seq.visibleBarCount);
    SetWindowTextA(g_barsEdit, valBuf);
    ShowWindow(g_barsHwnd, SW_SHOW);
    SetForegroundWindow(g_barsHwnd);
    SetFocus(g_barsEdit);
    SendMessageA(g_barsEdit, EM_SETSEL, 0, -1);
    InvalidateRect(g_barsHwnd, NULL, FALSE);
}

 

#define MIDI_EDIT_BASE 60      // C4 — the roll opens centered on C4..B5
#define MIDI_EDIT_KEYS 24      

 
static inline int midi_edit_get_base_note(void) {
    int b = MIDI_EDIT_BASE + g_midiEdit.octaveShift * 12;
    if (b < 0) b = 0;
    if (b > 127 - (MIDI_EDIT_KEYS - 1)) b = 127 - (MIDI_EDIT_KEYS - 1);
    return b;
}

 

static inline Clip* midi_edit_clip(void) {
    if (g_midiEdit.clipIdx < 0 || g_midiEdit.clipIdx >= g_Seq.clipCount) return NULL;
    Clip* c = &g_Seq.clips[g_midiEdit.clipIdx];
    if (!c->isMidi) return NULL;
    return c;
}

// --- Per-kind editor behavior (standard MIDI / Quadrum drums / Halo synth) ---
// Quadrum: 8 fixed drum voices, pitch field stores the voice index 0-7, no
// octaves. Halo: traditional roll, sample/soundfont UI hidden. Both re-tint
// the editor in their brand accent (cyan / orange).
#define MIDI_EDIT_KIND_MIDI    0
#define MIDI_EDIT_KIND_QUADRUM 1
#define MIDI_EDIT_KIND_HALO    2

// Quadrum voice names (from the synthsource quadrum engine VOICE_NAMES).
static const char* kQuadrumVoiceNames[8] = {
    "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Tom", "Cowbell", "Cymbal"
};

static inline int midi_edit_kind(void) {
    return g_midiEdit.editKind;
}

static inline bool midi_edit_is_quadrum(void) { return g_midiEdit.editKind == MIDI_EDIT_KIND_QUADRUM; }
static inline bool midi_edit_is_synth_kind(void) {
    return g_midiEdit.editKind == MIDI_EDIT_KIND_QUADRUM ||
           g_midiEdit.editKind == MIDI_EDIT_KIND_HALO;
}

// Effective row count / base pitch per kind.
static inline int midi_edit_key_count(void) {
    return midi_edit_is_quadrum() ? 8 : MIDI_EDIT_KEYS;
}
static inline int midi_edit_base_pitch(void) {
    return midi_edit_is_quadrum() ? 0 : midi_edit_get_base_note();
}

// Brand accent for the current kind: MIDI purple, Quadrum cyan, Halo orange.
static inline COLORREF midi_edit_accent(void) {
    if (midi_edit_is_quadrum()) return RGB(56, 194, 224);
    if (g_midiEdit.editKind == MIDI_EDIT_KIND_HALO) return RGB(255, 140, 25);
    return RGB(180, 140, 255);
}

static inline int midi_edit_row_to_pitch(int row) {
    return midi_edit_base_pitch() + (midi_edit_key_count() - 1 - row);
}

static inline int midi_edit_pitch_to_row(int pitch) {
    return (midi_edit_key_count() - 1) - (pitch - midi_edit_base_pitch());
}

static inline void midi_edit_geom(HWND hwnd, int *keysX, int *keysW, int *gridX, int *gridW, int *rollY, int *rollH) {
    RECT rc; GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    *keysX = 14;
    // The Quadrum drum strip shows voice names ("Closed Hat", "Open Hat", ...)
    // so it needs more room than the single-character piano keys.
    *keysW = midi_edit_is_quadrum() ? scale_x(74) : 46;
    *gridX = *keysX + *keysW + 2;
    *gridW = w - 14 - *gridX;
    if (*gridW < 10) *gridW = 10;
    *rollY = scale_y(56);
    *rollH = h - *rollY - 32;
    if (*rollH < 10) *rollH = 10;
}

// Shared toolbar button geometry for the piano-roll editor. One layout is used
// by the paint path, the click handler and the drop handler so the hit rects
// can never drift from what is drawn. PLAY/GEN are compact pills; the
// sample/soundfont source buttons only exist for non-synth clips.
#define MIDI_TOOL_PLAY_W 56
#define MIDI_TOOL_GEN_W  56
#define MIDI_TOOL_GAP    6
static inline void midi_edit_toolbar_rects(RECT* playRc, RECT* genRc,
                                           RECT* sampRc, RECT* sfRc,
                                           RECT* instRc, RECT* sfXRc) {
    const int top = 8, bot = 30;
    int x = 14;
    if (playRc) { SetRect(playRc, x, top, x + MIDI_TOOL_PLAY_W, bot); x += MIDI_TOOL_PLAY_W + MIDI_TOOL_GAP; }
    if (genRc)  { SetRect(genRc,  x, top, x + MIDI_TOOL_GEN_W,  bot); x += MIDI_TOOL_GEN_W  + MIDI_TOOL_GAP; }
    if (sampRc) { SetRect(sampRc, x, top, x + 160, bot); x += 160 + MIDI_TOOL_GAP; }
    if (sfRc)   { SetRect(sfRc,   x, top, x + 160, bot); x += 160 + MIDI_TOOL_GAP; }
    if (instRc) { SetRect(instRc, x, top, x + 24,  bot); x += 24  + MIDI_TOOL_GAP; }
    if (sfXRc)  { SetRect(sfXRc,  x, top, x + 24,  bot); }
}

// The "SYNTH" button that reopens the synth interface, shown only for synth
// clips. Sits immediately after the GEN button.
static inline void midi_edit_synth_btn_rect(RECT* out) {
    const int top = 8, bot = 30;
    int x = 14 + MIDI_TOOL_PLAY_W + MIDI_TOOL_GAP + MIDI_TOOL_GEN_W + MIDI_TOOL_GAP;
    SetRect(out, x, top, x + 64, bot);
}

static const char* kMidiNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

static inline void midi_edit_get_note_name(int midiNote, char* outBuf, size_t bufSize) {
    if (midiNote < 0) midiNote = 0;
    if (midiNote > 127) midiNote = 127;
    snprintf(outBuf, bufSize, "%s%d", kMidiNoteNames[midiNote % 12], (midiNote / 12) - 1);
}

 
static const char kMidiHintText[] =
    "[L/R Drag] Select | [Click] Add/Delete | [Wheel] Velocity | [ESC] Close";

// "Keyboard" audition toggle (bottom-bar button right of the Octave button).
// Off by default; when on, QWERTY keys sound notes while the window is
// focused. Mouse strip audition and keyboard audition share one polyphonic
// held-note set (MidiEditContext.auditionNotes, union built in types.h), so
// both can sound up to MIDI_KB_MAX notes at once.
static bool g_midiKbMode = false;

 
static HDC     g_midiCacheDC      = NULL;
static HBITMAP g_midiCacheBmp     = NULL;
static HBITMAP g_midiCacheOldBmp  = NULL;
static int     g_midiCacheW       = 0;
static int     g_midiCacheH       = 0;
static bool    g_midiCacheInvalid = true;

static inline void invalidate_midi_cache(void) {
    g_midiCacheInvalid = true;
}

static inline bool is_midi_piano_roll_dirty(int w, int h, const Clip* c, int baseNote) {
    static DWORD s_lastMidiHash = 0;
    static int   s_lastW = 0, s_lastH = 0;

    if (w != s_lastW || h != s_lastH || g_midiCacheInvalid) {
        s_lastW = w;
        s_lastH = h;
        return true;
    }
    if (!c) return false;

    DWORD hsh = 2166136261u;
    hsh = hash_dword(hsh, (DWORD)baseNote);
    hsh = hash_dword(hsh, (DWORD)c->sampleIndex);
    hsh = hash_float(hsh, c->lengthBeats);
    hsh = hash_dword(hsh, (DWORD)c->midiNoteCount);
    hsh = hash_dword(hsh, (DWORD)g_Seq.gridDivision);

    for (int i = 0; i < c->midiNoteCount && i < MIDI_MAX_NOTES; ++i) {
        const MidiNote* n = &c->midiNotes[i];
        hsh = hash_dword(hsh, (DWORD)n->pitch);
        hsh = hash_float(hsh, n->startBeat);
        hsh = hash_float(hsh, n->lengthBeats);
        hsh = hash_float(hsh, n->velocity);
        hsh = hash_dword(hsh, n->active ? 1u : 0u);
        hsh = hash_dword(hsh, n->isSelected ? 1u : 0u);
    }

    if (hsh != s_lastMidiHash) {
        s_lastMidiHash = hsh;
        return true;
    }
    return false;
}

 
static inline int midi_edit_get_note_under_mouse(const Clip* c, int mx, int my, int gridX, int gridW, int rollY, int rollH, int* outEdge) {
    if (!c || c->midiNoteCount <= 0) return -1;
    float rowH = (float)rollH / (float)midi_edit_key_count();
    float ppb = (float)gridW / (c->lengthBeats > 0.01f ? c->lengthBeats : 0.01f);
    if (outEdge) *outEdge = 0;

    for (int i = c->midiNoteCount - 1; i >= 0; --i) {
        const MidiNote* n = &c->midiNotes[i];
        int row = midi_edit_pitch_to_row(n->pitch);
        if (row < 0 || row >= midi_edit_key_count()) continue;

        int nx1 = gridX + (int)(n->startBeat * ppb);
        int nw = (int)(n->lengthBeats * ppb);
        // Minimum *click* width: very short notes render at 6px, but the grab
        // box stays comfortably wide so they don't become impossible to hit.
        if (nw < 8) nw = 8;
        int nx2 = nx1 + nw;
        int ny1 = rollY + (int)(row * rowH) + 1;
        int ny2 = rollY + (int)((row + 1) * rowH) - 1;

        if (mx >= nx1 - 6 && mx <= nx2 + 6 && my >= ny1 && my <= ny2) {
            if (outEdge) {
                // Resize handles only take the outer few pixels so the note
                // body stays grabbable for moving instead of every near-edge
                // click starting a resize.
                int edgeW = (nw >= 16) ? 4 : 3;
                if (mx < nx1 + edgeW) *outEdge = 1;
                else if (mx > nx2 - edgeW) *outEdge = 2;
                else *outEdge = 0;
            }
            return i;
        }
    }
    return -1;
}

 

static inline void midi_edit_clear_selection(Clip* c) {
    if (!c) return;
    for (int i = 0; i < c->midiNoteCount; ++i) c->midiNotes[i].isSelected = false;
}

static inline void midi_edit_select_all_notes(Clip* c) {
    if (!c) return;
    for (int i = 0; i < c->midiNoteCount; ++i) c->midiNotes[i].isSelected = true;
}

static inline int midi_edit_selected_count(const Clip* c) {
    if (!c) return 0;
    int n = 0;
    for (int i = 0; i < c->midiNoteCount; ++i)
        if (c->midiNotes[i].isSelected) n++;
    return n;
}

 
static inline void midi_edit_snap_selection_origins(Clip* c) {
    if (!c) return;
    for (int i = 0; i < c->midiNoteCount; ++i) {
        MidiNote* n = &c->midiNotes[i];
        if (!n->isSelected) continue;
        n->dragStartBeatOrig = n->startBeat;
        n->dragLengthOrig    = n->lengthBeats;
        n->dragPitchOrig     = n->pitch;
    }
}

static inline void midi_edit_delete_selected(Clip* c) {
    if (!c) return;
    int w = 0;
    for (int i = 0; i < c->midiNoteCount; ++i) {
        if (!c->midiNotes[i].isSelected) c->midiNotes[w++] = c->midiNotes[i];
    }
    c->midiNoteCount = w;
    midi_lock();
    g_midiEdit.selNote = -1;
    g_midiEdit.dragNote = -1;
    midi_unlock();
    g_timelineDirty = true;
    invalidate_midi_cache();
}

static inline void midi_edit_copy_selected(Clip* c) {
    midi_lock();
    g_midiEdit.copyCount = 0;
    if (!c) { midi_unlock(); return; }
    float minStart = 1e9f;
    for (int i = 0; i < c->midiNoteCount; ++i) {
        if (c->midiNotes[i].isSelected && c->midiNotes[i].startBeat < minStart)
            minStart = c->midiNotes[i].startBeat;
    }
    if (minStart > 1e8f) minStart = 0.0f;

    int cnt = 0;
    for (int i = 0; i < c->midiNoteCount && cnt < MIDI_MAX_NOTES; ++i) {
        if (!c->midiNotes[i].isSelected) continue;
        g_midiEdit.copyNotes[cnt] = c->midiNotes[i];
        g_midiEdit.copyNotes[cnt].startBeat -= minStart;
        g_midiEdit.copyNotes[cnt].isSelected = false;
        cnt++;
    }
    g_midiEdit.copyCount = cnt;
    midi_unlock();
}

static inline bool midi_edit_paste_clipboard(Clip* c, float baseBeat) {
     
    seq_lock();
    midi_lock();
    if (!c || g_midiEdit.copyCount <= 0) { midi_unlock(); seq_unlock(); return false; }
    float clipLen = (c->lengthBeats > 0.01f) ? c->lengthBeats : 0.01f;
    midi_edit_clear_selection(c);

    int pasted = 0;
    for (int i = 0; i < g_midiEdit.copyCount; ++i) {
        if (c->midiNoteCount >= MIDI_MAX_NOTES) break;
        float sb = baseBeat + g_midiEdit.copyNotes[i].startBeat;
        if (sb >= clipLen) continue;
        float len = g_midiEdit.copyNotes[i].lengthBeats;
        if (sb + len > clipLen) len = clipLen - sb;
        if (len < 0.02f) len = 0.02f;

        int idx = c->midiNoteCount++;
        MidiNote* n = &c->midiNotes[idx];
        *n = g_midiEdit.copyNotes[i];
        n->startBeat = sb;
        n->lengthBeats = len;
        n->isSelected = true;
        pasted++;
    }
    if (pasted > 0) {
        g_midiEdit.selNote = c->midiNoteCount - 1;
        g_timelineDirty = true;
        invalidate_midi_cache();
        midi_unlock();
        seq_unlock();
        return true;
    }
    midi_unlock();
    seq_unlock();
    return false;
}

 

 
static bool g_midiMarqueeBaseSel[MIDI_MAX_NOTES];
static bool g_midiMarqueeAdditive = false;

static inline void midi_edit_live_marquee(Clip* c, float b1, float b2, int pLo, int pHi) {
    if (!c) return;
    if (b1 > b2) { float t = b1; b1 = b2; b2 = t; }
    if (pLo > pHi) { int t = pLo; pLo = pHi; pHi = t; }
    for (int i = 0; i < c->midiNoteCount && i < MIDI_MAX_NOTES; ++i) {
        MidiNote* n = &c->midiNotes[i];
        bool inBox = (n->pitch >= pLo && n->pitch <= pHi &&
                      n->startBeat < b2 && (n->startBeat + n->lengthBeats) > b1);
        n->isSelected = inBox || (g_midiMarqueeAdditive && g_midiMarqueeBaseSel[i]);
    }
}

 
static inline void update_midi_piano_roll_cache(HWND hwnd, HDC hdc, int w, int h, Clip* c, int baseNote) {
    if (!g_midiCacheDC) g_midiCacheDC = CreateCompatibleDC(hdc);

    if (g_midiCacheW != w || g_midiCacheH != h || !g_midiCacheBmp) {
        if (g_midiCacheBmp) {
            SelectObject(g_midiCacheDC, g_midiCacheOldBmp);
            DeleteObject(g_midiCacheBmp);
            g_midiCacheBmp = NULL;
        }
        g_midiCacheBmp = CreateCompatibleBitmap(hdc, w, h);
        g_midiCacheOldBmp = (HBITMAP)SelectObject(g_midiCacheDC, g_midiCacheBmp);
        g_midiCacheW = w;
        g_midiCacheH = h;
    }

    HDC dc = g_midiCacheDC;
    HFONT oldFont = SELECT_UI_FONT(dc);

    RECT rc = { 0, 0, w, h };
    HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);

    int keysX, keysW, gridX, gridW, rollY, rollH;
    midi_edit_geom(hwnd, &keysX, &keysW, &gridX, &gridW, &rollY, &rollH);
    float rowH = (float)rollH / (float)midi_edit_key_count();
    float clipLen = (c->lengthBeats > 0.01f) ? c->lengthBeats : 0.01f;
    float ppb = (float)gridW / clipLen;
    COLORREF accent = midi_edit_accent();
    bool isQuad = midi_edit_is_quadrum();

     
    // Left strip: piano keys for standard/Halo rolls; numbered drum-voice
    // rows (1-8, quadrum voice names) for the Quadrum roll.
    for (int k = 0; k < midi_edit_key_count(); ++k) {
        int midi = baseNote + (midi_edit_key_count() - 1 - k);
        int noteInOct = midi % 12;
        bool isBlack = !isQuad &&
            (noteInOct == 1 || noteInOct == 3 || noteInOct == 6 || noteInOct == 8 || noteInOct == 10);
        bool isRootC = (!isQuad && noteInOct == 0);

        int ky1 = rollY + (int)(k * rowH);
        int ky2 = rollY + (int)((k + 1) * rowH);
        RECT kr = { keysX, ky1, keysX + keysW, ky2 };
        HBRUSH kBr = CreateSolidBrush(isBlack ? RGB(22, 26, 34) : RGB(36, 42, 54));
        FillRect(dc, &kr, kBr);
        DeleteObject(kBr);

        if (ky2 - ky1 > 9) {
            char nName[16];
            if (isQuad) {
                int voice = midi_edit_row_to_pitch(k);
                strncpy(nName, kQuadrumVoiceNames[voice], sizeof(nName) - 1);
                nName[sizeof(nName) - 1] = '\0';
            } else {
                midi_edit_get_note_name(midi, nName, sizeof(nName));
            }
            SetTextColor(dc, (isQuad || isRootC) ? accent
                            : (isBlack ? RGB(130, 145, 165) : RGB(190, 205, 225)));
            // Center Quadrum voice names so they don't shift when the row is
            // highlighted (the audition highlight draws centered too).
            UINT align = isQuad ? DT_CENTER : DT_LEFT;
            DrawTextA(dc, nName, -1, &kr, align | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

     
    for (int k = 0; k < midi_edit_key_count(); ++k) {
        int midi = baseNote + (midi_edit_key_count() - 1 - k);
        int noteInOct = midi % 12;
        bool isBlack = !isQuad &&
            (noteInOct == 1 || noteInOct == 3 || noteInOct == 6 || noteInOct == 8 || noteInOct == 10);
        RECT rowRc = { gridX, rollY + (int)(k * rowH), gridX + gridW, rollY + (int)((k + 1) * rowH) };
        HBRUSH rBr = CreateSolidBrush(isBlack ? RGB(19, 22, 30) : RGB(25, 29, 38));
        FillRect(dc, &rowRc, rBr);
        DeleteObject(rBr);
    }

     
    for (int k = 0; k <= midi_edit_key_count(); ++k) {
        int gy = rollY + (int)(k * rowH);
        int rowPitch = (k < midi_edit_key_count()) ? (baseNote + (midi_edit_key_count() - 1 - k)) : baseNote;
        bool isCLine = isQuad ? (k == 0 || k == midi_edit_key_count())
                              : (rowPitch % 12 == 0);
        HPEN rowPen = CreatePen(PS_SOLID, 1,
            isCLine ? (isQuad ? RGB(70, 92, 118) : RGB(55, 45, 75))
                    : (isQuad ? RGB(38, 48, 62) : RGB(25, 30, 40)));
        HGDIOBJ oldP = SelectObject(dc, rowPen);
        MoveToEx(dc, gridX, gy, NULL);
        LineTo(dc, gridX + gridW, gy);
        SelectObject(dc, oldP);
        DeleteObject(rowPen);
    }

     
    // Quadrum rows are flat (no black-key shading) so the grid needs brighter
    // line colors to stay visible against the uniform row fill.
    HPEN barPen = CreatePen(PS_SOLID, 1, isQuad ? RGB(70, 92, 118) : RGB(55, 68, 88));
    HPEN beatPen = CreatePen(PS_SOLID, 1, isQuad ? RGB(48, 60, 78) : RGB(36, 44, 58));
    HPEN sixPen = CreatePen(PS_SOLID, 1, isQuad ? RGB(34, 42, 56) : RGB(25, 30, 40));
    HGDIOBJ origPen = SelectObject(dc, sixPen);
    float gridFrac = grid_division_beat_fraction(g_Seq.gridDivision);
    int stepsPerBeat = (int)(1.0f / gridFrac + 0.5f);
    int stepsPerBar = (int)(stepsPerBeat * beats_per_bar() + 0.5f);
    int totalSteps = (int)(clipLen / gridFrac + 0.5f);
    for (int s = 0; s <= totalSteps; ++s) {
        float beat = (float)s * gridFrac;
        int gx = gridX + (int)(beat * ppb);
        if (gx > gridX + gridW) break;
        if (s % stepsPerBar == 0) SelectObject(dc, barPen);
        else if (s % stepsPerBeat == 0) SelectObject(dc, beatPen);
        else SelectObject(dc, sixPen);
        MoveToEx(dc, gx, rollY, NULL);
        LineTo(dc, gx, rollY + rollH);
    }
    SelectObject(dc, origPen);
    DeleteObject(barPen); DeleteObject(beatPen); DeleteObject(sixPen);

     
    for (int i = 0; i < c->midiNoteCount && i < MIDI_MAX_NOTES; ++i) {
        MidiNote* n = &c->midiNotes[i];
        int row = midi_edit_pitch_to_row(n->pitch);
        if (row < 0 || row >= midi_edit_key_count()) continue;

        int nx = gridX + (int)(n->startBeat * ppb);
        int nw = (int)(n->lengthBeats * ppb);
        if (nw < 6) nw = 6;
        int ny = rollY + (int)(row * rowH) + 1;
        int nh = (int)rowH - 1;
        if (nh < 3) nh = 3;

        bool sel = n->isSelected;
        float vNorm = clamp(n->velocity / 100.0f, 0.0f, 1.0f);

        // Note fill/border tinted per kind: velocity-scaled around the accent.
        BYTE aR = (BYTE)GetRValue(accent), aG = (BYTE)GetGValue(accent), aB = (BYTE)GetBValue(accent);
        BYTE fillR = (BYTE)(aR * (0.25f + 0.45f * vNorm));
        BYTE fillG = (BYTE)(aG * (0.25f + 0.45f * vNorm));
        BYTE fillB = (BYTE)(aB * (0.25f + 0.45f * vNorm));
        // Highlighted notes use the velocity-scaled accent fill/border so the
        // white note text stays readable; unhighlighted notes are dimmed below
        // that baseline.
        COLORREF fillCol   = sel ? RGB(fillR, fillG, fillB)
                                 : RGB((BYTE)(fillR * 0.5f), (BYTE)(fillG * 0.5f), (BYTE)(fillB * 0.5f));
        COLORREF borderCol = sel ? accent
                                 : RGB((BYTE)(aR * 0.45f), (BYTE)(aG * 0.45f), (BYTE)(aB * 0.45f));

        HBRUSH nBr = CreateSolidBrush(fillCol);
        HPEN nPn = CreatePen(PS_SOLID, 1, borderCol);
        HGDIOBJ onb = SelectObject(dc, nBr);
        HGDIOBJ onp = SelectObject(dc, nPn);
        RoundRect(dc, nx, ny, nx + nw, ny + nh, 3, 3);
        SelectObject(dc, onp);
        SelectObject(dc, onb);
        DeleteObject(nPn);
        DeleteObject(nBr);

         
        int vLineW = (int)((float)(nw - 4) * vNorm);
        if (vLineW > 0 && nh >= 5) {
            RECT vRc = { nx + 2, ny + nh - 3, nx + 2 + vLineW, ny + nh - 1 };
            HBRUSH vBr = CreateSolidBrush(sel ? RGB(255, 215, 120) : RGB(80, 240, 180));
            FillRect(dc, &vRc, vBr);
            DeleteObject(vBr);
        }

         
        // No text on Quadrum drum notes — the voice name already sits in the
        // left strip, and note bodies are small enough that a label just
        // clutters them.
        if (!isQuad && nh >= 10 && nw >= 28) {
            char nName[24];
            midi_edit_get_note_name(n->pitch, nName, sizeof(nName));
            char noteTxt[48];
            if (nw >= 56) {
                snprintf(noteTxt, sizeof(noteTxt), "%s (%d)", nName, (int)(n->velocity + 0.5f));
            } else {
                snprintf(noteTxt, sizeof(noteTxt), "%s", nName);
            }
            // White note text reads better on the tinted note bodies across
            // every piano roll (standard MIDI, Halo, Quadrum).
            SetTextColor(dc, RGB(250, 250, 255));
            RECT tr = { nx + 4, ny, nx + nw - 2, ny + nh - (nh >= 5 ? 3 : 0) };
            DrawTextA(dc, noteTxt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

     
    int botY = h - scale_y(26);
    int btnH = scale_y(20);
    RECT clrRc = { scale_x(14), botY, scale_x(100), botY + btnH };
    RECT octRc = { scale_x(106), botY, scale_x(210), botY + btnH };
    RECT kbRc  = { scale_x(216), botY, scale_x(310), botY + btnH };

    HBRUSH btnBr = CreateSolidBrush(RGB(26, 32, 42));
    HPEN btnPn = CreatePen(PS_SOLID, 1, RGB(48, 58, 72));
    HGDIOBJ ob = SelectObject(dc, btnBr);
    HGDIOBJ op = SelectObject(dc, btnPn);
    RoundRect(dc, clrRc.left, clrRc.top, clrRc.right, clrRc.bottom, 3, 3);
    RoundRect(dc, octRc.left, octRc.top, octRc.right, octRc.bottom, 3, 3);
    // Keyboard button: green when enabled (audition via QWERTY keys).
    if (g_midiKbMode) {
        HBRUSH kbBg = CreateSolidBrush(RGB(22, 90, 55));
        HPEN kbPn  = CreatePen(PS_SOLID, 1, RGB(80, 240, 180));
        SelectObject(dc, kbBg);
        SelectObject(dc, kbPn);
        RoundRect(dc, kbRc.left, kbRc.top, kbRc.right, kbRc.bottom, 3, 3);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(kbPn);
        DeleteObject(kbBg);
    } else {
        RoundRect(dc, kbRc.left, kbRc.top, kbRc.right, kbRc.bottom, 3, 3);
    }
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(btnPn); DeleteObject(btnBr);

    SetTextColor(dc, RGB(220, 120, 120));
    DrawTextA(dc, "Clear Notes", -1, &clrRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    char lowN[8], highN[8];
    midi_edit_get_note_name(baseNote, lowN, sizeof(lowN));
    midi_edit_get_note_name(baseNote + midi_edit_key_count() - 1, highN, sizeof(highN));
    char octBuf[32];
    if (isQuad) snprintf(octBuf, sizeof(octBuf), "Voices 1-8");
    else        snprintf(octBuf, sizeof(octBuf), "Oct: %s-%s", lowN, highN);
    SetTextColor(dc, accent);
    DrawTextA(dc, octBuf, -1, &octRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SetTextColor(dc, g_midiKbMode ? RGB(160, 255, 205) : RGB(140, 155, 175));
    DrawTextA(dc, "Keyboard", -1, &kbRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

     
    int minHintX = kbRc.right + scale_x(12);
    RECT hintRc = { minHintX, botY + 1, w - scale_x(14), botY + btnH };
    SetTextColor(dc, RGB(140, 155, 175));
    const char* hintTxt = g_midiKbMode
        ? (midi_edit_is_quadrum()
           ? "Keys: A W S E D F T G (8 voices)"
           : "Keys: A W S E D F T G Y H U J K O L P")
        : kMidiHintText;
    DrawTextA(dc, hintTxt,
              -1, &hintRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    SelectObject(dc, oldFont);
}

 
static inline void fx_draw_aa_knob(HDC hdc, int cx, int cy, float radius, float norm);

static inline void midi_adsr_knob_rect(int idx, RECT* rc) {
    // Top-right block, anchored to the window's right edge (callers pass a
    // client width so the knobs hug it regardless of window size).
    int w = 820;
    if (g_midiHwnd && IsWindow(g_midiHwnd)) {
        RECT crc; GetClientRect(g_midiHwnd, &crc);
        w = crc.right - crc.left;
    }
    int kw = scale_x(44);
    int kx = w - scale_x(14) - (4 - idx) * (kw + scale_x(6));
    int ky = scale_y(4);
    SetRect(rc, kx, ky, kx + kw, ky + scale_y(44));
}

// ADSR knobs
static inline HFONT get_ui_knob_label_font(void) {
    // Normal-weight small font for the knob captions (uppercase drawn).
    static HFONT s_hFontKnob = NULL;
    static int   s_lastPx = 0;
    int px = (int)(10.0f * scale_font(1.0f) + 0.5f);
    if (px < 7) px = 7;
    if (!s_hFontKnob || s_lastPx != px) {
        if (s_hFontKnob) DeleteObject(s_hFontKnob);
        s_hFontKnob = CreateFontA(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Inter");
        if (!s_hFontKnob) {
            s_hFontKnob = CreateFontA(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
        }
        s_lastPx = px;
    }
    return s_hFontKnob;
}


static inline void draw_midi_adsr_knobs(HDC dc, Clip* c) {
    // Quadrum/Halo synth clips use their own built-in engine ADSR envelopes
    // (editable on the [SYNTH] panel), so the piano-roll ADSR knobs are
    // nonfunctional for them and are not drawn.
    if (midi_edit_is_synth_kind()) return;
    // Synth-module (Quadrum/Halo) rolls map their knobs to the dedicated
    // synth-engine envelope fields so the values are engine-ready; the shared
    // adsr* fields belong to the sample/MIDI path.
    bool synth = midi_edit_is_synth_kind();
    const float vals[4] = {
        synth ? c->synthAttack  : c->adsrAttack,
        synth ? c->synthDecay   : c->adsrDecay,
        synth ? c->synthSustain : c->adsrSustain,
        synth ? c->synthRelease : c->adsrRelease
    };
    const float maxv[4] = { 5000.0f, 5000.0f, 1.0f, 5000.0f };
    const char* lbl[4] = { "ATTACK", "DECAY", "SUSTAIN", "RELEASE" };

    for (int i = 0; i < 4; ++i) {
        RECT rc;
        midi_adsr_knob_rect(i, &rc);
        float norm = (maxv[i] > 0.0f) ? (vals[i] / maxv[i]) : 0.0f;
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        int cx = (rc.left + rc.right) / 2;
        int cy = rc.top + scale_y(20);
        fx_draw_aa_knob(dc, cx, cy, (float)scale_y(12), norm);

        SetBkMode(dc, TRANSPARENT);
        HFONT oldF = (HFONT)SelectObject(dc, get_ui_knob_label_font());
        SetTextColor(dc, RGB(150, 165, 185));
        RECT lblRc = { rc.left - scale_x(8), rc.bottom - scale_y(13), rc.right + scale_x(8), rc.bottom };
        DrawTextA(dc, lbl[i], -1, &lblRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, oldF);
    }
}

 
static inline void draw_midi_dynamic_overlays(HWND hwnd, HDC dc, int w, int h, Clip* c, int baseNote) {
    (void)baseNote;
    SetBkMode(dc, TRANSPARENT);

     
    // Snapshot the polyphonic held set: mouse strip + keyboard keys + PLAY
    // loop (auditionNote = last-pressed for paint, auditionHeld = set active).
    int  snapHeldNotes[MIDI_KB_MAX];
    int  snapHeldCount = 0;
    midi_lock();
    bool snapAudition = g_midiEdit.isAuditionPlaying;
    snapHeldCount = g_midiEdit.auditionNoteCount;
    if (snapHeldCount > MIDI_KB_MAX) snapHeldCount = MIDI_KB_MAX;
    for (int i = 0; i < snapHeldCount; ++i) snapHeldNotes[i] = g_midiEdit.auditionNotes[i];
    double snapAudBeat  = g_midiEdit.auditionPlayheadBeat;
    midi_unlock();
    const bool snapHeld = (snapHeldCount > 0);

    int keysX, keysW, gridX, gridW, rollY, rollH;
    midi_edit_geom(hwnd, &keysX, &keysW, &gridX, &gridW, &rollY, &rollH);
    float rowH = (float)rollH / (float)midi_edit_key_count();
    float clipLen = (c->lengthBeats > 0.01f) ? c->lengthBeats : 0.01f;
    float ppb = (float)gridW / clipLen;

     
    // Toolbar order: PLAY, GEN, then (standard MIDI clips only) LOAD SAMPLE,
    // LOAD SOUNDFONT, instrument icon + [X]. Synth clips are pure synths and
    // have no sample-source UI; their buttons shift left accordingly.
    bool showSourceUI = !midi_edit_is_synth_kind();
    RECT playRc, genRc, sampRc, sfRc, instRc, sfXRc;
    midi_edit_toolbar_rects(&playRc, &genRc, &sampRc, &sfRc, &instRc, &sfXRc);
    char sampBuf[MAX_PATH + 16] = "[LOAD SAMPLE]";
    if (c->sampleIndex >= 0 && c->sampleIndex < g_Seq.sampleCount) {
        snprintf(sampBuf, sizeof(sampBuf), "[SAMPLE: %s]", g_Seq.samples[c->sampleIndex].name);
    }
    bool hasSamp = (c->sampleIndex >= 0 && c->sampleIndex < g_Seq.sampleCount);
    // Sample and soundfont are mutually exclusive sources on the piano roll:
    // each load button is greyed out while the other source is active.
    bool sfActive = sfont_is_loaded();
    bool sampDisabled = sfActive;          // can't load a sample while a font is loaded
    bool sfDisabled   = hasSamp;           // can't load a font while a sample is attached

     
    bool audition = snapAudition;
    COLORREF act = midi_edit_accent();
    HBRUSH pBg = CreateSolidBrush(audition ? RGB(45, 30, 65) : RGB(24, 30, 40));
    HPEN pPn = CreatePen(PS_SOLID, 1, audition ? act : RGB(50, 65, 85));
    HGDIOBJ ob = SelectObject(dc, pBg);
    HGDIOBJ op = SelectObject(dc, pPn);
    RoundRect(dc, playRc.left, playRc.top, playRc.right, playRc.bottom, 3, 3);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(pPn); DeleteObject(pBg);
    SetTextColor(dc, audition ? act : RGB(140, 155, 175));
    DrawTextA(dc, audition ? "[STOP]" : "[PLAY]", -1, &playRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

     
    {
        // Same neutral scheme as the PLAY/SAMPLE buttons.
        HBRUSH gBg = CreateSolidBrush(RGB(24, 30, 40));
        HPEN gPn = CreatePen(PS_SOLID, 1, RGB(50, 65, 85));
        HGDIOBJ oldgB = SelectObject(dc, gBg);
        HGDIOBJ oldgP = SelectObject(dc, gPn);
        RoundRect(dc, genRc.left, genRc.top, genRc.right, genRc.bottom, 3, 3);
        SelectObject(dc, oldgP);
        SelectObject(dc, oldgB);
        DeleteObject(gPn); DeleteObject(gBg);
        SetTextColor(dc, RGB(140, 155, 175));
        DrawTextA(dc, "[GEN]", -1, &genRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // For synth clips, a "SYNTH" button reopens the synth interface (it can be
    // closed independently of the piano roll).
    if (midi_edit_is_synth_kind()) {
        RECT synthRc;
        midi_edit_synth_btn_rect(&synthRc);
        HBRUSH sBg = CreateSolidBrush(RGB(24, 30, 40));
        HPEN sPn = CreatePen(PS_SOLID, 1, act);
        HGDIOBJ oldSB = SelectObject(dc, sBg);
        HGDIOBJ oldSP = SelectObject(dc, sPn);
        RoundRect(dc, synthRc.left, synthRc.top, synthRc.right, synthRc.bottom, 3, 3);
        SelectObject(dc, oldSP); SelectObject(dc, oldSB);
        DeleteObject(sPn); DeleteObject(sBg);
        SetTextColor(dc, act);
        DrawTextA(dc, "[SYNTH]", -1, &synthRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    if (!showSourceUI) {
        // Pure-synth rolls: no sample/soundfont source UI at all.
        (void)sampRc; (void)sfRc; (void)instRc; (void)sfXRc;
        (void)sampBuf; (void)hasSamp;
    } else {
     
    HBRUSH sBg = CreateSolidBrush(RGB(24, 30, 40));
    HPEN sPn = CreatePen(PS_SOLID, 1, sampDisabled ? RGB(50, 55, 62) : (hasSamp ? act : RGB(50, 65, 85)));
    ob = SelectObject(dc, sBg);
    op = SelectObject(dc, sPn);
    RoundRect(dc, sampRc.left, sampRc.top, sampRc.right, sampRc.bottom, 3, 3);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(sPn); DeleteObject(sBg);
    SetTextColor(dc, sampDisabled ? RGB(95, 100, 110) : (hasSamp ? act : RGB(140, 155, 175)));
     
    {
        wchar_t sampW[MAX_PATH + 20];
        if (utf8_to_wide_buf(sampBuf, sampW, MAX_PATH + 20) > 0)
            DrawTextW(dc, sampW, -1, &sampRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        else
            DrawTextA(dc, sampBuf, -1, &sampRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

     
    // [LOAD SOUNDFONT] + instrument-picker note icon. Same base styling as
    // the LOAD SAMPLE button; only the "loading" state tints the text.
    {
        char sfBuf[80];
        bool sfLoading = sfont_is_loading();
        bool sfLoaded  = sfont_is_loaded();
        if (sfLoading) snprintf(sfBuf, sizeof(sfBuf), "[SOUNDFONT: LOADING...]");
        else if (sfLoaded) snprintf(sfBuf, sizeof(sfBuf), "[SF: %s]", sfont_name());
        else snprintf(sfBuf, sizeof(sfBuf), "[LOAD SOUNDFONT]");

        HBRUSH fBg = CreateSolidBrush(RGB(24, 30, 40));
        HPEN fPn = CreatePen(PS_SOLID, 1, sfDisabled ? RGB(50, 55, 62)
                                  : (sfLoaded ? RGB(180, 140, 255) : RGB(50, 65, 85)));
        HGDIOBJ ob2 = SelectObject(dc, fBg);
        HGDIOBJ op2 = SelectObject(dc, fPn);
        RoundRect(dc, sfRc.left, sfRc.top, sfRc.right, sfRc.bottom, 3, 3);
        SelectObject(dc, op2);
        SelectObject(dc, ob2);
        DeleteObject(fPn); DeleteObject(fBg);
        SetTextColor(dc, sfDisabled ? RGB(95, 100, 110)
                    : sfLoaded ? RGB(215, 185, 255)
                    : sfLoading   ? RGB(120, 180, 140)
                                  : RGB(140, 155, 175));
        DrawTextA(dc, sfBuf, -1, &sfRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        // Instrument-pick icon: UTF-8 beamed note glyph in a small square
        // (opens the selector; only meaningful once a font is loaded).
        bool instHot = sfont_is_loaded();
        HBRUSH nBg = CreateSolidBrush(RGB(24, 30, 40));
        HPEN nPn = CreatePen(PS_SOLID, 1, instHot ? RGB(110, 200, 160) : RGB(50, 65, 60));
        ob2 = SelectObject(dc, nBg);
        op2 = SelectObject(dc, nPn);
        RoundRect(dc, instRc.left, instRc.top, instRc.right, instRc.bottom, 3, 3);
        SelectObject(dc, op2);
        SelectObject(dc, ob2);
        DeleteObject(nPn); DeleteObject(nBg);
        SetTextColor(dc, instHot ? RGB(160, 240, 190) : RGB(90, 105, 115));
        wchar_t noteW[4];
        if (utf8_to_wide_buf("\xE2\x99\xAB", noteW, 4) > 0)
            DrawTextW(dc, noteW, -1, &instRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        else
            DrawTextA(dc, "#", -1, &instRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Red [X]: clears the loaded soundfont AND any sample attached to the
        // current clip (the two sources are mutually exclusive). Active when
        // either source is present.
        bool xHot = sfLoaded || hasSamp;
        HBRUSH xBg = CreateSolidBrush(RGB(24, 30, 40));
        HPEN xPn = CreatePen(PS_SOLID, 1, xHot ? RGB(220, 100, 100) : RGB(50, 65, 60));
        ob2 = SelectObject(dc, xBg);
        op2 = SelectObject(dc, xPn);
        RoundRect(dc, sfXRc.left, sfXRc.top, sfXRc.right, sfXRc.bottom, 3, 3);
        SelectObject(dc, op2);
        SelectObject(dc, ob2);
        DeleteObject(xPn); DeleteObject(xBg);
        SetTextColor(dc, xHot ? RGB(255, 150, 150) : RGB(90, 105, 115));
        DrawTextA(dc, "X", -1, (RECT*)&sfXRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    } // end showSourceUI

     
    draw_midi_adsr_knobs(dc, c);

    (void)w; (void)h;

     
    // Highlight every held key (polyphonic chords highlight all rows).
    if (snapHeld) {
        for (int hh = 0; hh < snapHeldCount; ++hh) {
            int held = snapHeldNotes[hh];
            int row = midi_edit_pitch_to_row(held);
            if (row < 0 || row >= midi_edit_key_count()) continue;
            int ky1 = rollY + (int)(row * rowH);
            int ky2 = rollY + (int)((row + 1) * rowH);
            RECT kr = { keysX, ky1, keysX + keysW, ky2 };
            HBRUSH kBr = CreateSolidBrush(midi_edit_accent());
            FillRect(dc, &kr, kBr);
            DeleteObject(kBr);

            if (ky2 - ky1 > 9) {
                char nName[24];
                if (midi_edit_is_quadrum()) {
                    snprintf(nName, sizeof(nName), "%s", kQuadrumVoiceNames[held & 7]);
                } else {
                    midi_edit_get_note_name(held, nName, sizeof(nName));
                }
                SetTextColor(dc, RGB(10, 15, 20));
                DrawTextA(dc, nName, -1, &kr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        }
    }

     
    if (g_midiEdit.isMarqueeSelecting && g_midiEdit.hasMovedPastThreshold) {
        int mX1 = min(g_midiEdit.marqueeStartX, g_midiEdit.marqueeCurX);
        int mX2 = max(g_midiEdit.marqueeStartX, g_midiEdit.marqueeCurX);
        int mY1 = max(rollY, min(g_midiEdit.marqueeStartY, g_midiEdit.marqueeCurY));
        int mY2 = min(rollY + rollH, max(g_midiEdit.marqueeStartY, g_midiEdit.marqueeCurY));
        if (mX2 > mX1 && mY2 > mY1) {
            COLORREF ma = midi_edit_accent();
            draw_alpha_box(dc, mX1, mY1, mX2 - mX1, mY2 - mY1, ma, 60, ma);
        }
    }

     
    if (seq_is_playing()) {
        LONG pFrame = InterlockedCompareExchange(&g_Seq.playbackFrame, 0, 0);
        float curBeat = frame_to_beat((ma_uint64)pFrame, g_Seq.bpm, 0.0f);

         
        if (curBeat >= c->startBeat && curBeat <= (c->startBeat + c->lengthBeats) && curBeat <= total_beats()) {
            float relBeat = curBeat - c->startBeat;
            int phX = gridX + (int)(relBeat * ppb);
            if (phX >= gridX && phX <= gridX + gridW) {
                HPEN phPen = CreatePen(PS_SOLID, 1, midi_edit_accent());
                HGDIOBJ oldPh = SelectObject(dc, phPen);
                MoveToEx(dc, phX, rollY, NULL);
                LineTo(dc, phX, rollY + rollH);
                SelectObject(dc, oldPh);
                DeleteObject(phPen);
            }
        }
    }
    if (snapAudition) {
        int phX = gridX + (int)((float)snapAudBeat * ppb);
        if (phX >= gridX && phX <= gridX + gridW) {
            COLORREF aa = midi_edit_accent();
            HPEN phPen = CreatePen(PS_SOLID, 1, RGB((BYTE)min(255, GetRValue(aa) + 35),
                                                    (BYTE)min(255, GetGValue(aa) + 35),
                                                    (BYTE)min(255, GetBValue(aa) + 35)));
            HGDIOBJ oldPh = SelectObject(dc, phPen);
            MoveToEx(dc, phX, rollY, NULL);
            LineTo(dc, phX, rollY + rollH);
            SelectObject(dc, oldPh);
            DeleteObject(phPen);
        }
    }
}

// --- Humanization dialog -----------------------------------------------------
// Applies timing / duration / velocity jitter to every note in the current
// piano-roll clip. Opened with 'H' from any piano roll. Uses a Xorshift32 PRNG
// (never rand()) seeded from the high-resolution counter so repeated opens
// give fresh variation.
static HWND    g_humHwnd = NULL;
static HWND    g_humTimeEdit = NULL, g_humDurEdit = NULL, g_humVelEdit = NULL;
static WNDPROC g_humTimeDef = NULL, g_humDurDef = NULL, g_humVelDef = NULL;
static HBRUSH  g_humEditBg = NULL;

static inline void hum_edit_commit(HWND hwnd) {
    char tb[16] = "", db[16] = "", vb[16] = "";
    if (g_humTimeEdit && IsWindow(g_humTimeEdit)) GetWindowTextA(g_humTimeEdit, tb, sizeof(tb));
    if (g_humDurEdit  && IsWindow(g_humDurEdit))  GetWindowTextA(g_humDurEdit,  db, sizeof(db));
    if (g_humVelEdit  && IsWindow(g_humVelEdit))  GetWindowTextA(g_humVelEdit,  vb, sizeof(vb));
    int timeJit = atoi(tb), durJit = atoi(db), velJit = atoi(vb);
    if (timeJit < 0) timeJit = 0;
    if (durJit  < 0) durJit  = 0;
    if (velJit  < 0) velJit  = 0;
    if (timeJit > 64) timeJit = 64;
    if (durJit  > 64) durJit  = 64;
    if (velJit  > 64) velJit  = 64;

    Clip* c = midi_edit_clip();
    if (c && c->midiNoteCount > 0) {
        // PRNG seed from the high-resolution counter (never rand()).
        static uint32_t s_humRng = 0u;
        if (!s_humRng) {
            LARGE_INTEGER qpc;
            QueryPerformanceCounter(&qpc);
            s_humRng = (uint32_t)qpc.LowPart;
            if (!s_humRng) s_humRng = 0x9E3779B9u;
        }
        // In-beat timing jitter: how far a note can slide, as a fraction of
        // a beat. 1 tick at 480 PPQ ~= 1/480 beat; we map the entered integer
        // to ~1/96-beat steps (i.e. a value of N moves up to N/96 beat).
        const float beatStep = 1.0f / 96.0f;
        push_undo_state();
        seq_lock();
        for (int i = 0; i < c->midiNoteCount; ++i) {
            MidiNote* n = &c->midiNotes[i];
            // timing jitter (beats)
            if (timeJit > 0) {
                s_humRng ^= s_humRng << 13; s_humRng ^= s_humRng >> 17; s_humRng ^= s_humRng << 5;
                if (!s_humRng) s_humRng = 0x9E3779B9u;
                int dt = (int)((s_humRng >> 8) % (uint32_t)(timeJit * 2 + 1)) - timeJit;
                float nb = n->startBeat + (float)dt * beatStep;
                if (nb < 0.0f) nb = 0.0f;
                n->startBeat = nb;
            }
            // duration jitter (beats)
            if (durJit > 0) {
                s_humRng ^= s_humRng << 13; s_humRng ^= s_humRng >> 17; s_humRng ^= s_humRng << 5;
                if (!s_humRng) s_humRng = 0x9E3779B9u;
                int dd = (int)((s_humRng >> 8) % (uint32_t)(durJit * 2 + 1)) - durJit;
                float nl = n->lengthBeats + (float)dd * beatStep;
                if (nl < 0.02f) nl = 0.02f;
                if (n->startBeat + nl > c->lengthBeats) nl = c->lengthBeats - n->startBeat;
                if (nl < 0.02f) nl = 0.02f;
                n->lengthBeats = nl;
            }
            // velocity jitter (1..127)
            if (velJit > 0) {
                s_humRng ^= s_humRng << 13; s_humRng ^= s_humRng >> 17; s_humRng ^= s_humRng << 5;
                if (!s_humRng) s_humRng = 0x9E3779B9u;
                int dv = (int)((s_humRng >> 8) % (uint32_t)(velJit * 2 + 1)) - velJit;
                float nv = n->velocity + (float)dv;
                if (nv < 1.0f) nv = 1.0f;
                if (nv > 127.0f) nv = 127.0f;
                n->velocity = nv;
            }
        }
        g_Seq.isModified = true;
        seq_unlock();
        invalidate_midi_cache();
        if (g_midiHwnd && IsWindow(g_midiHwnd)) InvalidateRect(g_midiHwnd, NULL, FALSE);
        if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
    }
    ShowWindow(hwnd, SW_HIDE);
}

static LRESULT CALLBACK HumEditSubProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) { SendMessageA(GetParent(h), WM_APP + 1, 1, 0); return 0; }
        if (wParam == VK_ESCAPE) { SendMessageA(GetParent(h), WM_APP + 1, 0, 0); return 0; }
    }
    WNDPROC def = NULL;
    if (h == g_humTimeEdit) def = g_humTimeDef;
    else if (h == g_humDurEdit) def = g_humDurDef;
    else if (h == g_humVelEdit) def = g_humVelDef;
    return CallWindowProcA(def ? def : DefWindowProcA, h, msg, wParam, lParam);
}

static LRESULT CALLBACK HumWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC hdcC = (HDC)wParam;
            SetTextColor(hdcC, RGB(225, 235, 245));
            SetBkColor(hdcC, RGB(24, 28, 38));
            if (!g_humEditBg) g_humEditBg = CreateSolidBrush(RGB(24, 28, 38));
            return (LRESULT)g_humEditBg;
        }
        case WM_APP + 1:
            if (wParam) hum_edit_commit(hwnd);
            else ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left, hCl = rc.bottom - rc.top;
            // Double-buffer to avoid flicker.
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, hCl);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
            HFONT oldF = SELECT_UI_FONT(memDC);
            HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
            FillRect(memDC, &rc, bg);
            DeleteObject(bg);
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, RGB(150, 165, 185));
            RECT timeLbl = { 16, 40, 150, 60 };
            RECT durLbl  = { 16, 74, 150, 94 };
            RECT velLbl  = { 16, 108, 150, 128 };
            DrawTextA(memDC, "Timing jitter", -1, &timeLbl, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            DrawTextA(memDC, "Duration jitter", -1, &durLbl, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            DrawTextA(memDC, "Velocity jitter", -1, &velLbl, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

            // Flat rounded frames around the three jitter edit fields, matching
            // the BPM/Bars numeric-field style (no sunken client edge).
            {
                HBRUSH fBg = CreateSolidBrush(RGB(24, 28, 38));
                HPEN   fPn = CreatePen(PS_SOLID, 1, RGB(55, 68, 88));
                HGDIOBJ ob = SelectObject(memDC, fBg);
                HGDIOBJ op = SelectObject(memDC, fPn);
                const int fieldX = 170, fieldW = 70, fieldH = 22;
                const int fieldY[3] = { 38, 72, 106 };
                for (int i = 0; i < 3; ++i) {
                    RECT fr = { fieldX - 6, fieldY[i] - 6, fieldX + fieldW + 6, fieldY[i] + fieldH + 9 };
                    RoundRect(memDC, fr.left, fr.top, fr.right, fr.bottom, 4, 4);
                }
                SelectObject(memDC, op);
                SelectObject(memDC, ob);
                DeleteObject(fPn);
                DeleteObject(fBg);
            }

            // Bottom-centered APPLY (ENTER) / CANCEL (ESC) buttons.
            RECT okRc = { 43, 150, 163, 176 };
            RECT noRc = { 177, 150, 297, 176 };
            HBRUSH okBg = CreateSolidBrush(RGB(22, 90, 55));
            HPEN okPn = CreatePen(PS_SOLID, 1, RGB(80, 240, 180));
            HGDIOBJ ob = SelectObject(memDC, okBg);
            HGDIOBJ op = SelectObject(memDC, okPn);
            RoundRect(memDC, okRc.left, okRc.top, okRc.right, okRc.bottom, 4, 4);
            SelectObject(memDC, op); SelectObject(memDC, ob);
            DeleteObject(okPn); DeleteObject(okBg);
            SetTextColor(memDC, RGB(160, 255, 205));
            DrawTextA(memDC, "APPLY [ENTER]", -1, &okRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            HBRUSH noBg = CreateSolidBrush(RGB(60, 32, 32));
            HPEN noPn = CreatePen(PS_SOLID, 1, RGB(220, 100, 100));
            ob = SelectObject(memDC, noBg);
            op = SelectObject(memDC, noPn);
            RoundRect(memDC, noRc.left, noRc.top, noRc.right, noRc.bottom, 4, 4);
            SelectObject(memDC, op); SelectObject(memDC, ob);
            DeleteObject(noPn); DeleteObject(noBg);
            SetTextColor(memDC, RGB(255, 190, 190));
            DrawTextA(memDC, "CANCEL [ESC]", -1, &noRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(memDC, oldF);
            BitBlt(hdc, 0, 0, w, hCl, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
            if (mx >= 43 && mx <= 163 && my >= 150 && my <= 176) { hum_edit_commit(hwnd); return 0; }
            if (mx >= 177 && mx <= 297 && my >= 150 && my <= 176) { ShowWindow(hwnd, SW_HIDE); return 0; }
            return 0;
        }
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            if (g_humEditBg) { DeleteObject(g_humEditBg); g_humEditBg = NULL; }
            g_humHwnd = NULL; g_humTimeEdit = NULL; g_humDurEdit = NULL; g_humVelEdit = NULL;
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static inline void open_humanize_dialog(HWND parentHwnd) {
    int rw = 360, rh = 230;
    int scrW = GetSystemMetrics(SM_CXSCREEN), scrH = GetSystemMetrics(SM_CYSCREEN);
    int rx = (scrW - rw) / 2, ry = (scrH - rh) / 2;
    if (parentHwnd && IsWindow(parentHwnd)) {
        RECT prc; GetWindowRect(parentHwnd, &prc);
        rx = prc.left + ((prc.right - prc.left) - rw) / 2;
        ry = prc.top + ((prc.bottom - prc.top) - rh) / 2;
    }
    if (!g_humHwnd || !IsWindow(g_humHwnd)) {
        static bool s_registered = false;
        if (!s_registered) {
            WNDCLASSA wc = { 0 };
            wc.lpfnWndProc = HumWndProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractHumClass";
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_registered = true;
        }
        g_humHwnd = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, "RefractHumClass",
            "Humanize", WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE, rx, ry, rw, rh,
            parentHwnd, NULL, GetModuleHandle(NULL), NULL);
        // Edit boxes styled like the note generator's numeric fields (flat,
        // no sunken WS_EX_CLIENTEDGE border — a rounded frame is drawn in paint).
        g_humTimeEdit = CreateWindowExA(0, "EDIT", "4", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_NUMBER,
            170, 38, 70, 22, g_humHwnd, (HMENU)2101, GetModuleHandle(NULL), NULL);
        g_humDurEdit  = CreateWindowExA(0, "EDIT", "4", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_NUMBER,
            170, 72, 70, 22, g_humHwnd, (HMENU)2102, GetModuleHandle(NULL), NULL);
        g_humVelEdit  = CreateWindowExA(0, "EDIT", "8", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_NUMBER,
            170, 106, 70, 22, g_humHwnd, (HMENU)2103, GetModuleHandle(NULL), NULL);
        HWND edits[] = { g_humTimeEdit, g_humDurEdit, g_humVelEdit };
        for (int i = 0; i < 3; ++i) {
            SendMessageA(edits[i], WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
            SendMessageA(edits[i], EM_SETLIMITTEXT, 3, 0);
            SetWindowLongPtrA(edits[i], GWLP_USERDATA, (LONG_PTR)SetWindowLongPtrA(edits[i], GWLP_WNDPROC, (LONG_PTR)HumEditSubProc));
        }
        g_humTimeDef = (WNDPROC)GetWindowLongPtrA(g_humTimeEdit, GWLP_USERDATA);
        g_humDurDef  = (WNDPROC)GetWindowLongPtrA(g_humDurEdit,  GWLP_USERDATA);
        g_humVelDef  = (WNDPROC)GetWindowLongPtrA(g_humVelEdit,  GWLP_USERDATA);
    } else {
        SetWindowPos(g_humHwnd, HWND_TOPMOST, rx, ry, rw, rh, SWP_SHOWWINDOW);
    }
    ShowWindow(g_humHwnd, SW_SHOW);
    SetForegroundWindow(g_humHwnd);
    SetFocus(g_humTimeEdit);
    SendMessageA(g_humTimeEdit, EM_SETSEL, 0, -1);
    InvalidateRect(g_humHwnd, NULL, FALSE);
}

static LRESULT CALLBACK MidiEditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_GETMINMAXINFO: {
             
            MINMAXINFO* mmi = (MINMAXINFO*)lParam;
            int minWidth = scale_x(740);
            int minHeight = scale_y(480);

            HDC mdc = GetDC(hwnd);
            if (mdc) {
                HFONT oldF = SELECT_UI_FONT(mdc);
                SIZE sz;
                if (GetTextExtentPoint32A(mdc, kMidiHintText,
                                          (int)strlen(kMidiHintText), &sz)) {
                     
                    int needed = scale_x(322) + sz.cx + scale_x(14);
                    if (needed > minWidth) minWidth = needed;
                }
                SelectObject(mdc, oldF);
                ReleaseDC(hwnd, mdc);
            }

            mmi->ptMinTrackSize.x = minWidth;
            mmi->ptMinTrackSize.y = minHeight;
            return 0;
        }

        case WM_TIMER:
            if (wParam == 1) InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case WM_DROPFILES: {
            // Drop an audio file on the sample selector to replace the clip's
            // sample, or an .sf2 on the soundfont selector to load it.
            HDROP hDrop = (HDROP)wParam;
            POINT pt;
            DragQueryPoint(hDrop, &pt);
            RECT sampRc, sfRc;
            midi_edit_toolbar_rects(NULL, NULL, &sampRc, &sfRc, NULL, NULL);
            Clip* cDrop = midi_edit_clip();
            if (!cDrop) { DragFinish(hDrop); return 0; }
            // Pure-synth rolls accept no file drops (no sample/soundfont UI).
            if (midi_edit_is_synth_kind()) { DragFinish(hDrop); return 0; }
            bool overSamp = (pt.x >= sampRc.left && pt.x <= sampRc.right && pt.y >= sampRc.top && pt.y <= sampRc.bottom);
            bool overSf   = (pt.x >= sfRc.left   && pt.x <= sfRc.right   && pt.y >= sfRc.top   && pt.y <= sfRc.bottom);
            // Respect the mutual-exclusion greying: a drop onto a greyed-out
            // source button is ignored (a font is loaded, or a sample attached).
            if (overSamp && sfont_is_loaded()) overSamp = false;
            if (overSf   && cDrop->sampleIndex >= 0) overSf = false;
            if (!overSamp && !overSf) {
                DragFinish(hDrop);
                return 0;
            }
            wchar_t filepathW[MAX_PATH];
            if (DragQueryFileW(hDrop, 0, filepathW, MAX_PATH)) {
                char filepath[MAX_PATH];
                if (wide_to_utf8_buf(filepathW, filepath, MAX_PATH) > 0) {
                    const char* dot = strrchr(filepath, '.');
                    if (overSf) {
                        // SoundFont drop: .sf2 only. A soundfont and a sample
                        // are mutually exclusive on the piano roll, so detach
                        // any sample from the current clip first.
                        if (cDrop->sampleIndex >= 0) {
                            seq_lock();
                            cDrop->sampleIndex = -1;
                            seq_unlock();
                            g_timelineDirty = true;
                        }
                        sfont_load_async(filepath);
                    } else {
                        static const char* kDropAudioExts[] = {
                            ".wav", ".aiff", ".aif", ".flac", ".mp3", ".m4a", ".wma", ".ogg"
                        };
                        bool isAudio = false;
                        for (int e = 0; dot && e < (int)(sizeof(kDropAudioExts) / sizeof(kDropAudioExts[0])); ++e) {
                            if (_stricmp(dot, kDropAudioExts[e]) == 0) { isAudio = true; break; }
                        }
                        if (isAudio) {
                            int idx = load_audio_file(filepath);
                            if (idx != -1) {
                                seq_lock();
                                cDrop->sampleIndex = idx;
                                seq_unlock();
                                // Loading a sample clears the soundfont so the
                                // clip naming scheme can't conflict.
                                sfont_clear();
                                g_timelineDirty = true;
                                invalidate_midi_cache();
                            }
                        }
                    }
                }
            }
            DragFinish(hDrop);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            Clip* c = midi_edit_clip();
            if (!c) return 0;

            short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            float step = (zDelta > 0) ? 5.0f : -5.0f;

            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);

            // --- CHECK ADSR KNOBS FIRST (Stepwise hover adjustment) ---
            int adsrKnob = -1;
            for (int i = 0; i < 4; ++i) {
                RECT krc;
                midi_adsr_knob_rect(i, &krc);
                if (pt.x >= krc.left && pt.x <= krc.right &&
                    pt.y >= krc.top && pt.y <= krc.bottom) {
                    adsrKnob = i;
                    break;
                }
            }

            if (adsrKnob >= 0) {
                // Adjust the corresponding parameter
                float* pVal = NULL;
                float maxVal = 0.0f;
                bool synth = midi_edit_is_synth_kind();

                switch (adsrKnob) {
                    case 0: pVal = synth ? &c->synthAttack : &c->adsrAttack; maxVal = 5000.0f; break;
                    case 1: pVal = synth ? &c->synthDecay  : &c->adsrDecay;  maxVal = 5000.0f; break;
                    case 2: pVal = synth ? &c->synthSustain : &c->adsrSustain; maxVal = 1.0f; break;
                    case 3: pVal = synth ? &c->synthRelease : &c->adsrRelease; maxVal = 5000.0f; break;
                }

                if (pVal) {
                    seq_lock();
                    float delta;
                    if (adsrKnob == 2) {
                        // Sustain: 0.05 per scroll step
                        delta = (zDelta > 0) ? 0.05f : -0.05f;
                    } else {
                        // Time knobs: 250ms per scroll step
                        delta = (zDelta > 0) ? 250.0f : -250.0f;
                    }

                    float newVal = *pVal + delta;
                    if (newVal < 0.0f) newVal = 0.0f;
                    if (newVal > maxVal) newVal = maxVal;
                    *pVal = newVal;
                    g_Seq.isModified = true;
                    seq_unlock();

                    invalidate_midi_cache();
                    InvalidateRect(hwnd, NULL, FALSE);
                    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                    return 0; // Consumed by ADSR knob
                }
            }

            // --- OCTAVE BUTTON: wheel over the button shifts octaves ---
            // (1 step per notch; up = one octave up, down = one octave down)
            {
                RECT rcW; GetClientRect(hwnd, &rcW);
                int botY = (rcW.bottom - rcW.top) - scale_y(26);
                int btnH = scale_y(20);
                if (pt.x >= scale_x(106) && pt.x <= scale_x(210) && pt.y >= botY && pt.y <= botY + btnH) {
                    if (!midi_edit_is_quadrum()) {   // Quadrum has fixed voices
                        int dir = (zDelta > 0) ? 1 : -1;
                        midi_lock();
                        if (g_midiEdit.octaveShift < 3 && dir > 0) g_midiEdit.octaveShift++;
                        if (g_midiEdit.octaveShift > -3 && dir < 0) g_midiEdit.octaveShift--;
                        midi_unlock();
                        invalidate_midi_cache();
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }

            // --- EXISTING NOTE VELOCITY WHEEL HANDLING (unchanged below) ---
            int keysX, keysW, gridX, gridW, rollY, rollH;
            midi_edit_geom(hwnd, &keysX, &keysW, &gridX, &gridW, &rollY, &rollH);
            int edge = 0;
            int hit = midi_edit_get_note_under_mouse(c, pt.x, pt.y, gridX, gridW, rollY, rollH, &edge);

            seq_lock();
            int selCount = midi_edit_selected_count(c);
            bool modified = false;

            if (hit >= 0 && hit < c->midiNoteCount) {
                if (c->midiNotes[hit].isSelected && selCount > 1) {
                    for (int i = 0; i < c->midiNoteCount; ++i) {
                        if (c->midiNotes[i].isSelected)
                            c->midiNotes[i].velocity =
                                clamp(c->midiNotes[i].velocity + step, 0.0f, 100.0f);
                    }
                } else {
                    c->midiNotes[hit].velocity =
                        clamp(c->midiNotes[hit].velocity + step, 0.0f, 100.0f);
                }
                modified = true;
            }
            else if (selCount > 0) {
                for (int i = 0; i < c->midiNoteCount; ++i) {
                    if (c->midiNotes[i].isSelected)
                        c->midiNotes[i].velocity =
                            clamp(c->midiNotes[i].velocity + step, 0.0f, 100.0f);
                }
                modified = true;
            }

            if (modified) {
                g_timelineDirty = true;
                invalidate_midi_cache();
            }
            seq_unlock();

            if (modified) {
                InvalidateRect(hwnd, NULL, FALSE);
                if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HFONT oldFontMain = SELECT_UI_FONT(hdc);

            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left, h = rc.bottom - rc.top;
            if (w <= 0 || h <= 0) {
                SelectObject(hdc, oldFontMain);
                EndPaint(hwnd, &ps);
                return 0;
            }

            Clip* c = midi_edit_clip();

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
            HFONT oldFontMem = SELECT_UI_FONT(memDC);

            if (c) {
                int baseNote = midi_edit_get_base_note();

                 
                if (is_midi_piano_roll_dirty(w, h, c, baseNote)) {
                    update_midi_piano_roll_cache(hwnd, hdc, w, h, c, baseNote);
                    g_midiCacheInvalid = false;
                }

                if (g_midiCacheDC) {
                    BitBlt(memDC, 0, 0, w, h, g_midiCacheDC, 0, 0, SRCCOPY);

                     
                    draw_midi_dynamic_overlays(hwnd, memDC, w, h, c, baseNote);
                }
                else {
                    RECT fillRc = { 0, 0, w, h };
                    HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
                    FillRect(memDC, &fillRc, bg);
                    DeleteObject(bg);
                }
            }
            else {
                RECT fillRc = { 0, 0, w, h };
                HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
                FillRect(memDC, &fillRc, bg);
                DeleteObject(bg);
            }

            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldFontMem);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            SelectObject(hdc, oldFontMain);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            SetForegroundWindow(hwnd);
            SetFocus(hwnd);

            RECT playRc, genRc, sampRc, sfRc, instRc, sfXRc;
            midi_edit_toolbar_rects(&playRc, &genRc, &sampRc, &sfRc, &instRc, &sfXRc);
            Clip* c = midi_edit_clip();
            if (!c) { ShowWindow(hwnd, SW_HIDE); return 0; }

             
            // Quadrum/Halo synth clips have no piano-roll ADSR knobs (they use
            // their own engine envelopes), so skip the hit-test for them.
            if (!midi_edit_is_synth_kind()) {
            for (int i = 0; i < 4; ++i) {
                RECT krc;
                midi_adsr_knob_rect(i, &krc);
                if (mx >= krc.left && mx <= krc.right && my >= krc.top && my <= krc.bottom) {
                    midi_lock();
                    g_midiEdit.adsrDragKnob = i;
                    g_midiEdit.adsrDragStartY = my;
                    {
                        bool synth = midi_edit_is_synth_kind();
                        g_midiEdit.adsrDragStartVal =
                            (i == 0) ? (synth ? c->synthAttack  : c->adsrAttack)  :
                            (i == 1) ? (synth ? c->synthDecay   : c->adsrDecay)   :
                            (i == 2) ? (synth ? c->synthSustain : c->adsrSustain) :
                                       (synth ? c->synthRelease : c->adsrRelease);
                    }
                    midi_unlock();
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
            } // end !midi_edit_is_synth_kind (no piano-roll ADSR knobs for synth kinds)

            // [SYNTH]: toggle the synth interface (only for synth clips).
            // Checked BEFORE the sample/soundfont branches because for synth
            // clips those rects overlap this one and would otherwise swallow
            // the click.
            if (midi_edit_is_synth_kind()) {
                RECT synthClickRc;
                midi_edit_synth_btn_rect(&synthClickRc);
                if (mx >= synthClickRc.left && mx <= synthClickRc.right &&
                    my >= synthClickRc.top  && my <= synthClickRc.bottom) {
                    if (synth_ui_is_open() && IsWindowVisible(g_synthHwnd))
                        synth_ui_close();
                    else
                        synth_ui_open(hwnd);
                    return 0;
                }
            }

            if (mx >= sampRc.left && mx <= sampRc.right && my >= sampRc.top && my <= sampRc.bottom) {
                // Pure-synth rolls have no sample source.
                if (midi_edit_is_synth_kind()) return 0;
                // Greyed out while a soundfont is loaded (mutual exclusion).
                if (sfont_is_loaded()) return 0;
                
                OPENFILENAMEW ofn;
                wchar_t szFileW[MAX_PATH] = L"";
                ZeroMemory(&ofn, sizeof(ofn));
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"Audio (*.wav;*.mp3;*.flac;*.ogg)\0*.wav;*.mp3;*.flac;*.ogg\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = szFileW;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) {
                    char szFile[MAX_PATH];
                    if (wide_to_utf8_buf(szFileW, szFile, MAX_PATH) > 0) {
                        int idx = load_audio_file(szFile);
                        if (idx != -1) {
                            seq_lock();
                            c->sampleIndex = idx;
                            seq_unlock();
                            // A sample and a soundfont are mutually exclusive
                            // on the piano roll: loading a sample clears the
                            // soundfont so the clip naming scheme can't conflict.
                            sfont_clear();
                            g_timelineDirty = true;
                            invalidate_midi_cache();
                        }
                    }
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            if (mx >= playRc.left && mx <= playRc.right && my >= playRc.top && my <= playRc.bottom) {
                midi_lock();
                g_midiEdit.isAuditionPlaying = !g_midiEdit.isAuditionPlaying;
                g_midiEdit.auditionPlayheadBeat = 0.0;
                midi_unlock();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            // [LOAD SOUNDFONT]: pick an .sf2 and load it on the job thread.
            if (mx >= sfRc.left && mx <= sfRc.right && my >= sfRc.top && my <= sfRc.bottom) {
                if (midi_edit_is_synth_kind()) return 0;
                // Greyed out while the current clip has a sample attached
                // (mutual exclusion).
                {
                    Clip* cSf = midi_edit_clip();
                    if (cSf && cSf->sampleIndex >= 0) return 0;
                }
                OPENFILENAMEW ofn;
                wchar_t szFileW[MAX_PATH] = L"";
                ZeroMemory(&ofn, sizeof(ofn));
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"SoundFont (*.sf2)\0*.sf2\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = szFileW;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) {
                    char szFile[MAX_PATH];
                    if (wide_to_utf8_buf(szFileW, szFile, MAX_PATH) > 0) {
                        // A soundfont and a sample are mutually exclusive on
                        // the piano roll: loading a font detaches any sample
                        // from the current clip so the naming scheme can't
                        // conflict.
                        Clip* cFont = midi_edit_clip();
                        if (cFont && cFont->sampleIndex >= 0) {
                            seq_lock();
                            cFont->sampleIndex = -1;
                            seq_unlock();
                            g_timelineDirty = true;
                        }
                        sfont_load_async(szFile);
                    }
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            // Note icon: open the instrument selector for the loaded font.
            if (mx >= instRc.left && mx <= instRc.right && my >= instRc.top && my <= instRc.bottom) {
                if (!midi_edit_is_synth_kind()) sfont_open_inst_selector(hwnd);
                return 0;
            }

            // Red [X]: clears the soundfont AND any sample attached to the
            // current clip. The two sources are mutually exclusive, so X resets
            // both so the user can load whichever source they want next.
            if (mx >= sfXRc.left && mx <= sfXRc.right && my >= sfXRc.top && my <= sfXRc.bottom) {
                if (!midi_edit_is_synth_kind()) {
                    Clip* cX = midi_edit_clip();
                    if (cX && cX->sampleIndex >= 0) {
                        seq_lock();
                        cX->sampleIndex = -1;
                        seq_unlock();
                        g_timelineDirty = true;
                    }
                    sfont_clear();
                    invalidate_midi_cache();
                }
                InvalidateRect(hwnd, NULL, FALSE);
                if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                return 0;
            }

             
            {
                RECT genClickRc = genRc;
                if (mx >= genClickRc.left && mx <= genClickRc.right && my >= genClickRc.top && my <= genClickRc.bottom) {
                    open_seq_dialog(hwnd);
                    return 0;
                }
            }

             
            {
                RECT rcD; GetClientRect(hwnd, &rcD);
                int botY = (rcD.bottom - rcD.top) - scale_y(26);
                int btnH = scale_y(20);
                if (my >= botY && my <= botY + btnH) {
                    if (mx >= scale_x(14) && mx <= scale_x(100)) {
                        push_undo_state();
                        seq_lock();
                        c->midiNoteCount = 0;
                        g_timelineDirty = true;
                        invalidate_midi_cache();
                        seq_unlock();
                        g_midiEdit.selNote = -1;
                        g_midiEdit.dragNote = -1;
                        InvalidateRect(hwnd, NULL, FALSE);
                        if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                        return 0;
                    }
                    if (mx >= scale_x(106) && mx <= scale_x(210)) {
                        // Quadrum has fixed voices - no octave shifting.
                        if (!midi_edit_is_quadrum()) {
                            midi_lock();
                            if (g_midiEdit.octaveShift < 3) {
                                g_midiEdit.octaveShift++;
                                invalidate_midi_cache();
                            }
                            midi_unlock();
                        }
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                    if (mx >= scale_x(216) && mx <= scale_x(310)) {
                        g_midiKbMode = !g_midiKbMode;
                        if (g_midiKbMode) {
                            SetFocus(hwnd);
                        } else {
                            midi_lock();
                            midi_audition_clear_poly();
                            midi_unlock();
                        }
                        invalidate_midi_cache();
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                }
            }

            int keysX, keysW, gridX, gridW, rollY, rollH;
            midi_edit_geom(hwnd, &keysX, &keysW, &gridX, &gridW, &rollY, &rollH);
            float rowH = (float)rollH / (float)midi_edit_key_count();
            float clipLen = (c->lengthBeats > 0.01f) ? c->lengthBeats : 0.01f;
            float ppb = (float)gridW / clipLen;

             
            if (mx >= keysX && mx <= keysX + keysW && my >= rollY && my <= rollY + rollH) {
                int row = (int)((my - rollY) / rowH);
                if (row < 0) row = 0;
                if (row >= midi_edit_key_count()) row = midi_edit_key_count() - 1;
                int pitch = midi_edit_row_to_pitch(row);
                midi_lock();
                midi_audition_set_mouse(pitch);
                midi_unlock();
                SetCapture(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            if (mx >= gridX && mx <= gridX + gridW && my >= rollY && my <= rollY + rollH) {
                bool ctrl  = (wParam & MK_CONTROL) != 0;
                bool shift = (wParam & MK_SHIFT) != 0;
                int edge = 0;
                int hit = midi_edit_get_note_under_mouse(c, mx, my, gridX, gridW, rollY, rollH, &edge);
                float clickBeat = (float)(mx - gridX) / ppb;

                 
                if (hit < 0) {
                    seq_lock();
                    if (!shift && !ctrl) midi_edit_clear_selection(c);
                    for (int i = 0; i < c->midiNoteCount && i < MIDI_MAX_NOTES; ++i)
                        g_midiMarqueeBaseSel[i] = c->midiNotes[i].isSelected;
                    seq_unlock();
                    g_midiMarqueeAdditive = shift;

                    g_midiEdit.dragMode = MIDI_DRAG_MARQUEE;
                    g_midiEdit.isMarqueeSelecting = true;
                    g_midiEdit.hasMovedPastThreshold = false;
                    g_midiEdit.marqueeStartX = g_midiEdit.marqueeCurX = mx;
                    g_midiEdit.marqueeStartY = g_midiEdit.marqueeCurY = my;
                    g_midiEdit.dragStartX = mx;
                    g_midiEdit.dragStartY = my;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }

                 
                if (shift) {
                    seq_lock();
                    c->midiNotes[hit].isSelected = !c->midiNotes[hit].isSelected;
                    seq_unlock();
                    g_midiEdit.selNote = hit;
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }

                 
                seq_lock();
                MidiNote* n = &c->midiNotes[hit];
                 
                if (!(ctrl && edge == 0)) {
                    bool inMulti = (midi_edit_selected_count(c) > 1 && n->isSelected);
                    if (inMulti) {
                        g_midiEdit.pendingSingleSelectNote = hit;
                    }
                    else {
                        midi_edit_clear_selection(c);
                        n->isSelected = true;
                    }
                }
                g_midiEdit.selNote = hit;
                g_midiEdit.dragNote = hit;
                g_midiEdit.dragMode = (edge == 1) ? MIDI_DRAG_RESIZE_L
                                    : (edge == 2) ? MIDI_DRAG_RESIZE_R
                                    : MIDI_DRAG_MOVE;
                g_midiEdit.isCtrlDuplicating = (ctrl && edge == 0);
                g_midiEdit.hasMovedPastThreshold = false;
                midi_edit_snap_selection_origins(c);
                g_midiEdit.dragStartX = mx;
                g_midiEdit.dragStartY = my;
                g_midiEdit.dragStartBeatOffset = clickBeat;
                g_midiEdit.dragLeadBeatOrig = n->startBeat;
                g_midiEdit.dragLeadPitchOrig = n->pitch;
                seq_unlock();

                push_undo_state();
                SetCapture(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            return 0;
        }

        case WM_RBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            Clip* c = midi_edit_clip();
            if (!c) return 0;

             
            {
                RECT rcD; GetClientRect(hwnd, &rcD);
                int botY = (rcD.bottom - rcD.top) - scale_y(26);
                if (my >= botY && my <= botY + scale_y(20) && mx >= scale_x(106) && mx <= scale_x(210)) {
                    // Quadrum has fixed voices - no octave shifting.
                    if (!midi_edit_is_quadrum()) {
                        midi_lock();
                        if (g_midiEdit.octaveShift > -3) {
                            g_midiEdit.octaveShift--;
                            invalidate_midi_cache();
                        }
                        midi_unlock();
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }

            int keysX, keysW, gridX, gridW, rollY, rollH;
            midi_edit_geom(hwnd, &keysX, &keysW, &gridX, &gridW, &rollY, &rollH);
            if (mx >= gridX && mx <= gridX + gridW && my >= rollY && my <= rollY + rollH) {
                int hit = midi_edit_get_note_under_mouse(c, mx, my, gridX, gridW, rollY, rollH, NULL);
                if (hit >= 0) {
                    push_undo_state();
                    seq_lock();
                    if (c->midiNotes[hit].isSelected) {
                         
                        midi_edit_delete_selected(c);
                    } else {
                         
                        for (int i = hit; i < c->midiNoteCount - 1; ++i)
                            c->midiNotes[i] = c->midiNotes[i + 1];
                        c->midiNoteCount--;
                        if (g_midiEdit.selNote == hit) g_midiEdit.selNote = -1;
                        if (g_midiEdit.dragNote == hit) g_midiEdit.dragNote = -1;
                        g_timelineDirty = true;
                        invalidate_midi_cache();
                    }
                    seq_unlock();
                    InvalidateRect(hwnd, NULL, FALSE);
                    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                }
                else {
                     
                    g_midiMarqueeAdditive = false;
                    g_midiEdit.dragMode = MIDI_DRAG_MARQUEE;
                    g_midiEdit.isMarqueeSelecting = true;
                    g_midiEdit.hasMovedPastThreshold = false;
                    g_midiEdit.marqueeStartX = g_midiEdit.marqueeCurX = mx;
                    g_midiEdit.marqueeStartY = g_midiEdit.marqueeCurY = my;
                    g_midiEdit.dragStartX = mx;
                    g_midiEdit.dragStartY = my;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            Clip* c = midi_edit_clip();
            if (g_midiEdit.isMarqueeSelecting && g_midiEdit.dragMode == MIDI_DRAG_MARQUEE) {
                if (!g_midiEdit.hasMovedPastThreshold && c) {
                    seq_lock();
                    midi_edit_clear_selection(c);
                    seq_unlock();
                    invalidate_midi_cache();
                }
                g_midiEdit.isMarqueeSelecting = false;
                g_midiEdit.dragMode = MIDI_DRAG_NONE;
                if (GetCapture() == hwnd) ReleaseCapture();
                InvalidateRect(hwnd, NULL, FALSE);
                if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            Clip* c = midi_edit_clip();
            if (!c) return 0;

             
            if (g_midiEdit.adsrDragKnob >= 0 && g_midiEdit.adsrDragKnob < 4 && GetCapture() == hwnd) {
                int k = g_midiEdit.adsrDragKnob;
                int dy = g_midiEdit.adsrDragStartY - my;
                float v = g_midiEdit.adsrDragStartVal;
                if (k == 2) {
                    v += (float)dy * 0.005f;
                    if (v < 0.0f) v = 0.0f;
                    if (v > 1.0f) v = 1.0f;
                } else {
                    v += (float)dy * 20.0f;
                    if (v < 0.0f) v = 0.0f;
                    if (v > 5000.0f) v = 5000.0f;
                }
                seq_lock();
                bool synth = midi_edit_is_synth_kind();
                if (k == 0) { if (synth) c->synthAttack = v; else c->adsrAttack = v; }
                else if (k == 1) { if (synth) c->synthDecay = v; else c->adsrDecay = v; }
                else if (k == 2) { if (synth) c->synthSustain = v; else c->adsrSustain = v; }
                else { if (synth) c->synthRelease = v; else c->adsrRelease = v; }
                g_Seq.isModified = true;
                seq_unlock();
                InvalidateRect(hwnd, NULL, FALSE);
                if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                return 0;
            }

            int keysX, keysW, gridX, gridW, rollY, rollH;
            midi_edit_geom(hwnd, &keysX, &keysW, &gridX, &gridW, &rollY, &rollH);
            float rowH = (float)rollH / (float)midi_edit_key_count();
            float clipLen = (c->lengthBeats > 0.01f) ? c->lengthBeats : 0.01f;
            float ppb = (float)gridW / clipLen;

             
            if (g_midiEdit.auditionHeld && GetCapture() == hwnd &&
                mx >= keysX && mx <= keysX + keysW && my >= rollY && my <= rollY + rollH) {
                int row = (int)((my - rollY) / rowH);
                if (row < 0) row = 0;
                if (row >= midi_edit_key_count()) row = midi_edit_key_count() - 1;
                int pitch = midi_edit_row_to_pitch(row);
                bool changed = false;
                midi_lock();
                if (pitch != g_midiEdit.mousePitch) {
                    midi_audition_set_mouse(pitch);
                    changed = true;
                }
                midi_unlock();
                if (changed) InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            int mode = g_midiEdit.dragMode;
            if (mode == MIDI_DRAG_NONE) {
                 
                if (mx >= gridX && mx <= gridX + gridW && my >= rollY && my <= rollY + rollH) {
                    int e = 0;
                    int hit = midi_edit_get_note_under_mouse(c, mx, my, gridX, gridW, rollY, rollH, &e);
                    SetCursor(LoadCursor(NULL, (hit >= 0 && e != 0) ? IDC_SIZEWE : IDC_ARROW));
                }
                return 0;
            }

             
            if (mode == MIDI_DRAG_MARQUEE) {
                g_midiEdit.marqueeCurX = mx;
                g_midiEdit.marqueeCurY = my;
                if (abs(mx - g_midiEdit.dragStartX) > get_drag_threshold() ||
                    abs(my - g_midiEdit.dragStartY) > get_drag_threshold())
                    g_midiEdit.hasMovedPastThreshold = true;

                if (g_midiEdit.hasMovedPastThreshold) {
                    float mppb = (float)gridW / clipLen;
                    float b1 = (float)(min(g_midiEdit.marqueeStartX, g_midiEdit.marqueeCurX) - gridX) / mppb;
                    float b2 = (float)(max(g_midiEdit.marqueeStartX, g_midiEdit.marqueeCurX) - gridX) / mppb;
                    int rowA = (int)((min(g_midiEdit.marqueeStartY, g_midiEdit.marqueeCurY) - rollY) / rowH);
                    int rowB = (int)((max(g_midiEdit.marqueeStartY, g_midiEdit.marqueeCurY) - rollY) / rowH);
                    if (rowA < 0) rowA = 0;
                    if (rowA >= midi_edit_key_count()) rowA = midi_edit_key_count() - 1;
                    if (rowB < 0) rowB = 0;
                    if (rowB >= midi_edit_key_count()) rowB = midi_edit_key_count() - 1;
                    int pHi = midi_edit_row_to_pitch(rowA);
                    int pLo = midi_edit_row_to_pitch(rowB);

                    seq_lock();
                    midi_edit_live_marquee(c, b1, b2, pLo, pHi);
                    seq_unlock();
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

             
            if (g_midiEdit.isCtrlDuplicating && !g_midiEdit.hasMovedPastThreshold) {
                if (abs(mx - g_midiEdit.dragStartX) > get_drag_threshold() ||
                    abs(my - g_midiEdit.dragStartY) > get_drag_threshold()) {
                    g_midiEdit.hasMovedPastThreshold = true;
                    push_undo_state();
                    seq_lock();
                    int leadCopy = -1;
                    int count = c->midiNoteCount;
                    for (int i = 0; i < count && c->midiNoteCount < MIDI_MAX_NOTES; ++i) {
                        if (!c->midiNotes[i].isSelected) continue;
                        int dst = c->midiNoteCount++;
                        c->midiNotes[dst] = c->midiNotes[i];
                        c->midiNotes[i].isSelected = false;    
                        if (i == g_midiEdit.dragNote) leadCopy = dst;
                    }
                    if (leadCopy >= 0) {
                        g_midiEdit.dragNote = leadCopy;
                        g_midiEdit.selNote = leadCopy;
                    }
                    midi_edit_snap_selection_origins(c);
                    g_timelineDirty = true;
                    invalidate_midi_cache();
                    seq_unlock();
                    InvalidateRect(hwnd, NULL, FALSE);
                    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                }
                return 0;
            }

            if (g_midiEdit.dragNote < 0 || g_midiEdit.dragNote >= c->midiNoteCount) return 0;
            float clickBeat = (float)(mx - gridX) / ppb;
            float deltaBeat = quantize_beat_16th(clickBeat - g_midiEdit.dragStartBeatOffset);

             
            if (g_midiEdit.pendingSingleSelectNote >= 0 &&
                (abs(mx - g_midiEdit.dragStartX) > get_drag_threshold() / 2 ||
                 abs(my - g_midiEdit.dragStartY) > get_drag_threshold() / 2)) {
                g_midiEdit.pendingSingleSelectNote = -1;
            }

            seq_lock();
            if (mode == MIDI_DRAG_MOVE) {
                int row = (int)((my - rollY) / rowH);
                if (row < 0) row = 0;
                if (row >= midi_edit_key_count()) row = midi_edit_key_count() - 1;
                int pitchDelta = midi_edit_row_to_pitch(row) - g_midiEdit.dragLeadPitchOrig;

                // Valid pitch range is [base, base+keys-1]. Quadrum's base is 0
                // (8 voices); MIDI/Halo sit on a base note (e.g. C4=60). Clamping
                // to [0, keys-1] would shove MIDI/Halo notes far off the roll.
                int pitchLo = midi_edit_base_pitch();
                int pitchHi = midi_edit_base_pitch() + midi_edit_key_count() - 1;

                for (int i = 0; i < c->midiNoteCount; ++i) {
                    MidiNote* n = &c->midiNotes[i];
                    if (!n->isSelected) continue;
                    float ns = n->dragStartBeatOrig + deltaBeat;
                    if (ns < 0.0f) ns = 0.0f;
                    if (ns + n->lengthBeats > clipLen) ns = clipLen - n->lengthBeats;
                    if (ns < 0.0f) ns = 0.0f;
                    n->startBeat = ns;

                    int np = n->dragPitchOrig + pitchDelta;
                    if (np < pitchLo) np = pitchLo;
                    if (np > pitchHi) np = pitchHi;
                    n->pitch = np;
                }
            }
            else if (mode == MIDI_DRAG_RESIZE_R) {
                float minLen = grid_division_beat_fraction(g_Seq.gridDivision);
                if (minLen < 0.05f) minLen = 0.05f;
                for (int i = 0; i < c->midiNoteCount; ++i) {
                    MidiNote* n = &c->midiNotes[i];
                    if (!n->isSelected) continue;
                    float nl = n->dragLengthOrig + deltaBeat;
                    if (n->startBeat + nl > clipLen) nl = clipLen - n->startBeat;
                    if (nl < minLen) nl = minLen;
                    n->lengthBeats = nl;
                }
            }
            else if (mode == MIDI_DRAG_RESIZE_L) {
                float minLen = grid_division_beat_fraction(g_Seq.gridDivision);
                if (minLen < 0.05f) minLen = 0.05f;
                for (int i = 0; i < c->midiNoteCount; ++i) {
                    MidiNote* n = &c->midiNotes[i];
                    if (!n->isSelected) continue;
                    float rightEdge = n->dragStartBeatOrig + n->dragLengthOrig;   
                    float ns = n->dragStartBeatOrig + deltaBeat;
                    float nl = rightEdge - ns;
                    if (nl < minLen) { nl = minLen; ns = rightEdge - minLen; }
                    if (ns < 0.0f) { ns = 0.0f; nl = rightEdge; }
                    if (ns + nl > clipLen) { nl = clipLen - ns; if (nl < minLen) { nl = minLen; ns = clipLen - minLen; if (ns < 0.0f) { ns = 0.0f; nl = clipLen; } } }
                    n->startBeat = ns;
                    n->lengthBeats = nl;
                }
            }
            g_timelineDirty = true;
            invalidate_midi_cache();
            seq_unlock();

            InvalidateRect(hwnd, NULL, FALSE);
            if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
            return 0;
        }

        case WM_LBUTTONUP: {
            Clip* c = midi_edit_clip();

             
            if (g_midiEdit.adsrDragKnob >= 0) {
                g_midiEdit.adsrDragKnob = -1;
                ReleaseCapture();
                InvalidateRect(hwnd, NULL, FALSE);
                if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                return 0;
            }

             
            midi_lock();
            midi_audition_set_mouse(-1);
            midi_unlock();

             
            if (g_midiEdit.pendingSingleSelectNote >= 0 && c) {
                seq_lock();
                int h = g_midiEdit.pendingSingleSelectNote;
                if (h < c->midiNoteCount) {
                    midi_edit_clear_selection(c);
                    c->midiNotes[h].isSelected = true;
                }
                g_midiEdit.pendingSingleSelectNote = -1;
                seq_unlock();
            }

             
            if (g_midiEdit.dragMode == MIDI_DRAG_MARQUEE && c) {
                if (!g_midiEdit.hasMovedPastThreshold) {
                     
                    int keysX, keysW, gridX, gridW, rollY, rollH;
                    midi_edit_geom(hwnd, &keysX, &keysW, &gridX, &gridW, &rollY, &rollH);
                    float rowH = (float)rollH / (float)midi_edit_key_count();
                    float clipLen = (c->lengthBeats > 0.01f) ? c->lengthBeats : 0.01f;
                    float ppb = (float)gridW / clipLen;

                    float clickBeat = (float)(g_midiEdit.dragStartX - gridX) / ppb;
                    // Snap DOWN to the clicked grid cell so the note lands
                    // where the user clicked (round-half-up would push hits in
                    // the right half of a cell one cell to the right).
                    float startBeat = quantize_beat_floor(clickBeat);
                    float len = quantize_beat_16th(0.25f);
                    if (len < 0.05f) len = 0.05f;
                    // Boundary check: the placed note must sit entirely inside
                    // [0, clipLen] and stay at least min-len long.
                    if (startBeat < 0.0f) startBeat = 0.0f;
                    if (len > clipLen) len = clipLen;
                    if (startBeat >= clipLen) startBeat = clipLen - len;
                    if (startBeat < 0.0f) startBeat = 0.0f;
                    if (startBeat + len > clipLen) len = clipLen - startBeat;
                    if (len < 0.02f) len = 0.02f;

                    int row = (int)((g_midiEdit.dragStartY - rollY) / rowH);
                    if (row < 0) row = 0;
                    if (row >= midi_edit_key_count()) row = midi_edit_key_count() - 1;

                    push_undo_state();
                    seq_lock();
                    if (c->midiNoteCount < MIDI_MAX_NOTES) {
                        midi_edit_clear_selection(c);
                        int idx = c->midiNoteCount++;
                        MidiNote* nn = &c->midiNotes[idx];
                        memset(nn, 0, sizeof(MidiNote));
                        nn->pitch = midi_edit_row_to_pitch(row);
                        nn->startBeat = startBeat;
                        nn->lengthBeats = len;
                        nn->velocity = 100.0f;
                        nn->active = true;
                        nn->isSelected = true;
                        g_midiEdit.selNote = idx;
                        g_timelineDirty = true;
                        invalidate_midi_cache();
                    }
                    seq_unlock();
                }
                else {
                     
                    int keysX, keysW, gridX, gridW, rollY, rollH;
                    midi_edit_geom(hwnd, &keysX, &keysW, &gridX, &gridW, &rollY, &rollH);
                    float rowH = (float)rollH / (float)midi_edit_key_count();
                    float ppb = (float)gridW / ((c->lengthBeats > 0.01f) ? c->lengthBeats : 0.01f);

                    float b1 = (float)(min(g_midiEdit.marqueeStartX, g_midiEdit.marqueeCurX) - gridX) / ppb;
                    float b2 = (float)(max(g_midiEdit.marqueeStartX, g_midiEdit.marqueeCurX) - gridX) / ppb;
                    int rowA = (int)((min(g_midiEdit.marqueeStartY, g_midiEdit.marqueeCurY) - rollY) / rowH);
                    int rowB = (int)((max(g_midiEdit.marqueeStartY, g_midiEdit.marqueeCurY) - rollY) / rowH);
                    if (rowA < 0) rowA = 0;
                    if (rowB >= midi_edit_key_count()) rowB = midi_edit_key_count() - 1;
                    int pHi = midi_edit_row_to_pitch(rowA);
                    int pLo = midi_edit_row_to_pitch(rowB);

                    seq_lock();
                    midi_edit_live_marquee(c, b1, b2, pLo, pHi);
                    seq_unlock();
                }
            }

            g_midiEdit.isMarqueeSelecting = false;
            g_midiEdit.dragMode = MIDI_DRAG_NONE;
            g_midiEdit.dragNote = -1;
            g_midiEdit.isCtrlDuplicating = false;
            g_midiEdit.hasMovedPastThreshold = false;
            g_midiEdit.pendingSingleSelectNote = -1;
            if (GetCapture() == hwnd) ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
            if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
            return 0;
        }

        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                midi_lock();
                midi_audition_clear_poly();
                g_midiEdit.isAuditionPlaying = false;
                midi_unlock();
                ShowWindow(hwnd, SW_HIDE);
                if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                return 0;
            }

            Clip* c = midi_edit_clip();
            if (!c) return 0;
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

            if (wParam == VK_DELETE || wParam == VK_BACK) {
                bool any = false;
                seq_lock();
                any = (midi_edit_selected_count(c) > 0);
                seq_unlock();
                if (any) {
                    push_undo_state();
                    seq_lock();
                    midi_edit_delete_selected(c);
                    seq_unlock();
                    InvalidateRect(hwnd, NULL, FALSE);
                    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                }
                return 0;
            }

            if (ctrl && wParam == 'A') {
                seq_lock();
                midi_edit_select_all_notes(c);
                seq_unlock();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (ctrl && wParam == 'D') {
                seq_lock();
                midi_edit_clear_selection(c);
                seq_unlock();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (ctrl && wParam == 'C') {
                seq_lock();
                midi_edit_copy_selected(c);
                seq_unlock();
                return 0;
            }
            if (ctrl && wParam == 'V') {
                midi_lock();
                float base = quantize_beat_16th((float)g_midiEdit.auditionPlayheadBeat);
                midi_unlock();
                push_undo_state();
                bool ok = false;
                seq_lock();
                ok = midi_edit_paste_clipboard(c, base);
                seq_unlock();
                InvalidateRect(hwnd, NULL, FALSE);
                if (ok && g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                return 0;
            }
            // Keyboard note audition (A-P QWERTY row, relative to the roll's
            // current octave; Quadrum maps to its 8 drum voices). Only fires
            // when the toggle is on and no modifiers are held. Notes join the
            // same polyphonic held-set as the mouse strip, so keyboard and
            // mouse clicks/chords coexist up to MIDI_KB_MAX voices.
            if (g_midiKbMode) {
                int semi = pr_key_to_semitone((int)wParam);
                if (semi >= 0) {
                    int pitch = midi_edit_is_quadrum() ? (semi % 8)
                                                       : (midi_edit_get_base_note() + semi);
                    midi_lock();
                    midi_audition_kb_add((int)wParam, pitch);
                    midi_unlock();
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
            // 'V' (no ctrl): randomize the velocity of every present note.
            if (!ctrl && wParam == 'V') {
                if (c->midiNoteCount > 0) {
                    push_undo_state();
                    seq_lock();
                    // PRNG, never rand(): deterministic-ish, time-seeded once.
                    static uint32_t s_velRng = 0u;
                    if (!s_velRng) {
                        s_velRng = ((uint32_t)GetTickCount() * 2654435761u)
                                 ^ ((uint32_t)GetTickCount64() << 7) ^ 0x9E3779B9u;
                        if (!s_velRng) s_velRng = 0x6C078965u;
                    }
                    for (int i = 0; i < c->midiNoteCount; ++i) {
                        // xorshift32
                        s_velRng ^= s_velRng << 13;
                        s_velRng ^= s_velRng >> 17;
                        s_velRng ^= s_velRng << 5;
                        if (!s_velRng) s_velRng = 0x9E3779B9u;
                        int v = 20 + (int)((s_velRng >> 8) % 101u);   // 20..120
                        c->midiNotes[i].velocity = (float)v;
                    }
                    g_Seq.isModified = true;
                    seq_unlock();
                    invalidate_midi_cache();
                    InvalidateRect(hwnd, NULL, FALSE);
                    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                }
                return 0;
            }
            // 'H': open the humanization dialog (velocity + timing humanize).
            if (!ctrl && wParam == 'H') {
                open_humanize_dialog(hwnd);
                return 0;
            }
            return 0;
        }

        case WM_KEYUP: {
            // Release by the physical key: each entry's pitch was resolved at
            // press time, so re-resolving here (current octave) could miss
            // the entry and strand the held note.
            if (g_midiKbMode && pr_key_to_semitone((int)wParam) >= 0) {
                midi_lock();
                midi_audition_kb_remove((int)wParam);
                midi_unlock();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            break;
        }

        case WM_CLOSE:
            // Stop any preview that was playing before hiding, so a Halo /
            // Quadrum / MIDI audition doesn't keep sounding after the modal is
            // closed via the title bar (WM_DESTROY never runs on a hide-only
            // close, and the audio thread keeps looping while audHeld/playing).
            midi_lock();
            midi_audition_clear_poly();
            g_midiEdit.isAuditionPlaying = false;
            midi_unlock();
            // Closing the piano roll also closes the synth interface popup.
            synth_ui_close();
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, 1);
            // Destroying the piano roll tears down the synth UI as well.
            synth_ui_destroy();
             
            if (g_midiCacheDC) {
                if (g_midiCacheBmp) {
                    SelectObject(g_midiCacheDC, g_midiCacheOldBmp);
                    DeleteObject(g_midiCacheBmp);
                    g_midiCacheBmp = NULL;
                }
                DeleteDC(g_midiCacheDC);
                g_midiCacheDC = NULL;
            }
            g_midiCacheW = 0;
            g_midiCacheH = 0;
            g_midiCacheInvalid = true;
            g_midiHwnd = NULL;
            midi_lock();
            g_midiEdit.clipIdx = -1;
            g_midiEdit.dragMode = MIDI_DRAG_NONE;
            g_midiEdit.dragNote = -1;
            g_midiEdit.selNote = -1;
            midi_audition_clear_poly();
            g_midiEdit.isAuditionPlaying = false;
            g_midiEdit.isMarqueeSelecting = false;
            g_midiEdit.isCtrlDuplicating = false;
            g_midiEdit.hasMovedPastThreshold = false;
            g_midiEdit.pendingSingleSelectNote = -1;
            midi_unlock();
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static inline void open_midi_editor(HWND parentHwnd, int clipIdx) {
    seq_lock();
    bool valid = (clipIdx >= 0 && clipIdx < g_Seq.clipCount && g_Seq.clips[clipIdx].isMidi);
    seq_unlock();
    if (!valid) return;

    if (!g_midiHwnd || !IsWindow(g_midiHwnd)) {
        static bool s_registered = false;
        if (!s_registered) {
            WNDCLASSA wc = { 0 };
            wc.lpfnWndProc = MidiEditorWndProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractMidiEditClass";
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_registered = true;
        }

        int rw = 820, rh = 540;
        int scrW = GetSystemMetrics(SM_CXSCREEN), scrH = GetSystemMetrics(SM_CYSCREEN);
        int rx = (scrW - rw) / 2, ry = (scrH - rh) / 2;
        if (parentHwnd && IsWindow(parentHwnd)) {
            RECT prc;
            GetWindowRect(parentHwnd, &prc);
            rx = prc.left + ((prc.right - prc.left) - rw) / 2;
            ry = prc.top + ((prc.bottom - prc.top) - rh) / 2;
        }

        g_midiHwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW,
            "RefractMidiEditClass", "MIDI Editor",
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE | WS_SIZEBOX,
            rx, ry, rw, rh,
            parentHwnd, NULL, GetModuleHandle(NULL), NULL
        );
        SetTimer(g_midiHwnd, 1, 33, NULL);
        DragAcceptFiles(g_midiHwnd, TRUE);
    }

    midi_lock();
    g_midiEdit.clipIdx = clipIdx;
    g_midiEdit.selNote = -1;
    g_midiEdit.dragMode = MIDI_DRAG_NONE;
    g_midiEdit.dragNote = -1;
    g_midiEdit.adsrDragKnob = -1;   // no ADSR knob drag until one is grabbed
    midi_audition_clear_poly();
    g_midiEdit.isAuditionPlaying = false;
    g_midiEdit.auditionPlayheadBeat = 0.0;
    g_midiEdit.isMarqueeSelecting = false;
    g_midiEdit.isCtrlDuplicating = false;
    g_midiEdit.hasMovedPastThreshold = false;
    g_midiEdit.pendingSingleSelectNote = -1;
    g_midiEdit.copyCount = 0;
    g_midiEdit.octaveShift = 0;   // every clip opens at C4..B5
    midi_unlock();
    // Kind is read under the seq lock (clipKind is audio-shared state).
    seq_lock();
    g_midiEdit.editKind = (g_Seq.clips[clipIdx].clipKind == CLIP_KIND_QUADRUM)
                        ? MIDI_EDIT_KIND_QUADRUM
                        : (g_Seq.clips[clipIdx].clipKind == CLIP_KIND_HALO)
                        ? MIDI_EDIT_KIND_HALO : MIDI_EDIT_KIND_MIDI;
    seq_unlock();
    invalidate_midi_cache();
    seq_lock();
    {
        Clip* cOpen = &g_Seq.clips[clipIdx];
        midi_edit_clear_selection(cOpen);
    }
    seq_unlock();

    char title[96];
    const char* kindName = (g_midiEdit.editKind == MIDI_EDIT_KIND_QUADRUM) ? "quadrum - Drum Editor"
                         : (g_midiEdit.editKind == MIDI_EDIT_KIND_HALO)    ? "halo - Synth Editor"
                         : "MIDI Editor";
    snprintf(title, sizeof(title), "%s - Clip %d", kindName, clipIdx + 1);
    SetWindowTextA(g_midiHwnd, title);

    // Quadrum's 8-row roll is shorter than the 24-key MIDI/Halo rolls; give
    // it a slightly reduced height so the editor isn't taller than it needs.
    // Restore the standard height when a non-Quadrum clip is opened.
    {
        RECT wrc; GetWindowRect(g_midiHwnd, &wrc);
        int w = wrc.right - wrc.left;
        int h = wrc.bottom - wrc.top;
        int wantH = (g_midiEdit.editKind == MIDI_EDIT_KIND_QUADRUM) ? 470 : 540;
        if (h != wantH) {
            SetWindowPos(g_midiHwnd, NULL, wrc.left, wrc.top, w, wantH, SWP_NOZORDER);
            InvalidateRect(g_midiHwnd, NULL, TRUE);
        }
    }

    ShowWindow(g_midiHwnd, SW_SHOW);
    SetForegroundWindow(g_midiHwnd);
    InvalidateRect(g_midiHwnd, NULL, FALSE);

    // Logic gate: opening a synth clip's piano roll also opens the synth
    // interface popup. Closing the piano roll (below) closes both; closing the
    // synth UI alone leaves the piano roll open, and the [SYNTH] toolbar
    // button reopens it.
    if (midi_edit_is_synth_kind()) {
        synth_ui_open(g_midiHwnd);
    }
}

 
static inline int midi_edit_add_note(Clip* c, int pitch, float startBeat, float len, float velocity) {
    if (!c || c->midiNoteCount >= MIDI_MAX_NOTES) return -1;
    if (startBeat < 0.0f) startBeat = 0.0f;
    if (len < 0.02f) len = 0.02f;
    if (startBeat + len > c->lengthBeats) {
        len = c->lengthBeats - startBeat;
        if (len < 0.02f) return -1;
    }
    int idx = c->midiNoteCount++;
    MidiNote* nn = &c->midiNotes[idx];
    memset(nn, 0, sizeof(MidiNote));
    nn->pitch = pitch;
    nn->startBeat = startBeat;
    nn->lengthBeats = len;
    nn->velocity = velocity;
    nn->active = true;
    nn->isSelected = true;
    return idx;
}

 
typedef struct {
    HWND  hwnd;
    HWND  octLoEdit, octHiEdit, lenEdit, velLoEdit, velHiEdit, countEdit;
    HWND  seedEdit;
    WNDPROC octLoDef, octHiDef, lenDef, velLoDef, velHiDef, countDef;
    WNDPROC seedDef;
    // Hand-painted dropdowns (no win32 COMBOBOX — its classic nonclient
    // frame and button can't be fully skinned). Items live in static tables
    // set at creation; sel[] holds the current choice per dropdown.
    HWND  listHwnd;                 // popup list window while dropped
    int   openIdx;                  // which dropdown is open (-1 = none)
    int   hoverItem;                // hovered row in the open list (-1 = none)
    int   sel[4];                   // Root/Scale/Chord/Pattern selections
} SeqDialogState;
static SeqDialogState g_seqDlg = { 0 };

// Shared seq-dialog geometry. Both control creation (open_seq_dialog) and
// WM_PAINT label drawing derive their positions from this so they can't drift.
#define SEQ_ROW_STEP 26
#define SEQ_COMBO_H 22    // closed dropdown field height
#define SEQ_EDIT_H 20
#define SEQ_CLIENT_W 486
#define SEQ_CLIENT_H 236

// Dropdown item tables shared by the painted fields, the popup list and
// seq_dialog_apply. Compile-time constants — always initialized before any
// window that reads them can exist. Order matches the layout's dropdowns.
static const char* const kSeqRootItems[]  = { "C4", "C#4", "D4", "D#4", "E4", "F4", "F#4", "G4", "G#4", "A4", "A#4", "B4" };
static const char* const kSeqScaleItems[] = { "Major", "Minor" };
static const char* const kSeqChordItems[] = { "Major", "Minor", "Diminished", "Augmented", "Seventh" };
static const char* const kSeqArpItems[]   = { "Up", "Down", "Up-Down", "Random", "Order" };

static const char* const* const kSeqDropItems[4] = {
    kSeqRootItems,
    kSeqScaleItems,
    kSeqChordItems,
    kSeqArpItems
};
static const int kSeqDropCount[4] = { 12, 2, 5, 5 };

// Dice button beside the Seed row; x/y are filled in by seq_dialog_layout.
static RECT g_seqDiceRc = { 0, 0, 0, 0 };

static inline void seq_dialog_layout(int w, int h,
                                     RECT* comboLabels, RECT* combos,
                                     RECT* editLabels, RECT* edits,
                                     RECT* okRc, RECT* noRc) {
    const int mL = scale_x(16), mT = scale_y(12);
    const int lblW = scale_x(88);
    const int colGap = scale_x(8);
    const int midGap = scale_x(14);   // space between the two columns so the
                                       // left dropdowns don't touch the labels
    const int comboH = scale_y(22);
    const int rowStep = scale_y(SEQ_ROW_STEP);

    // Split the usable width into two equal columns so the Root/Scale/Chord/
    // Pattern area and the settings area (with the Seed) take equal space.
    int usable = w - 2 * mL - midGap;
    int colW = usable / 2;
    int colL = mL;
    int colR = mL + colW + midGap;

    // Left column: 4 combo rows. Right column: 7 edit rows (Seed last),
    // all on one shared row grid so labels never clip.
    for (int i = 0; i < 4; ++i) {
        int y = mT + i * rowStep;
        SetRect(&comboLabels[i], colL, y, colL + lblW, y + comboH);
        SetRect(&combos[i], colL + lblW + colGap, y, colL + colW, y + comboH);
    }
    const int editStep = scale_y(SEQ_ROW_STEP);
    for (int i = 0; i < 7; ++i) {
        int y = mT + i * editStep;
        SetRect(&editLabels[i], colR, y, colR + lblW, y + comboH);
        SetRect(&edits[i], colR + lblW + colGap, y, colR + colW, y + scale_y(SEQ_EDIT_H));
    }

    // Dice sits just left of the Seed edit box: a small square icon.
    {
        RECT seedEd = edits[6];
        int ds = scale_y(18);
        int cy = (seedEd.top + seedEd.bottom) / 2;
        g_seqDiceRc.left   = seedEd.left - ds - scale_x(6);
        g_seqDiceRc.top    = cy - ds / 2;
        g_seqDiceRc.right  = seedEd.left - scale_x(6);
        g_seqDiceRc.bottom = g_seqDiceRc.top + ds;
    }

    // Buttons fit "GENERATE (ENTER)" / "CANCEL [ESC]" text comfortably.
    int btnW = scale_x(140), btnH = scale_y(26);
    int btnY = h - btnH - scale_y(10);
    int gap = scale_x(14);
    int okX = w / 2 - btnW - gap / 2;
    int noX = w / 2 + gap / 2;
    if (okRc) SetRect(okRc, okX, btnY, okX + btnW, btnY + btnH);
    if (noRc) SetRect(noRc, noX, btnY, noX + btnW, btnY + btnH);
}

static void seq_dialog_apply(HWND hwnd) {
    char buf[16];
    int rootSel = g_seqDlg.sel[0]; if (rootSel < 0) rootSel = 0;
    int rootPitch = 12 * (4 + 1) + rootSel;

    int scaleSel = g_seqDlg.sel[1]; if (scaleSel < 0) scaleSel = 0;
    int chordSel = g_seqDlg.sel[2]; if (chordSel < 0) chordSel = 0;
    int arpSel   = g_seqDlg.sel[3]; if (arpSel < 0) arpSel = 0;

    int octLo = 0, octHi = 0;
    if (GetWindowTextA(g_seqDlg.octLoEdit, buf, sizeof(buf)) > 0) octLo = atoi(buf);
    if (GetWindowTextA(g_seqDlg.octHiEdit, buf, sizeof(buf)) > 0) octHi = atoi(buf);
    if (octHi < octLo) octHi = octLo;
    if (octHi - octLo > 4) octHi = octLo + 4;

    float noteLen = 0.25f;
    if (GetWindowTextA(g_seqDlg.lenEdit, buf, sizeof(buf)) > 0) {
        float v = (float)atof(buf);
        if (v > 0.0f) noteLen = v;
    }
    int velLo = 80, velHi = 120;
    if (GetWindowTextA(g_seqDlg.velLoEdit, buf, sizeof(buf)) > 0) velLo = atoi(buf);
    if (GetWindowTextA(g_seqDlg.velHiEdit, buf, sizeof(buf)) > 0) velHi = atoi(buf);
    if (velHi < velLo) velHi = velLo;
    if (velLo < 1) velLo = 1;
    if (velHi > 127) velHi = 127;

    int noteCount = 8;
    if (GetWindowTextA(g_seqDlg.countEdit, buf, sizeof(buf)) > 0) {
        int v = atoi(buf);
        if (v > 0 && v <= MIDI_MAX_NOTES) noteCount = v;
    }

    // Seed: blank = fresh random each generate; a number reproduces the same
    // pattern. After generating, the seed actually used is written back so
    // any generated pattern can be recalled verbatim by hitting GENERATE.
    char seedBuf[16] = "";
    GetWindowTextA(g_seqDlg.seedEdit, seedBuf, sizeof(seedBuf));
    uint32_t seed;
    if (seedBuf[0] && strspn(seedBuf, "0123456789") == strlen(seedBuf)) {
        seed = (uint32_t)_strtoui64(seedBuf, NULL, 10);
    } else {
        seed = (uint32_t)GetTickCount() ^ (((uint32_t)GetTickCount64()) << 13);
        if (!seed) seed = 1u;
    }
    // Keep the seed short: at most 7 digits, so the written-back value stays
    // compact. Clamping the seed (not just the display) means hitting GENERATE
    // again on the returned value still reproduces the same pattern.
    seed %= 10000000u;
    if (!seed) seed = 1u;
    // Small deterministic mixer so nearby seed numbers still give very
    // different sequences (SplitMix32 finalizer).
    uint32_t rng = seed;
    rng ^= rng >> 16; rng *= 0x7feb352du; rng ^= rng >> 15; rng *= 0x846ca68bu; rng ^= rng >> 16;
    if (!rng) rng = 1u;
    char seedOut[16];
    snprintf(seedOut, sizeof(seedOut), "%u", (unsigned)seed);

    // Deterministic PRNG so a given seed always yields the same pattern.
#define SEQ_RAND() (rng = rng * 1664525u + 1013904223u, (int)(rng >> 16))

    seq_lock();
    Clip* seqClip = midi_edit_clip();
    if (seqClip && midi_edit_is_quadrum()) {
        // --- Quadrum drum generator ---
        // No octaves, no scale/chord: just 8 fixed drum voices (pitch 0-7)
        // laid out over the clip's bars. The generator places notes across
        // the available beats; velocity accents the downbeat.
        seqClip->midiNoteCount = 0;
        int placed = 0;
        int beatsPerBarLocal = (int)(beats_per_bar() + 0.5f);
        if (beatsPerBarLocal < 1) beatsPerBarLocal = 4;
        for (int n = 0; n < noteCount; ++n) {
            float startBeat = (float)placed * noteLen;
            if (startBeat >= seqClip->lengthBeats) break;
            int stepInBar = (n % beatsPerBarLocal);
            // Pick a voice 0-7, weighted toward the downbeat (kick/snare).
            int voice;
            if (stepInBar == 0) {
                voice = (SEQ_RAND() % 100 < 60) ? 0 : (SEQ_RAND() % 8);   // kick-heavy
            } else if (stepInBar == (beatsPerBarLocal / 2) && (beatsPerBarLocal & 1) == 0) {
                voice = (SEQ_RAND() % 100 < 55) ? 1 : (SEQ_RAND() % 8);   // snare-heavy
            } else {
                voice = SEQ_RAND() % 8;
            }
            if (voice < 0) voice = 0;
            if (voice > 7) voice = 7;
            // Musical velocity: accent the downbeat, soft on off-beats.
            float vel;
            if (stepInBar == 0) vel = (float)velHi;
            else if ((stepInBar & 1) == 0) vel = (float)(velLo + (velHi - velLo) / 2);
            else vel = (float)velLo;
            midi_edit_add_note(seqClip, voice, startBeat, noteLen, vel);
            placed++;
        }
        if (seqClip->midiNoteCount > 0) {
            for (int i = 0; i < seqClip->midiNoteCount; ++i) seqClip->midiNotes[i].isSelected = true;
        }
        mark_clip_bars_dirty(seqClip);
        g_timelineDirty = true;
        g_Seq.isModified = true;
        SetWindowTextA(g_seqDlg.seedEdit, seedOut);
        seq_unlock();
        invalidate_midi_cache();
        if (g_midiHwnd && IsWindow(g_midiHwnd)) InvalidateRect(g_midiHwnd, NULL, FALSE);
        if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
        ShowWindow(hwnd, SW_HIDE);
        return;
    }
    seq_unlock();

    // Scale-aware chord pool: build the seven diatonic triads of the chosen
    // scale so the progression never leaves the key.
    static const int kScaleMajor[] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int kScaleMinor[] = { 0, 2, 3, 5, 7, 8, 10 };
    static const int kChordMajor[] = { 0, 4, 7 };
    static const int kChordMinor[] = { 0, 3, 7 };
    static const int kChordDim[]   = { 0, 3, 6 };
    static const int kChordAug[]   = { 0, 4, 8 };
    static const int kChordSeventh[] = { 0, 4, 7, 10 };
    const int* scale = (scaleSel == 1) ? kScaleMinor : kScaleMajor;
    const int* chord = NULL;
    int chordLen = 3;
    switch (chordSel) {
        case 0: chord = kChordMajor; chordLen = 3; break;
        case 1: chord = kChordMinor; chordLen = 3; break;
        case 2: chord = kChordDim;   chordLen = 3; break;
        case 3: chord = kChordAug;   chordLen = 3; break;
        default: chord = kChordSeventh; chordLen = 4; break;
    }

    seq_lock();
    Clip* c = midi_edit_clip();
    if (c) {
        // Generating replaces the clip's notes entirely.
        c->midiNoteCount = 0;
        int placed = 0;

        // One diatonic chord per bar: I - vi - IV - V for major scales
        // (i - iv - VI - v relative for minor), which keeps every note
        // in-key while still giving the pattern harmonic motion.
        static const int kProgMajor[4] = { 0, 5, 3, 4 };
        static const int kProgMinor[4] = { 0, 3, 5, 4 };
        const int* prog = (scaleSel == 1) ? kProgMinor : kProgMajor;
        int beatsPerBarLocal = (int)(beats_per_bar() + 0.5f);
        if (beatsPerBarLocal < 1) beatsPerBarLocal = 4;

        // Chord-tone pool: every chord tone in [octLo, octHi], ascending,
        // relative to rootPitch. All patterns operate over this pool.
        int pool[64];
        int poolCount = 0;
        for (int oct = octLo; oct <= octHi && oct <= octLo + 4; ++oct) {
            for (int i = 0; i < chordLen; ++i) {
                int semi = chord[i];
                if (scaleSel == 1 && (semi == 4)) semi = 3;
                int p = semi + oct * 12;
                if (p >= 0 && p <= 127 - rootPitch && poolCount < 64) pool[poolCount++] = p;
            }
        }
        if (poolCount == 0) { pool[poolCount++] = 0; }

        for (int n = 0; n < noteCount; ++n) {
            float startBeat = (float)placed * noteLen;
            if (startBeat >= c->lengthBeats) break;

            int stepInBar = (n % beatsPerBarLocal);
            int barIdx = (n / beatsPerBarLocal);
            int degree = prog[barIdx % 4];

            int pitch;
            if (arpSel == 4) {
                // Order: the current bar's chord tones, straight up, root
                // positioned by the progression degree so it follows harmony.
                int chordRoot = scale[degree % 7];
                int rel = chord[n % chordLen] - chord[0];
                pitch = rootPitch + chordRoot + rel + octLo * 12;
            } else if (arpSel == 0 || arpSel == 1) {
                // Up / Down over the full chord-tone pool.
                int idx = (arpSel == 0) ? (n % poolCount)
                                        : (poolCount - 1 - (n % poolCount));
                pitch = rootPitch + pool[idx];
            } else if (arpSel == 2) {
                // Up-down over the pool.
                int cycle = (poolCount > 1) ? poolCount * 2 - 1 : poolCount;
                int pos = n % cycle;
                int idx = (pos < poolCount) ? pos : (poolCount - 2 - (pos - poolCount));
                if (idx < 0) idx = 0;
                pitch = rootPitch + pool[idx];
            } else {
                // Random walk over the pool: mostly neighbor steps, occasional
                // skips and jumps — melodic rather than white-noise random.
                // Walk state is per-generate (seeded), so the same seed and
                // settings always reproduce the same pattern.
                int walkIdx = (SEQ_RAND() % 1024 * poolCount) >> 10;
                if (walkIdx >= poolCount) walkIdx = poolCount - 1;
                int r = SEQ_RAND() % 100;
                if (r < 55) walkIdx += (SEQ_RAND() % 2) ? 1 : -1;          // step
                else if (r < 75) walkIdx += (SEQ_RAND() % 2) ? 2 : -2;     // skip
                else walkIdx = SEQ_RAND() % poolCount;                     // jump
                if (walkIdx < 0) walkIdx = 1;
                if (walkIdx >= poolCount) walkIdx = poolCount - 2;
                if (walkIdx < 0) walkIdx = 0;
                pitch = rootPitch + pool[walkIdx];
            }

            // Musical velocity: accent the downbeat, medium on the middle
            // beat, soft on off-beat subdivisions, within the Lo..Hi range.
            float vel;
            if (stepInBar == 0)        vel = (float)velHi;
            else if (stepInBar == (beatsPerBarLocal / 2) && (beatsPerBarLocal & 1) == 0)
                                       vel = (float)(velLo + (velHi - velLo) * 3 / 4);
            else if ((stepInBar & 1) == 0)
                                       vel = (float)(velLo + (velHi - velLo) / 2);
            else                       vel = (float)velLo;

            midi_edit_add_note(c, pitch, startBeat, noteLen, vel);
            placed++;
        }
        if (c->midiNoteCount > 0) {
            for (int i = 0; i < c->midiNoteCount; ++i) c->midiNotes[i].isSelected = true;
        }
        mark_clip_bars_dirty(c);
        g_timelineDirty = true;
        g_Seq.isModified = true;
        // Record the seed used so the pattern can be regenerated exactly.
        SetWindowTextA(g_seqDlg.seedEdit, seedOut);
    }
    seq_unlock();

#undef SEQ_RAND

    invalidate_midi_cache();
    if (g_midiHwnd && IsWindow(g_midiHwnd)) InvalidateRect(g_midiHwnd, NULL, FALSE);
    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
    ShowWindow(hwnd, SW_HIDE);
}

// Box-filter downsample with true premultiplied alpha: averages each SSxSS
// block into one destination pixel and composites via AlphaBlend so icons
// seamlessly match any underlying UI background. DIB pixels are BGRA bytes
// (byte 0 = blue, byte 2 = red); non-black coverage becomes alpha so the
// black render canvas never shows as a box.
static inline void seq_blit_supersampled(HDC dc, int dx, int dy, int dw, int dh,
                                         HBITMAP bmp, int sw, int sh, int ss) {
    (void)ss;
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;

    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = sw;
    bi.bmiHeader.biHeight = -sh;              // top-down row order
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    unsigned int* px = (unsigned int*)malloc((size_t)sw * sh * 4);
    if (!px) return;

    HDC src = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(src, bmp);
    GetDIBits(src, bmp, 0, sh, px, &bi, DIB_RGB_COLORS);
    SelectObject(src, old);
    DeleteDC(src);

    // 32-bit premultiplied DIB section for AlphaBlend
    BITMAPINFO outBi = { 0 };
    outBi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    outBi.bmiHeader.biWidth = dw;
    outBi.bmiHeader.biHeight = -dh;
    outBi.bmiHeader.biPlanes = 1;
    outBi.bmiHeader.biBitCount = 32;
    outBi.bmiHeader.biCompression = BI_RGB;

    void* outBits = NULL;
    HDC outDC = CreateCompatibleDC(dc);
    HBITMAP outBmp = CreateDIBSection(outDC, &outBi, DIB_RGB_COLORS, &outBits, NULL, 0);
    if (!outBmp || !outBits) {
        if (outDC) DeleteDC(outDC);
        free(px);
        return;
    }
    HGDIOBJ oldOut = SelectObject(outDC, outBmp);
    DWORD* outPx = (DWORD*)outBits;

    for (int y = 0; y < dh; ++y) {
        int sy0 = y * sh / dh, sy1 = (y + 1) * sh / dh;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < dw; ++x) {
            int sx0 = x * sw / dw, sx1 = (x + 1) * sw / dw;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            unsigned int rSum = 0, gSum = 0, bSum = 0, covered = 0;
            unsigned int totalSamples = (unsigned int)((sy1 - sy0) * (sx1 - sx0));

            for (int yy = sy0; yy < sy1; ++yy) {
                const unsigned int* row = px + (size_t)yy * sw;
                for (int xx = sx0; xx < sx1; ++xx) {
                    unsigned int c = row[xx];
                    if (c & 0x00FFFFFF) { // non-black pixel
                        // 32-bit DIB (BI_RGB): byte 0 = blue, byte 2 = red.
                        bSum += (c >> 0) & 0xFF;
                        gSum += (c >> 8) & 0xFF;
                        rSum += (c >> 16) & 0xFF;
                        covered++;
                    }
                }
            }

            if (covered == 0 || totalSamples == 0) {
                outPx[y * dw + x] = 0; // 100% transparent
            } else {
                float cov = (float)covered / (float)totalSamples;
                if (cov > 1.0f) cov = 1.0f;
                float rAvg = (float)rSum / (float)covered;
                float gAvg = (float)gSum / (float)covered;
                float bAvg = (float)bSum / (float)covered;
                BYTE a  = (BYTE)(cov * 255.0f + 0.5f);
                BYTE pr = (BYTE)(rAvg * cov + 0.5f);
                BYTE pg = (BYTE)(gAvg * cov + 0.5f);
                BYTE pb = (BYTE)(bAvg * cov + 0.5f);
                outPx[y * dw + x] = ((DWORD)a << 24) | ((DWORD)pr << 16) | ((DWORD)pg << 8) | pb;
            }
        }
    }

    GdiFlush();
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(dc, dx, dy, dw, dh, outDC, 0, 0, dw, dh, bf);

    SelectObject(outDC, oldOut);
    DeleteObject(outBmp);
    DeleteDC(outDC);
    free(px);
}

// Supersampled arrow chevron: renders a thick "v" at 4x resolution and
// box-downsamples so the diagonal strokes come out antialiased instead of
// jagged. cx/cy is the chevron center; hw/hh its half-extents.
static inline void seq_draw_arrow_icon(HDC dc, int cx, int cy, int hw, int hh, COLORREF col) {
    const int SS = 4;
    int w = hw * 2 + scale_x(4), h = hh * 2 + scale_y(4);
    if (w <= 0 || h <= 0) return;
    int sw = w * SS, sh = h * SS;

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, sw, sh);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
    HGDIOBJ ob = SelectObject(mem, bg);
    PatBlt(mem, 0, 0, sw, sh, BLACKNESS);
    SelectObject(mem, ob);
    DeleteObject(bg);

    HPEN pn = CreatePen(PS_SOLID, SS * 2, col);
    HGDIOBJ op = SelectObject(mem, pn);
    MoveToEx(mem, sw / 4, sh / 3, NULL);
    LineTo(mem, sw / 2, (2 * sh) / 3);
    LineTo(mem, (3 * sw) / 4, sh / 3);
    SelectObject(mem, op);
    DeleteObject(pn);

    seq_blit_supersampled(dc, cx - w / 2, cy - h / 2, w, h, bmp, sw, sh, SS);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

// Supersampled dice icon: renders at 4x resolution and box-downsamples so
// the pips and rounded body stay smooth at small sizes. Draws the "five"
// face; body tinted by `hot` for hover feedback.
static inline void seq_draw_dice_icon(HDC dc, int left, int top, int right, int bottom, bool hot) {
    const int SS = 4;
    int w = right - left, h = bottom - top;
    if (w <= 0 || h <= 0) return;
    int sw = w * SS, sh = h * SS;

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, sw, sh);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
    HGDIOBJ ob = SelectObject(mem, bg);
    PatBlt(mem, 0, 0, sw, sh, BLACKNESS);
    SelectObject(mem, ob);
    DeleteObject(bg);

    COLORREF body = hot ? RGB(38, 56, 78) : RGB(28, 36, 48);
    COLORREF edge = hot ? RGB(120, 200, 240) : RGB(70, 84, 104);
    COLORREF pip  = hot ? RGB(170, 225, 250) : RGB(170, 185, 205);

    // Rounded body at supersampled scale.
    HPEN pn = CreatePen(PS_SOLID, SS, edge);
    HBRUSH br = CreateSolidBrush(body);
    HGDIOBJ op = SelectObject(mem, pn);
    HGDIOBJ obn = SelectObject(mem, br);
    RoundRect(mem, SS / 2, SS / 2, sw - SS / 2, sh - SS / 2, sw / 3, sh / 3);
    SelectObject(mem, op);
    SelectObject(mem, obn);
    DeleteObject(pn);
    DeleteObject(br);

    // Five-face pips at normalized positions. NULL_PEN so no dark outline
    // ring is baked around each pip by the default pen.
    HBRUSH pipBr = CreateSolidBrush(pip);
    obn = SelectObject(mem, pipBr);
    HGDIOBJ oldPen = SelectObject(mem, GetStockObject(NULL_PEN));
    const float pts[5][2] = { { 0.26f, 0.26f }, { 0.74f, 0.26f }, { 0.5f, 0.5f },
                              { 0.26f, 0.74f }, { 0.74f, 0.74f } };
    int pr = sw / 9;
    for (int i = 0; i < 5; ++i) {
        int cx = (int)(pts[i][0] * sw);
        int cy = (int)(pts[i][1] * sh);
        Ellipse(mem, cx - pr, cy - pr, cx + pr, cy + pr);
    }
    SelectObject(mem, oldPen);
    SelectObject(mem, obn);
    DeleteObject(pipBr);

    // Box-downsample back into the dialog's memory DC.
    seq_blit_supersampled(dc, left, top, w, h, bmp, sw, sh, SS);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

// Forward decl: the popup list needs to resolve its owner dialog.
static HWND seq_combo_popup_parent(HWND listHwnd);

// Close the dropped list, optionally committing the hovered row first.
static void seq_combo_close(HWND listHwnd, bool commit) {
    HWND owner = seq_combo_popup_parent(listHwnd);
    int idx = g_seqDlg.openIdx;
    if (commit && idx >= 0 && idx < 4 &&
        g_seqDlg.hoverItem >= 0 && g_seqDlg.hoverItem < kSeqDropCount[idx]) {
        g_seqDlg.sel[idx] = g_seqDlg.hoverItem;
    }
    if (listHwnd && IsWindow(listHwnd)) ReleaseCapture();
    g_seqDlg.openIdx = -1;
    g_seqDlg.hoverItem = -1;
    g_seqDlg.listHwnd = NULL;
    if (owner) {
        InvalidateRect(owner, NULL, FALSE);
        SetForegroundWindow(owner);
    }
    if (listHwnd && IsWindow(listHwnd)) DestroyWindow(listHwnd);
}

// Paint the dropped list through a plain BeginPaint cycle. The caller must
// have a real update region (set via InvalidateRect) — no manual GetDC or
// ValidateRect anywhere, both of which suppressed the one paint that made
// pixels visible.
static void seq_paint_list(HWND h, HDC hdc) {
    RECT rc; GetClientRect(h, &rc);
    int w = rc.right - rc.left, hCl = rc.bottom - rc.top;
    if (w <= 0 || hCl <= 0) return;

    // Double-buffer: the list repaints on every hover change, so paint into a
    // memory DC and blit once to avoid flicker.
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, hCl);
    HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
    HFONT oldFont = SELECT_UI_FONT(memDC);

    HBRUSH bg = CreateSolidBrush(RGB(24, 28, 38));
    FillRect(memDC, &rc, bg);
    DeleteObject(bg);
    SetBkMode(memDC, TRANSPARENT);

    int idx = g_seqDlg.openIdx;
    if (idx >= 0 && idx < 4 && kSeqDropItems[idx]) {
        int rowH = scale_y(SEQ_ROW_STEP - 6);
        for (int i = 0; i < kSeqDropCount[idx]; ++i) {
            RECT rowRc = { 0, i * rowH, w, (i + 1) * rowH };
            bool hov = (i == g_seqDlg.hoverItem);
            bool cur = (i == g_seqDlg.sel[idx]);
            if (hov || cur) {
                HBRUSH hb = CreateSolidBrush(hov ? RGB(45, 60, 82) : RGB(33, 42, 56));
                FillRect(memDC, &rowRc, hb);
                DeleteObject(hb);
            }
            SetTextColor(memDC, cur ? RGB(160, 220, 250)
                        : hov     ? RGB(225, 235, 245)
                                  : RGB(190, 200, 215));
            RECT tr = rowRc; tr.left += scale_x(8);
            DrawTextA(memDC, kSeqDropItems[idx][i], -1, &tr,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    // Dark border instead of the win32 white edge.
    HPEN pn = CreatePen(PS_SOLID, 1, RGB(60, 72, 92));
    HGDIOBJ op = SelectObject(memDC, pn);
    HGDIOBJ ob2 = SelectObject(memDC, GetStockObject(NULL_BRUSH));
    Rectangle(memDC, 0, 0, w, hCl);
    SelectObject(memDC, ob2);
    SelectObject(memDC, op);
    DeleteObject(pn);
    SelectObject(memDC, oldFont);

    BitBlt(hdc, 0, 0, w, hCl, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}

// Deferred repaint: posted after ShowWindow so the paint runs on a later
// message-loop pass, once the popup is actually composited and visible.
// Painting synchronously right after ShowWindow raced the compositor, and
// the trailing ValidateRect suppressed the WM_PAINT that would have fixed it.
#define SEQ_LIST_REPAINT (WM_APP + 2)

static LRESULT CALLBACK SeqListProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(h, &ps);
            seq_paint_list(h, hdc);
            EndPaint(h, &ps);
            return 0;
        }
        case SEQ_LIST_REPAINT:
            // Window is on screen by now: mark the whole thing dirty and let
            // the normal WM_PAINT cycle draw it. No ValidateRect here.
            InvalidateRect(h, NULL, TRUE);
            UpdateWindow(h);
            return 0;
        case WM_CAPTURECHANGED:
            // Something else took the capture: dismiss rather than linger.
            if (g_seqDlg.listHwnd == h) seq_combo_close(h, false);
            return 0;
        case WM_ERASEBKGND:
            return 1;   // the class background brush + full paints cover it
        case WM_MOUSEMOVE: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            RECT rc; GetClientRect(h, &rc);
            int item = -1;
            int idx = g_seqDlg.openIdx;
            if (PtInRect(&rc, (POINT){ mx, my })) {
                int rowH = scale_y(SEQ_ROW_STEP - 6);
                if (rowH > 0) item = my / rowH;
                if (idx < 0 || idx >= 4 || item < 0 || item >= kSeqDropCount[idx]) item = -1;
            }
            if (item != g_seqDlg.hoverItem) {
                g_seqDlg.hoverItem = item;
                InvalidateRect(h, NULL, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            RECT rc; GetClientRect(h, &rc);
            if (PtInRect(&rc, (POINT){ mx, my })) {
                int idx = g_seqDlg.openIdx;
                int rowH = scale_y(SEQ_ROW_STEP - 6);
                int item = (rowH > 0) ? (my / rowH) : -1;
                if (idx >= 0 && idx < 4 && item >= 0 && item < kSeqDropCount[idx]) {
                    g_seqDlg.hoverItem = item;
                    seq_combo_close(h, true);
                    return 0;
                }
            }
            seq_combo_close(h, false);
            return 0;
        }
        case WM_RBUTTONDOWN:
            seq_combo_close(h, false);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) { seq_combo_close(h, false); return 0; }
            if (wParam == VK_RETURN) { seq_combo_close(h, true); return 0; }
            if (wParam == VK_UP || wParam == VK_DOWN) {
                int idx = g_seqDlg.openIdx;
                int n = (idx >= 0 && idx < 4) ? kSeqDropCount[idx] : 0;
                if (n > 0) {
                    int it = g_seqDlg.hoverItem;
                    if (it < 0) it = g_seqDlg.sel[idx];
                    it += (wParam == VK_DOWN) ? 1 : -1;
                    if (it < 0) it = n - 1;
                    if (it >= n) it = 0;
                    g_seqDlg.hoverItem = it;
                    InvalidateRect(h, NULL, FALSE);
                }
                return 0;
            }
            break;
        case WM_DESTROY:
            if (g_seqDlg.listHwnd == h) {
                g_seqDlg.listHwnd = NULL;
                g_seqDlg.openIdx = -1;
                g_seqDlg.hoverItem = -1;
            }
            return 0;
    }
    return DefWindowProcA(h, msg, wParam, lParam);
}

// The popup list is owned by the dialog; recover the dialog from the owner.
static HWND seq_combo_popup_parent(HWND listHwnd) {
    return listHwnd ? GetWindow(listHwnd, GW_OWNER) : NULL;
}

// Open (or toggle closed) the hand-painted dropdown list for a field.
static void seq_combo_toggle(HWND dlg, int idx, const RECT* fieldRc) {
    if (g_seqDlg.openIdx == idx) {
        if (g_seqDlg.listHwnd && IsWindow(g_seqDlg.listHwnd))
            seq_combo_close(g_seqDlg.listHwnd, false);
        return;
    }
    if (g_seqDlg.openIdx >= 0 && g_seqDlg.listHwnd && IsWindow(g_seqDlg.listHwnd))
        seq_combo_close(g_seqDlg.listHwnd, false);

    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSA wc = { 0 };
        wc.lpfnWndProc = SeqListProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = "RefractSeqListClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        // Dark class brush: even a missed paint can never flash white.
        wc.hbrBackground = CreateSolidBrush(RGB(24, 28, 38));
        RegisterClassA(&wc);
        s_registered = true;
    }

    int rowH = scale_y(SEQ_ROW_STEP - 6);
    int lw = fieldRc->right - fieldRc->left;
    int lh = kSeqDropCount[idx] * rowH + 2;
    POINT pt = { fieldRc->left, fieldRc->bottom };
    ClientToScreen(dlg, &pt);

    g_seqDlg.openIdx = idx;
    g_seqDlg.hoverItem = g_seqDlg.sel[idx];

    HWND list = CreateWindowExA(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        "RefractSeqListClass", "",
        WS_POPUP,
        pt.x, pt.y, lw, lh,
        dlg, NULL, GetModuleHandle(NULL), NULL);
    g_seqDlg.listHwnd = list;

    ShowWindow(list, SW_SHOW);
    UpdateWindow(list);
    // Deferred repaint: fires on a later message-loop pass, after the popup
    // is composited. Painting synchronously here raced the compositor, and
    // the old trailing ValidateRect killed the real WM_PAINT (blank popup).
    PostMessageA(list, SEQ_LIST_REPAINT, 0, 0);
    SetCapture(list);              // hover/click-away via capture, no focus dance
    SetForegroundWindow(list);     // best-effort, for keyboard navigation
    SetFocus(list);
    InvalidateRect(dlg, NULL, FALSE);
}

// ---- SoundFont instrument selector -------------------------------------
// A horizontally-scrolling table of the loaded font's presets, styled like
// the app's piano keys: one "key" per instrument, grows with entry count.

static HWND g_sfontInstHwnd = NULL;   // selector popup
static int  g_sfontInstScroll = 0;    // first visible preset slot
static int  g_sfontInstHover = -1;

static inline int sfont_inst_key_width(void)  { return scale_x(120); }
static inline int sfont_inst_key_height(void) { return scale_y(56); }

static LRESULT CALLBACK SfontInstProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc; GetClientRect(h, &rc);
            int w = rc.right - rc.left, hgt = rc.bottom - rc.top;

            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, w, hgt);
            HGDIOBJ ob = SelectObject(mem, bmp);
            HFONT of = SELECT_UI_FONT(mem);
            SetBkMode(mem, TRANSPARENT);

            HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
            FillRect(mem, &rc, bg);
            DeleteObject(bg);

            int count = sfont_preset_count();
            int rowH = scale_y(22);
            int active = sfont_active_preset_slot();

            // Draw each visible row
            for (int i = g_sfontInstScroll; i < count; ++i) {
                int y = (i - g_sfontInstScroll) * rowH;
                if (y >= hgt) break;

                RECT rowRc = { 0, y, w, y + rowH };
                bool isAct = (i == active);
                bool isHov = (i == g_sfontInstHover);

                COLORREF bgCol = isAct ? RGB(60, 100, 80)
                               : isHov ? RGB(45, 52, 64)
                                       : RGB(22, 26, 34);
                COLORREF txtCol = isAct ? RGB(190, 255, 220)
                                : isHov ? RGB(225, 235, 245)
                                        : RGB(180, 195, 215);

                HBRUSH rowBr = CreateSolidBrush(bgCol);
                FillRect(mem, &rowRc, rowBr);
                DeleteObject(rowBr);

                SetTextColor(mem, txtCol);
                RECT tr = rowRc;
                tr.left += scale_x(8);
                tr.right -= scale_x(4);
                DrawTextA(mem, sfont_preset_name(i), -1, &tr,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }

            // Simple vertical scrollbar (if needed)
            SCROLLINFO si = { sizeof(si), SIF_ALL };
            si.nMin = 0;
            si.nMax = count - 1;
            si.nPage = hgt / rowH;
            si.nPos = g_sfontInstScroll;
            SetScrollInfo(h, SB_VERT, &si, TRUE);

            BitBlt(dc, 0, 0, w, hgt, mem, 0, 0, SRCCOPY);
            SelectObject(mem, of);
            SelectObject(mem, ob);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(h, &ps);
            return 0;
        }

        case WM_MOUSEMOVE: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT rc; GetClientRect(h, &rc);
            int rowH = scale_y(22);
            int item = -1;
            if (PtInRect(&rc, pt)) {
                int idx = g_sfontInstScroll + pt.y / rowH;
                if (idx < sfont_preset_count()) item = idx;
            }
            if (item != g_sfontInstHover) {
                g_sfontInstHover = item;
                InvalidateRect(h, NULL, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT rc; GetClientRect(h, &rc);
            if (PtInRect(&rc, pt)) {
                int rowH = scale_y(22);
                int idx = g_sfontInstScroll + pt.y / rowH;
                if (idx >= 0 && idx < sfont_preset_count()) {
                    sfont_set_active_preset_slot(idx);
                    // Play a quick preview note
                    int pi = sfont_preset_index(idx);
                    sfont_note_off_all();
                    sfont_note_on(pi, 60, 0.8f);
                    DestroyWindow(h); // close on selection (like a picker)
                }
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            int lines = delta / WHEEL_DELTA;
            g_sfontInstScroll -= lines * 2; // speed
            if (g_sfontInstScroll < 0) g_sfontInstScroll = 0;
            // FIXED: use a proper RECT variable instead of compound literal
            RECT rc;
            GetClientRect(h, &rc);
            int maxScroll = sfont_preset_count() - (rc.bottom / scale_y(22));
            if (maxScroll < 0) maxScroll = 0;
            if (g_sfontInstScroll > maxScroll) g_sfontInstScroll = maxScroll;
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }

        case WM_VSCROLL: {
            int newPos = g_sfontInstScroll;
            SCROLLINFO si = { sizeof(si), SIF_ALL };
            GetScrollInfo(h, SB_VERT, &si);
            switch (LOWORD(wParam)) {
                case SB_LINEUP:   newPos -= 1; break;
                case SB_LINEDOWN: newPos += 1; break;
                case SB_PAGEUP:   newPos -= (int)si.nPage; break;
                case SB_PAGEDOWN: newPos += (int)si.nPage; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: newPos = si.nTrackPos; break;
                default: return 0;
            }
            if (newPos < 0) newPos = 0;
            int maxScroll = sfont_preset_count() - (int)si.nPage;
            if (maxScroll < 0) maxScroll = 0;
            if (newPos > maxScroll) newPos = maxScroll;
            if (newPos != g_sfontInstScroll) {
                g_sfontInstScroll = newPos;
                InvalidateRect(h, NULL, FALSE);
            }
            return 0;
        }

        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) { DestroyWindow(h); return 0; }
            if (wParam == VK_UP || wParam == VK_DOWN) {
                int step = (wParam == VK_DOWN) ? 1 : -1;
                int newIdx = g_sfontInstScroll + step;
                if (newIdx < 0) newIdx = 0;
                if (newIdx >= sfont_preset_count()) newIdx = sfont_preset_count() - 1;
                g_sfontInstScroll = newIdx;
                // Also hover the current top item
                g_sfontInstHover = newIdx;
                InvalidateRect(h, NULL, FALSE);
                return 0;
            }
            if (wParam == VK_RETURN && g_sfontInstHover >= 0) {
                sfont_set_active_preset_slot(g_sfontInstHover);
                int pi = sfont_preset_index(g_sfontInstHover);
                sfont_note_off_all();
                sfont_note_on(pi, 60, 0.8f);
                DestroyWindow(h);
                return 0;
            }
            break;
        }

        case WM_KILLFOCUS:
            DestroyWindow(h);
            return 0;

        case WM_DESTROY:
            g_sfontInstHwnd = NULL;
            g_sfontInstHover = -1;
            return 0;
    }
    return DefWindowProcA(h, msg, wParam, lParam);
}

static inline void sfont_open_inst_selector(HWND parent) {
    if (!sfont_is_loaded()) return;
    if (g_sfontInstHwnd && IsWindow(g_sfontInstHwnd)) {
        DestroyWindow(g_sfontInstHwnd);
        return;
    }

    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSA wc = { 0 };
        wc.lpfnWndProc = SfontInstProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = "RefractSfontInstClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(17, 20, 26));
        RegisterClassA(&wc);
        s_registered = true;
    }

    int count = sfont_preset_count();
    // Window width based on longest name, capped at 280px, minimum 180px
    HDC dc = GetDC(parent);
    HFONT oldFont = SELECT_UI_FONT(dc);
    int maxW = 0;
    for (int i = 0; i < count; ++i) {
        SIZE sz;
        GetTextExtentPoint32A(dc, sfont_preset_name(i), (int)strlen(sfont_preset_name(i)), &sz);
        if (sz.cx > maxW) maxW = sz.cx;
    }
    SelectObject(dc, oldFont);
    ReleaseDC(parent, dc);
    int w = maxW + scale_x(32);
    if (w < scale_x(160)) w = scale_x(160);
    if (w > scale_x(280)) w = scale_x(280);

    // Height: up to 400px, but at least 4 rows
    int rowH = scale_y(22);
    int rows = count;
    int maxRows = 400 / rowH;
    if (rows > maxRows) rows = maxRows;
    if (rows < 4) rows = 4;
    int h = rows * rowH + 2; // extra for border

    // Position near the note icon (top-left of parent)
    POINT pt = { scale_x(476), scale_y(30) };
    ClientToScreen(parent, &pt);

    // Keep on screen
    RECT wa; SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    if (pt.x + w > wa.right) pt.x = wa.right - w;
    if (pt.y + h > wa.bottom) pt.y = wa.bottom - h;
    if (pt.x < wa.left) pt.x = wa.left;
    if (pt.y < wa.top) pt.y = wa.top;

    g_sfontInstScroll = 0;
    g_sfontInstHover = -1;
    g_sfontInstHwnd = CreateWindowExA(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        "RefractSfontInstClass",
        "Instruments",
        WS_POPUP | WS_VISIBLE | WS_VSCROLL,
        pt.x, pt.y, w, h,
        parent, NULL, GetModuleHandle(NULL), NULL
    );

    SetForegroundWindow(g_sfontInstHwnd);
    SetFocus(g_sfontInstHwnd);
}

static LRESULT CALLBACK SeqEditSubProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) { SendMessageA(GetParent(h), WM_APP + 1, 1, 0); return 0; }
        if (wParam == VK_ESCAPE) { SendMessageA(GetParent(h), WM_APP + 1, 0, 0); return 0; }
    }
    return CallWindowProcA((WNDPROC)GetWindowLongPtrA(h, GWLP_USERDATA), h, msg, wParam, lParam);
}

static LRESULT CALLBACK SeqWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HBRUSH s_hbrEditBg = NULL;
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC hdcC = (HDC)wParam;
            SetTextColor(hdcC, RGB(225, 235, 245));
            SetBkColor(hdcC, RGB(24, 28, 38));
            if (!s_hbrEditBg) s_hbrEditBg = CreateSolidBrush(RGB(24, 28, 38));
            return (LRESULT)s_hbrEditBg;
        }
        case WM_APP + 1:
            if (wParam) seq_dialog_apply(hwnd);
            else ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HFONT oldFontMain = SELECT_UI_FONT(hdc);
            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left, h = rc.bottom - rc.top;
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
            HFONT oldFontMem = SELECT_UI_FONT(memDC);
            HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
            FillRect(memDC, &rc, bg);
            DeleteObject(bg);
            SetBkMode(memDC, TRANSPARENT);

            static const char* comboLabels[] = { "Root", "Scale", "Chord", "Pattern" };
            static const char* editLabelsTxt[] = { "Octave Low", "Octave High", "Note Length", "Velocity Low", "Velocity High", "Note Count", "Seed" };
            RECT comboLblRc[4], comboRc[4], editLblRc[7], editRc[7], okRc, noRc;
            seq_dialog_layout(w, h, comboLblRc, comboRc, editLblRc, editRc, &okRc, &noRc);
            SetTextColor(memDC, RGB(140, 155, 175));
            for (int i = 0; i < 4; ++i)
                DrawTextA(memDC, comboLabels[i], -1, &comboLblRc[i], DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            for (int i = 0; i < 7; ++i)
                DrawTextA(memDC, editLabelsTxt[i], -1, &editLblRc[i], DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Hand-painted dropdown fields: dark body, current item text and
            // a small arrow chevron. Fully ours — no win32 combo chrome.
            {
                POINT cpt; GetCursorPos(&cpt); ScreenToClient(hwnd, &cpt);
                HPEN pn = CreatePen(PS_SOLID, 1, RGB(60, 72, 92));
                HGDIOBJ op2 = SelectObject(memDC, pn);
                HGDIOBJ ob2 = SelectObject(memDC, GetStockObject(NULL_BRUSH));
                for (int i = 0; i < 4; ++i) {
                    RECT fc = comboRc[i];
                    bool open = (g_seqDlg.openIdx == i);
                    bool hot = !open && PtInRect(&fc, cpt);
                    HBRUSH body = CreateSolidBrush(open  ? RGB(33, 42, 56)
                                                 : hot   ? RGB(30, 38, 50)
                                                         : RGB(24, 28, 38));
                    HGDIOBJ pb = SelectObject(memDC, body);
                    Rectangle(memDC, fc.left, fc.top, fc.right, fc.bottom);
                    SelectObject(memDC, pb);
                    DeleteObject(body);
                    // Arrow chevron centered in a right-side square.
                    int ax = fc.right - scale_x(14);
                    int ay = (fc.top + fc.bottom) / 2;
                    COLORREF arrCol = open ? RGB(160, 220, 250) : RGB(150, 165, 185);
                    seq_draw_arrow_icon(memDC, ax, ay, scale_x(4), scale_y(3), arrCol);
                    // Selected item text.
                    int sel = g_seqDlg.sel[i];
                    if (sel >= 0 && sel < kSeqDropCount[i] && kSeqDropItems[i]) {
                        SetTextColor(memDC, RGB(225, 235, 245));
                        RECT tr = fc;
                        tr.left += scale_x(8);
                        tr.right = ax - scale_x(6);
                        DrawTextA(memDC, kSeqDropItems[i][sel], -1, &tr,
                                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                    }
                }
                SelectObject(memDC, ob2);
                SelectObject(memDC, op2);
                DeleteObject(pn);
            }

            // Dice button beside the Seed row: randomizes the seed field.
            {
                POINT cpt;
                GetCursorPos(&cpt);
                ScreenToClient(hwnd, &cpt);
                bool hot = PtInRect(&g_seqDiceRc, cpt);
                seq_draw_dice_icon(memDC, g_seqDiceRc.left, g_seqDiceRc.top, g_seqDiceRc.right, g_seqDiceRc.bottom, hot);
            }

            HBRUSH okBg = CreateSolidBrush(RGB(22, 90, 55));
            HPEN okPn = CreatePen(PS_SOLID, 1, RGB(80, 240, 180));
            HGDIOBJ ob = SelectObject(memDC, okBg);
            HGDIOBJ op = SelectObject(memDC, okPn);
            RoundRect(memDC, okRc.left, okRc.top, okRc.right, okRc.bottom, 4, 4);
            SelectObject(memDC, op); SelectObject(memDC, ob);
            DeleteObject(okPn); DeleteObject(okBg);
            SetTextColor(memDC, RGB(160, 255, 205));
            DrawTextA(memDC, "GENERATE (ENTER)", -1, &okRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            HBRUSH noBg = CreateSolidBrush(RGB(60, 32, 32));
            HPEN noPn = CreatePen(PS_SOLID, 1, RGB(220, 100, 100));
            ob = SelectObject(memDC, noBg);
            op = SelectObject(memDC, noPn);
            RoundRect(memDC, noRc.left, noRc.top, noRc.right, noRc.bottom, 4, 4);
            SelectObject(memDC, op); SelectObject(memDC, ob);
            DeleteObject(noPn); DeleteObject(noBg);
            SetTextColor(memDC, RGB(255, 190, 190));
            DrawTextA(memDC, "CANCEL [ESC]", -1, &noRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldFontMem);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            SelectObject(hdc, oldFontMain);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left, h = rc.bottom - rc.top;
            RECT comboLblRc[4], comboRc[4], editLblRc[7], editRc[7], okRc, noRc;
            seq_dialog_layout(w, h, comboLblRc, comboRc, editLblRc, editRc, &okRc, &noRc);
            if (PtInRect(&okRc, (POINT){ mx, my })) { seq_dialog_apply(hwnd); return 0; }
            if (PtInRect(&noRc, (POINT){ mx, my })) { ShowWindow(hwnd, SW_HIDE); return 0; }
            // Dropdown fields: click toggles the painted popup list.
            for (int i = 0; i < 4; ++i) {
                if (PtInRect(&comboRc[i], (POINT){ mx, my })) {
                    seq_combo_toggle(hwnd, i, &comboRc[i]);
                    return 0;
                }
            }
            // Dice: fill the seed box with a fresh random value.
            if (PtInRect(&g_seqDiceRc, (POINT){ mx, my })) {
                uint32_t ns = ((uint32_t)GetTickCount() * 2654435761u)
                            ^ ((uint32_t)GetTickCount64() << 7) ^ (uint32_t)(mx << 16) ^ (uint32_t)my;
                if (!ns) ns = 1u;
                char sb[16];
                snprintf(sb, sizeof(sb), "%u", (unsigned)ns);
                SetWindowTextA(g_seqDlg.seedEdit, sb);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            // Redraw while the cursor crosses the dice or a dropdown field so
            // hover tints (and leaving them) take effect.
            static bool s_hot = false;
            static int  s_hotField = -1;
            POINT cpt;
            GetCursorPos(&cpt);
            ScreenToClient(hwnd, &cpt);
            bool hot = PtInRect(&g_seqDiceRc, cpt);
            int hotField = -1;
            if (!hot) {
                RECT comboLblRc[4], comboRc[4], editLblRc[7], editRc[7], okRc, noRc;
                RECT crc; GetClientRect(hwnd, &crc);
                seq_dialog_layout(crc.right, crc.bottom, comboLblRc, comboRc, editLblRc, editRc, &okRc, &noRc);
                for (int i = 0; i < 4; ++i)
                    if (PtInRect(&comboRc[i], cpt)) { hotField = i; break; }
            }
            if (hot != s_hot || hotField != s_hotField) {
                s_hot = hot;
                s_hotField = hotField;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            if (g_seqDlg.listHwnd && IsWindow(g_seqDlg.listHwnd)) DestroyWindow(g_seqDlg.listHwnd);
            if (s_hbrEditBg) { DeleteObject(s_hbrEditBg); s_hbrEditBg = NULL; }
            memset(&g_seqDlg, 0, sizeof(g_seqDlg));
            return 0;
        case WM_KEYDOWN:
            // Dialog-level Enter = generate, Escape = cancel. The edit boxes
            // forward these via SeqEditSubProc; this catches them when focus
            // is elsewhere (e.g. after a dropdown closes).
            if (wParam == VK_RETURN) { seq_dialog_apply(hwnd); return 0; }
            if (wParam == VK_ESCAPE) { ShowWindow(hwnd, SW_HIDE); return 0; }
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static inline void open_seq_dialog(HWND parentHwnd) {
    if (!g_seqDlg.hwnd || !IsWindow(g_seqDlg.hwnd)) {
        static bool s_registered = false;
        if (!s_registered) {
            WNDCLASSA wc = { 0 };
            wc.lpfnWndProc = SeqWndProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractSeqClass";
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_registered = true;
        }

        // Client area sized to the shared layout; window frame fitted on top.
        int rw = SEQ_CLIENT_W, rh = SEQ_CLIENT_H;
        RECT wr = { 0, 0, rw, rh };
        AdjustWindowRect(&wr, WS_POPUPWINDOW | WS_CAPTION, FALSE);
        rw = wr.right - wr.left;
        rh = wr.bottom - wr.top;
        int scrW = GetSystemMetrics(SM_CXSCREEN), scrH = GetSystemMetrics(SM_CYSCREEN);
        int rx = (scrW - rw) / 2, ry = (scrH - rh) / 2;
        if (parentHwnd && IsWindow(parentHwnd)) {
            RECT prc; GetWindowRect(parentHwnd, &prc);
            rx = prc.left + ((prc.right - prc.left) - rw) / 2;
            ry = prc.top + ((prc.bottom - prc.top) - rh) / 2;
        }
        g_seqDlg.hwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            "RefractSeqClass", "Generative Sequencer",
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE,
            rx, ry, rw, rh, parentHwnd, NULL, GetModuleHandle(NULL), NULL);

        // Right column: numeric edits from the shared geometry helper. The
        // dropdown fields on the left are painted (no child controls); the
        // item tables above are compile-time constants.
        RECT comboLblRc[4], comboRc[4], editLblRc[7], editRc[7];
        seq_dialog_layout(SEQ_CLIENT_W, SEQ_CLIENT_H, comboLblRc, comboRc, editLblRc, editRc, NULL, NULL);
        (void)comboLblRc; (void)comboRc; (void)editLblRc;

        g_seqDlg.octLoEdit = CreateWindowExA(0, "EDIT", "0", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_NUMBER, editRc[0].left, editRc[0].top, editRc[0].right - editRc[0].left, editRc[0].bottom - editRc[0].top, g_seqDlg.hwnd, (HMENU)(INT_PTR)2005, GetModuleHandle(NULL), NULL);
        g_seqDlg.octHiEdit = CreateWindowExA(0, "EDIT", "2", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_NUMBER, editRc[1].left, editRc[1].top, editRc[1].right - editRc[1].left, editRc[1].bottom - editRc[1].top, g_seqDlg.hwnd, (HMENU)(INT_PTR)2006, GetModuleHandle(NULL), NULL);
        g_seqDlg.lenEdit   = CreateWindowExA(0, "EDIT", "0.25", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER, editRc[2].left, editRc[2].top, editRc[2].right - editRc[2].left, editRc[2].bottom - editRc[2].top, g_seqDlg.hwnd, (HMENU)(INT_PTR)2007, GetModuleHandle(NULL), NULL);
        g_seqDlg.velLoEdit = CreateWindowExA(0, "EDIT", "80", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_NUMBER, editRc[3].left, editRc[3].top, editRc[3].right - editRc[3].left, editRc[3].bottom - editRc[3].top, g_seqDlg.hwnd, (HMENU)(INT_PTR)2008, GetModuleHandle(NULL), NULL);
        g_seqDlg.velHiEdit = CreateWindowExA(0, "EDIT", "120", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_NUMBER, editRc[4].left, editRc[4].top, editRc[4].right - editRc[4].left, editRc[4].bottom - editRc[4].top, g_seqDlg.hwnd, (HMENU)(INT_PTR)2009, GetModuleHandle(NULL), NULL);
        g_seqDlg.countEdit = CreateWindowExA(0, "EDIT", "8", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_NUMBER, editRc[5].left, editRc[5].top, editRc[5].right - editRc[5].left, editRc[5].bottom - editRc[5].top, g_seqDlg.hwnd, (HMENU)(INT_PTR)2010, GetModuleHandle(NULL), NULL);
        g_seqDlg.seedEdit  = CreateWindowExA(0, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_NUMBER, editRc[6].left, editRc[6].top, editRc[6].right - editRc[6].left, editRc[6].bottom - editRc[6].top, g_seqDlg.hwnd, (HMENU)(INT_PTR)2011, GetModuleHandle(NULL), NULL);

        HWND edits[] = { g_seqDlg.octLoEdit, g_seqDlg.octHiEdit, g_seqDlg.lenEdit, g_seqDlg.velLoEdit, g_seqDlg.velHiEdit, g_seqDlg.countEdit, g_seqDlg.seedEdit };
        for (int i = 0; i < 7; ++i) {
            SendMessageA(edits[i], WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
            SetWindowLongPtrA(edits[i], GWLP_USERDATA, (LONG_PTR)SetWindowLongPtrA(edits[i], GWLP_WNDPROC, (LONG_PTR)SeqEditSubProc));
        }

        // Default the Pattern dropdown to "Random" (index 3 in kSeqArpItems).
        // sel[] is zero-initialized, so the pattern would otherwise start at
        // "Up" (index 0) on every fresh dialog.
        g_seqDlg.sel[3] = 3;
    }
    ShowWindow(g_seqDlg.hwnd, SW_SHOW);
    SetForegroundWindow(g_seqDlg.hwnd);
    InvalidateRect(g_seqDlg.hwnd, NULL, FALSE);
}

 

typedef struct { UINT id; float val; const char* txt; } MenuPreset;

static const MenuPreset kRatePresets[] = {
    { ID_RATE_050, 0.5000f, "0.50x (-1 Octave)" },
    { ID_RATE_075, 0.7492f, "0.75x (-5 Semitones)" },
    { ID_RATE_100, 1.0000f, "1.00x (Normal C)" },
    { ID_RATE_125, 1.3348f, "1.33x (+5 Semitones)" },
    { ID_RATE_150, 1.4983f, "1.50x (+7 Semitones)" },
    { ID_RATE_200, 2.0000f, "2.00x (+1 Octave)" },
};
#define RATE_PRESET_COUNT ((int)(sizeof(kRatePresets) / sizeof(kRatePresets[0])))

static const MenuPreset kFadePresets[] = {
    { 0, 0.000f, "None" },
    { 1, 0.125f, "1/8 Beat" },
    { 2, 0.250f, "1/4 Beat" },
    { 3, 0.500f, "1/2 Beat" },
    { 4, 1.000f, "1 Beat" },
    { 5, 2.000f, "2 Beats" },
    { 6, 4.000f, "4 Beats" },
};
#define FADE_PRESET_COUNT ((int)(sizeof(kFadePresets) / sizeof(kFadePresets[0])))

 
static inline void show_midi_clip_context_menu(HWND hwnd, int clipIdx, int screenX, int screenY) {
    if (clipIdx < 0 || clipIdx >= g_Seq.clipCount) return;
    Clip* c = &g_Seq.clips[clipIdx];
    HMENU hMenu = CreatePopupMenu();

    int selectedCount = 0;
    seq_lock();
    for (int i = 0; i < g_Seq.clipCount; ++i) {
        if (g_Seq.clips[i].isSelected) selectedCount++;
    }
    seq_unlock();
    bool isMulti = (selectedCount > 1 && c->isSelected);

    if (!isMulti) {
        AppendMenuA(hMenu, MF_STRING, ID_CLIP_MIDI_EDIT, "Edit MIDI Clip (Dbl-Click / N)...");
        AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    }

    if (isMulti) {
        AppendMenuA(hMenu, MF_STRING, ID_CLIP_MUTE, "Toggle Mute on Selected (M)");
        AppendMenuA(hMenu, MF_STRING, ID_VOL_RESET, "Reset Volume of Selected (100%)");
        AppendMenuA(hMenu, MF_STRING, ID_CLIP_DELETE, "Delete Selected MIDI Clips (Del)");
    }
    else {
        if (c->isMuted) {
            AppendMenuA(hMenu, MF_STRING, ID_CLIP_MUTE, "Mute Clip (M)\t[ON]");
        }
        else {
            AppendMenuA(hMenu, MF_STRING, ID_CLIP_MUTE, "Mute Clip (M)");
        }
        AppendMenuA(hMenu, MF_STRING, ID_VOL_RESET, "Reset Volume (100%)");
        AppendMenuA(hMenu, MF_STRING, ID_CLIP_DELETE, "Delete MIDI Clip (Del)");
    }

    MENUINFO miNoCheck = { sizeof(MENUINFO), MIM_STYLE, MNS_NOCHECK, 0, 0, 0, 0 };
    SetMenuInfo(hMenu, &miNoCheck);

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenX, screenY, 0, hwnd, NULL);
    DestroyMenu(hMenu);
    if (cmd == 0) return;

    if (cmd == ID_CLIP_MIDI_EDIT) {
        open_midi_editor(hwnd, clipIdx);
        return;
    }

    push_undo_state();
    seq_lock();

    if (cmd == ID_CLIP_MUTE) {
        for (int k = 0; k < g_Seq.clipCount; ++k) {
            if ((c->isSelected && g_Seq.clips[k].isSelected) || (!c->isSelected && k == clipIdx)) {
                g_Seq.clips[k].isMuted = !g_Seq.clips[k].isMuted;
                mark_clip_bars_dirty(&g_Seq.clips[k]);
            }
        }
        g_timelineDirty = true;
    }

    if (cmd == ID_VOL_RESET) {
        for (int k = 0; k < g_Seq.clipCount; ++k) {
            if ((c->isSelected && g_Seq.clips[k].isSelected) || (!c->isSelected && k == clipIdx)) {
                g_Seq.clips[k].volume = 1.0f;
                mark_clip_bars_dirty(&g_Seq.clips[k]);
            }
        }
        g_Seq.volumePopupClip = clipIdx;
        g_Seq.volumePopupExpiry = GetTickCount64() + 1200;
    }

    if (cmd == ID_CLIP_DELETE) {
        delete_selected_clips();
    }

    seq_unlock();
    InvalidateRect(hwnd, NULL, FALSE);
}

 
static inline void show_fade_context_menu(HWND hwnd, int clipIdx, bool isFadeIn, int screenX, int screenY) {
    if (clipIdx < 0 || clipIdx >= g_Seq.clipCount) return;

    seq_lock();
    uint8_t curType = isFadeIn ? g_Seq.clips[clipIdx].fadeInType
                               : g_Seq.clips[clipIdx].fadeOutType;
    seq_unlock();

    HMENU hMenu = CreatePopupMenu();
    UINT baseId = isFadeIn ? ID_FADE_IN_CURVE_LIN : ID_FADE_OUT_CURVE_LIN;

    AppendMenuA(hMenu, MF_STRING | (curType == FADE_CURVE_LINEAR ? MF_CHECKED : 0), baseId + FADE_CURVE_LINEAR, "Linear");
    AppendMenuA(hMenu, MF_STRING | (curType == FADE_CURVE_EXP ? MF_CHECKED : 0),    baseId + FADE_CURVE_EXP,    "Fast Transient (Exponential)");
    AppendMenuA(hMenu, MF_STRING | (curType == FADE_CURVE_SMOOTH ? MF_CHECKED : 0), baseId + FADE_CURVE_SMOOTH, "Smooth (S-Curve)");
    AppendMenuA(hMenu, MF_STRING | (curType == FADE_CURVE_LOG ? MF_CHECKED : 0),    baseId + FADE_CURVE_LOG,    "Slow / Long (Logarithmic)");

    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenX, screenY, 0, hwnd, NULL);
    DestroyMenu(hMenu);

    if (cmd >= (int)baseId && cmd < (int)baseId + FADE_CURVE_COUNT) {
        uint8_t newType = (uint8_t)(cmd - baseId);
        push_undo_state();
        seq_lock();
        // Always apply to the clicked clip first...
        if (isFadeIn) {
            g_Seq.clips[clipIdx].fadeInType = newType;
            // ...then to every selected clip so the choice propagates across
            // the whole multi-selection (the clicked clip is set again if it
            // is selected, which is harmless).
            for (int i = 0; i < g_Seq.clipCount; ++i) {
                if (g_Seq.clips[i].isSelected) g_Seq.clips[i].fadeInType = newType;
            }
        }
        else {
            g_Seq.clips[clipIdx].fadeOutType = newType;
            for (int i = 0; i < g_Seq.clipCount; ++i) {
                if (g_Seq.clips[i].isSelected) g_Seq.clips[i].fadeOutType = newType;
            }
        }
         
        for (int i = 0; i < g_Seq.clipCount; ++i) {
            if (g_Seq.clips[i].isSelected) {
                mark_clip_bars_dirty(&g_Seq.clips[i]);
            }
        }
        seq_unlock();
        g_timelineDirty = true;
        g_Seq.isModified = true;
        InvalidateRect(hwnd, NULL, FALSE);
        if (g_hWnd && g_hWnd != hwnd) InvalidateRect(g_hWnd, NULL, FALSE);
    }
}

// --- Interactive Transient Slicing dialog ------------------------------------
// A modeless single-slider popup that scrubs transient-detection sensitivity
// and previews slice boundaries as dashed overlay lines on the timeline. No
// destructive edits happen until [APPLY] / ENTER; ESC / click-outside cancels.

// Convert a span of sample frames to timeline beats. This matches the codebase
// convention (see split_single_clip_internal / render_frames) where playbackRate
// is in the denominator: beats = frames * bpm / (60 * SAMPLE_RATE * playbackRate).
static inline float slice_beats_from_frames(double frames, float bpm, float pRate) {
    if (pRate < 0.01f) pRate = 1.0f;
    if (bpm <= 0.0f) bpm = 120.0f;
    return (float)(frames * (double)bpm / (60.0 * (double)SAMPLE_RATE * (double)pRate));
}

// Number of sample frames a clip actually spans (bounded by the sample buffer).
static inline ma_uint64 slice_clip_region_frames(const Clip* c, const AudioSample* s, float fpb) {
    double pRate = (c->playbackRate > 0.01f) ? c->playbackRate : 1.0f;
    double lenFrames = (double)c->lengthBeats * (double)fpb * (double)pRate;
    if (lenFrames < 1.0) lenFrames = 1.0;
    if (c->sampleOffsetFrames >= s->frameCount) return 0;
    ma_uint64 avail = s->frameCount - c->sampleOffsetFrames;
    if ((double)avail < lenFrames) lenFrames = (double)avail;
    return (ma_uint64)lenFrames;
}

// Recompute the preview slice maps for every target clip from the current
// sensitivity. Non-destructive: only writes g_slicePreview.maps.
static inline void slice_preview_recompute(void) {
    if (g_Seq.isBusy) return;
    float fpb = frames_per_beat(g_Seq.bpm);
    float sens = g_slicePreview.sensitivity;
    seq_lock();
    for (int ti = 0; ti < g_slicePreview.clipCount; ++ti) {
        TransientSliceMap* m = &g_slicePreview.maps[ti];
        m->count = 0;
        int idx = g_slicePreview.clipIdx[ti];
        if (idx < 0 || idx >= g_Seq.clipCount) continue;
        const Clip* c = &g_Seq.clips[idx];
        if (c->isMidi) continue;
        if (c->sampleIndex < 0 || c->sampleIndex >= g_Seq.sampleCount) continue;
        const AudioSample* s = &g_Seq.samples[c->sampleIndex];
        if (!s->loaded || !s->pFrames || s->frameCount == 0) continue;
        ma_uint64 region = slice_clip_region_frames(c, s, fpb);
        if (region == 0) continue;
        *m = detect_clip_transients(s->pFrames + (size_t)c->sampleOffsetFrames * NUM_CHANNELS,
                                    (size_t)region, NUM_CHANNELS, (uint32_t)SAMPLE_RATE, sens);
    }
    seq_unlock();
}

// Commit the preview maps: turn each target clip into Slice 0 and append
// Slices 1..N as new zero-copy clips (all share the parent's sampleIndex).
static inline void commit_slice_preview(void) {
    if (!g_slicePreview.active || g_slicePreview.clipCount <= 0) return;
    if (g_Seq.isBusy) return;

    push_undo_state();
    seq_lock();

    float bpm = g_Seq.bpm;
    float fpb = frames_per_beat(bpm);
    float minLen = get_min_clip_length_beats();
    int budget = MAX_CLIPS - g_Seq.clipCount;
    if (budget < 0) budget = 0;

    for (int ti = 0; ti < g_slicePreview.clipCount; ++ti) {
        if (budget <= 0) break;
        int idx = g_slicePreview.clipIdx[ti];
        if (idx < 0 || idx >= g_Seq.clipCount) continue;
        Clip* parent = &g_Seq.clips[idx];
        if (parent->isMidi) continue;
        if (parent->sampleIndex < 0 || parent->sampleIndex >= g_Seq.sampleCount) continue;
        const AudioSample* s = &g_Seq.samples[parent->sampleIndex];
        if (!s->loaded || !s->pFrames || s->frameCount == 0) continue;

        const TransientSliceMap* m = &g_slicePreview.maps[ti];
        size_t cnt = m->count;
        if (cnt < 2) continue;                 // single slice: nothing to split

        size_t addCount = cnt - 1;
        if ((int)addCount > budget) addCount = (size_t)budget;
        if (addCount < 1) continue;
        budget -= (int)addCount;

        float pRate = (parent->playbackRate > 0.01f) ? parent->playbackRate : 1.0f;
        ma_uint64 clipFrames = slice_clip_region_frames(parent, s, fpb);

        // Slice 0: the original clip becomes the first slice.
        double len0 = (double)(m->frame_indices[1] - m->frame_indices[0]);
        parent->lengthBeats = slice_beats_from_frames(len0, bpm, pRate);
        if (parent->lengthBeats < minLen) parent->lengthBeats = minLen;
        parent->isSelected = true;

        // Slices 1..addCount appended as new clips.
        for (size_t k = 1; k <= addCount; ++k) {
            size_t startFr = m->frame_indices[k];
            size_t endFr = (k + 1 < cnt) ? m->frame_indices[k + 1] : (size_t)clipFrames;
            if (endFr <= startFr) endFr = startFr + 1;

            Clip n;
            memset(&n, 0, sizeof(Clip));
            n.nextClipInBar = 0xFFFF;
            n.sampleIndex = parent->sampleIndex;   // zero-copy: share the buffer
            n.track = parent->track;
            n.startBeat = parent->startBeat + slice_beats_from_frames((double)startFr, bpm, pRate);
            n.lengthBeats = slice_beats_from_frames((double)(endFr - startFr), bpm, pRate);
            if (n.lengthBeats < minLen) n.lengthBeats = minLen;
            n.sampleOffsetFrames = parent->sampleOffsetFrames + startFr;
            if (n.sampleOffsetFrames >= s->frameCount) break;
            n.volume = parent->volume;
            n.playbackRate = parent->playbackRate;
            n.fadeInBeats = 0.0f;
            n.fadeOutBeats = 0.0f;
            n.fadeInType = parent->fadeInType;
            n.fadeOutType = parent->fadeOutType;
            n.isSelected = true;

            int newIdx = g_Seq.clipCount;
            g_Seq.clips[newIdx] = n;
            memset(&g_ClipGran[newIdx], 0, sizeof(GranularEngine));
            g_ClipGran[newIdx].clipIdx = newIdx;
            g_ClipGran[newIdx].trackIdx = n.track;
            g_ClipGran[newIdx].sampleIndex = n.sampleIndex;
            g_ClipGran[newIdx].volume = 0.85f;
            g_Seq.clipCount++;
            synth_state_init_clip(newIdx);
        }
    }

    seq_unlock();
    cseq_clip_structure_changed();
    g_timelineDirty = true;
    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
}

typedef struct {
    HWND hwnd;
    bool isDragging;
} SliceWinContext;
static SliceWinContext g_SliceWin = { 0 };

static inline void slice_dialog_button_rects(int w, int h, RECT* apply, RECT* cancel) {
    int bw = scale_x(90), bh = scale_y(28);
    int gap = scale_x(16);
    int total = bw * 2 + gap;
    int x0 = (w - total) / 2;
    int y0 = h - bh - scale_y(14);
    apply->left = x0; apply->top = y0; apply->right = x0 + bw; apply->bottom = y0 + bh;
    cancel->left = x0 + bw + gap; cancel->top = y0; cancel->right = x0 + bw + gap + bw; cancel->bottom = y0 + bh;
}

static inline void slice_dialog_commit(void) {
    commit_slice_preview();
    g_slicePreview.active = false;
    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
    ShowWindow(g_SliceWin.hwnd, SW_HIDE);
}

static inline void slice_dialog_cancel(void) {
    g_slicePreview.active = false;
    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
    ShowWindow(g_SliceWin.hwnd, SW_HIDE);
}

static LRESULT CALLBACK SliceWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            slice_dialog_cancel();
            return 0;
        }
        break;

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int trackLeft = 24, trackRight = w - 24, trackY = 60;
        int trackW = trackRight - trackLeft;
        if (my >= trackY - 12 && my <= trackY + 12 && mx >= trackLeft && mx <= trackRight && trackW > 0) {
            float norm = (float)(mx - trackLeft) / (float)trackW;
            if (norm < 0.01f) norm = 0.01f;
            if (norm > 1.0f) norm = 1.0f;
            g_slicePreview.sensitivity = norm;
            slice_preview_recompute();
            g_SliceWin.isDragging = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (g_SliceWin.isDragging) {
            int mx = GET_X_LPARAM(lParam);
            RECT rc; GetClientRect(hwnd, &rc);
            int trackLeft = 24, trackRight = rc.right - 24;
            int trackW = trackRight - trackLeft;
            if (trackW > 0) {
                float norm = (float)(mx - trackLeft) / (float)trackW;
                if (norm < 0.01f) norm = 0.01f;
                if (norm > 1.0f) norm = 1.0f;
                g_slicePreview.sensitivity = norm;
                slice_preview_recompute();
                InvalidateRect(hwnd, NULL, FALSE);
                if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        RECT apply, cancel;
        slice_dialog_button_rects(w, h, &apply, &cancel);
        bool overApply = (mx >= apply.left && mx <= apply.right && my >= apply.top && my <= apply.bottom);
        bool overCancel = (mx >= cancel.left && mx <= cancel.right && my >= cancel.top && my <= cancel.bottom);
        if (g_SliceWin.isDragging) {
            g_SliceWin.isDragging = false;
            ReleaseCapture();
        }
        if (overApply) { slice_dialog_commit(); return 0; }
        if (overCancel) { slice_dialog_cancel(); return 0; }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_RETURN) { slice_dialog_commit(); return 0; }
        if (wParam == VK_ESCAPE) { slice_dialog_cancel(); return 0; }
        break;

    case WM_CLOSE:
        slice_dialog_cancel();
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HFONT oldFontMain = SELECT_UI_FONT(hdc);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0) {
            SelectObject(hdc, oldFontMain);
            EndPaint(hwnd, &ps);
            return 0;
        }

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
        HFONT oldFontMem = SELECT_UI_FONT(memDC);
        HGDIOBJ origPen = GetCurrentObject(memDC, OBJ_PEN);
        HGDIOBJ origBrush = GetCurrentObject(memDC, OBJ_BRUSH);

        HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
        FillRect(memDC, &rc, bg);
        DeleteObject(bg);

        int trackLeft = 24, trackRight = w - 24;
        int trackY = 60, trackW = trackRight - trackLeft;

        HPEN railPen = CreatePen(PS_SOLID, 4, RGB(28, 33, 42));
        SelectObject(memDC, railPen);
        MoveToEx(memDC, trackLeft, trackY, NULL);
        LineTo(memDC, trackRight, trackY);
        SelectObject(memDC, origPen);
        DeleteObject(railPen);

        float sens = g_slicePreview.sensitivity;
        float norm = sens; if (norm < 0.01f) norm = 0.01f; if (norm > 1.0f) norm = 1.0f;
        int thumbX = trackLeft + (int)(norm * (float)trackW);

        HPEN fillPen = CreatePen(PS_SOLID, 4, RGB(80, 210, 240));
        SelectObject(memDC, fillPen);
        MoveToEx(memDC, trackLeft, trackY, NULL);
        LineTo(memDC, thumbX, trackY);
        SelectObject(memDC, origPen);
        DeleteObject(fillPen);

        draw_aa_circle(memDC, thumbX, trackY, 6.5f, RGB(80, 240, 180), RGB(255, 255, 255), 1.8f);

        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, RGB(215, 225, 240));
        char hdrBuf[96];
        if (g_slicePreview.clipCount > 1)
            snprintf(hdrBuf, sizeof(hdrBuf), "How much slicing? (%d clips)", g_slicePreview.clipCount);
        else
            snprintf(hdrBuf, sizeof(hdrBuf), "How much slicing?");
        RECT hdrRc = { 0, 10, w, 30 };
        DrawTextA(memDC, hdrBuf, -1, &hdrRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SetTextColor(memDC, RGB(80, 95, 115));
        TextOutA(memDC, trackLeft, trackY + 12, "1%", 2);
        TextOutA(memDC, trackRight - 20, trackY + 12, "100%", 4);

        RECT apply, cancel;
        slice_dialog_button_rects(w, h, &apply, &cancel);
        // [APPLY]
        HBRUSH ab = CreateSolidBrush(RGB(30, 96, 66));
        HPEN ap = CreatePen(PS_SOLID, 1, RGB(80, 210, 150));
        HGDIOBJ ob = SelectObject(memDC, ab);
        HGDIOBJ op = SelectObject(memDC, ap);
        RoundRect(memDC, apply.left, apply.top, apply.right, apply.bottom, 6, 6);
        SelectObject(memDC, ob); SelectObject(memDC, op);
        DeleteObject(ap); DeleteObject(ab);
        SetTextColor(memDC, RGB(200, 255, 225));
        DrawTextA(memDC, "[APPLY]", -1, &apply, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        // [CANCEL]
        HBRUSH cb = CreateSolidBrush(RGB(96, 40, 40));
        HPEN cp = CreatePen(PS_SOLID, 1, RGB(210, 90, 90));
        ob = SelectObject(memDC, cb);
        op = SelectObject(memDC, cp);
        RoundRect(memDC, cancel.left, cancel.top, cancel.right, cancel.bottom, 6, 6);
        SelectObject(memDC, ob); SelectObject(memDC, op);
        DeleteObject(cp); DeleteObject(cb);
        SetTextColor(memDC, RGB(255, 200, 200));
        DrawTextA(memDC, "[CANCEL]", -1, &cancel, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldFontMem);
        SelectObject(memDC, origPen);
        SelectObject(memDC, origBrush);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        SelectObject(hdc, oldFontMain);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static inline void open_slice_dialog(HWND parentHwnd, const int* clipIdxList, int clipCount, int screenX, int screenY) {
    if (clipCount <= 0) return;
    g_slicePreview.clipCount = clipCount;
    for (int i = 0; i < clipCount && i < MAX_CLIPS; ++i) g_slicePreview.clipIdx[i] = clipIdxList[i];
    g_slicePreview.sensitivity = 0.5f;
    g_slicePreview.active = true;
    slice_preview_recompute();

    if (!g_SliceWin.hwnd) {
        static bool s_registered = false;
        if (!s_registered) {
            WNDCLASSA wc = { 0 };
            wc.lpfnWndProc   = SliceWndProc;
            wc.hInstance     = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractSliceWindowClass";
            wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_registered = true;
        }
        int rw = 420, rh = 170;
        int rx = screenX, ry = screenY;
        int scrW = GetSystemMetrics(SM_CXSCREEN), scrH = GetSystemMetrics(SM_CYSCREEN);
        if (rx + rw > scrW) rx = scrW - rw;
        if (ry + rh > scrH) ry = scrH - rh;
        if (rx < 0) rx = 0;
        if (ry < 0) ry = 0;

        g_SliceWin.hwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            "RefractSliceWindowClass",
            "Slice",
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE,
            rx, ry, rw, rh,
            parentHwnd, NULL, GetModuleHandle(NULL), NULL
        );
    }
    ShowWindow(g_SliceWin.hwnd, SW_SHOW);
    SetForegroundWindow(g_SliceWin.hwnd);
    InvalidateRect(g_SliceWin.hwnd, NULL, FALSE);
    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
}

 
static inline void show_clip_context_menu(HWND hwnd, int clipIdx, int screenX, int screenY) {
    if (clipIdx < 0 || clipIdx >= g_Seq.clipCount) return;
    Clip* c = &g_Seq.clips[clipIdx];
    HMENU hMenu = CreatePopupMenu();

    int selectedCount = 0;
    seq_lock();
    for (int i = 0; i < g_Seq.clipCount; ++i) {
        if (g_Seq.clips[i].isSelected) selectedCount++;
    }
    seq_unlock();
    bool isMulti = (selectedCount > 1 && c->isSelected);

    // Feature 2: determine the slice target clips (all selected for batch,
    // else just the clicked clip) and whether they are all sliceable: the app
    // must not be busy, and every target must be a non-MIDI sample clip with a
    // valid loaded PCM buffer. If not sliceable the entry is hidden.
    int sliceTargets[MAX_CLIPS];
    int sliceTargetCount = 0;
    bool canSlice = !g_Seq.isBusy;
    if (canSlice) {
        seq_lock();
        for (int i = 0; i < g_Seq.clipCount && sliceTargetCount < MAX_CLIPS; ++i) {
            bool target = isMulti ? g_Seq.clips[i].isSelected : (i == clipIdx);
            if (!target) continue;
            const Clip* sc = &g_Seq.clips[i];
            bool ok = !sc->isMidi &&
                      sc->sampleIndex >= 0 && sc->sampleIndex < g_Seq.sampleCount &&
                      g_Seq.samples[sc->sampleIndex].loaded &&
                      g_Seq.samples[sc->sampleIndex].pFrames &&
                      g_Seq.samples[sc->sampleIndex].frameCount > 0;
            if (!ok) { canSlice = false; break; }
            sliceTargets[sliceTargetCount++] = i;
        }
        seq_unlock();
    }
    if (sliceTargetCount == 0) canSlice = false;

    
    HMENU hRateMenu = CreatePopupMenu();
    AppendMenuA(hRateMenu, MF_STRING, ID_RATE_CUSTOM, "Custom...");
    AppendMenuA(hRateMenu, MF_SEPARATOR, 0, NULL);
    for (int i = 0; i < RATE_PRESET_COUNT; ++i) {
        char buf[64];
        bool isCur = fabsf(c->playbackRate - kRatePresets[i].val) < 0.01f;
        snprintf(buf, sizeof(buf), "%s\t%s", kRatePresets[i].txt, (!isMulti && isCur) ? "[ON]" : "[  ]");
        AppendMenuA(hRateMenu, MF_STRING, kRatePresets[i].id, buf);
    }
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hRateMenu, isMulti ? "Batch Playback Rate" : "Playback Rate");

    
    HMENU hFadeMenu = CreatePopupMenu();
    HMENU hFadeInMenu = CreatePopupMenu();
    HMENU hFadeOutMenu = CreatePopupMenu();

    for (int i = 0; i < FADE_PRESET_COUNT; ++i) {
        char inBuf[64], outBuf[64];
        bool isInCur = (kFadePresets[i].val <= 0.001f) ? (c->fadeInBeats <= 0.01f) : (fabsf(c->fadeInBeats - kFadePresets[i].val) < 0.01f);
        bool isOutCur = (kFadePresets[i].val <= 0.001f) ? (c->fadeOutBeats <= 0.01f) : (fabsf(c->fadeOutBeats - kFadePresets[i].val) < 0.01f);

        snprintf(inBuf, sizeof(inBuf), "%s\t%s", kFadePresets[i].txt, (!isMulti && isInCur) ? "[ON]" : "[  ]");
        snprintf(outBuf, sizeof(outBuf), "%s\t%s", kFadePresets[i].txt, (!isMulti && isOutCur) ? "[ON]" : "[  ]");

        AppendMenuA(hFadeInMenu, MF_STRING, ID_CLIP_FADE_IN_000 + i, inBuf);
        AppendMenuA(hFadeOutMenu, MF_STRING, ID_CLIP_FADE_OUT_000 + i, outBuf);
    }
    AppendMenuA(hFadeMenu, MF_POPUP, (UINT_PTR)hFadeInMenu, "Fade In");
    AppendMenuA(hFadeMenu, MF_POPUP, (UINT_PTR)hFadeOutMenu, "Fade Out");
    AppendMenuA(hFadeMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hFadeMenu, MF_STRING, ID_CLIP_FADE_CLEAR, isMulti ? "Reset All Fades" : "Reset Fades");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hFadeMenu, isMulti ? "Batch Fade Envelope" : "Fade Envelope");

    
    if (!isMulti) {
        if (c->isMidi) {
            AppendMenuA(hMenu, MF_STRING, ID_CLIP_MIDI_EDIT, "Edit MIDI...");
            AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
        }
        AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
        bool isGran = granular_is_clip_enabled(clipIdx);
        if (isGran) {
            AppendMenuA(hMenu, MF_STRING, ID_CLIP_GRANULAR, "Granular Editor...\t[ON]");
        }
        else {
            AppendMenuA(hMenu, MF_STRING, ID_CLIP_GRANULAR, "Granular Editor...");
        }
        AppendMenuA(hMenu, MF_STRING, ID_CLIP_GRANULAR_TOGGLE,
            isGran ? "Disable Granular Mode" : "Enable Granular Mode");
    }

    // Feature 2: interactive transient slicing, in its own separator-delimited
    // group. Label depends on selection: "Slice..." (single) / "Batch Slice..."
    // (multi). Hidden entirely when no sliceable target exists.
    if (canSlice) {
        AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
        if (sliceTargetCount > 1) {
            char batchLabel[64];
            snprintf(batchLabel, sizeof(batchLabel), "Batch Slice... (%d clips)", sliceTargetCount);
            AppendMenuA(hMenu, MF_STRING, ID_CLIP_SLICE, batchLabel);
            char batchRevLabel[64];
            snprintf(batchRevLabel, sizeof(batchRevLabel), "Batch Reverse... (%d clips)", sliceTargetCount);
            AppendMenuA(hMenu, MF_STRING, ID_CLIP_REVERSE, batchRevLabel);
        } else {
            AppendMenuA(hMenu, MF_STRING, ID_CLIP_SLICE, "Slice...");
            AppendMenuA(hMenu, MF_STRING, ID_CLIP_REVERSE, "Reverse");
        }
    }

    
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    if (isMulti) {
        AppendMenuA(hMenu, MF_STRING, ID_CLIP_MUTE, "Toggle Mute on Selected (M)");
        AppendMenuA(hMenu, MF_STRING, ID_VOL_RESET, "Reset Volume of Selected (100%)");
        AppendMenuA(hMenu, MF_STRING, ID_CLIP_DELETE, "Delete Selected Clips (Del)");
    }
    else {
        if (c->isMuted) {
            AppendMenuA(hMenu, MF_STRING, ID_CLIP_MUTE, "Mute Clip (M)\t[ON]");
        }
        else {
            AppendMenuA(hMenu, MF_STRING, ID_CLIP_MUTE, "Mute Clip (M)");
        }
        AppendMenuA(hMenu, MF_STRING, ID_VOL_RESET, "Reset Volume (100%)");
        AppendMenuA(hMenu, MF_STRING, ID_CLIP_DELETE, "Delete Clip (Del)");
    }

    
    MENUINFO miNoCheck = { sizeof(MENUINFO), MIM_STYLE, MNS_NOCHECK, 0, 0, 0, 0 };
    SetMenuInfo(hMenu, &miNoCheck);
    SetMenuInfo(hRateMenu, &miNoCheck);
    SetMenuInfo(hFadeMenu, &miNoCheck);
    SetMenuInfo(hFadeInMenu, &miNoCheck);
    SetMenuInfo(hFadeOutMenu, &miNoCheck);

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenX, screenY, 0, hwnd, NULL);
    DestroyMenu(hMenu);
    if (cmd == 0) return;

    
    if (cmd == ID_CLIP_MIDI_EDIT) {
        open_midi_editor(hwnd, clipIdx);
        return;
    }

    
    if (cmd == ID_CLIP_GRANULAR) {
        open_clip_granular_dialog(hwnd, clipIdx);
        return;
    }
    if (cmd == ID_CLIP_GRANULAR_TOGGLE) {
        granular_toggle_clip(clipIdx);
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }
    if (cmd == ID_RATE_CUSTOM) {
        open_custom_rate_dialog(hwnd, clipIdx, -1);
        return;
    }
    if (cmd == ID_CLIP_SLICE) {
        open_slice_dialog(hwnd, sliceTargets, sliceTargetCount, screenX, screenY);
        return;
    }
    if (cmd == ID_CLIP_REVERSE) {
        reverse_clips_action(sliceTargets, sliceTargetCount);
        return;
    }

    push_undo_state();
    seq_lock();

    
    for (int i = 0; i < RATE_PRESET_COUNT; ++i) {
        if (cmd == (int)kRatePresets[i].id) {
            for (int k = 0; k < g_Seq.clipCount; ++k) {
                if ((c->isSelected && g_Seq.clips[k].isSelected) || (!c->isSelected && k == clipIdx)) {
                    g_Seq.clips[k].playbackRate = kRatePresets[i].val;
                    mark_clip_bars_dirty(&g_Seq.clips[k]);
                }
            }
            break;
        }
    }

    
    if (cmd >= ID_CLIP_FADE_IN_000 && cmd <= ID_CLIP_FADE_IN_300) {
        int idx = cmd - ID_CLIP_FADE_IN_000;
        if (idx >= 0 && idx < FADE_PRESET_COUNT) {
            for (int k = 0; k < g_Seq.clipCount; ++k) {
                if ((c->isSelected && g_Seq.clips[k].isSelected) || (!c->isSelected && k == clipIdx)) {
                    float v = kFadePresets[idx].val;
                    if (v > g_Seq.clips[k].lengthBeats) v = g_Seq.clips[k].lengthBeats;
                    g_Seq.clips[k].fadeInBeats = v;
                    mark_clip_bars_dirty(&g_Seq.clips[k]);
                }
            }
        }
    }

    
    if (cmd >= ID_CLIP_FADE_OUT_000 && cmd <= ID_CLIP_FADE_OUT_300) {
        int idx = cmd - ID_CLIP_FADE_OUT_000;
        if (idx >= 0 && idx < FADE_PRESET_COUNT) {
            for (int k = 0; k < g_Seq.clipCount; ++k) {
                if ((c->isSelected && g_Seq.clips[k].isSelected) || (!c->isSelected && k == clipIdx)) {
                    float v = kFadePresets[idx].val;
                    if (v > g_Seq.clips[k].lengthBeats) v = g_Seq.clips[k].lengthBeats;
                    g_Seq.clips[k].fadeOutBeats = v;
                    mark_clip_bars_dirty(&g_Seq.clips[k]);
                }
            }
        }
    }

    
    if (cmd == ID_CLIP_FADE_CLEAR) {
        for (int k = 0; k < g_Seq.clipCount; ++k) {
            if ((c->isSelected && g_Seq.clips[k].isSelected) || (!c->isSelected && k == clipIdx)) {
                g_Seq.clips[k].fadeInBeats = 0.0f;
                mark_clip_bars_dirty(&g_Seq.clips[k]);
                g_Seq.clips[k].fadeOutBeats = 0.0f;
            }
        }
    }

    
    if (cmd == ID_CLIP_MUTE) {
        for (int k = 0; k < g_Seq.clipCount; ++k) {
            if ((c->isSelected && g_Seq.clips[k].isSelected) || (!c->isSelected && k == clipIdx)) {
                g_Seq.clips[k].isMuted = !g_Seq.clips[k].isMuted;
                mark_clip_bars_dirty(&g_Seq.clips[k]);
            }
        }
        g_timelineDirty = true;
    }

    
    if (cmd == ID_VOL_RESET) {
        for (int k = 0; k < g_Seq.clipCount; ++k) {
            if ((c->isSelected && g_Seq.clips[k].isSelected) || (!c->isSelected && k == clipIdx)) {
                g_Seq.clips[k].volume = 1.0f;
                mark_clip_bars_dirty(&g_Seq.clips[k]);
            }
        }
        g_Seq.volumePopupClip = clipIdx;
        g_Seq.volumePopupExpiry = GetTickCount64() + 1200;
    }

    
    if (cmd == ID_CLIP_DELETE) {
        delete_selected_clips();
    }

    seq_unlock();
    InvalidateRect(hwnd, NULL, FALSE);
}

 
HWND g_fxRackHwnd = NULL;
static int  g_fxTrack = -1;

 
static HFONT g_fxRackSmallFont = NULL;

static HFONT fx_rack_small_font(void) {
    if (!g_fxRackSmallFont) {
        int px = (int)(11.0f * scale_font(1.0f) + 0.5f);
        if (px < 8) px = 8;
        g_fxRackSmallFont = cseq_create_face_font("Inter", px);
        if (!g_fxRackSmallFont) g_fxRackSmallFont = cseq_create_face_font("Segoe UI", px);
    }
    return g_fxRackSmallFont;
}

static HFONT g_fxRackClearFont = NULL;

static HFONT fx_rack_clear_font(void) {
    if (!g_fxRackClearFont) {
        int px = (int)(13.0f * scale_font(1.0f) + 0.5f);
        if (px < 9) px = 9;
        g_fxRackClearFont = cseq_create_face_font("Inter", px);
        if (!g_fxRackClearFont) g_fxRackClearFont = cseq_create_face_font("Segoe UI", px);
    }
    return g_fxRackClearFont;
}

 

 
static inline void fx_draw_aa_capsule(HDC hdc, float ax, float ay, float bx, float by,
                                      float radius, COLORREF color) {
    float minX = (ax < bx ? ax : bx) - radius - 1.0f;
    float minY = (ay < by ? ay : by) - radius - 1.0f;
    float maxX = (ax > bx ? ax : bx) + radius + 1.0f;
    float maxY = (ay > by ? ay : by) + radius + 1.0f;
    int x0 = (int)floorf(minX), y0 = (int)floorf(minY);
    int w = (int)ceilf(maxX) - x0 + 1;
    int h = (int)ceilf(maxY) - y0 + 1;
    if (w <= 0 || h <= 0 || w > 2048 || h > 2048) return;

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
    float abx = bx - ax, aby = by - ay;
    float len2 = abx * abx + aby * aby;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float sumA = 0.0f;
            for (int sy = 0; sy < SAMPLES; ++sy) {
                float py = (float)(y + y0) + offset + (float)sy * step;
                for (int sx = 0; sx < SAMPLES; ++sx) {
                    float px = (float)(x + x0) + offset + (float)sx * step;
                    float t = 0.0f;
                    if (len2 > 0.000001f) {
                        t = ((px - ax) * abx + (py - ay) * aby) / len2;
                        if (t < 0.0f) t = 0.0f;
                        if (t > 1.0f) t = 1.0f;
                    }
                    float dx = px - (ax + t * abx);
                    float dy = py - (ay + t * aby);
                    if (dx * dx + dy * dy <= radius * radius) sumA += 1.0f;
                }
            }
            if (sumA <= 0.001f) {
                pPix[y * w + x] = 0;
            } else {
                BYTE a = (BYTE)(sumA * invTotal * 255.0f + 0.5f);
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

 
static inline void fx_draw_aa_knob(HDC hdc, int cx, int cy, float radius, float norm) {
    int rCeil = (int)ceilf(radius) + 3;
    int size = rCeil * 2 + 1;
    if (size <= 0) return;

    HDC memDC = CreateCompatibleDC(hdc);
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* pBits = NULL;
    HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hBmp || !pBits) { if (memDC) DeleteDC(memDC); return; }

    HGDIOBJ oldBmp = SelectObject(memDC, hBmp);
    DWORD* pPix = (DWORD*)pBits;

    const int SAMPLES = 4;
    const float step = 1.0f / (float)SAMPLES;
    const float offset = step * 0.5f;
    const float invTotal = 1.0f / (float)(SAMPLES * SAMPLES);
    const float kDeg = 57.29577951f;    
    const float kRad = 0.0174532925f;   

    const float trackR = radius * 0.92f;
    const float trackHW = 1.7f;
    const float bodyR = radius * 0.62f;
    const float aMin = 135.0f, aSweep = 270.0f;
    float nClamped = norm;
    if (nClamped < 0.0f) nClamped = 0.0f;
    if (nClamped > 1.0f) nClamped = 1.0f;
    float aVal = aMin + aSweep * nClamped;
    float aValRad = aVal * kRad;
     
    float iAx = cosf(aValRad) * bodyR * 0.15f, iAy = sinf(aValRad) * bodyR * 0.15f;
    float iBx = cosf(aValRad) * bodyR * 0.85f, iBy = sinf(aValRad) * bodyR * 0.85f;
    float abx = iBx - iAx, aby = iBy - iAy;
    float len2 = abx * abx + aby * aby;

     
    const float trR = 30.0f,  trG = 36.0f,  trB = 46.0f;     
    const float acR = 80.0f,  acG = 210.0f, acB = 240.0f;    
    const float boR = 24.0f,  boG = 30.0f,  boB = 40.0f;     
    const float bdR = 60.0f,  bdG = 72.0f,  bdB = 90.0f;     
    const float inR = 150.0f, inG = 235.0f, inB = 255.0f;    

    float center = (float)rCeil;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float sumR = 0, sumG = 0, sumB = 0, sumA = 0;
            for (int sy = 0; sy < SAMPLES; ++sy) {
                float py = ((float)y + offset + (float)sy * step) - center;
                for (int sx = 0; sx < SAMPLES; ++sx) {
                    float px = ((float)x + offset + (float)sx * step) - center;
                    float d = sqrtf(px * px + py * py);
                    float ang = atan2f(py, px) * kDeg;
                    if (ang < 0.0f) ang += 360.0f;
                    // Sweep-relative angle: 0 at aMin (135 deg), growing
                    // clockwise through the bottom to aSweep (270 deg) at
                    // 45 deg. Plain wrapped angles can't express this range.
                    float rel = ang - aMin;
                    if (rel < 0.0f) rel += 360.0f;

                    float rC = 0, gC = 0, bC = 0, aC = 0;

                    if (rel <= aSweep && fabsf(d - trackR) <= trackHW) {
                        rC = trR; gC = trG; bC = trB; aC = 1.0f;
                    }

                    if (rel <= aSweep * nClamped && fabsf(d - trackR) <= trackHW + 0.4f) {
                        rC = acR; gC = acG; bC = acB; aC = 1.0f;
                    }
                     
                    if (d <= bodyR) {
                        if (d > bodyR - 1.0f) { rC = bdR; gC = bdG; bC = bdB; }
                        else                  { rC = boR; gC = boG; bC = boB; }
                        aC = 1.0f;
                    }
                     
                    if (len2 > 0.000001f) {
                        float t = (px * abx + py * aby) / len2;
                        if (t < 0.0f) t = 0.0f;
                        if (t > 1.0f) t = 1.0f;
                        float dx = px - (iAx + t * abx);
                        float dy = py - (iAy + t * aby);
                        if (dx * dx + dy * dy <= 1.44f) { rC = inR; gC = inG; bC = inB; aC = 1.0f; }
                    }

                    sumR += rC * aC; sumG += gC * aC; sumB += bC * aC; sumA += aC;
                }
            }
            if (sumA <= 0.001f) {
                pPix[y * size + x] = 0;
            } else {
                BYTE a = (BYTE)(sumA * invTotal * 255.0f + 0.5f);
                BYTE pr = (BYTE)(sumR * invTotal + 0.5f);
                BYTE pg = (BYTE)(sumG * invTotal + 0.5f);
                BYTE pb = (BYTE)(sumB * invTotal + 0.5f);
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

FxRackUiState g_fxRackUi = { 0 };

 
static void fx_rack_notify_main(void) {
    invalidate_timeline_cache();
    if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, FALSE);
}

 
static COLORREF fx_rack_text_col(COLORREF base) {
     
    if (!g_fxRackUi.dragging || !g_fxRackUi.moved || g_fxRackUi.dragParam >= 0) return base;

    const COLORREF bg = RGB(17, 20, 26);
    int r = (int)(GetRValue(bg) + (GetRValue(base) - GetRValue(bg)) * 0.5f);
    int g = (int)(GetGValue(bg) + (GetGValue(base) - GetGValue(bg)) * 0.5f);
    int b = (int)(GetBValue(bg) + (GetBValue(base) - GetBValue(bg)) * 0.5f);
    return RGB(r, g, b);
}

static const char* fx_module_hint(int type) {
    switch (type) {
        case FX_TYPE_TEST:       return "output/gain";
        case FX_TYPE_BUFF:       return "buffer/glitch";
        case FX_TYPE_DELAY:      return "tape/echo";
        case FX_TYPE_REVERB:     return "freeverb/tank";
        case FX_TYPE_LOFI:       return "crush/decimate";
        case FX_TYPE_PHASER:     return "6-stage/sweep";
        case FX_TYPE_CHORUS:     return "doubler/wide";
        case FX_TYPE_COMPRESSOR: return "VCA/glue";
        case FX_TYPE_RESONATOR:  return "ring/tone";
        case FX_TYPE_TREMOLO:    return "pulse/vibe";
        case FX_TYPE_AUTOPAN:    return "stereo/pan";
        case FX_TYPE_FLANGER:    return "jet/flange";
        default:                 return "";
    }
}

typedef struct {
    int  w, h;
    RECT leftPane, chainPane, paramStrip;
    int  rowH;
} FxRackLayout;

static inline void fx_rack_layout(HWND hwnd, FxRackLayout* L) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    L->w = rc.right - rc.left;
    L->h = rc.bottom - rc.top;
    L->rowH = scale_y(26);
    int pad = scale_x(12);
    int headerH = scale_y(30);
    int paramH = scale_y(112);
    int leftW = scale_x(176);
    int gap = scale_x(12);

    // Compute heights based on row count
    int leftH  = 2 + FX_DESCRIPTOR_COUNT * L->rowH + 2;
    int chainH = 2 + FX_MAX_SLOTS        * L->rowH + 2;
    // Align both panes to whichever is taller (flush bottoms)
    int panesH = (leftH > chainH) ? leftH : chainH;

    SetRect(&L->leftPane,   pad, headerH, pad + leftW, headerH + panesH);
    SetRect(&L->chainPane,  L->leftPane.right + gap, headerH, L->w - pad, headerH + panesH);
    SetRect(&L->paramStrip, pad, headerH + panesH + scale_y(10), L->w - pad, headerH + panesH + scale_y(10) + paramH);
}

static inline void fx_rack_clear_btn_rect(const FxRackLayout* L, RECT* r) {
    int bw = scale_x(64), bh = scale_y(20);
    SetRect(r, L->w - scale_x(12) - bw, scale_y(5), L->w - scale_x(12), scale_y(5) + bh);
}

static inline void fx_rack_param_cell(const FxRackLayout* L, int count, int i, RECT* cell) {
    int pad = scale_x(12);
    int usable = (L->paramStrip.right - L->paramStrip.left) - pad * 2;
    int cw = usable / (count > 0 ? count : 1);
    SetRect(cell,
            L->paramStrip.left + pad + i * cw,
            L->paramStrip.top + scale_y(26),
            L->paramStrip.left + pad + (i + 1) * cw,
            L->paramStrip.bottom - scale_y(4));
}

static inline void fx_rack_draw_slider(HDC dc, const RECT* cell, float norm) {
    int railY = cell->top + scale_y(30);
    int railX = cell->left + scale_x(6);
    int railW = (cell->right - cell->left) - scale_x(12);
    if (railW < 20) railW = 20;
    int fillW = (int)(norm * (float)railW);

     
    fx_draw_aa_capsule(dc, (float)railX, (float)railY,
                       (float)(railX + railW), (float)railY, 2.5f, RGB(30, 36, 46));
    if (fillW > 0)
        fx_draw_aa_capsule(dc, (float)railX, (float)railY,
                           (float)(railX + fillW), (float)railY, 2.5f, RGB(80, 210, 240));
    draw_aa_circle(dc, railX + fillW, railY, 5.5f, RGB(100, 245, 210), RGB(255, 255, 255), 1.5f);
}

static inline void fx_rack_draw_knob(HDC dc, const RECT* cell, float norm) {
    int cx = (cell->left + cell->right) / 2;
    int cy = cell->top + scale_y(30);
    float r = (float)scale_y(14);
    // Log knobs get a copper face so their non-linear sweep is obvious.
    fx_draw_aa_knob(dc, cx, cy, r, norm);
}

static inline void fx_rack_draw_toggle(HDC dc, const RECT* cell, bool on,
                                       const char* labelOn, const char* labelOff,
                                       bool large, bool rightAlign) {
    const char* label = on ? (labelOn ? labelOn : "ON") : (labelOff ? labelOff : "OFF");

    
    SIZE sizeOn = { 0 }, sizeOff = { 0 };
    if (labelOn)  GetTextExtentPoint32A(dc, labelOn, (int)strlen(labelOn), &sizeOn);
    if (labelOff) GetTextExtentPoint32A(dc, labelOff, (int)strlen(labelOff), &sizeOff);
    int maxTextW = (sizeOn.cx > sizeOff.cx) ? sizeOn.cx : sizeOff.cx;
    int minTextWidth = maxTextW + scale_x(16);

    int btnWidth = large ? scale_x(68) : scale_x(48);
    if (minTextWidth > btnWidth) btnWidth = minTextWidth;

    int btnHeight = large ? scale_y(24) : scale_y(18);
    int yOffset = large ? scale_y(14) : scale_y(20);

    RECT b;
    if (rightAlign) {
        b.right = cell->right - scale_x(6);
        b.left = b.right - btnWidth;
    } else {
        b.left = cell->left + scale_x(6);
        b.right = b.left + btnWidth;
    }
    b.top = cell->top + yOffset;
    b.bottom = b.top + btnHeight;

    HBRUSH br = CreateSolidBrush(on ? RGB(25, 50, 45) : RGB(30, 32, 38));
    HPEN pen = CreatePen(PS_SOLID, 1, on ? RGB(80, 240, 180) : RGB(55, 60, 72));
    HBRUSH oldBr = (HBRUSH)SelectObject(dc, br);
    HPEN oldPen = (HPEN)SelectObject(dc, pen);
    RoundRect(dc, b.left, b.top, b.right, b.bottom, 3, 3);
    SetTextColor(dc, fx_rack_text_col(on ? RGB(80, 240, 180) : RGB(130, 140, 155)));
    DrawTextA(dc, label, -1, &b, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DeleteObject(SelectObject(dc, oldPen));
    SelectObject(dc, oldBr);
    DeleteObject(br);
}

static LRESULT CALLBACK FxRackWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0) { EndPaint(hwnd, &ps); return 0; }

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
        HFONT oldFont = SELECT_UI_FONT(memDC);
        SetBkMode(memDC, TRANSPARENT);

        HBRUSH bgBr = CreateSolidBrush(RGB(17, 20, 26));
        FillRect(memDC, &rc, bgBr);
        DeleteObject(bgBr);

        FxRackLayout L;
        fx_rack_layout(hwnd, &L);
        FxChain* chain = (g_fxTrack >= 0 && g_fxTrack < MAX_TRACKS) ? &g_TrackFx[g_fxTrack] : NULL;
        int chainCount = chain ? chain->count : 0;

         
        SetTextColor(memDC, fx_rack_text_col(RGB(120, 135, 155)));
        TextOutA(memDC, L.leftPane.left, L.leftPane.top - scale_y(21), "MODULES", 7);
        {
            char hdr[64];
            snprintf(hdr, sizeof(hdr), "FX RACK - TRACK %d", g_fxTrack + 1);
            TextOutA(memDC, L.chainPane.left, L.chainPane.top - scale_y(21), hdr, (int)strlen(hdr));
        }

         
        {
            RECT btn;
            fx_rack_clear_btn_rect(&L, &btn);
            HBRUSH btnBr = CreateSolidBrush(RGB(30, 34, 42));
            HPEN btnPen = CreatePen(PS_SOLID, 1, RGB(70, 80, 96));
            HGDIOBJ oldBr = SelectObject(memDC, btnBr);
            HGDIOBJ oldPn = SelectObject(memDC, btnPen);
            RoundRect(memDC, btn.left, btn.top, btn.right, btn.bottom, 4, 4);
            SelectObject(memDC, oldPn);
            SelectObject(memDC, oldBr);
            DeleteObject(btnBr);
            DeleteObject(btnPen);
            HFONT oldSmallF = (HFONT)SelectObject(memDC, fx_rack_clear_font());
            SetTextColor(memDC, RGB(220, 120, 120));
            DrawTextA(memDC, "Clear All", -1, &btn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(memDC, oldSmallF);
        }

         
        HBRUSH paneBr = CreateSolidBrush(RGB(13, 16, 21));
        HPEN panePen = CreatePen(PS_SOLID, 1, RGB(40, 48, 60));
        HGDIOBJ oldPanePn = SelectObject(memDC, panePen);
        HGDIOBJ oldPaneBr = SelectObject(memDC, paneBr);
        Rectangle(memDC, L.leftPane.left, L.leftPane.top, L.leftPane.right, L.leftPane.bottom);
        Rectangle(memDC, L.chainPane.left, L.chainPane.top, L.chainPane.right, L.chainPane.bottom);
        SelectObject(memDC, oldPanePn);
        SelectObject(memDC, oldPaneBr);
        DeleteObject(panePen);
        DeleteObject(paneBr);

         
        for (int i = 0; i < FX_DESCRIPTOR_COUNT; ++i) {
            const FxDescriptor* d = &kFxDescriptors[i];
            RECT rowRc = { L.leftPane.left + 2, L.leftPane.top + 2 + i * L.rowH,
                           L.leftPane.right - 2, L.leftPane.top + 2 + (i + 1) * L.rowH };
            bool dragSrc = (g_fxRackUi.dragging && g_fxRackUi.moved &&
                            !g_fxRackUi.fromChain && g_fxRackUi.index == i);
            HBRUSH rowBr = CreateSolidBrush(dragSrc ? RGB(26, 40, 50) : RGB(19, 24, 31));
            FillRect(memDC, &rowRc, rowBr);
            DeleteObject(rowBr);
            SetTextColor(memDC, fx_rack_text_col(RGB(170, 190, 210)));
            TextOutA(memDC, rowRc.left + scale_x(8), rowRc.top + scale_y(3), d->name, (int)strlen(d->name));
            const char* hint = fx_module_hint(d->type);
            HFONT oldHintFont = (HFONT)SelectObject(memDC, fx_rack_small_font());
            SIZE tsz = { 0, 0 };
            GetTextExtentPoint32A(memDC, hint, (int)strlen(hint), &tsz);
            SetTextColor(memDC, fx_rack_text_col(RGB(95, 108, 126)));
            TextOutA(memDC, rowRc.right - tsz.cx - scale_x(8), rowRc.top + scale_y(5), hint, (int)strlen(hint));
            SelectObject(memDC, oldHintFont);
        }

         
        for (int i = 0; i < FX_MAX_SLOTS; ++i) {
            RECT rowRc = { L.chainPane.left + 2, L.chainPane.top + 2 + i * L.rowH,
                           L.chainPane.right - 2, L.chainPane.top + 2 + (i + 1) * L.rowH };
            if (rowRc.top >= L.chainPane.bottom) break;
            bool filled = (i < chainCount);
            bool selected = (filled && i == g_fxRackUi.selectedSlot);
            HBRUSH rowBr = CreateSolidBrush(selected ? RGB(26, 44, 54)
                                            : filled  ? RGB(24, 30, 40)
                                                      : RGB(15, 18, 24));
            FillRect(memDC, &rowRc, rowBr);
            DeleteObject(rowBr);
            HPEN rowPen = CreatePen(PS_SOLID, 1, selected ? RGB(80, 210, 240) : RGB(36, 44, 56));
            HPEN oldPen = (HPEN)SelectObject(memDC, rowPen);
            HBRUSH nullBr = (HBRUSH)GetStockObject(NULL_BRUSH);
            HBRUSH oldBr = (HBRUSH)SelectObject(memDC, nullBr);
            Rectangle(memDC, rowRc.left, rowRc.top, rowRc.right, rowRc.bottom);
            SelectObject(memDC, oldBr);
            DeleteObject(SelectObject(memDC, oldPen));
            if (filled) {
                char rowTxt[96];
                const char* nm = chain->slots[i].desc ? chain->slots[i].desc->name : "???";
                int type = chain->slots[i].desc ? chain->slots[i].desc->type : FX_TYPE_NONE;
                int typeCount = 0;
                for (int k = 0; k < chain->count; ++k) {
                    if (chain->slots[k].desc && chain->slots[k].desc->type == type) typeCount++;
                }
                if (typeCount > 1) {
                    int thisNum = 0;
                    for (int k = 0; k <= i && k < chain->count; ++k) {
                        if (chain->slots[k].desc && chain->slots[k].desc->type == type) thisNum++;
                    }
                    snprintf(rowTxt, sizeof(rowTxt), "%d.  %s %d", i + 1, nm, thisNum);
                } else {
                    snprintf(rowTxt, sizeof(rowTxt), "%d.  %s", i + 1, nm);
                }
                SetTextColor(memDC, fx_rack_text_col(selected ? RGB(120, 235, 255) : RGB(180, 195, 215)));
                TextOutA(memDC, rowRc.left + scale_x(8), rowRc.top + scale_y(3), rowTxt, (int)strlen(rowTxt));
            }
        }

         
        if (g_fxRackUi.dragging && g_fxRackUi.moved && g_fxRackUi.dropSlot >= 0) {
            int iy = L.chainPane.top + 2 + g_fxRackUi.dropSlot * L.rowH;
            if (iy >= L.chainPane.top && iy <= L.chainPane.bottom) {
                HPEN dPen = CreatePen(PS_SOLID, 2, RGB(80, 240, 180));
                HPEN oldPen = (HPEN)SelectObject(memDC, dPen);
                MoveToEx(memDC, L.chainPane.left + 4, iy, NULL);
                LineTo(memDC, L.chainPane.right - 4, iy);
                DeleteObject(SelectObject(memDC, oldPen));
            }
        }

         
        HBRUSH stripBr = CreateSolidBrush(RGB(13, 16, 21));
        HPEN stripPen = CreatePen(PS_SOLID, 1, RGB(40, 48, 60));
        HGDIOBJ oldStripPn = SelectObject(memDC, stripPen);
        HGDIOBJ oldStripBr = SelectObject(memDC, stripBr);
        Rectangle(memDC, L.paramStrip.left, L.paramStrip.top, L.paramStrip.right, L.paramStrip.bottom);
        SelectObject(memDC, oldStripPn);
        SelectObject(memDC, oldStripBr);
        DeleteObject(stripPen);
        DeleteObject(stripBr);

        const FxDescriptor* sd = NULL;
        FxInstance* sfx = NULL;
        if (chain && g_fxRackUi.selectedSlot >= 0 && g_fxRackUi.selectedSlot < chain->count &&
            chain->slots[g_fxRackUi.selectedSlot].desc) {
            sd = chain->slots[g_fxRackUi.selectedSlot].desc;
            sfx = &chain->slots[g_fxRackUi.selectedSlot];
        }
        if (!sd) {
            SetTextColor(memDC, fx_rack_text_col(RGB(95, 108, 126)));
            const char* hint = "Select an FX slot above to edit its parameters.";
            TextOutA(memDC, L.paramStrip.left + scale_x(12), L.paramStrip.top + scale_y(10),
                     hint, (int)strlen(hint));
        } else {
            for (int p = 0; p < sd->paramCount && p < FX_MAX_PARAMS; ++p) {
                RECT cell;
                fx_rack_param_cell(&L, sd->paramCount, p, &cell);
                const FxParamDef* pd = &sd->params[p];
                SetTextColor(memDC, fx_rack_text_col(RGB(140, 155, 175)));
                TextOutA(memDC, cell.left, cell.top - scale_y(20), pd->name, (int)strlen(pd->name));
                
                float norm = (pd->max > pd->min)
                             ? (sfx->params[p] - pd->min) / (pd->max - pd->min) : 0.0f;
                if (norm < 0.0f) norm = 0.0f;
                if (norm > 1.0f) norm = 1.0f;
                if (pd->kind == FX_PARAM_KNOB) {
                    fx_rack_draw_knob(memDC, &cell, norm);

                } else if (pd->kind == FX_PARAM_TOGGLE) {
                    bool isMode = (sd->type == FX_TYPE_DELAY && p == 4);   
                    const char* labelOn  = isMode ? "Ping-Pong" : NULL;
                    const char* labelOff = isMode ? "Stereo"    : NULL;

                    fx_rack_draw_toggle(memDC, &cell,
                                        sfx->params[p] > 0.5f,
                                        labelOn, labelOff,
                                        isMode,   
                                        false);   
                } else {

                    fx_rack_draw_slider(memDC, &cell, norm);
                }
                char vbuf[32];
                snprintf(vbuf, sizeof(vbuf), pd->fmt, sfx->params[p]);
                SetTextColor(memDC, fx_rack_text_col(RGB(180, 195, 215)));
                TextOutA(memDC, cell.left, cell.bottom - scale_y(18), vbuf, (int)strlen(vbuf));
            }
        }

         
        if (g_fxRackUi.dragging && g_fxRackUi.moved && g_fxRackUi.dragParam < 0) {
            if (g_fxRackUi.overRemove) {
                HBRUSH remBr = CreateSolidBrush(RGB(58, 20, 24));
                HPEN remPen = CreatePen(PS_SOLID, 2, RGB(225, 90, 90));
                HPEN oldPen = (HPEN)SelectObject(memDC, remPen);
                HBRUSH oldBr = (HBRUSH)SelectObject(memDC, remBr);
                Rectangle(memDC, L.leftPane.left, L.leftPane.top, L.leftPane.right, L.leftPane.bottom);
                DeleteObject(SelectObject(memDC, oldPen));
                SelectObject(memDC, oldBr);
                DeleteObject(remBr);
                SetTextColor(memDC, RGB(240, 130, 130));
                const char* remTxt = "DROP TO REMOVE";
                SIZE tsz = { 0, 0 };
                GetTextExtentPoint32A(memDC, remTxt, (int)strlen(remTxt), &tsz);
                TextOutA(memDC,
                         (L.leftPane.left + L.leftPane.right - tsz.cx) / 2,
                         L.leftPane.bottom - scale_y(24), remTxt, (int)strlen(remTxt));
            } else if (g_fxRackUi.dropSlot >= 0) {
                 
                RECT dropRc = { L.chainPane.left + 2, L.chainPane.top + 2 + g_fxRackUi.dropSlot * L.rowH,
                                L.chainPane.right - 2, L.chainPane.top + 2 + (g_fxRackUi.dropSlot + 1) * L.rowH };
                if (dropRc.bottom > L.chainPane.bottom) dropRc.bottom = L.chainPane.bottom - 1;
                HBRUSH dropBr = CreateSolidBrush(RGB(26, 50, 46));
                HPEN dropPen = CreatePen(PS_SOLID, 2, RGB(80, 240, 180));
                HPEN oldPen = (HPEN)SelectObject(memDC, dropPen);
                HBRUSH oldBr = (HBRUSH)SelectObject(memDC, dropBr);
                Rectangle(memDC, dropRc.left, dropRc.top, dropRc.right, dropRc.bottom);
                DeleteObject(SelectObject(memDC, oldPen));
                SelectObject(memDC, oldBr);
                DeleteObject(dropBr);   // dropBr was never freed -> GDI leak on drag
                 
                MoveToEx(memDC, dropRc.left + 3, dropRc.top, NULL);
                LineTo(memDC, dropRc.right - 3, dropRc.top);
            }
        }

        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldFont);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);

        FxRackLayout L;
        fx_rack_layout(hwnd, &L);
        FxChain* chain = (g_fxTrack >= 0 && g_fxTrack < MAX_TRACKS) ? &g_TrackFx[g_fxTrack] : NULL;
        if (!chain) return 0;

        if (g_fxRackUi.selectedSlot >= 0 && g_fxRackUi.selectedSlot < chain->count) {
            const FxDescriptor* sd = chain->slots[g_fxRackUi.selectedSlot].desc;
            FxInstance* sfx = &chain->slots[g_fxRackUi.selectedSlot];

            if (sd && pt.y >= L.paramStrip.top && pt.y <= L.paramStrip.bottom) {
                for (int p = 0; p < sd->paramCount && p < FX_MAX_PARAMS; ++p) {
                    RECT cell;
                    fx_rack_param_cell(&L, sd->paramCount, p, &cell);
                    if (pt.x >= cell.left && pt.x <= cell.right) {
                        const FxParamDef* pd = &sd->params[p];
                        if (pd->kind == FX_PARAM_TOGGLE) {
                            push_undo_state();
                            seq_lock();
                            sfx->params[p] = (sfx->params[p] > 0.5f) ? 0.0f : 1.0f;
                            seq_unlock();
                        } else {
                            push_undo_state();
                            float stepVal;
                            if (pd->kind == FX_PARAM_KNOB_LOG) {
                                // Step in normalized log space so wheel sweeps
                                // sound even across the frequency decades.
                                float norm = fx_param_norm_log(pd, sfx->params[p]);
                                float notches = (float)zDelta / (float)WHEEL_DELTA;
                                norm += notches * 0.05f;
                                stepVal = fx_param_from_norm_log(pd, norm) - sfx->params[p];
                            } else {
                                float range = pd->max - pd->min;
                                float notches = (float)zDelta / (float)WHEEL_DELTA;
                                stepVal = range * 0.05f * notches;
                            }
                            float v = sfx->params[p] + stepVal;
                            if (v < pd->min) v = pd->min;
                            if (v > pd->max) v = pd->max;

                            seq_lock();
                            sfx->params[p] = v;
                            seq_unlock();
                        }
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                }
            }
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        FxRackLayout L;
        fx_rack_layout(hwnd, &L);
        FxChain* chain = (g_fxTrack >= 0 && g_fxTrack < MAX_TRACKS) ? &g_TrackFx[g_fxTrack] : NULL;
        if (!chain) return 0;

         
        {
            RECT btn;
            fx_rack_clear_btn_rect(&L, &btn);
            if (mx >= btn.left && mx <= btn.right && my >= btn.top && my <= btn.bottom) {
                if (chain->count > 0) {
                    push_undo_state();
                    seq_lock();
                    fx_chain_clear(chain);
                    seq_unlock();
                    g_fxRackUi.selectedSlot = -1;
                    fx_rack_notify_main();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            }
        }

         
        const FxDescriptor* sd = NULL;
        if (g_fxRackUi.selectedSlot >= 0 && g_fxRackUi.selectedSlot < chain->count &&
            chain->slots[g_fxRackUi.selectedSlot].desc)
            sd = chain->slots[g_fxRackUi.selectedSlot].desc;
        if (sd && my >= L.paramStrip.top && my <= L.paramStrip.bottom) {
            for (int p = 0; p < sd->paramCount && p < FX_MAX_PARAMS; ++p) {
                RECT cell;
                fx_rack_param_cell(&L, sd->paramCount, p, &cell);
                if (mx < cell.left || mx > cell.right) continue;
                const FxParamDef* pd = &sd->params[p];
                FxInstance* sfx = &chain->slots[g_fxRackUi.selectedSlot];
                if (pd->kind == FX_PARAM_KNOB || pd->kind == FX_PARAM_KNOB_LOG) {
                    if (my >= cell.top && my <= cell.bottom) {
                        g_fxRackUi.dragging = true;
                        g_fxRackUi.dragParam = p;
                        g_fxRackUi.paramIsKnob = true;
                        g_fxRackUi.knobStartX = mx;
                        g_fxRackUi.knobStartY = my;
                        // Log knobs drag in normalized log space so the sweep
                        // feels even across decades; linear knobs drag in raw
                        // units. knobStartVal carries whichever the param uses.
                        g_fxRackUi.knobStartVal =
                            (pd->kind == FX_PARAM_KNOB_LOG)
                            ? fx_param_norm_log(pd, sfx->params[p])
                            : sfx->params[p];
                        g_fxRackUi.knobIsLog = (pd->kind == FX_PARAM_KNOB_LOG);
                        g_fxRackUi.moved = false;
                        SetCapture(hwnd);
                    }
                    return 0;
                }
                if (pd->kind == FX_PARAM_TOGGLE) {
                    if (my >= cell.top && my <= cell.bottom) {
                        push_undo_state();
                        seq_lock();
                        sfx->params[p] = (sfx->params[p] > 0.5f) ? 0.0f : 1.0f;
                        seq_unlock();
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    return 0;
                }
                 
                if (my >= cell.top && my <= cell.bottom) {
                    int railX = cell.left + scale_x(6);
                    int railW = (cell.right - cell.left) - scale_x(12);
                    if (railW < 20) railW = 20;
                    float norm = (float)(mx - railX) / (float)railW;
                    if (norm < 0.0f) norm = 0.0f;
                    if (norm > 1.0f) norm = 1.0f;
                    push_undo_state();
                    seq_lock();
                    sfx->params[p] = pd->min + norm * (pd->max - pd->min);
                    seq_unlock();
                    g_fxRackUi.dragging = true;
                    g_fxRackUi.dragParam = p;
                    g_fxRackUi.paramIsKnob = false;
                    g_fxRackUi.moved = false;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            }
        }

         
        if (mx >= L.leftPane.left && mx <= L.leftPane.right && my >= L.leftPane.top && my <= L.leftPane.bottom) {
            int row = (my - L.leftPane.top - 2) / L.rowH;
            if (row >= 0 && row < FX_DESCRIPTOR_COUNT) {
                g_fxRackUi.dragging = true;
                g_fxRackUi.fromChain = false;
                g_fxRackUi.index = row;
                g_fxRackUi.startX = mx;
                g_fxRackUi.startY = my;
                g_fxRackUi.dragX = mx;
                g_fxRackUi.dragY = my;
                g_fxRackUi.moved = false;
                g_fxRackUi.overRemove = false;
                g_fxRackUi.dropSlot = -1;
                SetCapture(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

         
        if (mx >= L.chainPane.left && mx <= L.chainPane.right && my >= L.chainPane.top && my <= L.chainPane.bottom) {
            int row = (my - L.chainPane.top - 2) / L.rowH;
            if (row >= 0 && row < chain->count) {
                g_fxRackUi.selectedSlot = row;
                g_fxRackUi.dragging = true;
                g_fxRackUi.fromChain = true;
                g_fxRackUi.index = row;
                g_fxRackUi.startX = mx;
                g_fxRackUi.startY = my;
                g_fxRackUi.dragX = mx;
                g_fxRackUi.dragY = my;
                g_fxRackUi.moved = false;
                g_fxRackUi.overRemove = false;
                g_fxRackUi.dropSlot = -1;
                SetCapture(hwnd);
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        FxRackLayout L;
        fx_rack_layout(hwnd, &L);
        FxChain* chain = (g_fxTrack >= 0 && g_fxTrack < MAX_TRACKS) ? &g_TrackFx[g_fxTrack] : NULL;
        if (!chain) return 0;
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);

         
        if (g_fxRackUi.dragging && g_fxRackUi.dragParam >= 0 &&
            g_fxRackUi.selectedSlot >= 0 && g_fxRackUi.selectedSlot < chain->count) {
            const FxDescriptor* sd = chain->slots[g_fxRackUi.selectedSlot].desc;
            if (sd && g_fxRackUi.dragParam < sd->paramCount) {
                const FxParamDef* pd = &sd->params[g_fxRackUi.dragParam];
                FxInstance* sfx = &chain->slots[g_fxRackUi.selectedSlot];
                float v = sfx->params[g_fxRackUi.dragParam];
                if (g_fxRackUi.paramIsKnob) {
                    if (!g_fxRackUi.moved) {
                        push_undo_state();
                        g_fxRackUi.moved = true;
                    }
                    int delta = (g_fxRackUi.knobStartY - my) + (mx - g_fxRackUi.knobStartX);
                    if (g_fxRackUi.knobIsLog) {
                        // knobStartVal holds the normalized log position.
                        float norm = g_fxRackUi.knobStartVal
                                   + (float)delta * (1.0f / 200.0f);
                        v = fx_param_from_norm_log(pd, norm);
                    } else {
                        v = g_fxRackUi.knobStartVal + (float)delta * (pd->max - pd->min) / 200.0f;
                    }
                } else {
                    RECT cell;
                    fx_rack_param_cell(&L, sd->paramCount, g_fxRackUi.dragParam, &cell);
                    int railX = cell.left + scale_x(6);
                    int railW = (cell.right - cell.left) - scale_x(12);
                    if (railW < 20) railW = 20;
                    float norm = (float)(mx - railX) / (float)railW;
                    if (norm < 0.0f) norm = 0.0f;
                    if (norm > 1.0f) norm = 1.0f;
                    v = pd->min + norm * (pd->max - pd->min);
                }
                if (v < pd->min) v = pd->min;
                if (v > pd->max) v = pd->max;
                seq_lock();
                sfx->params[g_fxRackUi.dragParam] = v;
                seq_unlock();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

         
        if (g_fxRackUi.dragging) {
            g_fxRackUi.dragX = mx;
            g_fxRackUi.dragY = my;
            if (!g_fxRackUi.moved) {
                int dx = mx - g_fxRackUi.startX, dy = my - g_fxRackUi.startY;
                int thr = get_drag_threshold();
                if (dx * dx + dy * dy > thr * thr) {
                    g_fxRackUi.moved = true;
                }
            }
            g_fxRackUi.overRemove = (g_fxRackUi.moved && g_fxRackUi.fromChain &&
                                     mx >= L.leftPane.left && mx <= L.leftPane.right &&
                                     my >= L.leftPane.top && my <= L.leftPane.bottom);
            g_fxRackUi.dropSlot = -1;
            if (g_fxRackUi.moved && !g_fxRackUi.overRemove &&
                mx >= L.chainPane.left && mx <= L.chainPane.right &&
                my >= L.chainPane.top && my <= L.chainPane.bottom) {
                bool full = (!g_fxRackUi.fromChain && chain->count >= FX_MAX_SLOTS);
                if (!full) {
                    int slot = (my - L.chainPane.top - 2 + L.rowH / 2) / L.rowH;
                    if (slot < 0) slot = 0;
                    if (slot > chain->count) slot = chain->count;
                    g_fxRackUi.dropSlot = slot;
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        FxRackLayout L;
        fx_rack_layout(hwnd, &L);
        FxChain* chain = (g_fxTrack >= 0 && g_fxTrack < MAX_TRACKS) ? &g_TrackFx[g_fxTrack] : NULL;

        if (g_fxRackUi.dragging && g_fxRackUi.dragParam >= 0) {
            g_fxRackUi.dragging = false;
            g_fxRackUi.dragParam = -1;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        if (g_fxRackUi.dragging && chain) {
            bool wasMoved = g_fxRackUi.moved;
            bool fromChain = g_fxRackUi.fromChain;
            int src = g_fxRackUi.index;
            int slot = g_fxRackUi.dropSlot;
            g_fxRackUi.dragging = false;
            g_fxRackUi.dropSlot = -1;
            g_fxRackUi.overRemove = false;
            ReleaseCapture();

            if (wasMoved) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                bool overLeft = (pt.x >= L.leftPane.left && pt.x <= L.leftPane.right &&
                                 pt.y >= L.leftPane.top && pt.y <= L.leftPane.bottom);
                bool overChain = (pt.x >= L.chainPane.left && pt.x <= L.chainPane.right &&
                                  pt.y >= L.chainPane.top && pt.y <= L.chainPane.bottom);
                if (fromChain && overLeft && src >= 0 && src < chain->count) {
                     
                    push_undo_state();
                    seq_lock();
                    fx_chain_remove_at(chain, src);
                    seq_unlock();
                    if (g_fxRackUi.selectedSlot == src) g_fxRackUi.selectedSlot = -1;
                    else if (g_fxRackUi.selectedSlot > src) g_fxRackUi.selectedSlot--;
                    fx_rack_notify_main();
                } else if (overChain && slot >= 0) {
                    if (!fromChain) {
                        const FxDescriptor* d = fx_descriptor_by_index(src);
                        if (d && chain->count < FX_MAX_SLOTS) {
                            push_undo_state();
                            seq_lock();
                            if (fx_chain_insert(chain, d, slot, (int)SAMPLE_RATE)) {
                                g_fxRackUi.selectedSlot = slot;
                                fx_rack_notify_main();
                            }
                            seq_unlock();
                        }
                    } else {
                        int to = (slot > src) ? slot - 1 : slot;
                        if (to < 0) to = 0;
                        if (to > chain->count - 1) to = chain->count - 1;
                        if (to != src) {
                            push_undo_state();
                            seq_lock();
                            fx_chain_move(chain, src, to);
                            seq_unlock();
                            g_fxRackUi.selectedSlot = to;
                        }
                    }
                }
            } else if (!fromChain) {
                 
                const FxDescriptor* d = fx_descriptor_by_index(src);
                if (d && chain->count < FX_MAX_SLOTS) {
                    push_undo_state();
                    seq_lock();
                    if (fx_chain_insert(chain, d, chain->count, (int)SAMPLE_RATE)) {
                        g_fxRackUi.selectedSlot = chain->count - 1;
                        fx_rack_notify_main();
                    }
                    seq_unlock();
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        if (g_fxRackUi.dragging) {
            g_fxRackUi.dragging = false;
            g_fxRackUi.dropSlot = -1;
            g_fxRackUi.overRemove = false;
            ReleaseCapture();
        }
        return 0;
    }

    case WM_RBUTTONDOWN: {
        FxRackLayout L;
        fx_rack_layout(hwnd, &L);
        FxChain* chain = (g_fxTrack >= 0 && g_fxTrack < MAX_TRACKS) ? &g_TrackFx[g_fxTrack] : NULL;
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        if (chain && mx >= L.chainPane.left && mx <= L.chainPane.right &&
            my >= L.chainPane.top && my <= L.chainPane.bottom) {
            int row = (my - L.chainPane.top - 2) / L.rowH;
            if (row >= 0 && row < chain->count) {
                push_undo_state();
                seq_lock();
                fx_chain_remove_at(chain, row);
                seq_unlock();
                if (g_fxRackUi.selectedSlot == row) g_fxRackUi.selectedSlot = -1;
                else if (g_fxRackUi.selectedSlot > row) g_fxRackUi.selectedSlot--;
                fx_rack_notify_main();
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        g_fxRackUi.dragging = false;
        g_fxRackUi.dropSlot = -1;
        g_fxRackUi.dragParam = -1;
        g_fxRackUi.overRemove = false;
        return 0;
    case WM_KEYDOWN:
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'Z') {
            if (GetKeyState(VK_SHIFT) & 0x8000) redo_last_action();
            else undo_last_action();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'Y') {
            redo_last_action();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        if (wParam == VK_DELETE || wParam == VK_BACK) {
             
            FxChain* chain = (g_fxTrack >= 0 && g_fxTrack < MAX_TRACKS) ? &g_TrackFx[g_fxTrack] : NULL;
            if (chain && g_fxRackUi.selectedSlot >= 0 && g_fxRackUi.selectedSlot < chain->count) {
                int row = g_fxRackUi.selectedSlot;
                push_undo_state();
                seq_lock();
                fx_chain_remove_at(chain, row);
                seq_unlock();
                g_fxRackUi.selectedSlot = -1;
                fx_rack_notify_main();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        break;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        g_fxRackHwnd = NULL;
        if (g_fxRackSmallFont) {
            DeleteObject(g_fxRackSmallFont);
            g_fxRackSmallFont = NULL;
        }
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static inline void open_fx_rack_dialog(HWND parentHwnd, int trackIdx) {
    if (trackIdx < 0 || trackIdx >= g_Seq.trackCount || trackIdx >= MAX_TRACKS) return;
    g_fxTrack = trackIdx;
    memset(&g_fxRackUi, 0, sizeof(g_fxRackUi));
    g_fxRackUi.selectedSlot = -1;
    g_fxRackUi.dragParam = -1;

    if (!g_fxRackHwnd) {
        static bool s_registered = false;
        if (!s_registered) {
            WNDCLASSA wc = { 0 };
            wc.lpfnWndProc   = FxRackWndProc;
            wc.hInstance     = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractFxRackClass";
            wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_registered = true;
        }

        int rw = scale_x(560), rh = scale_y(520);
        int scrW = GetSystemMetrics(SM_CXSCREEN);
        int scrH = GetSystemMetrics(SM_CYSCREEN);

        g_fxRackHwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            "RefractFxRackClass",
            "FX Rack",
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE,
            (scrW - rw) / 2, (scrH - rh) / 2, rw, rh,
            parentHwnd, NULL, GetModuleHandle(NULL), NULL
        );
    }

    char titleBuf[64];
    snprintf(titleBuf, sizeof(titleBuf), "FX Rack - Track %d", trackIdx + 1);
    SetWindowTextA(g_fxRackHwnd, titleBuf);

    ShowWindow(g_fxRackHwnd, SW_SHOW);
    SetForegroundWindow(g_fxRackHwnd);
    InvalidateRect(g_fxRackHwnd, NULL, FALSE);
}

// --- Track Trigger Probability dialog (modeless popup slider) ---------------
// A single supersampled slider (0%..100%) that live-applies the track's global
// trigger probability during playback. Dismissal: click-outside / ESC / ENTER.
typedef struct {
    HWND hwnd;
    int  trackIdx;
    bool isDragging;
} ProbWindowContext;
static ProbWindowContext g_ProbWin = { 0 };

static inline void set_track_trigger_prob_value(float prob) {
    if (prob < 0.0f) prob = 0.0f;
    if (prob > 1.0f) prob = 1.0f;
    if (g_ProbWin.trackIdx < 0 || g_ProbWin.trackIdx >= g_Seq.trackCount) return;
    seq_lock();
    g_Seq.trackTriggerProb[g_ProbWin.trackIdx] = prob;
    seq_unlock();
}

static inline float get_track_trigger_prob_value(void) {
    if (g_ProbWin.trackIdx < 0 || g_ProbWin.trackIdx >= g_Seq.trackCount) return 1.0f;
    return g_Seq.trackTriggerProb[g_ProbWin.trackIdx];
}

static LRESULT CALLBACK ProbWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_ACTIVATE:
        // Click-outside (WA_INACTIVE) cancels the popup: hide it, no changes.
        if (LOWORD(wParam) == WA_INACTIVE) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam);
        RECT rc; GetClientRect(hwnd, &rc);
        int trackLeft = 24, trackRight = rc.right - 24;
        int trackW = trackRight - trackLeft;
        if (mx >= trackLeft && mx <= trackRight && trackW > 0) {
            float norm = (float)(mx - trackLeft) / (float)trackW;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;
            set_track_trigger_prob_value(norm);
            g_ProbWin.isDragging = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (g_ProbWin.isDragging) {
            int mx = GET_X_LPARAM(lParam);
            RECT rc; GetClientRect(hwnd, &rc);
            int trackLeft = 24, trackRight = rc.right - 24;
            int trackW = trackRight - trackLeft;
            if (trackW > 0) {
                float norm = (float)(mx - trackLeft) / (float)trackW;
                if (norm < 0.0f) norm = 0.0f;
                if (norm > 1.0f) norm = 1.0f;
                set_track_trigger_prob_value(norm);
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        float cur = get_track_trigger_prob_value();
        float delta = (zDelta > 0) ? 0.05f : -0.05f;
        set_track_trigger_prob_value(cur + delta);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_ProbWin.isDragging) {
            g_ProbWin.isDragging = false;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_RBUTTONDOWN: {
        set_track_trigger_prob_value(1.0f);   // right-click resets to 100%
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE || wParam == VK_RETURN) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HFONT oldFontMain = SELECT_UI_FONT(hdc);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0) {
            SelectObject(hdc, oldFontMain);
            EndPaint(hwnd, &ps);
            return 0;
        }

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
        HFONT oldFontMem = SELECT_UI_FONT(memDC);
        HGDIOBJ origPen = GetCurrentObject(memDC, OBJ_PEN);
        HGDIOBJ origBrush = GetCurrentObject(memDC, OBJ_BRUSH);

        HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
        FillRect(memDC, &rc, bg);
        DeleteObject(bg);

        int trackLeft = 24, trackRight = w - 24;
        int trackY = 56, trackW = trackRight - trackLeft;

        HPEN railPen = CreatePen(PS_SOLID, 4, RGB(28, 33, 42));
        SelectObject(memDC, railPen);
        MoveToEx(memDC, trackLeft, trackY, NULL);
        LineTo(memDC, trackRight, trackY);
        SelectObject(memDC, origPen);
        DeleteObject(railPen);

        float prob = get_track_trigger_prob_value();
        float norm = prob; if (norm < 0.0f) norm = 0.0f; if (norm > 1.0f) norm = 1.0f;
        int thumbX = trackLeft + (int)(norm * (float)trackW);

        HPEN fillPen = CreatePen(PS_SOLID, 4, RGB(80, 210, 240));
        SelectObject(memDC, fillPen);
        MoveToEx(memDC, trackLeft, trackY, NULL);
        LineTo(memDC, thumbX, trackY);
        SelectObject(memDC, origPen);
        DeleteObject(fillPen);

        draw_aa_circle(memDC, thumbX, trackY, 6.5f, RGB(80, 240, 180), RGB(255, 255, 255), 1.8f);

        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, RGB(215, 225, 240));
        RECT valRc = { 0, 12, w, 34 };
        DrawTextA(memDC, "How often should events trigger?", -1, &valRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SetTextColor(memDC, RGB(80, 95, 115));
        TextOutA(memDC, trackLeft, trackY + 12, "0%", 2);
        TextOutA(memDC, trackRight - 20, trackY + 12, "100%", 4);

        // --- ADDED HINT TEXT (matches pan/width dialog) ---
        SetTextColor(memDC, RGB(140, 155, 175));
        RECT hintRc = { 0, h - 24, w, h - 4 };
        DrawTextA(memDC, "Right-Click slider to reset | [ESC] to close", -1, &hintRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldFontMem);
        SelectObject(memDC, origPen);
        SelectObject(memDC, origBrush);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        SelectObject(hdc, oldFontMain);
        EndPaint(hwnd, &ps);
        return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static inline void open_track_probability_dialog(HWND parentHwnd, int trackIdx, int screenX, int screenY) {
    (void)screenX; (void)screenY;  // suppress unreferenced parameter warnings
    if (trackIdx < 0 || trackIdx >= g_Seq.trackCount) return;
    if (!g_ProbWin.hwnd) {
        static bool s_registered = false;
        if (!s_registered) {
            WNDCLASSA wc = { 0 };
            wc.lpfnWndProc   = ProbWndProc;
            wc.hInstance     = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractProbWindowClass";
            wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_registered = true;
        }
        int rw = 420, rh = 150;
        
        // Center the dialog (like pan/width) instead of using cursor position
        int scrW = GetSystemMetrics(SM_CXSCREEN), scrH = GetSystemMetrics(SM_CYSCREEN);
        int rx = (scrW - rw) / 2;
        int ry = (scrH - rh) / 2;
        
        // Center relative to parent window if available
        if (parentHwnd && IsWindow(parentHwnd) && !IsIconic(parentHwnd)) {
            RECT prc;
            GetWindowRect(parentHwnd, &prc);
            if (prc.right > prc.left && prc.bottom > prc.top) {
                rx = prc.left + ((prc.right - prc.left) - rw) / 2;
                ry = prc.top + ((prc.bottom - prc.top) - rh) / 2;
            }
        }
        
        // Keep it on screen
        if (rx < 0 || rx + rw > scrW) rx = (scrW - rw) / 2;
        if (ry < 0 || ry + rh > scrH) ry = (scrH - rh) / 2;

        g_ProbWin.hwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            "RefractProbWindowClass",
            "Trigger Probability",
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE,
            rx, ry, rw, rh,
            parentHwnd, NULL, GetModuleHandle(NULL), NULL
        );
    }
    g_ProbWin.trackIdx = trackIdx;
    char titleBuf[64];
    snprintf(titleBuf, sizeof(titleBuf), "Track %d - Probability", trackIdx + 1);
    SetWindowTextA(g_ProbWin.hwnd, titleBuf);
    ShowWindow(g_ProbWin.hwnd, SW_SHOW);
    SetForegroundWindow(g_ProbWin.hwnd);
    InvalidateRect(g_ProbWin.hwnd, NULL, FALSE);
}

 
static inline void show_track_context_menu(HWND hwnd, int trackIdx, int screenX, int screenY) {
    if (trackIdx < 0 || trackIdx >= g_Seq.trackCount) return;
    HMENU hMenu = CreatePopupMenu();
    bool isMuted = g_Seq.trackMuted[trackIdx];
    bool isSolo = g_Seq.trackSolo[trackIdx];

    
    if (isMuted) {
        AppendMenuA(hMenu, MF_STRING, ID_TRACK_MUTE, "Mute Track (M)\t[ON]");
    }
    else {
        AppendMenuA(hMenu, MF_STRING, ID_TRACK_MUTE, "Mute Track (M)");
    }

    
    if (isSolo) {
        AppendMenuA(hMenu, MF_STRING, ID_TRACK_SOLO, "Solo Track (Shift+S)\t[ON]");
    }
    else {
        AppendMenuA(hMenu, MF_STRING, ID_TRACK_SOLO, "Solo Track (Shift+S)");
    }
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);

    
    AppendMenuA(hMenu, MF_STRING, ID_TRACK_FX_RACK, "FX Rack...");
    AppendMenuA(hMenu, MF_STRING, ID_TRACK_PAN_WIDTH, "Pan && Width...");
    AppendMenuA(hMenu, MF_STRING, ID_TRACK_EQ, "Parametric EQ...");
    AppendMenuA(hMenu, MF_STRING, ID_TRACK_FILTER, "Filter Plotter...");
    AppendMenuA(hMenu, MF_STRING, ID_TRACK_GRANULAR, "Granular Engine...");
    AppendMenuA(hMenu, MF_STRING, ID_TRACK_TRIGGER_PROB, "Trigger Probability...");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);

    
    HMENU hRateMenu = CreatePopupMenu();
    AppendMenuA(hRateMenu, MF_STRING, ID_TRACK_RATE_CUSTOM, "Custom...");
    AppendMenuA(hRateMenu, MF_SEPARATOR, 0, NULL);
    for (int i = 0; i < RATE_PRESET_COUNT; ++i) {
        UINT tId = ID_TRACK_RATE_050 + (kRatePresets[i].id - ID_RATE_050);
        AppendMenuA(hRateMenu, MF_STRING, tId, kRatePresets[i].txt);
    }
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hRateMenu, "Set Rate of All Clips");

    
    HMENU hBatchFadeMenu = CreatePopupMenu();
    HMENU hBatchFadeInMenu = CreatePopupMenu();
    HMENU hBatchFadeOutMenu = CreatePopupMenu();

    for (int i = 0; i < FADE_PRESET_COUNT; ++i) {
        AppendMenuA(hBatchFadeInMenu, MF_STRING, ID_TRACK_FADE_IN_000 + i, kFadePresets[i].txt);
        AppendMenuA(hBatchFadeOutMenu, MF_STRING, ID_TRACK_FADE_OUT_000 + i, kFadePresets[i].txt);
    }
    AppendMenuA(hBatchFadeMenu, MF_POPUP, (UINT_PTR)hBatchFadeInMenu, "Fade In");
    AppendMenuA(hBatchFadeMenu, MF_POPUP, (UINT_PTR)hBatchFadeOutMenu, "Fade Out");
    AppendMenuA(hBatchFadeMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hBatchFadeMenu, MF_STRING, ID_TRACK_FADE_CLEAR, "Reset Fades");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hBatchFadeMenu, "Set Fades of All Clips");

    
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING, ID_TRACK_RESET_CLIP_VOL, "Reset Volume of All Clips (100%)");
    AppendMenuA(hMenu, MF_STRING, ID_TRACK_CLEAR, "Clear All Clips on Track");

    MENUINFO miNoCheck = { sizeof(MENUINFO), MIM_STYLE, MNS_NOCHECK, 0, 0, 0, 0 };
    SetMenuInfo(hMenu, &miNoCheck);
    SetMenuInfo(hRateMenu, &miNoCheck);
    SetMenuInfo(hBatchFadeMenu, &miNoCheck);
    SetMenuInfo(hBatchFadeInMenu, &miNoCheck);
    SetMenuInfo(hBatchFadeOutMenu, &miNoCheck);

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenX, screenY, 0, hwnd, NULL);
    DestroyMenu(hMenu);
    if (cmd == 0) return;

    if (cmd == ID_TRACK_PAN_WIDTH) {
        open_track_pan_width_dialog(hwnd, trackIdx);
        return;
    }
    if (cmd == ID_TRACK_EQ) {
        open_track_eq_dialog(hwnd, trackIdx);
        return;
    }
    if (cmd == ID_TRACK_FILTER) {
        open_track_filter_dialog(hwnd, trackIdx);
        return;
    }
    if (cmd == ID_TRACK_FX_RACK) {
        open_fx_rack_dialog(hwnd, trackIdx);
        return;
    }
    if (cmd == ID_TRACK_GRANULAR) {
        open_granular_dialog(hwnd, trackIdx);
        return;
    }
    if (cmd == ID_TRACK_TRIGGER_PROB) {
        open_track_probability_dialog(hwnd, trackIdx, screenX, screenY);
        return;
    }
    if (cmd == ID_TRACK_RATE_CUSTOM) {
        open_custom_rate_dialog(hwnd, -1, trackIdx);
        return;
    }
    if (cmd == ID_TRACK_MUTE) {
        seq_lock();
        seq_set_track_mute(trackIdx, !g_Seq.trackMuted[trackIdx]);
        seq_unlock();
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }
    if (cmd == ID_TRACK_SOLO) {
        seq_lock();
        seq_set_track_solo(trackIdx, !g_Seq.trackSolo[trackIdx]);
        seq_unlock();
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }

    push_undo_state();
    seq_lock();

    
    if (cmd == ID_TRACK_RESET_CLIP_VOL) {
        for (int k = 0; k < g_Seq.clipCount; ++k) {
            if (g_Seq.clips[k].track == trackIdx) {
                g_Seq.clips[k].volume = 1.0f;
                mark_clip_bars_dirty(&g_Seq.clips[k]);
            }
        }
    }

    
    for (int i = 0; i < RATE_PRESET_COUNT; ++i) {
        UINT tId = ID_TRACK_RATE_050 + (kRatePresets[i].id - ID_RATE_050);
        if (cmd == (int)tId) {
            for (int k = 0; k < g_Seq.clipCount; ++k) {
                if (g_Seq.clips[k].track == trackIdx) {
                    g_Seq.clips[k].playbackRate = kRatePresets[i].val;
                    mark_clip_bars_dirty(&g_Seq.clips[k]);
                }
            }
            break;
        }
    }

    
    if (cmd >= ID_TRACK_FADE_IN_000 && cmd <= ID_TRACK_FADE_IN_400) {
        int idx = cmd - ID_TRACK_FADE_IN_000;
        if (idx >= 0 && idx < FADE_PRESET_COUNT) {
            for (int k = 0; k < g_Seq.clipCount; ++k) {
                if (g_Seq.clips[k].track == trackIdx) {
                    float v = kFadePresets[idx].val;
                    if (v > g_Seq.clips[k].lengthBeats) v = g_Seq.clips[k].lengthBeats;
                    g_Seq.clips[k].fadeInBeats = v;
                    mark_clip_bars_dirty(&g_Seq.clips[k]);
                }
            }
        }
    }

    
    if (cmd >= ID_TRACK_FADE_OUT_000 && cmd <= ID_TRACK_FADE_OUT_400) {
        int idx = cmd - ID_TRACK_FADE_OUT_000;
        if (idx >= 0 && idx < FADE_PRESET_COUNT) {
            for (int k = 0; k < g_Seq.clipCount; ++k) {
                if (g_Seq.clips[k].track == trackIdx) {
                    float v = kFadePresets[idx].val;
                    if (v > g_Seq.clips[k].lengthBeats) v = g_Seq.clips[k].lengthBeats;
                    g_Seq.clips[k].fadeOutBeats = v;
                    mark_clip_bars_dirty(&g_Seq.clips[k]);
                }
            }
        }
    }

    
    if (cmd == ID_TRACK_FADE_CLEAR) {
        for (int k = 0; k < g_Seq.clipCount; ++k) {
            if (g_Seq.clips[k].track == trackIdx) {
                g_Seq.clips[k].fadeInBeats = 0.0f;
                g_Seq.clips[k].fadeOutBeats = 0.0f;
                mark_clip_bars_dirty(&g_Seq.clips[k]);
            }
        }
    }

    
    if (cmd == ID_TRACK_CLEAR) {
        for (int k = 0; k < g_Seq.clipCount;) {
            if (g_Seq.clips[k].track == trackIdx) {
                if (g_ClipGran[k].ownFrames) {
                    free(g_ClipGran[k].ownFrames);
                    g_ClipGran[k].ownFrames = NULL;
                }
                for (int j = k; j < g_Seq.clipCount - 1; ++j) {
                    g_Seq.clips[j] = g_Seq.clips[j + 1];
                    g_ClipGran[j] = g_ClipGran[j + 1];
                    g_ClipGran[j].clipIdx = j;
                }
                memset(&g_ClipGran[g_Seq.clipCount - 1], 0, sizeof(GranularEngine));
                g_ClipGran[g_Seq.clipCount - 1].clipIdx = g_Seq.clipCount - 1;
                g_ClipGran[g_Seq.clipCount - 1].sampleIndex = -1;
                g_Seq.clipCount--;
            }
            else {
                k++;
            }
        }
    }

    seq_unlock();
    InvalidateRect(hwnd, NULL, FALSE);
}

// --- Master Volume dialog (modeless popup slider) --------------------------
// A single supersampled AA-capsule slider (0%..150%) that live-applies to
// g_Seq.masterVolume (under seq_lock) as you drag, scroll, or reset, so the
// bottom dock's MASTER number indicator updates in real time. Closing the
// popup by any path (ENTER, ESC, click-outside, X) keeps the applied value and
// marks the project modified.
typedef struct {
    HWND   hwnd;
    float  local;        // live value in [0.0, 1.5]
    bool   isDragging;
} MasterVolWindowContext;
static MasterVolWindowContext g_MasterVolWin = { 0 };

#define MASTER_VOL_RANGE_LO 0.0f
#define MASTER_VOL_RANGE_HI 1.5f

static inline void mastervol_track_bounds(HWND hwnd, int* outLeft, int* outRight) {
    RECT rc; GetClientRect(hwnd, &rc);
    *outLeft  = 24;
    *outRight = rc.right - 24;
}

static inline void mastervol_commit(HWND hwnd) {
    float v = g_MasterVolWin.local;
    if (v < MASTER_VOL_RANGE_LO) v = MASTER_VOL_RANGE_LO;
    if (v > MASTER_VOL_RANGE_HI) v = MASTER_VOL_RANGE_HI;
    seq_lock();
    g_Seq.masterVolume = v;
    g_Seq.isModified = true;
    seq_unlock();
    update_window_title();
    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
    DestroyWindow(hwnd);
}

// Live-apply the popup's value to the master volume and refresh the bottom
// dock's MASTER button number indicator so the drag is visible immediately.
// isModified is set on close (see mastervol_commit / mastervol_cancel), not on
// every drag frame.
static inline void mastervol_apply(void) {
    float v = g_MasterVolWin.local;
    if (v < MASTER_VOL_RANGE_LO) v = MASTER_VOL_RANGE_LO;
    if (v > MASTER_VOL_RANGE_HI) v = MASTER_VOL_RANGE_HI;
    seq_lock();
    g_Seq.masterVolume = v;
    seq_unlock();
    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
}

static inline void mastervol_cancel(HWND hwnd) {
    // The value is already live-applied on every interaction, so closing the
    // popup by any path simply keeps it. Mark the project modified since the
    // applied value is a real change.
    seq_lock();
    g_Seq.isModified = true;
    seq_unlock();
    update_window_title();
    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
    DestroyWindow(hwnd);
}

static LRESULT CALLBACK MasterVolWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_ACTIVATE:
        // Click-outside (WA_INACTIVE) cancels the popup: destroy, no write-back.
        if (LOWORD(wParam) == WA_INACTIVE) {
            mastervol_cancel(hwnd);
            return 0;
        }
        break;

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        int trackLeft, trackRight;
        mastervol_track_bounds(hwnd, &trackLeft, &trackRight);
        int trackW = trackRight - trackLeft;
        if (my >= 42 && my <= 72 && mx >= trackLeft && mx <= trackRight && trackW > 0) {
            float norm = (float)(mx - trackLeft) / (float)trackW;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;
            g_MasterVolWin.local = MASTER_VOL_RANGE_LO + norm * (MASTER_VOL_RANGE_HI - MASTER_VOL_RANGE_LO);
            g_MasterVolWin.isDragging = true;
            SetCapture(hwnd);
            mastervol_apply();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (g_MasterVolWin.isDragging) {
            int mx = GET_X_LPARAM(lParam);
            int trackLeft, trackRight;
            mastervol_track_bounds(hwnd, &trackLeft, &trackRight);
            int trackW = trackRight - trackLeft;
            if (trackW > 0) {
                float norm = (float)(mx - trackLeft) / (float)trackW;
                if (norm < 0.0f) norm = 0.0f;
                if (norm > 1.0f) norm = 1.0f;
                g_MasterVolWin.local = MASTER_VOL_RANGE_LO + norm * (MASTER_VOL_RANGE_HI - MASTER_VOL_RANGE_LO);
                mastervol_apply();
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        float step = (zDelta > 0) ? 0.05f : -0.05f;
        g_MasterVolWin.local += step;
        if (g_MasterVolWin.local < MASTER_VOL_RANGE_LO) g_MasterVolWin.local = MASTER_VOL_RANGE_LO;
        if (g_MasterVolWin.local > MASTER_VOL_RANGE_HI) g_MasterVolWin.local = MASTER_VOL_RANGE_HI;
        mastervol_apply();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONUP:
        if (g_MasterVolWin.isDragging) {
            g_MasterVolWin.isDragging = false;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_RBUTTONDOWN: {
        g_MasterVolWin.local = 1.0f;   // right-click resets to 100%
        mastervol_apply();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_RETURN) {
            mastervol_commit(hwnd);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            mastervol_cancel(hwnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        mastervol_cancel(hwnd);
        return 0;

    case WM_DESTROY:
        g_MasterVolWin.hwnd = NULL;
        g_MasterVolWin.isDragging = false;
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HFONT oldFontMain = SELECT_UI_FONT(hdc);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0) {
            SelectObject(hdc, oldFontMain);
            EndPaint(hwnd, &ps);
            return 0;
        }

        // Double-buffered GDI: offscreen bitmap redrawn only when invalidated.
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
        HFONT oldFontMem = SELECT_UI_FONT(memDC);

        HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
        FillRect(memDC, &rc, bg);
        DeleteObject(bg);

        int trackLeft = 24, trackRight = w - 24;
        int trackW = trackRight - trackLeft;
        int trackY = 56;

        // Supersampled AA capsule track + fill (same style as FX rack popups).
        fx_draw_aa_capsule(memDC, (float)trackLeft, (float)trackY,
                           (float)trackRight, (float)trackY, 2.5f, RGB(30, 36, 46));

        float norm = (g_MasterVolWin.local - MASTER_VOL_RANGE_LO) /
                     (MASTER_VOL_RANGE_HI - MASTER_VOL_RANGE_LO);
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        int fillX = trackLeft + (int)(norm * (float)trackW);
        if (fillX > trackLeft)
            fx_draw_aa_capsule(memDC, (float)trackLeft, (float)trackY,
                               (float)fillX, (float)trackY, 2.5f, RGB(80, 210, 240));
        draw_aa_circle(memDC, fillX, trackY, 6.5f, RGB(80, 240, 180), RGB(255, 255, 255), 1.8f);

        SetBkMode(memDC, TRANSPARENT);
        char valTxt[64];
        snprintf(valTxt, sizeof(valTxt), "Master: %d%%",
                 (int)(g_MasterVolWin.local * 100.0f + 0.5f));
        SetTextColor(memDC, RGB(180, 220, 245));
        RECT valRc = { 0, 12, w, 34 };
        DrawTextA(memDC, valTxt, -1, &valRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SetTextColor(memDC, RGB(80, 95, 115));
        TextOutA(memDC, trackLeft, trackY + 12, "0%", 2);
        TextOutA(memDC, trackRight - 24, trackY + 12, "150%", 4);

        SetTextColor(memDC, RGB(140, 155, 175));
        RECT hintRc = { 0, h - 26, w, h - 5 };
        DrawTextA(memDC, "Right-click to reset to 100% | Scroll to fine-tune (5%)", -1,
                  &hintRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldFontMem);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        SelectObject(hdc, oldFontMain);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static inline void open_master_volume_popup(HWND parentHwnd) {
    if (!g_MasterVolWin.hwnd) {
        static bool s_registered = false;
        if (!s_registered) {
            WNDCLASSA wc = { 0 };
            wc.lpfnWndProc   = MasterVolWndProc;
            wc.hInstance     = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractMasterVolClass";
            wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_registered = true;
        }

        int rw = 420, rh = 150;
        int scrW = GetSystemMetrics(SM_CXSCREEN), scrH = GetSystemMetrics(SM_CYSCREEN);
        int rx = (scrW - rw) / 2, ry = (scrH - rh) / 2;
        if (parentHwnd && IsWindow(parentHwnd)) {
            RECT prc;
            GetWindowRect(parentHwnd, &prc);
            rx = prc.left + ((prc.right - prc.left) - rw) / 2;
            ry = prc.top + ((prc.bottom - prc.top) - rh) / 2;
        }
        if (rx < 0 || rx + rw > scrW) rx = (scrW - rw) / 2;
        if (ry < 0 || ry + rh > scrH) ry = (scrH - rh) / 2;

        g_MasterVolWin.hwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            "RefractMasterVolClass",
            "Master Volume",
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE,
            rx, ry, rw, rh,
            parentHwnd, NULL, GetModuleHandle(NULL), NULL
        );
    }

    // Popup owns its preview value; snapshot the committed volume once at open.
    seq_lock();
    g_MasterVolWin.local = g_Seq.masterVolume;
    seq_unlock();
    if (g_MasterVolWin.local < MASTER_VOL_RANGE_LO) g_MasterVolWin.local = MASTER_VOL_RANGE_LO;
    if (g_MasterVolWin.local > MASTER_VOL_RANGE_HI) g_MasterVolWin.local = MASTER_VOL_RANGE_HI;
    g_MasterVolWin.isDragging = false;

    ShowWindow(g_MasterVolWin.hwnd, SW_SHOW);
    SetForegroundWindow(g_MasterVolWin.hwnd);
    InvalidateRect(g_MasterVolWin.hwnd, NULL, FALSE);
}
