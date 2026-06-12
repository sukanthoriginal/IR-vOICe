# IR-vOICe — Progress Log

A complete, narrative log of what's been built so far on top of the
upstream `seeingwithsound/raspivoice` codebase.

For the issue-by-issue debugging log (problems, root causes, fixes),
see [`issues.md`](./issues.md). This file is the bigger-picture story.

---

## The end goal

Sensory substitution for a blind user via thermal/IR vision. A USB
infrared camera (Arducam B0322 OV2311 with 780 nm long-pass filter)
feeds frames to P.B.L. Meijer's vOICe algorithm, which converts each
frame into a 1.05 s stereo soundscape — vertical = pitch, horizontal =
time (left-to-right scan), brightness = loudness. Audio plays through
earphones.

**Hardware stack:**
- Raspberry Pi 4 (Bookworm 64-bit, aarch64)
- Arducam OV2311 USB IR camera (`/dev/video0`, MJPG @ 320×240)
- 780 nm long-pass filter
- USB / 3.5 mm earphones for audio

**Software stack:**
- Forked from `seeingwithsound/raspivoice` (Meijer's hificode OpenCV
  port, last upstream update Dec 2014)
- Patched for modern Pi OS Bookworm + OpenCV 4 + aarch64
- USB camera path replaces the deprecated Pi CSI / raspicam path

---

## Milestone 1 — Get it building on modern Pi OS

Upstream raspivoice hasn't been touched since 2014 and assumes the
Broadcom MMAL stack + OpenCV 2 + armv7. None of that is true on
Bookworm 64-bit. To make it build:

- Compiled out the Pi CSI camera path via `-DNO_RASPICAM`.
- Rewrote `release.mak` / `release_rpi2.mak` to use
  `pkg-config opencv4`, dropped hard-coded `-march=armv7-a` so it builds
  on aarch64, and trimmed the link line to `ncurses`, `pthread`,
  `wiringPi`.
- Switched OpenCV includes to the OpenCV 4 layout — pulled in the
  targeted headers (`opencv2/core.hpp`, `imgproc.hpp`, `imgcodecs.hpp`,
  `highgui.hpp`, `videoio.hpp`, `calib3d.hpp`) instead of the umbrella
  `opencv.hpp` (which drags in `stitching.hpp` and collides with
  ncurses' `#define OK 0`).
- Renamed every `CV_FOO_BAR` constant to its `cv::FOO_BAR` equivalent.
- USB camera open now forces `cv::CAP_V4L2` backend and MJPG FOURCC —
  the IR camera doesn't respond to V4L2 default backend (mirrors what
  `ir-cam.py` does as a 19-line sanity check).

Outcome: `./setup.sh` clones, installs deps, builds wiringPi, and
produces a working `Release/raspivoice` in ~5–10 min.

---

## Milestone 2 — Tame the exposure (outdoors vs indoors)

The OV2311 + 780 nm LP filter passes a huge slice of solar near-IR.
Result: outdoors is roughly **100× brighter** than indoors at the
sensor. The native V4L2 auto-exposure has no range cap — indoors it
cranks `exposure_time_absolute` to 157 (≈ 15.7 ms) which works fine
indoors but instantly saturates the sensor outdoors. There is no
standard V4L2 interface to bound the AE range on USB cameras.

**Manual mode:**
Disabled native AE via `v4l2-ctl --set-ctrl=auto_exposure=1`. OpenCV's
`CAP_PROP_AUTO_EXPOSURE` setter is silently ignored by many USB drivers,
so we shell out to `v4l2-ctl` directly.

**Software auto-exposure (the default when `-e 0`):**
Run our own AE loop bounded to the useful range 1–15:
- Measure mean pixel brightness each frame.
- If too bright (mean > 148), drop exposure by 2.
- If too dark (mean < 108), raise exposure by 2.
- ±20 dead-band around 128 prevents hunting.
- Step-by-2 (not 1) converges fast enough without overshooting.

**Exposure mapping (for the user's mental model):**

| `-e` value | exposure_time_absolute | Real time |
|------------|------------------------|-----------|
| `-e 1`     | 10                     | 1 ms      |
| `-e 15`    | 150                    | 15 ms     |

So `-e` = direct milliseconds. The window is genuinely tight: one step
too high outdoors blows the whole frame to white.

---

## Milestone 3 — Live preview with exposure visibility

`-p` opens a side-by-side preview window: **Raw IR** (left, what the
camera sees) and **vOICe input** (right, the small resized image
actually fed to the algorithm). A 260 px info panel on the right shows
the current `e =` value and equivalent `ms`.

Background capture thread keeps `latestRawFrame_` fresh at camera FPS,
so the raw preview panel updates at ~30 Hz even while audio plays. The
vOICe-input panel and info panel get re-blitted from the last processed
frame.

Earlier attempts used `cv::vconcat` to add a bar below the frame — that
changed image height per frame and tripped assertion failures in
`hconcat`. Settled on a fixed-width right-side info panel via
`hconcat`. After adding the 260 px info column, `PlayFrame()`'s
left/right panel split needed `target_w = (cols - kInfoW) / 2` instead
of `cols / 2`.

---

## Milestone 4 — Hot column (sensor defect at x=0)

A persistent bright vertical line appeared at the leftmost edge of every
frame — visible even with the lens covered.

**Diagnosis (`--dark_capture` mode):**
Cover the lens, capture 30 averaged frames at a fixed exposure, dump
per-column mean brightness for all 320 columns to stdout, save the
averaged frame to `/tmp/dark_frame.png`, and exit.

Result: col 0 mean = 69.5, col 1 mean = 37.2, global mean = 27.1. Col 0
is a genuine OV2311 sensor defect — Fixed Pattern Noise / hot column.

**False starts:** Three commits tried cropping inside `processImage()`:

```cpp
cv::Mat processedImage = rawImage(cv::Rect(1, 0, rawImage.cols - 1, rawImage.rows));
```

This fixed the soundscape but not the visible preview — because the
"Raw IR" preview panel still drew from the uncropped `rawImage`, and
`PlayFrame()`'s refresh loop pulled directly from `latestRawFrame_`
which was never cropped. All three attempts reverted.

**Correct fix:** Crop in `captureLoop()` at the source. Every
downstream consumer (raw preview, processImage, PlayFrame refresh)
sees the cleaned 319-wide frame:

```cpp
cv::Mat cropped = (frame.cols > 1)
    ? frame(cv::Rect(1, 0, frame.cols - 1, frame.rows)).clone()
    : frame.clone();
```

`.clone()` ensures contiguous memory across the mutex boundary.

**Bonus observations from dark frames:**
- Cols 1–29 and 301–319 show a smooth gradient — normal CMOS edge FPN.
- 8-pixel plateaus across the frame (e.g. cols 88–95 all identical) are
  the MJPG 8×8 DCT block-boundary signature. The faint vertical grid
  seen in preview is JPEG compression artifact, not sensor defect.
  Eliminating it would require switching to YUYV or doing dark-frame
  subtraction.

---

## Milestone 5 — Kill the per-frame "click"

Every 1.05 s, at the start of each soundscape frame, an annoying
click/tick was audible regardless of camera input.

**False start:** Suspected ALSA device-reopen click from re-spawning
`aplay` every frame. Implemented a persistent `aplay` subprocess via
`popen()` streaming raw PCM. Did **not** fix the click. Reverted —
the click was in the audio data itself.

**Root cause:** P.B.L. Meijer's hificode deliberately injects a ~1 ms
burst of white noise into the **left channel** at the start of every
frame (`ImageToSoundscape.cpp:245`):

```cpp
if (sample < sampleCount / (5 * columns))
{
    sl = (2.0*rnd() - 1.0) / scale;   // Left "click"
}
```

The comment literally says "click". It's an auditory **scan-start
anchor** — the Windows vOICe app exposes it as a checkbox. For our
user it was just distracting.

**Fix:** Added `--no_click` flag, defaulted to ON. Upstream behavior is
preserved if anyone passes `--no_click=0`.

---

## Milestone 6 — Dataset recording

Goal: capture everything needed for offline analysis of what the user
hears in any given moment. Paired files per soundscape frame:

```
$HOME/IR-vOICe-datasets/session_YYYYMMDD_HHMMSS/
    raw_000001.png      (319×240 grayscale, post hot-column crop)
    voice_000001.png    (vOICe algorithm input, e.g. 176×64)
    audio_000001.wav    (16-bit PCM stereo @ 48 kHz, ~1.05 s)
    raw_000002.png ...
    metadata.csv        (frame, iso_timestamp, exposure, mean_brightness)
```

**Architecture:**
- New `DatasetRecorder` class owns a bounded queue (max 8 frames) +
  background pthread that does the actual disk I/O.
- `RaspiVoice::GrabAndProcessFrame()` enqueues each frame right after
  `Process()` runs, while the audio buffer is fresh. Mats are cloned,
  audio samples are copied — the audio thread never blocks on disk.
- If the writer can't keep up, the queue drops the **oldest** frame
  (not the newest) — that way the most recent capture is always
  preserved.
- Hand-rolled WAV writer in the background thread (rather than
  borrowing `AudioData::SaveToWavFile`) so the audio buffer can be
  serialized at leisure without racing the live one.

**Defaults:**
- Recording is **ON** by default — every run produces a session.
- Default path: `$HOME/IR-vOICe-datasets/`.
- `--no_record` disables for a run.
- `--record=DIR` overrides the location.

**Throughput (~1 Hz, one frame per soundscape):**
- ~30–50 ms PNG encode on Pi 4 (raw) + small voice PNG + 384 KB WAV.
- Roughly **~1 GB/hour** of recording — easy fit on the SD card.

**Verification:** Pulled session from Pi via rsync, opened raw + voice
PNGs in Preview, played WAVs in QuickTime. All three line up frame by
frame.

---

## Quality-of-life additions

- **`run.sh`** — wraps the binary with the right `-s2 -v -a2
  --read_frames=5` defaults so day-to-day use is just `./run.sh`.
- **`setup.sh`** — one-time deps install + wiringPi build + raspivoice
  build for a fresh SD card.
- **Replicating from scratch (in README)** — full recovery procedure if
  the SD card dies: Pi Imager → Tailscale → clone → setup → Mac SSH +
  XQuartz for remote preview.
- **`ir-cam.py`** — 19-line V4L2 + MJPG capture loop. If something
  feels off, run this first to isolate camera vs audio vs algorithm.

---

## Current state of the binary's flags

The flags that matter most day-to-day:

| Flag | What it does |
|------|--------------|
| `-p` | Preview window (Raw IR \| vOICe input \| exposure info panel) |
| `-aN` | Audio card number (default 2 = USB earphones on the Pi) |
| `-e N` | Manual exposure 1–15 (= ms). Use `-e 0` for software AE. |
| `-n` | Negative image (good for IR — hot = bright, vOICe maps bright = loud) |
| `--no_click` | Suppress upstream scan-start tick (default ON) |
| `--no_record` | Disable dataset recording for this run |
| `--record=DIR` | Override dataset output directory |
| `--dark_capture` | Debug: average 30 dark frames + per-column stats, exit |

---

## What's not solved yet (parking lot)

- **MJPG block-boundary grid** — the faint 8-pixel vertical grid is
  JPEG DCT artifact. Switching the camera to YUYV would eliminate it
  but costs bandwidth. Not worth fixing until it actually bothers the
  listener.
- **Edge FPN gradient (cols 1–29, 301–319)** — present but mild.
  Dark-frame subtraction would fix it cleanly. Not currently
  prioritized.
- **Batch cloud upload** — datasets currently stay on the Pi.
  Planned: a separate script to rsync sessions to GCS and prune local
  copies. Not built yet.
- **Native auto-exposure cap** — V4L2 doesn't expose an interface to
  bound the AE range on USB cameras. Software AE is the workaround;
  doing it in the kernel/driver would be cleaner but is a much bigger
  project.

---

## Commit timeline (highest-signal commits only)

| Commit  | What changed |
|---------|--------------|
| `cc91cae` | Disable native AE via v4l2-ctl |
| `da77865` | Software AE loop (range-bounded) |
| `ba79d78` | Software AE step size 2 (faster convergence) |
| `1b15852` | Exposure info bar in preview |
| `aaa79f4` | Info panel as right-side hconcat (fix hconcat asserts) |
| `a0feb08` | Fix PlayFrame split arithmetic for info-panel width |
| `71acaee` | Display correct ms label (was 10×) |
| `d031502` | `--dark_capture` debug mode |
| `7f6b150` | Source-level hot-column crop in captureLoop |
| `13b08e1` | Merge hot-column fix into main |
| `944d32d` | `--no_click` flag |
| `5312c49` | Default `--no_click` to ON |
| `1ddcc41` | README: hot-column, --no_click, --dark_capture |
| `5b694d9` | issues.md log |
| `2b171a7` | Dataset recording (paired raw + voice + WAV per frame) |

For the granular blow-by-blow including reverted attempts and dead
ends, see [`issues.md`](./issues.md).
