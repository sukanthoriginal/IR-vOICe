# Resolved Issues

Chronological log of issues encountered and fixed during development.

---

## 1. Auto-exposure disabled / manual exposure control

**Problem:** The OV2311 camera's native V4L2 auto-exposure has no range limits.
Indoors it cranks exposure up to 157+ (the sensor default), which is fine
indoors but instantly saturates the sensor outdoors under near-IR sunlight.
There is no standard V4L2 interface to cap the AE range on USB cameras.

**Root cause:** Near-IR sunlight (passed by the 780nm LP filter) is ~100x more
intense than indoor ambient. The usable exposure range is extremely narrow
(1-15 in our V4L2 units).

**Fix:** Disable native auto-exposure via `v4l2-ctl --set-ctrl=auto_exposure=1`
(manual mode). Implemented in `initUsbCam()` in `RaspiVoice.cpp`.

**Why v4l2-ctl instead of OpenCV:** OpenCV's `CAP_PROP_AUTO_EXPOSURE` setter is
silently ignored by many USB camera drivers. The command-line tool reliably
controls the hardware register.

**Commits:** `cc91cae` (disable AE via v4l2-ctl)

---

## 2. Software auto-exposure (constrained range)

**Problem:** With no auto-exposure, transitioning between indoor and outdoor
scenes requires manually changing the `-e` flag.

**Fix:** When no `-e` flag is passed (exposure=0), the program runs its own AE
loop constrained to range 1-15:
- After each frame, mean pixel brightness is measured.
- If too bright (mean > 148), exposure steps down by 2.
- If too dark (mean < 108), exposure steps up by 2.
- A +/-20 dead-band around 128 prevents constant hunting.
- v4l2-ctl is called directly to set the exposure.
- Initial value: 8 (mid-range).

**Why step by 2:** Step-by-1 converged too slowly for the user. Step-by-2 is
fast enough without overshooting (exposure range is only 1-15).

**Location:** `applySoftwareAE()` in `RaspiVoice.cpp`, member variables
`softwareAE_exposure_` and `softwareAE_camId_` in `RaspiVoice.h`.

**Commits:** `da77865` (software AE), `ba79d78` (speed up convergence to step 2)

---

## 3. Exposure info panel in preview window

**Problem:** No way to see the current exposure value while running.

**Fix:** Added a 260px info panel to the right of the preview window showing
`e = N` and the equivalent `N ms` value. Uses `cv::hconcat` to append the panel
to the side-by-side (Raw IR | vOICe input) layout.

**First attempt (failed):** Used `cv::vconcat` to add a bar below the frame.
This changed the image height each frame, causing assertion failures. Switched
to fixed-width right-side `hconcat` panel.

**Second bug:** `PlayFrame()` splits `lastPreviewFrame_` into left/right panels.
After adding the 260px info panel, it was slicing at `cols/2` (wrong). Fixed by
computing `target_w = (lastPreviewFrame_.cols - kInfoW) / 2`.

**Commits:** `1b15852` (info bar), `aaa79f4` (fix to hconcat), `a0feb08` (fix
PlayFrame crash)

---

## 4. Exposure millisecond label wrong

**Problem:** The info panel showed `150 ms` for `e=15`. Should show `15 ms`.

**Root cause:** Code displayed `expVal * 10` instead of `expVal`. The ×10
conversion is for V4L2 units (exposure_time_absolute), not for display.
The mapping: `-e 15` -> `exposure_time_absolute=150` -> `150 * 100us = 15ms`.

**Fix:** Display `expVal` directly as the ms value.

**Commit:** `71acaee`

---

## 5. Hot column (persistent bright vertical line at x=0)

**Problem:** A single bright vertical line visible at the leftmost edge of the
frame, persistent even in complete darkness (lens covered). Appears in both
the Raw IR and vOICe input preview panels.

**Diagnosis:** Created `--dark_capture` debug mode that:
1. Captures 30 averaged frames with lens covered at fixed exposure.
2. Saves averaged frame to `/tmp/dark_frame.png`.
3. Prints per-column mean brightness for all 320 columns.

Results: Col 0 mean = 69.5, Col 1 mean = 37.2, global mean = 27.1.
Col 0 is a genuine OV2311 sensor defect (Fixed Pattern Noise / hot column).

**First fix attempts (failed):** Three commits tried cropping inside
`processImage()`:
```cpp
cv::Mat processedImage = rawImage(cv::Rect(1, 0, rawImage.cols - 1, rawImage.rows));
```
This only cropped `processedImage`, but:
- The "Raw IR" preview panel still drew from the uncropped `rawImage`.
- `PlayFrame()` re-grabs `latestRawFrame_` which was never cropped.
- Visually nothing changed (though the soundscape WAS actually fixed).

All three attempts reverted in `cf0fa1b`.

**Correct fix:** Crop in `captureLoop()` at the source, before the frame is
published to any consumer:
```cpp
cv::Mat cropped = frame(cv::Rect(1, 0, frame.cols - 1, frame.rows)).clone();
rv->latestRawFrame_ = cropped;
```
Now every downstream path (raw preview, processImage, PlayFrame refresh) sees
the cleaned 319-wide frame. `.clone()` ensures contiguous memory across the
mutex boundary.

**Commits:** `d031502` (dark_capture debug mode), `7f6b150` (source-level crop),
merged to main in `13b08e1`

---

## 6. Audio click/tick every 1.05 seconds

**Problem:** An annoying click/tick sound at the start of every soundscape frame
(every ~1.05s). Persists regardless of what the camera sees.

**Misdiagnosis (reverted):** Initially suspected ALSA device-reopen click from
spawning a new `aplay` process every frame. Implemented persistent `aplay`
subprocess via `popen()` streaming raw PCM. This did NOT fix the issue because
the click was embedded in the audio data itself. Reverted in `d7ee62d`.

**Root cause:** P.B.L. Meijer's original hificode deliberately injects a ~1ms
burst of white noise into the LEFT channel at sample 0 of every frame
(`ImageToSoundscape.cpp:245`):
```cpp
if (sample < sampleCount / (5 * columns))
{
    sl = (2.0*rnd() - 1.0) / scale;   // Left "click"
}
```
The comment literally says "click". It's the upstream vOICe convention for
"scan starts now" — an auditory anchor for the left-to-right sweep. The Windows
vOICe app exposes it as a checkbox.

**Fix:** Added `--no_click` flag, gating the noise burst:
```cpp
if (!no_click && sample < sampleCount / (5 * columns))
```
Plumbing: `Options.h` -> `Options.cpp` -> `RaspiVoice.cpp` constructor ->
`ImageToSoundscapeConverter` constructor -> `processStereo()`.

Default: suppressed (`opt.no_click = true`). The upstream behavior is
preserved if anyone passes `--no_click=0` in the future.

**Commits:** `944d32d` (add flag), `5312c49` (make default)

---

## 7. Additional dark-frame observations (informational, not bugs)

The dark capture also revealed:
- **Cols 1-29 and 301-319:** Smooth gradient (FPN edge falloff), normal for
  CMOS sensors. Not a defect — just fixed-pattern noise at the edges.
- **8-pixel plateaus** across the frame (e.g., cols 88-95 all identical): This
  is the MJPG 8x8 DCT block boundary signature. The "faint vertical grid" seen
  in the preview is compression artifact, not sensor defect. Would require
  switching to YUYV codec or dark-frame subtraction to fully eliminate.

---

## Build / environment issues encountered

| Error | Cause | Fix |
|-------|-------|-----|
| `Unable to locate package v4l2-utils` | Package is `v4l-utils` (no `2`) | `sudo apt install v4l-utils` |
| `make: *** No targets. Stop.` | Ran `make -f release_rpi2.mak` directly | Use `make CONFIG=release_rpi2` |
| `make: Nothing to be done for 'all'.` | Source not updated on Pi | Push to GitHub, pull on Pi |
| `sudo: a terminal is required` | SSH without `-t` flag | Use `ssh -t` for commands needing sudo |
| `hconcat` assertion failure | Mismatched image dimensions | Fixed panel sizing logic |
| Pi branch tracking deleted remote | Deleted `fix/hot-column-debug` on remote while Pi was still on it | `git checkout main && git branch -D fix/hot-column-debug && git pull` |
