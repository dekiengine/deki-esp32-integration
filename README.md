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

## Pins

The ESP-IDF side of `deki-gpio`: drive, read and count a pin's edges by
interrupt. The boot step that drives a pin, `GpioPinSetup`, is in `deki-gpio`.

## Running under QEMU

Tick **Simulate in QEMU** in the Build panel (or pass `--simulate` with
`--build-firmware` and `--deploy`) to build a board's firmware for QEMU and
run it there. The board's `boot.scene`, its build and its firmware are not
changed; the simulated build goes to `generated/build/<board>_qemu`, from its
own copy of the boot scene:

- Every board step is kept, and `ESP32QemuDisplaySetup` is added after them,
  so the game draws to QEMU's screen at the board's size.
- An SD card wired for SPI is moved to QEMU's SD host (SDMMC 1-bit, CMD on
  the MOSI pin, D0 on the MISO pin). QEMU has no SPI controller for it.
- The flash is read in DIO at 40 MHz, PSRAM is quad (QEMU crashes with an
  octal PSRAM build), and the console is on UART0.

Deploy becomes **Run**: QEMU opens a window with the screen and the board's
flash size and PSRAM, and the board's serial output shows in the build
output. It runs until the window is closed or the run is cancelled. Each Run
gets a fresh SD card: it holds the game's assets when the target keeps them
on the card, and is empty otherwise.

QEMU has no I2C or SPI devices, so a touch panel or an I2C keyboard is not
found, as on a board without one.

`ESP32QemuDisplaySetup` (width, height, RGB565 or ARGB8888) drives the screen
Espressif's QEMU gives an ESP32-S3 machine and no real chip has. A platform
made only for QEMU can put it in its own boot scene. The monitor's
`screendump <file>.ppm` saves the screen when QEMU runs with `-display none`.
On a real chip the setup fails, saying there is no QEMU panel.

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
