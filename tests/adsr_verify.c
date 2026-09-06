// adsr_verify.c — numeric sanity check for the MIDI audition ADSR math.
// The two functions below are copied verbatim from headers/audio.h
// (midi_adsr_level_at / midi_adsr_gain, the shape shared by the PLAY-loop
// voices and the purple-roll audition slots). This verifies the envelope
// semantics the audition slots rely on:
//   1. Held note runs A -> D -> S and settles on the Sustain knob.
//   2. Release tail starts from the exact level-at-release and fades over
//      relFrames (midi_adsr_gain with noteFrames frozen at the release point).
//   3. Sustain 0 => silent while held, no tail after release.
#include <stdio.h>
#include <math.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); g_fail++; } \
    else           { printf("ok  : %s\n", msg); } \
} while (0)

static inline float midi_adsr_level_at(double posInNote, double noteFrames,
                                       float atkFrames, float decFrames, float sustain) {
    (void)noteFrames;
    if (atkFrames > 0.0f && posInNote < (double)atkFrames) {
        float x = (float)(posInNote / (double)atkFrames);
        if (x > 1.0f) x = 1.0f;
        return x;
    }
    double decStart = (double)atkFrames;
    double decEnd = decStart + (double)decFrames;
    if (decFrames > 0.0f && posInNote < decEnd) {
        float x = (float)((posInNote - decStart) / (double)decFrames);
        if (x < 0.0f) x = 0.0f;
        if (x > 1.0f) x = 1.0f;
        return 1.0f + (sustain - 1.0f) * x;
    }
    return sustain;
}

static inline float midi_adsr_gain(double posInNote, double noteFrames,
                                   float atkFrames, float decFrames, float sustain,
                                   float relFrames) {
    if (noteFrames <= 0.0) return 0.0f;
    if (posInNote < 0.0) posInNote = 0.0;

    if (sustain < 0.0f) sustain = 0.0f;
    if (sustain > 1.0f) sustain = 1.0f;

    if (posInNote < noteFrames)
        return midi_adsr_level_at(posInNote, noteFrames, atkFrames, decFrames, sustain);

    if (relFrames <= 0.0f) return 0.0f;
    double relPos = posInNote - noteFrames;
    if (relPos >= (double)relFrames) return 0.0f;
    float levelAtOff = midi_adsr_level_at(noteFrames, noteFrames, atkFrames, decFrames, sustain);
    float x = (float)(relPos / (double)relFrames);
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    return levelAtOff * (1.0f - x);
}

int main(void) {
    const float atk = 2000.0f;   // frames
    const float dec = 3000.0f;
    const float sus = 0.4f;
    const float rel = 8000.0f;
    const double held = 1.0e12;  // the slots' kHeldLen

    // 1. Held: A -> D -> S.
    float g_midAtk = midi_adsr_gain(1000.0, held, atk, dec, sus, rel);
    CHECK(fabs(g_midAtk - 0.5f) < 1e-4, "held: mid-attack gain is 0.5 at atk/2");
    float g_endAtk = midi_adsr_gain(2000.0, held, atk, dec, sus, rel);
    CHECK(fabs(g_endAtk - 1.0f) < 1e-4, "held: gain peaks at 1.0 at end of attack");
    float g_midDec = midi_adsr_gain(3500.0, held, atk, dec, sus, rel);
    CHECK(fabs(g_midDec - 0.7f) < 1e-4, "held: mid-decay gain 1->0.4 halfway = 0.7");
    float g_sus = midi_adsr_gain(20000.0, held, atk, dec, sus, rel);
    CHECK(fabs(g_sus - 0.4f) < 1e-6, "held: settles on Sustain knob (0.4)");

    // 2. Release from sustain level (envLen frozen at note-off frame).
    float r0 = midi_adsr_gain(10000.0, 10000.0, atk, dec, sus, rel);
    CHECK(fabs(r0 - 0.4f) < 1e-6, "key-up at sustain: tail starts at exactly 0.4");
    float rHalf = midi_adsr_gain(10000.0 + rel / 2, 10000.0, atk, dec, sus, rel);
    CHECK(fabs(rHalf - 0.2f) < 1e-4, "key-up at sustain: half-way tail = 0.2");
    float rEnd = midi_adsr_gain(10000.0 + rel, 10000.0, atk, dec, sus, rel);
    CHECK(rEnd == 0.0f, "key-up at sustain: tail reaches 0 at relFrames");

    // 3. Mid-attack release: tail starts from the exact attack level.
    float frozen = 500.0;  // envLen frozen 500 frames into a 2000-frame attack
    float m0 = midi_adsr_gain(500.0, frozen, atk, dec, sus, rel);
    CHECK(fabs(m0 - 0.25f) < 1e-6, "mid-attack key-up: tail starts at level-at-release (0.25)");
    float mHalf = midi_adsr_gain(500.0 + rel / 2, frozen, atk, dec, sus, rel);
    CHECK(fabs(mHalf - 0.125f) < 1e-4, "mid-attack key-up: tail fades linearly over relFrames");

    // 4. Sustain 0: silent while held, no tail.
    float s0 = midi_adsr_gain(20000.0, held, atk, dec, 0.0f, rel);
    CHECK(s0 == 0.0f, "sustain 0 held: silent");
    float s0r = midi_adsr_gain(10000.0 + 1.0, 10000.0, atk, dec, 0.0f, rel);
    CHECK(s0r == 0.0f, "sustain 0 key-up: no audible tail (retires immediately)");

    // 5. Zero release knob: sound stops at note-off, no tail.
    CHECK(midi_adsr_gain(10000.0, 10000.0, atk, dec, sus, 0.0f) == 0.0f,
          "release knob 0: no tail");

    // 6. Continuity: no click at the freeze frame. The gain step per frame
    // during a 2000-frame attack is 1/2000, so allow one frame of slew.
    float heldAt = midi_adsr_gain(499.0, held, atk, dec, sus, rel);
    float tailAt = midi_adsr_gain(500.0, 500.0, atk, dec, sus, rel);
    CHECK(fabs(heldAt - tailAt) < 2.0f / atk, "freeze frame is gain-continuous (no click)");

    printf(g_fail ? "\n%d check(s) FAILED\n" : "\nAll checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
