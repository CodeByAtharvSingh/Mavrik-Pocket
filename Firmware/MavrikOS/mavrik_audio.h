// Mavrik OS — audio engine: records the INMP441 mic to WAV on the SD card and plays WAV
// files through the MAX98357A. Runs in its own task on core 0 so the UI stays smooth.
#pragma once
#include <Arduino.h>
#include <SD.h>
#include <ESP_I2S.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "mavrik_config.h"

// The screen and the SD card share one SPI bus; every SD call takes this lock.
SemaphoreHandle_t sdMutex = nullptr;
#define SD_LOCK()   xSemaphoreTake(sdMutex, portMAX_DELAY)
#define SD_UNLOCK() xSemaphoreGive(sdMutex)

enum AudioState : uint8_t { A_IDLE, A_REC, A_PLAY, A_PAUSE };
enum AudioCmdType : uint8_t { AC_REC, AC_REC_STOP, AC_REC_DISCARD, AC_PLAY, AC_PAUSE, AC_RESUME, AC_STOP };
struct AudioCmd { AudioCmdType type; char path[48]; };

static I2SClass i2sMic(I2S_NUM_0);
static I2SClass i2sAmp(I2S_NUM_1);
static QueueHandle_t audioQ = nullptr;
static volatile AudioState aState = A_IDLE;
static volatile uint32_t aPosFrames = 0, aLenFrames = 0, aRate = REC_RATE;
static volatile uint32_t aRecBytes = 0;
static volatile bool aEnded = false, aFailed = false, aRecDone = false;
static volatile float aLevel = 0;
static volatile int aVolume = 60;
static bool micOk = false, ampOk = false;
static uint32_t ampRate = 0;

static File aFile;
static uint32_t aDataStart = 0;
static uint16_t aChannels = 1;
static char aRecPath[48];

static void wavHeader(uint8_t *h, uint32_t dataBytes, uint32_t rate, uint16_t ch) {
  uint32_t byteRate = rate * ch * 2;
  memcpy(h, "RIFF", 4);
  uint32_t v = 36 + dataBytes; memcpy(h + 4, &v, 4);
  memcpy(h + 8, "WAVEfmt ", 8);
  v = 16; memcpy(h + 16, &v, 4);
  uint16_t s = 1; memcpy(h + 20, &s, 2);          // PCM
  memcpy(h + 22, &ch, 2);
  memcpy(h + 24, &rate, 4);
  memcpy(h + 28, &byteRate, 4);
  s = ch * 2; memcpy(h + 32, &s, 2);
  s = 16; memcpy(h + 34, &s, 2);
  memcpy(h + 36, "data", 4);
  memcpy(h + 40, &dataBytes, 4);
}

// Reads a WAV header; leaves the file positioned at the first sample.
static bool wavOpen(File &f, uint32_t &rate, uint16_t &ch, uint32_t &dataBytes) {
  uint8_t h[12];
  if (f.read(h, 12) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return false;
  bool fmt = false;
  while (f.available()) {
    uint8_t c[8];
    if (f.read(c, 8) != 8) return false;
    uint32_t len; memcpy(&len, c + 4, 4);
    if (!memcmp(c, "fmt ", 4)) {
      uint8_t b[16];
      if (len < 16 || f.read(b, 16) != 16) return false;
      uint16_t format, bits; memcpy(&format, b, 2); memcpy(&ch, b + 2, 2);
      memcpy(&rate, b + 4, 4); memcpy(&bits, b + 14, 2);
      if (format != 1 || bits != 16 || ch < 1 || ch > 2) return false;   // 16-bit PCM only
      f.seek(f.position() + (len - 16) + (len & 1));
      fmt = true;
    } else if (!memcmp(c, "data", 4)) {
      if (!fmt) return false;
      dataBytes = len;
      return true;
    } else {
      f.seek(f.position() + len + (len & 1));
    }
  }
  return false;
}

static bool ampSetRate(uint32_t rate) {
  if (ampOk && rate == ampRate) return true;
  if (ampOk) i2sAmp.end();
  i2sAmp.setPins(PIN_AMP_BCLK, PIN_AMP_LRC, PIN_AMP_DIN);
  ampOk = i2sAmp.begin(I2S_MODE_STD, rate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  ampRate = ampOk ? rate : 0;
  return ampOk;
}

static void closeFile() {
  if (aFile) { SD_LOCK(); aFile.close(); SD_UNLOCK(); }
}

static void startRec(const char *path) {
  if (!micOk) return;
  strncpy(aRecPath, path, sizeof aRecPath - 1); aRecPath[sizeof aRecPath - 1] = 0;
  SD_LOCK();
  SD.mkdir("/ideas");
  aFile = SD.open(aRecPath, FILE_WRITE);
  if (aFile) { uint8_t h[44]; wavHeader(h, 0, REC_RATE, 1); aFile.write(h, 44); }
  SD_UNLOCK();
  if (!aFile) return;
  aRecBytes = 0; aRecDone = false;
  aState = A_REC;
}

static void stopRec(bool keep) {
  if (aState != A_REC) return;
  SD_LOCK();
  if (keep) {
    uint8_t h[44]; wavHeader(h, aRecBytes, REC_RATE, 1);
    aFile.seek(0); aFile.write(h, 44);
    aFile.close();
  } else {
    aFile.close();
    SD.remove(aRecPath);
  }
  SD_UNLOCK();
  aState = A_IDLE; aLevel = 0; aRecDone = true;
}

static void startPlay(const char *path) {
  closeFile();
  uint32_t rate = 0, bytes = 0; uint16_t ch = 1;
  SD_LOCK();
  aFile = SD.open(path, FILE_READ);
  bool ok = aFile && wavOpen(aFile, rate, ch, bytes);
  if (ok) aDataStart = aFile.position();
  else if (aFile) aFile.close();
  SD_UNLOCK();
  if (!ok || !ampSetRate(rate)) { aState = A_IDLE; aFailed = true; return; }
  aChannels = ch; aRate = rate;
  aLenFrames = bytes / (2 * ch); aPosFrames = 0;
  aEnded = false;
  aState = A_PLAY;
}

static void audioTask(void *) {
  static int32_t micBuf[256];
  static int16_t pcm[512];
  static int16_t out[1024];
  float hpY = 0, hpX = 0, recDc = 0;
  for (;;) {
    AudioCmd c;
    while (xQueueReceive(audioQ, &c, 0) == pdTRUE) {
      switch (c.type) {
        case AC_REC: if (aState == A_PLAY || aState == A_PAUSE) { closeFile(); } startRec(c.path); break;
        case AC_REC_STOP: stopRec(true); break;
        case AC_REC_DISCARD: stopRec(false); break;
        case AC_PLAY: if (aState == A_REC) stopRec(true); startPlay(c.path); hpX = hpY = 0; break;
        case AC_PAUSE: if (aState == A_PLAY) aState = A_PAUSE; break;
        case AC_RESUME: if (aState == A_PAUSE) aState = A_PLAY; break;
        case AC_STOP: if (aState == A_REC) stopRec(true); else { closeFile(); aState = A_IDLE; } break;
      }
    }

    if (aState == A_REC) {
      size_t got = i2sMic.readBytes((char *)micBuf, sizeof micBuf);
      int n = got / 4;
      float sum = 0;
      for (int i = 0; i < n; i++) {
        float s = (float)(micBuf[i] >> MIC_SHIFT);
        recDc += (s - recDc) * 0.001f;                 // remove the mic's DC offset
        s -= recDc;
        if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
        pcm[i] = (int16_t)s;
        sum += s * s;
      }
      if (n) {
        aLevel = sqrtf(sum / n) / 6000.0f;
        SD_LOCK(); aFile.write((uint8_t *)pcm, n * 2); SD_UNLOCK();
        aRecBytes += n * 2;
        if (aRecBytes >= (uint32_t)MAX_REC_SECS * REC_RATE * 2) stopRec(true);
      }
      continue;
    }

    if (aState == A_PLAY) {
      int frames = 256;
      size_t want = frames * 2 * aChannels;
      SD_LOCK(); int got = aFile.read((uint8_t *)pcm, want); SD_UNLOCK();
      int f = got / (2 * aChannels);
      if (f <= 0) { closeFile(); aState = A_IDLE; aEnded = true; continue; }
      float v = aVolume / 100.0f; v = v * v;              // feels even across the wheel
      const float a = SPEAKER_HPF_HZ > 0 ? 1.0f / (1.0f + 2.0f * PI * SPEAKER_HPF_HZ / aRate) : 1.0f;
      for (int i = 0; i < f; i++) {
        float x = aChannels == 2 ? (pcm[2 * i] + pcm[2 * i + 1]) * 0.5f : pcm[i];
        float y = SPEAKER_HPF_HZ > 0 ? a * (hpY + x - hpX) : x;   // one-pole high-pass
        hpX = x; hpY = y;
        float s = y * v;
        if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
        out[2 * i] = out[2 * i + 1] = (int16_t)s;
      }
      i2sAmp.write((uint8_t *)out, f * 4);   // blocks until the DMA has room: this paces playback
      aPosFrames += f;
      continue;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ---------------------------------------------------------------- calls used by the UI
void audioBegin() {
  if (!sdMutex) sdMutex = xSemaphoreCreateMutex();
  i2sMic.setPins(PIN_MIC_SCK, PIN_MIC_WS, -1, PIN_MIC_SD);
  micOk = i2sMic.begin(I2S_MODE_STD, REC_RATE, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT);
  ampSetRate(44100);
  audioQ = xQueueCreate(8, sizeof(AudioCmd));
  xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 5, nullptr, 0);
  Serial.printf("[Mavrik] audio: mic %s, amp %s\n", micOk ? "ok" : "FAILED", ampOk ? "ok" : "FAILED");
}

static void audioSend(AudioCmdType t, const char *path = "") {
  AudioCmd c; c.type = t;
  strncpy(c.path, path, sizeof c.path - 1); c.path[sizeof c.path - 1] = 0;
  xQueueSend(audioQ, &c, pdMS_TO_TICKS(50));
}

bool audioMicReady() { return micOk; }
void audioRecStart(const char *path) { audioSend(AC_REC, path); }
// Stops recording and waits for the file to be finished. Returns the length in ms.
uint32_t audioRecStop(bool keep) {
  aRecDone = false;
  audioSend(keep ? AC_REC_STOP : AC_REC_DISCARD);
  for (int i = 0; i < 100 && !aRecDone; i++) delay(5);
  return (uint32_t)((uint64_t)aRecBytes * 1000 / (REC_RATE * 2));
}
uint32_t audioRecMs() { return (uint32_t)((uint64_t)aRecBytes * 1000 / (REC_RATE * 2)); }
bool audioRecording() { return aState == A_REC; }
void audioPlay(const char *path) { aEnded = false; aFailed = false; audioSend(AC_PLAY, path); }
void audioPause() { audioSend(AC_PAUSE); }
void audioResume() { audioSend(AC_RESUME); }
void audioStop() { audioSend(AC_STOP); }
bool audioActive() { return aState == A_PLAY || aState == A_PAUSE; }
bool audioTakeEnded() { bool e = aEnded; aEnded = false; return e; }
bool audioTakeFailed() { bool e = aFailed; aFailed = false; return e; }
uint32_t audioPosMs() { return aRate ? (uint32_t)((uint64_t)aPosFrames * 1000 / aRate) : 0; }
uint32_t audioLenMs() { return aRate ? (uint32_t)((uint64_t)aLenFrames * 1000 / aRate) : 0; }
void audioSetVolume(int pct) { aVolume = constrain(pct, 0, 100); }
float audioLevel() { float l = aLevel; return l > 1 ? 1 : l; }
