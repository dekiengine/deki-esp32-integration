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

/// The builder state ExecuteIDF needs.
struct ESPIDFExecContext
{
    bool enableLogging;
    bool enableInternalLogging;
    bool hasPlatformConfig;
    const PlatformConfig* platformConfig;
    const std::vector<std::string>* packageDefines;
    std::atomic<bool>& cancelRequested;
};

/// Finds the ESP-IDF install, checks its status and runs idf.py commands
/// in the right environment.
class ESPIDFToolchain
{
public:
    // Path discovery
    std::string GetIDFPath() const;
    std::string GetToolchainsDir() const;
    std::string GetDekiEditorDir() const;

    /// Installed means the SDK is there AND is the version this backend
    /// pins; any other version builds against headers this code was not
    /// written for, so it is reported, not used.
    bool IsInstalled() const;
    std::string GetStatus() const;

    /// The version the toolchain definition pins ("v6.1"), from ESPIDFBuilder.
    void SetRequiredVersion(const std::string& version) { m_RequiredVersion = version; }

    /// The installed SDK's version, from its own tools/cmake/version.cmake,
    /// so it is right for any install, whoever made it. Empty when there is
    /// no SDK.
    std::string InstalledVersion() const;

    /// The engine version from the project's deki.json.
    std::string ReadEngineVersion(const std::string& projectPath) const;

    /// Runs an idf.py command after sourcing export.bat/export.sh and setting
    /// the build's environment variables.
    int ExecuteIDF(const std::string& command, const std::string& workDir, const std::string& enginePath,
                   BuildOutputCallback outputCallback, const ESPIDFExecContext& ctx);

    /// Deletes an object file before the build so its timestamp is refreshed.
    void PrepareForBuild(BuildOutputCallback outputCallback, const std::string& buildDir);

private:
    std::string m_RequiredVersion;
};

}  // namespace DekiEditor
