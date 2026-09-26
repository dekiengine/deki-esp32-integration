# Deki ESP32 Integration

Docs: https://dekiengine.github.io/deki-esp32-integration/ (components and properties, generated from the code)

ESP32 platform HAL (Hardware Abstraction Layer) for the Deki Engine: serial commands, SD card support via ESP-IDF, memory and filesystem setup.

Part of [Deki Engine](https://github.com/dekiengine/deki-engine).

## Namespace

Types live in `DekiEsp32`. Scene files store the qualified name, and so does code:

```cpp
using namespace DekiEsp32;
obj->AddComponent<SomeComponent>();
```

Scenes saved before 0.16.0 used bare names and still load; saving writes the current one.

## Pins

The ESP-IDF side of `deki-gpio`: drive, read and count a pin's edges by
interrupt. The boot step that drives a pin, `GpioPinSetup`, is in `deki-gpio`.

## Running under QEMU

Espressif's QEMU gives an ESP32-S3 machine a screen no real chip has. Put
`ESP32QemuDisplaySetup` (width, height, RGB565 or ARGB8888) in a QEMU
platform's boot scene in place of a board's display, and a firmware build
renders into it: QEMU shows it in a window, or saves it with the monitor's
`screendump <file>.ppm` when it runs with `-display none`. On a real chip the
setup fails, saying there is no QEMU panel.

Use the QEMU that ESP-IDF 6.1 recommends (`esp_develop_9.2.2_20260417`); the
older 9.0.0 build does not find the PSRAM. For PSRAM, launch it yourself with
`-m 8M`: the `-m 32M` that `idf.py qemu` passes leaves no room to map the flash.

## Install

Package Manager in the Deki Editor, or `DekiEditor --packages-add deki-esp32-integration <project>`.

## Dependencies

| Dependency | Type |
|---|---|
| `deki-wifi` | Deki package |
| `deki-ble` | Deki package |
| `deki-http` | Deki package |
| `deki-i2s` | Deki package |
| `deki-uart` | Deki package |
| `deki-i2c` | Deki package |
| `deki-sdcard` | Deki package |
| `deki-rendering` | Deki package |
| `fatfs` | ESP-IDF component (Apache 2.0) |
| `sdmmc` | ESP-IDF component (Apache 2.0) |
| `driver` | ESP-IDF component (Apache 2.0) |

## License

Apache 2.0. See [LICENSE](LICENSE).

Third-party licenses are listed in [NOTICE](NOTICE).
