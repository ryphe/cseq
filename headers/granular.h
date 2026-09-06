#pragma once
#include "globals.h"
#include "dsp.h"
#include "ui.h"
#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>

 
extern GranNote       g_granNoteClipboard[GRAN_MAX_NOTES];
extern int            g_granNoteClipboardCount;
extern HWND g_granHwnd;
extern int  g_granTrack;
extern int  g_granClip;

// "Keyboard" audition toggle (bottom-bar button right of the Octave button).
// Off by default; when on, QWERTY keys sound notes while the window is
// focused. Mouse strip audition and keyboard audition share one polyphonic
// held-note set (GranularEngine.auditionNotes, union built in types.h), so
// both can sound up to MIDI_KB_MAX notes at once.
static bool g_granKbMode = false;

static inline GranularEngine* gran_get_current_engine(void) {
    if (g_granClip >= 0 && g_granClip < MAX_CLIPS) {
        return &g_ClipGran[g_granClip];
    }
    if (g_granTrack >= 0 && g_granTrack < MAX_TRACKS) {
        return &g_TrackGran[g_granTrack];
    }
    return NULL;
}

 
static inline uint32_t gran_xorshift32(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x ? x : 0x9E3779B9u;  
    return x;
}

 
static inline float gran_rand_float(uint32_t* state) {
    if (*state == 0u) {
        uint32_t seed = (uint32_t)GetCurrentThreadId() * 2654435761u
                        ^ (uint32_t)GetTickCount64()
                        ^ 0x6C078965u;
        *state = seed ? seed : 0x9E3779B9u;
    }
    return (float)(gran_xorshift32(state) >> 8) * (1.0f / 16777216.0f);
}

 
static __declspec(thread) uint32_t tls_granRngState = 0u;

 
static inline int gran_get_base_note(const GranularEngine *e) {
    return GRAN_PIANO_BASE + (e ? e->octaveShift * 12 : 0);
}

static inline float gran_get_clip_playback_rate(const GranularEngine *e) {
    if (e && e->clipIdx >= 0 && e->clipIdx < g_Seq.clipCount) {
        return g_Seq.clips[e->clipIdx].playbackRate;
    }
    return 1.0f;
}

static inline float gran_get_total_beats(const GranularEngine *e) {
    if (e && e->clipIdx >= 0 && e->clipIdx < g_Seq.clipCount) {
        return g_Seq.clips[e->clipIdx].lengthBeats;
    }
    return total_beats();
}

static inline float gran_midi_to_rate(int midi, float pitchOffset) {
    float n = (float)(midi - 60) + pitchOffset;
    return powf(2.0f, n / 12.0f);
}

static inline float gran_envelope(float phase, float attack, float release) {
    if (phase < 0.0f || phase >= 1.0f) return 0.0f;
    float att = (attack < 0.001f) ? 0.001f : attack;
    float rel = (release < 0.001f) ? 0.001f : release;
    if (att + rel > 1.0f) {
        float scale = 1.0f / (att + rel);
        att *= scale;
        rel *= scale;
    }
    if (phase < att) {
        float x = phase / att;
        return 0.5f * (1.0f - cosf(x * 3.14159265f));
    } else if (phase > 1.0f - rel) {
        float x = (1.0f - phase) / rel;
        return 0.5f * (1.0f - cosf(x * 3.14159265f));
    }
    return 1.0f;
}

static inline void gran_get_note_name(int midiNote, char *outBuf, size_t bufSize) {
    static const char *names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int octave = (midiNote / 12) - 1;
    int note = (midiNote >= 0) ? (midiNote % 12) : 0;
    snprintf(outBuf, bufSize, "%s%d", names[note], octave);
}

static inline AudioSample *gran_get_active_sample(GranularEngine *e) {
    if (!e) return NULL;
    if (e->clipIdx < 0 && e->ownLoaded && e->ownFrames && e->ownFrameCount >= 256) {
        e->ownSample.pFrames = e->ownFrames;
        e->ownSample.frameCount = e->ownFrameCount;
        e->ownSample.loaded = true;
        return &e->ownSample;
    }
     
    AudioSample *table = e->sampleTable ? e->sampleTable : g_Seq.samples;
    int tableCount = e->sampleTable ? e->sampleTableCount : g_Seq.sampleCount;
    if (e->sampleIndex >= 0 && e->sampleIndex < tableCount) {
        AudioSample *s = &table[e->sampleIndex];
        if (s->loaded && s->pFrames && s->frameCount >= 256) return s;
    }
    return NULL;
}


static inline void granular_stop_all(void) {
    for (int t = 0; t < MAX_TRACKS; ++t) {
        g_TrackGran[t].spawnAcc = 0.0f;
        for (int i = 0; i < GRAN_MAX_GRAINS; ++i) g_TrackGran[t].grains[i].active = false;
    }
    for (int c = 0; c < MAX_CLIPS; ++c) {
        g_ClipGran[c].spawnAcc = 0.0f;
        for (int i = 0; i < GRAN_MAX_GRAINS; ++i) g_ClipGran[c].grains[i].active = false;
    }
}

 
static inline bool granular_is_active(void) {
    for (int t = 0; t < MAX_TRACKS; ++t) {
        if (g_TrackGran[t].auditionNoteCount > 0 || (g_TrackGran[t].enabled && g_TrackGran[t].droneMode)) return true;
    }
    for (int c = 0; c < MAX_CLIPS; ++c) {
        if (g_ClipGran[c].auditionNoteCount > 0 || (g_ClipGran[c].enabled && g_ClipGran[c].droneMode)) return true;
    }
    return false;
}

 
static inline void gran_spawn_grain(GranularEngine *e, float rate, float vel, float playbackRateMult) {
    if (!e || (!e->enabled && e->auditionNoteCount <= 0)) return;
    AudioSample *s = gran_get_active_sample(e);
    if (!s || !s->loaded || !s->pFrames || s->frameCount < 256) return;

int grainIdx = -1;
    for (int i = 0; i < GRAN_MAX_GRAINS; ++i) {
        if (!e->grains[i].active) { grainIdx = i; break; }
    }
    if (grainIdx < 0) {
        float maxPhase = -1.0f;
        for (int i = 0; i < GRAN_MAX_GRAINS; ++i) {
            if (e->grains[i].phase > maxPhase) {
                maxPhase = e->grains[i].phase;
                grainIdx = i;
            }
        }
        if (grainIdx < 0) grainIdx = 0;  
    }

    Grain *g = &e->grains[grainIdx];
    g->active = true;
    g->phase = 0.0f;
    g->pos = 0.0f;

    float pitchDetuneRatio = 1.0f;
    if (e->pitchJitter > 0.001f) {
        float jitterSemitones = e->pitchJitter * (gran_rand_float(&tls_granRngState) * 2.0f - 1.0f);
        pitchDetuneRatio = powf(2.0f, jitterSemitones / 12.0f);
    }
    
    g->rate = rate * pitchDetuneRatio * playbackRateMult;
    if (g->rate < 0.02f) g->rate = 0.02f;
    if (g->rate > 16.0f) g->rate = 16.0f;

    float sizeVariation = 0.85f + 0.30f * gran_rand_float(&tls_granRngState);
    float sizeMs = e->grainSizeMs * sizeVariation;

    g->lengthFrames = (int)(sizeMs * 0.001f * (float)SAMPLE_RATE);
    if (g->lengthFrames < GRAN_GRAIN_SIZE_MIN) g->lengthFrames = GRAN_GRAIN_SIZE_MIN;
    if (g->lengthFrames > GRAN_GRAIN_SIZE_MAX) g->lengthFrames = GRAN_GRAIN_SIZE_MAX;
    // Precompute the phase increment so the per-sample grain loop only adds.
    g->phaseInc = (g->lengthFrames > 0) ? (1.0f / (float)g->lengthFrames) : 1.0f;

    float posNorm = e->position;
    if (!e->freeze && e->posJitter > 0.001f) {
        posNorm += e->posJitter * (gran_rand_float(&tls_granRngState) * 2.0f - 1.0f);
    }
    if (posNorm < 0.0f) posNorm = 0.0f;
    if (posNorm > 1.0f) posNorm = 1.0f;

    ma_uint64 maxStart = (s->frameCount > (ma_uint64)g->lengthFrames) ? (s->frameCount - (ma_uint64)g->lengthFrames) : 0;
    g->startFrame = (ma_uint64)(posNorm * (float)maxStart);

    float panVal = 0.5f;
    if (e->panSpread > 0.001f) {
        panVal = 0.5f + (e->panSpread * 0.5f) * (gran_rand_float(&tls_granRngState) * 2.0f - 1.0f);
        if (panVal < 0.0f) panVal = 0.0f;
        if (panVal > 1.0f) panVal = 1.0f;
    }
    g->panL = cosf(panVal * 1.5707963f);
    g->panR = sinf(panVal * 1.5707963f);
    g->velocity = (vel > 0.01f) ? vel : 1.0f;
    g->amp = 0.0f;
}

static inline void ensure_granular_params(GranularEngine *e, int sampleIndex) {
    if (!e) return;
    if (e->grainSizeMs < 1.0f) {
        e->grainSizeMs = 40.0f;
        e->density = 25.0f;
        e->position = 0.20f;
        e->posJitter = 0.15f;
        e->pitch = 0.0f;
        e->pitchJitter = 0.25f;
        e->panSpread = 0.40f;
        e->attack = 0.10f;
        e->release = 0.25f;
        e->volume = 0.85f;
        e->freeze = false;
        e->droneMode = false;
        e->octaveShift = 0;
    }
    if (e->sampleIndex < 0 && sampleIndex >= 0) {
        e->sampleIndex = sampleIndex;
    }
}

 
static inline void granular_process_engine(GranularEngine *e, float *L, float *R, ma_uint32 frames,
                                           float bpm, float swing, ma_uint64 startFrame,
                                           const Clip *clip) {
    if (!e) return;
    ensure_granular_params(e, clip ? clip->sampleIndex : e->sampleIndex);
    if (!e->enabled && e->auditionNoteCount <= 0) return;

    AudioSample *s = gran_get_active_sample(e);
    if (!s || !s->loaded || !s->pFrames || s->frameCount < 256) return;

    float dens = e->density;
    if (dens < 1.0f) dens = 1.0f;
    const float grainsPerFrame = dens / (float)SAMPLE_RATE;

     
    float playbackRateMult = 1.0f;
    float clipVolume       = 1.0f;
    float fadeInBeats      = 0.0f;
    float fadeOutBeats     = 0.0f;
    float swungStart       = 0.0f;
    float clipLenBeats     = 0.0f;
    if (clip) {
        playbackRateMult = (clip->playbackRate > 0.01f) ? clip->playbackRate : 1.0f;
        clipVolume       = clip->volume;
        fadeInBeats      = clip->fadeInBeats;
        fadeOutBeats     = clip->fadeOutBeats;
        swungStart       = apply_clip_swing(clip->startBeat, swing);
        clipLenBeats     = clip->lengthBeats;
    }

    for (ma_uint32 f = 0; f < frames; ++f) {
        ma_uint64 thisFrame = startFrame + f;
        float curBeat = frame_to_beat(thisFrame, bpm, 0.0f);
        float spawnBeat = curBeat;
        if (clip) spawnBeat = curBeat - swungStart;

         
        bool inClip = clip ? (spawnBeat >= 0.0f && spawnBeat < clipLenBeats) : true;

        if (inClip) {
            e->spawnAcc += grainsPerFrame;
            if (e->spawnAcc > 4.0f) e->spawnAcc = 4.0f;

            while (e->spawnAcc >= 1.0f) {
                e->spawnAcc -= 1.0f;

                if (e->auditionNoteCount > 0) {
                    // Round-robin across every held note so chords stream
                    // grains on all pitches at the shared density.
                    int grab = e->auditionSpawnIdx % e->auditionNoteCount;
                    e->auditionSpawnIdx = grab + 1;
                    int useNote = e->auditionNotes[grab];
                    if (useNote < 0) useNote = 0;
                    if (useNote > 127) useNote = 127;
                    float rate = gran_midi_to_rate(useNote, e->pitch);
                    gran_spawn_grain(e, rate, 0.9f, playbackRateMult);
                } else if (e->droneMode) {
                    float rate = gran_midi_to_rate(60, e->pitch);
                    gran_spawn_grain(e, rate, 0.85f, playbackRateMult);
                } else if (clip) {
                    for (int n = 0; n < e->noteCount; ++n) {
                        GranNote *nt = &e->notes[n];
                        if (!nt->active) continue;
                        float sNote = apply_note_clip_swing(nt->startBeat, swing);
                        float end = sNote + nt->lengthBeats;
                        if (spawnBeat >= sNote && spawnBeat < end) {
                            float rate = gran_midi_to_rate(nt->midiNote, e->pitch);
                            gran_spawn_grain(e, rate, nt->velocity, playbackRateMult);
                        }
                    }
                } else {
                    float rollBeats = gran_get_total_beats(e);
                    if (rollBeats <= 0.0f) rollBeats = 16.0f;
                    float modBeat = fmodf(curBeat, rollBeats);
                    if (modBeat < 0.0f) modBeat += rollBeats;

                    for (int n = 0; n < e->noteCount; ++n) {
                        GranNote *nt = &e->notes[n];
                        if (!nt->active) continue;
                        float sNote = apply_note_clip_swing(nt->startBeat, swing);
                        float end = sNote + nt->lengthBeats;
                        if (modBeat >= sNote && modBeat < end) {
                            float rate = gran_midi_to_rate(nt->midiNote, e->pitch);
                            gran_spawn_grain(e, rate, nt->velocity, playbackRateMult);
                        }
                    }
                }
            }
        }

         
        float fadeMultiplier = 1.0f;
        if (clip) {
            if (inClip) {
                if (fadeInBeats > 0.0001f && spawnBeat < fadeInBeats) {
                    fadeMultiplier = spawnBeat / fadeInBeats;
                } else if (fadeOutBeats > 0.0001f && (clipLenBeats - spawnBeat) < fadeOutBeats) {
                    fadeMultiplier = (clipLenBeats - spawnBeat) / fadeOutBeats;
                    if (fadeMultiplier < 0.0f) fadeMultiplier = 0.0f;
                }
            }
             
        }

         
        float sumL = 0.0f, sumR = 0.0f;
        for (int i = 0; i < GRAN_MAX_GRAINS; ++i) {
            Grain *g = &e->grains[i];
            if (!g->active) continue;

            double srcPos = (double)g->startFrame + (double)g->pos;
            if (srcPos < 0.0) { g->active = false; continue; }
            ma_uint64 i0 = (ma_uint64)srcPos;
            if (i0 >= s->frameCount) {
                g->active = false;
                continue;
            }
            ma_uint64 i1 = (i0 + 1 < s->frameCount) ? (i0 + 1) : i0;

            float frac = (float)(srcPos - (double)i0);
            // Interleaved stereo: precompute the doubled indices once per grain.
            ma_uint64 i0x2 = i0 << 1, i1x2 = i1 << 1;
            float sl = s->pFrames[i0x2]     + frac * (s->pFrames[i1x2]     - s->pFrames[i0x2]);
            float sr = s->pFrames[i0x2 | 1] + frac * (s->pFrames[i1x2 | 1] - s->pFrames[i0x2 | 1]);
            float env = gran_envelope(g->phase, e->attack, e->release);
            float gain = env * e->volume * g->velocity * clipVolume * fadeMultiplier;

            sumL += sl * gain * g->panL;
            sumR += sr * gain * g->panR;

            g->pos += g->rate;
            g->phase += g->phaseInc;
            if (g->phase >= 1.0f) {
                g->active = false;
            }
        }
        L[f] += sumL;
        R[f] += sumR;
    }
}

 
static inline void granular_process_track_ptr(GranularEngine *trackGranEngines, int trackIdx,
                                              float *L, float *R, ma_uint32 frames,
                                              float bpm, float swing, ma_uint64 startFrame) {
    if (!trackGranEngines || trackIdx < 0 || trackIdx >= MAX_TRACKS) return;
    granular_process_engine(&trackGranEngines[trackIdx], L, R, frames, bpm, swing, startFrame, NULL);
}

static inline void granular_process_clip_ptr(GranularEngine *clipGranEngines, const Clip *clips, int clipIdx,
                                             float *L, float *R, ma_uint32 frames,
                                             float bpm, float swing, ma_uint64 startFrame) {
    if (!clipGranEngines || !clips || clipIdx < 0 || clipIdx >= MAX_CLIPS) return;
    const Clip *c = &clips[clipIdx];
    if (!c->isGranular) return;
    granular_process_engine(&clipGranEngines[clipIdx], L, R, frames, bpm, swing, startFrame, c);
}

 
static inline void gran_draw_note_display(HDC hdc, int x, int y, int w, int h, GranularEngine *e) {
    HFONT oldFont = SELECT_UI_FONT(hdc);
    int baseNote = gran_get_base_note(e);
    float rowH = (float)h / (float)GRAN_PIANO_KEYS;

    HBRUSH bgBrush = CreateSolidBrush(RGB(18, 22, 28));
    RECT bgRc = { x, y, x + w, y + h };
    FillRect(hdc, &bgRc, bgBrush);
    DeleteObject(bgBrush);

    for (int k = 0; k < GRAN_PIANO_KEYS; ++k) {
        int midi = baseNote + (GRAN_PIANO_KEYS - 1 - k);
        int noteInOct = midi % 12;
        bool isBlack = (noteInOct == 1 || noteInOct == 3 || noteInOct == 6 || noteInOct == 8 || noteInOct == 10);
        bool isRootC = (noteInOct == 0);
        bool isAuditioning = false;
        for (int a = 0; a < e->auditionNoteCount; ++a) {
            if (e->auditionNotes[a] == midi) { isAuditioning = true; break; }
        }

        int ky1 = y + (int)(k * rowH);
        int ky2 = y + (int)((k + 1) * rowH);
        RECT kr = { x, ky1, x + w - 1, ky2 };

        COLORREF fillCol = isAuditioning ? RGB(80, 210, 240) : (isBlack ? RGB(22, 26, 34) : RGB(36, 42, 54));
        HBRUSH br = CreateSolidBrush(fillCol);
        FillRect(hdc, &kr, br);
        DeleteObject(br);

        HPEN sep = CreatePen(PS_SOLID, 1, RGB(28, 33, 42));
        HGDIOBJ oldP = SelectObject(hdc, sep);
        MoveToEx(hdc, x, ky2 - 1, NULL);
        LineTo(hdc, x + w, ky2 - 1);
        SelectObject(hdc, oldP);
        DeleteObject(sep);

        char nName[8];
        gran_get_note_name(midi, nName, sizeof(nName));

        SetBkMode(hdc, TRANSPARENT);
        if (isAuditioning) {
            SetTextColor(hdc, RGB(10, 15, 20));
        } else if (isRootC) {
            SetTextColor(hdc, RGB(100, 235, 255));
        } else if (isBlack) {
            SetTextColor(hdc, RGB(130, 145, 165));
        } else {
            SetTextColor(hdc, RGB(190, 205, 225));
        }

        RECT tr = { x + 2, ky1 - 1, x + w - 4, ky2 + 1 };
        DrawTextA(hdc, nName, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    HPEN borderP = CreatePen(PS_SOLID, 1, RGB(48, 56, 70));
    HGDIOBJ oldB = SelectObject(hdc, borderP);
    MoveToEx(hdc, x + w - 1, y, NULL);
    LineTo(hdc, x + w - 1, y + h);
    SelectObject(hdc, oldB);
    DeleteObject(borderP);
    SelectObject(hdc, oldFont);
}

static inline void gran_draw_piano_roll(HDC hdc, int x, int y, int w, int h, GranularEngine *e) {
    HFONT oldFont = SELECT_UI_FONT(hdc);
    HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
    RECT rc = { x, y, x + w, y + h };
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    int baseNote = gran_get_base_note(e);
    float rowH = (float)h / (float)GRAN_PIANO_KEYS;
    float totalBeats = gran_get_total_beats(e);
    float ppb = (float)w / totalBeats;

     
    for (int k = 0; k < GRAN_PIANO_KEYS; ++k) {
        int midi = baseNote + (GRAN_PIANO_KEYS - 1 - k);
        int noteInOct = midi % 12;
        bool isBlack = (noteInOct == 1 || noteInOct == 3 || noteInOct == 6 || noteInOct == 8 || noteInOct == 10);
        bool isRootC = (noteInOct == 0);

        int ky1 = y + (int)(k * rowH);
        int ky2 = y + (int)((k + 1) * rowH);
        RECT rr = { x, ky1, x + w, ky2 };

        HBRUSH rowBr = CreateSolidBrush(isBlack ? RGB(19, 22, 29) : RGB(24, 28, 37));
        FillRect(hdc, &rr, rowBr);
        DeleteObject(rowBr);

        if (isRootC) {
            HPEN rootPen = CreatePen(PS_SOLID, 1, RGB(40, 80, 105));
            HGDIOBJ oldRP = SelectObject(hdc, rootPen);
            MoveToEx(hdc, x, ky2 - 1, NULL);
            LineTo(hdc, x + w, ky2 - 1);
            SelectObject(hdc, oldRP);
            DeleteObject(rootPen);
        }
    }

     
    HPEN barPen = CreatePen(PS_SOLID, 1, RGB(55, 68, 88));
    HPEN beatPen = CreatePen(PS_SOLID, 1, RGB(36, 44, 58));
    HPEN sixteenthPen = CreatePen(PS_SOLID, 1, RGB(25, 30, 40));
    HGDIOBJ origPen = SelectObject(hdc, sixteenthPen);

    int total16ths = (int)(totalBeats * 4.0f);
    for (int s = 0; s <= total16ths; ++s) {
        int gx = x + (int)((float)s * 0.25f * ppb);
        if ((s & 15) == 0) SelectObject(hdc, barPen);
        else if ((s & 3) == 0) SelectObject(hdc, beatPen);
        else SelectObject(hdc, sixteenthPen);

        MoveToEx(hdc, gx, y, NULL);
        LineTo(hdc, gx, y + h);
    }

    SelectObject(hdc, origPen);
    DeleteObject(barPen);
    DeleteObject(beatPen);
    DeleteObject(sixteenthPen);

     
    for (int i = 0; i < e->noteCount; ++i) {
        GranNote *n = &e->notes[i];
        if (!n->active) continue;
        int keyIdx = (GRAN_PIANO_KEYS - 1) - (n->midiNote - baseNote);
        if (keyIdx < 0 || keyIdx >= GRAN_PIANO_KEYS) continue;

        int nx = x + (int)(n->startBeat * ppb);
        int nw = (int)(n->lengthBeats * ppb);
        if (nw < 6) nw = 6;
        int ny = y + (int)(keyIdx * rowH) + 1;
        int nh = (int)rowH - 1;
        if (nh < 3) nh = 3;

        COLORREF fillCol   = n->isSelected ? RGB(32, 68, 86)    : RGB(22, 42, 54);
        COLORREF borderCol = n->isSelected ? RGB(140, 245, 255) : RGB(60, 195, 235);

        HBRUSH nBr = CreateSolidBrush(fillCol);
        HPEN nPen = CreatePen(PS_SOLID, 1, borderCol);
        HGDIOBJ oldNB = SelectObject(hdc, nBr);
        HGDIOBJ oldNP = SelectObject(hdc, nPen);

        
        RoundRect(hdc, nx, ny, nx + nw, ny + nh, 4, 4);

        if (n->isSelected && nh > 6 && nw > 6) {
            RoundRect(hdc, nx + 1, ny + 1, nx + nw - 1, ny + nh - 1, 3, 3);
        }

        SelectObject(hdc, oldNP);
        SelectObject(hdc, oldNB);
        DeleteObject(nPen);
        DeleteObject(nBr);

        if (nw >= 18 && nh >= 10) {
            char nName[8];
            gran_get_note_name(n->midiNote, nName, sizeof(nName));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, n->isSelected ? RGB(200, 250, 255) : RGB(120, 205, 230));
            RECT tr = { nx + 4, ny, nx + nw - 2, ny + nh };
            DrawTextA(hdc, nName, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

     
    if (e->isMarqueeSelecting && e->hasMovedPastThreshold) {
        int mX1 = min(e->marqueeStartX, e->marqueeCurX);
        int mX2 = max(e->marqueeStartX, e->marqueeCurX);
        int mY1 = min(e->marqueeStartY, e->marqueeCurY);
        int mY2 = max(e->marqueeStartY, e->marqueeCurY);

        draw_alpha_box(hdc, mX1, mY1, mX2 - mX1, mY2 - mY1, RGB(60, 140, 240), 65, RGB(110, 190, 255));
    }

     
    if (seq_is_playing()) {
        LONG pFrame = InterlockedCompareExchange(&g_Seq.playbackFrame, 0, 0);
        float curBeat = frame_to_beat((ma_uint64)pFrame, g_Seq.bpm, 0.0f);
        float modBeat = fmodf(curBeat, totalBeats);
        int phX = x + (int)(modBeat * ppb);

        if (phX >= x && phX <= x + w) {
            HPEN phPen = CreatePen(PS_SOLID, 1, RGB(80, 220, 245));  
            HGDIOBJ oldPh = SelectObject(hdc, phPen);
            MoveToEx(hdc, phX, y, NULL);
            LineTo(hdc, phX, y + h);
            SelectObject(hdc, oldPh);
            DeleteObject(phPen);
            SelectObject(hdc, oldFont);
        }
    }
}

 
static inline int gran_get_note_under_mouse(GranularEngine *e, int mx, int my, int gridX, int rollY, int gridW, int rollH, int *outEdge) {
    if (!e || e->noteCount == 0) return -1;
    float rowH = (float)rollH / (float)GRAN_PIANO_KEYS;
    float totalBeats = gran_get_total_beats(e);
    float ppb = (float)gridW / totalBeats;
    int baseNote = gran_get_base_note(e);

    if (outEdge) *outEdge = 0;

    for (int i = e->noteCount - 1; i >= 0; --i) {
        GranNote *n = &e->notes[i];
        if (!n->active) continue;
        int keyIdx = (GRAN_PIANO_KEYS - 1) - (n->midiNote - baseNote);
        if (keyIdx < 0 || keyIdx >= GRAN_PIANO_KEYS) continue;

        int nx1 = gridX + (int)(n->startBeat * ppb);
        int nw = (int)(n->lengthBeats * ppb);
        if (nw < 6) nw = 6;
        int nx2 = nx1 + nw;
        int ny1 = rollY + (int)(keyIdx * rowH) + 1;
        int ny2 = ny1 + (int)rowH - 1;

        if (mx >= nx1 - 3 && mx <= nx2 + 3 && my >= ny1 && my <= ny2) {
            if (outEdge) {
                if (mx <= nx1 + 5) *outEdge = 1;
                else if (mx >= nx2 - 5) *outEdge = 2;
                else *outEdge = 0;
            }
            return i;
        }
    }
    return -1;
}

 
static LRESULT CALLBACK GranWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    GranularEngine *e = gran_get_current_engine();

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_TIMER:
        if (wParam == 1 && g_granHwnd) {
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        
        HFONT oldFontMain = SELECT_UI_FONT(hdc);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0 || !e) {
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

        // Capture the DC's original pen/brush once; every created pen/brush is
        // restored to these before DeleteObject so the delete succeeds instead
        // of leaking a GDI object on every repaint.
        HGDIOBJ origPenG   = GetCurrentObject(memDC, OBJ_PEN);
        HGDIOBJ origBrushG = GetCurrentObject(memDC, OBJ_BRUSH);

         
        int padX = 14, gapX = 8, sBoxY = 10, sBoxH = 22;
        int totalBtnW = w - 2 * padX;
        bool isClipMode = (e->clipIdx >= 0);

        RECT sBoxRc = {0}, granToggleRc = {0}, freezeRc = {0}, droneRc = {0};

        if (isClipMode) {
             
            int btnW = (totalBtnW - gapX) / 2;
            SetRect(&granToggleRc, padX,               sBoxY, padX + btnW,               sBoxY + sBoxH);
            SetRect(&freezeRc,     padX + btnW + gapX, sBoxY, padX + 2 * btnW + gapX,    sBoxY + sBoxH);
        } else {
             
            int btnW = (totalBtnW - 3 * gapX) / 4;
            SetRect(&sBoxRc,       padX,                          sBoxY, padX + btnW,                       sBoxY + sBoxH);
            SetRect(&granToggleRc, padX + 1 * (btnW + gapX),      sBoxY, padX + 2 * btnW + 1 * gapX,        sBoxY + sBoxH);
            SetRect(&freezeRc,     padX + 2 * (btnW + gapX),      sBoxY, padX + 3 * btnW + 2 * gapX,        sBoxY + sBoxH);
            SetRect(&droneRc,      padX + 3 * (btnW + gapX),      sBoxY, padX + 4 * btnW + 3 * gapX,        sBoxY + sBoxH);

             
            char sNameBuf[64] = "[LOAD SAMPLE]";
            if (e->ownLoaded && e->ownFrames) {
                snprintf(sNameBuf, sizeof(sNameBuf), "[SAMPLE: %s]", e->ownSampleName);
            } else if (e->sampleIndex >= 0 && e->sampleIndex < g_Seq.sampleCount) {
                snprintf(sNameBuf, sizeof(sNameBuf), "[SAMPLE: %s]", g_Seq.samples[e->sampleIndex].name);
            }

            HBRUSH sBg = CreateSolidBrush(RGB(24, 30, 40));
            HPEN sPen = CreatePen(PS_SOLID, 1, RGB(50, 65, 85));
            SelectObject(memDC, sBg);
            SelectObject(memDC, sPen);
            RoundRect(memDC, sBoxRc.left, sBoxRc.top, sBoxRc.right, sBoxRc.bottom, 3, 3);
            SetTextColor(memDC, RGB(140, 230, 210));
             
            wchar_t sNameW[128];
            if (utf8_to_wide_buf(sNameBuf, sNameW, 128) > 0)
                DrawTextW(memDC, sNameW, -1, &sBoxRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            else
                DrawTextA(memDC, sNameBuf, -1, &sBoxRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(memDC, origPenG);
            SelectObject(memDC, origBrushG);
            DeleteObject(sPen);
            DeleteObject(sBg);
        }

         
        HBRUSH gActiveBg = CreateSolidBrush(e->enabled ? RGB(25, 50, 45) : RGB(30, 32, 38));
        HPEN gActivePen = CreatePen(PS_SOLID, 1, e->enabled ? RGB(80, 240, 180) : RGB(55, 60, 72));
        SelectObject(memDC, gActiveBg);
        SelectObject(memDC, gActivePen);
        RoundRect(memDC, granToggleRc.left, granToggleRc.top, granToggleRc.right, granToggleRc.bottom, 3, 3);
        SetTextColor(memDC, e->enabled ? RGB(80, 240, 180) : RGB(130, 140, 155));
        DrawTextA(memDC, e->enabled ? "[GRANULAR: ON]" : "[GRANULAR: BYPASS]", -1, &granToggleRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(memDC, origPenG);
        SelectObject(memDC, origBrushG);
        DeleteObject(gActivePen);
        DeleteObject(gActiveBg);

         
        HBRUSH fActiveBg = CreateSolidBrush(e->freeze ? RGB(48, 42, 25) : RGB(30, 32, 38));
        HPEN fActivePen = CreatePen(PS_SOLID, 1, e->freeze ? RGB(255, 200, 80) : RGB(55, 60, 72));
        SelectObject(memDC, fActiveBg);
        SelectObject(memDC, fActivePen);
        RoundRect(memDC, freezeRc.left, freezeRc.top, freezeRc.right, freezeRc.bottom, 3, 3);
        SetTextColor(memDC, e->freeze ? RGB(255, 200, 80) : RGB(130, 140, 155));
        DrawTextA(memDC, e->freeze ? "[FREEZE: ON]" : "[FREEZE: OFF]", -1, &freezeRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(memDC, origPenG);
        SelectObject(memDC, origBrushG);
        DeleteObject(fActivePen);
        DeleteObject(fActiveBg);

         
        if (!isClipMode) {
            HBRUSH dActiveBg = CreateSolidBrush(e->droneMode ? RGB(40, 28, 55) : RGB(30, 32, 38));
            HPEN dActivePen = CreatePen(PS_SOLID, 1, e->droneMode ? RGB(200, 140, 255) : RGB(55, 60, 72));
            SelectObject(memDC, dActiveBg);
            SelectObject(memDC, dActivePen);
            RoundRect(memDC, droneRc.left, droneRc.top, droneRc.right, droneRc.bottom, 3, 3);
            SetTextColor(memDC, e->droneMode ? RGB(210, 160, 255) : RGB(130, 140, 155));
            DrawTextA(memDC, e->droneMode ? "[DRONE: ON]" : "[DRONE: OFF]", -1, &droneRc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(memDC, origPenG);
            SelectObject(memDC, origBrushG);
            DeleteObject(dActivePen);
            DeleteObject(dActiveBg);
        }

         
        const char *labels[] = { "Size", "Density", "Position", "Spray", "Pan",
                                 "Pitch", "Detune", "Attack", "Release", "Volume" };
        float *params[] = {
            &e->grainSizeMs, &e->density, &e->position, &e->posJitter, &e->panSpread,
            &e->pitch, &e->pitchJitter, &e->attack, &e->release, &e->volume
        };
        
        
        float mins[] = { 5.0f, 1.0f, 0.0f, 0.0f, 0.0f, -24.0f, 0.0f, 0.01f, 0.01f, 0.0f };
        float maxs[] = { 2500.0f, 80.0f, 1.0f, 1.0f, 1.0f, 24.0f, 12.0f, 0.50f, 0.50f, 1.50f };

        int sy = 38, col1X = 14, colW = (w - 38) / 2, col2X = col1X + colW + 10;
        for (int p = 0; p < 10; ++p) {
            int col = (p < 5) ? 0 : 1;
            int row = (p < 5) ? p : (p - 5);
            int sx = (col == 0) ? col1X : col2X;
            int py = sy + row * 19;

            SetTextColor(memDC, RGB(140, 155, 175));
            TextOutA(memDC, sx, py, labels[p], (int)strlen(labels[p]));

            
            int railX = sx + 56;          
            int railW = colW - 112;       
            float norm = (*params[p] - mins[p]) / (maxs[p] - mins[p]);
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;
            int fillW = (int)(norm * (float)railW);

            HPEN railP = CreatePen(PS_SOLID, 3, RGB(30, 36, 46));
            SelectObject(memDC, railP);
            MoveToEx(memDC, railX, py + 7, NULL);
            LineTo(memDC, railX + railW, py + 7);
            SelectObject(memDC, origPenG);
            DeleteObject(railP);

            HPEN fillP = CreatePen(PS_SOLID, 3, (p == 5 || p == 6) ? RGB(255, 175, 80) : RGB(80, 210, 240));
            SelectObject(memDC, fillP);
            MoveToEx(memDC, railX, py + 7, NULL);
            LineTo(memDC, railX + fillW, py + 7);
            SelectObject(memDC, origPenG);
            DeleteObject(fillP);

            draw_aa_circle(memDC, railX + fillW, py + 7, 5.5f, RGB(100, 245, 210), RGB(255, 255, 255), 1.5f);

            char valBuf[16];
            if (p == 0) snprintf(valBuf, sizeof(valBuf), "%dms", (int)*params[p]);
            else if (p == 1) snprintf(valBuf, sizeof(valBuf), "%dHz", (int)*params[p]);
            else if (p == 5) snprintf(valBuf, sizeof(valBuf), "%+dst", (int)*params[p]);
            else if (p == 6) snprintf(valBuf, sizeof(valBuf), "%.1fst", *params[p]);
            else snprintf(valBuf, sizeof(valBuf), "%d%%", (int)(norm * 100.0f));

            SetTextColor(memDC, RGB(180, 195, 215));
            TextOutA(memDC, railX + railW + 6, py, valBuf, (int)strlen(valBuf));
        }

         
        int visY = 138, visH = 14, visX = 14, visW = w - 28;
        RECT visRc = { visX, visY, visX + visW, visY + visH };
        HBRUSH visBg = CreateSolidBrush(RGB(14, 17, 23));
        HPEN visPen = CreatePen(PS_SOLID, 1, RGB(36, 44, 56));
        SelectObject(memDC, visBg);
        SelectObject(memDC, visPen);
        Rectangle(memDC, visRc.left, visRc.top, visRc.right, visRc.bottom);
        SelectObject(memDC, origPenG);
        SelectObject(memDC, origBrushG);
        DeleteObject(visPen);
        DeleteObject(visBg);

        AudioSample *activeSample = gran_get_active_sample(e);
        if (activeSample && activeSample->frameCount > 0) {
            int curPosX = visX + (int)(e->position * (float)visW);
            HPEN posPen = CreatePen(PS_SOLID, 2, RGB(80, 210, 240));
            SelectObject(memDC, posPen);
            MoveToEx(memDC, curPosX, visY, NULL);
            LineTo(memDC, curPosX, visY + visH);
            SelectObject(memDC, origPenG);
            DeleteObject(posPen);

            HBRUSH gDotBr = CreateSolidBrush(RGB(80, 240, 180));
            SelectObject(memDC, gDotBr);
            for (int i = 0; i < GRAN_MAX_GRAINS; ++i) {
                if (e->grains[i].active) {
                    int gx = visX + (int)(((float)e->grains[i].startFrame / (float)activeSample->frameCount) * (float)visW);
                    int gy = visY + visH / 2;
                    Ellipse(memDC, gx - 2, gy - 2, gx + 3, gy + 3);
                }
            }
            SelectObject(memDC, origBrushG);
            DeleteObject(gDotBr);
        }

         
        int rollY = 158;
        int rollH = h - rollY - 32;
        int noteDispW = 46;

        gran_draw_note_display(memDC, 14, rollY, noteDispW, rollH, e);
        gran_draw_piano_roll(memDC, 14 + noteDispW + 2, rollY, w - 30 - noteDispW, rollH, e);

         
        int botY = h - 26;
        RECT clrRc = { 14, botY, 100, botY + 20 };
        RECT octRc = { 106, botY, 206, botY + 20 };
        RECT kbRc  = { 212, botY, 302, botY + 20 };

        HBRUSH btnBr = CreateSolidBrush(RGB(26, 32, 42));
        HPEN btnPn = CreatePen(PS_SOLID, 1, RGB(48, 58, 72));
        SelectObject(memDC, btnBr);
        SelectObject(memDC, btnPn);
        RoundRect(memDC, clrRc.left, clrRc.top, clrRc.right, clrRc.bottom, 3, 3);
        RoundRect(memDC, octRc.left, octRc.top, octRc.right, octRc.bottom, 3, 3);
        if (g_granKbMode) {
            HBRUSH kbBg = CreateSolidBrush(RGB(22, 90, 55));
            HPEN kbPn  = CreatePen(PS_SOLID, 1, RGB(80, 240, 180));
            SelectObject(memDC, kbBg);
            SelectObject(memDC, kbPn);
            RoundRect(memDC, kbRc.left, kbRc.top, kbRc.right, kbRc.bottom, 3, 3);
            SelectObject(memDC, btnPn);
            SelectObject(memDC, btnBr);
            DeleteObject(kbPn);
            DeleteObject(kbBg);
        } else {
            RoundRect(memDC, kbRc.left, kbRc.top, kbRc.right, kbRc.bottom, 3, 3);
        }
        SelectObject(memDC, origPenG);
        SelectObject(memDC, origBrushG);
        DeleteObject(btnPn);
        DeleteObject(btnBr);

        SetTextColor(memDC, RGB(220, 120, 120));
        DrawTextA(memDC, "Clear Notes", -1, &clrRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        char octBuf[32];
        int bNote = gran_get_base_note(e);
        char lowN[8], highN[8];
        gran_get_note_name(bNote, lowN, sizeof(lowN));
        gran_get_note_name(bNote + GRAN_PIANO_KEYS - 1, highN, sizeof(highN));
        snprintf(octBuf, sizeof(octBuf), "Oct: %s-%s", lowN, highN);

        SetTextColor(memDC, RGB(110, 210, 240));
        DrawTextA(memDC, octBuf, -1, &octRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SetTextColor(memDC, g_granKbMode ? RGB(160, 255, 205) : RGB(140, 155, 175));
        DrawTextA(memDC, "Keyboard", -1, &kbRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        
        SetTextColor(memDC, RGB(140, 155, 175));
        RECT hintRc = { 306, botY + 1, w - 14, botY + 21 };
        const char* hintTxt = g_granKbMode
            ? "Keys: A W S E D F T G Y H U J K O L P"
            : "[L/R Drag] Select | [Click] Add/Delete | [ESC] Close";
        DrawTextA(memDC, hintTxt, -1, &hintRc,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        
        
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
        if (!e) return 0;
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        int padX = 14, gapX = 8, sBoxY = 10, sBoxH = 22;
        int totalBtnW = w - 2 * padX;
        bool isClipMode = (e->clipIdx >= 0);

        if (my >= sBoxY && my <= sBoxY + sBoxH) {
            if (isClipMode) {
                int btnW = (totalBtnW - gapX) / 2;
                if (mx >= padX && mx <= padX + btnW) {
                    seq_lock();
                    e->enabled = !e->enabled;
                    if (e->clipIdx >= 0 && e->clipIdx < g_Seq.clipCount) {
                        g_Seq.clips[e->clipIdx].isGranular = e->enabled;
                    }
                    seq_unlock();
                    g_timelineDirty = true;
                    InvalidateRect(hwnd, NULL, FALSE);
                    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                    return 0;
                }
                if (mx >= padX + btnW + gapX && mx <= padX + 2 * btnW + gapX) {
                    seq_lock();
                    e->freeze = !e->freeze;
                    seq_unlock();
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            } else {
                int btnW = (totalBtnW - 2 * gapX) / 4; 

                 
                if (mx >= padX && mx <= padX + btnW) {
                     
                    OPENFILENAMEW ofn;
                    wchar_t szFileW[MAX_PATH] = L"";
                    ZeroMemory(&ofn, sizeof(ofn));
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"Audio (*.wav;*.mp3;*.flac)\0*.wav;*.mp3;*.flac\0All\0*.*\0";
                    ofn.lpstrFile = szFileW;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    if (GetOpenFileNameW(&ofn)) {
                        ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, NUM_CHANNELS, SAMPLE_RATE);
                        ma_decoder dec;
                        if (ma_decoder_init_file_w(szFileW, &cfg, &dec) == MA_SUCCESS) {
                            ma_uint64 total = 0;
                            ma_decoder_get_length_in_pcm_frames(&dec, &total);
                            if (total > 0) {
                                float *buf = (float*)malloc(sizeof(float) * total * NUM_CHANNELS);
                                if (buf) {
                                    ma_uint64 read = 0;
                                    ma_decoder_read_pcm_frames(&dec, buf, total, &read);
                                    seq_lock();
                                    if (e->ownFrames) free(e->ownFrames);
                                    e->ownFrames = buf;
                                    e->ownFrameCount = read;
                                    e->ownLoaded = true;
                                    const wchar_t *base = wcsrchr(szFileW, L'\\');
                                    base = base ? base + 1 : szFileW;
                                    wide_to_utf8_buf(base, e->ownSampleName, (int)sizeof(e->ownSampleName));
                                    e->sampleIndex = -1;
                                    memset(&e->ownSample, 0, sizeof(AudioSample));
                                    strncpy(e->ownSample.name, e->ownSampleName, sizeof(e->ownSample.name) - 1);
                                    e->ownSample.pFrames = e->ownFrames;
                                    e->ownSample.frameCount = e->ownFrameCount;
                                    e->ownSample.loaded = true;
                                    seq_unlock();
                                }
                            }
                            ma_decoder_uninit(&dec);
                        }
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    return 0;
                }

                 
                 
                
                 
                if (mx >= padX + btnW + gapX && mx <= padX + 2 * btnW + gapX) {
                    seq_lock();
                    e->enabled = !e->enabled;
                    seq_unlock();
                    g_timelineDirty = true;
                    InvalidateRect(hwnd, NULL, FALSE);
                    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
                    return 0;
                }

                 
                if (mx >= padX + 2 * (btnW + gapX) && mx <= padX + 3 * btnW + 2 * gapX) {
                    seq_lock();
                    e->freeze = !e->freeze;
                    seq_unlock();
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }

                 
                if (mx >= padX + 3 * (btnW + gapX) && mx <= padX + 4 * btnW + 3 * gapX) {
                    seq_lock();
                    e->droneMode = !e->droneMode;
                    seq_unlock();
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
        }

         
        int sy = 38, col1X = 14, colW = (w - 38) / 2, col2X = col1X + colW + 10;
        for (int p = 0; p < 10; ++p) {
            int col = (p < 5) ? 0 : 1;
            int row = (p < 5) ? p : (p - 5);
            int sx = (col == 0) ? col1X : col2X;
            int py = sy + row * 19;
            int railX = sx + 54, railW = colW - 108;

            if (my >= py && my <= py + 16 && mx >= railX - 4 && mx <= railX + railW + 4) {
                e->isDraggingParam = true;
                e->dragParam = p;
                SetCapture(hwnd);

                float norm = (float)(mx - railX) / (float)railW;
                if (norm < 0.0f) norm = 0.0f;
                if (norm > 1.0f) norm = 1.0f;

                
                float mins[] = { 5.0f, 1.0f, 0.0f, 0.0f, 0.0f, -24.0f, 0.0f, 0.01f, 0.01f, 0.0f };
                float maxs[] = { 2500.0f, 80.0f, 1.0f, 1.0f, 1.0f, 24.0f, 12.0f, 0.50f, 0.50f, 1.50f };

                float *params[] = {
                    &e->grainSizeMs, &e->density, &e->position, &e->posJitter, &e->panSpread,
                    &e->pitch, &e->pitchJitter, &e->attack, &e->release, &e->volume
                };
                seq_lock();
                *params[p] = mins[p] + norm * (maxs[p] - mins[p]);
                seq_unlock();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }

         
        int botY = h - 26;
        if (my >= botY && my <= botY + 20) {
            if (mx >= 14 && mx <= 100) {
                seq_lock();
                e->noteCount = 0;
                seq_unlock();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (mx >= 106 && mx <= 206) {
                if (e->octaveShift < 3) e->octaveShift++;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (mx >= 212 && mx <= 302) {
                g_granKbMode = !g_granKbMode;
                if (g_granKbMode) {
                    SetFocus(hwnd);
                } else {
                    seq_lock();
                    gran_audition_clear(e);
                    seq_unlock();
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }

        int rollY = 158;
        int rollH = h - rollY - 32;
        int noteDispW = 46;

         
        if (mx >= 14 && mx < 14 + noteDispW && my >= rollY && my < rollY + rollH) {
            float rowH = (float)rollH / (float)GRAN_PIANO_KEYS;
            int k = (int)((my - rollY) / rowH);
            if (k >= 0 && k < GRAN_PIANO_KEYS) {
                int baseNote = gran_get_base_note(e);
                int midi = baseNote + (GRAN_PIANO_KEYS - 1 - k);
                seq_lock();
                gran_audition_set_mouse(e, midi);
                float clipRate = gran_get_clip_playback_rate(e);
                gran_spawn_grain(e, gran_midi_to_rate(midi, e->pitch), 0.95f, clipRate);
                seq_unlock();
                SetCapture(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

         
        int gridX = 14 + noteDispW + 2;
        int gridW = w - 30 - noteDispW;
        if (mx >= gridX && mx < gridX + gridW && my >= rollY && my < rollY + rollH) {
            float totalBeats = gran_get_total_beats(e);
            float ppb = (float)gridW / totalBeats;

            int edge = 0;
            int hitNote = gran_get_note_under_mouse(e, mx, my, gridX, rollY, gridW, rollH, &edge);

            if (hitNote >= 0) {
                bool ctrlHeld = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                seq_lock();
                if (ctrlHeld) {
                    e->notes[hitNote].isSelected = true;
                    e->isCtrlDuplicating = true;
                } else if (!shiftHeld) {
                    if (!e->notes[hitNote].isSelected) {
                        for (int i = 0; i < e->noteCount; ++i) e->notes[i].isSelected = false;
                        e->notes[hitNote].isSelected = true;
                    }
                } else {
                    e->notes[hitNote].isSelected = !e->notes[hitNote].isSelected;
                }

                e->selectedNote = hitNote;
                e->dragStartX = mx;
                e->dragStartY = my;
                e->hasMovedPastThreshold = false;

                float clickBeat = (float)(mx - gridX) / ppb;
                e->dragStartBeatOffset = clickBeat - e->notes[hitNote].startBeat;
                e->dragLeadBeatOrig = e->notes[hitNote].startBeat;
                e->dragLeadMidiOrig = e->notes[hitNote].midiNote;

                for (int i = 0; i < e->noteCount; ++i) {
                    if (e->notes[i].isSelected) {
                        e->notes[i].dragStartBeatOrig = e->notes[i].startBeat;
                        e->notes[i].dragLengthOrig    = e->notes[i].lengthBeats;
                        e->notes[i].dragMidiOrig      = e->notes[i].midiNote;
                    }
                }

                if (edge == 1) {
                    e->isResizingLeft = true;
                } else if (edge == 2) {
                    e->isResizingRight = true;
                } else {
                    e->isDraggingNote = true;
                }
                seq_unlock();

                SetCapture(hwnd);
            } else {
                seq_lock();
                if (!(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_SHIFT) & 0x8000)) {
                    for (int i = 0; i < e->noteCount; ++i) e->notes[i].isSelected = false;
                }
                seq_unlock();

                e->isMarqueeSelecting = true;
                e->marqueeStartX = mx;
                e->marqueeStartY = my;
                e->marqueeCurX = mx;
                e->marqueeCurY = my;
                e->dragStartX = mx;
                e->dragStartY = my;
                e->hasMovedPastThreshold = false;
                SetCapture(hwnd);
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!e) return 0;
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        if (e->isDraggingParam) {
            int p = e->dragParam;
            int col = (p < 5) ? 0 : 1;
            int col1X = 14, colW = (w - 38) / 2, col2X = col1X + colW + 10;
            int sx = (col == 0) ? col1X : col2X;
            int railX = sx + 54, railW = colW - 108;

            float norm = (float)(mx - railX) / (float)railW;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;

            float mins[] = { 5.0f, 1.0f, 0.0f, 0.0f, 0.0f, -24.0f, 0.0f, 0.01f, 0.01f, 0.0f };
            float maxs[] = { 2500.0f, 80.0f, 1.0f, 1.0f, 1.0f, 24.0f, 12.0f, 0.50f, 0.50f, 1.50f };

            float *params[] = {
                &e->grainSizeMs, &e->density, &e->position, &e->posJitter, &e->panSpread,
                &e->pitch, &e->pitchJitter, &e->attack, &e->release, &e->volume
            };
            seq_lock();
            *params[p] = mins[p] + norm * (maxs[p] - mins[p]);
            seq_unlock();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        if (e->mouseNote >= 0) {
            int rollY = 158;
            int rollH = h - rollY - 32;
            float rowH = (float)rollH / (float)GRAN_PIANO_KEYS;
            int k = (int)((my - rollY) / rowH);
            if (k >= 0 && k < GRAN_PIANO_KEYS) {
                int baseNote = gran_get_base_note(e);
                int midi = baseNote + (GRAN_PIANO_KEYS - 1 - k);
                if (midi != e->mouseNote) {
                    seq_lock();
                    gran_audition_set_mouse(e, midi);
                    float clipRate = gran_get_clip_playback_rate(e);
                    gran_spawn_grain(e, gran_midi_to_rate(midi, e->pitch), 0.95f, clipRate);
                    seq_unlock();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
        }

        int rollY = 158;
        int rollH = h - rollY - 32;
        int noteDispW = 46;
        int gridX = 14 + noteDispW + 2;
        int gridW = w - 30 - noteDispW;
        float rowH = (float)rollH / (float)GRAN_PIANO_KEYS;
        float totalBeats = gran_get_total_beats(e);
        float ppb = (float)gridW / totalBeats;
        int baseNote = gran_get_base_note(e);

         
        if (e->isMarqueeSelecting) {
            e->marqueeCurX = mx;
            e->marqueeCurY = my;

            if (abs(mx - e->dragStartX) > 4 || abs(my - e->dragStartY) > 4) {
                e->hasMovedPastThreshold = true;
            }

            int mX1 = min(e->marqueeStartX, e->marqueeCurX);
            int mX2 = max(e->marqueeStartX, e->marqueeCurX);
            int mY1 = min(e->marqueeStartY, e->marqueeCurY);
            int mY2 = max(e->marqueeStartY, e->marqueeCurY);

            seq_lock();
            for (int i = 0; i < e->noteCount; ++i) {
                GranNote *n = &e->notes[i];
                if (!n->active) continue;
                int keyIdx = (GRAN_PIANO_KEYS - 1) - (n->midiNote - baseNote);
                if (keyIdx < 0 || keyIdx >= GRAN_PIANO_KEYS) continue;

                int nx1 = gridX + (int)(n->startBeat * ppb);
                int nx2 = nx1 + (int)(n->lengthBeats * ppb);
                int ny1 = rollY + (int)(keyIdx * rowH) + 1;
                int ny2 = ny1 + (int)rowH - 1;

                n->isSelected = !(nx2 < mX1 || nx1 > mX2 || ny2 < mY1 || ny1 > mY2);
            }
            seq_unlock();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

         
        if (e->isResizingLeft && e->selectedNote >= 0) {
            float mouseBeat = (float)(mx - gridX) / ppb;
            mouseBeat = quantize_beat_16th(mouseBeat);
            float deltaBeats = mouseBeat - e->dragLeadBeatOrig;

            seq_lock();
            for (int i = 0; i < e->noteCount; ++i) {
                if (!e->notes[i].isSelected) continue;
                GranNote *n = &e->notes[i];
                float origRight = n->dragStartBeatOrig + n->dragLengthOrig;
                float newStart = n->dragStartBeatOrig + deltaBeats;
                if (newStart < 0.0f) newStart = 0.0f;
                float newLen = origRight - newStart;
                if (newLen < 0.25f) {
                    newLen = 0.25f;
                    newStart = origRight - newLen;
                    if (newStart < 0.0f) newStart = 0.0f;
                }
                n->startBeat = newStart;
                n->lengthBeats = newLen;
            }
            seq_unlock();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

         
        if (e->isResizingRight && e->selectedNote >= 0) {
            float mouseBeat = (float)(mx - gridX) / ppb;
            mouseBeat = quantize_beat_16th(mouseBeat);
            float deltaBeats = mouseBeat - (e->dragLeadBeatOrig + e->notes[e->selectedNote].dragLengthOrig);

            seq_lock();
            for (int i = 0; i < e->noteCount; ++i) {
                if (!e->notes[i].isSelected) continue;
                GranNote *n = &e->notes[i];
                float newLen = n->dragLengthOrig + deltaBeats;
                if (newLen < 0.25f) newLen = 0.25f;
                if (n->startBeat + newLen > totalBeats) newLen = totalBeats - n->startBeat;
                n->lengthBeats = newLen;
            }
            seq_unlock();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

         
        if (e->isDraggingNote && e->selectedNote >= 0) {
            if (!e->hasMovedPastThreshold) {
                if (abs(mx - e->dragStartX) > 4 || abs(my - e->dragStartY) > 4) {
                    e->hasMovedPastThreshold = true;
                    if (e->isCtrlDuplicating) {
                        seq_lock();
                        int origCount = e->noteCount;
                        int newLeadIdx = e->selectedNote;
                        for (int i = 0; i < origCount; ++i) {
                            if (e->notes[i].isSelected && e->noteCount < GRAN_MAX_NOTES) {
                                int cloneIdx = e->noteCount++;
                                e->notes[cloneIdx] = e->notes[i];
                                e->notes[cloneIdx].isSelected = true;
                                e->notes[i].isSelected = false;
                                if (i == e->selectedNote) newLeadIdx = cloneIdx;
                            }
                        }
                        e->selectedNote = newLeadIdx;
                        e->isCtrlDuplicating = false;
                        seq_unlock();
                    }
                }
            }

            if (e->hasMovedPastThreshold) {
                float mouseBeat = (float)(mx - gridX) / ppb;
                float newLeadBeat = quantize_beat_16th(mouseBeat - e->dragStartBeatOffset);
                float deltaBeat = newLeadBeat - e->dragLeadBeatOrig;

                int curKey = (int)((my - rollY) / rowH);
                if (curKey < 0) curKey = 0;
                if (curKey >= GRAN_PIANO_KEYS) curKey = GRAN_PIANO_KEYS - 1;
                int newLeadMidi = baseNote + (GRAN_PIANO_KEYS - 1 - curKey);
                int deltaMidi = newLeadMidi - e->dragLeadMidiOrig;

                float minAllowedBeatDelta = -1e9f, maxAllowedBeatDelta = 1e9f;
                int minAllowedMidiDelta = -999, maxAllowedMidiDelta = 999;

                seq_lock();
                for (int i = 0; i < e->noteCount; ++i) {
                    if (e->notes[i].isSelected) {
                        float lDelta = -e->notes[i].dragStartBeatOrig;
                        if (lDelta > minAllowedBeatDelta) minAllowedBeatDelta = lDelta;
                        float rDelta = totalBeats - (e->notes[i].dragStartBeatOrig + e->notes[i].dragLengthOrig);
                        if (rDelta < maxAllowedBeatDelta) maxAllowedBeatDelta = rDelta;

                        int minM = baseNote - e->notes[i].dragMidiOrig;
                        int maxM = (baseNote + GRAN_PIANO_KEYS - 1) - e->notes[i].dragMidiOrig;
                        if (minM > minAllowedMidiDelta) minAllowedMidiDelta = minM;
                        if (maxM < maxAllowedMidiDelta) maxAllowedMidiDelta = maxM;
                    }
                }
                if (deltaBeat < minAllowedBeatDelta) deltaBeat = minAllowedBeatDelta;
                if (deltaBeat > maxAllowedBeatDelta) deltaBeat = maxAllowedBeatDelta;
                if (deltaMidi < minAllowedMidiDelta) deltaMidi = minAllowedMidiDelta;
                if (deltaMidi > maxAllowedMidiDelta) deltaMidi = maxAllowedMidiDelta;

                for (int i = 0; i < e->noteCount; ++i) {
                    if (e->notes[i].isSelected) {
                        float b = e->notes[i].dragStartBeatOrig + deltaBeat;
                        if (b < 0.0f) b = 0.0f;
                        int m = e->notes[i].dragMidiOrig + deltaMidi;
                        e->notes[i].startBeat = b;
                        e->notes[i].midiNote = m;
                    }
                }
                seq_unlock();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }

         
        if (!e->isDraggingNote && !e->isResizingLeft && !e->isResizingRight && !e->isMarqueeSelecting) {
            int edge = 0;
            int hNote = gran_get_note_under_mouse(e, mx, my, gridX, rollY, gridW, rollH, &edge);
            if (hNote >= 0 && (edge == 1 || edge == 2)) {
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            } else {
                SetCursor(LoadCursor(NULL, IDC_ARROW));
            }
        }

        return 0;
    }

    case WM_LBUTTONUP: {
        if (!e) return 0;
        if (e->isDraggingParam) {
            e->isDraggingParam = false;
            ReleaseCapture();
        }
        if (e->mouseNote >= 0) {
            seq_lock();
            gran_audition_set_mouse(e, -1);
            seq_unlock();
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        if (e->isMarqueeSelecting) {
            if (!e->hasMovedPastThreshold) {
                RECT rcClient;
                GetClientRect(hwnd, &rcClient);
                int clientW = rcClient.right - rcClient.left;
                int clientH = rcClient.bottom - rcClient.top;

                int rollY = 158;
                int rollH = clientH - rollY - 32;
                int noteDispW = 46;
                int gridX = 14 + noteDispW + 2;
                int gridW = clientW - 30 - noteDispW;
                float rowH = (float)rollH / (float)GRAN_PIANO_KEYS;
                float totalBeats = gran_get_total_beats(e);
                float ppb = (float)gridW / totalBeats;

                float clickBeat = quantize_beat_16th((float)(e->dragStartX - gridX) / ppb);
                int k = (int)((e->dragStartY - rollY) / rowH);
                if (k >= 0 && k < GRAN_PIANO_KEYS) {
                    int baseNote = gran_get_base_note(e);
                    int midi = baseNote + (GRAN_PIANO_KEYS - 1 - k);
                    seq_lock();
                    if (e->noteCount < GRAN_MAX_NOTES) {
                        GranNote *n = &e->notes[e->noteCount++];
                        n->active = true;
                        n->midiNote = midi;
                        n->velocity = 0.85f;
                        n->startBeat = clickBeat;
                        n->lengthBeats = 1.0f;
                        if (n->startBeat + n->lengthBeats > totalBeats) n->lengthBeats = totalBeats - n->startBeat;
                        if (n->lengthBeats < 0.25f) n->lengthBeats = 0.25f;
                        n->isSelected = true;
                    }
                    seq_unlock();
                }
            }
            e->isMarqueeSelecting = false;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        if (e->isDraggingNote || e->isResizingLeft || e->isResizingRight) {
            e->isDraggingNote = false;
            e->isResizingLeft = false;
            e->isResizingRight = false;
            e->isCtrlDuplicating = false;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_RBUTTONDOWN: {
        if (!e) return 0;
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        int botY = h - 26;
        if (my >= botY && my <= botY + 20) {
            if (mx >= 106 && mx <= 206) {
                if (e->octaveShift > -3) e->octaveShift--;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }

        int rollY = 158;
        int rollH = h - rollY - 32;
        int noteDispW = 46;
        int gridX = 14 + noteDispW + 2;
        int gridW = w - 30 - noteDispW;

        int edge = 0;
        int hit = gran_get_note_under_mouse(e, mx, my, gridX, rollY, gridW, rollH, &edge);
        if (hit >= 0) {
            seq_lock();
            if (e->notes[hit].isSelected) {
                for (int i = 0; i < e->noteCount;) {
                    if (e->notes[i].isSelected) {
                        for (int j = i; j < e->noteCount - 1; ++j) e->notes[j] = e->notes[j + 1];
                        e->noteCount--;
                    } else i++;
                }
            } else {
                for (int j = hit; j < e->noteCount - 1; ++j) e->notes[j] = e->notes[j + 1];
                e->noteCount--;
            }
            seq_unlock();
        } else if (mx >= gridX && mx <= gridX + gridW && my >= rollY && my <= rollY + rollH) {
             
            e->isMarqueeSelecting = true;
            e->marqueeStartX = mx; e->marqueeStartY = my;
            e->marqueeCurX = mx;   e->marqueeCurY = my;
            e->dragStartX = mx;    e->dragStartY = my;
            e->hasMovedPastThreshold = false;
            SetCapture(hwnd);
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_RBUTTONUP: {
        if (e && e->isMarqueeSelecting) {
            if (!e->hasMovedPastThreshold) {
                seq_lock();
                for (int i = 0; i < e->noteCount; ++i) e->notes[i].isSelected = false;
                seq_unlock();
            }
            e->isMarqueeSelecting = false;
            e->hasMovedPastThreshold = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        // Wheel over the Octave button shifts the roll one octave per notch
        // (up = octave up, down = octave down), matching the L/R click path.
        if (!e) return 0;
        short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (zDelta == 0) break;
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        RECT rcW; GetClientRect(hwnd, &rcW);
        int botY = (rcW.bottom - rcW.top) - 26;
        if (pt.x >= 106 && pt.x <= 206 && pt.y >= botY && pt.y <= botY + 20) {
            if (zDelta > 0) {
                if (e->octaveShift < 3) e->octaveShift++;
            } else {
                if (e->octaveShift > -3) e->octaveShift--;
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        break;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE || wParam == VK_RETURN) {
            if (e) {
                seq_lock();
                gran_audition_clear(e);
                seq_unlock();
            }
            ShowWindow(hwnd, SW_HIDE);
        } else if ((wParam == VK_DELETE || wParam == VK_BACK) && e) {
            seq_lock();
            for (int i = 0; i < e->noteCount;) {
                if (e->notes[i].isSelected) {
                    for (int j = i; j < e->noteCount - 1; ++j) e->notes[j] = e->notes[j + 1];
                    e->noteCount--;
                } else i++;
            }
            seq_unlock();
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) && e) {
            seq_lock();
            for (int i = 0; i < e->noteCount; ++i) e->notes[i].isSelected = true;
            seq_unlock();
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (wParam == 'D' && (GetKeyState(VK_CONTROL) & 0x8000) && e) {
            seq_lock();
            for (int i = 0; i < e->noteCount; ++i) e->notes[i].isSelected = false;
            seq_unlock();
            InvalidateRect(hwnd, NULL, FALSE);
            
        } else if (wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000) && e) {
            seq_lock();
            g_granNoteClipboardCount = 0;
            for (int i = 0; i < e->noteCount && g_granNoteClipboardCount < GRAN_MAX_NOTES; ++i) {
                if (e->notes[i].isSelected && e->notes[i].active)
                    g_granNoteClipboard[g_granNoteClipboardCount++] = e->notes[i];
            }
            seq_unlock();
        } else if (wParam == 'V' && (GetKeyState(VK_CONTROL) & 0x8000) && e) {
            seq_lock();
            for (int i = 0; i < g_granNoteClipboardCount && e->noteCount < GRAN_MAX_NOTES; ++i) {
                GranNote n = g_granNoteClipboard[i];
                n.isSelected = true;
                e->notes[e->noteCount++] = n;
            }
            seq_unlock();
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (g_granKbMode && e &&
                   !(GetKeyState(VK_CONTROL) & 0x8000) &&
                   !(GetKeyState(VK_SHIFT) & 0x8000) &&
                   !(GetKeyState(VK_MENU) & 0x8000)) {
            // Keyboard note audition (A-P QWERTY row, relative to the roll's
            // current octave). Notes join the same polyphonic held-set as the
            // mouse strip, so keyboard and mouse chords coexist. Auto-repeat
            // keeps the note held but only spawns a fresh grain on the
            // initial press.
            int semi = pr_key_to_semitone((int)wParam);
            if (semi >= 0) {
                bool isRepeat = (lParam & 0x40000000) != 0;
                int base = gran_get_base_note(e);
                int midi = base + semi;
                if (midi > 127) midi = 127;
                if (!isRepeat) {
                    seq_lock();
                    gran_audition_kb_add(e, (int)wParam, midi);
                    float clipRate = gran_get_clip_playback_rate(e);
                    gran_spawn_grain(e, gran_midi_to_rate(midi, e->pitch), 0.95f, clipRate);
                    seq_unlock();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
        }
        return 0;

    case WM_KEYUP:
        // Release by the physical key: each entry's pitch was resolved at
        // press time, so re-resolving here (current octave) could miss the
        // entry and strand the held note.
        if (e && g_granKbMode && pr_key_to_semitone((int)wParam) >= 0) {
            seq_lock();
            gran_audition_kb_remove(e, (int)wParam);
            seq_unlock();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        break;

    case WM_CLOSE:
        if (e) {
            seq_lock();
            gran_audition_clear(e);
            seq_unlock();
        }
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DROPFILES: {
        // Drop an audio file on the [SAMPLE: ...] selector (track mode) to
        // load it as the engine's own sample, replacing whatever was there.
        HDROP hDrop = (HDROP)wParam;
        POINT pt;
        DragQueryPoint(hDrop, &pt);
        GranularEngine *eDrop = gran_get_current_engine();
        if (!eDrop || eDrop->clipIdx >= 0) { DragFinish(hDrop); return 0; }

        int padX = 14, gapX = 8, sBoxY = 10, sBoxH = 22;
        RECT rcDrop;
        GetClientRect(hwnd, &rcDrop);
        int totalBtnW = rcDrop.right - rcDrop.left - 2 * padX;
        int btnW = (totalBtnW - 3 * gapX) / 4;
        // sBoxRc in track mode (matches WM_PAINT layout).
        RECT sBoxRc = { padX, sBoxY, padX + btnW, sBoxY + sBoxH };
        if (pt.x < sBoxRc.left || pt.x > sBoxRc.right || pt.y < sBoxRc.top || pt.y > sBoxRc.bottom) {
            DragFinish(hDrop);
            return 0;
        }
        wchar_t filepathW[MAX_PATH];
        if (DragQueryFileW(hDrop, 0, filepathW, MAX_PATH)) {
            ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, NUM_CHANNELS, SAMPLE_RATE);
            ma_decoder dec;
            if (ma_decoder_init_file_w(filepathW, &cfg, &dec) == MA_SUCCESS) {
                ma_uint64 total = 0;
                ma_decoder_get_length_in_pcm_frames(&dec, &total);
                if (total > 0) {
                    float *buf = (float*)malloc(sizeof(float) * total * NUM_CHANNELS);
                    if (buf) {
                        ma_uint64 read = 0;
                        ma_decoder_read_pcm_frames(&dec, buf, total, &read);
                        seq_lock();
                        if (eDrop->ownFrames) free(eDrop->ownFrames);
                        eDrop->ownFrames = buf;
                        eDrop->ownFrameCount = read;
                        eDrop->ownLoaded = true;
                        const wchar_t *base = wcsrchr(filepathW, L'\\');
                        base = base ? base + 1 : filepathW;
                        wide_to_utf8_buf(base, eDrop->ownSampleName, (int)sizeof(eDrop->ownSampleName));
                        eDrop->sampleIndex = -1;
                        memset(&eDrop->ownSample, 0, sizeof(AudioSample));
                        strncpy(eDrop->ownSample.name, eDrop->ownSampleName, sizeof(eDrop->ownSample.name) - 1);
                        eDrop->ownSample.pFrames = eDrop->ownFrames;
                        eDrop->ownSample.frameCount = eDrop->ownFrameCount;
                        eDrop->ownSample.loaded = true;
                        seq_unlock();
                    }
                }
                ma_decoder_uninit(&dec);
            }
        }
        DragFinish(hDrop);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_DESTROY:
        g_granHwnd = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

 
static inline void open_granular_dialog(HWND parentHwnd, int trackIdx) {
    if (trackIdx < 0 || trackIdx >= MAX_TRACKS) return;
    g_granTrack = trackIdx;
    g_granClip = -1;

    GranularEngine *e = &g_TrackGran[trackIdx];
    e->trackIdx = trackIdx;
    e->clipIdx = -1;

    seq_lock();
    gran_audition_clear(e);
    seq_unlock();

    
    
    if (e->grainSizeMs < 1.0f) {
        e->enabled = true;
        e->grainSizeMs = 50.0f;
        e->density = 20.0f;
        e->position = 0.20f;
        e->posJitter = 0.20f;
        e->pitch = 0.0f;
        e->pitchJitter = 0.0f;
        e->panSpread = 0.30f;
        e->attack = 0.05f;
        e->release = 0.35f;
        e->volume = 0.75f;
        e->freeze = false;
        e->droneMode = false;
        e->sampleIndex = -1;
        
        
        e->octaveShift = 0;
    }

    if (!g_granHwnd) {
        static bool s_registered = false;
        if (!s_registered) {
            WNDCLASSA wc = {0};
            wc.lpfnWndProc   = GranWndProc;
            wc.hInstance     = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractGranularClass";
            wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_registered = true;
        }

        int rw = 720, rh = 640;
        int scrW = GetSystemMetrics(SM_CXSCREEN);
        int scrH = GetSystemMetrics(SM_CYSCREEN);
        int rx = (scrW - rw) / 2;
        int ry = (scrH - rh) / 2;

        g_granHwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            "RefractGranularClass",
            "Granular Editor",
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE,
            rx, ry, rw, rh,
            parentHwnd, NULL, GetModuleHandle(NULL), NULL
        );
        SetTimer(g_granHwnd, 1, 16, NULL);
        DragAcceptFiles(g_granHwnd, TRUE);
    }

    char titleBuf[64];
    snprintf(titleBuf, sizeof(titleBuf), "Granular Editor - Track %d", trackIdx + 1);
    SetWindowTextA(g_granHwnd, titleBuf);

    ShowWindow(g_granHwnd, SW_SHOW);
    SetForegroundWindow(g_granHwnd);
    InvalidateRect(g_granHwnd, NULL, FALSE);
}

static inline void open_clip_granular_dialog(HWND parentHwnd, int clipIdx) {
    if (clipIdx < 0 || clipIdx >= g_Seq.clipCount) return;

    seq_lock();
    Clip *c = &g_Seq.clips[clipIdx];
    c->isGranular = true;
    int trackIdx = c->track;
    if (trackIdx < 0 || trackIdx >= MAX_TRACKS) trackIdx = 0;

    g_granTrack = trackIdx;
    g_granClip = clipIdx;

    GranularEngine *e = &g_ClipGran[clipIdx];
    if (e->grainSizeMs < 1.0f) {
        e->grainSizeMs = 40.0f;
        e->density = 25.0f;
        e->position = 0.20f;
        e->posJitter = 0.15f;
        e->pitch = 0.0f;
        e->pitchJitter = 0.25f;
        e->panSpread = 0.40f;
        e->attack = 0.10f;
        e->release = 0.25f;
        e->volume = 0.85f;
        e->freeze = false;
        e->droneMode = false;
        e->octaveShift = 0;
    }
    e->enabled = true;
    e->sampleIndex = c->sampleIndex;
    e->trackIdx = trackIdx;
    e->clipIdx = clipIdx;
    gran_audition_clear(e);   // drop any stale held note from a prior session
    seq_unlock();

    g_timelineDirty = true;
    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);

    open_granular_dialog(parentHwnd, trackIdx);

    g_granClip = clipIdx;

    if (g_granHwnd) {
        char titleBuf[64];
        snprintf(titleBuf, sizeof(titleBuf), "Granular Editor - Clip %d (Track %d)", clipIdx + 1, trackIdx + 1);
        SetWindowTextA(g_granHwnd, titleBuf);
        InvalidateRect(g_granHwnd, NULL, FALSE);
    }
}



static inline void granular_toggle_clip(int clipIdx) {
    if (clipIdx < 0 || clipIdx >= g_Seq.clipCount) return;
    seq_lock();
    Clip *c = &g_Seq.clips[clipIdx];
    bool targetState = !c->isGranular;
    if (c->isSelected) {
        for (int i = 0; i < g_Seq.clipCount; ++i) {
            if (g_Seq.clips[i].isSelected) {
                g_Seq.clips[i].isGranular = targetState;
                g_ClipGran[i].enabled = targetState;
                g_ClipGran[i].sampleIndex = g_Seq.clips[i].sampleIndex;
                g_ClipGran[i].trackIdx = g_Seq.clips[i].track;
                g_ClipGran[i].clipIdx = i;
            }
        }
    } else {
        c->isGranular = targetState;
        g_ClipGran[clipIdx].enabled = targetState;
        g_ClipGran[clipIdx].sampleIndex = c->sampleIndex;
        g_ClipGran[clipIdx].trackIdx = c->track;
        g_ClipGran[clipIdx].clipIdx = clipIdx;
    }
    seq_unlock();
    g_timelineDirty = true;
}

static inline void granular_init_all(void) {
    memset(g_TrackGran, 0, sizeof(g_TrackGran));
    memset(g_ClipGran, 0, sizeof(g_ClipGran));
    for (int t = 0; t < MAX_TRACKS; ++t) {
        g_TrackGran[t].trackIdx = t;
        g_TrackGran[t].clipIdx = -1;
        g_TrackGran[t].sampleIndex = -1;
        g_TrackGran[t].volume = 0.85f;
    }
    for (int c = 0; c < MAX_CLIPS; ++c) {
        g_ClipGran[c].trackIdx = -1;
        g_ClipGran[c].clipIdx = c;
        g_ClipGran[c].sampleIndex = -1;
        g_ClipGran[c].volume = 0.85f;
    }
}