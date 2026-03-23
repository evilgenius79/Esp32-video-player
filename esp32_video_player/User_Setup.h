// ============================================================
//  TFT_eSPI User Setup — GC9A01 on Seeed XIAO ESP32S3
//
//  Place this file in the TFT_eSPI library folder
//  (Arduino/libraries/TFT_eSPI/User_Setup.h)
//  replacing the existing one, OR copy it there from the sketch.
// ============================================================

#define USER_SETUP_INFO "GC9A01 XIAO ESP32S3"

// ─── Driver ─────────────────────────────────────────────────
#define GC9A01_DRIVER

// ─── Display size ───────────────────────────────────────────
#define TFT_WIDTH   240
#define TFT_HEIGHT  240

// ─── SPI pins — Seeed XIAO ESP32S3 ─────────────────────────
//   D10 = GPIO 9  MOSI
//   D8  = GPIO 7  SCLK
//   D1  = GPIO 2  CS
//   D3  = GPIO 4  DC
//   RST = -1 (not wired or tied high on the round display)
#define TFT_MOSI    9
#define TFT_SCLK    7
#define TFT_CS      2
#define TFT_DC      4
#define TFT_RST    -1

// ─── SPI bus ────────────────────────────────────────────────
// ESP32-S3: use SPI2 (FSPI).  Required to prevent crashes.
#define USE_HSPI_PORT

// ─── SPI frequency ──────────────────────────────────────────
#define SPI_FREQUENCY       40000000   // 40 MHz display writes
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000

// ─── DMA ────────────────────────────────────────────────────
#define USE_DMA_TO_TFT

// ─── Fonts (include what you need) ──────────────────────────
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4

// ─── Colour order ───────────────────────────────────────────
// GC9A01 is BGR; TFT_eSPI inverts automatically.
// If colours look wrong, try uncommenting:
// #define TFT_RGB_ORDER TFT_BGR
