#include "avi_player.h"

// ============================================================
//  Static instance pointer for JPEG callback
// ============================================================
AVIPlayer* AVIPlayer::_instance = nullptr;

// ============================================================
//  Constructor / Destructor
// ============================================================
AVIPlayer::AVIPlayer(TFT_eSPI& tft) : _tft(tft) {}

AVIPlayer::~AVIPlayer() {
    close();
}

// ============================================================
//  open() — parse header and allocate buffers
// ============================================================
bool AVIPlayer::open(const char* path) {
    close();

    _file = SD.open(path, FILE_READ);
    if (!_file) {
        Serial.printf("[AVI] Cannot open: %s\n", path);
        return false;
    }
    strncpy(_filename, path, sizeof(_filename) - 1);

    // Allocate JPEG frame buffer in PSRAM if available
    _jpegBuf = (uint8_t*)ps_malloc(MAX_JPEG_SIZE);
    if (!_jpegBuf) {
        _jpegBuf = (uint8_t*)malloc(MAX_JPEG_SIZE);
    }
    if (!_jpegBuf) {
        Serial.println("[AVI] Failed to allocate JPEG buffer");
        _file.close();
        return false;
    }

    // Allocate audio chunk buffer
    size_t audioBufSz = AUDIO_DMA_BUFS * AUDIO_DMA_LEN * 4; // generous
    _audioBuf = (uint8_t*)ps_malloc(audioBufSz);
    if (!_audioBuf) {
        _audioBuf = (uint8_t*)malloc(audioBufSz);
    }
    if (!_audioBuf) {
        Serial.println("[AVI] Failed to allocate audio buffer");
        free(_jpegBuf); _jpegBuf = nullptr;
        _file.close();
        return false;
    }

    if (!parseHeader()) {
        Serial.println("[AVI] Header parse failed");
        close();
        return false;
    }

    _frameMs      = 1000u / (_fps ? _fps : 15u);
    _currentFrame = 0;
    _instance     = this;

    if (_hasAudio) {
        initI2S();
    }

    _state = PlayState::Playing;
    Serial.printf("[AVI] Opened: %s  %ux%u @ %u fps  audio=%s\n",
                  path, _vidWidth, _vidHeight, _fps, _hasAudio ? "yes" : "no");
    return true;
}

// ============================================================
//  close()
// ============================================================
void AVIPlayer::close() {
    _state = PlayState::Stopped;
    if (_file) _file.close();
    free(_jpegBuf);  _jpegBuf  = nullptr;
    free(_audioBuf); _audioBuf = nullptr;
    if (_i2sReady) {
        i2s_driver_uninstall(I2S_PORT);
        _i2sReady = false;
    }
    _hasAudio     = false;
    _totalFrames  = 0;
    _currentFrame = 0;
    _moviStart    = 0;
    _moviEnd      = 0;
}

// ============================================================
//  Control
// ============================================================
void AVIPlayer::pause()  { if (_state == PlayState::Playing) _state = PlayState::Paused; }
void AVIPlayer::resume() { if (_state == PlayState::Paused)  _state = PlayState::Playing; }
void AVIPlayer::stop()   { _state = PlayState::Stopped; }

// ============================================================
//  parseHeader() — navigate RIFF/AVI structure
// ============================================================
bool AVIPlayer::parseHeader() {
    _file.seek(0);

    // RIFF header
    uint32_t tag, size;
    if (_file.read((uint8_t*)&tag,  4) != 4) return false;
    if (_file.read((uint8_t*)&size, 4) != 4) return false;
    if (tag != TAG_RIFF) { Serial.println("[AVI] Not a RIFF file"); return false; }

    uint32_t fileType;
    if (_file.read((uint8_t*)&fileType, 4) != 4) return false;
    if (fileType != TAG_AVI) { Serial.println("[AVI] Not an AVI file"); return false; }

    // Walk chunks until we find hdrl LIST and movi LIST
    bool gotAVIH = false, gotVideoStream = false, gotAudioStream = false, gotMovi = false;

    while (_file.available()) {
        uint32_t chunkTag, chunkSize;
        if (!readChunkHeader(chunkTag, chunkSize)) break;
        uint32_t chunkStart = _file.position();

        if (chunkTag == TAG_LIST) {
            uint32_t listType;
            if (_file.read((uint8_t*)&listType, 4) != 4) break;

            if (listType == TAG_hdrl) {
                // Parse hdrl sub-chunks
                uint32_t hdrlEnd = chunkStart + chunkSize;
                while (_file.position() < hdrlEnd - 4) {
                    uint32_t sc, ss;
                    if (!readChunkHeader(sc, ss)) break;
                    uint32_t scStart = _file.position();

                    if (sc == TAG_avih && ss >= sizeof(AVIMainHeader)) {
                        AVIMainHeader avih;
                        _file.read((uint8_t*)&avih, sizeof(avih));
                        _totalFrames = avih.totalFrames;
                        _vidWidth    = avih.width;
                        _vidHeight   = avih.height;
                        uint32_t uspf = avih.microSecPerFrame;
                        _fps = (uspf > 0) ? (1000000u / uspf) : 15u;
                        gotAVIH = true;
                    } else if (sc == TAG_LIST) {
                        uint32_t lt;
                        _file.read((uint8_t*)&lt, 4);
                        if (lt == TAG_strl) {
                            // Read strh + strf
                            uint32_t strlEnd = scStart + ss;
                            AVIStreamHeader strh = {};
                            bool gotStrh = false;
                            while (_file.position() < strlEnd - 4) {
                                uint32_t sc2, ss2;
                                if (!readChunkHeader(sc2, ss2)) break;
                                uint32_t sc2Start = _file.position();
                                if (sc2 == TAG_strh && ss2 >= sizeof(AVIStreamHeader)) {
                                    _file.read((uint8_t*)&strh, sizeof(strh));
                                    gotStrh = true;
                                } else if (sc2 == TAG_strf && gotStrh) {
                                    uint32_t type = TAG(strh.fccType[0], strh.fccType[1],
                                                        strh.fccType[2], strh.fccType[3]);
                                    if (type == TAG_vids) {
                                        // BITMAPINFOHEADER — skip (we already have dims)
                                        gotVideoStream = true;
                                        // Update fps from strh (more reliable)
                                        if (strh.scale > 0)
                                            _fps = strh.rate / strh.scale;
                                    } else if (type == TAG_auds && ss2 >= sizeof(WaveFormatEx)) {
                                        _file.read((uint8_t*)&_waveFmt, sizeof(WaveFormatEx));
                                        _hasAudio    = (_waveFmt.formatTag == 1); // PCM only
                                        gotAudioStream = true;
                                    }
                                }
                                // seek past sub-chunk
                                _file.seek(sc2Start + ss2 + (ss2 & 1));
                            }
                        }
                    }
                    // seek past chunk
                    _file.seek(scStart + ss + (ss & 1));
                }
            } else if (listType == TAG_movi) {
                _moviStart = _file.position();   // points at first chunk inside movi
                _moviEnd   = chunkStart + chunkSize;
                gotMovi    = true;
                break;  // Done — start reading frames from here
            } else {
                // Unknown LIST — skip
                _file.seek(chunkStart + chunkSize);
            }
        } else {
            // Non-LIST chunk at top level — skip
            _file.seek(chunkStart + chunkSize + (chunkSize & 1));
        }
    }

    if (!gotMovi) {
        Serial.println("[AVI] movi section not found");
        return false;
    }
    if (!gotAVIH) {
        Serial.println("[AVI] avih not found");
        return false;
    }

    Serial.printf("[AVI] Header OK — %u frames @ %u fps, movi@0x%08X\n",
                  _totalFrames, _fps, _moviStart);
    _file.seek(_moviStart);
    return true;
}

// ============================================================
//  readChunkHeader() — read 8-byte id+size
// ============================================================
bool AVIPlayer::readChunkHeader(uint32_t& tagOut, uint32_t& sizeOut) {
    if (_file.read((uint8_t*)&tagOut,  4) != 4) return false;
    if (_file.read((uint8_t*)&sizeOut, 4) != 4) return false;
    return true;
}

void AVIPlayer::skipBytes(uint32_t n) {
    _file.seek(_file.position() + n);
}

// ============================================================
//  JPEG draw callback (called per 128×16 block by JPEGDEC)
// ============================================================
int AVIPlayer::jpegDraw(JPEGDRAW* pDraw) {
    if (!_instance) return 0;
    _instance->_tft.pushImage(pDraw->x, pDraw->y,
                               pDraw->iWidth, pDraw->iHeight,
                               pDraw->pPixels);
    return 1;
}

// ============================================================
//  processVideoChunk() — decode one MJPEG frame
// ============================================================
void AVIPlayer::processVideoChunk(uint32_t size) {
    if (size == 0 || size > MAX_JPEG_SIZE) {
        skipBytes(size + (size & 1));
        return;
    }

    size_t bytesRead = _file.read(_jpegBuf, size);
    if (size & 1) _file.read();  // padding byte

    if (bytesRead != size) return;

    if (_jpeg.openRAM(_jpegBuf, (int)size, jpegDraw)) {
        // Center image if smaller than display
        int x = (_tft.width()  - _jpeg.getWidth())  / 2;
        int y = (_tft.height() - _jpeg.getHeight()) / 2;
        if (x < 0) x = 0;
        if (y < 0) y = 0;

        _jpeg.setPixelType(RGB565_BIG_ENDIAN);
        _jpeg.decode(x, y, 0);
        _jpeg.close();
    }

    _currentFrame++;
}

// ============================================================
//  processAudioChunk() — send PCM to I2S
// ============================================================
void AVIPlayer::processAudioChunk(uint32_t size) {
    if (!_hasAudio || !_i2sReady || size == 0) {
        skipBytes(size + (size & 1));
        return;
    }

    uint32_t remaining = size;
    size_t   audioBufSz = AUDIO_DMA_BUFS * AUDIO_DMA_LEN * 4;

    while (remaining > 0) {
        uint32_t batch = min(remaining, (uint32_t)audioBufSz);
        size_t   got   = _file.read(_audioBuf, batch);
        if (got == 0) break;
        writeAudioI2S(_audioBuf, got);
        remaining -= got;
    }

    if (size & 1) _file.read();  // padding byte
}

// ============================================================
//  initI2S()
// ============================================================
void AVIPlayer::initI2S() {
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate          = _hasAudio ? _waveFmt.samplesPerSec : 44100,
        .bits_per_sample      = (i2s_bits_per_sample_t)(_hasAudio ? _waveFmt.bitsPerSample : 16),
        .channel_format       = (_hasAudio && _waveFmt.channels == 1)
                                    ? I2S_CHANNEL_FMT_ONLY_LEFT
                                    : I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = 0,
        .dma_buf_count        = AUDIO_DMA_BUFS,
        .dma_buf_len          = AUDIO_DMA_LEN,
        .use_apll             = false,
        .tx_desc_auto_clear   = true,
        .fixed_mclk           = 0,
    };

    i2s_pin_config_t pins = {
        .bck_io_num   = I2S_BCLK,
        .ws_io_num    = I2S_LRCLK,
        .data_out_num = I2S_DOUT,
        .data_in_num  = I2S_PIN_NO_CHANGE,
    };

    esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, nullptr);
    if (err != ESP_OK) {
        Serial.printf("[I2S] Install failed: %d\n", err);
        return;
    }
    i2s_set_pin(I2S_PORT, &pins);
    _i2sReady = true;
    Serial.printf("[I2S] Ready — %u Hz, %u ch, %u bit\n",
                  cfg.sample_rate, _waveFmt.channels, cfg.bits_per_sample);
}

// ============================================================
//  writeAudioI2S()
// ============================================================
void AVIPlayer::writeAudioI2S(const uint8_t* data, size_t len) {
    size_t written = 0;
    i2s_write(I2S_PORT, data, len, &written, portMAX_DELAY);
}

// ============================================================
//  waitFrameTiming() — throttle to target FPS
// ============================================================
void AVIPlayer::waitFrameTiming() {
    uint32_t now = micros();
    uint32_t targetUs = _frameMs * 1000u;

    if (_lastFrameUs > 0) {
        uint32_t elapsed = now - _lastFrameUs;
        if (elapsed < targetUs) {
            uint32_t waitUs = targetUs - elapsed;
            if (waitUs > 1000) {
                vTaskDelay(pdMS_TO_TICKS(waitUs / 1000));
            }
        }
    }
    _lastFrameUs = micros();
}

// ============================================================
//  runTask() — main playback loop (runs inside FreeRTOS task)
// ============================================================
void AVIPlayer::runTask() {
    Serial.println("[AVI] Task started");
    _file.seek(_moviStart);
    _lastFrameUs = 0;

    while (_state != PlayState::Stopped) {
        // Pause handling
        if (_state == PlayState::Paused) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // End of movi?
        if (_file.position() >= _moviEnd || !_file.available()) {
            Serial.println("[AVI] End of file — looping");
            _file.seek(_moviStart);
            _currentFrame = 0;
            _lastFrameUs  = 0;
            continue;
        }

        uint32_t tag, size;
        if (!readChunkHeader(tag, size)) {
            Serial.println("[AVI] Chunk read error — rewinding");
            _file.seek(_moviStart);
            _currentFrame = 0;
            _lastFrameUs  = 0;
            continue;
        }

        if (tag == TAG_00dc) {
            waitFrameTiming();
            processVideoChunk(size);
        } else if (tag == TAG_01wb) {
            processAudioChunk(size);
        } else if (tag == TAG_LIST) {
            // Nested LIST inside movi (some encoders add this) — step past type tag
            uint32_t lt;
            _file.read((uint8_t*)&lt, 4);
            // Don't skip — let the loop re-read sub-chunks naturally
        } else {
            // Unknown chunk — skip
            skipBytes(size + (size & 1));
        }
    }

    Serial.println("[AVI] Task ended");
    vTaskDelete(nullptr);
}

// ============================================================
//  scanFiles() — list .avi files in a folder
// ============================================================
int AVIPlayer::scanFiles(const char* folder, char filenames[][64], int maxFiles) {
    File dir = SD.open(folder);
    if (!dir || !dir.isDirectory()) return 0;

    int count = 0;
    while (count < maxFiles) {
        File entry = dir.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) {
            const char* name = entry.name();
            size_t len = strlen(name);
            if (len > 4 && strcasecmp(name + len - 4, VIDEO_EXT) == 0) {
                snprintf(filenames[count], 64, "%s/%s",
                         strcmp(folder, "/") == 0 ? "" : folder, name);
                count++;
            }
        }
        entry.close();
    }
    dir.close();
    return count;
}

// ============================================================
//  FreeRTOS task wrapper
// ============================================================
static void aviTaskFn(void* param) {
    static_cast<AVIPlayer*>(param)->runTask();
}

void aviPlayerStartTask(AVIPlayer* player) {
    xTaskCreatePinnedToCore(
        aviTaskFn,
        "aviPlayer",
        VIDEO_STACK_SIZE,
        player,
        VIDEO_TASK_PRIORITY,
        nullptr,
        VIDEO_TASK_CORE
    );
}
