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

## Chips

A platform names its chip in `idfTarget`: `esp32`, `esp32s2`, `esp32s3`,
`esp32c3`, `esp32c6`, `esp32h2` or `esp32p4`.

**ESP32-P4: runs on the Elecrow CrowPanel Advanced 9" (chip v1.3).** What differs from the other
chips:

- PSRAM runs at 200 MHz in its one mode (16 lines); `psramMode` does not apply.
- `cpuFreqHz` is 360 or 400 MHz, the two speeds ESP-IDF has for it; any
  other is refused. With none set, ESP-IDF picks the chip revision's default.
- A build is for chip revisions 3.x unless the platform's `sdkconfig` list
  has `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`; ESP-IDF cannot make one image
  for both, and a 3.x image does not boot on a 1.x chip.
- The partition table is at 0x9000, not 0x8000: the P4's bootloader starts
  at 0x2000 and does not fit before 0x8000. The default table's NVS is 16 KB.
- The P4 has no radio. Its WiFi goes through the board's WiFi chip (an
  ESP32-C6, say) over Espressif's `esp_hosted`, when the platform names that
  chip in `wifiCoprocessor` and the game uses deki-wifi: the build then gets
  `esp_hosted` (3.1) and the platform's `wifiCoprocessorSdkconfig` lines, the
  link's pins and reset. A game without WiFi gets neither. The link takes
  about 114 KB of internal RAM. A 3.x `esp_hosted` works with the 2.x
  firmware boards ship on their WiFi chip; a project can pin another version
  in `deki-packages.json`'s `dependencies`. Bluetooth is not available.
- No QEMU.
- Large blits draw their lower rows on the second core, and large plain copies (a map's view into the frame) go to the AXI DMA, which does not read the destination before writing it as the CPU's cache does. A copy over whole rows of the frame goes on while the game draws, each blit waiting only for the rows it touches.
- deki-display's MIPI-DSI panels run on its DSI. Its LDO regulators are
  turned on from the boot scene with `ESP32LdoSetup` (channel, millivolts);
  the DSI PHY needs channel 3 at 2.5 V before the panel.

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

## Community

Questions, ideas and what you made with Deki: [the Deki Discord](https://dekiengine.com/discord/).

## License

Apache 2.0. See [LICENSE](LICENSE).

Third-party licenses are listed in [NOTICE](NOTICE).
