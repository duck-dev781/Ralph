This folder is the firmware portion of an update package.
The ESP32 cannot flash an .ino source file directly. GitHub Actions compiles Ralph.ino into Ralph-OTA.bin and places the binary here when a release package is built.
