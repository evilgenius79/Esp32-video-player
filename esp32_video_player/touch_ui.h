#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "avi_player.h"

// CST816S gesture codes
enum class Gesture : uint8_t {
    None      = 0x00,
    SwipeUp   = 0x01,
    SwipeDown = 0x02,
    SwipeLeft = 0x03,
    SwipeRight= 0x04,
    Tap       = 0x05,
    LongPress = 0x0B,
};

struct TouchPoint {
    uint16_t x = 0;
    uint16_t y = 0;
    bool     pressed = false;
    Gesture  gesture = Gesture::None;
};

// ============================================================
//  TouchUI — reads CST816S touch and draws a round-display UI
// ============================================================
class TouchUI {
public:
    TouchUI(TFT_eSPI& tft, AVIPlayer& player);

    // Call once in setup()
    void begin();

    // Call from loop() — reads touch, updates overlay visibility, draws UI
    void update();

private:
    TFT_eSPI&  _tft;
    AVIPlayer& _player;

    bool     _overlayVisible = false;
    uint32_t _lastTouchMs   = 0;

    // CST816S
    TouchPoint readTouch();
    void       initCST816S();

    // Drawing
    void drawOverlay();
    void drawPlayPauseButton(bool paused);
    void drawProgressArc(float progress);
    void hideOverlay();

    // Previous state for redraw optimization
    bool     _lastPaused   = false;
    float    _lastProgress = -1.0f;
};
