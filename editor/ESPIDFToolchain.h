#pragma once

#include <deki-editor/build/TargetBuilder.h>
#include <string>
#include <vector>
#include <atomic>

namespace DekiEditor
{

/// The platform's "displayBus" option is an identifier: A-Z, 0-9 and '_'. It
/// reaches a shell line, so both users of it check. Defined in ESPIDFBuilder.cpp.
bool IsValidDisplayBus(const std::string& bus);

struct PlatformConfig;

/// Context passed to ExecuteIDF from the builder's state
struct ESPIDFExecContext
{
    bool enableLogging;
    bool enableInternalLogging;
    bool hasPlatformConfig;
    const PlatformConfig* platformConfig;
    const std::vector<std::string>* packageDefines;
    std::atomic<bool>& cancelRequested;
};

/**
 * @brief ESP-IDF toolchain path detection and command execution
 *
 * Handles IDF path discovery, toolchain status checks, and
 * executing idf.py commands with the correct environment.
 */
class ESPIDFToolchain
{
public:
    // Path discovery
    std::string GetIDFPath() const;
    std::string GetToolchainsDir() const;
    std::string GetDekiEditorDir() const;

    // Toolchain status
    bool IsInstalled() const;
    std::string GetStatus() const;

    // Read engine version from deki.json
    std::string ReadEngineVersion(const std::string& projectPath) const;

    // Execute IDF command (sources export.bat/export.sh, sets env vars)
    int ExecuteIDF(const std::string& command, const std::string& workDir,
                   const std::string& enginePath, BuildOutputCallback outputCallback,
                   const ESPIDFExecContext& ctx);

    // Build preparation — delete obj to refresh timestamp
    void PrepareForBuild(BuildOutputCallback outputCallback, const std::string& buildDir);
};

}  // namespace DekiEditor
