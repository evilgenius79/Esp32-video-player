#pragma once

// ============================================================
//  Pin Configuration — Seeed XIAO ESP32S3 + Round Display
// ============================================================
//
//  Round Display SPI bus (shared by display & SD card):
//    D8  = GPIO 7  → SCK  (TFT_SCLK / SD CLK)
//    D9  = GPIO 8  → MISO (SD MISO; display write-only)
//    D10 = GPIO 9  → MOSI (TFT_MOSI / SD MOSI)
//
//  Display control (GC9A01):
//    D1  = GPIO 2  → TFT_CS
//    D3  = GPIO 4  → TFT_DC
//    RST handled by library (-1 or tied high)
//    Backlight always on (or control via software on D6/GPIO43)
//
//  SD card:
//    D2  = GPIO 3  → SD_CS
//
//  Touch (CST816S) — I2C:
//    D4  = GPIO 5  → SDA
//    D5  = GPIO 6  → SCL
//    D7  = GPIO 44 → INT  (used for polling in this project)
//
//  I2S Audio (MAX98357A) — wire externally:
//    D6  = GPIO 43 → BCLK  (repurposed; disable BL control if needed)
//    D7  = GPIO 44 → LRCLK (repurposed from touch INT — use polling)
//    D0  = GPIO 1  → DOUT
//
//  !! Adjust the I2S pins below to match your actual wiring. !!
// ============================================================

// ─── Display ────────────────────────────────────────────────
// NOTE: TFT_eSPI pin config lives in User_Setup.h
#define TFT_BL_PIN      (-1)   // -1 = always on; set to 43 for PWM control

// ─── SD Card ────────────────────────────────────────────────
#define SD_CS_PIN       3      // D2

// ─── Touch (CST816S, I2C) ───────────────────────────────────
#define TOUCH_SDA       5      // D4
#define TOUCH_SCL       6      // D5
#define TOUCH_INT       44     // D7  (polled, not interrupt-driven)
#define TOUCH_I2C_ADDR  0x15

// ─── I2S Audio (MAX98357A) ──────────────────────────────────
#define I2S_BCLK        43     // D6
#define I2S_LRCLK       44     // D7  — only free if touch INT not used as IRQ
#define I2S_DOUT        1      // D0
#define I2S_PORT        I2S_NUM_0

// ─── Screen ─────────────────────────────────────────────────
#define SCREEN_W        240
#define SCREEN_H        240

// ─── Video playback ─────────────────────────────────────────
#define VIDEO_FOLDER    "/"           // Root of SD card
#define VIDEO_EXT       ".avi"        // Must be MJPEG+PCM AVI (see convert script)
#define MAX_JPEG_SIZE   (50 * 1024)   // 50 KB — generous per-frame budget
#define AUDIO_DMA_BUFS  8             // I2S DMA buffer count
#define AUDIO_DMA_LEN   512           // I2S DMA buffer length (samples)

// ─── UI ─────────────────────────────────────────────────────
#define UI_TIMEOUT_MS   3000          // Hide controls after this idle time

// ─── FreeRTOS ───────────────────────────────────────────────
#define VIDEO_TASK_CORE      1
#define VIDEO_TASK_PRIORITY  5
#define VIDEO_STACK_SIZE     8192
