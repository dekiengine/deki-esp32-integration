#pragma once

#include <deki-editor/IconsTabler.h>

#include <deki-editor/build/FirmwareBuilderBase.h>
#include "ESPIDFToolchain.h"
#include <thread>

namespace DekiEditor
{

/**
 * @brief ESP-IDF firmware builder implementation
 *
 * Builds firmware using the Espressif ESP-IDF framework and idf.py tool.
 * Supports ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6, ESP32-H2 targets.
 */
class ESPIDFBuilder : public FirmwareBuilderBase
{
public:
    ESPIDFBuilder();
    ~ESPIDFBuilder() override;

    // Core operations — use RunOnBuildThread()
    void Build(const std::string& projectPath, BuildOutputCallback outputCallback = nullptr,
               BuildProgressCallback progressCallback = nullptr) override;
    void Clean(const std::string& projectPath, BuildOutputCallback outputCallback = nullptr,
               BuildProgressCallback progressCallback = nullptr) override;

    // Deploy = write the image over a serial port. The target id is the port;
    // empty lets idf.py find one.
    bool SupportsDeploy() const override { return true; }
    const char* GetDeployLabel() const override { return "Flash"; }
    std::vector<DeployTarget> EnumerateDeployTargets() const override;
    void Deploy(const std::string& projectPath, const std::string& port,
                BuildOutputCallback outputCallback = nullptr,
                BuildProgressCallback progressCallback = nullptr) override;
    std::vector<std::pair<std::string, std::string>> DescribePlatform(const PlatformConfig& config) const override;

    // Toolchain — ESP-IDF specific
    bool IsToolchainInstalled() const override;
    std::string GetToolchainStatus() const override;
    // Build file generation
    bool GenerateBuildFiles(const std::string& projectPath,
                            const PlatformConfig& config,
                            const std::vector<std::string>& packageDefines) override;

    // Identity
    const char* GetName() const override { return "ESP-IDF"; }
    // Shown in the platform editor's framework picker. The editor used to
    // hold these strings for the backends it shipped; a backend describes
    // itself now, so one it has never heard of is not anonymous.
    const char* GetIcon() const override { return ICON_TI_CPU; }
    const char* GetDescription() const override
    {
        return "Build for ESP32, ESP32-S3 and other Espressif chips";
    }

    // The generated project packs this directory into the data partition.
    std::string GetBootPayloadDirectory(const std::string& projectPath) const override
    {
        return GetBuildDirectory(projectPath) + "/spiffs_data";
    }
    std::string GetFrameworkId() const override { return "espidf"; }
    std::vector<std::string> ValidatePlatform(const PlatformConfig& config) const override;
    std::string GetBuildDirectory(const std::string& projectPath) const override;

    // Platform editor UI
    std::unique_ptr<IPlatformEditorUI> CreateEditorUI(const PlatformConfig& config) const override;

    // ESP-IDF specific public methods
    std::string GetIDFPath() const;

private:
    ESPIDFToolchain m_Toolchain;

    // Build execution context helper
    ESPIDFExecContext MakeExecContext();

    // Internal worker functions
    void DoBuild(const std::string& projectPath, BuildOutputCallback outputCallback,
                 BuildProgressCallback progressCallback);
    void DoFlash(const std::string& projectPath, const std::string& port,
                 BuildOutputCallback outputCallback, BuildProgressCallback progressCallback);
    void DoClean(const std::string& projectPath, BuildOutputCallback outputCallback,
                 BuildProgressCallback progressCallback);

    // Build file generation helpers
    bool GenerateRootCMakeLists(const std::string& espIdfPath, const PlatformConfig& config);
    bool GenerateMainCMakeLists(const std::string& mainPath, const std::string& projectPath,
                                const PlatformConfig& config,
                                const std::vector<std::string>& packageDefines);
    bool GenerateSdkConfigDefaults(const std::string& boardPath, const PlatformConfig& config);
    bool GeneratePartitionsCsv(const std::string& boardPath, const PlatformConfig& config);
};

}  // namespace DekiEditor
