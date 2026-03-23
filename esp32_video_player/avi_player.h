#pragma once
#include <Arduino.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <JPEGDEC.h>
#include <driver/i2s.h>
#include "config.h"

// ============================================================
//  AVI chunk / RIFF tag identifiers (little-endian uint32)
// ============================================================
#define TAG(a,b,c,d) ((uint32_t)(a)|((uint32_t)(b)<<8)|((uint32_t)(c)<<16)|((uint32_t)(d)<<24))

static constexpr uint32_t TAG_RIFF = TAG('R','I','F','F');
static constexpr uint32_t TAG_AVI  = TAG('A','V','I',' ');
static constexpr uint32_t TAG_LIST = TAG('L','I','S','T');
static constexpr uint32_t TAG_hdrl = TAG('h','d','r','l');
static constexpr uint32_t TAG_avih = TAG('a','v','i','h');
static constexpr uint32_t TAG_strl = TAG('s','t','r','l');
static constexpr uint32_t TAG_strh = TAG('s','t','r','h');
static constexpr uint32_t TAG_strf = TAG('s','t','r','f');
static constexpr uint32_t TAG_movi = TAG('m','o','v','i');
static constexpr uint32_t TAG_vids = TAG('v','i','d','s');
static constexpr uint32_t TAG_auds = TAG('a','u','d','s');
static constexpr uint32_t TAG_00dc = TAG('0','0','d','c');
static constexpr uint32_t TAG_01wb = TAG('0','1','w','b');
static constexpr uint32_t TAG_idx1 = TAG('i','d','x','1');

// ============================================================
//  Packed AVI structs (must not have padding)
// ============================================================
#pragma pack(push, 1)

struct AVIMainHeader {       // avih, 56 bytes
    uint32_t microSecPerFrame;
    uint32_t maxBytesPerSec;
    uint32_t paddingGranularity;
    uint32_t flags;
    uint32_t totalFrames;
    uint32_t initialFrames;
    uint32_t streams;
    uint32_t suggestedBufferSize;
    uint32_t width;
    uint32_t height;
    uint32_t reserved[4];
};

struct AVIStreamHeader {     // strh, 56 bytes
    char     fccType[4];
    char     fccHandler[4];
    uint32_t flags;
    uint16_t priority;
    uint16_t language;
    uint32_t initialFrames;
    uint32_t scale;
    uint32_t rate;           // FPS = rate / scale
    uint32_t start;
    uint32_t length;
    uint32_t suggestedBufferSize;
    uint32_t quality;
    uint32_t sampleSize;
    struct { int16_t left, top, right, bottom; } frame;
};

struct WaveFormatEx {        // strf for audio, 18 bytes
    uint16_t formatTag;      // 1 = PCM
    uint16_t channels;
    uint32_t samplesPerSec;
    uint32_t avgBytesPerSec;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    uint16_t cbSize;
};

#pragma pack(pop)

// ============================================================
//  Playback state
// ============================================================
enum class PlayState { Stopped, Playing, Paused };

// ============================================================
//  AVIPlayer
// ============================================================
class AVIPlayer {
public:
    explicit AVIPlayer(TFT_eSPI& tft);
    ~AVIPlayer();

    // Open an AVI file and start playback.  Call startTask() after.
    bool open(const char* path);

    // Close file and free buffers.
    void close();

    // Control
    void pause();
    void resume();
    void stop();       // sets state to Stopped; task exits on next iteration

    // Info
    PlayState   state()        const { return _state; }
    uint32_t    currentFrame() const { return _currentFrame; }
    uint32_t    totalFrames()  const { return _totalFrames; }
    uint32_t    fps()          const { return _fps; }
    const char* filename()     const { return _filename; }

    // FreeRTOS task entry — call from a task created with xTaskCreatePinnedToCore
    void runTask();

    // Scan SD for .avi files; returns count found (up to maxFiles).
    // filenames[][64] must be allocated by caller.
    static int scanFiles(const char* folder, char filenames[][64], int maxFiles);

    // JPEG pixel-block callback (must be public for static linkage)
    static int jpegDraw(JPEGDRAW* pDraw);

private:
    TFT_eSPI& _tft;
    JPEGDEC   _jpeg;
    File      _file;

    PlayState _state        = PlayState::Stopped;
    char      _filename[64] = {};

    // AVI metadata
    uint32_t _totalFrames   = 0;
    uint32_t _currentFrame  = 0;
    uint32_t _fps           = 15;
    uint32_t _moviStart     = 0;   // absolute file offset to first chunk in movi
    uint32_t _moviEnd       = 0;   // absolute file offset past last movi byte
    uint32_t _vidWidth      = 240;
    uint32_t _vidHeight     = 240;

    // Audio metadata
    bool        _hasAudio       = false;
    WaveFormatEx _waveFmt       = {};

    // Buffers (allocated in PSRAM when available)
    uint8_t* _jpegBuf  = nullptr;
    uint8_t* _audioBuf = nullptr;

    // Timing
    uint32_t _frameMs      = 0;    // target ms per frame
    uint32_t _lastFrameUs  = 0;

    // I2S
    bool _i2sReady = false;

    // Helpers
    bool parseHeader();
    bool seekToMovi();
    bool readChunkHeader(uint32_t& tagOut, uint32_t& sizeOut);
    void skipBytes(uint32_t n);
    void processVideoChunk(uint32_t size);
    void processAudioChunk(uint32_t size);
    void initI2S();
    void writeAudioI2S(const uint8_t* data, size_t len);
    void waitFrameTiming();

    // Static pointer for JPEG callback
    static AVIPlayer* _instance;
};

// Launch the video task pinned to VIDEO_TASK_CORE.
void aviPlayerStartTask(AVIPlayer* player);
