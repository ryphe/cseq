# cseq Source Tree Overview (1.33)

This document describes the purpose and responsibilities of each source file in the cseq project. The project is a **Unity build**. All `.c` files include `.h` files that contain full static implementations, and `main.c`'s include order defines the dependency graph.

---

## Core Engine

### `main.c`
**Entry point & orchestration.** `WinMain` initializes the entire application:
- Sets up DPI awareness, UI font, and all engine state.
- Creates the main window and registers all core window classes.
- Initializes the audio device (miniaudio) with `audio_callback`.
- Starts the 120 fps UI pacer thread.
- Handles the message loop (`GetMessageA`/`DispatchMessageA`).
- Processes command-line arguments (`.csq` project or audio file).
- Manages shutdown: stops audio, drains workers, deletes critical sections.

### `types.h`
**Core data structures & global state.** Defines the primary structs that underpin the entire engine:
- `SequencerState`�the master state container (clips, samples, tracks, playback).
- `Clip`�timeline clip metadata (sample ref, timing, volume, fade, MIDI notes, synth patches).
- `AudioSample`�decoded PCM buffer (heap or memory-mapped cache), peak cache.
- `TrackFilter`�per-track RBJ biquad filter stack.
- `GranularEngine`�granular synthesis engine state. Carries the same VK-keyed keyboard-audition set (`kbHeldVKs[]`) as `MidiEditContext` (see types.h/globals notes in arch.md section 5.6).
- `MidiEditContext`�MIDI/piano-roll editor state. Its polyphonic audition set (`auditionNotes[]` = mouse strip pitch + QWERTY keys) keys each keyboard entry by physical key (`kbHeldNotes[]`/`kbHeldVKs[]` pairs) so key-up matches even when the octave changed mid-hold.
- Track masks (`TrackMask128`), bitfields for bar dirty/valid/presence.
- Inline utilities for fixed-point math, denormal flushing, track filtering.

### `globals.h`
**Global declarations & project-wide utilities.** Exposes all major global state arrays and provides:
- `seq_lock`/`seq_unlock`�the main `CRITICAL_SECTION` for protecting `g_Seq`.
- `midi_lock`/`midi_unlock`�lock for MIDI editor audition state.
- **Job system** (`job_begin`/`job_end`/`job_set_progress`)�serializes long-running operations and reports progress to the UI.
- Track mask operations (`track_mask_test`, `track_mask_set`).
- Bar bitfield operations (`bar_bit_set`, `bar_bit_clear`, `bar_bit_test`).
- Clip/structure change notifiers (`cseq_clip_structure_changed`, `cseq_rebuild_clip_maps_only`).
- Global externs for `g_TrackGran`, `g_ClipGran`, `g_ClipHalo`, `g_ClipQuadrum`.

### `state.h`
**Undo/redo system.** Implements the snapshot-based undo/redo pipeline:
- `gran_engine_to_snapshot` / `gran_snapshot_to_engine`�serialize/deserialize granular engine state.
- `push_undo_state`�captures current `g_Seq.clips` and `g_ClipGran` into the undo stack.
- `undo_last_action` / `redo_last_action`�restores a snapshot, reinitializes synth state, and invalidates the timeline cache.
- Manages `MAX_UNDO_STATES` with full `Clip` and `GranClipSnapshot` storage�no pointer persistence.

---

## DSP & Audio Processing

### `dsp.h`
**Low-level DSP primitives & timeline math.** Provides:
- Denormal flushing (`denormal_flush_f`, `denormal_flush_ps128`, `denormal_flush_ps256`).
- Fade curve evaluation (`compute_fade_gain`�linear/exp/smooth/log).
- Peak biquad filter (`peak_biquad_set`/`peak_biquad_process`).
- Timeline geometry helpers: `get_pixels_per_beat`, `total_beats`, `frames_per_beat`.
- Swing and quantization: `apply_clip_swing`, `apply_note_clip_swing`, `quantize_beat_16th` (round-half-up), `quantize_beat_floor` (snap to the clicked grid cell, used by piano-roll note entry).
- Frame ? beat conversion (`frame_to_beat`, `beat_to_frame`).

### `audio.h`
**Real-time audio rendering callback & export engine.** This is the heart of audio production:
- `audio_callback`�miniaudio device callback. Renders per-chunk under `seq_lock`, applies per-track FX/EQ/filter, master limiter/lofi, and drives the visualizer ring buffer.
- `render_frames`�the **core mixing loop**. Renders sample clips, MIDI/SF2 clips, granular tracks, and synth clips (Halo/Quadrum) into per-track accumulators, then sums to stereo.
- **External sidechain routing.** Per-track `trackSidechainSource` (pre-FX source track index, `-1` = internal) is fed into the routed track's slot-0 compressor via per-instance `scFeedL/scFeedR/scActive` (thread-safe against concurrent live+export renders).
- `midi_process_clip_frames`�MIDI note playback with per-voice ADSR (sample or SF2 source).
- `midi_editor_process_preview`�audition rendering for the piano roll. Synth clips drive their per-clip engines; standard sample/SoundFont clips render 8 polyphonic audition slots shaped by the clip ADSR knobs (`midi_adsr_gain`: A-D-S while held, `envLen` freeze + release tail on key-up) plus the looping [PLAY] voices with release tails. Known gap: the tail of the last released key is truncated (preview early-out + synth-only keep-alive gate).
- **Export.** `ExportTimelineThreadProc` runs on a background worker, renders the full timeline from a snapshot, and writes WAV at the configured bit depth (16/24/32).
- `StereoLimiter`�lookahead limiter used in both live and export paths.

### `fx.h`
**Effect rack engine.** Full modular DSP chain with drag-and-drop modules:
- `FxChain`�up to 8 serial effect slots per track.
- **Effects implemented:** Gain, Buffer (glitch/loop), Delay (tape-style with ping-pong, filtering, saturation), Reverb (Schroeder network with predelay and modulation), Lo-Fi (bit-crusher + sample-rate reduction), Phaser (6-stage allpass, quadrature LFO), Chorus (4-voice dual-diffused BBD), Compressor (feed-forward, stereo-linked), Resonator (ZDF SVF note-tuned). The Buffer effect's Rate knob spans **-2.00x to 2.00x** (default 0.00x at the middle); negative rates play the internal loop buffer in **reverse** (bidirectional read wrap + reversed interpolation neighbor).
- **External sidechain.** `FxInstance` carries `scFeedL/scFeedR/scActive`; `fx_compressor_process` substitutes the feed into its peak detector when active. A routed track's slot-0 compressor is pinned for sidechain ducking.
- UI rendering helpers (`fx_draw_aa_knob`, `fx_draw_aa_capsule`).
- Snapshot serialization (`fx_chain_to_snapshot`, `fx_chain_load`).

### `eq.h` / `eq.c`
**Smooth 3-band parametric EQ.** A split-filter design with:
- High-shelf and low-shelf biquads with independent gain controls.
- Dedicated mid-range band with separate parametric control.
- Smooth parameter transitions via internal state�no clicks during UI drags.
- Double-precision internal processing; float and double input/output variants.
- Denormal dither and NaN/Inf guarding.

### `slicing.h`
**Interactive transient slicing detector.** Pure C99 signal processing:
- `detect_clip_transients`�scans mono-mixed PCM, tracks a one-pole low-pass envelope, and emits slice boundaries at rising edges past a sensitivity threshold.
- Zero-crossing snap (`snap_to_zero_crossing`)�aligns slices to silent points to avoid clicks.
- `SlicePreviewState`�UI preview struct for the Slice dialog.

---

## Synth Engines

### `synth_halo.h`
**Halo polyphonic synthesizer.** A complete subtractive/FM/additive synth engine:
- **8 voices** with per-voice state (phase, filters, envelopes, LFO).
- **Oscillator types:** sine, triangle, square (anti-aliased), saw (polyBLEP), with continuous morphing.
- **FM modulation** with feedback, **additive partial engine** (up to 12 partials), **pink noise**.
- Zero-delay feedback (TPT) state-variable filter (LP/HP/BP/SVF).
- Master chorus effect, polyphony-smoothing limiter.
- Preset system (8 factory presets: Obsidian Pad, Solar Lead, Prism Bell, etc.).
- `halo_process_audio`�real-time voice renderer; `halo_render_offline`�offline render for exports.

### `synth_quadrum.h`
**Quadrum drum synthesizer.** Eight fixed drum voices (Kick, Snare, Clap, Closed Hat, Open Hat, Tom, Cowbell, Cymbal):
- Each voice is a whole transient rendered into a cached buffer (`quadrum_render`).
- **Oscillator + FM + noise + click transient + filter + saturation.**
- Factory presets for each voice type.
- SIMD-optimized (SSE) with FTZ/DAZ denormal protection.
- WAV export helper (standalone, though UI uses the async export path).

### `synth.h`
**Integration layer** connecting the synth engines to the timeline and UI:
- `SynthHaloState` & `SynthQuadrumState` per-clip runtime state arrays (`g_ClipHalo`, `g_ClipQuadrum`).
- `synth_state_init_clip` initializes synth state for a clip (allocates Quadrum transient buffers **off-thread** — never call it under `seq_lock`; it renders ~30-90 ms and would starve the audio callback into crackling).
- `synth_state_shift_left` compacts synth state after a clip delete. **Ownership rules:** transient buffers move *with* their clip (free only the removed slot, never the tail); cached `HaloVoice*` pointers are re-anchored via `synth_halo_remap_copied_state` so note-off/stealing never touches another clip's engine. Violating this double-frees transients and crashes playback with a dangling-buffer AV.
- `synth_quadrum_rerender_clip` / `synth_quadrum_rerender_voice` off-thread transient re-render on parameter edits (staging-buffer + brief locked swap).
- `synth_clip_process_frames` dispatches per-frame rendering to Halo or Quadrum engines; Quadrum triggers scale the one-shot transient by the note's velocity (`midi_velocity_gain`) in timeline playback and in the piano-roll audition preview.
- `synth_editor_process_preview` audition rendering for the piano roll.
- `synth_editor_has_ringing` keeps audio callback alive during release tails.
- Snapshot support (`synth_snapshot_take`, `synth_snapshot_free`) for export isolation.

### `synthui.h`
**Synth module UI (knob rack).** The companion panel to the piano roll:
- Full knob-only rack for Quadrum (16 knobs per voice) and Halo (29 knobs).
- **Supersampled knob renderer** (3� DIB + HALFTONE downsample) for smooth anti-aliased dials.
- **Double-buffered cache** with per-knob partial redraw during drags.
- Preset dropdown modal for Halo (shows preset names in a compact list).
- Voice pad selector for Quadrum (8 drum voice buttons).
- Drag-to-adjust knobs with live parameter updates; right-click to reset individual knobs.

---

## UI & Rendering

### `ui.h`
**Timeline rendering and UI dispatch.** The main window's drawing engine:
- **Waveform renderer**�draws smooth, anti-aliased waveforms with multi-resolution LOD peaks. Uses a hash-based cache (`WaveCacheEntry`) so identical views are blitted, not redrawn.
- **Fade curve renderer**�supersampled curve + wedge drawing for fade-in/out handles. The wedge (low-opacity fill) derives its boundary from the same `compute_fade_gain` evaluation and pixel rounding as the envelope line, so non-linear fades hug the curve with no gap (see arch.md §6.4).
- **Supersampled badge glyph**�the animated visualizer button (attractor ring / oscilloscope preview).
- **Double-buffered GDI pipeline**�timeline cache (`g_cacheDC`), main back buffer (`g_mainBackDC`), final `BitBlt`.
- **Hash tree** (`compute_timeline_param_hash`, `compute_timeline_content_hash`)�Merkle-style dirty-detection over the timeline grid.
- **Clip title gutters** (`draw_waveform_clip`)�the sample name is left-pinned inside the track header and the playback-rate badge (`(1.25x)`) is right-pinned inside the visible canvas, so both stay in view when a clip is clipped on either edge. Each gets its own alpha scrim sized to its glyphs (merged only on narrow clips), leaving the waveform middle clean.
- `render_ui`�the main `WM_PAINT` dispatcher. Draws the timeline cache, dynamic overlays (playhead, marquee, hover highlights, slice preview), bottom dock, and synth launcher buttons. In duplicate-select mode it greys the track area out and pops the hovered track back into color.

### `visualizer.h`
**Audio visualizer (non-modal popup).** A real-time oscilloscope and spectrum analyzer:
- Lock-free ring buffer (`g_visRing`) fed by the audio callback.
- **Oscilloscope** mode with zoom and trigger; **spectrum analyzer** with adjustable FFT size (256�8192).
- **Combo mode**�oscilloscope + spectrum side-by-side.
- Stereo/mono/left/right/mid-side channel selection.
- Freeze button (captures the current frame).
- Hue slider, zoom control, and mouse-over frequency HUD (shows frequency, note name, dB).
- VSync-timed repaint thread (`DwmFlush`) for smooth animation.

### `scrollbar.h`
**Custom vertical scrollbar.** A skinned, double-buffered scrollbar:
- `RefractSbState`�handles thumb dragging, hover states, page scrolling.
- `cseq_sb_draw_aa_thumb`�anti-aliased rounded thumb (supersampled DIB).
- Integrates with the timeline cache: `scroll_shift_timeline_cache` reuses the cached bitmap with `ScrollDC`.

### `font.h`
**UI font management & color utilities.**
- Loads Inter font from embedded resource (`IDR_INTER_FONT`).
- `SELECT_UI_FONT`�macro to select the global UI font into an HDC.
- `get_ui_small_font`�lazy-created smaller/bold variant for badges and buttons.
- `hsl_to_rgb`�HSL ? RGB conversion for track theming.
- `cseq_report_error`�central error reporting (MessageBox with guard).

### `dpi.h`
**DPI awareness & scaling.**
- Enables per-monitor DPI awareness via `SetProcessDpiAwarenessContext`.
- `cseq_update_scales_from_dpi`�maintains `g_dpiScaleX`/`g_dpiScaleY`.
- `scale_x`/`scale_y`�DPI-scaling helpers used throughout the UI.
- `cseq_query_window_dpi`�fetches the current window's DPI.

### `utf8.h`
**UTF-8 ? UTF-16 conversion.** Safe helpers for:
- `utf8_to_wide_buf` / `wide_to_utf8_buf`�bounded conversion.
- `utf8_to_wide_heap` / `wide_to_utf8_heap`�heap-allocated conversion.
- `fopen_utf8`�opens a UTF-8 path with a wide-char mode string.

---

## Dialogs & Editors

### `dialogs.h`
**All modeless popup dialogs.** Hosts the window procs and launchers for:
- **BPM dialog**�tempo input with numeric edit.
- **Swing dialog**�swing percentage (0�95%).
- **Bar count dialog**�number of bars (1�1024).
- **Time signature dialog**�beats/bar + note value.
- **Lo-Fi dialog**�bit depth (8�12) + sample rate slider.
- **Pan/Width dialog**�per-track stereo pan and width sliders.
- **Rate dialog**�custom playback rate slider (0.01x�2.00x).
- **EQ dialog**�3-band parametric EQ with draggable curve nodes. Background worker renders the supersampled curve.
- **Filter plotter dialog**�stackable LP/HP/BP/Notch filter with live curve preview.
- **MIDI editor**�piano roll with note editing, audition, and MIDI clip naming. Click-to-add snaps to the clicked grid cell (`quantize_beat_floor`); note hit-testing keeps a comfortable grab area with narrow resize handles. Quadrum/Halo rolls share the same editor. The ADSR knobs take hover-wheel steps and the Octave button takes the scroll wheel (one octave per notch, clamped to -3..3; Quadrum excluded); keyboard audition uses the shared VK-keyed polyphonic held-set.
- **Generative sequencer**�melodic pattern generator (root/scale/chord/arp, octave range, velocity, seed).
- **Humanizer**�timing/duration/velocity jitter.
- **Confirm dialog**�project reset / quit / load confirmation.
- **Keybinds dialog**�keyboard shortcut reference.
- **Transient slicing dialog**�sensitivity slider + preview overlay.
- **Master Volume dialog**�single 0–150% slider (default 100%) that live-applies to `g_Seq.masterVolume` as you drag/scroll/reset, updating the bottom-dock **MASTER** number indicator in real time; closing the popup by any path (ENTER/ESC/click-outside/X) keeps the applied value and marks the project modified.
- **Sidechain routing**�track context menu (`Sidechain Input`) sets `trackSidechainSource`; `set_track_sidechain_source` reserves a compressor at FX slot 0 (pinned, ducking defaults) when linked and removes it on unlink. The FX rack shows the source in the reserved slot's title and blocks removing it while linked.

### `media.h`
**Media Explorer (non-modal file browser + audition preview).**
- Dual-pane directory browser (left: folders, right: audio files).
- Background dir-scan worker (`media_scan_thread_proc`)�enumerates files and reads audio metadata (duration, sample rate, channels) with miniaudio.
- Background preview decode worker (`media_preview_thread_proc`)�decodes selected file into the audition ping-pong buffer and builds waveform peaks.
- **Audition voice integration** (`headers/audition.h`)�real-time preview playback mixed into the master bus.
- Drag-and-drop from explorer to timeline (`media_import_at_point`).
- **Import to canvas** (`media_import_to_canvas`)�adds selected file at the edit cursor with current preview speed.
- Speed/volume knobs, Play/Pause, Repeat toggle, waveform strip with scrub head.

### `audition.h`
**Audition voice (Media Explorer preview).** A lightweight real-time preview voice that mixes into the master bus:
- **Ping-pong buffers** (`g_audBuf[2][AUDITION_MAX_FRAMES * 2]`)�pre-allocated, never freed during browsing.
- All control state is `volatile LONG` + `Interlocked*` for cross-thread safety.
- Linear resampling at configured speed (0.5ז2.0�).
- ~1.5 ms de-click fade (`AUDITION_FADE_FRAMES`) on start, stop, and buffer switch.
- Loop (Repeat) and play-once modes with smooth end fade.
- Scrub-seeking (`audition_seek`)�the UI waveform click jumps to that position.

### `granular.h`
**Granular synthesis engine UI and processing.** Full granular editor:
- `GranularEngine`�state per track or per clip (grains, notes, parameters).
- Grain spawning with position/pitch/pan jitter, ADSR envelope, freeze mode, drone mode.
- `granular_process_engine`�audio-thread renderer; spawns grains from MIDI-like note grid.
- **Granular UI window**�note grid (piano roll), parameter sliders (size, density, position, spray, pan, pitch, detune, attack, release, volume), note copy/paste. Octave button (left/right click or scroll wheel, clamped to -3..3) and QWERTY keyboard audition join the same polyphonic held-set, VK-keyed for octave-safe key-up.
- Sample loading (own sample or referencing `g_Seq.samples`).

### `events.h`
**Main window event handling.** The `cseq_main_wndproc` implementation:
- All `WM_*` handlers: `WM_PAINT`?`render_ui`, `WM_MOUSEWHEEL`, `WM_DROPFILES`, `WM_LBUTTONDOWN`, `WM_KEYDOWN`, etc.
- **Topbar badge clicks**�PLAY, BPM, Bar Count, Swing, Snap, Lo-Fi, Import, Export, Save, Load, Keybinds, Visualizer.
- **Clip drag/resize/fade/alt-slip/volume/rate**�interactive clip editing with undo.
- **Marquee selection**�rectangle selection of clips.
- **Track header drag**�Shift+drag to reorder tracks.
- **Duplicate-select mode**�the bottom-dock DUP button toggles a mode that greys out the timeline and color-highlights the hovered track; clicking a track duplicates it directly below and exits the mode.
- **Synth launcher buttons**�spawn Quadrum/Halo clips at the playhead.
- **Keyboard shortcuts**�Space (play), S (split), M (mute), N (MIDI editor), G (granular toggle), E (export), etc.
- Rate-change undo debounce (Shift+Wheel ? 300 ms delay ? single undo step).

### `project.h`
**Project save/load (`.csq` files).** Serialization and deserialization:
- `SaveProjectThreadProc`�runs on background worker: snapshots `g_Seq`, writes header, tracks, clips, samples (compressed LZ), granular engines, MIDI notes, fade types, ADSR, FX rack, SoundFont reference, track filters, synth clip kinds, trigger probability, master params, track mix.
- `LoadProjectThreadProc`�stops audio, rebuilds `g_Seq` from disk, restarts audio.
- **SoundFont path resolution**�resolves relative/absolute paths and fallback directories.
- **Sample dedup**�uses the disk-backed PCM cache.
- Trailing section support for backward/forward compatibility (`CSQY`, `CSQV`, `CSQP`, `CSQM`, `CSQT`).
- **Track mix** (`CSQT`, v2) persists pan/width/solo plus the per-track sidechain source (`sidechainSource`, `-1` = none).

### `codec.h`
**Project serialization format definitions.** All on-disk structs and compression:
- `CSQHeader`, `CSQExtHeader`, `CSQTrack`, `CSQ3ClipEntry`, `CSQSampleHeader`.
- Trailing section structs: `CSQSynthSection`, `CSQFilterSection`, `CSQProbSection`, `CSQMasterSection`, `CSQTrackMixSection` (v2 adds `sidechainSource`).
- **LZ compression** (`csq_compress_lz`/`csq_decompress_lz`)�a simple LZ77-style compressor for PCM samples.
- In-memory deserialization helpers for granular engines and FX chains.

### `samplecache.h`
**Disk-backed PCM cache.** Content-addressed, memory-mapped sample storage:
- **64-bit FNV-1a** hash of raw PCM bytes�cache filenames are `<hash>.pcm`.
- `sample_install_cached`�stores a heap PCM buffer to the cache and memory-maps it back, freeing the heap copy.
- `sample_unmap`�releases either a mapped or heap PCM buffer.
- `sample_cache_evict_if_large`�maintains a 4 GB cache cap; deletes oldest files on startup.
- `sample_hash_pcm`�FNV-1a over raw PCM bytes (used for dedup).

### `soundfont.h`
**SoundFont (`.sf2`) loader and pre-rendered note cache.**
- TinySoundFont (`tsf.h`) integration�loads `.sf2` on a background worker (`sfont_load_async`).
- **Pre-rendered note bank.** The active preset's 128 notes are rendered once into static PCM buffers (`g_SFontCache`). The audio thread reads these exactly like sample files.
- Preset selector UI (instrument picker) opens as a modeless popup.
- **Lifetime management.** tsf synth instances are pinned during background builds (`sfont_synth_pin`/`sfont_synth_unpin`); deferred close prevents use-after-free.
- `sfont_get_sample_nearest`�falls back to the nearest loaded pitch when the exact key has no region.

### `actions.h`
**Edit operations on clips and tracks.** High-level actions used by the UI:
- `load_audio_file`�decodes audio (via miniaudio, Media Foundation, OGG, or Opus) into the sample cache.
- `add_clip`�creates a new sample clip at the given position.
- `split_clips_at_playhead` / `split_single_clip_internal`�clip splitting with zero-crossing snap.
- `copy_selected_clips` / `paste_clipboard_clips`�clipboard with custom `CSQ3_CLIPBOARD` format.
- `delete_selected_clips`�removes selected clips.
- `reorder_track`�moves a track and renumbers all per-track arrays and clip track indices.
- `duplicate_track_action`�clones a track (per-track arrays, deep-copied FX/granular engines, and all its clips) and inserts the copy **directly below the source** via `reorder_track`; synth clips are re-initialized off the lock.
- `insert_midi_clip`�creates a new MIDI clip.
- `spawn_synth_clip`�creates a Quadrum or Halo synth clip with factory patch defaults.
- **Reverse clips** (`reverse_clips_action`, `get_or_create_reversed_sample`)�content-addressed sample reversal with zero-copy dedup.
- **Multi-resolution peak cache**�builds LOD peak pyramids for waveform rendering (inline for short files, background thread for huge files).

---

## Utilities & Vendored Libraries

### `miniaudio.h` / `miniaudio.c`
**Vendored audio I/O library.** Provides the device context (`ma_device`) and decoder/encoder APIs. Build configured for **WASAPI only** (`MA_ENABLE_WASAPI`), no engine/graph/resource manager.

### `tsf.h`
**Vendored TinySoundFont.** `.sf2` parser and synthesizer. Used only on background workers for pre-rendering note banks�**never** on the real-time audio thread.

### `vorbis.c`
**Vendored stb_vorbis.** Ogg/Vorbis decoder. Used by `ogg.h` for `.ogg` file support.

### `ogg.h`
**Ogg/Vorbis decoder wrapper.** Provides a unified decoder interface:
- `ogg_open`/`ogg_open_memory`�open from file or memory.
- `ogg_decode_all`�decodes entire file, converts to stereo, resamples to `SAMPLE_RATE`.
- Uses `vorbis.c` (stb_vorbis) for actual decoding.

### `opus/` (opus_wrap.c/h, ogg/, opus/, opusfile/)
**Vendored Opus decoder stack.** libogg + libopus + libopusfile compiled as source under `opus/` (self-contained, no external dependency), with generated configs in `opus/config/`. `opus_wrap.c`/`opus_wrap.h` expose a unified decoder interface (`opus_open`/`opus_open_memory`, `opus_decode_all`, `opus_close`); `opus_decode_all` decodes the whole file to interleaved stereo float PCM and resamples 48 kHz -> `SAMPLE_RATE`. The wrapper struct is `OpusWrapDecoder` to avoid colliding with libopus's opaque `OpusDecoder`. Wired into `load_audio_file()` after the OGG fallback.

---

## Configuration & Architecture

### `config.h`
**Global constants and compile-time settings.**
- `SAMPLE_RATE`, `NUM_CHANNELS`, `MAX_TRACKS`, `MAX_SAMPLES`, `MAX_CLIPS`, `MAX_UNDO_STATES`.
- DPI scaling base values (`HEADER_HEIGHT_BASE`, `TRACK_HEIGHT_BASE`, etc.).
- Grid division enum, fade curve types, menu command IDs (incl. sidechain: `ID_TRACK_SC_NONE`, `ID_TRACK_SC_UNLINK`, `ID_TRACK_SC_BASE`).
- `scale_x`/`scale_y`�DPI-scaling helpers.

### `arch.md`
**Architecture & design guide.** The authoritative reference covering:
- Threading model (UI, audio, workers) and lock hierarchy.
- Memory ownership (zero-copy slicing, PCM immutability, disk cache).
- Hard Rules (no malloc on audio thread, no volatile for sync, etc.).
- SOPs for adding features, dialogs, DSP, preview voices.
- Feature references: trigger probability, transient slicing, reversing, rate-change undo.
- FNV hash tree & double-buffered GDI pipeline.

---

## File Dependency Flow

```
main.c
+-- eq.h/eq.c (SmoothEQ3 implementation)
+-- font.h (UI font)
+-- dpi.h (DPI awareness)
+-- config.h (constants)
+-- types.h (core structs)
+-- soundfont.h (SF2 loader)
+-- globals.h (global declarations)
+-- dsp.h (DSP primitives)
+-- fx.h (effect rack)
+-- visualizer.h (spectrum/oscilloscope)
+-- codec.h (serialization)
+-- audition.h (Media Explorer preview voice)
+-- slicing.h (transient detection)
+-- ui.h (timeline rendering)
+-- audio.h (audio callback + export)
+-- synth.h (synth engine integration)
+-- synthui.h (synth UI)
+-- project.h (save/load)
+-- state.h (undo/redo)
+-- actions.h (clip operations)
+-- samplecache.h (disk PCM cache)
+-- dialogs.h (modeless popups)
+-- media.h (Media Explorer)
+-- events.h (main window events)
+-- miniaudio.h (vendored audio I/O)
+-- opus/ (vendored Opus decoder stack: opus_wrap.c/h + libogg/libopus/libopusfile)
```

---

## Project File Format (`.csq`) Overview

The `.csq` file is a self-contained **module project** with a fixed-magic header (`CSQ4`) followed by versioned trailing sections. Each section has a 4-byte magic + version; loaders rewind on any mismatch, ensuring backward/forward compatibility.

**Sections (in save order):**

| Magic | Section | Description |
|-------|---------|-------------|
| `CSQ4` | Header + `CSQX` | BPM, swing, bar count, track/sample/clip counts, lofi, quantize, grid division, master mode, time signature |
| � | Tracks | `CSQTrack`: mute, volume, EQ low/mid/high, EQ freq/Q |
| � | Clips | `CSQ3ClipEntry`: sample index, track, beats, offset, volume, playback rate, fades, flags, MIDI note count |
| � | Samples | name, frame count, compressed PCM (LZ) |
| � | Granular | engine header + notes + optional own-sample PCM |
| `MIDI` | MIDI | per-clip note arrays |
| `CSQF` | Fades | per-clip fade-in/out curve types |
| `CSQA` | ADSR | per-clip attack/decay/sustain/release |
| `CSQE` | FX rack | per-track slot count, type IDs, params |
| `CSQS` | SoundFont | relative/absolute path + preset slot (`.sf2` bytes **never** embedded) |
| `CSQV` | Filter | per-track filter type mask, frequency, Q, enabled |
| `CSQY` | Synth clip | per-clip kind + synth ADSR + `QuadrumParams[8]` + `HaloPatch` |
| `CSQP` | Trigger prob | per-track trigger probability + RNG seed |
| `CSQM` | Master params | master volume, lofi bit depth/sample rate, export bit depth |
| `CSQT` | Track mix | per-track pan, stereo width, solo |

All serialization is pointer-free: only scalars, strings, sample indices, frame offsets, and normalized values are persisted.