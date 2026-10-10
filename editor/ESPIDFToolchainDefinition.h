#pragma once

// The ESP-IDF toolchain definition: what the SDK's pieces are, where each is
// downloaded from, the digest each download is pinned to, and how an installed
// copy is recognised.
//
// Compiled into the backend, not read from a file: this package's editor DLL
// is built into the project's runtime directory, far from these sources, and
// the editor itself carries no framework's recipe.
//
// It is JSON so it reads as the data it is; ToolchainComponentManager parses it
// through DekiEditor::ParseBuilderDefinition.
//
// Python on Windows is the Python Software Foundation's full portable build,
// published as the `python` package on nuget.org (a signed zip). ESP-IDF's
// install.bat creates a virtual environment and installs into it with pip;
// python.org's embeddable build has no venv, ensurepip or pip, so it cannot.
// Its pathEntries put it on PATH for install.bat, and ESPIDFToolchain does the
// same for export.bat, since a fresh Windows has no python but the Microsoft
// Store's stub, which exits with 9009.

namespace DekiEditor
{

inline constexpr const char* kESPIDFToolchainDefinition = R"json(
{
  "id": "espidf",
  "name": "ESP-IDF",
  "family": "espressif",
  "supportedTargets": ["esp32", "esp32s2", "esp32s3", "esp32c3", "esp32c6", "esp32h2"],
  "buildDirectory": "builders/esp-idf",
  "debug": {
    "gdb": {
      "searchPaths": [
        "~/.espressif/tools/xtensa-esp-elf-gdb/*/xtensa-esp-elf-gdb/bin",
        "~/.espressif/tools/riscv32-esp-elf-gdb/*/riscv32-esp-elf-gdb/bin"
      ],
      "binaryPattern": "${toolchainPrefix}gdb"
    },
    "server": {
      "searchPaths": [
        "~/.espressif/tools/openocd-esp32/*/openocd-esp32/bin"
      ],
      "binary": "openocd",
      "scriptsDir": "../share/openocd/scripts"
    },
    "boardConfigs": {
      "builtin-jtag": "board/${idfTarget}-builtin.cfg",
      "ftdi": "board/${idfTarget}-ftdi.cfg",
      "esp-prog": "board/${idfTarget}-bridge.cfg"
    },
    "defaultBoardConfig": "builtin-jtag",
    "elf": "build/${platformId}/build/DekiGame.elf",
    "setupCommands": [
      "set remote hardware-watchpoint-limit 2",
      "mon reset halt",
      "flushregs"
    ]
  },
  "components": [
    {
      "id": "esp-idf",
      "displayName": "ESP-IDF SDK",
      "required": true,
      "canInstall": true,
      "canSetup": true,
      "installPath": "espressif/esp-idf",
      "versionCheck": {
        "type": "github_release",
        "repo": "espressif/esp-idf",
        "assetPattern": ".zip"
      },
      "fallback": {
        "version": "v6.1",
        "url": "https://github.com/espressif/esp-idf/releases/download/v6.1/esp-idf-v6.1.zip",
        "sha256": "cdeea7db47b90064ef185b2a1f1b33d17bcb13469a9f8cc20e06c4c4cdb4cc16"
      },
      "detection": {
        "windows": "{installPath}/export.bat",
        "unix": "{installPath}/export.sh"
      },
      "postInstall": {
        "windows": "{installPath}/install.bat esp32s3",
        "unix": "{installPath}/install.sh esp32s3"
      },
      "tooltip": "Espressif IoT Development Framework"
    },
    {
      "id": "python",
      "displayName": "Python",
      "required": true,
      "canInstall": true,
      "canSetup": false,
      "installPath": "espressif/python",
      "versionCheck": {
        "type": "static",
        "version": "3.11.9"
      },
      "fallback": {
        "version": "3.11.9",
        "url": "https://www.nuget.org/api/v2/package/python/3.11.9",
        "sha256": "9283876d58c017e0e846f95b490da3bca0fc0a6ee1134b2870677cfb7eec3c67"
      },
      "detection": {
        "windows": "{installPath}/tools/python.exe",
        "unix": "{installPath}/bin/python3"
      },
      "pathEntries": {
        "windows": ["{installPath}/tools", "{installPath}/tools/Scripts"]
      },
      "tooltip": "Python interpreter (needed by ESP-IDF)"
    },
    {
      "id": "esptool",
      "displayName": "esptool",
      "required": false,
      "canInstall": true,
      "canSetup": false,
      "installPath": "espressif/esptool",
      "versionCheck": {
        "type": "github_release",
        "repo": "espressif/esptool",
        "assetPattern": "win64.zip"
      },
      "fallback": {
        "version": "v4.8.1",
        "url": "https://github.com/espressif/esptool/releases/download/v4.8.1/esptool-v4.8.1-win64.zip",
        "sha256": "2483d409e241d8826ae0ff023eecf31a7d4de6c10ca5ee855b1420cdfd53aaf6"
      },
      "detection": {
        "windows": "{installPath}/esptool.exe",
        "unix": "espressif/esptool/*/esptool.py"
      },
      "tooltip": "Flash tool for ESP32 devices"
    }
  ]
}
)json";

}  // namespace DekiEditor
