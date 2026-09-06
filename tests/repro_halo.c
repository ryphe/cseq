// Repro harness for the halo editor crash: drives the piano-roll paint path
// for each clip kind with real GDI, mimicking WM_PAINT.
#define _CRT_SECURE_NO_WARNINGS
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <commdlg.h>
#define MAX_TRACKS 128

#include "../headers/fx.h"
#include "../headers/visualizer.h"
#include "../headers/soundfont.h"
#include "../headers/actions.h"
#include "../headers/ui.h"
#include "../headers/dialogs.h"
#include "../headers/project.h"

// Global definitions the extern declarations expect.
SequencerState g_Seq;
HWND g_hWnd = NULL;
GranularEngine g_TrackGran[128];
GranularEngine g_ClipGran[2048];
HFONT g_hFontUI = NULL;
float g_dpiScaleX = 1.0f, g_dpiScaleY = 1.0f;
HWND g_midiHwnd = NULL;
MidiEditContext g_midiEdit = { 0 };
CRITICAL_SECTION g_midiLock;

static const float SR = 44100.0f;

int main(void) {
    InitializeCriticalSection(&g_midiLock);
    setvbuf(stdout, NULL, _IONBF, 0);

    g_Seq.trackCount = 1;
    g_Seq.clipCount = 2;
    g_Seq.gridDivision = 0;
    g_Seq.bpm = 120.0f;
    for (int k = 0; k < 2; ++k) {
        Clip* c = &g_Seq.clips[k];
        memset(c, 0, sizeof(Clip));
        c->track = 0;
        c->isMidi = true;
        c->clipKind = (k == 0) ? CLIP_KIND_QUADRUM : CLIP_KIND_HALO;
        c->lengthBeats = 4.0f;
        c->volume = 1.0f;
        c->playbackRate = 1.0f;
        c->adsrAttack = 5.0f; c->adsrDecay = 0.0f;
        c->adsrSustain = 1.0f; c->adsrRelease = 10.0f;
        for (int n = 0; n < 4; ++n) {
            MidiNote* nn = &c->midiNotes[n];
            nn->pitch = (k == 0) ? (n % 8) : (60 + n * 2);
            nn->startBeat = (float)n;
            nn->lengthBeats = 0.5f;
            nn->velocity = 90.0f;
            nn->active = true;
        }
        c->midiNoteCount = 4;
    }

    HDC screen = GetDC(NULL);
    int w = 820, h = 540;
    HDC memDC = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ oldBmp = SelectObject(memDC, bmp);
    g_hFontUI = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

    // Real window so midi_edit_geom gets honest geometry (the app path).
    WNDCLASSA wcx = {0};
    wcx.lpfnWndProc = DefWindowProcA;
    wcx.hInstance = GetModuleHandle(NULL);
    wcx.lpszClassName = "ReproHost";
    RegisterClassA(&wcx);
    HWND fakeHwnd = CreateWindowExA(0, "ReproHost", "repro", WS_OVERLAPPEDWINDOW,
                                    0, 0, 900, 640, NULL, NULL, GetModuleHandle(NULL), NULL);

    for (int k = 0; k < 2; ++k) {
        g_Seq.clips[0].clipKind = (k == 0) ? CLIP_KIND_QUADRUM : CLIP_KIND_HALO;
        g_midiEdit.clipIdx = 0;
        g_midiEdit.editKind = (k == 0) ? 1 : 2;
        g_midiEdit.octaveShift = 0;
        g_midiCacheInvalid = true;
        printf("[kind %d] start\n", g_midiEdit.editKind);

        Clip* c = midi_edit_clip();
        if (!c) { printf("FAIL: clip NULL\n"); return 1; }
        printf("[kind %d] clip ok\n", g_midiEdit.editKind);

        int baseNote = midi_edit_get_base_note();
        printf("[kind %d] base=%d\n", g_midiEdit.editKind, baseNote);

        bool dirty = is_midi_piano_roll_dirty(w, h, c, baseNote);
        printf("[kind %d] dirty=%d\n", g_midiEdit.editKind, (int)dirty);

        update_midi_piano_roll_cache(fakeHwnd, memDC, w, h, c, baseNote);
        printf("[kind %d] cache ok\n", g_midiEdit.editKind);

        draw_midi_dynamic_overlays(fakeHwnd, memDC, w, h, c, baseNote);
        printf("[kind %d] overlays ok\n", g_midiEdit.editKind);

        g_midiEdit.isMarqueeSelecting = true;
        g_midiEdit.hasMovedPastThreshold = true;
        g_midiEdit.marqueeStartX = 100; g_midiEdit.marqueeStartY = 100;
        g_midiEdit.marqueeCurX = 300;   g_midiEdit.marqueeCurY = 300;
        g_midiEdit.auditionNote = (k == 0) ? 3 : 64;
        draw_midi_dynamic_overlays(fakeHwnd, memDC, w, h, c, baseNote);
        g_midiEdit.isMarqueeSelecting = false;
        printf("[kind %d] overlays2 ok\n", g_midiEdit.editKind);
    }

    printf("ALL PAINT PATHS OK\n");
    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDC);
    ReleaseDC(NULL, screen);
    return 0;
}
