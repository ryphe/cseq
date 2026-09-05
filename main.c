#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>

#pragma warning(push)
#pragma warning(disable: 4244)
#include "miniaudio.h"
#pragma warning(pop)

#include "eq.h"
#include "font.h"
#include "dpi.h"
#include "config.h"
#include "types.h"
#include "soundfont.h"
#include "globals.h"
#include "dsp.h"
#include "fx.h"
#include "visualizer.h"
#include "codec.h"
#include "audition.h"
#include "slicing.h"
#include "ui.h"
#include "audio.h"
#include "synth.h"
#include "synthui.h"
#include "project.h"
#include "state.h"
#include "actions.h"
#include "samplecache.h"
#include "dialogs.h"
#include "media.h"
#include "events.h"

SequencerState g_Seq;
HWND g_hWnd = NULL;

volatile LONG g_shuttingDown = 0;
volatile LONG g_haloActiveVoices = 0;

 
GranularEngine  g_TrackGran[MAX_TRACKS];
GranularEngine  g_ClipGran[MAX_CLIPS];
 
SynthHaloState    g_ClipHalo[MAX_CLIPS];
SynthQuadrumState g_ClipQuadrum[MAX_CLIPS];
 
FxChain         g_TrackFx[MAX_TRACKS];
GranNote        g_granNoteClipboard[GRAN_MAX_NOTES];
int             g_granNoteClipboardCount = 0;
HWND            g_granHwnd = NULL;
int             g_granTrack = -1;
int             g_granClip = -1;

 
HWND             g_midiHwnd = NULL;
MidiEditContext  g_midiEdit = { .pendingSingleSelectNote = -1 };
CRITICAL_SECTION g_midiLock;

 
HWND g_synthHwnd = NULL;

// Interactive transient-slice preview state (see slicing.h).
SlicePreviewState g_slicePreview = { 0 };

 
HANDLE        g_hMainPacerThread     = NULL;
volatile LONG g_mainPacerRunning     = 0;
volatile LONG g_timelineDynamicDirty = 0;

 
VisualizerState g_Vis = {
    .mode      = VIS_MODE_COMBO,
    .fftSize   = 1024,
    .zoom      = 1.0f,
    .hue       = 185.0f,  
    .channels  = VIS_CH_STEREO,
    .isFrozen  = false
};
HWND g_visHwnd = NULL;
volatile LONG g_visBadgeHover = 0;

 
float g_dpiScaleX = 1.0f;
float g_dpiScaleY = 1.0f;

 
HFONT g_hFontUI = NULL;

 
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        render_ui(hdc, &rc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return cseq_main_wndproc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine;

     
    cseq_enable_process_dpi_awareness();

    memset(&g_Seq, 0, sizeof(g_Seq));
    
    memset(&g_sbState, 0, sizeof(g_sbState));

    g_Seq.pendingPlayheadSet = false;
    g_Seq.pendingPlayheadFrame = 0;

    
    if (!init_ui_font()) {
        
        g_hFontUI = cseq_build_ui_font_locked();
        if (!g_hFontUI) {
            
            g_hFontUI = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        }
    }

    
    granular_init_all();

    
    synth_state_reset_all();

    
    fx_init_all();

    InitializeCriticalSection(&g_Seq.lock);
    InitializeCriticalSection(&g_midiLock);

    // Keep the persistent PCM cache directory bounded (deletes oldest files if
    // it has grown past the cap since the last run).
    sample_cache_evict_if_large();

     
    seq_sync_track_masks();
    cseq_clip_structure_changed();

     
    MFStartup(MF_VERSION, MFSTARTUP_LITE);

    g_Seq.bpm = 120.0f; g_Seq.trackCount = 4; g_Seq.visibleBarCount = 4; g_Seq.zoom = 1.0f;
    g_Seq.lofiBitDepth   = 12; g_Seq.lofiSampleRate = 14700.0f;
    g_Seq.timeSigNum = 4; g_Seq.timeSigDen = 4;    

     
    g_Seq.masterVolume  = 1.0f;
    g_Seq.masterMode    = 1;   

    g_Seq.quantizeEnabled = true;
     
    g_Seq.exportBitDepth = 32;
    g_Seq.isModified = false;
    update_window_title();

     
    audio_detect_cpu_features();

    
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

        
        g_Seq.trackEqFreq[t][0] = 0.20f;
        g_Seq.trackEqFreq[t][1] = 0.50f;
        g_Seq.trackEqFreq[t][2] = 0.80f;
        g_Seq.trackEqQ[t][0] = g_Seq.trackEqQ[t][1] = g_Seq.trackEqQ[t][2] = 0.7f;

        for (int b = 0; b < 3; ++b) {
            peak_biquad_clear(&g_Seq.trackPeak[t][b]);
            peak_biquad_set(&g_Seq.trackPeak[t][b], 1000.0f, 0.7f, 0.0f, (float)SAMPLE_RATE);
        }

        smooth_eq3_init(&g_Seq.trackEQ[t], (double)SAMPLE_RATE);
        smooth_eq3_set_params(&g_Seq.trackEQ[t], 0.5f, 0.5f, 0.5f);

        track_filter_init_defaults(&g_Seq.trackFilter[t]);
    }

    
    
    ma_device_config devCfg = ma_device_config_init(ma_device_type_playback);
    devCfg.playback.format = ma_format_f32; devCfg.playback.channels = NUM_CHANNELS; devCfg.sampleRate = SAMPLE_RATE;
    devCfg.dataCallback = audio_callback;
    // Pre-allocate the master limiter's lookahead buffers so the first audio
    // callback never mallocs on the realtime thread.
    audio_limiter_preinit();
    // Pre-allocate the Media Explorer audition ping-pong buffers (static, so
    // this just guarantees they are zeroed before the device starts).
    audition_preinit();
    if (ma_device_init(NULL, &devCfg, &g_Seq.device) == MA_SUCCESS) {
        if (ma_device_start(&g_Seq.device) == MA_SUCCESS) {
            g_Seq.deviceInitialized = true;
        } else {
            ma_device_uninit(&g_Seq.device);
        }
    }

    
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "cseqMainWindow";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.style = CS_DBLCLKS;    

    
    wc.hIcon = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(1), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    RegisterClassA(&wc);

    
    g_hWnd = CreateWindowExA(0, "cseqMainWindow", "cseq", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1220, 510, NULL, NULL, hInstance, NULL);
    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

     
    cseq_apply_dpi_for_window(g_hWnd);
    refresh_ui_font_for_dpi();
    invalidate_timeline_cache();
    update_window_title();
    InvalidateRect(g_hWnd, NULL, FALSE);

     
    if (!g_Seq.deviceInitialized) {
        MessageBoxA(g_hWnd,
                    "No audio output device is available.\n\n"
                    "Playback will be silent. Connect or enable an output "
                    "device (check Windows sound settings) and restart cseq.",
                    "cseq - audio device",
                    MB_OK | MB_ICONWARNING);
    }

     
    if (lpCmdLine && lpCmdLine[0]) {
        char argPath[MAX_PATH] = "";
        const char* p = lpCmdLine;
        while (*p == ' ' || *p == '\t') ++p;

        if (*p) {
            size_t n = 0;
            if (*p == '"') {
                p++;
                while (p[n] && p[n] != '"') n++;
            } else {
                while (p[n] && p[n] != ' ' && p[n] != '\t') n++;
            }
            if (n >= sizeof(argPath)) n = sizeof(argPath) - 1;
            memcpy(argPath, p, n);
            argPath[n] = '\0';
        }

        if (argPath[0]) {
            HANDLE hProbe = CreateFileA(argPath, GENERIC_READ, FILE_SHARE_READ,
                                        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            bool fileExists = (hProbe != INVALID_HANDLE_VALUE);
            if (fileExists) CloseHandle(hProbe);

            if (fileExists) {
                const char* dot = strrchr(argPath, '.');
                bool loaded = false;

                 
                if (dot && _stricmp(dot, ".csq") == 0) {
                    if (!job_is_busy()) {
                        load_project_from_csq(argPath);
                        loaded = true;
                    }
                }

                 
                if (!loaded && dot) {
                    static const char* kAudioExts[] = {
                        ".wav", ".aiff", ".aif", ".flac", ".mp3", ".m4a", ".wma", ".ogg"
                    };
                    for (int e = 0; e < (int)(sizeof(kAudioExts) / sizeof(kAudioExts[0])); ++e) {
                        if (_stricmp(dot, kAudioExts[e]) == 0) {
                            int sampleIdx = load_audio_file(argPath);
                            if (sampleIdx != -1) add_clip(sampleIdx, 0, 0.0f);
                            loaded = true;
                            break;
                        }
                    }
                }
            }
        }
    }

    
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    
    // Signal shutdown so detached job workers bail out of their loops instead
    // of entering seq_lock()/midi_lock() after the critical sections below are
    // deleted (use-after-free on window close during a load/save/export).
    InterlockedExchange(&g_shuttingDown, 1);
    // Drain the job system: workers clear isBusy via job_end().
    for (int i = 0; i < 2000 && job_is_busy(); ++i) Sleep(10);
    // Stop/join the SFont preset-build worker before freeing g_SFont.lock.
    sfont_build_worker_wait();

    if (g_Seq.deviceInitialized) {
        ma_device_stop(&g_Seq.device);
        ma_device_uninit(&g_Seq.device);
    }
    audition_shutdown();
    for (int i = 0; i < g_Seq.sampleCount; ++i) {
        sample_unmap(&g_Seq.samples[i]);
        free_peak_cache(&g_Seq.samples[i]);
    }
    DeleteCriticalSection(&g_Seq.lock);
     
    DeleteCriticalSection(&g_midiLock);

    
    MFShutdown();

    
    if (g_hFontUI && g_hFontUI != GetStockObject(DEFAULT_GUI_FONT)) {
        DeleteObject(g_hFontUI);
    }
    
    return (int)msg.wParam;
}
