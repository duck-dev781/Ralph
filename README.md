# Ralph

## First installation
1. From the GitHub Actions artifact named Ralph-Tamagotchi, use Ralph.ino.bin to flash the ESP32.
2. Copy the contents of SD_Initial/RALPH into the SD card's /RALPH folder.
3. Power Ralph on.
4. Use BLE command KEY, then send SETUP <key>.
5. Configure Wi-Fi with WIFI_SSID <name> and WIFI_PASS <password>.

## Update system
Firmware and SD data have separate versions.
- Firmware-only change: bump FIRMWARE_VERSION and publish a vX.Y.Z release.
- SD-only change: bump SD_VERSION and edit packages/latest/SD_Update/index.txt. No firmware rebuild is required for Ralph to receive the SD update.
- Ralph checks automatically every 6 hours while Wi-Fi is connected and can also be told UPDATE over BLE or the web panel.

## Web panel
Use http://ralph.local or Ralph's IP when Wi-Fi is connected. Login is username ralph and the setup key.

## Wiring

This is the recommended Ralph wiring for the Freenove ESP32 WROVER-E / FNK0047.

| Part | Pin | ESP32 |
|---|---|---|
| 16x2 I2C LCD | SDA | GPIO 21 |
| 16x2 I2C LCD | SCL | GPIO 22 |
| 16x2 I2C LCD | VCC | 5V |
| 16x2 I2C LCD | GND | GND |
| MPU6050 | SDA | GPIO 21 |
| MPU6050 | SCL | GPIO 22 |
| MPU6050 | VCC | 3.3V |
| MPU6050 | GND | GND |
| Brake LED | + | GPIO 25 through 220-330 ohm resistor |
| Brake LED | - | GND |
| SD card | — | Use the onboard FNK0047/WROVER-E SD interface |
| Camera | — | Use the onboard camera connector |

The LCD and MPU6050 share the same I2C bus:

```
                 ESP32 WROVER-E
                +---------------+
 GPIO 21 -------+---- SDA -------+--- LCD
                |       +--------+--- MPU6050
 GPIO 22 -------+---- SCL -------+--- LCD
                |       +--------+--- MPU6050
 GPIO 25 -------+-- resistor ----+--- LED +
                |               |
 GND -----------+---------------+--- LCD GND
                |               +--- MPU GND
                |               +--- LED -
                +---------------+
```

### Wiring notes

- Keep the MPU6050 powered from **3.3V**.
- Do not connect the MPU6050 I2C lines directly to 5V.
- Put a **220-330 ohm resistor** in series with the brake LED.
- The LCD and MPU6050 intentionally use the same SDA/SCL lines.
- The SD card and camera use the FNK0047's onboard connectors/interface.

