# Deki ESP32 Integration

Documentation: https://dekiengine.github.io/deki-esp32-integration/ (components and properties, generated from the code)

ESP32 platform HAL (Hardware Abstraction Layer) for the Deki Engine: serial commands, SD card support via ESP-IDF, memory and filesystem setup.

Part of the [Deki Engine](https://github.com/dekiengine/deki-engine) package ecosystem.

## Namespace

This package's types live in `DekiEsp32`. Scene files store the qualified
name, so a component is `DekiEsp32::SomeComponent` there, and code naming one
needs the namespace:

```cpp
using namespace DekiEsp32;
obj->AddComponent<SomeComponent>();
```

Scenes saved before 0.16.0 used bare names and still load: every component
records what it used to be called, and a save writes the current name.

## Installation

Install via the Package Manager inside the Deki Editor.

## Dependencies

| Dependency | Type |
|---|---|
| `fatfs` | ESP-IDF component (Apache 2.0) |
| `sdmmc` | ESP-IDF component (Apache 2.0) |
| `driver` | ESP-IDF component (Apache 2.0) |

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for details.

Third-party licenses are listed in [NOTICE](NOTICE).
