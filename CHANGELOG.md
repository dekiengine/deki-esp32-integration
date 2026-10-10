# Changelog

Notable changes to `deki-esp32-integration`. Engine and editor changes are in the
[engine changelog](https://github.com/dekiengine/deki-engine/blob/master/CHANGELOG.md).

A package's `minEngine` names the engine version it needs. Before 1.0 a
breaking change bumps the minor across the editor, the engine and every
package together, so a package with no changes of its own is still released
alongside one that has them.

## Unreleased

### Added
- **Screen capture over serial**: the firmware answers the line
  `DEKI:SCREENSHOT` on its console with the frame on screen (base64, with a
  CRC), for the editor's `device_screenshot`. Started at boot
  (`DekiESP32HALInitSystem`, `PACKAGE_HAS_SYSTEM_INIT`); a USB Serial/JTAG
  console is read from its FIFO, a UART one through stdin. A frame takes about
  a second to send; the game waits for it, and that second is not counted as
  frame time, so nothing timed jumps ahead.

### Fixed
- A flash with no port named (MCP's `deploy_build`, the CLI) picks one: the
  only serial port there is, or, with several, the first that answers as an
  ESP32. It failed with "No board found" even with a board plugged in.
- Windows: an installed esptool showed as not installed. The editor unpacks
  its archive straight into `espressif/esptool`, and the toolchain definition
  looked for `esptool.exe` a folder deeper.
- Windows: ESP-IDF's setup and builds work on a machine with no Python of its
  own. The Python component was python.org's embeddable build, which has no
  venv, ensurepip or pip, so ESP-IDF's install.bat could not create its
  environment; and nothing put it on PATH, so install.bat and export.bat found
  only the Microsoft Store's stub and stopped with 9009. It is now the Python
  Software Foundation's full portable build (the `python` package on
  nuget.org, pinned by SHA-256), on PATH for install.bat (`pathEntries`, which
  needs the editor that reads them) and for export.bat in every build. An
  installed copy of the old one shows as not installed; install it again.
- The build's PATH is set in one `set`: a second `set PATH=...%PATH%...` on
  the same cmd line expands %PATH% to the value before the first and dropped
  what it added.
- A flash with no board connected, or with a refused port, fails with the
  reason ("No board found on a serial port..."). It used to stop without a
  result, so the build status stayed "Flashing firmware..." for good.

## 0.18.0

### Changed
- `minEngine` 0.18.0. Reflection ABI 21: the package must be rebuilt.
- **Names follow the code style** (deki-engine/docs/codestyle): types, functions and enum values are PascalCase, constants kPascalCase, members m_PascalCase, locals and parameters camelCase. The code is formatted with clang-format 22.
- The functions the editor finds by name are PascalCase: DekiESP32HALRegisterComponents, DekiESP32HALGetAutoComponentCount, DekiESP32HALEnsureRegistered and the rest. Built against engine ABI 21; a build of this package from before does not load and is rebuilt.
- Renamed: the S3 PIE blit kernels (`S3PIERGB565CopyRow`, ...); `SetWakeGpio(gpioNum, level)`.

### Fixed
- WiFi: a connect fails at once, with the reason, when the WiFi driver does
  not start. It waited out the whole connect timeout first.
- **A flash partition that fails to mount is no longer formatted.** It holds
  the boot payload, the assets and anything the game saved there, and any
  mount fault wiped all of it. Only a partition that was never written is
  formatted now; otherwise the board runs on without `F:/`, nothing on it is
  changed, and the console says to flash the firmware again.
- **HTTP responses are capped at 512 KB**, and a response is dropped (status
  -1, logged) when the board has no room left to hold it. With no limit, a
  large response grew until an allocation failed, which aborts the board.
- I2S: initializing twice leaked the first channel.
- External memory on the classic ESP32 (with PSRAM) works: every allocation
  asked for DMA-capable PSRAM, which only the S2 and S3 have, so all of them
  failed.
- WiFi and Bluetooth initialise NVS first. `esp_wifi_init` needs it, so
  connecting failed.
- Bluetooth starts on first use. It started from a static constructor, before
  FreeRTOS was running, where its wait loop asserts.

### Removed
- The `D:/` path prefix on the SD card. Paths use `S:/`.
- The former names from before 0.16.0 (bare class names, and deki-gpio's
  `DekiEsp32::ESP32PinSetup`). A scene that old is upgraded with 0.17 first.

## 0.17.0

### Added
- `ESP32QemuDisplaySetup` (feature `qemu_display`): the screen Espressif's
  QEMU gives an ESP32-S3 machine, so a firmware build runs its renderer under
  QEMU and a frame can be captured with `screendump`. RGB565 or ARGB8888. See
  the README.
- **Simulate in QEMU** for ESP32 and ESP32-S3 boards: the build keeps the
  board's boot steps, adds the QEMU screen in its own copy of the boot scene,
  moves an SPI SD card to QEMU's SD host, and builds into
  `generated/build/<board>_qemu` (DIO flash, quad PSRAM, UART0 console).
  Deploy is **Run**: QEMU runs with the board's flash size and PSRAM and a
  fresh SD card (the assets, for a target that keeps them on the card), its
  serial output in the build output, until its window is closed. The board's
  boot scene and firmware are not changed. See the README.
- **`ESPIDFGPIO`**, the ESP-IDF pins behind deki-gpio: drive, read, and count
  a pin's edges from an interrupt handler. Registered at start-up like the
  other buses. Requires `deki-gpio`. The boot step that drives a pin, briefly
  here as `ESP32PinSetup`, is deki-gpio's `GpioPinSetup` and works on any
  platform; scenes naming the old one load as the new one.
- `ESPIDFI2C::ReadRaw`: the ESP-IDF side of deki-i2c's register-less read
  (`i2c_master_receive`), which an I2C keyboard needs.
- **Built against ESP-IDF 6.1** (was 5.3.2). The pinned SDK is v6.1, and the
  build now refuses an installed ESP-IDF of any other version with a message
  saying which is installed and which is needed, rather than quietly building
  against headers this package was not written for. Update it from the Build
  panel or with `--install-toolchain esp-idf`.
- The build backend, its toolchain definition and the ESP-IDF component
  installer now ship in this package (`editor/`) instead of the editor. The
  toolchain definition travels inside the backend.
- "Flash" is this backend's deploy step (builder ABI 2): targets are the
  serial ports. The Build panel's chip/flash/PSRAM rows come from here.
- A board's chip, flash size, clock, display driver and bus, and component
  list are this backend's settings (`frameworkOptions`); existing platform
  files load unchanged. PSRAM size is the platform's `externalMemorySize`.
- Depends on the individual `esp_driver_*` components it uses instead of the
  deprecated `driver` umbrella, and no longer on `json`, which nothing here
  used and which ESP-IDF 6 removed.
- DMA-capable external memory is `heap_caps_malloc(SPIRAM | DMA | CACHE_ALIGNED)`,
  ESP-IDF 6's replacement for the removed `esp_dma_malloc`. The old fallback
  (plain `SPIRAM | DMA`) was not cache aligned.

### Changed
- `minEngine` 0.17.0. Reflection ABI 20: the package must be rebuilt.
- The default partition table's data partition (LittleFS, `F:/`) takes the
  rest of the flash instead of 960 KB: about 13 MB on a 16 MB board. A build
  that keeps its assets in internal storage puts them there, and the build
  stops with a message if they do not fit.
- ESP-IDF builds no longer define `DEKI_SCREEN_WIDTH`/`HEIGHT`: the engine takes
  the framebuffer size from the display.

### Fixed
- A changed platform setting (PSRAM, CPU frequency, the board's sdkconfig
  lines) did not reach a board already built: the stale `sdkconfig` removed
  was the old shared `generated/build/esp-idf` one, not the board's own.
- **The SD card can share the display's SPI bus.** `spi_bus_initialize`
  answering that SPI2 is already up used to fail the card; the card now joins
  that bus as one more device, and the pin pre-conditioning (which bit-bangs
  the pins as GPIO) and the bus release on shutdown happen only for a bus the
  card set up itself. The LilyGO T-Deck wires its card, display and radio to
  one bus.
- Bumping a dependency's version in a package (LovyanGFX, say) had no effect:
  ESP-IDF's component lock kept the old commit. A changed component manifest
  now invalidates the lock.
- The generated reflection tables could be compiled before they were
  regenerated, so a change to the generator never took effect.
- `displayBus` is validated by this backend before it reaches a shell line.

## 0.16.0

### Changed
- **Moved into the `DekiEsp32` namespace.** Every component was declared at global
  scope, which made its identity a bare class name - the name a scene file
  stores and the name the registry keys on - so two packages defining one name
  collided there with nothing to tell them apart. Each component carries
  `DEKI_FORMER_NAME` with the name it was saved under before, so existing
  scenes load unchanged and are written back qualified on the next save.
  Code naming these types needs the namespace: `using namespace DekiEsp32;` or a
  qualified name.
- Enum properties are stored by name rather than by number, so appending to an
  enum or reordering one no longer changes what a saved scene means. Files
  written before this still read.
- `minEngine` 0.16.0. Reflection ABI 17: the package must be rebuilt.

## 0.15.0

### Fixed
- Uses the new I2C driver, so a board with a display boots at all.
