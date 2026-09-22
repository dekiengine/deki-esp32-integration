# Changelog

Notable changes to `deki-esp32-integration`. Engine and editor changes are in the
[engine changelog](https://github.com/dekiengine/deki-engine/blob/master/CHANGELOG.md).

A package's `minEngine` names the engine version it needs. Before 1.0 a
breaking change bumps the minor across the editor, the engine and every
package together, so a package with no changes of its own is still released
alongside one that has them.

## Unreleased

### Added
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

### Fixed
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
