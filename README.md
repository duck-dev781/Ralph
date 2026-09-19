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
