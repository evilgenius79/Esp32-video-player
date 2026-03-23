#include "touch_ui.h"

// ─── Colour palette ──────────────────────────────────────────────────────────
#define COL_OVERLAY_BG   0x0000   // Black
#define COL_BTN_FILL     0x4208   // Dark grey
#define COL_BTN_BORDER   0xFFFF   // White
#define COL_BTN_ICON     0xFFFF   // White
#define COL_ARC_BG       0x2104   // Dark grey arc track
#define COL_ARC_FG       0x07FF   // Cyan progress arc
#define ALPHA_OVERLAY    128      // Unused directly; we draw semi-opaque by choice

// ─── UI geometry (for 240×240 round display) ─────────────────────────────────
#define CX              120   // screen centre x
#define CY              120   // screen centre y
#define BTN_R           36    // play/pause button radius
#define ARC_OUTER_R     115   // progress arc outer radius
#define ARC_INNER_R     108   // progress arc inner radius (ring width = 7 px)
#define ARC_START_DEG   135   // arc start angle (°, clockwise from 3-o'clock)
#define ARC_SPAN_DEG    270   // total arc span

// ─── CST816S register map ────────────────────────────────────────────────────
#define CST816S_REG_GESTURE  0x01
#define CST816S_REG_FINGERS  0x02
#define CST816S_REG_XH       0x03  // touch X high
#define CST816S_REG_XL       0x04
#define CST816S_REG_YH       0x05
#define CST816S_REG_YL       0x06

// ============================================================
//  Constructor
// ============================================================
TouchUI::TouchUI(TFT_eSPI& tft, AVIPlayer& player)
    : _tft(tft), _player(player) {}

// ============================================================
//  begin()
// ============================================================
void TouchUI::begin() {
    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    Wire.setClock(400000);
    initCST816S();
    // Touch INT pin — input with pull-up for polling
    pinMode(TOUCH_INT, INPUT_PULLUP);
}

// ============================================================
//  initCST816S() — wake the chip and set continuous reporting
// ============================================================
void TouchUI::initCST816S() {
    // Issue a reset pulse via INT pin pulse (some boards need this)
    // For most setups just writing the mode register is enough.
    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write(0xFE);   // IRQ CTL register
    Wire.write(0x01);   // Enable touch reporting
    Wire.endTransmission();
    delay(10);
}

// ============================================================
//  readTouch()
// ============================================================
TouchPoint TouchUI::readTouch() {
    TouchPoint tp;

    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write(CST816S_REG_GESTURE);
    if (Wire.endTransmission(false) != 0) return tp;

    Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, (uint8_t)6);
    if (Wire.available() < 6) return tp;

    uint8_t gesture = Wire.read();
    uint8_t fingers = Wire.read();
    uint8_t xh      = Wire.read();
    uint8_t xl      = Wire.read();
    uint8_t yh      = Wire.read();
    uint8_t yl      = Wire.read();

    tp.gesture = (Gesture)gesture;
    tp.pressed = (fingers > 0);
    tp.x       = ((uint16_t)(xh & 0x0F) << 8) | xl;
    tp.y       = ((uint16_t)(yh & 0x0F) << 8) | yl;

    return tp;
}

// ============================================================
//  update() — call from loop()
// ============================================================
void TouchUI::update() {
    TouchPoint tp = readTouch();

    if (tp.pressed || tp.gesture != Gesture::None) {
        _lastTouchMs    = millis();
        _overlayVisible = true;

        // Handle tap on play/pause button area
        if (tp.gesture == Gesture::Tap || tp.pressed) {
            int dx = (int)tp.x - CX;
            int dy = (int)tp.y - CY;
            if (dx * dx + dy * dy <= BTN_R * BTN_R) {
                // Tapped the centre button
                if (_player.state() == PlayState::Playing) {
                    _player.pause();
                } else if (_player.state() == PlayState::Paused) {
                    _player.resume();
                }
            }
        }

        // Swipe left/right could be next/prev (stubbed — extend as needed)
    }

    // Auto-hide overlay after UI_TIMEOUT_MS
    if (_overlayVisible && (millis() - _lastTouchMs > UI_TIMEOUT_MS)) {
        hideOverlay();
        _overlayVisible  = false;
        _lastPaused      = !_lastPaused; // force redraw on next show
        _lastProgress    = -1.0f;
        return;
    }

    if (_overlayVisible) {
        drawOverlay();
    }
}

// ============================================================
//  drawOverlay()
// ============================================================
void TouchUI::drawOverlay() {
    bool  paused   = (_player.state() == PlayState::Paused);
    float progress = (_player.totalFrames() > 0)
                     ? (float)_player.currentFrame() / (float)_player.totalFrames()
                     : 0.0f;
    progress = constrain(progress, 0.0f, 1.0f);

    bool needBtnRedraw = (paused != _lastPaused);
    bool needArcRedraw = (fabsf(progress - _lastProgress) > 0.003f); // ~1° threshold

    if (needBtnRedraw) {
        drawPlayPauseButton(paused);
        _lastPaused = paused;
    }

    if (needArcRedraw) {
        drawProgressArc(progress);
        _lastProgress = progress;
    }
}

// ============================================================
//  drawPlayPauseButton()  — circle with play ▶ or pause ‖ icon
// ============================================================
void TouchUI::drawPlayPauseButton(bool paused) {
    // Button background circle
    _tft.fillCircle(CX, CY, BTN_R, COL_BTN_FILL);
    _tft.drawCircle(CX, CY, BTN_R, COL_BTN_BORDER);

    if (paused) {
        // ▶ Play triangle
        int16_t x0 = CX - 10, y0 = CY - 16;
        int16_t x1 = CX - 10, y1 = CY + 16;
        int16_t x2 = CX + 18, y2 = CY;
        _tft.fillTriangle(x0, y0, x1, y1, x2, y2, COL_BTN_ICON);
    } else {
        // ‖ Pause bars
        _tft.fillRect(CX - 14, CY - 16, 10, 32, COL_BTN_ICON);
        _tft.fillRect(CX +  4, CY - 16, 10, 32, COL_BTN_ICON);
    }
}

// ============================================================
//  drawProgressArc() — swept arc around the edge of the circle
//
//  Draws the arc as a series of thick pixel segments using
//  TFT_eSPI's fillArcHelper if available, otherwise we draw
//  an approximation with line segments.
// ============================================================
void TouchUI::drawProgressArc(float progress) {
    // Draw background ring first (full arc)
    for (int deg = ARC_START_DEG; deg < ARC_START_DEG + ARC_SPAN_DEG; deg += 2) {
        float rad = deg * DEG_TO_RAD;
        int x1 = CX + (int)(ARC_INNER_R * cosf(rad));
        int y1 = CY + (int)(ARC_INNER_R * sinf(rad));
        int x2 = CX + (int)(ARC_OUTER_R * cosf(rad));
        int y2 = CY + (int)(ARC_OUTER_R * sinf(rad));
        _tft.drawLine(x1, y1, x2, y2, COL_ARC_BG);
    }

    // Draw filled progress arc
    int progressDeg = (int)(progress * ARC_SPAN_DEG);
    for (int deg = ARC_START_DEG; deg < ARC_START_DEG + progressDeg; deg += 2) {
        float rad = deg * DEG_TO_RAD;
        int x1 = CX + (int)(ARC_INNER_R * cosf(rad));
        int y1 = CY + (int)(ARC_INNER_R * sinf(rad));
        int x2 = CX + (int)(ARC_OUTER_R * cosf(rad));
        int y2 = CY + (int)(ARC_OUTER_R * sinf(rad));
        _tft.drawLine(x1, y1, x2, y2, COL_ARC_FG);
    }
}

// ============================================================
//  hideOverlay() — erase the UI elements
// ============================================================
void TouchUI::hideOverlay() {
    // Erase centre button area
    _tft.fillCircle(CX, CY, BTN_R + 1, TFT_BLACK);

    // Erase arc ring
    for (int deg = ARC_START_DEG; deg < ARC_START_DEG + ARC_SPAN_DEG; deg += 2) {
        float rad = deg * DEG_TO_RAD;
        int x1 = CX + (int)((ARC_INNER_R - 1) * cosf(rad));
        int y1 = CY + (int)((ARC_INNER_R - 1) * sinf(rad));
        int x2 = CX + (int)((ARC_OUTER_R + 1) * cosf(rad));
        int y2 = CY + (int)((ARC_OUTER_R + 1) * sinf(rad));
        _tft.drawLine(x1, y1, x2, y2, TFT_BLACK);
    }
}
