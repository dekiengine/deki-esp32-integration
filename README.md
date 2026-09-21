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

## Boot pins

`ESP32PinSetup` drives one GPIO high or low during boot, with an optional wait
afterwards. Boards gate things behind a pin: the rail feeding the display, or
the chip select of another device on the display's SPI bus. Add one per pin to
the platform's boot scene and list it in `setupComponents` ahead of the step
that needs it.

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
