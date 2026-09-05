# cseq — Architecture & Design Guide (1.3)

This is the authoritative reference for how the engine is built, how it threads, how memory is owned, and how to extend it without breaking real-time safety.

> **Layout note:** this project is a **Unity build**. Files under `headers/`
> contain *full static implementations* (not just prototypes) and are included
> by `main.c` / `eq.c`; `main.c`'s include order *is* the dependency graph.
> So a `file:line` reference is a definition, not a declaration. Vendored code
> (`miniaudio.h`, `tsf.h`, `ogg.h`, `codec.h`, `vorbis.c`) is excluded from the
> rules that apply to project code. The Media Explorer lives in two headers:
> `headers/media.h` (the non-modal panel + background workers) and
> `headers/audition.h` (the real-time preview voice), included by `main.c`
> before `audio.h` so the audio callback can call the voice.

---

## 1. Architectural Philosophy & Design Rationale

### Why C99 and Win32

The engine is deliberately built on **C99 + the Win32 API** with **zero
third-party runtime dependencies** (only vendored audio libraries: miniaudio,
TinySoundFont, OGG/Vorbis). This buys four properties:

- **Zero-dependency footprint.** No CRT-managed plugin host, no dynamic
  loader, no framework to keep patched. The binary is self-contained.
- **Instantaneous boot.** There is no interpreter, JIT, or managed startup to
  warm up; `WinMain` runs directly and the audio device starts within
  milliseconds.
- **Cache locality.** Fixed-size flat structs and contiguous buffers let the
  CPU prefetch and the L1/L2 caches work for us on the hot audio path.
- **Protection against dependency rot.** A vendored, version-pinned codebase
  cannot break because an upstream package changed its ABI. When a vendored
  library is updated it is a deliberate, reviewed act.

### Data-Oriented & Deterministic

Audio generation and sequencing are built around **contiguous memory and
fixed-size flat structs**, and the render loop is **offline bit-perfect**:

- **Fixed-size flat structs.** `SequencerState`, `Clip`, `GranularEngine`, the
  FX rack, and the synth engines are all plain structs with fixed-size arrays,
  laid out for predictable sizeof and no hidden indirection. This makes whole
  struct copies (undo/redo snapshots, project save, export snapshots) a single
  `memcpy` — cheap and deterministic.
- **Contiguous PCM.** Decoded audio is one interleaved-stereo `float*` buffer
  per sample. Renderers read it with linear interpolation at an advancing
  position — a streaming sequential access pattern that the memory system
  handles efficiently.
- **Deterministic render.** The same inputs always produce the same output
  samples. There is no recording-into-a-driver abstraction, no variable-size
  callback accumulation, and no dependence on wall-clock timing inside the
  mix. Export reuses the *exact same* `render_frames` as live playback,
  rendering into a snapshot, so a WAV export is bit-identical to what you
  hear (given the same state).

### Audio Thread Isolation

The three concerns — **UI, disk I/O, and DSP** — are strictly decoupled:

- The **UI thread** mutates editable state under `seq_lock()` and never does
  audio processing or blocking disk I/O on the realtime path.
- The **real-time audio callback** renders and mixes only; it never touches
  the filesystem, never allocates, and never calls Win32 GUI functions. The
  Media Explorer's audition voice renders inside this callback
  (`audition_process_voice`, `audio.h:1354`) so previews mix through the same
  master limiter/lofi as everything else.
- **Background workers** do the slow work (decode, project save/load, export,
  peak extraction, SF2 rendering, Media Explorer folder scan + preview
  decode) on their own threads, snapshotting shared state under a lock
  *before* doing I/O so the audio thread never blocks on disk.

The payoff: the audio thread has a hard, provable upper bound on latency, and
UI stalls can never glitch the audio.

### Allocations
- Everything is statically allocated - including maximum clip and note pools (2048);
  which are the main contributor to the cold boot of ~100MB RAM usage.

---

## 2. Threading Model, Concurrency & Synchronization

### 2.1 The three execution domains

**1. UI Thread (Win32 message loop).** `WinMain` (`main.c:97`) runs the
`GetMessageA` loop. All window procedures, painting, and input handling run
here. Painting is driven by a **120 fps pacer thread**
(`main_ui_pacer_thread_proc`, `headers/events.h:52`) that calls
`InvalidateRect(g_hWnd)` only when something is actually dirty
(`TARGET_MAIN_FPS = 120`, `headers/config.h:10`); a 16 ms `SetTimer`
(`events.h:433`) handles deferred `.csq` load dispatch. The UI thread owns all
GDI surfaces and the timeline cache. It also owns the modeless popups
(FX rack, EQ, BPM, the MIDI/synth editors, and the Media Explorer) — these
return to the main loop and are dispatched by `DispatchMessageA`; the Media
Explorer additionally drives its waveform scrub head with a 33 ms `SetTimer`
(`media.h`) while a preview is playing.

**2. Real-Time Audio Callback Thread.** `audio_callback`
(`headers/audio.h:1168`) runs on the miniaudio device thread
(`main.c:179-181`). It is a **non-blocking rendering pass**: it snapshots
state per ≤1024-frame chunk under `seq_lock()`, renders the timeline, granular
engines, MIDI/SF2, per-track FX, the master limiter, and lofi, and pushes
audio to the visualizer through a lock-free ring. It never allocates, never
does I/O, and never calls GUI functions.

**3. Background Worker Threads.** Created with `CreateThread`, they handle all
slow work and post results back to the UI:

| Worker | Proc | Role |
|---|---|---|
| Project save | `SaveProjectThreadProc` (`project.h:376`) | Snapshots `g_Seq` under `seq_lock`, writes `.csq` |
| Project load | `LoadProjectThreadProc` (`project.h:733`) | Stops device, rebuilds `g_Seq`, restarts device || WAV export | `ExportTimelineThreadProc` (`audio.h:1258`) | Renders `render_frames` from a snapshot to `.wav` |
| SF2 load | `SFontLoadThreadProc` (`soundfont.h:418`) | Parses `.sf2`, pre-renders 128-note PCM bank |
| SF2 preset rebuild | `SFontCacheBuildThreadProc` (`soundfont.h:290`) | Re-renders active preset, atomic bank swap |
| EQ curve | `EqCurveWorkerThreadProc` (`dialogs.h:1774`) | Supersampled EQ curve bitmap render |
| Visualizer vsync | `vis_vsync_thread_proc` (`visualizer.h:825`) | `DwmFlush` + invalidate at vsync |
| Media dir scan | `media_scan_thread_proc` (`media.h:144`) | `FindFirstFileW`/`FindNextFileW` folder enumeration + audio metadata |
| Media preview decode | `media_preview_thread_proc` (`media.h:252`) | Decodes selected file into audition ping-pong buffer, builds waveform peaks |

All workers use the shared **job system** (`job_begin`/`job_end`/
`job_set_progress`, `headers/globals.h:82-117`, gated by `g_Seq.isBusy`) to
serialize long operations and report progress/status back to the UI via
`PostMessageA(g_hWnd, WM_APP_FULL_REDRAW, 0, 0)`.

> **Project load lock scope.** `LoadProjectThreadProc` stops the audio device
> *before* taking `seq_lock()`, and the job gate blocks other workers, so a
> load holds `seq_lock` across its parse/rebuild without an audio underrun.
> The slow, independent SF2 teardown (`sfont_clear()`) runs *before* the lock
> (it only touches `g_SFont`, never `g_Seq`). If the loader is ever extended to
> do more heavy work inside the lock, prefer parsing into a temporary snapshot
> and committing under `seq_lock` (the §4.3 pattern) over holding the lock
> across I/O.

### 2.2 Lock hierarchy & ordering

All locks are Win32 `CRITICAL_SECTION`s. There are six:

| Lock | Declared | Guarded state |
|---|---|---|
| `g_Seq.lock` (`seq_lock`/`seq_unlock`) | `types.h` (`SequencerState.lock`) | clips, samples, tracks, masks, gran engines |
| `g_midiLock` (`midi_lock`/`midi_unlock`) | `main.c` | MIDI editor audition state |
| `g_SFont.lock` | `soundfont.h` | SF2 preset tables + PCM bank |
| `g_SFontBuildLock` | `soundfont.h` | the offline SF2 render bank `g_SFontCacheBuild` (build worker ↔ `sfont_cache_clear` only — never on the audio path) |
| `g_eqCurveLock` | `dialogs.h` | EQ curve render job + front bitmap |
| `g_mediaListLock` | `media.h` | Media Explorer entry list + current directory (UI ↔ dir-scan worker only, never realtime) |

The `g_SFontBuildLock` exists because the preset-build worker writes into
`g_SFontCacheBuild` while `sfont_cache_clear()` (UI/load thread) frees it. The
audio thread never reads `g_SFontCacheBuild`, so this lock is never on the
realtime path; it only serializes the two non-audio writers.

**Strict ordering rule (never violated):**

```
g_Seq.lock  →  g_SFont.lock      (audio thread: seq_lock chunk, then sfont_get_sample)
g_Seq.lock  →  g_midiLock        (audio callback chunk -> MIDI preview; MIDI paste)
```

- The audio thread acquires `g_Seq.lock` per chunk, and *within* that scope may
  take `g_SFont.lock` via `sfont_get_sample`. Therefore `g_Seq.lock` must
  **always** be acquired before `g_SFont.lock`, never the reverse.
- The MIDI preview path (`midi_editor_process_preview`) and the MIDI paste path
  (`midi_edit_paste_clipboard`) acquire `g_midiLock` only while already holding
  `g_Seq.lock`, so the order is `g_Seq.lock → g_midiLock`. Nothing acquires
  `g_midiLock` and then `g_Seq.lock` while holding it (the callback's "ringing
  check" releases `midi_lock` before taking `seq_lock`), so the two never
  deadlock. Do not add a `g_midiLock → g_Seq.lock` nesting anywhere. One level
  deeper: the preview's standard-roll audition slots resolve SoundFont notes
  (`sfont_get_sample`) *while holding* `g_midiLock`, so the full chain there is
  `g_Seq.lock → g_midiLock → g_SFont.lock`. Nothing acquires those locks in
  reverse (the UI thread takes `g_midiLock` alone for held-set updates), so the
  hierarchy stays acyclic.
- **Rule:** never hold a lock across a blocking call (file I/O, `malloc`,
  `MessageBox`, `WaitForSingleObject`). Workers snapshot under `seq_lock()`
  and do I/O *after* releasing it.
- `g_Seq.lock` is held only for one ≤1024-frame chunk at a time
  (`audio.h:1278/1363`) — it is a hot, short critical section, never a
  long-lived one.

### 2.3 Lock-free communication

Cross-thread signals avoid locks where possible, using **Interlocked
primitives** and **single-producer/single-consumer rings**:

- **Atomic flags.** Play/pause, playback frame, and job state use
  `InterlockedExchange`/`InterlockedCompareExchange` on `volatile LONG`s:
  `seq_is_playing`/`seq_set_playing` (`globals.h:64-70`), `set_playback_frame`
  (`dsp.h:264`), and the job system `job_begin`/`job_end`/`job_set_progress`
  (`globals.h`). `job_begin` uses `InterlockedCompareExchange` as an atomic
  test-and-set so two callers can never both observe "not busy" and start two
  jobs concurrently; `job_end`/`job_set_progress` publish with
  `InterlockedExchange`. These are the correct replacement for
  bare `volatile` — see Hard Rule 5.
- **SPSC ring buffer (audio → visualizer).** `vis_push_audio_samples`
  (`visualizer.h:46`) writes into `g_visRing.bufferL/R` and publishes the
  write position with `InterlockedExchange`; the UI thread reads it
  lock-free. Buffer contents are unsynchronized by design — a transient
  partial read only produces a single-frame visual artifact, never a crash.
- **Ping-pong / atomic publish (soundfont).** The SF2 preset worker renders
  the full bank into `g_SFontCacheBuild`, then atomically publishes it into
  `g_SFontCache` under `g_SFont.lock` (`soundfont.h:267-271`). The audio
  thread keeps reading the previous bank until the swap — it never sees a
  half-built bank. This is the pattern to copy for any "build off-thread,
  swap under lock" handoff. Two audit additions protect the build worker:
  - **tsf lifetime pinning.** `sfont_cache_build_preset` drives a `tsf*` on a
    worker/loader thread; a concurrent `sfont_unload()`/`sfont_load_file_internal()`
    would otherwise `tsf_close()` the instance being rendered. The build pins
    the synth it holds (`sfont_synth_pin`/`sfont_synth_unpin`); a close
    encountered while a build holds a pin is deferred (`sfont_close_or_defer`)
    and flushed when the last build releases (`soundfont.h`). This prevents a
    use-after-free of `g_SFont.synth` on a preset switch racing an unload/load.
  - **Offline bank serialization.** `g_SFontCacheBuild` is written by the build
    worker and freed by `sfont_cache_clear()`; both take `g_SFontBuildLock`
    (§2.2). The audio thread never reads it, so the lock stays off the
    realtime path.
- **Job-system shutdown drain.** All job workers are created detached
  (`CreateThread` + immediate `CloseHandle`). On window close, `WinMain` sets
  `g_shuttingDown`, drains the job system (`job_is_busy()` loop) and joins the
  SF2 build worker (`sfont_build_worker_wait()`) *before* `DeleteCriticalSection`,
  so a detached worker can never enter a freed lock.
- **Audition voice (Media Explorer preview).** `headers/audition.h` implements
  a lightweight preview voice that mixes into the master bus inside the audio
  callback (via `audition_process_voice`, `audio.h:1354`) — it never opens a
  second output device, so it cannot conflict with WASAPI Exclusive / ASIO.
  All control state (`playing`, `trigger_stop`, `activeIdx`, `totalFrames`,
  `speedBits`, `volBits`, `repeat`, `seek_frame`, `read_frame_idx`) is
  `volatile LONG` + `Interlocked*` (Hard Rule 5). PCM lives in two
  pre-allocated ping-pong buffers (`g_audBuf[2][...]`, `audition.h:34`) that
  are never freed during browsing; the worker decodes into the *inactive*
  slot and publishes via `audition_play()`, so the audio thread only ever
  reads the published buffer — no use-after-free, no realtime allocation.
  The audio callback keeps rendering while the voice is playing or fading out
  (`audition_is_playing()` / `audition_voice_rendering()` are part of
  `shouldPlay`, `audio.h:1224-1226`).
- **Pacer redraw hints.** The pacer and workers write `g_timelineDynamicDirty`
  and `g_timelineDirty` as advisory redraw hints read by the UI thread; these
  are hints, not synchronization, and must not be used to gate data access.

---

## 3. Pointer Lifetime & Memory Ownership Model

### 3.1 Audio buffers vs. clips — zero-copy slicing

- **One decoded PCM buffer per sample.** `AudioSample.pFrames` is the single
  interleaved-stereo `float*` for a sample, owned by the `g_Seq.samples[]`
  pool (indexed by `Clip.sampleIndex`).
- **Clips are read-only views.** A `Clip` never copies PCM. It references a
  sample *by index* and describes a region with `sampleOffsetFrames`,
  `lengthBeats`, and `playbackRate`. **Multiple clips share one buffer** —
  splitting, duplicating, and pasting clips all point at the same
  `AudioSample` with different offsets. This is zero-copy slicing: no buffer
  is ever duplicated when slicing audio.
- **PCM is immutable after load.** All readers use `const` access; editing
  (split, trim, pitch, zero-crossing) mutates only `Clip` metadata. This
  invariant is what lets the peak-build thread and the disk-backed PCM cache
  (see below) run safely against a shared buffer.
- **Reversing preserves immutability & zero-copy.** Reversing a clip never
  mutates the underlying `AudioSample` in place (which would silently corrupt
  other clips or slices sharing that sample). Instead, it performs a
  content-addressed transform (§5.5) creating a reversed `AudioSample` in the
  cache, and remaps the clip's `sampleOffsetFrames`. Multiple clips/slices
  referencing the reversed audio share the same reversed buffer zero-copy.
- **Disk-backed PCM cache.** Since PCM is immutable, decoded audio may live in
  a content-addressed, memory-mapped cache file (`headers/samplecache.h`)
  instead of heap. `pFrames` points into the mapping (`hCacheMap` set); the OS
  pages it in/out under memory pressure. `sample_unmap` releases either a
  mapping or a heap buffer, so every free site is correct regardless of which
  backing the sample uses.
- **New-clip placement.** Spawned synth clips (Quadrum/Halo dock buttons,
  `spawn_synth_clip` in `actions.h`) and Media Explorer "Import" both place
  their clip on `g_Seq.lastClickedTrack` (`types.h`) — the most recently
  clicked timeline track, recorded in `events.h` on clip/empty-space clicks —
  falling back to the first selected clip's track, then track 0.

- **Sample-clip playback loop policy** (`render_frames`, `audio.h`; mirrored
  by the waveform view in `wave_compute_spans`, `ui.h`). A sample clip reads
  its sample linearly and decides whether to loop from the **full** sample
  length (`s->frameCount`), never from the alt-slip offset:
  - clip length **< 1×** sample → play once and stop;
  - clip length **≥ 1× and < 2×** sample → play once, then silence;
  - clip length **≥ 2×** sample → loop the sample to fill the clip.
  When looping, the read position wraps back to the sample's **actual start**
  (frame 0) — the alt-slip `sampleOffsetFrames` only sets the entry point, so
  every repeat restarts at the beginning of the sample. Because the loop
  decision ignores `sampleOffsetFrames`, alt-slipping never flips a clip
  between looping and not, and the per-pass micro-fade is derived from the
  monotonic clip-elapsed frames (not the wrapped read position) so repeated
  passes of a looping, alt-slipped clip are never silenced.

  The waveform view (`draw_waveform_clip`, `ui.h`) mirrors this policy: it
  draws the sample once then silence for <2×, repeats it for ≥2×, and renders
  a white **loop marker** at the sample-end boundary whenever the clip is
  longer than the playable sample — regardless of whether the clip was
  alt-slipped. The marker and the loop flag are part of the wave cache key, so
  a looping vs. non-looping render never shares a stale cached bitmap.

- **Fade curve & alt-slip apply to the whole selection** (`events.h`,
  `dialogs.h`). Two right-drag/edit interactions operate on all selected clips
  and must never silently collapse the selection:
  - **Fade context menu.** In `WM_RBUTTONDOWN` the fade-handle hit test runs
    *before* any selection change, so right-clicking a fade handle preserves
    the current multi-selection; `show_fade_context_menu` then applies the new
    curve type to the clicked clip and every selected clip.
  - **Alt-slip.** Starting a slip drag does not deselect; the selection logic
    collapses to a single clip only when the clicked clip was unselected, and
    the slip update loop (`WM_MOUSEMOVE`) moves every selected clip's
    `sampleOffsetFrames` together.

### 3.2 Voice allocation

- **Halo (poly synth):** a fixed pool of `HALO_MAX_VOICES` voices. A note-on
  claims a voice via `voice_manager_alloc`; if none is free, an existing voice
  is **stolen** (usually the quietest/oldest). Note-off releases the exact
  voice cached for that note (`noteVoice[i]`) without re-entering the
  allocator, which would reset phase/filter state and cause a pop.
- **Quadrum (drum synth):** 8 fixed voices, each with a pre-rendered transient
  buffer. Voices are triggered by index; the audio thread only *plays back*
  the cached transient (`mix_quadrum_active`), never re-renders it live. The
  triggering note's velocity is honored exactly like the sample/SF2 and Halo
  paths: the trigger normalizes the note velocity and caches
  `midi_velocity_gain(vel)` into the per-voice `vel[8]` gain, and the mixer
  scales the one-shot transient sample-wise with it — so velocity edits made
  on the piano roll are audible in both timeline playback and the in-window
  audition preview.
- **Granular:** a pool of `GRAN_MAX_GRAINS` grains; a spawn claims an inactive
  grain, or **chokes** the oldest grain (highest `phase`) when full.

### 3.3 Lifetime rules — preventing use-after-free

- **Samples are freed only when the device is stopped or on shutdown.**
  Project load stops the audio device (`project.h:815`) before rebuilding and
  freeing `g_Seq.samples`; the callback is quiescent during that window. Live
  removal is not done mid-playback.
- **Export and save snapshot first.** Both copy the state they need under
  `seq_lock()` (deep PCM copies for export) and then operate on the snapshot,
  so the live buffers can be mutated/freed without affecting the worker.
- **Swap preview buffers under a lock.** When a knob edit triggers a re-render
  (e.g. Quadrum transient, SF2 bank), the worker renders into a *staging*
  buffer and publishes it atomically under the relevant lock — the audio
  thread never sees a half-written or freed buffer.
- **Granular `ownFrames`.** Private per-engine PCM is freed under `seq_lock`
  (e.g. `project.h:836-846`) and only while the device is stopped or on
  explicit engine teardown.
- **Detached workers must not outlive the locks they take.** Job workers
  (`SaveProject`, `LoadProject`, `Export`, `SFontLoad`) are created detached.
  On window close, `WinMain` sets `g_shuttingDown` and drains the job system
  (bounded `job_is_busy()` wait) plus joins the SF2 build worker
  (`sfont_build_worker_wait()`) *before* `DeleteCriticalSection(&g_Seq.lock)` /
  `&g_midiLock`. Never delete a critical section while a worker may still
  enter it.
- **Rule of thumb:** if the audio thread reads a pointer, that pointer must be
  either (a) immutable after publish, (b) swapped atomically under a lock the
  audio thread takes, or (c) valid for the whole time the device is running.
  Anything else is a use-after-free bug waiting to happen.

---

## 4. Step-by-Step SOP for Adding New Features

### 4.1 Adding a UI element / dialog / slider

1. **Register the window.** Add a `WndProc` (e.g. `MyWndProc`) and register it
   with `RegisterClassA` (see the existing dialogs in `headers/dialogs.h`,
   e.g. `LofiWndProc` at `dialogs.h:996`). Create the window from the existing
   dialog-launcher path in `dialogs.h` — **do not pump your own modal loop**;
   return to the main message loop.
2. **Own your globals.** Declare any globals in your module header, define them
   in `main.c`, and initialize them in `WinMain`'s init block.
3. **Capture input without holding engine locks.** Read mouse/keyboard state in
   your `WndProc`'s message handlers on the UI thread. Do **not** take
   `seq_lock()` inside `WM_PAINT`; snapshot what you need into locals first.
   Repaint via `InvalidateRect` on your window only.
4. **Synchronize state changes to the audio engine.** Any value the audio
   thread reads must be published safely:
   - A single scalar → `volatile LONG` + `InterlockedExchange` (e.g. how
     `seq_set_playing` works).
   - A struct/array → take `seq_lock()`, mutate, release. The audio thread
     re-reads under `seq_lock()` per chunk, so it sees a consistent snapshot.
   - Follow the granular-engine pattern (UI mutates under `seq_lock`, audio
     reads under `seq_lock`).
5. **Request a repaint** of the main window via
   `PostMessageA(g_hWnd, WM_APP_FULL_REDRAW, 0, 0)` (or set a pacer dirty flag)
   — never call `InvalidateRect(g_hWnd)` directly from a worker thread.

### 4.2 Adding a new DSP / FX / synth voice

1. **Define parameters in a struct.** Add your parameters to the relevant
   patch/params struct (e.g. `HaloPatch`, `QuadrumParams`, or a new
   `FxParamDef` table in `fx.h`). Keep it a flat struct so it rides the
   whole-struct undo memcpy and project serialization automatically.
2. **Implement the exact callback signature.** For FX, implement a
   `void myfx_process(MyFxState* s, float* inL, float* inR)` (per-sample, see
   `fx_delay_process` in `fx.h:205`). For a synth voice, implement a
   `voice_render_block`-style per-sample renderer (see `synth_halo.h`).
   Match the existing convention exactly so it plugs into the loop.
3. **Integrate into the master mixing loop.** In `render_frames`
   (`audio.h:736`), call your process function where the per-track DSP runs
   (`fx_chain_process` at `audio.h:898`), or add your voice to the
   `voice_render_block`/`halo_process_audio` path. Feed it through the
   existing per-track FX, EQ (`smooth_eq3_process_float`), and peak-biquad
   chain so your output gets the same master treatment.
4. **Pre-allocate all state off the realtime path.** Allocate your
   `MyFxState`/voice buffers in the init path (before the device starts), not
   inside the process function. The realtime path must never call `malloc`.
5. **Guard the denormals.** Route IIR state through `denormal_flush_f` and
   gate inputs with the `_finite` check, matching `peak_biquad_process`. The
   EQ path (`smooth_eq3_process_float`/`_double`, `eq.c`) gates NaN/Inf inputs
   to silence *before* they enter the persistent IIR state and only dithers
   genuine denormals (never turning a true `0`/silence into signal) — apply
   the same two guards to any new filter so NaN/Inf can't be latched into
   state that persists across callbacks.
6. **Use SIMD load/store helpers.** Prefer `_mm_loadu_ps`/`_mm256_loadu_ps`
   unless alignment is statically guaranteed.

### 4.3 Adding a file / analysis task (e.g. chopper, importer)

1. **Offload to a background worker.** Spawn a `CreateThread` worker (see
   `SFontLoadThreadProc` as the model). Wrap it in the job system:
   `job_begin(kind, path)` at launch, `job_set_progress(...)` as it runs,
   `job_end("done")` on completion — this keeps the busy-gate and progress
   banner consistent.
2. **Snapshot state before I/O.** Under `seq_lock()`, copy the inputs the task
   needs (file paths, clip indices, sample references) into a local snapshot.
   Do all file I/O **after** releasing the lock.
3. **Publish results safely.** When the task produces PCM, follow the sample
   load path: decode into a heap buffer, then install it via
   `sample_install_cached` (which writes the disk cache and maps it) and
   publish the `AudioSample` into `g_Seq.samples` under `seq_lock()`. Build
   the peak cache before publishing.
4. **Post completion back to the UI.** From the worker, call
   `PostMessageA(g_hWnd, WM_APP_FULL_REDRAW, 0, 0)` (or set a pacer dirty
   flag) and `job_end(...)`. Do **not** call `InvalidateRect`/`MessageBoxA`
   on the main window from the worker; report errors through
   `cseq_report_error` (in `font.h`).

### 4.4 Adding a preview/audition voice (e.g. the Media Explorer)

The Media Explorer (`headers/media.h`) is the reference for a non-modal,
modeless popup with background I/O and a real-time preview voice. It follows
the FX-rack dialog pattern for the window and adds two background workers plus
an integrated audition voice (`headers/audition.h`):

1. **Non-modal window.** Lazy `RegisterClassA` + `CreateWindowExA`
   (`WS_EX_TOOLWINDOW | WS_EX_TOPMOST`, `WS_POPUPWINDOW | WS_CAPTION`),
   owner-centered, returned to the main message loop (no nested pump). See
   `open_media_explorer` (`media.h:1218`).
2. **Background dir scan.** `media_scan_thread_proc` (`media.h:144`) runs
   `FindFirstFileW`/`FindNextFileW` and header-only `ma_decoder` metadata reads
   on a worker; it publishes the entry list under `g_mediaListLock` (UI/worker
   only — never the audio thread) and posts `WM_APP_MEDIA_LIST`.
3. **Background preview decode.** `media_preview_thread_proc` (`media.h:252`)
   decodes the selected file into the *inactive* audition ping-pong slot,
   builds min/max waveform peaks, and publishes via `audition_play()`. It is
   single-flight (`g_mediaPreviewBusy`) with a `g_mediaPreviewGen` generation
   counter so stale decodes are discarded.
4. **Real-time voice.** `audition_process_voice` (`audition.h:162`) runs inside
   the master audio callback and must be **allocation-free, I/O-free, and
   GUI-free** (Hard Rules 1–2). It reads control state via `Interlocked*`,
   resamples at the configured speed, applies an output-level gain and a ~1.5 ms
   de-click fade (`AUDITION_FADE_FRAMES`), and supports loop (Repeat) or
   play-once with a smooth end fade.
5. **Lifecycle.** Pre-allocate the ping-pong buffers in `audition_preinit()`
   (called from `main.c:205` next to `audio_limiter_preinit`) and free them in
   `audition_shutdown()` (`main.c:315`). Stopping the panel (close/ESC) must
   call `audition_stop()` so a preview can't keep looping cached PCM in the
   master bus while the user works elsewhere.

---

## 5. Feature Reference: Track Trigger Probability, Transient Slicing & the MIDI Clip Source Model

Sections 5.1–5.2 are two context-menu slider features built on the shared
modeless-popup-silder paradigm (see §4.1). Both are **non-destructive until
committed**: they preview live and only write real state on `[APPLY]` / `ENTER`;
`ESC` / click-outside / `[CANCEL]` discards. §5.3 documents the piano-roll MIDI
clip source model (sample vs. SoundFont) and its dynamic clip naming. §5.4
documents how Shift+Wheel rate changes are debounced into their own undo step.
§5.5 documents sample clip reversing (single & batch) via content-addressed
transforms. §5.6 documents the piano-roll octave wheel and the VK-keyed
keyboard audition set.

### 5.1 Track-Level Trigger Probability

**Purpose.** Right-click a track header → **"Trigger Probability..."** opens a
single-slider popup that sets a per-track probability for every clip/note-on on
that track (0–100%, default 100%). Dragging applies it live during playback.

**Data model** (`types.h`, `SequencerState`). There is no `Track` struct —
track params are flat arrays, so the feature adds two:
`float trackTriggerProb[MAX_TRACKS]` (0..1, default 1.0) and
`uint32_t trackRngState[MAX_TRACKS]`, seeded `(track_index * 1337) + 1`. Both
are initialized at every track-init site (WinMain, `add_track_action`, the
new-project reset, and project load).

**PRNG.** `track_roll_probability(uint32_t *state, float prob)` (`types.h`) is a
lock-free **xorshift32** — never `rand()` (Hard Rule 3). It returns early for
`prob >= 1.0` / `<= 0.0` and otherwise compares a 24-bit sample against `prob`.
The state is per-track and deterministically seeded so live playback and WAV
export agree bit-for-bit given the same state.

**Playback hooks** (audio thread, allocation-free). The probability is read from
`RenderContext` and applied at three points:

- **Sample clips** (`render_frames`, `audio.h`): sample clips *stream*
  continuously rather than edge-trigger, so the roll happens **once at window
  entry** and is held for the whole window — otherwise a skipped clip would
  stutter on/off every frame. The per-clip edge state lives in
  `ctx->clipTrigState[]` (`0`=armed, `1`=play, `2`=skip); it re-arms when the
  clip leaves its window.
- **Synth clips** (Halo/Quadrum, `synth.h`): the roll gates each
  `nowOn && !wasOn` note-on edge. A failed roll marks the note active (so it
  won't re-roll) but allocates no voice; the note-off path is a safe NULL no-op.
- **Sample-backed MIDI** (`midi_process_clip_frames`, `audio.h`): per-note edge
  state (`midiNoteArmed[]` / `midiNoteSkipped[]`, indexed by
  `clipIdx*MIDI_MAX_NOTES + noteIndex`) rolls once at body entry and holds
  through body+tail.

**Concurrency.** All transient edge state lives per-`RenderContext` (zeroed =
armed). The live path uses static arrays in `audio_callback`, re-armed on the
stopped→playing edge; the export thread `calloc`s its own copies, so playback
can keep running during export and the export is deterministic. See §2.3 / §3.3
for the "state lives with the render pass" rule.

**Persistence.** A trailing optional `CSQP` section (`codec.h` / `project.h`)
stores each track's probability and RNG seed. Legacy files (and legacy loaders)
ignore unknown trailing sections, so this is backward/forward compatible.

**Project-file format & trailing sections.** `.csq` files are a fixed-magic
header (`CSQ4`) followed by a sequence of **versioned trailing sections**, each
with a 4-byte magic + version. A loader reads each section, validates the magic
and version, and **rewinds to the section start on any mismatch** — so a newer
file's unknown sections are skipped, and an older loader never misparses a
newer file. Every section's element count is clamped to its array bound before
the element loop, and the sample-decode path additionally requires
`rawBytes == frameCount * sizeof(float) * NUM_CHANNELS` (a corrupt header that
disagrees is skipped, preventing out-of-bounds peak/cache reads). Current
sections, in save order:

| Section | Magic | Persists |
|---|---|---|
| Header + ext | `CSQ4` / `CSQX` | bpm, swing, bars, track/sample/clip counts, lofi flag, quantize, grid division, master mode, time signature |
| Tracks | — | `CSQTrack`: mute, volume, EQ low/mid/high, EQ freq/Q |
| Clips | — | `CSQ3ClipEntry`: sample index, track, beats, offset frames, volume, playback rate, fades, flags, MIDI note count |
| Samples | — | name, frame count, compressed PCM |
| Granular | — | `CSQGranEngineHeader` + notes + optional own-sample PCM |
| MIDI | `MIDI` | per-clip note arrays (v1/v2) |
| Fades | `CSQF` | per-clip fade-in/out curve types |
| ADSR | `CSQA` | per-clip attack/decay/sustain/release |
| FX rack | `CSQE` | per-track slot count, type IDs, params |
| SoundFont ref | `CSQS` | relative/absolute path + preset slot (`.sf2` bytes never embedded) |
| Filter | `CSQV` | per-track filter type mask, frequency, Q, enabled |
| Synth clip kind | `CSQY` | per-clip kind + synth ADSR + `QuadrumParams[8]` + `HaloPatch` (v1/2/3) |
| Trigger prob | `CSQP` | per-track trigger probability + RNG seed |
| Master params | `CSQM` | master volume, lofi bit depth / sample rate, export bit depth |
| Track mix | `CSQT` | per-track pan, stereo width, solo |

`CSQM` and `CSQT` are the newest additions: they persist the master/lofi/export
settings and the per-track pan/width/solo state that the legacy `CSQTrack`
record does not carry, so a reloaded project keeps its mix and export
characteristics. Like every other section they are trailing and optional —
old files load with defaults, and new files load cleanly on older builds.
Serialization is pointer-free by design: only scalars, strings, sample indices,
frame offsets, and normalized values are written; no `float*`/`HWND`/`HANDLE`
is ever persisted.

### 5.2 Interactive Transient Slicing ("Slice..." / "Batch Slice...")

**Purpose.** Right-click a sample clip → **"Slice..."** (single) or
**"Batch Slice..." (`[N] clips`)** (multi) opens a single-slider dialog that
scrubs transient-detection sensitivity (1–100%, default 50%) and previews slice
boundaries as dashed overlay lines. `[APPLY]`/`ENTER` commits real cuts; cancel
leaves the clips untouched.

**Guard.** The entry is shown only when the app is not busy (`!g_Seq.isBusy`)
and every target clip is a non-MIDI sample clip with a valid loaded PCM buffer.
MIDI/piano-roll clips are never sliceable.

**Detection algorithm** (`headers/slicing.h`, pure C99). `detect_clip_transients`
scans the clip's own frame region, maps sensitivity inversely to a threshold,
tracks a **one-pole low-pass envelope** (`prev_env = prev_env*decay + mono*(1-decay)`),
and emits a slice at each rising edge past a ~40 ms minimum distance, snapped to
a zero crossing. Two corrections to the original spec are load-bearing:

- The envelope must be a one-pole low-pass, **not** the leaky-integrator form
  `mono + prev_env*decay`, which accumulates to ~`1/(1-decay)` for a sustained
  tone and makes `diff` permanently negative (the detector then never fires).
- The minimum-distance guard must track the **loop index**, not the
  zero-crossing-snapped position — the snap can land at/before the detection
  point and cause immediate re-firing (duplicate slices).

**Preview.** Dragging the slider recomputes the maps into `g_slicePreview`
(`SlicePreviewState` in `slicing.h`, defined in `main.c`) and
`draw_slice_preview_overlay` (`ui.h`) draws dashed lines on the timeline overlay
pass. No destructive edits happen during the drag.

**Commit pipeline** (`commit_slice_preview`, `dialogs.h`), under `seq_lock()` +
`push_undo_state()`:

1. **Zero-copy:** every slice reuses the parent's `sampleIndex` — no PCM is ever
   duplicated (Hard Rule 4).
2. **Timeline math:** `beats = frames * bpm / (60 * SAMPLE_RATE * playbackRate)`.
   `playbackRate` is in the **denominator**, matching `split_single_clip_internal`
   (`actions.h`) and `render_frames` — the spec's numerator form would misalign
   slices on rate-changed clips. The original clip becomes Slice 0; Slices 1..N
   are appended with `nextClipInBar=0xFFFF` and `synth_state_init_clip`.
3. **Capacity:** if adding slices would exceed `MAX_CLIPS`, excess slices are
   truncated gracefully.
4. **Selection handoff:** the derived slices are selected and old parents
   deselected, then `cseq_clip_structure_changed()` + `InvalidateRect`.

### 5.3 The MIDI clip source model & dynamic clip naming

**Purpose.** A standard (purple) MIDI clip can play from one of two sources: a
**sample** attached to the clip (`Clip.sampleIndex >= 0`) or the **global
SoundFont** (`sampleIndex == -1`, the clip falls through to `g_SFont`). These
two sources are **mutually exclusive** on the piano roll — the app guarantees
they can never be loaded at the same time — so the clip's on-timeline label can
be derived unambiguously from whichever source is active.

**Mutual exclusion** (`dialogs.h`, the piano-roll toolbar + `WM_DROPFILES`).
Loading one source clears the other, defensively, in every entry path:

- **LOAD SAMPLE** button / audio drop: after attaching the sample, calls
  `sfont_clear()`.
- **LOAD SOUNDFONT** button / `.sf2` drop: before loading, detaches the current
  clip's sample (`sampleIndex = -1`).
- The **red [X]** clears **both** the soundfont and the current clip's attached
  sample, so the user can load whichever source they want next.

The two source buttons are also **greyed out / disabled** while the other source
is active (`LOAD SAMPLE` while a font is loaded, `LOAD SOUNDFONT` while a sample
is attached) — enforced in both the draw pass and the click/drop guards. This
prevents a race where a clip's source changes mid-flight and keeps the naming
scheme from ever conflicting.

**Dynamic clip naming** (`ui.h`, `draw_waveform_clip`). Clip labels are computed
at draw time — there is no stored `Clip.name` field. A MIDI clip with
`sampleIndex < 0` and a loaded font renders `"<font> / <instrument> (N notes)"`
(e.g. `GM.sf2 / Acoustic Grand Piano (4 notes)`), where the instrument is
`sfont_preset_name(sfont_active_preset_slot())`. Because the label reads the
selected instrument live, switching presets in the instrument picker
(`sfont_set_active_preset_slot`) updates the clip label on the next redraw with
no stored-name persistence. This mirrors the existing sample-clip naming
(`"<sampleName> (N notes)"`); a clip with no source at all still falls back to
`"MIDI (N notes)"`. All of this is UI-thread-only — the audio thread's
sample-vs-soundfont resolution (`audio.h:291-297`) is unchanged.

### 5.4 Rate-change undo: Shift+Wheel debounce & flush

**Purpose.** Shift+Wheel over a clip adjusts its playback rate
(`Clip.playbackRate`). Because a wheel tick is a single continuous gesture —
one scroll can emit many `WM_MOUSEWHEEL` messages — the rate change is **not**
committed as an undo step per tick. Instead it is debounced so that a burst of
ticks collapses into one undo step, and the step is forced to fire before any
*different* action (like starting a drag) so the rate change never gets glued
onto another edit.

**Data model** (`types.h`, `SequencerState`). A single
`ULONGLONG rateUndoDebounceTimer` field: `0` = no pending undo, `>0` = the
`GetTickCount64()` expiry time in ms. It is zero-initialized at startup (the
whole `g_Seq` struct is zeroed) and explicitly reset to `0` in
`reset_to_init_state` (`dialogs.h`).

**Arming** (`events.h`, `WM_MOUSEWHEEL`). The timer is set **only** in the
`shiftHeld` branch — i.e. only for rate adjustments:

```c
if (shiftHeld) {
    // ... playbackRate += deltaRate on the clip / all selected clips ...
    g_Seq.rateUndoDebounceTimer = GetTickCount64() + 300;
}
```

Volume adjustments (the `else if isSelected` / `else` branches) deliberately do
**not** touch the timer. A plain wheel over a clip adjusts volume continuously
with no undo debounce, so the timer's presence is a reliable signal that a
rate change is in flight.

**Firing** (`events.h`, `WM_TIMER`). If the user just stops scrolling, the
timer fires after 300 ms and commits the rate change as its own undo step:

```c
if (g_Seq.rateUndoDebounceTimer != 0 &&
    GetTickCount64() >= g_Seq.rateUndoDebounceTimer) {
    g_Seq.rateUndoDebounceTimer = 0;
    push_undo_state();
    g_Seq.isModified = true;
    update_window_title();
}
```

**Flush** (`events.h`, `WM_LBUTTONDOWN`). If the user starts a new action
before the timer fires — most importantly, mousedown on a clip to begin a
drag — the pending rate-undo is flushed immediately, **before** any selection
changes or drag setup. This runs under `seq_lock()` (it sits inside the clip
hit-test block) and guarantees the rate change and the drag each get their own
undo step:

```c
Clip* c = &g_Seq.clips[clipIdx];

// --- Flush any pending rate-change undo before starting a new action ---
if (g_Seq.rateUndoDebounceTimer != 0) {
    g_Seq.rateUndoDebounceTimer = 0;
    push_undo_state();          // this sets g_Seq.isModified = true
    update_window_title();
}
```

**Why the two paths are both needed.** The debounce alone would eventually
commit the rate change, but if the user begins a drag within the 300 ms window
the timer would fire mid-drag and produce a rate-undo interleaved with the
drag-undo. The flush forces the pending rate change to be committed first, so
the subsequent drag pushes a clean, separate undo state. The whole mechanism
adds no extra timers or locks — it reuses the existing 16 ms `WM_TIMER` slot
and the existing `seq_lock()`.

### 5.5 Sample Clip Reversing (Single & Batch)

**Purpose.** Right-click a sample clip → **"Reverse"** (single) or
**"Batch Reverse... (`[N] clips`)"** (multi) reverses the audio playback of the
targeted clip(s). Unlike Transient Slicing (§5.2, which presents an interactive
sensitivity slider dialog with scrubbing overlay lines), Reversing is a binary
deterministic operation that commits immediately upon click under `seq_lock()`
and the undo pipeline.

**Guard & Context Menu Integration.** Defined in `headers/dialogs.h`
(`show_clip_context_menu`), following the exact eligibility guard as Slicing:
```c
// Shown only when:
// - !g_Seq.isBusy
// - every target clip is non-MIDI, sampleIndex >= 0, valid loaded PCM
```
If any target fails (e.g. MIDI clip, unassigned sample, or empty buffer), the
reverse items are omitted from the context menu. Menu command ID `ID_CLIP_REVERSE`
(`headers/config.h:116`) dispatches directly to `reverse_clips_action`.

**Architectural Decision: Content-Addressed Transform vs. Real-Time Playback Branch.**
A naive implementation might add a boolean `isReversed` flag to `Clip` and
modify the audio mixing callback (`render_frames` in `audio.h`) to step backward
through `pFrames`. That approach was rejected for four critical architectural reasons:
1. **Real-time hot path pollution:** branching `render_frames` for backward
   streaming complicates linear interpolation, SIMD summing paths, and sample-loop
   wrapping logic (the $\ge 2\times$ length loop policy wraps back to frame 0, which
   in reverse would need to wrap to `frameCount - 1`).
2. **Cross-cutting interactions:** backward playback would require special-cased
   handling in fade curves, ADSR envelopes, alt-slip dragging, and granular
   engines (`g_ClipGran`).
3. **Export bit-parity:** live playback and WAV export share the exact same
   `render_frames` loop; altering the playback loop risks divergence between live
   rendering and offline export.
4. **Hard Rule 1–3 preservation:** by pushing all transform complexity to the UI/edit
   thread and leaving the audio callback reading PCM strictly forward from
   `sampleOffsetFrames`, the real-time thread remains allocation-free, lock-minimal,
   and deterministic.

Reversing is therefore implemented as a **content-addressed sample transform**:
the edit thread produces a reversed sample buffer, installs it into the sample pool
and disk cache, and reassigns the clip's `sampleIndex`.

**Core Pipeline & Content Dedup** (`get_or_create_reversed_sample`, `actions.h`):
1. **PCM buffer reversal:** a temporary stereo buffer (`frameCount * 2 * sizeof(float)`)
   is allocated and populated with the reversed interleaved channels:
   `revPcm[i * 2 + ch] = srcPcm[(frameCount - 1 - i) * 2 + ch]`.
2. **64-bit FNV-1a hashing:** hashed using `sample_hash_pcm`.
3. **Lossless dedup lookup:** `g_Seq.samples` is scanned for an existing loaded
   sample with matching `frameCount` and `contentHash`:
   - **Reversing back to original:** reversing an already-reversed sample produces
     PCM identical to the original forward sample. Dedup matches the original
     sample's hash and returns its existing `sampleIndex` without adding any new
     entry to `g_Seq.samples` or disk.
   - **Shared reversed instances:** if multiple clips or batches reverse the same
     source sound, the existing reversed `sampleIndex` is reused.
   - **Hard Rule 4 zero-copy guarantee:** slices and clips referencing the same
     reversed sample share its buffer without duplicating PCM.
4. **Cache registration:** if new, the buffer is stored in the memory-mapped disk
   cache via `sample_install_cached` (`%LOCALAPPDATA%\cseq\cache\<hash>.pcm`),
   assigned a suffixed display name (`"kick.wav (reversed)"`), and its LOD peak
   pyramid is built synchronously via `generate_peak_cache_auto` so waveform
   drawing renders immediately.

**Frame-Offset Remapping Math** (`reverse_remap_clip_offset`, `actions.h`):
Because `sampleOffsetFrames` is relative to the start of the buffer, reversing the
buffer requires remapping the clip's playback window:

$$\text{newOffset} = \text{frameCount} - \text{sampleOffsetFrames} - \text{clipFrameLength}$$

Key mathematical details (the "corrections to the naive spec"):
- **Rate-scaled frame length:** converting clip duration from timeline beats to
  buffer frames scales by playback rate in the numerator:
  $$\text{spanFrames} = \text{lengthBeats} \times \text{fpb} \times \text{playbackRate}$$
  (This is the exact inverse of Slicing's frame-to-beat formula in §5.2, where
  `playbackRate` sits in the denominator: `beats = frames / (fpb * playbackRate)`).
- **Available-frame boundary clamp:** `clipFrameLength` is clamped to
  `frameCount - sampleOffsetFrames`. If a clip reaches or extends past the buffer
  end, this clamp prevents unsigned integer underflow and clamps `newOffset` to 0.
- **Zero-crossing snap omission:** unlike Slicing and splitting (which snap cuts to
  nearest zero-crossings to eliminate boundary transients), `reverse_remap_clip_offset`
  intentionally does **not** snap to zero crossings. Snapping on reverse would cause
  subtle phase drift across successive reverse/un-reverse operations. Without snapping,
  remapping is **strictly symmetric and invertible**:
  $$\text{remap}(\text{remap}(\text{offset})) \equiv \text{offset}$$
  Toggling reverse back and forth is 100% mathematically lossless.

**Batch Execution Path** (`reverse_clips_action`, `actions.h`):
Iterates all target clips and memoizes source transformations in `remapSample[MAX_SAMPLES]`:
- Unique source samples are reversed and hashed **once per batch**, not once per clip.
- Each clip's `sampleOffsetFrames` is individually remapped around its source buffer.
- `g_ClipGran[cIdx].sampleIndex` is kept in sync for clips with granular mode enabled.
- All target bars are marked dirty (`mark_clip_bars_dirty`).

**Commit, Undo & Redraw Pipeline:**
```c
push_undo_state();
seq_lock();
// remap offsets, update sampleIndex, sync granular engines
seq_unlock();
cseq_clip_structure_changed();
invalidate_timeline_cache();
InvalidateRect(g_hWnd, NULL, FALSE);
```
- `push_undo_state()` captures the pre-reversed state so `Ctrl+Z` immediately restores
  the original sample indices and offsets.
- `cseq_clip_structure_changed()` rebuilds the timeline bar grids and track masks.
- `invalidate_timeline_cache()` flushes cached waveform bitmaps and fade templates
  so the reversed waveform renders on the very next paint pass.
- **Audio-thread impact: zero.** No locks held across audio processing, no memory
  allocations on the audio thread, and no changes to `render_frames`.

### 5.6 Piano-roll octave wheel & the VK-keyed keyboard audition set

**Purpose.** Two refinements to the piano rolls (the MIDI/synth editor
`MidiEditorWndProc`, `dialogs.h:5848`, and the granular editor `GranWndProc`,
`granular.h:632`): the **Octave button** takes the scroll wheel, and the
QWERTY keyboard audition set is keyed by the **physical key** so an octave
change mid-hold can never strand a sounding note.

**Octave wheel.** Wheeling over the Octave button shifts the roll one octave
per notch — up = one octave up, down = one down — clamped to ±3, exactly like
the existing left/right-click handlers. In the MIDI roll it is the
`WM_MOUSEWHEEL` octave block (`dialogs.h:6010`), checked after the ADSR-knob
hover step and skipped for Quadrum (fixed voices); the granular roll gains a
new `WM_MOUSEWHEEL` case in `GranWndProc` (`granular.h:1578`) — it previously
had no wheel handling at all, and unhandled wheel positions fall through to
`DefWindowProc`. Hit rects mirror the draw pass exactly:
`{scale_x(106) .. scale_x(210) × h-scale_y(26) .. +scale_y(20)}` for the MIDI
roll and `{106 .. 206 × h-26 .. h-26+20}` for granular (that window lays out
in raw pixels).

**Why pitch-keyed key-up was broken.** The polyphonic keyboard audition set
(`kbHeldNotes[]`, up to `MIDI_KB_MAX` = 8) stored the pitch resolved at press
time, but `WM_KEYUP` re-resolved the pitch against the *current* octave and
removed by pitch. An octave change while a key was held made the key-up miss:
the orphaned entry kept republishing its pitch through
`midi_audition_rebuild_poly()` / `gran_audition_rebuild()`, the audio-side
audition slot never saw a release (its "still held" check matches by pitch),
and the note rang until some later key-up happened to re-resolve to the stuck
pitch (octave back, then release) or an explicit clear ran (ESC, close,
Keyboard toggle, editor open). Re-pressing the same key did not help —
`kb_add` deduplicated by pitch, so the new octave's entry was simply added
alongside the orphan.

**The fix: key entries by the physical key (VK).** `MidiEditContext` and
`GranularEngine` each carry a parallel `kbHeldVKs[MIDI_KB_MAX]` array
(`types.h:749` / `types.h:238`). `midi_audition_kb_add(vk, pitch)` /
`midi_audition_kb_remove(vk)` (`types.h:814`, under `midi_lock`) and
`gran_audition_kb_add(e, vk, pitch)` / `gran_audition_kb_remove(e, vk)`
(`types.h:877`, under `seq_lock`) dedupe, evict and match by VK; the
`WM_KEYUP` handlers pass `(int)wParam` and resolve no pitch (the
`pr_key_to_semitone` check survives only as the "is this an audition key"
gate). Consequences:

- An octave change (wheel or click) while holding keys is safe by
  construction: held notes keep sounding at their press-time pitch and
  release on the physical key-up.
- Keys that collapse onto one voice (Quadrum's `semi % 8`: 'A' and 'K' both
  map voice 0) now track independently — releasing one keeps the voice while
  the other is down. The union published to the audio thread
  (`auditionNotes[]`) still deduplicates pitches.
- The mouse strip needed nothing: `WM_LBUTTONUP` unconditionally calls
  `midi_audition_set_mouse(-1)` / `gran_audition_set_mouse(e, -1)`, so it
  can never strand an entry.

Snapshot/serialization impact: none — the granular engine snapshot already
zeroes `kbHeldCount` on restore (`state.h:128`) and audition state is never
persisted.

---

## 6. The FNV Hash Tree & the Double-Buffered GDI Pipeline

Two coupled mechanisms keep the UI fast, flicker-free, and cheap to redraw:
a **content-addressed FNV hash tree** that decides, per frame, exactly which
region of the timeline actually changed, and a **multi-layer double-buffered
GDI pipeline** that turns "something changed" into a screen blit without ever
redrawing the whole scene from scratch. The same hash + cache idiom is reused
for the synth editor's knob rack and for the disk-backed sample cache.

### 6.1 FNV hashing primitives

All dirty-detection hashing is **FNV-1a**, folded to a `DWORD`. The two helpers
in `headers/ui.h` (`hash_dword`, `hash_float`) mix one field into an
accumulator:

```c
static inline DWORD hash_dword(DWORD hash, DWORD val) {
    return (hash ^ val) * 16777619u;            // FNV-1a prime
}
static inline DWORD hash_float(DWORD hash, float val) {
    DWORD u = 0; memcpy(&u, &val, sizeof(float));   // bit-preserving
    return (hash ^ u) * 16777619u;
}
```

Every hash starts from the FNV-1a offset basis `2166136261u`. Floats are
folded by copying their bits into a `DWORD` (via `memcpy`, never a cast) so
`-0.0`/`+0.0` and subnormals hash consistently with their raw representation.
A separate 64-bit FNV-1a (`sample_hash_pcm`, `headers/samplecache.h:24`) hashes
raw PCM bytes for the content-addressed sample cache; see §6.5.

### 6.2 The timeline hash tree (a Merkle-style accumulator)

The timeline is too large to rehash in full every frame, and re-rendering it in
full every frame would be unaffordable. Instead the timeline is partitioned
into a **fixed grid of bar-chunks** and each chunk is hashed independently,
then the chunk hashes are folded into one root hash — the same shape as a
Merkle tree, but with a flat, cache-friendly layout.

- **Chunk grid.** `MAX_BARS` (1024) bars are split into
  `TIMELINE_HASH_CHUNK_COUNT` chunks of `TIMELINE_HASH_CHUNK_BARS` (32) bars
  each (`headers/ui.h:2411`). Per-track chunk hashes live in
  `g_chunkHash[MAX_TRACKS][TIMELINE_HASH_CHUNK_COUNT]`.
- **Per-chunk hash** (`hash_track_chunk`, `ui.h:2426`): seeded with the track
  and chunk indices, then each clip overlapping the chunk's bar range folds in
  its playback-relevant fields (sample, track, startBeat, lengthBeats,
  sampleOffsetFrames, volume, playbackRate, fades, flags, midiNoteCount).
- **Root hash** (`compute_timeline_content_hash`, `ui.h:2477`): folds every
  chunk hash into one accumulator under `seq_lock()`. Only chunks whose
  `barDirty` bits are set are recomputed (`hash_chunk_is_stale`, `ui.h:2419`);
  unchanged chunks keep their cached hash, so a one-clip edit recomputes a
  handful of chunk hashes instead of the whole track.
- **Param hash** (`compute_timeline_param_hash`, `ui.h:2461`): a second,
cheaper root over global state — zoom, BPM, visibleBarCount, gridDivision,
trackCount, clipCount, per-track mute/solo/volume/granular flags, and
**per-track active filter status**(`enabled && typeMask != 0`).
It is checked before the content hash because it changes far more often
and is cheaper to fold.

`is_timeline_dirty(w, h)` (`ui.h:2498`) is the gate: it returns true on a
window resize or explicit `g_timelineDirty`, else compares the param hash, then
the content hash, against the last committed values. Only when one differs does
the render path touch the cache. After a full render, `update_timeline_cache`
commits `g_lastParamHash`/`g_lastContentHash` and marks the bars rendered
(`bars_mark_rendered`, `ui.h:2658`), so the next frame is a pure blit.

The synth editor uses the same idiom in miniature: `synth_ui_is_dirty`
(`headers/synthui.h:451`) hashes the target clip's kind, the selected Quadrum
voice / Halo patch, and the knob-drag highlight into `s_lastHash`, forcing a
cache rebuild only when the hash (or an explicit `invalidate_synth_cache()`)
changes.

### 6.3 The double-buffered GDI pipeline

Rendering never draws straight to the screen. Every surface is drawn into an
offscreen `CreateCompatibleBitmap` selected into a `CreateCompatibleDC`, then
copied to the window DC with `BitBlt`/`StretchBlt` (or `AlphaBlend` for the
supersampled AA capsules/knobs). This eliminates flicker (no erase-then-draw)
and lets a frame skip the expensive parts entirely.

The main window is a **three-layer** pipeline (`headers/ui.h`):

1. **Timeline cache** (`g_cacheDC`/`g_cacheBmp`, `ui.h:2328`) — the static
   scene: track grid, clips, ruler. It is rebuilt only when the hash tree says
   the content changed, or the window resized. On a pure scroll it is *not*
   rebuilt: `scroll_shift_timeline_cache` (`ui.h:3094`) `ScrollDC`s the bitmap
   by the scroll delta and re-renders only the narrow gutter strip that was
   exposed. When only a few bars changed, `timeline_redraw_dirty_bars`
   (`ui.h:2515`) re-renders just those bar bands into the existing cache.
2. **Main back buffer** (`g_mainBackDC`/`g_mainBackBmp`, `ui.h:2340`) — a
   full-client-compatible surface. `render_ui` (`ui.h:3311`) blits the timeline
   cache into it, then draws the *dynamic* overlays on top — hover/selection
   highlights, fades, marquee, playhead, drag ghosts — clipped to the track
   viewport. This layer exists so the per-frame dynamic stuff doesn't dirty the
   expensive static cache.
3. **Window DC** — the final `BitBlt` of the back buffer to the screen
   (`ui.h:3900`).

The synth editor's `SynthUIWndProc` (`headers/synthui.h:798`) uses the same
pattern at a smaller scale: `WM_ERASEBKGND` returns 1 (no default erase), the
knob rack is rendered into `s_synthCacheDC`/`s_synthCacheBmp` only when
`synth_ui_is_dirty` says so, then blitted to a per-paint `memDC` (which carries
the footer hints) and finally to the window DC. The Media Explorer, FX rack,
and other modeless popups follow the same double-buffer idiom (see §4.1/§4.4).

**Partial knob redraw during a live drag.** A knob drag updates the param on
every `WM_MOUSEMOVE`. Rebuilding the whole rack cache (29 supersampled knobs,
each a 3× DIB) per mouse-move would hog the UI thread and stall the piano-roll
playhead, which is repainted on its own 33 ms timer in the separate
`MidiEditorWndProc` (`dialogs.h:5751`) — both windows share the one UI thread.
So the drag path (`synthui.h:1135-1139`) calls `synth_ui_redraw_knob(idx)`,
which sets `s_synthRedrawOnly` (`synthui.h:458`) and invalidates only the
dragged knob's rect. `synth_ui_update_cache` (`synthui.h:547`) then skips the
background clear and voice-pad row and redraws just that one knob into the
persistent cache. Because the knob's label/value text is drawn with
`SetBkMode(TRANSPARENT)` directly on the cache, the partial path first clears
the knob's whole cell (`FillRect`, `synthui.h:619/634`) so the text doesn't
stack across frames; the full-rebuild path clears the whole cache and needs no
per-cell clear. `invalidate_synth_cache()` resets `s_synthRedrawOnly = -1`, so
all non-drag changes (voice select, preset, reset, wheel, grab, drag-end) fall
back to a full rebuild.

**Throttled Quadrum transient re-render.** For Quadrum, a live knob edit must
re-render the pre-rendered transient off the audio thread
(`synth_ui_push_patch`, `synthui.h:90` → `synth_quadrum_rerender_voice`). That
render is also throttled to ~60 Hz (a `GetTickCount` gate) so a fast drag
doesn't flood the UI thread; the param value itself is written live every move,
so the sound stays immediate.

**GDI hygiene.** Every created brush/pen/bitmap/DC is paired with a
`DeleteObject`/`DeleteDC` *after* the original object is restored via
`SelectObject` — see Hard Rule 8. Because these surfaces persist across many
repaints (a 16 ms/33 ms timer or the 120 fps pacer), a single leak in a
repaint path exhausts GDI handles over time. `shutdown_render_surfaces`
(`ui.h:2347`) and the synth editor's `WM_DESTROY` tear all of these down
cleanly.

### 6.4 Drawing rule: shapes that share a boundary with a curve must derive from the same evaluation

The fade overlays in `headers/ui.h` are a reusable example of a general drawing
rule in this codebase: **whenever a filled shape's boundary is supposed to
coincide with a curve you also draw, build the shape from the *same* analytic
evaluation the curve uses — never approximate the shared edge with a straight
segment.**

Fade rendering (`ui.h:1427-1818`) is supersampled into a cached alpha template
keyed by `(w, h, curveType, isFadeIn)` (`fade_template_get`/`g_fadeTpls`,
`ui.h:1645`), so the shape is rasterized once per size/curve and reused across
clips. Two passes write into that template:

- `fade_raster_curve_2x` (`ui.h:1608`) draws the bright envelope **line** by
  sampling `compute_fade_gain(t, curveType, isFadeIn)` (`dsp.h:63`).
- `fade_raster_wedge_2x` (`ui.h:1582`) draws the low-opacity **wedge** beneath
  it, whose top edge is the envelope.

The invariant: the wedge's top edge must land exactly on the curve line, so the
fill hugs the envelope with no gap. For non-linear fades (exp/smooth/log) the
envelope bows away from a straight diagonal, so the wedge boundary must be
sampled with the **same** `compute_fade_gain` calls **and the same rounding**
as the line pass. `fade_raster_wedge_2x` therefore builds a polygon from
`compute_fade_gain` with the identical `(int)(t*(ssW-1)+0.5f)` /
`(int)((1.0f-gain)*(ssH-1)+0.5f)` rounding, then closes along the bottom. Gain
is monotonic in `t`, so the polygon boundary never self-intersects. Linear
fades degenerate to the original triangle, so the straight-line shortcut is
still the correct special case — the rule is to derive the shared edge from the
curve, not to hard-code a shape that happens to match linear only.

Two practical consequences worth keeping:

1. **Never hand-draw the shared edge** (a `Polygon` triangle with a straight
   hypotenuse) for a boundary that must match an analytic curve. Reuse the
   curve evaluation.
2. **Match rounding exactly.** A shape that samples the same function but with
   different pixel rounding will still leave a visible seam against the line.
   Keep the two passes' `t → pixel` mapping identical.

`tests/fade_wedge_verify.c` (+ `fade_wedge_verify.bat`) extracts the real
`config.h` fade enum, `dsp.h` `compute_fade_gain`, and `ui.h` fade-template
block verbatim and asserts the wedge reaches the curve line in every pixel
column for all curve types — the regression guard for this rule.

### 6.5 Content-addressed sample cache (FNV-1a 64)

The disk-backed PCM cache (`headers/samplecache.h`) is named by a **64-bit
FNV-1a hash of the raw decoded PCM bytes** (`sample_hash_pcm`): the file name
is `<hash>.pcm`. This makes the cache content-addressed — identical decoded
audio maps to one file regardless of source path — and it is what the importer
uses for **import dedup** (`Clip.contentHash`, `types.h:137`). Decoded float
PCM is written once, then memory-mapped so the OS pages it in/out under memory
pressure instead of the app holding the full buffer resident. The realtime
audio thread only dereferences `pFrames[i]` as plain memory reads; only the
load thread touches disk.

### 6.6 The synth / MIDI preview lifecycle

The piano roll (`MidiEditorWndProc`, `dialogs.h:5848`) hosts an **in-window
audition** that plays the current clip through the same engine the timeline
uses. For synth-module clips (Halo/Quadrum) the preview drives the per-clip
voice state (`g_ClipHalo`/`g_ClipQuadrum`) in `midi_editor_process_preview`
(`audio.h:447`). For standard sample/SF2 MIDI clips it renders two voice
families, both shaped by the clip's ADSR knobs through the same
`midi_adsr_gain` / `midi_adsr_level_at` primitives the timeline's
`midi_process_clip_frames` voices use:

- **Audition slots.** 8 static `AudSlot`s (`audio.h:575`), one per held key
  from the shared held-set (mouse strip + QWERTY, §5.6), all under
  `midi_lock`. While held, a slot runs A→D→S (`envLen` pinned to
  `kHeldLen`) and settles on the Sustain knob; on key-up `envLen` freezes at
  the current frame (`audio.h:589`), so the release tail fades over the
  Release knob's time from the exact level-at-release (gain-continuous at
  the freeze frame). Slots retire only once the tail has fully died
  (`audio.h:628-637`), and a sustain-0 held key stays allocated (silent)
  instead of being recycled. Mouse-strip glide retrigger-attacks the new
  pitch (fresh slot, `envFrame = 0`) while the old pitch freezes into its
  tail. Attack/Release knobs of 0 are floored at 2 ms in the audition
  (de-click); the timeline path uses exact 0, so the two differ only at
  knob-0 settings. Minor edge case: gliding back onto a pitch whose slot is
  still mid-tail reuses that slot, so the tail keeps fading instead of
  retriggering.
- **[PLAY]-loop voices** (`s_ring`): notes scanned at the looping playhead
  mirror the timeline envelopes and keep ringing through their release tail
  past the written note end (`relUntil`), with tails dropped on loop wrap.

The audio thread renders the preview only while the audition flags are set
(`auditionHeld` or `isAuditionPlaying`), so clearing them is what actually
silences it.

**Known gap: the last release tail is truncated.** A slot's tail renders
only while the preview keeps running. When the *final* held key is released
(or PLAY stops), `auditionHeld`/`isAuditionPlaying` drop, the callback's
keep-alive check consults only `synth_editor_has_ringing` — a Halo/Quadrum
engine check that returns false for standard MIDI clips (`synth.h:588-608`)
— and `midi_editor_process_preview` early-outs on `!playLoop && !audHeld`
(`audio.h:522`), so the tail of the last key is cut to the ~6 ms master fade
(`kRampStep` = 1/256 per frame) instead of the Release knob's time. Tails
while other keys remain held are unaffected. Lifting this means keeping the
preview alive while any audition slot is still keyed — the slot table is a
function-local static under `midi_lock`, so the gate cannot see it today.
`tests/adsr_verify.c` pins the shared envelope shape numerically (A/D/S
levels, release-from-level-at-release, sustain-0 semantics, freeze-frame
continuity).

**Playhead repaint is independent of knob edits.** The playhead is a *dynamic
overlay* drawn on top of the cached piano-roll bitmap each frame and repainted
by the piano roll's own 33 ms `WM_TIMER` (`dialogs.h:5781`) — it reads the
audio-published beat position per tick. The Halo/Quadrum knob rack is a
separate window (`SynthUIWndProc`, `synthui.h:798`). The two windows share the
UI thread, which is why the knob-drag path must stay cheap (see §6.3) — it
must not delay the piano roll's timer and stall the playhead.

**Stopping a preview on close.** Closing the piano roll via the title-bar X
hides the window (`WM_CLOSE`, `dialogs.h:6982`) rather than destroying it, so
`WM_DESTROY` (which clears the audition flags) never runs on a normal close.
`WM_CLOSE` therefore clears the audition state itself — under `midi_lock()`
it empties the polyphonic held-set (`midi_audition_clear_poly`, which zeroes
`kbHeldCount`/`mousePitch` and rebuilds `auditionHeld` to false) and clears
`isAuditionPlaying` before hiding — so a Halo/Quadrum/MIDI preview stops
cleanly instead of looping in the master bus while the user works elsewhere.
The ESC key path (`dialogs.h:6850`) clears the same state, and `WM_DESTROY`
still clears it as a safety net for the destroy case.

**Piano-roll note entry & hit-testing.** Click-to-add in the grid
(`dialogs.h` WM_LBUTTONUP) snaps the pointer beat **down** to the clicked
grid cell via `quantize_beat_floor` (`dsp.h`) — a round-half-up snap would
push clicks in the right half of a cell one cell to the right — and clamps
the placed note so it always sits fully inside `[0, clipLen]` with at least
the minimum note length. Note hit-testing (`midi_edit_get_note_under_mouse`,
`dialogs.h`) uses a ±6 px slack around the note with a minimum 8 px click
width (short notes render at 6 px but stay grabbable), and the resize-edge
zones are only the outer 3–4 px so the note body remains grabbable for
moving.

---

## 7. The "Hard Rules" (What to Avoid and Why)

Zero-tolerance antipatterns. Violating any of these is a blocking code-review
failure.

| # | Rule | Why |
|---|---|---|
| 1 | **NO `malloc`/`calloc`/`free`/`realloc` inside the audio callback** (or any function it calls on the realtime path). | Heap lock contention and OS priority inversion. The RT path must be allocation-free; pre-allocate everything before the device starts (e.g. the master limiter delay lines via `stereo_limiter_init`). |
| 2 | **NO Win32 GUI calls, file I/O, or blocking mutex locks inside the audio callback.** | `SendMessage`/`GetDC`/`InvalidateRect`, `fopen`/`fread`, and long-held locks stall the realtime thread and cause glitches. The visualizer is fed through the lock-free SPSC ring; all disk work lives in workers. |
| 3 | **NO `rand()` in audio/sequencing loops.** | `rand()` is global, non-reentrant, and locks a shared RNG state across threads. Use track/engine-local `xorshift32` (`gran_xorshift32`, `granular.h:31`) with a thread-local seed (`tls_granRngState`). |
| 4 | **NO buffer duplication when slicing audio clips.** | Always use offset/length views into shared sample memory (`Clip.sampleOffsetFrames`/`lengthBeats`), never copy PCM per clip. Zero-copy slicing is the ownership model. |
| 5 | **NO `volatile` for cross-thread synchronization.** | Bare `volatile` does not provide atomicity or ordering guarantees. Use true atomic primitives: `InterlockedExchange`/`InterlockedCompareExchange`/`InterlockedOr64`, or `CRITICAL_SECTION`s. `volatile` is acceptable only as a compiler barrier on a value already guarded by atomics. |
| 6 | **NO allocations or re-entrant calls while holding `seq_lock()`.** | The lock is hot (per-chunk) and short; holding it across I/O or allocation blocks the audio thread and can deadlock with a worker. |
| 7 | **NO `InvalidateRect`/`MessageBoxA` on the main window from worker threads.** | GDI/user32 calls from a non-owner thread are unsafe. Post `WM_APP_FULL_REDRAW` or set a pacer hint instead. |
| 8 | **NO deleting a GDI object while it is still selected into a DC.** | `DeleteObject` on a currently-selected object fails silently and leaks it; every `CreateSolidBrush`/`CreatePen`/`CreateCompatibleBitmap` must be paired with a `DeleteObject`/`DeleteDC` *after* the original object is restored via `SelectObject`. A leak in a repaint path (many repaint on a 16 ms timer) exhausts GDI handles over time. Capture the DC's original pen/brush once and restore before each delete. |

---

## Appendix: Building & verification

| Variant | Command | Output | Notes |
|---|---|---|---|
| Release | `build.bat` | `cseq.exe` | `/O1 /Os /GL /LTCG`; `/W4 /WX` on project code |
| AddressSanitizer | `build_asan.bat` | `cseq_asan.exe` | `/MTd /Od /Zi /fsanitize=address` |

Optional env toggles for `build.bat`: `cseq_AVX2=1` (8-wide AVX2 track-summing
path, runtime CPU-guarded) and `cseq_PROFILE=1` (profiling via
`OutputDebugString`).

**Pre-merge verification checklist:**
1. `build.bat` completes with **zero warnings** (project code is `/W4 /WX`).
2. Launch app; toggle playback ~10×; resize across several sizes; close.
3. GDI/USER handle counts (Task Manager) stable across the run.
4. `build_asan.bat` + same workflow → no AddressSanitizer report.
5. Idle CPU (playback stopped) ≈ 0%; no flicker during resize.
6. Close the window *while* a save/load/export/SF2 load is in flight → no crash
   or hang (the shutdown drain in `WinMain` must join the detached workers
   before the critical sections are deleted).
7. Open the Media Explorer, Granular editor, and FX rack; repaint each several
   times and confirm GDI handle counts stay flat (Hard Rule 8).
