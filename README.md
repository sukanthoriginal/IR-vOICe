# IR-vOICe

Sensory substitution: an **infrared camera** feeds a live image into the
[vOICe](https://www.seeingwithsound.com/) algorithm, which turns it into a
soundscape played through headphones. The aim is to let a blind user _hear_
thermal/IR scenes.

Built on top of [seeingwithsound/raspivoice](https://github.com/seeingwithsound/raspivoice)
(P.B.L. Meijer's hificode port), with patches so it builds on modern
Raspberry Pi OS (Bookworm, 64-bit, OpenCV 4) and reads from a USB IR
camera instead of the Pi CSI camera.

## Hardware

- Raspberry Pi 4 (tested) running Pi OS Bookworm 64-bit
- USB IR camera that shows up as `/dev/video0` (Arducam IR USB module
  works — see `ir-cam.py` for a 19-line capture sanity check)
- Audio out: USB headphones, 3.5mm headphone jack, or HDMI

## Replicating from scratch (new Pi)

If the SD card dies, follow these steps in order:

### 1. Flash the OS
Use **Raspberry Pi Imager** → Raspberry Pi OS (64-bit) Bookworm.
In the imager's advanced settings (⚙️):
- Set hostname: `ir-voice-pi` (or anything you like)
- Enable SSH (password auth)
- Set username + password
- Set WiFi SSID + password

### 2. First boot — install Tailscale
```bash
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up
# Follow the auth link, sign in with ontelligency@gmail.com
```

### 3. Clone and build
```bash
cd ~/Desktop
git clone https://github.com/sukanthoriginal/IR-vOICe.git
cd IR-vOICe
./setup.sh      # installs all deps, enables SSH, builds raspivoice
```

### 4. Mac setup (one-time)
- Install **Tailscale**: https://tailscale.com/download — sign in with the same account used on the Pi
- Install **XQuartz**: https://www.xquartz.org (needed for the `-p` preview window over SSH)
- After installing XQuartz: **log out of macOS and log back in**, then open XQuartz once

### 5. Remote access from Mac
```bash
ssh -Y <user>@<tailscale-ip> "cd ~/Desktop/IR-vOICe && ./run.sh -p"
```
The preview window appears on the Mac. Audio plays on the Pi's earphones.
Find the Pi's Tailscale IP in the Tailscale app under Devices.

### Audio card reference
Run `aplay -l` on the Pi to find your card numbers. Pass with `-aN`:
```
./run.sh -a1    # e.g. HDMI
./run.sh -a2    # e.g. 3.5mm earphones
```
Card numbers can differ between Pi units — always verify with `aplay -l`.

---

## Quick start

```bash
git clone https://github.com/sukanthoriginal/IR-vOICe.git
cd IR-vOICe
./setup.sh      # one-time: deps + wiringPi + build (~5–10 min)
./run.sh        # camera -> soundscape -> earphones
```

`./run.sh` defaults to **audio card 2** (USB / earphones in our setup).
Pass a different `-a` flag to override:

```bash
./run.sh -a1    # HDMI monitor
./run.sh -a0    # whatever card 0 is on your Pi
aplay -l        # list the cards on your Pi
```

Any extra flags are forwarded to the `raspivoice` binary, e.g.:

```bash
./run.sh -p                    # preview window (needs X)
./run.sh -o /tmp/frame.wav     # dump every frame as a WAV
./run.sh -M                    # mute (useful while testing)
./run.sh --help                # full option list
```

## Verify the IR camera independently

If something feels off, sanity-check the camera by itself:

```bash
python3 ir-cam.py
```

This is a minimal V4L2 + MJPG capture loop — same settings that
`raspivoice` uses internally. If `ir-cam.py` shows a clean frame and
`./run.sh` doesn't, the problem is in audio or the soundscape pipeline,
not the camera.

## What we changed vs. upstream raspivoice

- Compiled out the Pi CSI camera path (`NO_RASPICAM` flag) — modern Pi OS
  no longer ships the Broadcom MMAL stack raspicam needs.
- Switched OpenCV includes to the OpenCV 4 layout; renamed all
  `CV_FOO_BAR` constants to their `cv::FOO_BAR` equivalents.
- USB camera open now forces the V4L2 backend and MJPG FOURCC — needed
  by the IR camera (see `ir-cam.py`).
- Rewrote `release.mak` / `release_rpi2.mak` to use `pkg-config opencv4`,
  drop hard-coded armv7 flags (so it builds on aarch64), and pull in
  only `ncurses`, `pthread`, `wiringPi`.
- Crop OV2311 hot column (x=0) in the capture loop so every downstream
  consumer sees a clean 319-wide frame. Diagnosed via `--dark_capture`.
- Suppress the upstream vOICe "scan-start click" by default (`--no_click`
  is now the default; pass `--no_click=0` to re-enable if needed).
- Dataset recording is enabled by default — every run writes paired raw
  IR + vOICe-input frames + WAV files to `$HOME/IR-vOICe-datasets/session_*`.
  Override the directory with `--record DIR`, or disable per-run with
  `--no_record`.

## Tuning the soundscape

`raspivoice --help` lists every knob. The most useful ones:

| Flag                                  | What it does                                         |
| ------------------------------------- | ---------------------------------------------------- |
| `--freq_lowest=N`                     | Bottom of the frequency sweep (default 500 Hz)       |
| `--freq_highest=N`                    | Top of the frequency sweep (default 5000 Hz)         |
| `--total_time_s=N`                    | Duration of one left-to-right sweep (default 1.05 s) |
| `--rows=N --columns=N`                | Image resolution fed to the algorithm                |
| `-E` / `--edge_detection_threshold=N` | Edge detection strength                              |
| `--foveal_mapping`                    | More resolution in the center of the image           |
| `-n` / `--negative_image`             | Invert image (useful for IR where hot = bright)      |
| `--read_frames=N`                     | Frames drained from V4L2 buffer before processing. Default is 5 — **do not lower this.** The camera buffers frames while audio plays; without draining them you get 3-4s stale-frame lag. |
| `-e N` / `--exposure=N`               | Camera exposure. See **Exposure** section below. |
| `--no_click` (default)                | Suppress the ~1 ms left-channel tick at the start of each scan. Upstream vOICe uses it as an auditory anchor; we disable it because it's distracting. |
| `--dark_capture`                      | Debug: average 30 dark frames, dump per-column brightness stats, save to `/tmp/dark_frame.png`, and exit. Cover the lens first. |
| `--record=DIR`                        | Override the dataset output directory. Recording is **on by default** to `$HOME/IR-vOICe-datasets`. Per soundscape frame: saves `raw_NNNNNN.png` (post-crop IR), `voice_NNNNNN.png` (algorithm input), and `audio_NNNNNN.wav` into `DIR/session_YYYYMMDD_HHMMSS/`. Also writes `metadata.csv` (frame, ISO timestamp, exposure, mean brightness). Writes happen on a background thread; if disk falls behind, oldest queued frames are dropped so audio never blocks. |
| `--no_record`                         | Disable dataset recording for this run. |

For IR specifically, `-n` is often what you want — thermal pictures show
heat as bright pixels, but vOICe maps brightness to loudness. Inverting
makes cold/empty scenes quiet.

## Exposure

The IR camera with a 780nm long-pass filter passes a large slice of the solar
spectrum, so sunlight near-IR is far more intense than indoor ambient — roughly
100× the energy. This means the usable exposure range is very narrow:

| Condition | Flag |
| --------- | ---- |
| Outdoors, bright sun | `-e 1` |
| Outdoors, overcast | `-e 2` to `-e 5` |
| Indoors with IR illuminator | `-e 3` to `-e 15` |
| Indoors, ambient only | `-e 15`+ |

The `-e` value maps directly to milliseconds: `-e 1` = 1 ms, `-e 15` = 15 ms.
Internally this sets `exposure_time_absolute = e × 10` (V4L2 units of 100 µs each). Going one step too high outdoors blows
the entire image to white — the window is that tight.

**Why native auto-exposure doesn't work well here:** V4L2's built-in AE has no
range limits. Indoors it cranks exposure up to 157+ (the sensor default), which
is fine indoors but instantly saturates the sensor outdoors. There is also no
standard V4L2 interface to cap the AE range on USB cameras.

**Software AE (default, `-e 0`):** When no `-e` flag is passed, the program runs
its own auto-exposure loop constrained to the range 1–15:

- After each frame, the mean pixel brightness is measured.
- If the image is too bright (mean > 148), exposure steps down by 2.
- If too dark (mean < 108), exposure steps up by 2.
- A ±20 dead-band around 128 prevents constant micro-adjustments.
- The v4l2-ctl tool is called directly (OpenCV's setter is silently ignored by
  many USB camera drivers).

This lets the camera transition between indoor and outdoor scenes without manual
intervention, while never going outside the range known to work.

The current exposure value and equivalent time are shown in the **info panel**
on the right side of the preview window (`-p` flag).

## Troubleshooting

- **`audio open error: Unknown error 524`** — wrong ALSA card. Run
  `aplay -l` and pass the right `-aN`.
- **`Could not open camera.`** — IR cam not at `/dev/video0`. Check
  `ls /dev/video*` and try `-s3`, `-s4`.
- **`Error reading frame from camera.`** — camera doesn't support
  320×240. Comment out the `cv::CAP_PROP_FRAME_WIDTH/HEIGHT` lines in
  `raspivoice/RaspiVoice.cpp` and rebuild.
- **Build fails on `pkg-config opencv4`** — your Pi OS version doesn't
  register OpenCV as `opencv4`. Try `pkg-config --list-all | grep -i
opencv` and adjust `LINUX_PACKAGES` in `release_rpi2.mak`.

## License

Upstream raspivoice is CC BY 4.0 (P.B.L. Meijer 1996; OpenCV port 2013).
Patches in this repo follow the same license.
