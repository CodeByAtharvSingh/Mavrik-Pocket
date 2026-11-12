// Mavrik OS — Wi-Fi: real clock from the internet, and sending ideas off for transcription.
// Speech-to-text cannot run on the ESP32-S3 itself, so each idea's WAV file is posted to
// TRANSCRIBE_URL (the companion app or a small server) and the text that comes back is saved
// next to it as /ideas/idea_<n>.txt. Ideas recorded offline are sent once Wi-Fi is back.
#pragma once
#include <Arduino.h>
#include "mavrik_config.h"
#include "mavrik_audio.h"

volatile bool netTextReady = false;      // set when a new transcript has been saved

#if MAVRIK_WIFI
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

static QueueHandle_t netQ = nullptr;

static bool sendIdea(uint16_t num) {
  char wav[40], txt[40];
  snprintf(wav, sizeof wav, "/ideas/idea_%u.wav", num);
  snprintf(txt, sizeof txt, "/ideas/idea_%u.txt", num);
  SD_LOCK();
  bool done = SD.exists(txt);
  File f = done ? File() : SD.open(wav, FILE_READ);
  size_t len = f ? f.size() : 0;
  uint8_t *buf = len ? (uint8_t *)ps_malloc(len) : nullptr;    // ideas are small enough for PSRAM
  if (buf) len = f.read(buf, len);
  if (f) f.close();
  SD_UNLOCK();
  if (done) return true;
  if (!buf) return false;

  HTTPClient http;
  http.setTimeout(30000);
  bool ok = false;
  if (http.begin(TRANSCRIBE_URL)) {
    http.addHeader("Content-Type", "audio/wav");
    if (strlen(TRANSCRIBE_TOKEN)) http.addHeader("Authorization", String("Bearer ") + TRANSCRIBE_TOKEN);
    int code = http.POST(buf, len);
    if (code == 200) {
      String text = http.getString();
      text.trim();
      SD_LOCK();
      File o = SD.open(txt, FILE_WRITE);
      if (o) { o.print(text); o.close(); ok = true; }
      SD_UNLOCK();
      if (ok) netTextReady = true;
    }
    Serial.printf("[Mavrik] transcribe idea %u: HTTP %d\n", num, code);
    http.end();
  }
  free(buf);
  return ok;
}

static void netTask(void *) {
  for (;;) {
    uint16_t num;
    if (xQueueReceive(netQ, &num, pdMS_TO_TICKS(1000)) != pdTRUE) continue;
    while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(2000));
    if (!sendIdea(num)) {                 // try again later
      vTaskDelay(pdMS_TO_TICKS(15000));
      xQueueSend(netQ, &num, 0);
    }
  }
}

void netBegin() {
  if (!strlen(WIFI_SSID)) return;
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTzTime(TIMEZONE, "pool.ntp.org", "time.google.com");
  netQ = xQueueCreate(32, sizeof(uint16_t));
  xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 1, nullptr, 0);
}

void netQueueIdea(uint16_t num) {
  if (netQ && strlen(TRANSCRIBE_URL)) xQueueSend(netQ, &num, 0);
}

// Minute of the day from the internet clock, or -1 until it has been set.
int netMinuteOfDay() {
  struct tm t;
  if (!getLocalTime(&t, 0) || t.tm_year < 120) return -1;
  return t.tm_hour * 60 + t.tm_min;
}

bool netConnected() { return WiFi.status() == WL_CONNECTED; }
#else
void netBegin() {}
void netQueueIdea(uint16_t) {}
int netMinuteOfDay() { return -1; }
bool netConnected() { return false; }
#endif
