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
- Set hostname: `sukanth-raspberry-pi`
- Enable SSH (password auth)
- Set username: `sukanth` + your password
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

### 4. Remote access from Mac
Ensure Tailscale is running on the Mac, then:
```bash
ssh -Y sukanth@sukanth-raspberry-pi "cd ~/Desktop/IR-vOICe && ./run.sh -p"
```

### Audio card reference (this Pi)
```
-a1  →  HDMI monitor
-a2  →  3.5mm earphones  (default in run.sh)
```
Run `aplay -l` on a new Pi to confirm card numbers — they may differ.

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

For IR specifically, `-n` is often what you want — thermal pictures show
heat as bright pixels, but vOICe maps brightness to loudness. Inverting
makes cold/empty scenes quiet.

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
