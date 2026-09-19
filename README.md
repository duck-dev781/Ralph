# Ralph

Offline ESP32 WROVER-E character project.

## Hardware
- ESP32 WROVER-E
- 16x2 I2C LCD, default 0x27
- MPU6050, default 0x68
- SD card
- BLE
- optional brake/status LED on GPIO 25

## Features
- first-boot BLE setup key
- settings stored on SD
- chat history stored on SD
- offline response brain
- Fahrenheit internal ESP32 temperature display
- gravity/orientation detection
- upside-down fall state
- shake/dizzy state
- house-jumping animation during hard shaking
- 30-minute inactivity sleep
- brake light off during sleep
- tiny movement wakes Ralph
- LCD animation characters in characters.h

## BLE
Device: Ralph-ESP32

Send `SETUP <key>` on first boot. The key is displayed on the LCD and saved in /RALPH/SETTINGS.TXT.

After setup:
- `CHAT hello Ralph`
- `NAME Ralph`
- `HOUSE Tiny House`
- `OWNER Chase`
- `PERSONALITY friendly`
- `SLEEP`
- `WAKE`
- `STATUS`
- `RESETSETUP`

This is an offline response engine, not a full Llama model. A future tiny quantized model can be added if it fits the WROVER's actual RAM/compute limits. SD storage can hold model files and huge text databases, but SD capacity does not itself provide inference RAM.

## SD layout
/ RALPH /
- SETTINGS.TXT
- CHATS.TXT
- AI/

The current firmware creates the directory automatically.
