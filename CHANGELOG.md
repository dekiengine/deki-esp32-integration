# Changelog

Notable changes to `deki-esp32-integration`. Engine and editor changes are in the
[engine changelog](https://github.com/dekiengine/deki-engine/blob/master/CHANGELOG.md).

A package's `minEngine` names the engine version it needs. Before 1.0 a
breaking change bumps the minor across the editor, the engine and every
package together, so a package with no changes of its own is still released
alongside one that has them.

## 0.40.0

### Added
- `DEKI:PROFILE` also reports each object in the scene, most expensive first:
  a `DEKI:PROFILE:OBJECT` line with its path, type, sorting order, its share
  of finding what changed, of making and drawing its pixels, how many times it
  was drawn and why (new, changed, still, overPin), per frame; the first 64,
  then `DEKI:PROFILE:OBJECTS` with how many there were.

### Changed
- `minEngine` 0.40.0: built against engine 0.40.0. A build of this package
  from before does not load and is rebuilt.
- Builds keep the WiFi driver's code in flash (`CONFIG_ESP_WIFI_IRAM_OPT=n`,
  `CONFIG_ESP_WIFI_RX_IRAM_OPT=n`): 18.5 KB more internal RAM for the game,
  the display and the radio's own buffers, whether WiFi is on or not.
  Measured on a T-Deck Plus: 1 MB downloads over a phone hotspot averaged
  140 KB/s against 149 KB/s (the ranges overlap), and frames cost the same
  while WiFi was busy. A board that wants the driver's code in internal RAM
  again sets both to `y` in its platform's `sdkconfig` list, which comes
  last. Existing projects get it on their next build: the builder regenerates
  a stale `sdkconfig` when its defaults change.
- Builds also keep the radio's PHY code and libc's locks in flash
  (`CONFIG_ESP_PHY_IRAM_OPT=n`, `CONFIG_LIBC_LOCKS_PLACE_IN_IRAM=n`): 1 KB
  more internal RAM. Frames on a T-Deck Plus cost the same (19.7 ms against
  19.6 on a farm map, 32.9 against 32.9 with 16 sprites walking). The heap's
  functions stay in internal RAM: in flash they cost about 5.5 ms a frame.
- ESP32-S3 builds have a 32 KB instruction cache
  (`CONFIG_ESP32S3_INSTRUCTION_CACHE_32KB=y`; ESP-IDF's default is 16 KB). A
  frame runs more code than 16 KB holds, and how often it missed depended on
  where a build placed the code: the same scene moved by a millisecond
  between unrelated builds. Measured on a T-Deck Plus with a scrolling farm:
  19.0 to 16.9 ms a frame, 21.9 to 18.5 ms with 4 sprites walking, 30.6 to
  26.0 ms with 16, and builds within 0.2 ms of each other. It takes 16 KB of
  internal RAM; a board that needs it back sets
  `CONFIG_ESP32S3_INSTRUCTION_CACHE_16KB=y` in its `sdkconfig` list.
- Boards with PSRAM keep the static data ESP-IDF's libraries allow there in
  PSRAM (`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`): 14.4 KB of WiFi,
  lwIP and network tables in a T-Deck build, which only WiFi touches.
- Allocations through `operator new`, `std::vector` and `std::string` go to
  PSRAM once DMA-capable internal RAM, not all internal RAM, is down to
  16 KB. An ESP32-S3's RTC fast memory (about 8 KB) is internal but no
  driver's DMA reaches it, so the old count let a scene's objects leave
  drivers almost nothing. With these three changes, a T-Deck Plus running the
  farm in whole frames keeps 14.9 KB of DMA-capable RAM free (largest block
  13.8 KB), room for an SD card and audio started mid-game.

### Fixed
- Choosing ESP-IDF in the New Platform dialog no longer crashes the panel: the package now takes the editor's ImGui context.

## 0.30.0

### Changed
- `minEngine` 0.30.0: built against engine 0.30.0. A build of this package
  from before does not load and is rebuilt.

### Fixed
- A firmware build from the editor runs ESP-IDF. On a machine whose PATH
  holds an entry with a stray quote (a Windows Kits installer leaves one), the
  build ran nothing and reported success: the variables it sets were `set`
  lines in one cmd line, and the quote, expanded from %PATH%, closed one early
  and turned the rest of the line, export.bat and idf.py included, into a
  value. They are the build's environment now, not part of its command, and
  such an entry's quote is left out of the build's PATH, which the build says,
  naming the entry (otherwise cmd finds nothing after it, and export.bat
  reports git missing).
- A build is a success only when it leaves the firmware
  (`build/DekiGame.bin`): one that exits 0 without it fails, saying so. A
  build with nothing to rebuild says the firmware was already up to date. The
  build output names the firmware file.
- ESP-IDF runs with `PYTHONUTF8=1`, as idf.py asks for on a console that is
  not UTF-8.

## 0.22.1

### Fixed
- Small allocations (operator new, std::vector, std::string) leave 16 KB of
  internal RAM free and go to PSRAM after that. ESP-IDF kept them internal
  until none was left, so a scene of many small objects (the demo's Widgets
  on the T-Deck) left 39 bytes, and a task stack, DMA buffer or driver asking
  for internal RAM afterwards failed.

## 0.22.0

### Added
- A board answers `DEKI:MEMORY` on its serial console with where its memory
  is: internal and external RAM in use by the engine with their peaks, the
  heap's free space, lowest free space since boot and size, and one
  `DEKI:MEMORY:SITE` line per call site that allocated through the engine. The
  editor's Memory panel and its `device_memory` MCP tool read it.

### Changed
- Firmware builds and flashes run at below-normal priority and with as many
  compile jobs as free memory holds (needs editor 0.22).
- `minEngine` 0.22.0: built against engine 0.22.0. A build of this package
  from before does not load and is rebuilt.

## 0.21.0

### Added
- The clock gives microseconds (`GetTicksUs`), so the engine times frames
  finer than a millisecond.

### Changed
- `minEngine` 0.21.0: built against engine 0.21.0. A build of this package
  from before does not load and is rebuilt.
- **Scenes open faster.** The game's data (the boot scene, `project_data.bin`
  and, on internal storage, the assets) is flashed as one read-only image in a
  `gamedata` partition and read where it lies, instead of from LittleFS. Finding
  a file took 6 to 13 ms on a T-Deck Plus and now takes microseconds: the first
  visit to the demo's meadow loads its assets in 35 ms instead of 615 ms, and
  flashing the data takes seconds, not a 13 MB image. Needs engine with
  `Deki::FileImage`.
- **Games can write files.** `F:/` files the game writes (saves, settings) go
  to a `userdata` LittleFS partition at the end of the flash: 1 MB on 16 MB,
  512 KB on 8 MB, 128 KB on 4 MB. Flashing never touches it, so they survive
  updates. A file of the game's data is never written: opening one to write
  fails and logs why. A board with its own `partitionTable` needs a `gamedata`
  partition; the build says so.
- The partitions are mounted on first use rather than in a static constructor,
  so what they log (a missing or damaged image) reaches the console.

## 0.20.0

### Changed
- `minEngine` 0.20.0: built against engine 0.20.0. A build of this package from before does not load and is rebuilt.

### Added
- `ESPIDFGPIO::WaitForEdge`: the edge interrupt that counts a pin's edges
  also gives the pin's semaphore, made the first time a task waits on it.
- `ESPIDFGPIO::TimeSinceEdge`: the edge interrupt also notes when it ran.
- The present worker gives deki-display a microsecond clock (esp_timer).
- `ESPIDFSPIDevice` reads at the device's `readClockHz` through a second
  driver handle at that clock. The two share the device's chip select, which
  the driver callbacks then drive themselves, low as a transfer starts and
  high after it unless the transfer keeps it; a read lets the bus go from
  the other handle and takes it back. `threeWire` sets `SPI_DEVICE_3WIRE`.
- deki-display's parallel panel bus on ESP-IDF's esp_lcd i80 driver (the
  ESP32-S3's LCD peripheral, I2S on an ESP32), 8 or 16 data lines,
  registered as deki-display's `ParallelBus` factory with the present worker.
  Pixels are queued for DMA and counted back as each transfer finishes; the
  few bytes at either end of a part that are off the DMA's alignment go out
  as parameter bytes. Brings ESP-IDF's `esp_lcd` component. Built, not yet
  run on a board.
- `ESPIDFSPIBus` and `ESPIDFSPIDevice`: deki-spi on ESP-IDF's `spi_master`,
  registered as deki-spi's factory. Long writes are queued and go out by DMA
  back to back; the data/command pin is set in the driver's pre-transfer
  callback as each transfer starts; chip select can stay low across a run of
  transfers while the bus is held. A bus another driver already started is
  joined. Optional, as the `spi` feature: installing this package does not
  bring deki-spi; the board that uses it does. A transfer of four bytes or less travels in the
  transaction itself, queued or not.
- deki-display's present worker: a FreeRTOS task pinned to the core the
  engine is not on, which sends a frame while the next is drawn. None on a
  single-core chip. Optional, as the `display_worker` feature: installing this
  package does not bring deki-display; the board that uses it does, so a
  project on LovyanGFX gets no new packages.
- `ESPIDFI2C::WriteRead` and `WriteRaw`, deki-i2c's reads and writes for
  devices with wider registers (`i2c_master_transmit_receive`,
  `i2c_master_transmit`).
- Big-endian RGB565 (`DEKI_RGB565_BIG_ENDIAN`): `DEKI:SCREENSHOT` names the format
  `RGB565_BE` or `RGB565A8_BE`. `ESP32QemuDisplay` refuses such a build: the
  QEMU panel is little-endian.
- A failed allocation prints its size, caps and caller, the internal and
  DMA-capable heaps' free bytes, largest block and low-water mark, and how
  much the engine placed in each region. ESP-IDF aborts when it cannot make
  a FreeRTOS lock, and the abort alone does not say memory ran out.
- `DEKI:PROFILE` names each component type and adds a
  `DEKI:PROFILE:PATH` line per blitter path, with nanoseconds per pixel.
- `DEKI:PROFILE:OFF` switches the render profiler off again. A report leaves
  it on, and it times every draw, which costs a few frames a second, so a
  measurement after profiling read low until the board restarted.

## 0.19.0

### Changed
- `minEngine` 0.19.0: built against engine 0.19.0, whose component metadata records the package each class is in. A build of this package from before does not load and is rebuilt.

### Added
- Japanese, Simplified Chinese and Korean: `editor/lang/ja.json`, `zh.json`
  and `ko.json` translate the names, tooltips and enum values this
  package's components show in the Inspector.
- The package's editor text goes through the editor's translation (`Tr`), so
  it shows in the editor's language. Needs the editor release that adds
  `Localization.h`; raise `minEngine` to it when releasing.

## 0.18.2

### Added
- **Frame statistics over serial**: the line `DEKI:STATS` is answered with
  the frames per second and the average frame, update, render and present
  times over the last second, and the free internal and external memory, for
  the editor's `device_stats`.
- **Screen capture over serial**: the firmware answers the line
  `DEKI:SCREENSHOT` on its console with the frame on screen (base64, with a
  CRC), for the editor's `device_screenshot`. Started at boot
  (`DekiESP32HALInitSystem`, `PACKAGE_HAS_SYSTEM_INIT`); a USB Serial/JTAG
  console is read from its FIFO, a UART one through stdin. A frame takes about
  a second to send; the game waits for it, and that second is not counted as
  frame time, so nothing timed jumps ahead.
- `DEKI:PROFILE` turns on deki-rendering's render profiler and then reports
  where the render time went since the request before, per component type.
  `DEKI:SCENE <guid>` opens that scene, for measuring each scene without
  pressing through the game.

### Changed
- `minEngine` 0.18.2: the frame statistics read the engine's
  `GetLastFrameTimings`.
- ESP32-S3 builds use 64-byte data cache lines, which make a frame in PSRAM
  quicker to draw and send. The cache stays 32 KB: 64 KB takes 32 KB more of
  internal RAM, which a scene with many sprites ran out of.

### Fixed
- **A UART ignored its `baudRate` and ran at 9600.** Only the key `baud` was
  read, and deki-gps and the components name it `baudRate`, so a GPS sending
  at 38400 (the T-Deck Plus's) was never understood. Both keys are read.
- A console reply's last bytes stayed on the board until the next reply
  pushed them out, so `device_stats` said the board did not answer. The USB
  Serial/JTAG console sends what it holds when flushed, and a flush made
  while the packet before is still going out is lost; it is now made again
  once the console takes it.
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
