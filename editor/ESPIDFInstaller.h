#pragma once

#include <deki-editor/PackageContract.h>
#include <filesystem>

namespace fs = std::filesystem;

class ESPIDFInstaller : public IPackageInstaller
{
public:
    ESPIDFInstaller();
    ~ESPIDFInstaller();

    // Also what project files say for a component from Espressif's registry
    // ("source": "espidf"). Do not rename it.
    std::string GetId() const override { return "espidf"; }
    std::string GetPlatformName() const override { return "ESP-IDF"; }

    bool IsApplicable(const std::string& projectPath) const override;
    std::vector<PackageEntry> GetInstalledPackages(const std::string& projectPath) override;
    void Install(const std::string& projectPath, const PackageEntry& pkg, const std::string& version,
                 PackageCallback callback) override;
    void Remove(const std::string& projectPath, const PackageEntry& pkg, PackageCallback callback) override;

    // Core dependency management
    std::vector<CoreDependency> GetCoreDependencies() const override;
    void EnsureCoreDependencies(const std::string& projectPath) override;
    bool IsCoreDependency(const std::string& name) const override;

    // Merges the installed packages' dependencies into idf_component.yml.
    void MergePackageDeps(const std::string& projectPath);

    // Merges a ready list of deps into idf_component.yml, adding or upgrading.
    void MergeDeps(const std::string& projectPath, const std::vector<DependencyInfo>& deps);

private:
    fs::path GetYamlPath(const std::string& projectPath) const;

    bool ParseYaml(const fs::path& path, std::vector<DependencyInfo>& dependencies);

    bool WriteYaml(const fs::path& path, const std::vector<DependencyInfo>& dependencies);
};
