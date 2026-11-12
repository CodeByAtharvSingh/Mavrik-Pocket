// Mavrik OS 0.2 — build options and pin map for the ESP32-S3 prototype.
// Change the values here; nothing else needs editing for a normal build.
#pragma once

// ---------------------------------------------------------------- build options
#define MAVRIK_VERSION      "0.2"
#ifndef MAVRIK_WHEEL_TOUCH
#define MAVRIK_WHEEL_TOUCH  0     // 0 = KY-040 rotary encoder, 1 = copper touch wheel (3 pads)
#endif
#ifndef MAVRIK_HAS_BUZZER
#define MAVRIK_HAS_BUZZER   0     // 1 only if a buzzer is fitted on PIN_BUZZER
#endif
#ifndef MAVRIK_WIFI
#define MAVRIK_WIFI         0     // 1 = internet clock + idea transcription over Wi-Fi
#endif

// Wi-Fi and transcription (used only when MAVRIK_WIFI is 1)
#define WIFI_SSID           ""
#define WIFI_PASS           ""
#define TIMEZONE            "IST-5:30"          // India Standard Time
// Speech-to-text endpoint: the idea is sent as an HTTP POST with Content-Type audio/wav,
// and the reply body is saved as the idea's text. Point it at the companion app or your server.
#define TRANSCRIBE_URL      ""
#define TRANSCRIBE_TOKEN    ""                   // sent as "Authorization: Bearer <token>" if set

// ---------------------------------------------------------------- shared SPI bus: screen + microSD
#define PIN_SCK       12
#define PIN_MOSI      11
#define PIN_MISO      13
#define PIN_TFT_CS    10
#define PIN_TFT_DC     9
#define PIN_SD_CS      8      // the SD slot on the back of the 2.8" module
// Screen RESET and LED pins go to 3V3. The module's touch chip is not used: tie T_CS to 3V3.

// ---------------------------------------------------------------- controls
#define PIN_ENC_CLK    4      // encoder A    (touch wheel pad A when MAVRIK_WHEEL_TOUCH = 1)
#define PIN_ENC_DT     5      // encoder B    (touch wheel pad B)
#define PIN_TOUCH_C   14      // touch wheel pad C (touch wheel only)
#define PIN_ENC_SW     6      // centre select (encoder push, or a tactile switch under the centre)
#define PIN_ACTION     7      // orange action button
#define PIN_BACK       1
#define PIN_UP         2      // optional extra buttons, handy on the breadboard
#define PIN_DOWN      42
#define PIN_BUZZER    41

// ---------------------------------------------------------------- audio
// INMP441 microphone (L/R pin to GND = left channel), on I2S port 0
#define PIN_MIC_SCK   15
#define PIN_MIC_WS    16
#define PIN_MIC_SD    17
// MAX98357A amplifier, on I2S port 1 (VIN to 5V, GAIN pin left open = 9 dB)
#define PIN_AMP_BCLK  18
#define PIN_AMP_LRC   21
#define PIN_AMP_DIN   47

#define REC_RATE      16000   // Hz, mono, 16-bit: good for voice and for speech-to-text
#define MIC_SHIFT     13      // INMP441 sends 24-bit samples in 32-bit slots; lower = louder
#define SPEAKER_HPF_HZ 180    // cut bass the small speaker cannot play (0 = off)
#define MAX_REC_SECS  300     // longest idea, in seconds

// ---------------------------------------------------------------- haptics
#define PIN_HAPTIC    40      // coin vibration motor through a BC547 (1 kΩ to the base)
#define HAPTIC_TICK_MS   8
#define HAPTIC_SELECT_MS 16
#define HAPTIC_BUMP_MS   28

// ---------------------------------------------------------------- touch wheel tuning
#define TOUCH_STEP_DEG  20.0f  // wheel steps per turn = 360 / this
#define TOUCH_THRESHOLD 0.015f // fraction above the resting reading that counts as a finger
