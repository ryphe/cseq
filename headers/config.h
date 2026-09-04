#pragma once

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

 
#define TARGET_MAIN_FPS        120.0
#define TARGET_FRAME_TIME_MS   (1000.0 / TARGET_MAIN_FPS)  

 
#define CSEQ_VERSION_MAJOR 1
#define CSEQ_VERSION_MINOR 0
#define CSEQ_VERSION_STRING "3.5"

 
#define IDR_INTER_FONT 101

 
#define EXPORT_CHUNK_FRAMES 4096

 
#define SAMPLE_RATE 44100
#define NUM_CHANNELS 2
#define MAX_TRACKS 128
#define MIN_TRACKS 1
#define MAX_SAMPLES 256 // bumped from 64
#define MAX_CLIPS 2048
#define BEATS_PER_BAR 4
#define FADE_SAMPLES 64
#define MAX_UNDO_STATES 32
#define MIDI_MAX_VOICES 32

 
#define HEADER_HEIGHT_BASE 68    
#define TRACK_HEIGHT_BASE 78
#define BOTTOM_DOCK_HEIGHT_BASE 44
#define TRACK_HEADER_WIDTH_BASE 100   
#define PIXELS_PER_BEAT_BASE 64.0f
#define DRAG_THRESHOLD_BASE 8

 
extern float g_dpiScaleX;
extern float g_dpiScaleY;

static inline int scale_x(int v) {
    int r = (int)((float)v * g_dpiScaleX + 0.5f);
    return r < 1 ? 1 : r;
}
static inline int scale_y(int v) {
    int r = (int)((float)v * g_dpiScaleY + 0.5f);
    return r < 1 ? 1 : r;
}
static inline float scale_font(float size) { return size * ((g_dpiScaleX + g_dpiScaleY) * 0.5f); }

 
static inline int get_header_height(void)      { return scale_y(HEADER_HEIGHT_BASE); }
static inline int get_track_height(void)       { return scale_y(TRACK_HEIGHT_BASE); }
static inline int get_bottom_dock_height(void) { return scale_y(BOTTOM_DOCK_HEIGHT_BASE); }
static inline int get_track_header_width(void) { return scale_x(TRACK_HEADER_WIDTH_BASE); }
static inline int get_drag_threshold(void)     { return scale_x(DRAG_THRESHOLD_BASE); }

 
#define MIN_BARS 1
#define MAX_BARS 1024
#define BAR_BITFIELD_WORDS (MAX_BARS / 64)    
#define MAX_CLIPS_PER_BAR 32    
#define MIN_CLIP_LENGTH_BEATS 0.25f
#define FAKE_ATTRACTOR_POINTS 64

 
#define GRAN_MAX_GRAINS 128
#define GRAN_MAX_NOTES 256

 
#define MIDI_MAX_NOTES 256
#define MIDI_CLIP_INSERT_BEATS 0.25f    
#define GRAN_GRAIN_SIZE_MIN 32
#define GRAN_GRAIN_SIZE_MAX 250000   
#define GRAN_PIANO_KEYS 24
#define GRAN_PIANO_BASE 48
#define GRAN_ROLL_BARS 4

 
enum {
    ID_RATE_CUSTOM = 1000, ID_RATE_050, ID_RATE_075, ID_RATE_100, ID_RATE_125, ID_RATE_150, ID_RATE_200,
    ID_VOL_RESET = 1010, ID_CLIP_DELETE = 1011, ID_CLIP_MUTE = 1012,
    ID_CLIP_FADE_IN_000 = 1050, ID_CLIP_FADE_IN_025, ID_CLIP_FADE_IN_050, ID_CLIP_FADE_IN_100, ID_CLIP_FADE_IN_150, ID_CLIP_FADE_IN_200, ID_CLIP_FADE_IN_300,
    ID_CLIP_FADE_OUT_000 = 1060, ID_CLIP_FADE_OUT_025, ID_CLIP_FADE_OUT_050, ID_CLIP_FADE_OUT_100, ID_CLIP_FADE_OUT_150, ID_CLIP_FADE_OUT_200, ID_CLIP_FADE_OUT_300,
    ID_CLIP_FADE_CLEAR = 1067,
    ID_TRACK_MUTE = 1020, ID_TRACK_CLEAR = 1040, ID_TRACK_EQ = 1090, ID_TRACK_PAN_WIDTH = 1095,
    ID_TRACK_RATE_CUSTOM = 1030, ID_TRACK_RATE_050, ID_TRACK_RATE_075, ID_TRACK_RATE_100, ID_TRACK_RATE_125, ID_TRACK_RATE_150, ID_TRACK_RATE_200,
    ID_TRACK_FADE_IN_000 = 1070, ID_TRACK_FADE_IN_025, ID_TRACK_FADE_IN_050, ID_TRACK_FADE_IN_100, ID_TRACK_FADE_IN_150, ID_TRACK_FADE_IN_200, ID_TRACK_FADE_IN_400,
    ID_TRACK_FADE_OUT_000 = 1080, ID_TRACK_FADE_OUT_025, ID_TRACK_FADE_OUT_050, ID_TRACK_FADE_OUT_100, ID_TRACK_FADE_OUT_150, ID_TRACK_FADE_OUT_200, ID_TRACK_FADE_OUT_400,
    ID_TRACK_FADE_CLEAR = 1091,
    ID_TRACK_GRANULAR = 1092,
    ID_CLIP_GRANULAR = 1093,
    ID_CLIP_GRANULAR_TOGGLE = 1094,
    ID_TRACK_RESET_CLIP_VOL = 1096,
    ID_EXPORT_DEPTH_16 = 1097,
    ID_EXPORT_DEPTH_24 = 1098,
    ID_EXPORT_DEPTH_32 = 1099,
    ID_GRID_1_16  = 1100,
    ID_GRID_1_16T = 1101,
    ID_GRID_1_32  = 1102,
    ID_GRID_1_32T = 1103,
    ID_CLIP_MIDI_EDIT = 1104,
    ID_TRACK_FX_RACK = 1105,
    ID_TRACK_SOLO = 1106,
    ID_TRACK_FILTER = 1107,
    ID_TRACK_TRIGGER_PROB = 1108,
    ID_CLIP_SLICE = 1109
};

 
enum FadeCurveType {
    FADE_CURVE_LINEAR = 0,  
    FADE_CURVE_EXP,         
    FADE_CURVE_SMOOTH,      
    FADE_CURVE_LOG,         
    FADE_CURVE_COUNT
};

 
enum {
    ID_FADE_IN_CURVE_LIN = 1110,
    ID_FADE_IN_CURVE_EXP,
    ID_FADE_IN_CURVE_SMOOTH,
    ID_FADE_IN_CURVE_LOG,
    ID_FADE_OUT_CURVE_LIN = 1120,
    ID_FADE_OUT_CURVE_EXP,
    ID_FADE_OUT_CURVE_SMOOTH,
    ID_FADE_OUT_CURVE_LOG
};