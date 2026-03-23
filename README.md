# ESP32 Video Player

MJPEG video player for the **Seeed Studio XIAO ESP32S3** with the **Seeed Round Display for XIAO** (240×240 GC9A01) and a **MAX98357A** I2S audio amplifier.

## Features

- Plays MJPEG-in-AVI files from a FAT32 SD card
- Synchronized PCM audio via I2S / MAX98357A
- Dual-core: video decode on Core 1, system + UI on Core 0
- JPEG decoding via [JPEGDEC](https://github.com/bitbank2/JPEGDEC) with DMA pixel push
- Capacitive touch (CST816S) to show/hide UI overlay
- Play / Pause button and arc progress indicator on the round display
- Automatic playlist — loops through all `.avi` files in SD root
- FFmpeg conversion script included (`tools/convert_video.sh`)

---

## Hardware

| Component | Notes |
|---|---|
| Seeed XIAO ESP32S3 | 8 MB PSRAM, 16 MB flash |
| Seeed Round Display for XIAO | GC9A01 240×240, CST816S touch, SD card slot |
| MAX98357A breakout | I2S amplifier + speaker |
| Speaker (4Ω or 8Ω) | Any small speaker |
| MicroSD card | FAT32, ≤32 GB |

---

## Pin Map

### Round Display (connects automatically via XIAO header)

| Signal | XIAO pin | GPIO |
|---|---|---|
| SPI MOSI | D10 | 9 |
| SPI SCK | D8 | 7 |
| SPI MISO | D9 | 8 |
| Display CS | D1 | 2 |
| Display DC | D3 | 4 |
| Display RST | — | –1 (tied high on board) |
| SD card CS | D2 | 3 |
| Touch SDA | D4 | 5 |
| Touch SCL | D5 | 6 |
| Touch INT | D7 | 44 |

### MAX98357A (wire separately with jumper wires)

| MAX98357A pin | XIAO pin | GPIO |
|---|---|---|
| BCLK | D6 | 43 |
| LRC (LRCLK) | D7 | 44 |
| DIN | D0 | 1 |
| GND | GND | — |
| VIN | 3V3 | — |
| SD_MODE | 3V3 | — (always enabled) |
| GAIN | leave floating | — (+15 dB; tie to GND for +3 dB) |

> **Note:** D7 (GPIO 44) is used for both touch INT and I2S LRCLK.
> Touch INT is polled, not interrupt-driven, so sharing this pin is safe.
> Verify these assignments match your actual hardware before flashing.

---

## Software Setup

### 1. Install PlatformIO

Install the [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode) for VS Code,
or use the [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation/index.html).

### 2. Open the project

```bash
cd Esp32-video-player
pio run          # build
pio run -t upload  # build + flash
pio device monitor # open serial monitor
```

Or open the folder in VS Code — PlatformIO will detect `platformio.ini` automatically.

### 3. TFT_eSPI configuration

`include/User_Setup.h` is picked up automatically via the `build_flags` in `platformio.ini`:

```ini
-DUSER_SETUP_LOADED
'-DUSER_SETUP_FILE="User_Setup.h"'
```

No manual file copying needed — the build system handles it.

### 4. Build environments

| Environment | Use case |
|---|---|
| `xiao_esp32s3` | Normal (default) |
| `xiao_esp32s3_debug` | Verbose logging, debug symbols |

Switch with `pio run -e xiao_esp32s3_debug`.

---

## Preparing Videos

Install [ffmpeg](https://ffmpeg.org/), then run the conversion script:

```bash
chmod +x tools/convert_video.sh
./tools/convert_video.sh my_video.mp4
```

This produces `my_video.avi` — copy it to the **root** of your FAT32 SD card.

### Manual ffmpeg command

```bash
ffmpeg -i input.mp4 \
  -vf "scale=240:240:force_original_aspect_ratio=increase,crop=240:240" \
  -r 15 \
  -vcodec mjpeg -q:v 5 -pix_fmt yuvj420p -huffman 0 \
  -acodec pcm_s16le -ar 22050 -ac 1 \
  output.avi
```

### Recommended settings

| Setting | Value | Notes |
|---|---|---|
| Resolution | 240×240 | Matches round display |
| Frame rate | 15 fps | Safe target; try 24 fps if smooth |
| JPEG quality | 4–6 | Lower = larger file but better image |
| Audio | PCM 16-bit, mono, 22050 Hz | Small + fast |

---

## Project Structure

```
platformio.ini          — Board, libs, build flags
src/
├── main.cpp            — setup(), loop(), playlist management
├── avi_player.cpp      — AVI parser, MJPEG decoder, I2S audio
└── touch_ui.cpp        — CST816S touch reading + UI overlay drawing
include/
├── config.h            — All pin definitions and tunable settings
├── avi_player.h
├── touch_ui.h
└── User_Setup.h        — TFT_eSPI driver config (GC9A01 + XIAO ESP32S3)
tools/
└── convert_video.sh    — FFmpeg conversion helper script
```

---

## How It Works

1. **SD scan** — on boot, lists all `.avi` files in the SD root.
2. **AVI parse** — reads RIFF/AVI headers to find frame count, FPS, audio format, and the `movi` section offset.
3. **Video task** (Core 1) — reads `00dc` MJPEG chunks, decodes with JPEGDEC (128×16 pixel blocks pushed via DMA), throttles to target FPS.
4. **Audio** — `01wb` PCM chunks are written directly to I2S DMA buffers, which play continuously in hardware.
5. **UI** (main loop, Core 0) — polls CST816S for touch; shows play/pause button and arc progress bar for `UI_TIMEOUT_MS` after each touch.

---

## Tuning

All adjustable values are in `config.h`:

| Define | Default | Effect |
|---|---|---|
| `MAX_JPEG_SIZE` | 50 KB | Increase for higher quality JPEG frames |
| `AUDIO_DMA_BUFS` | 8 | More = smoother audio but more RAM |
| `AUDIO_DMA_LEN` | 512 | I2S DMA buffer length in samples |
| `UI_TIMEOUT_MS` | 3000 | Hide controls after N ms of no touch |

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Display blank / wrong colours | Wrong TFT_eSPI pin config | Verify `User_Setup.h` pins match your board |
| "SD card not found" | Wrong SD_CS pin or bad card | Check SD_CS_PIN in config.h; reformat to FAT32 |
| "No .avi files found" | Wrong format or path | Re-run convert script; check file is in SD root |
| Audio crackling | DMA buffers too small | Increase AUDIO_DMA_BUFS or AUDIO_DMA_LEN |
| Video stutters | SD read slow or FPS too high | Lower FPS in convert script; use Class 10 card |
| Wrong I2S pins | Wiring mismatch | Adjust I2S_BCLK/LRCLK/DOUT in config.h |
| Touch not responding | I2C address wrong | Try 0x14 instead of 0x15 in TOUCH_I2C_ADDR |
