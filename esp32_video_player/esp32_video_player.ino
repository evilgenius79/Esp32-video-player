/*
 *  ESP32 Video Player
 *  ─────────────────
 *  Hardware : Seeed Studio XIAO ESP32S3
 *             Seeed Studio Round Display for XIAO (GC9A01, 240×240)
 *             MAX98357A I2S audio amplifier
 *
 *  Libraries required (install via Arduino Library Manager):
 *    • TFT_eSPI       by Bodmer
 *    • JPEGDEC        by bitbank2
 *    • SD             (built-in)
 *    • Wire           (built-in)
 *
 *  See README.md for wiring, pin configuration, and video conversion.
 */

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <TFT_eSPI.h>

#include "config.h"
#include "avi_player.h"
#include "touch_ui.h"

// ─── Globals ─────────────────────────────────────────────────────────────────
TFT_eSPI  tft;
AVIPlayer player(tft);
TouchUI   ui(tft, player);

// File list from SD card
static constexpr int MAX_VIDEO_FILES = 20;
static char videoFiles[MAX_VIDEO_FILES][64];
static int  videoCount   = 0;
static int  currentVideo = 0;

// ─── Forward declarations ─────────────────────────────────────────────────────
void showSplash(const char* msg);
void loadNextVideo(int direction);

// =============================================================================
//  setup()
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[Boot] ESP32 Video Player");
    Serial.printf("[Boot] PSRAM: %u bytes\n", ESP.getPsramSize());

    // ── Backlight ──
    if (TFT_BL_PIN >= 0) {
        pinMode(TFT_BL_PIN, OUTPUT);
        digitalWrite(TFT_BL_PIN, HIGH);
    }

    // ── Display init ──
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    showSplash("Initialising...");

    // ── SPI bus (shared display + SD card) ──
    //    TFT_eSPI manages display SPI.  We point the SD library at the same bus.
    SPIClass* hspi = new SPIClass(HSPI);
    hspi->begin(TFT_SCLK, 8 /*MISO=D9/GPIO8*/, TFT_MOSI, SD_CS_PIN);

    // ── SD card ──
    showSplash("Mounting SD...");
    if (!SD.begin(SD_CS_PIN, *hspi, 20000000)) {
        showSplash("SD card not found!\nCheck wiring & card.");
        Serial.println("[SD] Mount failed");
        while (true) delay(1000);
    }
    Serial.printf("[SD] Mounted — %llu MB\n", SD.totalBytes() / (1024 * 1024));

    // ── Scan for video files ──
    videoCount = AVIPlayer::scanFiles(VIDEO_FOLDER, videoFiles, MAX_VIDEO_FILES);
    if (videoCount == 0) {
        showSplash("No .avi files found!\nCopy videos to SD root.");
        Serial.println("[SD] No AVI files found");
        while (true) delay(1000);
    }
    Serial.printf("[SD] Found %d video(s)\n", videoCount);
    for (int i = 0; i < videoCount; i++) {
        Serial.printf("  [%d] %s\n", i, videoFiles[i]);
    }

    // ── Touch UI ──
    ui.begin();

    // ── Start first video ──
    tft.fillScreen(TFT_BLACK);
    loadNextVideo(0);
}

// =============================================================================
//  loop()
// =============================================================================
void loop() {
    // Touch + UI runs on the main core (Core 1 alongside the video task).
    ui.update();

    // Auto-advance when video stops (end of file → AVIPlayer loops internally,
    // but if you want playlist advance, monitor state here).
    // To enable playlist mode, call player.stop() at end-of-file instead of
    // looping in runTask(), then detect Stopped here.

    delay(20);   // ~50 Hz UI poll rate
}

// =============================================================================
//  showSplash() — simple centred message on black background
// =============================================================================
void showSplash(const char* msg) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);  // middle-centre
    tft.drawString(msg, SCREEN_W / 2, SCREEN_H / 2, 2);
}

// =============================================================================
//  loadNextVideo() — direction: 0=current, +1=next, -1=prev
// =============================================================================
void loadNextVideo(int direction) {
    if (videoCount == 0) return;

    player.stop();
    delay(100);  // allow task to exit

    currentVideo = (currentVideo + direction + videoCount) % videoCount;

    Serial.printf("[Play] %s\n", videoFiles[currentVideo]);

    if (!player.open(videoFiles[currentVideo])) {
        showSplash("Failed to open video");
        delay(2000);
        return;
    }

    aviPlayerStartTask(&player);
}
