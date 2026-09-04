#pragma once
#include "globals.h"
#include "dsp.h"
#include "ui.h"
#include "audio.h"
#include "state.h"
#include "actions.h"
#include "dialogs.h"
#include "project.h"
#include "scrollbar.h"
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

 
static inline void set_seq_status_msg(HWND hwnd, const char* msg) {
    snprintf(g_Seq.exportMsg, sizeof(g_Seq.exportMsg), "%s", msg);
    g_Seq.exportMsgActive = true;
    g_Seq.exportMsgExpiry = GetTickCount64() + 4000;
    if (hwnd) InvalidateRect(hwnd, NULL, FALSE);
}

 

 
static inline bool is_csq_path(const char* filepath) {
    size_t len = strlen(filepath);
    return len > 4 && _stricmp(filepath + len - 4, ".csq") == 0;
}

 
static inline void play_with_start_reset(void) {
    if (!seq_is_playing() && g_Seq.playFromStartOnPlay) {
        
        g_Seq.scrollX = 0;
        update_scrollbar(g_hWnd);
        
        set_playback_frame(0);
        
        granular_stop_all();
        synth_stop_all();
        
        InvalidateRect(g_hWnd, NULL, FALSE);
    }
    toggle_playback();
}

 
static DWORD WINAPI main_ui_pacer_thread_proc(LPVOID lpParam) {
    (void)lpParam;
    timeBeginPeriod(1);    

    LARGE_INTEGER freq, lastTime, curTime;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&lastTime);

    const double targetTicks = (double)freq.QuadPart / TARGET_MAIN_FPS;

    while (InterlockedCompareExchange(&g_mainPacerRunning, 1, 1) == 1) {
        QueryPerformanceCounter(&curTime);
        double elapsed = (double)(curTime.QuadPart - lastTime.QuadPart);

        if (elapsed >= targetTicks) {
            lastTime.QuadPart += (LONGLONG)targetTicks;

            bool interacting = g_Seq.isDraggingClip || g_Seq.isVolumeDragging ||
                               g_Seq.isSlipDragging || g_Seq.isMarqueeSelecting ||
                               g_Seq.isResizingLeft || g_Seq.isResizingRight ||
                               g_Seq.isFadeInDragging || g_Seq.isFadeOutDragging ||
                               g_Seq.isMiddlePanning || g_Seq.isTrackHeaderDragging;

            ULONGLONG now = GetTickCount64();
            bool transientActive =
                (g_Seq.volumePopupClip >= 0 && now < g_Seq.volumePopupExpiry + 64) ||
                g_Seq.exportMsgActive ||
                g_Seq.isBusy;

             
            bool visOpen = (g_visHwnd && IsWindow(g_visHwnd) && IsWindowVisible(g_visHwnd));

            bool needsRedraw = seq_is_playing() || interacting || transientActive || visOpen ||
                               g_visBadgeHover ||
                               (InterlockedExchange(&g_timelineDynamicDirty, 0) == 1) ||
                               g_timelineDirty;

            if (needsRedraw && g_hWnd && IsWindow(g_hWnd) && !IsIconic(g_hWnd)) {
                InvalidateRect(g_hWnd, NULL, FALSE);
            }
        }

         
        Sleep(1);
    }

    timeEndPeriod(1);
    return 0;
}

static inline void start_main_120fps_pacer(void) {
    if (InterlockedCompareExchange(&g_mainPacerRunning, 1, 0) == 0) {
        g_hMainPacerThread = CreateThread(NULL, 0, main_ui_pacer_thread_proc, NULL, 0, NULL);
    }
}

static inline void stop_main_120fps_pacer(void) {
    if (InterlockedExchange(&g_mainPacerRunning, 0) == 1) {
        if (g_hMainPacerThread) {
            WaitForSingleObject(g_hMainPacerThread, 200);
            CloseHandle(g_hMainPacerThread);
            g_hMainPacerThread = NULL;
        }
    }
}

 
typedef enum {
    CLIP_HIT_NONE = 0,
    CLIP_HIT_BODY,
    CLIP_HIT_TRIM_LEFT,
    CLIP_HIT_TRIM_RIGHT,
    CLIP_HIT_FADE_IN_HANDLE,
    CLIP_HIT_FADE_OUT_HANDLE
} ClipHitZone;

static inline ClipHitZone hit_test_clip_zones(int mx, int my, const Clip* c, int cX1, int cX2, int cY1, int cY2, float ppb) {
    (void)cY2;
    if (c->isMidi) {
        int trimW = scale_x(6);
        if (abs(mx - cX1) <= trimW) return CLIP_HIT_TRIM_LEFT;
        if (abs(mx - cX2) <= trimW) return CLIP_HIT_TRIM_RIGHT;
        return CLIP_HIT_BODY;
    }

    int headerZoneH = scale_y(22);
    int inApexX = cX1 + (int)(c->fadeInBeats * ppb);
    int outApexX = cX2 - (int)(c->fadeOutBeats * ppb);
    int apexY = cY1 + scale_y(3);

     
    bool hitFadeIn = (c->fadeInBeats > 0.001f)
        ? (abs(mx - inApexX) <= scale_x(9) && abs(my - apexY) <= scale_y(12))
        : (mx >= cX1 && mx <= cX1 + scale_x(18) && my >= cY1 && my <= cY1 + headerZoneH);
    if (hitFadeIn) return CLIP_HIT_FADE_IN_HANDLE;

    bool hitFadeOut = (c->fadeOutBeats > 0.001f)
        ? (abs(mx - outApexX) <= scale_x(9) && abs(my - apexY) <= scale_y(12))
        : (mx <= cX2 && mx >= cX2 - scale_x(18) && my >= cY1 && my <= cY1 + headerZoneH);
    if (hitFadeOut) return CLIP_HIT_FADE_OUT_HANDLE;

     
    int trimW = scale_x(6);
    if (abs(mx - cX1) <= trimW && my > cY1 + scale_y(10)) return CLIP_HIT_TRIM_LEFT;
    if (abs(mx - cX2) <= trimW && my > cY1 + scale_y(10)) return CLIP_HIT_TRIM_RIGHT;

    return CLIP_HIT_BODY;
}

 
static inline void zoom_to_cursor(HWND hwnd, float newZoom) {
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int viewportLeft = get_track_header_width();
    int viewportWidth = rc.right - rc.left - viewportLeft;
    int headerH = get_header_height();
    int bottomH = get_bottom_dock_height();

    bool inTimeline = (pt.x >= viewportLeft && pt.x < rc.right &&
                       pt.y >= headerH && pt.y < rc.bottom - bottomH);

    float oldPpb = get_pixels_per_beat();   
    float cursorBeat = 0.0f;
    if (inTimeline && viewportWidth > 0) {
        cursorBeat = (float)(pt.x - viewportLeft + g_Seq.scrollX) / oldPpb;
    }

    g_Seq.zoom = newZoom;
    float newPpb = get_pixels_per_beat();

    if (inTimeline && viewportWidth > 0) {
        int newScrollX = (int)(cursorBeat * newPpb - (pt.x - viewportLeft) + 0.5f);
        int totalTimelineWidth = (int)(total_beats() * newPpb);
        int maxScrollX = totalTimelineWidth - viewportWidth;
        if (maxScrollX < 0) maxScrollX = 0;
        if (newScrollX < 0) newScrollX = 0;
        if (newScrollX > maxScrollX) newScrollX = maxScrollX;
        g_Seq.scrollX = newScrollX;
    }

    update_scrollbar(hwnd);
    invalidate_timeline_cache();
    InvalidateRect(hwnd, NULL, FALSE);
}

 
static inline int get_solo_target_track(void) {
    if (!g_hWnd || !IsWindow(g_hWnd)) return -1;

    RECT rc;
    GetClientRect(g_hWnd, &rc);
    int clientH = rc.bottom - rc.top;
    int clientW = rc.right - rc.left;
    int my = g_Seq.mouseY;
    int mx = g_Seq.mouseX;

    
    if (my <= get_header_height() || my >= clientH - get_bottom_dock_height()) return -1;
    if (mx >= clientW - cseq_sb_width()) return -1;

    
    int t = (my - get_header_height() + g_Seq.scrollY) / get_track_height();
    if (t < 0 || t >= g_Seq.trackCount) return -1;

    
    if (mx >= get_track_header_width()) {
        int clipIdx = get_clip_under_mouse(mx, my);
        if (clipIdx >= 0 && clipIdx < g_Seq.clipCount) {
            int clipTrack = g_Seq.clips[clipIdx].track;
            if (clipTrack >= 0 && clipTrack < g_Seq.trackCount)
                return clipTrack;
        }
    }

    return t;
}

 
static inline void pan_timeline_bars(HWND hwnd, int bars) {
    float ppb = get_pixels_per_beat();
    int totalTimelineWidth = (int)(total_beats() * ppb);
    RECT rc;
    GetClientRect(hwnd, &rc);
    int visibleWidth = (rc.right - rc.left) - get_track_header_width();
    int maxScrollX = totalTimelineWidth - visibleWidth;
    if (maxScrollX < 0) maxScrollX = 0;

    g_Seq.scrollX += (int)(bars * beats_per_bar() * ppb);
    if (g_Seq.scrollX < 0) g_Seq.scrollX = 0;
    if (g_Seq.scrollX > maxScrollX) g_Seq.scrollX = maxScrollX;
    g_timelineDirty = true;

    update_scrollbar(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
}

static inline bool handle_topbar_click(HWND hwnd, int mx, int btnMode) {
    for (int b = 0; b < TOPBAR_SLOT_COUNT; ++b) {
        int bx = 0, bw = 0;
        get_topbar_slot_bounds(NULL, b, &bx, &bw);

        if (mx >= bx && mx <= bx + bw) {
            switch (b) {
             
            case 0:
                if (btnMode == 2) {
                    seq_set_playing(false);
                    set_playback_frame(0);
                    granular_stop_all();
                    synth_stop_all();
                    
                    
                }
                else {
                    
                    play_with_start_reset();
                }
                break;
             
            case 1:
                if (btnMode == 0) {
                    g_Seq.bpm += 1.0f;
                    if (g_Seq.bpm > 300.0f) g_Seq.bpm = 40.0f;
                }
                else if (btnMode == 1) {
                     
                    open_bpm_dialog(hwnd);
                }
                else {
                    g_Seq.bpm = 120.0f;
                }
                break;
             
            case 2:
                if (btnMode == 0) {
                    bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    change_bar_count(shiftHeld ? 4 : 1);
                }
                else if (btnMode == 1) open_bars_dialog(hwnd);  
                else change_bar_count(4 - g_Seq.visibleBarCount);
                break;
             
            case 3:
                if (btnMode == 0) {
                    g_Seq.swing += 0.05f;
                    if (g_Seq.swing > 0.95f) g_Seq.swing = 0.0f;
                }
                else if (btnMode == 1) {
                    open_swing_dialog(hwnd);
                }
                else {
                    g_Seq.swing = 0.0f;
                }
                break;
             
            case 4:
                if (btnMode == 1) {
                    POINT pt; GetCursorPos(&pt);
                    show_grid_context_menu(hwnd, pt.x, pt.y);
                }
                else if (btnMode == 2) {
                    g_Seq.quantizeEnabled = true;
                }
                else {
                    g_Seq.quantizeEnabled = !g_Seq.quantizeEnabled;
                }
                break;
             
            case 5:
                if (btnMode == 2) g_Seq.playFromStartOnPlay = false;
                else g_Seq.playFromStartOnPlay = !g_Seq.playFromStartOnPlay;
                break;
             
            case 6:
                if (btnMode == 1) {                
                    open_lofi_dialog(hwnd);
                }
                else if (btnMode == 2) {           
                    g_Seq.isLofi = false;
                }
                else {                             
                    g_Seq.isLofi = !g_Seq.isLofi;
                }
                break;
             
            case 7: {
                LONG currentFrame = InterlockedCompareExchange(&g_Seq.playbackFrame, 0, 0);
                float curBeat = frame_to_beat((ma_uint64)currentFrame, g_Seq.bpm, g_Seq.swing);
                open_sample_dialog(hwnd, 0, quantize_beat_16th(curBeat));
                break;
            }
             
            case 8:
                 
                if (btnMode == 1) {
                    POINT pt; GetCursorPos(&pt);
                    show_export_context_menu(hwnd, pt.x, pt.y);
                }
                else {
                    SendMessageA(hwnd, WM_KEYDOWN, 'E', 0);
                }
                break;
             
            case 9:
                if (btnMode == 1) {
                     
                    save_project_dialog(hwnd);
                }
                else if (g_Seq.currentProjectFile[0]) {
                     
                    save_project_to_csq(g_Seq.currentProjectFile);
                }
                else {
                     
                    save_project_dialog(hwnd);
                }
                break;
             
            case 10:
                if (btnMode == 1) {
                    open_confirm_dialog(hwnd, CONFIRM_INIT);
                }
                else if (g_Seq.isModified && (g_Seq.clipCount > 0 || g_Seq.currentProjectFile[0] != '\0') && !job_is_busy()) {
                    g_confirmLoadPath[0] = '\0';
                    open_confirm_dialog(hwnd, CONFIRM_LOAD);
                }
                else {
                    load_project_dialog(hwnd);
                }
                break;
             
            case 11:
                open_keybinds_dialog(hwnd);
                break;
             
            case 12:
                 
                toggle_visualizer_dialog(hwnd);
                break;
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return true;
        }
    }
    return false;
}

 
static inline LRESULT cseq_main_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Worker threads request UI updates through this message instead of
    // touching GDI/flags directly from their own threads.
    if (msg == WM_APP_FULL_REDRAW) {
        g_timelineDirty = true;
        update_scrollbar(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    if (g_Seq.isBusy) {
        if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN ||
            msg == WM_KEYDOWN || msg == WM_DROPFILES || msg == WM_MOUSEWHEEL) {
            return 0;
        }
    }

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_CREATE: {
        g_hWnd = hwnd;

        LONG style = GetWindowLongA(hwnd, GWL_STYLE);
        style &= ~WS_VSCROLL;
        SetWindowLongA(hwnd, GWL_STYLE, style);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

        cseq_apply_dpi_for_window(hwnd);
        refresh_ui_font_for_dpi();
        invalidate_timeline_cache();
        
         
        g_Seq.isModified = false;
        update_window_title();

        DragAcceptFiles(hwnd, TRUE);
        SetTimer(hwnd, 1, 16, NULL);
        update_scrollbar(hwnd);

         
        start_main_120fps_pacer();

        HICON hIconBig = (HICON)LoadImageA(GetModuleHandle(NULL), MAKEINTRESOURCEA(1), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
        HICON hIconSmall = (HICON)LoadImageA(GetModuleHandle(NULL), MAKEINTRESOURCEA(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);

        SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
        SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
        return 0;
    }

    case WM_SIZE: {
        update_scrollbar(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_DPICHANGED: {
        UINT newDpi = HIWORD(wParam);
        cseq_update_scales_from_dpi(newDpi, newDpi);
        refresh_ui_font_for_dpi();
        invalidate_timeline_cache();
        cseq_sb_cancel_drag();

         
        RECT *sug = (RECT *)lParam;
        SetWindowPos(hwnd, NULL,
                     sug->left, sug->top,
                     sug->right - sug->left, sug->bottom - sug->top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_TIMER:
        if (wParam == 1) {
             
            if (g_pendingCsqPath[0] && !g_Seq.isBusy) {
                char path[MAX_PATH];
                strncpy(path, g_pendingCsqPath, MAX_PATH - 1);
                path[MAX_PATH - 1] = '\0';
                g_pendingCsqPath[0] = '\0';
                load_project_from_csq(path);
            }
             
            else if (g_loadDialogPending && !g_Seq.isBusy) {
                g_loadDialogPending = false;
                load_project_dialog(hwnd);
            }
             
        }
        return 0;

    case WM_MOUSEWHEEL: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        POINT pt = {mx, my};
        ScreenToClient(hwnd, &pt);

        short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        float deltaVol = (zDelta > 0) ? 0.05f : -0.05f;

        
        if (pt.y <= get_header_height() / 2) {
            for (int b = 1; b <= 3; ++b) {
                int bx = 0, bw = 0;
                get_topbar_slot_bounds(NULL, b, &bx, &bw);
                if (pt.x >= bx && pt.x <= bx + bw) {
                    if (b == 1) {
                        g_Seq.bpm += (zDelta > 0) ? 1.0f : -1.0f;
                        if (g_Seq.bpm < 40.0f) g_Seq.bpm = 40.0f;
                        if (g_Seq.bpm > 300.0f) g_Seq.bpm = 300.0f;
                    } else if (b == 2) {
                        bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                        int step = shiftHeld ? 4 : 1;
                        change_bar_count((zDelta > 0) ? step : -step);
                    } else if (b == 3) {
                        g_Seq.swing += (zDelta > 0) ? 0.05f : -0.05f;
                        if (g_Seq.swing < 0.0f) g_Seq.swing = 0.0f;
                        if (g_Seq.swing > 0.95f) g_Seq.swing = 0.95f;
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
        }

        
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        int clientH = rcClient.bottom - rcClient.top;
        int clientW = rcClient.right - rcClient.left;

        
        if ((GetKeyState(VK_CONTROL) & 0x8000) &&
            pt.x >= get_track_header_width() &&
            pt.y > get_header_height() &&
            pt.y < clientH - get_bottom_dock_height()) {
            
            // Ctrl+Wheel: timeline zoom
            float newZoom = g_Seq.zoom + (zDelta > 0 ? 0.1f : -0.1f);   
            if (newZoom > 8.0f) newZoom = 8.0f;
            if (newZoom < 0.25f) newZoom = 0.25f;   
            zoom_to_cursor(hwnd, newZoom);
            return 0;
        }

        
        if (pt.y >= clientH - get_bottom_dock_height()) {
            int ctrlW = scale_x(56), dockH = scale_y(30);
            int zoomX = get_track_header_width() + scale_x(8);
            int zoomY = clientH - scale_y(38);
            if (pt.x >= zoomX && pt.x <= zoomX + ctrlW &&
                pt.y >= zoomY && pt.y <= zoomY + dockH) {
                float step = (zDelta > 0) ? 0.05f : -0.05f;   
                float newZoom = g_Seq.zoom + step;
                if (newZoom < 0.25f) newZoom = 0.25f;
                if (newZoom > 8.0f) newZoom = 8.0f;
                zoom_to_cursor(hwnd, newZoom);
                return 0;
            }
        }

         
        if (pt.y >= clientH - get_bottom_dock_height()) {
            int ctrlW = scale_x(56), gap = scale_x(5);
            int mstX = get_track_header_width() + scale_x(8) + ctrlW + gap;
            if (pt.x >= mstX && pt.x <= mstX + ctrlW) {
                float d = (zDelta > 0) ? 0.05f : -0.05f;
                g_Seq.masterVolume += d;
                if (g_Seq.masterVolume < 0.0f) g_Seq.masterVolume = 0.0f;
                if (g_Seq.masterVolume > 1.5f) g_Seq.masterVolume = 1.5f;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }

        
        if (pt.y >= clientH - get_bottom_dock_height()) {
            float ppb = get_pixels_per_beat();
            int totalTimelineWidth = (int)(total_beats() * ppb);
            int visibleWidth = clientW - get_track_header_width();
            int maxScrollX = totalTimelineWidth - visibleWidth;
            if (maxScrollX < 0) maxScrollX = 0;

            int scrollStep = (int)ppb;
            g_Seq.scrollX += (zDelta > 0) ? -scrollStep : scrollStep;
            if (g_Seq.scrollX < 0) g_Seq.scrollX = 0;
            if (g_Seq.scrollX > maxScrollX) g_Seq.scrollX = maxScrollX;
            g_timelineDirty = true;

            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        
        if (pt.y > get_header_height() && pt.x < get_track_header_width()) {
            int track = (pt.y - get_header_height() + g_Seq.scrollY) / get_track_height();
            if (track >= 0 && track < g_Seq.trackCount && track < MAX_TRACKS) {
                seq_lock();
                g_Seq.trackVolume[track] += deltaVol;
                if (g_Seq.trackVolume[track] < 0.0f) g_Seq.trackVolume[track] = 0.0f;
                if (g_Seq.trackVolume[track] > 1.0f) g_Seq.trackVolume[track] = 1.0f;
                seq_unlock();
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        
        int clipIdx = get_clip_under_mouse(pt.x, pt.y);
        if (clipIdx != -1) {
            bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            seq_lock();
            if (clipIdx >= 0 && clipIdx < g_Seq.clipCount) {
                if (shiftHeld) {
                    
                    // Shift+Wheel (clip playback rate)
                    float deltaRate = (zDelta > 0) ? 0.02f : -0.02f;
                    if (g_Seq.clips[clipIdx].isSelected) {
                        for (int i = 0; i < g_Seq.clipCount; ++i) {
                            if (g_Seq.clips[i].isSelected) {
                                g_Seq.clips[i].playbackRate += deltaRate;
                                if (g_Seq.clips[i].playbackRate < 0.01f) g_Seq.clips[i].playbackRate = 0.01f;
                                if (g_Seq.clips[i].playbackRate > 2.00f) g_Seq.clips[i].playbackRate = 2.00f;
                                mark_clip_bars_dirty(&g_Seq.clips[i]);
                            }
                        }
                    } else {
                        g_Seq.clips[clipIdx].playbackRate += deltaRate;
                        if (g_Seq.clips[clipIdx].playbackRate < 0.01f) g_Seq.clips[clipIdx].playbackRate = 0.01f;
                        if (g_Seq.clips[clipIdx].playbackRate > 2.00f) g_Seq.clips[clipIdx].playbackRate = 2.00f;
                        mark_clip_bars_dirty(&g_Seq.clips[clipIdx]);
                    }
                } else if (g_Seq.clips[clipIdx].isSelected) {
                    for (int i = 0; i < g_Seq.clipCount; ++i) {
                        if (g_Seq.clips[i].isSelected) {
                            g_Seq.clips[i].volume += deltaVol;
                            if (g_Seq.clips[i].volume < 0.0f) g_Seq.clips[i].volume = 0.0f;
                            if (g_Seq.clips[i].volume > 2.0f) g_Seq.clips[i].volume = 2.0f;
                            mark_clip_bars_dirty(&g_Seq.clips[i]);
                        }
                    }
                    g_Seq.volumePopupClip = clipIdx;
                    g_Seq.volumePopupExpiry = GetTickCount64() + 1200;
                } else {
                    g_Seq.clips[clipIdx].volume += deltaVol;
                    if (g_Seq.clips[clipIdx].volume < 0.0f) g_Seq.clips[clipIdx].volume = 0.0f;
                    if (g_Seq.clips[clipIdx].volume > 2.0f) g_Seq.clips[clipIdx].volume = 2.0f;
                    mark_clip_bars_dirty(&g_Seq.clips[clipIdx]);
                    g_Seq.volumePopupClip = clipIdx;
                    g_Seq.volumePopupExpiry = GetTickCount64() + 1200;
                }
            }
            seq_unlock();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        
        g_Seq.scrollY -= (zDelta / WHEEL_DELTA) * (get_track_height() / 2);
        g_timelineDirty = true;
        update_scrollbar(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        POINT pt;
        DragQueryPoint(hDrop, &pt);

         
        wchar_t filepathW[MAX_PATH];
        char filepath[MAX_PATH];
        UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);

         
        if (fileCount == 1 && DragQueryFileW(hDrop, 0, filepathW, MAX_PATH) &&
            wide_to_utf8_buf(filepathW, filepath, MAX_PATH) > 0 &&
            is_csq_path(filepath)) {
            DragFinish(hDrop);
            if (!job_is_busy()) {
                if (g_Seq.isModified && (g_Seq.clipCount > 0 || g_Seq.currentProjectFile[0] != '\0')) {
                    strncpy(g_confirmLoadPath, filepath, MAX_PATH - 1);
                    g_confirmLoadPath[MAX_PATH - 1] = '\0';
                    open_confirm_dialog(hwnd, CONFIRM_LOAD);
                }
                else {
                    load_project_from_csq(filepath);
                }
            }
            return 0;
        }

         
        int track = (pt.y - get_header_height() + g_Seq.scrollY) / get_track_height();
        if (track < 0) track = 0;
        if (track >= g_Seq.trackCount) track = g_Seq.trackCount - 1;

        float ppb = get_pixels_per_beat();
        float dropBeat = (float)(pt.x - get_track_header_width() + g_Seq.scrollX) / ppb;
        if (dropBeat < 0.0f) dropBeat = 0.0f;
        dropBeat = quantize_beat_16th(dropBeat);

        
        
        deselect_all_clips();

        for (UINT i = 0; i < fileCount; ++i) {
            if (DragQueryFileW(hDrop, i, filepathW, MAX_PATH) &&
                wide_to_utf8_buf(filepathW, filepath, MAX_PATH) > 0) {
                seq_lock();
                bool samplesFull  = (g_Seq.sampleCount >= MAX_SAMPLES);
                bool atCapacity   = samplesFull || (g_Seq.clipCount >= MAX_CLIPS);
                seq_unlock();

                 
                if (atCapacity) {
                    set_seq_status_msg(hwnd,
                        samplesFull ? "Sample limit reached" : "Clip limit reached");
                    break;
                }

                int sampleIdx = load_audio_file(filepath);
                if (sampleIdx != -1) {
                    seq_lock();
                    bool clipFull = (g_Seq.clipCount >= MAX_CLIPS);
                    seq_unlock();
                    if (clipFull) {
                        set_seq_status_msg(hwnd, "Clip limit reached");
                        break;
                    }
                    if (add_clip(sampleIdx, track, dropBeat) != -1)
                        dropBeat += 1.0f;    
                }
                else if (is_csq_path(filepath)) {
                    set_seq_status_msg(hwnd, "Drop .csq files individually.");
                    break;
                }
            }
        }
        DragFinish(hDrop);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_KILLFOCUS: {
         
        g_Seq.hoveredClip       = -1;
        g_Seq.volumePopupClip   = -1;
        g_Seq.hasMovedPastThreshold = false;

        if (g_Seq.isDraggingClip || g_Seq.isVolumeDragging || g_Seq.isSlipDragging ||
            g_Seq.isMarqueeSelecting || g_Seq.isResizingLeft || g_Seq.isResizingRight ||
            g_Seq.isFadeInDragging || g_Seq.isFadeOutDragging || g_Seq.isMiddlePanning) {
            g_Seq.isDraggingClip = false;
            g_Seq.isCtrlDuplicating = false;
            g_Seq.isVolumeDragging = false;
            g_Seq.isSlipDragging = false;
            g_Seq.isMarqueeSelecting = false;
            g_Seq.marqueeStartX = 0;
            g_Seq.marqueeStartY = 0;
            g_Seq.marqueeCurX = 0;
            g_Seq.marqueeCurY = 0;
            g_Seq.isResizingLeft = false;
            g_Seq.isResizingRight = false;
            g_Seq.isStretchResizing = false;
            g_Seq.isFadeInDragging = false;
            g_Seq.isFadeOutDragging = false;
            g_Seq.isMiddlePanning = false;
            g_Seq.pendingPlayheadSet = false;
            g_Seq.draggedClipIndex = -1;
            g_Seq.dragOrigTrack = 0;
            cseq_clip_structure_changed();
            g_timelineDirty = true;
            if (GetCapture() == hwnd) ReleaseCapture();
        }
        cseq_sb_end_drag(hwnd);
        cseq_sb_set_hover(hwnd, false, false);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (g_sbState.dragging) {
            cseq_sb_cancel_drag();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_MBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        if (my <= get_header_height() / 2) {
            if (handle_topbar_click(hwnd, mx, 2)) return 0;
        }

        if (my > get_header_height() && mx < get_track_header_width()) {
            int track = (my - get_header_height() + g_Seq.scrollY) / get_track_height();
            if (track >= 0 && track < g_Seq.trackCount) {
                LONG curFrame = InterlockedCompareExchange(&g_Seq.playbackFrame, 0, 0);
                float curBeat = frame_to_beat((ma_uint64)curFrame, g_Seq.bpm, g_Seq.swing);
                open_sample_dialog(hwnd, track, quantize_beat_16th(curBeat));
            }
            return 0;
        }

        if (my > get_header_height() && mx >= get_track_header_width()) {
            g_Seq.isMiddlePanning = true;
            g_Seq.panStartX = mx;
            g_Seq.panStartY = my;
            g_Seq.panOrigScrollX = g_Seq.scrollX;
            g_Seq.panOrigScrollY = g_Seq.scrollY;
            SetCapture(hwnd);
            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
        }
        return 0;
    }

    case WM_MBUTTONUP: {
        if (g_Seq.isMiddlePanning) {
            g_Seq.isMiddlePanning = false;
            ReleaseCapture();
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);

             
            {
                RefractSbGeom sg;
                if (cseq_sb_get_geom(hwnd, &sg)) {
                    RefractSbPart sbPart = cseq_sb_hit_test(&sg, mx, my);
                    if (sbPart != CSEQ_SB_NONE) {
                        if (sbPart == CSEQ_SB_THUMB) {
                            cseq_sb_begin_drag(hwnd, my, &sg);
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                        else if (sbPart == CSEQ_SB_TRACK_UP)   cseq_sb_page_scroll(hwnd, true);
                        else                                   cseq_sb_page_scroll(hwnd, false);
                        return 0;
                    }
                }
            }

            if (my <= get_header_height() / 2) {
                if (handle_topbar_click(hwnd, mx, 0)) return 0;
            }

             
            {
                RECT rcPage;
                GetClientRect(hwnd, &rcPage);
                RECT upRc, dnRc;
                if (get_pager_button_rects(rcPage.right - rcPage.left, rcPage.bottom - rcPage.top, &upRc, &dnRc)) {
                    if (mx >= upRc.left && mx <= upRc.right && my >= upRc.top && my <= upRc.bottom) {
                        g_Seq.scrollY -= 8 * get_track_height();
                        update_scrollbar(hwnd);
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                    if (mx >= dnRc.left && mx <= dnRc.right && my >= dnRc.top && my <= dnRc.bottom) {
                        g_Seq.scrollY += 8 * get_track_height();
                        update_scrollbar(hwnd);
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                }
                RECT tsRc;
                get_timesig_badge_rect(rcPage.right - rcPage.left, rcPage.bottom - rcPage.top, &tsRc);
                if (mx >= tsRc.left && mx <= tsRc.right && my >= tsRc.top && my <= tsRc.bottom) {
                    open_timesig_dialog(hwnd);
                    return 0;
                }
            }

            if (my > get_header_height()) {
                RECT rcClient;
                GetClientRect(hwnd, &rcClient);
                int clientH = rcClient.bottom - rcClient.top;

                int btnY = clientH - scale_y(38);
                if (my >= btnY && my <= btnY + scale_y(30)) {
                    int availW = get_track_header_width();
                    int btnMargin = scale_x(6);
                    int btnGap = scale_x(4);
                    int btnW = (availW - btnMargin * 2 - btnGap) / 2;
                    int totalBtnsW = btnW * 2 + btnGap;
                    int btnStartX = btnMargin;

                    
                    if (mx >= btnStartX && mx <= btnStartX + btnW && my <= btnY + scale_y(20)) {
                        add_track_action(hwnd);
                        return 0;
                    }
                    
                    if (mx >= btnStartX + btnW + btnGap && mx <= btnStartX + totalBtnsW && my <= btnY + scale_y(20)) {
                        remove_track_action(hwnd);
                        return 0;
                    }
                    
                    int ctrlW = scale_x(56);
                    int gap = scale_x(5);
                    int zoomX = get_track_header_width() + scale_x(8);
                    if (mx >= zoomX && mx <= zoomX + ctrlW) {
                        zoom_to_cursor(hwnd, 1.0f);
                        return 0;
                    }
                     
                    int mstX = zoomX + ctrlW + gap;
                    if (mx >= mstX && mx <= mstX + ctrlW) {
                        seq_lock();
                        g_Seq.masterVolume = 1.0f;
                        seq_unlock();
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                     
                    int modeX = mstX + ctrlW + gap;
                    if (mx >= modeX && mx <= modeX + ctrlW) {
                        seq_lock();
                        g_Seq.masterMode = (g_Seq.masterMode == 0) ? 1 : 0;
                        seq_unlock();
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }

                    // Synth module launchers: spawn a clip of the matching
                    // kind and open its piano roll (layout mirrors ui.h).
                    int mediaX = modeX + ctrlW + gap;
                    int quadX = mediaX + ctrlW + gap;
                    int haloX = quadX + ctrlW + gap;
                    if (mx >= mediaX && mx <= mediaX + ctrlW) {
                        open_media_explorer(hwnd);
                        return 0;
                    }
                    if (mx >= quadX && mx <= quadX + ctrlW) {
                        int idx = spawn_synth_clip(CLIP_KIND_QUADRUM);
                        if (idx >= 0) open_midi_editor(hwnd, idx);
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                    if (mx >= haloX && mx <= haloX + ctrlW) {
                        int idx = spawn_synth_clip(CLIP_KIND_HALO);
                        if (idx >= 0) open_midi_editor(hwnd, idx);
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                }

                int clipIdx = get_clip_under_mouse(mx, my);
                float ppb = get_pixels_per_beat();

                if (mx < (get_track_header_width() - 6) && clipIdx == -1) {
                    int track = (my - get_header_height() + g_Seq.scrollY) / get_track_height();
                    if (track >= 0 && track < g_Seq.trackCount) {
                        if (GetKeyState(VK_CONTROL) & 0x8000) {
                            toggle_select_all_clips_on_track(track);
                             
                            g_timelineDirty = true;
                        }
                        else if (GetKeyState(VK_SHIFT) & 0x8000) {
                            // Shift+drag reorders the track. Starting here lets
                            // the drag begin immediately without a mute toggle.
                            g_Seq.isTrackHeaderDragging = true;
                            g_Seq.dragTrackOrig = track;
                            g_Seq.dragTrackCur = track;
                            g_Seq.hasMovedPastThreshold = false;
                            g_Seq.dragStartX = mx;
                            g_Seq.dragStartY = my;
                            SetCapture(hwnd);
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                        else {
                            // Plain click toggles mute (unchanged behavior).
                            seq_lock();
                            seq_set_track_mute(track, !g_Seq.trackMuted[track]);
                            seq_unlock();
                            g_timelineDirty = true;
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    return 0;
                }

                
                if (clipIdx == -1 && mx >= (get_track_header_width() - 6)) {
                    float clickedBeat = (float)(mx - get_track_header_width() + g_Seq.scrollX) / ppb;
                    if (clickedBeat < 0.0f) clickedBeat = 0.0f;
                    if (clickedBeat > total_beats()) clickedBeat = total_beats();
                    ma_uint64 newFrame = beat_to_frame(clickedBeat, g_Seq.bpm, g_Seq.swing);

                    // Remember the track that was clicked so spawned synth clips
                    // and Media-Explorer "Add to Canvas" land on it.
                    int clickTrack = (my - get_header_height() + g_Seq.scrollY) / get_track_height();
                    if (clickTrack >= 0 && clickTrack < g_Seq.trackCount)
                        g_Seq.lastClickedTrack = clickTrack;

                    g_Seq.origPlaybackFrame = InterlockedCompareExchange(&g_Seq.playbackFrame, 0, 0);
                    set_playback_frame((LONG)newFrame);
                    
                    g_Seq.pendingPlayheadSet = true;
                    g_Seq.shouldRevertPlayhead = false;

                    int startMx = max(get_track_header_width(), mx);
                    g_Seq.isMarqueeSelecting = true;
                    g_Seq.marqueeStartX = startMx;
                    g_Seq.marqueeStartY = my;
                    g_Seq.marqueeCurX = startMx;
                    g_Seq.marqueeCurY = my;

                    bool ctrlOrShift = (GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000);
                    if (!ctrlOrShift) {
                        deselect_all_clips();
                    }

                    
                    seq_lock();
                    for (int i = 0; i < g_Seq.clipCount; ++i) {
                        g_Seq.clips[i].dragStartTrackOrig = g_Seq.clips[i].isSelected ? 1 : 0;
                    }
                    seq_unlock();

                    SetCapture(hwnd);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }

                if (clipIdx != -1) {
                    seq_lock();
                    if (clipIdx < 0 || clipIdx >= g_Seq.clipCount) {
                        seq_unlock();
                        return 0;
                    }
                    Clip* c = &g_Seq.clips[clipIdx];
                    // Remember the clip's track as the last-clicked track.
                    if (c->track >= 0 && c->track < g_Seq.trackCount)
                        g_Seq.lastClickedTrack = c->track;
                    int cX1 = get_track_header_width() - g_Seq.scrollX + (int)(c->startBeat * ppb);
                    int cX2 = cX1 + (int)(c->lengthBeats * ppb);
                    int cY1 = get_header_height() - g_Seq.scrollY + c->track * get_track_height();

                    bool ctrlHeld  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    bool altHeld   = (GetKeyState(VK_MENU) & 0x8000) != 0;

                     
                    bool canSlip = (altHeld && !c->isMidi &&
                                    c->sampleIndex >= 0 && c->sampleIndex < g_Seq.sampleCount &&
                                    g_Seq.samples[c->sampleIndex].loaded &&
                                    g_Seq.samples[c->sampleIndex].frameCount > 0);

                    if (ctrlHeld) {
                         
                        g_Seq.ctrlClickOrigSelected = c->isSelected;
                        c->isSelected = true;
                        g_Seq.isCtrlDuplicating = true;
                    }
                    else if (!shiftHeld && !canSlip) {
                         
                        bool inMulti = false;
                        for (int i = 0; i < g_Seq.clipCount; ++i) {
                            if (i != clipIdx && g_Seq.clips[i].isSelected) { inMulti = true; break; }
                        }
                        if (inMulti && c->isSelected) {
                            g_Seq.pendingSingleSelectClip = clipIdx;
                        }
                        else {
                            for (int i = 0; i < g_Seq.clipCount; ++i) g_Seq.clips[i].isSelected = false;
                            c->isSelected = true;
                        }
                    }
                    else if (!c->isSelected) {
                         
                        for (int i = 0; i < g_Seq.clipCount; ++i) g_Seq.clips[i].isSelected = false;
                        c->isSelected = true;
                    }

                     
                    g_timelineDirty = true;

                    for (int i = 0; i < g_Seq.clipCount; ++i) {
                        if (g_Seq.clips[i].isSelected) {
                            g_Seq.clips[i].dragStartBeatOrig = g_Seq.clips[i].startBeat;
                            g_Seq.clips[i].dragStartLengthOrig = g_Seq.clips[i].lengthBeats;
                            g_Seq.clips[i].dragStartTrackOrig = g_Seq.clips[i].track;
                            g_Seq.clips[i].dragStartOffsetOrig = g_Seq.clips[i].sampleOffsetFrames;
                        }
                    }

                     
                    int cY2 = cY1 + get_track_height();
                    ClipHitZone hitZone = CLIP_HIT_BODY;
                    if (!canSlip) {
                        hitZone = hit_test_clip_zones(mx, my, c, cX1, cX2, cY1, cY2, ppb);
                    }

                    if (hitZone == CLIP_HIT_TRIM_LEFT) {
                        g_Seq.dragStartX = mx;
                        g_Seq.dragStartY = my;
                        g_Seq.hasMovedPastThreshold = false;
                        g_Seq.draggedClipIndex = clipIdx;
                        g_Seq.isResizingLeft = true;
                        g_Seq.resizeOrigStartBeat = c->startBeat;
                        g_Seq.resizeOrigLengthBeats = c->lengthBeats;
                        g_Seq.resizeOrigOffsetFrames = c->sampleOffsetFrames;
                        mark_selected_clips_bars_dirty();
                        seq_unlock();
                        SetCapture(hwnd);
                        return 0;
                    }
                    else if (hitZone == CLIP_HIT_TRIM_RIGHT) {
                        g_Seq.dragStartX = mx;
                        g_Seq.dragStartY = my;
                        g_Seq.hasMovedPastThreshold = false;
                        g_Seq.draggedClipIndex = clipIdx;
                        g_Seq.isResizingRight = true;
                        // Shift held = stretch: playback rate follows the
                        // length so the waveform keeps its same audio content
                        // but compressed/expanded in time.
                        g_Seq.isStretchResizing = shiftHeld;
                        g_Seq.resizeOrigRate = (c->playbackRate > 0.01f) ? c->playbackRate : 1.0f;
                        g_Seq.resizeOrigStartBeat = c->startBeat;
                        g_Seq.resizeOrigLengthBeats = c->lengthBeats;
                        mark_selected_clips_bars_dirty();
                        seq_unlock();
                        SetCapture(hwnd);
                        return 0;
                    }

                     
                    bool onFadeIn = (hitZone == CLIP_HIT_FADE_IN_HANDLE);
                    bool onFadeOut = (hitZone == CLIP_HIT_FADE_OUT_HANDLE);

                    if (onFadeIn || onFadeOut) {
                        seq_unlock();
                        push_undo_state();
                        seq_lock();
                        g_Seq.draggedClipIndex = clipIdx;
                        g_Seq.dragStartX = mx;
                        g_Seq.dragStartY = my;
                        for (int i = 0; i < g_Seq.clipCount; ++i) {
                            if (g_Seq.clips[i].isSelected) {
                                g_Seq.clips[i].dragStartBeatOrig = onFadeIn
                                    ? g_Seq.clips[i].fadeInBeats
                                    : g_Seq.clips[i].fadeOutBeats;
                            }
                        }
                        if (onFadeIn) {
                            g_Seq.isFadeInDragging = true;
                        }
                        else {
                            g_Seq.isFadeOutDragging = true;
                        }
                        mark_selected_clips_bars_dirty();
                        seq_unlock();
                        SetCapture(hwnd);
                        return 0;
                    }

                    float clickedBeat = (float)(mx - get_track_header_width() + g_Seq.scrollX) / ppb;
                    if (clickedBeat < 0.0f) clickedBeat = 0.0f;
                    if (clickedBeat > total_beats()) clickedBeat = total_beats();
                    ma_uint64 newFrame = beat_to_frame(clickedBeat, g_Seq.bpm, g_Seq.swing);

                    set_playback_frame((LONG)newFrame);

                    g_Seq.dragStartX = mx;
                    g_Seq.dragStartY = my;
                    g_Seq.hasMovedPastThreshold = false;
                    g_Seq.draggedClipIndex = clipIdx;

                    if (canSlip) {
                         
                        if (!shiftHeld && !ctrlHeld) {
                            deselect_all_clips();
                        }
                        c->isSelected = true;
                        g_Seq.isSlipDragging = true;
                        g_Seq.isDraggingClip = false;
                        g_Seq.isVolumeDragging = false;
                        g_Seq.isResizingLeft = false;
                        g_Seq.isResizingRight = false;
                        g_Seq.isFadeInDragging = false;
                        g_Seq.isFadeOutDragging = false;
                        g_Seq.hasMovedPastThreshold = false;
                        g_Seq.dragStartSampleOffset = c->sampleOffsetFrames;
                    }
                    else if (shiftHeld) {
                        g_Seq.isVolumeDragging = true;
                        g_Seq.dragStartVolume = c->volume;
                        g_Seq.volumePopupClip = clipIdx;
                        g_Seq.volumePopupExpiry = GetTickCount64() + 1500;
                    }
                    else {
                         
                        g_Seq.isDraggingClip = true;
                        g_Seq.dragStartBeatOffset = clickedBeat - c->startBeat;
                        g_Seq.dragOrigTrack = c->track;
                    }
                    mark_selected_clips_bars_dirty();
                    seq_unlock();
                    SetCapture(hwnd);
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

    case WM_LBUTTONDBLCLK: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

         
        if (my > get_header_height() && mx >= get_track_header_width()) {
            int clipIdx = get_clip_under_mouse(mx, my);
            if (clipIdx >= 0 && clipIdx < g_Seq.clipCount) {
                seq_lock();
                bool isMidi = g_Seq.clips[clipIdx].isMidi;
                seq_unlock();
                if (isMidi) {
                    open_midi_editor(hwnd, clipIdx);
                    return 0;
                }
            }
        }

         
        return cseq_main_wndproc(hwnd, WM_LBUTTONDOWN, wParam, lParam);
    }

    case WM_RBUTTONDBLCLK:
        return cseq_main_wndproc(hwnd, WM_RBUTTONDOWN, wParam, lParam);

    case WM_MBUTTONDBLCLK:
        return cseq_main_wndproc(hwnd, WM_MBUTTONDOWN, wParam, lParam);

    case WM_RBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

         
        {
            RefractSbGeom sg;
            if (cseq_sb_get_geom(hwnd, &sg) &&
                cseq_sb_hit_test(&sg, mx, my) != CSEQ_SB_NONE) return 0;
        }

        if (my <= get_header_height() / 2) {
            if (handle_topbar_click(hwnd, mx, 1)) return 0;
        }

         
        {
            RECT rcPage;
            GetClientRect(hwnd, &rcPage);
            RECT tsRc;
            get_timesig_badge_rect(rcPage.right - rcPage.left, rcPage.bottom - rcPage.top, &tsRc);
            if (mx >= tsRc.left && mx <= tsRc.right && my >= tsRc.top && my <= tsRc.bottom) {
                open_timesig_dialog(hwnd);
                return 0;
            }
        }

        if (my > get_header_height()) {
            if (mx < get_track_header_width()) {
                POINT screenPt = { mx, my };
                ClientToScreen(hwnd, &screenPt);
                int track = (my - get_header_height() + g_Seq.scrollY) / get_track_height();
                if (track >= 0 && track < g_Seq.trackCount) {
                    show_track_context_menu(hwnd, track, screenPt.x, screenPt.y);
                }
                return 0;
            }

            int clipIdx = get_clip_under_mouse(mx, my);
            bool ctrlOrShift = (GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000);

            
            if (clipIdx != -1) {
                seq_lock();
                if (!g_Seq.clips[clipIdx].isSelected && !ctrlOrShift) {
                    for (int i = 0; i < g_Seq.clipCount; ++i) g_Seq.clips[i].isSelected = false;
                    g_Seq.clips[clipIdx].isSelected = true;
                }
                seq_unlock();
            }

             
            if (clipIdx != -1) {
                float ppb = get_pixels_per_beat();
                seq_lock();
                Clip* rcClip = &g_Seq.clips[clipIdx];
                int cX1 = get_track_header_width() - g_Seq.scrollX + (int)(rcClip->startBeat * ppb);
                int cX2 = cX1 + (int)(rcClip->lengthBeats * ppb);
                int cY1 = get_header_height() - g_Seq.scrollY + rcClip->track * get_track_height();
                int cY2 = cY1 + get_track_height();
                ClipHitZone zone = hit_test_clip_zones(mx, my, rcClip, cX1, cX2, cY1, cY2, ppb);
                seq_unlock();
                if (zone == CLIP_HIT_FADE_IN_HANDLE || zone == CLIP_HIT_FADE_OUT_HANDLE) {
                    POINT screenPt = { mx, my };
                    ClientToScreen(hwnd, &screenPt);
                    show_fade_context_menu(hwnd, clipIdx, zone == CLIP_HIT_FADE_IN_HANDLE, screenPt.x, screenPt.y);
                    return 0;
                }
            }

            g_Seq.dragStartX = mx;
            g_Seq.dragStartY = my;
            g_Seq.hasMovedPastThreshold = false;

            int startMx = max(get_track_header_width(), mx);
            g_Seq.isMarqueeSelecting = true;
            g_Seq.marqueeStartX = startMx;
            g_Seq.marqueeStartY = my;
            g_Seq.marqueeCurX = startMx;
            g_Seq.marqueeCurY = my;

            if (!ctrlOrShift && clipIdx == -1) {
                deselect_all_clips();
            }

            seq_lock();
            for (int i = 0; i < g_Seq.clipCount; ++i) {
                g_Seq.clips[i].dragStartTrackOrig = g_Seq.clips[i].isSelected ? 1 : 0;
            }
            seq_unlock();

            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        return 0;
    }

    case WM_RBUTTONUP: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        if (g_Seq.isMarqueeSelecting) {
            g_Seq.isMarqueeSelecting = false;
            ReleaseCapture();

            if (!g_Seq.hasMovedPastThreshold && my > get_header_height() && mx >= get_track_header_width()) {
                int clipIdx = get_clip_under_mouse(mx, my);
                if (clipIdx != -1) {
                    POINT screenPt = { mx, my };
                    ClientToScreen(hwnd, &screenPt);
                    seq_lock();
                    bool isMidiClip = g_Seq.clips[clipIdx].isMidi;
                    seq_unlock();
                    if (isMidiClip) {
                        show_midi_clip_context_menu(hwnd, clipIdx, screenPt.x, screenPt.y);
                    } else {
                        show_clip_context_menu(hwnd, clipIdx, screenPt.x, screenPt.y);
                    }
                }
            } else {
                 
                cseq_clip_structure_changed();
            }
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);

        g_Seq.mouseX = mx;
        g_Seq.mouseY = my;

         
        if (g_Seq.hoveredClip != -1) InvalidateRect(hwnd, NULL, FALSE);

         
        {
            RefractSbGeom sg;
            if (cseq_sb_get_geom(hwnd, &sg)) {
                if (g_sbState.dragging) {
                    cseq_sb_update_drag(hwnd, my);
                    return 0;
                }
                bool busyDrag = g_Seq.isDraggingClip || g_Seq.isVolumeDragging ||
                                g_Seq.isSlipDragging || g_Seq.isMarqueeSelecting ||
                                g_Seq.isResizingLeft || g_Seq.isResizingRight ||
                                g_Seq.isFadeInDragging || g_Seq.isFadeOutDragging ||
                                g_Seq.isMiddlePanning;
                if (!busyDrag) {
                    RefractSbPart part = cseq_sb_hit_test(&sg, mx, my);
                    cseq_sb_set_hover(hwnd,
                                      part == CSEQ_SB_THUMB,
                                      part != CSEQ_SB_NONE && part != CSEQ_SB_THUMB);
                    if (part != CSEQ_SB_NONE) {
                        g_Seq.hoveredClip = -1;
                        SetCursor(LoadCursor(NULL, IDC_ARROW));
                        return 0;
                    }
                }
            } else {
                if (g_sbState.hoverThumb || g_sbState.hoverTrack) {
                    g_sbState.hoverThumb = false;
                    g_sbState.hoverTrack = false;
                }
                cseq_sb_cancel_drag();
            }
        }

        
        int oldHovered = g_Seq.hoveredClip;

        if (g_Seq.isMiddlePanning) {
            bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            int hMultiplier = shiftHeld ? 4 : 1;    
            int dx = (mx - g_Seq.panStartX) * hMultiplier;
            int dy = my - g_Seq.panStartY;
            g_Seq.scrollX = g_Seq.panOrigScrollX - dx;
            g_Seq.scrollY = g_Seq.panOrigScrollY - dy;
            g_timelineDirty = true;

            
            float ppb = get_pixels_per_beat();
            int totalTimelineWidth = (int)(total_beats() * ppb);
            RECT rcClient;
            GetClientRect(hwnd, &rcClient);
            int visibleWidth = (rcClient.right - rcClient.left) - get_track_header_width();
            int maxScrollX = totalTimelineWidth - visibleWidth;
            if (maxScrollX < 0) maxScrollX = 0;
            if (g_Seq.scrollX < 0) g_Seq.scrollX = 0;
            if (g_Seq.scrollX > maxScrollX) g_Seq.scrollX = maxScrollX;

            
            int totalHeight = get_header_height() + g_Seq.trackCount * get_track_height() + get_bottom_dock_height();
            int clientHeight = rcClient.bottom - rcClient.top;
            int maxScrollY = totalHeight - clientHeight;
            if (maxScrollY < 0) maxScrollY = 0;
            if (g_Seq.scrollY < 0) g_Seq.scrollY = 0;
            if (g_Seq.scrollY > maxScrollY) g_Seq.scrollY = maxScrollY;

            update_scrollbar(hwnd);
            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        int clientH = rcClient.bottom - rcClient.top;
        float ppb = get_pixels_per_beat();

        if (g_Seq.isDraggingClip || g_Seq.isVolumeDragging || g_Seq.isSlipDragging ||
            g_Seq.isResizingLeft || g_Seq.isResizingRight || g_Seq.isMarqueeSelecting ||
            g_Seq.isFadeInDragging || g_Seq.isFadeOutDragging) {
            g_Seq.hoveredClip = g_Seq.draggedClipIndex;
        }
        else if (mx < get_track_header_width() || my <= get_header_height() || my >= clientH - get_bottom_dock_height()) {
            g_Seq.hoveredClip = -1;
        }
        else {
            g_Seq.hoveredClip = get_clip_under_mouse(mx, my);
        }

         
        {
            RECT tsRc;
            get_timesig_badge_rect(clientH ? rcClient.right - rcClient.left : 0, clientH, &tsRc);
            if (mx >= tsRc.left && mx <= tsRc.right && my >= tsRc.top && my <= tsRc.bottom) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
            }
        }

        
        if (g_Seq.hoveredClip != oldHovered &&
            !g_Seq.isDraggingClip && !g_Seq.isMarqueeSelecting &&
            !g_Seq.isVolumeDragging && !g_Seq.isSlipDragging &&
            !g_Seq.isResizingLeft && !g_Seq.isResizingRight &&
            !g_Seq.isFadeInDragging && !g_Seq.isFadeOutDragging) {
            InvalidateRect(hwnd, NULL, FALSE);
        }

        
        if (g_Seq.isMarqueeSelecting) {
            if (!g_Seq.hasMovedPastThreshold &&
                (abs(mx - g_Seq.dragStartX) > get_drag_threshold() || abs(my - g_Seq.dragStartY) > get_drag_threshold())) {
                g_Seq.hasMovedPastThreshold = true;
                if (g_Seq.pendingPlayheadSet) {
                     
                    set_playback_frame(g_Seq.origPlaybackFrame);
                    g_Seq.pendingPlayheadSet = false;
                    g_Seq.shouldRevertPlayhead = true;
                }
            }

            int curMx = max(get_track_header_width(), mx);
            g_Seq.marqueeCurX = curMx;
            g_Seq.marqueeCurY = my;
            int mX1 = max(get_track_header_width(), min(g_Seq.marqueeStartX, curMx));
            int mX2 = max(get_track_header_width(), max(g_Seq.marqueeStartX, curMx));
            int mY1 = min(g_Seq.marqueeStartY, my);
            int mY2 = max(g_Seq.marqueeStartY, my);

            bool ctrlHeld = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

            seq_lock();
            bool selChanged = false;
            for (int i = 0; i < g_Seq.clipCount; ++i) {
                Clip* c = &g_Seq.clips[i];
                if (c->startBeat >= total_beats()) continue;
                int cX1 = get_track_header_width() - g_Seq.scrollX + (int)(c->startBeat * ppb);
                int cX2 = cX1 + (int)(c->lengthBeats * ppb);
                int cY1 = get_header_height() - g_Seq.scrollY + c->track * get_track_height();
                int cY2 = cY1 + get_track_height();

                bool inBox = !(cX2 < mX1 || cX1 > mX2 || cY2 < mY1 || cY1 > mY2);
                bool newSel = (ctrlHeld || shiftHeld) ? ((c->dragStartTrackOrig != 0) || inBox) : inBox;

                if (c->isSelected != newSel) {
                    mark_clip_bars_dirty(c);
                    c->isSelected = newSel;
                    mark_clip_bars_dirty(c);
                    selChanged = true;
                }
            }
            if (selChanged) {
                InterlockedExchange(&g_allChunksStale, 1);
            }
            seq_unlock();

            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        if (g_Seq.isFadeInDragging && g_Seq.draggedClipIndex >= 0) {
            seq_lock();
            if (g_Seq.draggedClipIndex < g_Seq.clipCount) {
                Clip* lead = &g_Seq.clips[g_Seq.draggedClipIndex];
                int cX1 = get_track_header_width() - g_Seq.scrollX + (int)(lead->startBeat * ppb);
                float newFadeBeats = (float)(mx - cX1) / ppb;
                if (newFadeBeats < 0.0f) newFadeBeats = 0.0f;
                if (newFadeBeats > lead->lengthBeats - lead->fadeOutBeats)
                    newFadeBeats = lead->lengthBeats - lead->fadeOutBeats;
                if (newFadeBeats > lead->lengthBeats) newFadeBeats = lead->lengthBeats;

                float delta = newFadeBeats - lead->dragStartBeatOrig;
                for (int i = 0; i < g_Seq.clipCount; ++i) {
                    if (g_Seq.clips[i].isSelected) {
                        Clip* c = &g_Seq.clips[i];
                        float newVal = c->dragStartBeatOrig + delta;
                        if (newVal < 0.0f) newVal = 0.0f;
                        if (newVal > c->lengthBeats - c->fadeOutBeats)
                            newVal = c->lengthBeats - c->fadeOutBeats;
                        if (newVal > c->lengthBeats) newVal = c->lengthBeats;
                        c->fadeInBeats = newVal;
                        mark_clip_bars_dirty(c);
                    }
                }
            }
            seq_unlock();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        if (g_Seq.isFadeOutDragging && g_Seq.draggedClipIndex >= 0) {
            seq_lock();
            if (g_Seq.draggedClipIndex < g_Seq.clipCount) {
                Clip* lead = &g_Seq.clips[g_Seq.draggedClipIndex];
                int cX2 = get_track_header_width() - g_Seq.scrollX + (int)((lead->startBeat + lead->lengthBeats) * ppb);
                float newFadeBeats = (float)(cX2 - mx) / ppb;
                if (newFadeBeats < 0.0f) newFadeBeats = 0.0f;
                if (newFadeBeats > lead->lengthBeats - lead->fadeInBeats)
                    newFadeBeats = lead->lengthBeats - lead->fadeInBeats;
                if (newFadeBeats > lead->lengthBeats) newFadeBeats = lead->lengthBeats;

                float delta = newFadeBeats - lead->dragStartBeatOrig;
                for (int i = 0; i < g_Seq.clipCount; ++i) {
                    if (g_Seq.clips[i].isSelected) {
                        Clip* c = &g_Seq.clips[i];
                        float newVal = c->dragStartBeatOrig + delta;
                        if (newVal < 0.0f) newVal = 0.0f;
                        if (newVal > c->lengthBeats - c->fadeInBeats)
                            newVal = c->lengthBeats - c->fadeInBeats;
                        if (newVal > c->lengthBeats) newVal = c->lengthBeats;
                        c->fadeOutBeats = newVal;
                        mark_clip_bars_dirty(c);
                    }
                }
            }
            seq_unlock();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

         
        if (g_Seq.hoveredClip != -1 && !g_Seq.isDraggingClip && !g_Seq.isMarqueeSelecting &&
            !g_Seq.isVolumeDragging && !g_Seq.isSlipDragging &&
            !g_Seq.isResizingLeft && !g_Seq.isResizingRight &&
            !g_Seq.isFadeInDragging && !g_Seq.isFadeOutDragging) {
            seq_lock();
            if (g_Seq.hoveredClip >= 0 && g_Seq.hoveredClip < g_Seq.clipCount) {
                Clip* c = &g_Seq.clips[g_Seq.hoveredClip];
                int cX1 = get_track_header_width() - g_Seq.scrollX + (int)(c->startBeat * ppb);
                int cX2 = cX1 + (int)(c->lengthBeats * ppb);
                int cY1 = get_header_height() - g_Seq.scrollY + c->track * get_track_height();
                int cY2 = cY1 + get_track_height();
                bool isMidiClip = c->isMidi;
                seq_unlock();

                bool altKeyHeld = (GetKeyState(VK_MENU) & 0x8000) != 0;
                if (altKeyHeld && !isMidiClip) {
                     
                    SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                } else {
                    ClipHitZone zone = hit_test_clip_zones(mx, my, c, cX1, cX2, cY1, cY2, ppb);
                    switch (zone) {
                    case CLIP_HIT_TRIM_LEFT:
                    case CLIP_HIT_TRIM_RIGHT:
                        SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                        break;
                    case CLIP_HIT_FADE_IN_HANDLE:
                    case CLIP_HIT_FADE_OUT_HANDLE:
                        SetCursor(LoadCursor(NULL, IDC_HAND));
                        break;
                    default:
                        SetCursor(LoadCursor(NULL, IDC_ARROW));
                        break;
                    }
                }
            }
            else {
                seq_unlock();
            }
        }

     
        if (g_Seq.isResizingLeft && g_Seq.draggedClipIndex >= 0) {
            seq_lock();
            if (g_Seq.draggedClipIndex < g_Seq.clipCount) {
                float mouseBeat = (float)(mx - get_track_header_width() + g_Seq.scrollX) / ppb;
                if (g_Seq.quantizeEnabled) mouseBeat = quantize_beat_16th(mouseBeat);

                float deltaBeats = mouseBeat - g_Seq.resizeOrigStartBeat;

                for (int i = 0; i < g_Seq.clipCount; ++i) {
                    if (!g_Seq.clips[i].isSelected) continue;
                    Clip* c = &g_Seq.clips[i];
                    if (c->sampleIndex < 0 || c->sampleIndex >= g_Seq.sampleCount) continue;

                    float origRightEdge = c->dragStartBeatOrig + c->dragStartLengthOrig;
                    float newStart = c->dragStartBeatOrig + deltaBeats;
                    if (newStart < 0.0f) newStart = 0.0f;

                    float minLen = get_min_clip_length_beats();
                    float newLen = origRightEdge - newStart;
                    if (newLen < minLen) {
                        newLen = minLen;
                        newStart = origRightEdge - newLen;
                        if (newStart < 0.0f) newStart = 0.0f;
                    }

                    mark_clip_bars_dirty(c);
                    c->startBeat = newStart;
                    c->lengthBeats = newLen;
                    if (c->fadeInBeats > c->lengthBeats)  c->fadeInBeats = c->lengthBeats;
                    if (c->fadeOutBeats > c->lengthBeats) c->fadeOutBeats = c->lengthBeats;
                    mark_clip_bars_dirty(c);

                    AudioSample* s = &g_Seq.samples[c->sampleIndex];
                    float fpb = frames_per_beat(g_Seq.bpm);
                    float pRate = (c->playbackRate > 0.01f) ? c->playbackRate : 1.0f;
                    long long newOffset = (long long)c->dragStartOffsetOrig + (long long)((newStart - c->dragStartBeatOrig) * fpb * pRate);
                    if (s->frameCount > 0) {
                        newOffset %= (long long)s->frameCount;
                        if (newOffset < 0) newOffset += (long long)s->frameCount;
                    } else {
                        newOffset = 0;
                    }
                    c->sampleOffsetFrames = find_nearest_zero_crossing(s, (ma_uint64)newOffset, 128);
                }
                InterlockedExchange(&g_allChunksStale, 1);
            }
            seq_unlock();
             
            mark_selected_clips_bars_dirty();
            g_timelineDirty = true;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

         
        if (g_Seq.isResizingRight && g_Seq.draggedClipIndex >= 0) {
            seq_lock();
            if (g_Seq.draggedClipIndex < g_Seq.clipCount) {
                float mouseBeat = (float)(mx - get_track_header_width() + g_Seq.scrollX) / ppb;
                if (g_Seq.quantizeEnabled) mouseBeat = quantize_beat_16th(mouseBeat);

                float deltaBeats = mouseBeat - (g_Seq.resizeOrigStartBeat + g_Seq.resizeOrigLengthBeats);

                for (int i = 0; i < g_Seq.clipCount; ++i) {
                    if (!g_Seq.clips[i].isSelected) continue;
                    Clip* c = &g_Seq.clips[i];

                    float minLen = get_min_clip_length_beats();
                    float newLen = c->dragStartLengthOrig + deltaBeats;
                    float tlLen = total_beats() - c->startBeat;
                    if (newLen < minLen) newLen = minLen;
                    if (newLen > tlLen) newLen = tlLen;
                    if (newLen < minLen && minLen <= tlLen) newLen = minLen;

                    mark_clip_bars_dirty(c);
                    c->lengthBeats = newLen;
                    if (c->fadeInBeats > c->lengthBeats)  c->fadeInBeats = c->lengthBeats;
                    if (c->fadeOutBeats > c->lengthBeats) c->fadeOutBeats = c->lengthBeats;
                    mark_clip_bars_dirty(c);

                    // Stretch mode (Shift held): keep the same audio content
                    // spanned over the new length by adjusting playback rate,
                    // so the waveform visually compresses/expands to match.
                    if (g_Seq.isStretchResizing && !c->isMidi &&
                        c->sampleIndex >= 0 && c->sampleIndex < g_Seq.sampleCount) {
                        float origLen = c->dragStartLengthOrig;
                        if (origLen > 0.01f && newLen > 0.01f) {
                            float newRate = g_Seq.resizeOrigRate * (origLen / newLen);
                            if (newRate < 0.01f) newRate = 0.01f;
                            if (newRate > 2.00f) newRate = 2.00f;
                            c->playbackRate = newRate;
                        }
                    }
                }
                InterlockedExchange(&g_allChunksStale, 1);
            }
            seq_unlock();
             
            mark_selected_clips_bars_dirty();
            g_timelineDirty = true;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        if (!g_Seq.hasMovedPastThreshold) {
            if (abs(mx - g_Seq.dragStartX) > get_drag_threshold() / 2 || abs(my - g_Seq.dragStartY) > get_drag_threshold() / 2) {
                g_Seq.hasMovedPastThreshold = true;
                 
                g_Seq.pendingSingleSelectClip = -1;
                push_undo_state();
                if (g_Seq.isDraggingClip && g_Seq.isCtrlDuplicating) {
                    seq_lock();
                    int originalCount = g_Seq.clipCount;
                    int newLeadIdx = g_Seq.draggedClipIndex;
                    for (int i = 0; i < originalCount; ++i) {
                        if (g_Seq.clips[i].isSelected && g_Seq.clipCount < MAX_CLIPS) {
                            int cloneIdx = g_Seq.clipCount++;
                            g_Seq.clips[cloneIdx] = g_Seq.clips[i];
                            g_Seq.clips[cloneIdx].isSelected = true;
                            g_Seq.clips[i].isSelected = false;

                            g_ClipGran[cloneIdx] = g_ClipGran[i];
                            g_ClipGran[cloneIdx].clipIdx = cloneIdx;
                            g_ClipGran[cloneIdx].ownFrames = NULL;
                            g_ClipGran[cloneIdx].ownLoaded = false;
                            memset(g_ClipGran[cloneIdx].grains, 0, sizeof(g_ClipGran[cloneIdx].grains));

                            if (i == g_Seq.draggedClipIndex) newLeadIdx = cloneIdx;
                        }
                    }
                    g_Seq.draggedClipIndex = newLeadIdx;
                    g_Seq.isCtrlDuplicating = false;
                    seq_unlock();
                    cseq_clip_structure_changed();
                }
            }
        }

        // Track-header drag reorder (Shift+drag).
        if (g_Seq.isTrackHeaderDragging) {
            if (!g_Seq.hasMovedPastThreshold) {
                if (abs(mx - g_Seq.dragStartX) > get_drag_threshold() ||
                    abs(my - g_Seq.dragStartY) > get_drag_threshold()) {
                    g_Seq.hasMovedPastThreshold = true;
                }
            }

            // Compute the target track from the pointer, clamped to range.
            int t = (my - get_header_height() + g_Seq.scrollY) / get_track_height();
            if (t < 0) t = 0;
            if (t >= g_Seq.trackCount) t = g_Seq.trackCount - 1;
            g_Seq.dragTrackCur = t;

            // Auto-scroll near the viewport top/bottom so off-screen tracks
            // come into view while dragging.
            RECT rcClientTrack;
            GetClientRect(hwnd, &rcClientTrack);
            int viewTop = get_header_height();
            int viewBottom = rcClientTrack.bottom - rcClientTrack.top - get_bottom_dock_height();
            if (my < viewTop + get_track_height()) {
                g_Seq.scrollY -= get_track_height();
                update_scrollbar(hwnd);
            } else if (my > viewBottom - get_track_height()) {
                g_Seq.scrollY += get_track_height();
                update_scrollbar(hwnd);
            }

            g_timelineDirty = true;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

         
        if (g_Seq.isSlipDragging && g_Seq.draggedClipIndex >= 0 && g_Seq.hasMovedPastThreshold) {
            int dx = mx - g_Seq.dragStartX;
            float beatDelta = (float)dx / ppb;
            float fpb = frames_per_beat(g_Seq.bpm);

            seq_lock();
            for (int i = 0; i < g_Seq.clipCount; ++i) {
                 
                if (g_Seq.clips[i].isSelected || i == g_Seq.draggedClipIndex) {
                    Clip* c = &g_Seq.clips[i];
                    if (c->isMidi) continue;  
                    if (c->sampleIndex < 0 || c->sampleIndex >= g_Seq.sampleCount) continue;

                    AudioSample* s = &g_Seq.samples[c->sampleIndex];
                     
                    if (!s->loaded || s->frameCount <= 0) continue;
                    float pRate = (c->playbackRate > 0.01f) ? c->playbackRate : 1.0f;
                    int frameDelta = (int)(beatDelta * fpb * pRate);

                     
                    long long startOffset = (i == g_Seq.draggedClipIndex)
                                                ? (long long)g_Seq.dragStartSampleOffset
                                                : (long long)c->dragStartOffsetOrig;
                    long long newOffset = startOffset - frameDelta;
                    if (s->frameCount > 0) {
                        newOffset %= (long long)s->frameCount;
                        if (newOffset < 0) newOffset += (long long)s->frameCount;
                    } else {
                        newOffset = 0;
                    }

                    mark_clip_bars_dirty(c);
                    c->sampleOffsetFrames = find_nearest_zero_crossing(s, (ma_uint64)newOffset, 128);
                    mark_clip_bars_dirty(c);
                }
            }
            InterlockedExchange(&g_allChunksStale, 1);
            seq_unlock();

            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        else if (g_Seq.isVolumeDragging && g_Seq.draggedClipIndex >= 0) {
            int dy = g_Seq.dragStartY - my;
            float newVol = g_Seq.dragStartVolume + (float)dy * 0.01f;
            if (newVol < 0.0f) newVol = 0.0f;
            if (newVol > 2.0f) newVol = 2.0f;

            seq_lock();
            if (g_Seq.draggedClipIndex < g_Seq.clipCount) {
                g_Seq.clips[g_Seq.draggedClipIndex].volume = newVol;
                mark_clip_bars_dirty(&g_Seq.clips[g_Seq.draggedClipIndex]);
            }
            seq_unlock();

            g_Seq.volumePopupClip = g_Seq.draggedClipIndex;
            g_Seq.volumePopupExpiry = GetTickCount64() + 1500;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
         
        else if (g_Seq.isDraggingClip && g_Seq.draggedClipIndex >= 0 && g_Seq.hasMovedPastThreshold) {
            float mouseBeat = (float)(mx - get_track_header_width() + g_Seq.scrollX) / ppb;
            float newLeadBeat = mouseBeat - g_Seq.dragStartBeatOffset;
            if (g_Seq.quantizeEnabled) newLeadBeat = quantize_beat_16th(newLeadBeat);

            int newLeadTrack = (my - get_header_height() + g_Seq.scrollY) / get_track_height();

            seq_lock();
            if (g_Seq.draggedClipIndex < g_Seq.clipCount) {
                float rawBeatDelta = newLeadBeat - g_Seq.clips[g_Seq.draggedClipIndex].dragStartBeatOrig;
                int rawTrackDelta = newLeadTrack - g_Seq.clips[g_Seq.draggedClipIndex].dragStartTrackOrig;

                float minAllowedBeatDelta = -1e9f;
                float maxAllowedBeatDelta = 1e9f;
                int minAllowedTrackDelta = -999;
                int maxAllowedTrackDelta = 999;

                for (int i = 0; i < g_Seq.clipCount; ++i) {
                    if (g_Seq.clips[i].isSelected) {
                        float leftBoundDelta = -g_Seq.clips[i].dragStartBeatOrig;
                        if (leftBoundDelta > minAllowedBeatDelta) minAllowedBeatDelta = leftBoundDelta;

                        float rightBoundDelta = total_beats() - (g_Seq.clips[i].dragStartBeatOrig + g_Seq.clips[i].lengthBeats);
                        if (rightBoundDelta < maxAllowedBeatDelta) maxAllowedBeatDelta = rightBoundDelta;

                        int minTDelta = -g_Seq.clips[i].dragStartTrackOrig;
                        int maxTDelta = (g_Seq.trackCount - 1) - g_Seq.clips[i].dragStartTrackOrig;

                        if (minTDelta > minAllowedTrackDelta) minAllowedTrackDelta = minTDelta;
                        if (maxTDelta < maxAllowedTrackDelta) maxAllowedTrackDelta = maxTDelta;
                    }
                }

                float finalBeatDelta = rawBeatDelta;
                if (finalBeatDelta < minAllowedBeatDelta) finalBeatDelta = minAllowedBeatDelta;
                if (finalBeatDelta > maxAllowedBeatDelta) finalBeatDelta = maxAllowedBeatDelta;

                int finalTrackDelta = rawTrackDelta;
                if (finalTrackDelta < minAllowedTrackDelta) finalTrackDelta = minAllowedTrackDelta;
                if (finalTrackDelta > maxAllowedTrackDelta) finalTrackDelta = maxAllowedTrackDelta;

                for (int i = 0; i < g_Seq.clipCount; ++i) {
                    if (g_Seq.clips[i].isSelected) {
                        mark_clip_bars_dirty(&g_Seq.clips[i]); 

                        float b = g_Seq.clips[i].dragStartBeatOrig + finalBeatDelta;
                        if (b < 0.0f) b = 0.0f;
                        int t = g_Seq.clips[i].dragStartTrackOrig + finalTrackDelta;
                        if (t < 0) t = 0;
                        if (t >= g_Seq.trackCount) t = g_Seq.trackCount - 1;

                        g_Seq.clips[i].startBeat = b;
                        g_Seq.clips[i].track = t;

                        mark_clip_bars_dirty(&g_Seq.clips[i]); 
                    }
                }
                InterlockedExchange(&g_allChunksStale, 1);
            }
            seq_unlock();
             
            mark_selected_clips_bars_dirty();
            g_timelineDirty = true;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        g_Seq.hoveredClip = -1;
        g_Seq.mouseX = -1;
        g_Seq.mouseY = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONUP: {
        cseq_sb_end_drag(hwnd);

        // Track-header drag (Shift+drag): commit the reorder.
        if (g_Seq.isTrackHeaderDragging) {
            if (g_Seq.hasMovedPastThreshold) {
                reorder_track(g_Seq.dragTrackOrig, g_Seq.dragTrackCur);
            }
            g_Seq.isTrackHeaderDragging = false;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
        }

        if (g_Seq.isCtrlDuplicating && !g_Seq.hasMovedPastThreshold && g_Seq.draggedClipIndex >= 0) {
            seq_lock();
            if (g_Seq.draggedClipIndex < g_Seq.clipCount) {
                 
                g_Seq.clips[g_Seq.draggedClipIndex].isSelected = !g_Seq.ctrlClickOrigSelected;
            }
            seq_unlock();
             
            g_timelineDirty = true;
        }

         
        if (g_Seq.pendingSingleSelectClip >= 0) {
            seq_lock();
            if (g_Seq.pendingSingleSelectClip < g_Seq.clipCount) {
                for (int i = 0; i < g_Seq.clipCount; ++i)
                    g_Seq.clips[i].isSelected = (i == g_Seq.pendingSingleSelectClip);
            }
            g_Seq.pendingSingleSelectClip = -1;
            seq_unlock();
             
            g_timelineDirty = true;
            InvalidateRect(hwnd, NULL, FALSE);
        }

        if (g_Seq.isDraggingClip || g_Seq.isVolumeDragging || g_Seq.isSlipDragging ||
            g_Seq.isMarqueeSelecting || g_Seq.isResizingLeft || g_Seq.isResizingRight ||
            g_Seq.isFadeInDragging || g_Seq.isFadeOutDragging) {
            g_Seq.isDraggingClip = false;
            g_Seq.isCtrlDuplicating = false;
            g_Seq.isVolumeDragging = false;
            g_Seq.isSlipDragging = false;
            g_Seq.isMarqueeSelecting = false;
            g_Seq.isResizingLeft = false;
            g_Seq.isResizingRight = false;
            g_Seq.isStretchResizing = false;
            g_Seq.isFadeInDragging = false;
            g_Seq.isFadeOutDragging = false;
            g_Seq.pendingPlayheadSet = false;
            g_Seq.pendingSingleSelectClip = -1;
            g_Seq.draggedClipIndex = -1;
             
            cseq_clip_structure_changed();
            g_timelineDirty = true;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_SYSKEYDOWN: {
         
        if (GetKeyState(VK_MENU) & 0x8000) {
            if (wParam == VK_OEM_PERIOD) { pan_timeline_bars(hwnd, 8);  return 0; }
            if (wParam == VK_OEM_COMMA)  { pan_timeline_bars(hwnd, -8); return 0; }
        }
        break;  
    }

    case WM_KEYDOWN: {
        switch (wParam) {
        case 'X':
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                copy_selected_clips();
                delete_selected_clips();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        case 'Y':
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                redo_last_action();
            }
            break;
        case 'Z':
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (GetKeyState(VK_SHIFT) & 0x8000) {
                    redo_last_action();
                }
                else {
                    undo_last_action();
                }
            }
            break;
        case 'S':
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                 
                if (g_Seq.currentProjectFile[0]) {
                    save_project_to_csq(g_Seq.currentProjectFile);
                }
                else {
                    save_project_dialog(hwnd);
                }
            }
            else if (GetKeyState(VK_SHIFT) & 0x8000) {
                 
                int soloTrack = get_solo_target_track();
                if (soloTrack >= 0) {
                    seq_lock();
                    seq_set_track_solo(soloTrack, !g_Seq.trackSolo[soloTrack]);
                    seq_unlock();
                    g_timelineDirty = true;
                }
            }
            else {
                 
                split_clip_at_mouse_or_playhead();
            }
            break;
        case 'O':
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (g_Seq.isModified && (g_Seq.clipCount > 0 || g_Seq.currentProjectFile[0] != '\0') && !job_is_busy()) {
                    g_confirmLoadPath[0] = '\0';
                    open_confirm_dialog(hwnd, CONFIRM_LOAD);
                }
                else {
                    load_project_dialog(hwnd);
                }
            }
            break;
        case 'E': {
             
            OPENFILENAMEW ofn;
            wchar_t szFileW[MAX_PATH] = L"export.wav";
            if (g_Seq.currentProjectName[0]) {
                wchar_t projW[MAX_PATH];
                if (utf8_to_wide_buf(g_Seq.currentProjectName, projW, MAX_PATH) > 0) {
                    wchar_t* dot = wcsrchr(projW, L'.');
                    if (dot) *dot = L'\0';
                    _snwprintf(szFileW, MAX_PATH, L"%s.wav", projW);
                    szFileW[MAX_PATH - 1] = L'\0';
                }
            }
            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = L"WAV Audio (*.wav)\0*.wav\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = szFileW;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            ofn.lpstrDefExt = L"wav";

            if (GetSaveFileNameW(&ofn)) {
                char szFile[MAX_PATH];
                if (wide_to_utf8_buf(szFileW, szFile, MAX_PATH) > 0) {
                    export_timeline_to_wav(szFile);
                }
            }
            break;
        }
        case 'A':
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                seq_lock();
                for (int i = 0; i < g_Seq.clipCount; ++i) {
                    g_Seq.clips[i].isSelected = true;
                }
                seq_unlock();
                 
                g_timelineDirty = true;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        case 'K': {
            // Open keybinds menu - no modifiers needed
            if (!(GetKeyState(VK_CONTROL) & 0x8000) && 
                !(GetKeyState(VK_MENU) & 0x8000) &&
                !(GetKeyState(VK_SHIFT) & 0x8000)) {
                open_keybinds_dialog(hwnd);
            }
            break;
        }        
        case 'D':
             
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                deselect_all_clips();
                g_timelineDirty = true;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        
        case VK_ESCAPE:
            SendMessageA(hwnd, WM_CLOSE, 0, 0);
            return 0;
        case VK_LEFT: {
            float ppb = get_pixels_per_beat();
            int totalTimelineWidth = (int)(total_beats() * ppb);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int visibleWidth = (rc.right - rc.left) - get_track_header_width();
            int maxScrollX = totalTimelineWidth - visibleWidth;
            if (maxScrollX > 0 && g_Seq.scrollX > 0) {
                g_Seq.scrollX -= (int)ppb;
                if (g_Seq.scrollX < 0) g_Seq.scrollX = 0;
                g_timelineDirty = true;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }
        case VK_RIGHT: {
            float ppb = get_pixels_per_beat();
            int totalTimelineWidth = (int)(total_beats() * ppb);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int visibleWidth = (rc.right - rc.left) - get_track_header_width();
            int maxScrollX = totalTimelineWidth - visibleWidth;
            if (maxScrollX > 0 && g_Seq.scrollX < maxScrollX) {
                g_Seq.scrollX += (int)ppb;
                if (g_Seq.scrollX > maxScrollX) g_Seq.scrollX = maxScrollX;
                g_timelineDirty = true;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }
        case VK_UP:
            g_Seq.scrollY -= get_track_height();
            g_timelineDirty = true;
            update_scrollbar(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        case VK_DOWN:
            g_Seq.scrollY += get_track_height();
            g_timelineDirty = true;
            update_scrollbar(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        case VK_SPACE:
        case VK_RETURN:
            play_with_start_reset(); 
            break;
        case 'Q':
            g_Seq.quantizeEnabled = !g_Seq.quantizeEnabled;
            break;
        case 'L':
            g_Seq.isLofi = !g_Seq.isLofi;
            break;
        case VK_OEM_2:   // '/' opens the Media Explorer
            if (!(GetKeyState(VK_CONTROL) & 0x8000) &&
                !(GetKeyState(VK_MENU) & 0x8000) &&
                !(GetKeyState(VK_SHIFT) & 0x8000)) {
                open_media_explorer(hwnd);
            }
            break;
        case 'G': {
            if (g_Seq.mouseY > get_header_height()) {
                int clipIdx = get_clip_under_mouse(g_Seq.mouseX, g_Seq.mouseY);
                if (clipIdx >= 0 && clipIdx < g_Seq.clipCount) {
                    granular_toggle_clip(clipIdx);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            break;
        }
        case VK_PRIOR: {
            float newZoom = g_Seq.zoom + 0.25f;
            if (newZoom > 8.0f) newZoom = 8.0f;
            zoom_to_cursor(hwnd, newZoom);
            break;
        }
        case 'M': {
            if ((GetKeyState(VK_SHIFT) & 0x8000) && !(GetKeyState(VK_CONTROL) & 0x8000)) {

                int targetTrack = get_solo_target_track();
                if (targetTrack < 0) targetTrack = 0;
                if (targetTrack >= g_Seq.trackCount) targetTrack = g_Seq.trackCount - 1;

                // Insert at the cursor's timeline beat when hovering over the
                // track canvas; fall back to the playhead when the mouse is
                // over headers/dock where the grid isn't meaningful.
                float targetBeat = 0.0f;
                float ppb = get_pixels_per_beat();

                RECT rcClient;
                GetClientRect(hwnd, &rcClient);
                int clientH = rcClient.bottom - rcClient.top;

                if (g_Seq.mouseX >= get_track_header_width() &&
                    g_Seq.mouseY > get_header_height() &&
                    g_Seq.mouseY < clientH - get_bottom_dock_height() &&
                    ppb > 0.0f) {
                    targetBeat = (float)(g_Seq.mouseX - get_track_header_width() + g_Seq.scrollX) / ppb;
                } else {
                    LONG currentFrame = InterlockedCompareExchange(&g_Seq.playbackFrame, 0, 0);
                    targetBeat = frame_to_beat((ma_uint64)currentFrame, g_Seq.bpm, g_Seq.swing);
                }

                insert_midi_clip(targetTrack, targetBeat);
                InvalidateRect(hwnd, NULL, FALSE);
                break;
            }
            if (g_Seq.mouseY > get_header_height()) {
                int clipIdx = get_clip_under_mouse(g_Seq.mouseX, g_Seq.mouseY);
                if (clipIdx >= 0 && clipIdx < g_Seq.clipCount) {
                    seq_lock();
                    if (g_Seq.clips[clipIdx].isSelected) {
                        for (int i = 0; i < g_Seq.clipCount; ++i) {
                            if (g_Seq.clips[i].isSelected) {
                                g_Seq.clips[i].isMuted = !g_Seq.clips[i].isMuted;
                                mark_clip_bars_dirty(&g_Seq.clips[i]);
                            }
                        }
                    }
                    else {
                        g_Seq.clips[clipIdx].isMuted = !g_Seq.clips[clipIdx].isMuted;
                        mark_clip_bars_dirty(&g_Seq.clips[clipIdx]);
                    }
                    seq_unlock();
                    g_timelineDirty = true;
                }
                else if (g_Seq.mouseX < get_track_header_width()) {
                    int track = (g_Seq.mouseY - get_header_height() + g_Seq.scrollY) / get_track_height();
                    if (track >= 0 && track < g_Seq.trackCount && track < MAX_TRACKS) {
                        seq_lock();
                        seq_set_track_mute(track, !g_Seq.trackMuted[track]);
                        seq_unlock();
                        g_timelineDirty = true;
                    }
                }
            }
            break;
        }
        case 'N': {
            if (!(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
                int targetClip = -1;
                if (g_Seq.mouseY > get_header_height()) {
                    int hovClip = get_clip_under_mouse(g_Seq.mouseX, g_Seq.mouseY);
                    if (hovClip >= 0 && hovClip < g_Seq.clipCount && g_Seq.clips[hovClip].isMidi) {
                        targetClip = hovClip;
                    }
                }
                if (targetClip < 0) {
                    
                    seq_lock();
                    for (int i = 0; i < g_Seq.clipCount; ++i) {
                        if (g_Seq.clips[i].isSelected && g_Seq.clips[i].isMidi) {
                            targetClip = i;
                            break;
                        }
                    }
                    seq_unlock();
                }
                if (targetClip >= 0) {
                    open_midi_editor(hwnd, targetClip);
                }
            }
            break;
        }
        case 'F': {
             
            if (!(GetKeyState(VK_CONTROL) & 0x8000) &&
                !(GetKeyState(VK_MENU) & 0x8000) &&
                !(GetKeyState(VK_SHIFT) & 0x8000)) {
                int fxTrack = get_solo_target_track();
                if (fxTrack >= 0) open_fx_rack_dialog(hwnd, fxTrack);
            }
            break;
        }
        case VK_NEXT: {
            float newZoom = g_Seq.zoom - 0.25f;
            if (newZoom < 0.25f) newZoom = 0.25f;
            zoom_to_cursor(hwnd, newZoom);
            break;
        }
        case 'T':
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                add_track_action(hwnd);
            }
            else if (GetKeyState(VK_SHIFT) & 0x8000) {
                remove_track_action(hwnd);
            }
            break;
        case VK_HOME:
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                g_Seq.playFromStartOnPlay = !g_Seq.playFromStartOnPlay;
            }
            else {
                set_playback_frame(0);
            }
            break;
        case VK_END:
            seq_set_playing(false);
            set_playback_frame(0);
            granular_stop_all();
            synth_stop_all();
            break;
        case VK_DELETE:
            delete_selected_clips();
            break;
        case VK_INSERT: {
            int targetTrack = 0;
            if (g_Seq.mouseY >= get_header_height()) {
                targetTrack = (g_Seq.mouseY - get_header_height() + g_Seq.scrollY) / get_track_height();
            }
            if (targetTrack < 0) targetTrack = 0;
            if (targetTrack >= g_Seq.trackCount) targetTrack = g_Seq.trackCount - 1;

            LONG currentFrame = InterlockedCompareExchange(&g_Seq.playbackFrame, 0, 0);
            float curBeat = frame_to_beat((ma_uint64)currentFrame, g_Seq.bpm, g_Seq.swing);
            open_sample_dialog(hwnd, targetTrack, quantize_beat_16th(curBeat));
            break;
        }
        case VK_OEM_PLUS:
        case VK_ADD:
            g_Seq.bpm += 1.0f;
            if (g_Seq.bpm > 300.0f) g_Seq.bpm = 300.0f;
            break;
        case VK_OEM_MINUS:
        case VK_SUBTRACT:
            g_Seq.bpm -= 1.0f;
            if (g_Seq.bpm < 40.0f) g_Seq.bpm = 40.0f;
            break;
        case VK_OEM_PERIOD: {
            bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool altHeld   = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (altHeld)        pan_timeline_bars(hwnd, 8);
            else if (shiftHeld) change_bar_count_even4(4);
             
            break;
        }
        case VK_OEM_COMMA: {
            bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool altHeld   = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (altHeld)        pan_timeline_bars(hwnd, -8);
            else if (shiftHeld) change_bar_count_even4(-4);
            break;
        }
        case VK_OEM_4:
            g_Seq.swing -= 0.05f;
            if (g_Seq.swing < 0.0f) g_Seq.swing = 0.0f;
            break;
        case VK_OEM_6:
            g_Seq.swing += 0.05f;
            if (g_Seq.swing > 0.95f) g_Seq.swing = 0.95f;
            break;
        case 'C':
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                copy_selected_clips();
            }
            break;
        case 'V':
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                paste_clipboard_clips();
            }
            break;
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

     
    case WM_CLOSE:
        if (g_Seq.isModified && (g_Seq.clipCount > 0 || g_Seq.currentProjectFile[0] != '\0')) {
            open_confirm_dialog(hwnd, CONFIRM_QUIT);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY: {
        KillTimer(hwnd, 1);

         
        stop_main_120fps_pacer();

         
        if (g_visHwnd && IsWindow(g_visHwnd))               DestroyWindow(g_visHwnd);
        if (g_granHwnd && IsWindow(g_granHwnd))             DestroyWindow(g_granHwnd);
        if (g_confirmHwnd && IsWindow(g_confirmHwnd))       DestroyWindow(g_confirmHwnd);
        if (g_keybindsHwnd && IsWindow(g_keybindsHwnd))     DestroyWindow(g_keybindsHwnd);
        if (g_PanWidthWin.hwnd && IsWindow(g_PanWidthWin.hwnd)) DestroyWindow(g_PanWidthWin.hwnd);
        if (g_LofiWin.hwnd && IsWindow(g_LofiWin.hwnd))     DestroyWindow(g_LofiWin.hwnd);
        if (g_RateWin.hwnd && IsWindow(g_RateWin.hwnd))     DestroyWindow(g_RateWin.hwnd);
        if (g_eqHwnd && IsWindow(g_eqHwnd))                 DestroyWindow(g_eqHwnd);
        if (g_midiHwnd && IsWindow(g_midiHwnd))             DestroyWindow(g_midiHwnd);
        if (g_bpmHwnd && IsWindow(g_bpmHwnd))               DestroyWindow(g_bpmHwnd);
        if (g_barsHwnd && IsWindow(g_barsHwnd))             DestroyWindow(g_barsHwnd);

         
        shutdown_render_surfaces();
        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}
