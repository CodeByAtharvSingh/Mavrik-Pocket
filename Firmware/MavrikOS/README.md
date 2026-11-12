# Mavrik OS 0.2 firmware

Firmware for the Mavrik Pocket prototype: an ESP32-S3 N16R8 board with a 2.8" 240×320 SPI screen,
INMP441 microphone, MAX98357A amplifier and speaker, a scroll wheel, and a vibration motor.

**Status:** compiles for ESP32-S3 with the ESP32 Arduino core 3.3.12. Not yet run on the real hardware.

## Files

| File | What it does |
|---|---|
| `MavrikOS.ino` | The OS: Home, Record idea, Ideas, To-do, Music, Settings, About |
| `mavrik_config.h` | Pin map and options (wheel type, Wi-Fi, transcription) |
| `mavrik_audio.h` | Records ideas to WAV on the SD card; plays ideas and music through the amp |
| `mavrik_touchwheel.h` | iPod-style touch wheel from three copper pads |
| `mavrik_net.h` | Optional Wi-Fi: internet clock, and ideas sent off for speech-to-text |
| `sd_card/` | Starter files for the microSD card |

## Build

1. Arduino IDE 2, with **esp32 by Espressif Systems** 3.x from the Boards Manager.
2. Libraries: **Adafruit GFX Library** and **Adafruit BusIO**.
3. Board **ESP32S3 Dev Module**, with:
   - Flash Size: 16MB
   - PSRAM: OPI PSRAM
   - USB CDC On Boot: Enabled
   - Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)
4. Open `MavrikOS.ino`, plug into the board's USB-C port marked USB or COM, and upload.

## Wiring

| Part | Pin | ESP32-S3 GPIO |
|---|---|---|
| Screen | SCK / MOSI / MISO | 12 / 11 / 13 |
| Screen | CS / DC | 10 / 9 |
| Screen | RESET, LED | 3V3 |
| Screen | T_CS (touch chip, unused) | 3V3 |
| SD slot on the screen | SD_CS (SCK/MOSI/MISO shared with the screen) | 8 |
| Encoder (KY-040) | CLK / DT / SW | 4 / 5 / 6 |
| Touch wheel (option) | Pads A / B / C, centre switch | 4 / 5 / 14, 6 |
| Action button (orange) | to GND | 7 |
| Back button | to GND | 1 |
| INMP441 mic | SCK / WS / SD, L/R to GND, VDD to 3V3 | 15 / 16 / 17 |
| MAX98357A amp | BCLK / LRC / DIN, VIN to 5V | 18 / 21 / 47 |
| Vibration motor | BC547 base through 1 kΩ | 40 |

Buttons use the board's internal pull-ups, so each one goes from its pin to GND.
The vibration motor needs a diode across it (1N4148, stripe to +).

## SD card

Copy what's inside the `sd_card` folder to the top level of a FAT32 microSD card.

```
/library.txt        Title|Artist|m:ss|/music/file.wav   (one track per line)
/music/1.wav        music, 16-bit PCM WAV (44.1 kHz stereo works)
/ideas/             created automatically: idea_<n>.wav, and idea_<n>.txt when transcribed
```

If a track line has no file, the firmware plays `/music/1.wav`, `/music/2.wav` and so on in order.
MP3 is not supported yet; convert music to WAV first.

## Options in `mavrik_config.h`

- `MAVRIK_WHEEL_TOUCH 1` switches from the encoder to the touch wheel. Tune `TOUCH_THRESHOLD` if it is too sensitive or not sensitive enough.
- `MAVRIK_WIFI 1`, with `WIFI_SSID` and `WIFI_PASS`, sets the clock from the internet.
- `TRANSCRIBE_URL`: each idea is sent there as `audio/wav`, and the reply text is saved as the idea's text. The ESP32-S3 can't turn speech into text by itself; this is where the companion app or a server does it.
- `MIC_SHIFT` sets the microphone loudness, and `SPEAKER_HPF_HZ` cuts bass the small speaker can't play.
