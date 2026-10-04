#include "ESPIDFInstaller.h"
#include <deki-editor/Paths.h>
#include <deki-editor/build/ProjectPaths.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <deki-editor/VersionCompare.h>

ESPIDFInstaller::ESPIDFInstaller() = default;
ESPIDFInstaller::~ESPIDFInstaller() = default;

fs::path ESPIDFInstaller::GetYamlPath(const std::string& projectPath) const
{
    // build/esp-idf/main/idf_component.yml
    fs::path buildPath = DekiEditor::ProjectPaths::Build(projectPath) / "esp-idf" / "main" / "idf_component.yml";
    if (fs::exists(buildPath))
    {
        return buildPath;
    }

    // Created when needed.
    return buildPath;
}

bool ESPIDFInstaller::IsApplicable(const std::string& projectPath) const
{
    fs::path yamlPath = GetYamlPath(projectPath);
    return fs::exists(yamlPath) || fs::exists(DekiEditor::ProjectPaths::Build(projectPath) / "esp-idf");
}

bool ESPIDFInstaller::ParseYaml(const fs::path& path, std::vector<DependencyInfo>& dependencies)
{
    if (!fs::exists(path))
    {
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open())
    {
        return false;
    }

    std::string line;
    bool inDependencies = false;
    DependencyInfo currentDep;
    bool hasCurrentDep = false;

    // A small parser for exactly this YAML shape:
    // dependencies:
    //   namespace/name:           # Registry package
    //     version: "x.x.x"
    //   PackageName:              # GitHub package
    //     git: https://github.com/owner/repo.git
    //     version: "x.x.x"

    while (std::getline(file, line))
    {
        if (line.find("dependencies:") != std::string::npos)
        {
            inDependencies = true;
            continue;
        }

        if (!inDependencies)
        {
            continue;
        }

        // A new top-level key ends the dependencies section.
        if (!line.empty() && line[0] != ' ' && line[0] != '\t')
        {
            break;
        }

        // A dependency name: 2-space indent, ends with ':'
        if (line.size() > 2 && line[0] == ' ' && line[1] == ' ' && line[2] != ' ')
        {
            // Keep the previous dependency if it is complete.
            if (hasCurrentDep && !currentDep.name.empty() && !currentDep.version.empty())
            {
                dependencies.push_back(currentDep);
            }

            currentDep = DependencyInfo{};
            hasCurrentDep = true;

            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos)
            {
                currentDep.name = line.substr(2, colonPos - 2);
                currentDep.name.erase(0, currentDep.name.find_first_not_of(" \t"));
                currentDep.name.erase(currentDep.name.find_last_not_of(" \t") + 1);
            }
        }

        // git line (4-space indent)
        if (line.size() > 4 && line.substr(0, 4) == "    " && line.find("git:") != std::string::npos)
        {
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos && hasCurrentDep)
            {
                std::string gitUrl = line.substr(colonPos + 1);
                gitUrl.erase(0, gitUrl.find_first_not_of(" \t"));
                gitUrl.erase(gitUrl.find_last_not_of(" \t") + 1);
                currentDep.gitUrl = gitUrl;
                currentDep.source = PackageSourceType::GitHub;
            }
        }

        // version line (4-space indent)
        if (line.size() > 4 && line.substr(0, 4) == "    " && line.find("version:") != std::string::npos)
        {
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos && hasCurrentDep)
            {
                std::string version = line.substr(colonPos + 1);
                version.erase(0, version.find_first_not_of(" \t\"'"));
                version.erase(version.find_last_not_of(" \t\"'") + 1);
                currentDep.version = version;
            }
        }
    }

    // The last dependency
    if (hasCurrentDep && !currentDep.name.empty() && !currentDep.version.empty())
    {
        dependencies.push_back(currentDep);
    }

    return true;
}

bool ESPIDFInstaller::WriteYaml(const fs::path& path, const std::vector<DependencyInfo>& dependencies)
{
    fs::create_directories(path.parent_path());

    std::ofstream file(path);
    if (!file.is_open())
    {
        return false;
    }

    file << "# Deki Game - Component Dependencies\n";
    file << "dependencies:\n";

    for (const auto& dep : dependencies)
    {
        file << "  " << dep.name << ":\n";
        if (dep.source == PackageSourceType::GitHub && !dep.gitUrl.empty())
        {
            file << "    git: " << dep.gitUrl << "\n";
        }
        file << "    version: \"" << dep.version << "\"\n";
    }

    return file.good();
}

std::vector<PackageEntry> ESPIDFInstaller::GetInstalledPackages(const std::string& projectPath)
{
    std::vector<PackageEntry> packages;

    fs::path yamlPath = GetYamlPath(projectPath);
    std::vector<DependencyInfo> dependencies;

    if (!ParseYaml(yamlPath, dependencies))
    {
        return packages;
    }

    for (const auto& dep : dependencies)
    {
        PackageEntry pkg;
        pkg.name = dep.name;
        pkg.displayName = dep.name;
        pkg.source = dep.source;

        if (dep.source == PackageSourceType::GitHub)
        {
            // GitHub package: the ID is owner/repo from the git URL, e.g.
            // "https://github.com/lovyan03/LovyanGFX.git" -> "github:lovyan03/LovyanGFX"
            std::string ownerRepo = dep.gitUrl;
            if (ownerRepo.size() > 4 && ownerRepo.substr(ownerRepo.size() - 4) == ".git")
            {
                ownerRepo = ownerRepo.substr(0, ownerRepo.size() - 4);
            }
            size_t githubPos = ownerRepo.find("github.com/");
            if (githubPos != std::string::npos)
            {
                ownerRepo = ownerRepo.substr(githubPos + 11);  // past "github.com/"
            }
            pkg.id = "github:" + ownerRepo;
            pkg.url = dep.gitUrl;
            // The URL is shown without its .git suffix.
            if (pkg.url.size() > 4 && pkg.url.substr(pkg.url.size() - 4) == ".git")
            {
                pkg.url = pkg.url.substr(0, pkg.url.size() - 4);
            }
        }
        else
        {
            // Registry package
            pkg.id = "espidf:" + dep.name;
            // Display name from the package name ("owner/PackageName" -> "PackageName")
            size_t slashPos = dep.name.find('/');
            if (slashPos != std::string::npos)
            {
                pkg.displayName = dep.name.substr(slashPos + 1);
            }
        }

        pkg.installedVersion = dep.version;
        pkg.isInstalled = true;

        packages.push_back(pkg);
    }

    return packages;
}

void ESPIDFInstaller::Install(const std::string& projectPath, const PackageEntry& pkg, const std::string& version,
                              PackageCallback callback)
{
    fs::path yamlPath = GetYamlPath(projectPath);

    std::vector<DependencyInfo> dependencies;
    ParseYaml(yamlPath, dependencies);

    DependencyInfo newDep;
    // A "branch:" prefix is stripped from the version.
    if (version.rfind("branch:", 0) == 0)
    {
        newDep.version = version.substr(7);
    }
    else
    {
        newDep.version = version;
    }
    newDep.source = pkg.source;

    if (pkg.source == PackageSourceType::GitHub)
    {
        // GitHub packages use the repo name from pkg.name ("lovyan03/LovyanGFX" -> "LovyanGFX").
        std::string repoName = pkg.name;
        size_t slashPos = repoName.find('/');
        if (slashPos != std::string::npos)
        {
            repoName = repoName.substr(slashPos + 1);
        }
        newDep.name = repoName;

        newDep.gitUrl = pkg.url;
        if (!newDep.gitUrl.empty() && newDep.gitUrl.substr(newDep.gitUrl.size() - 4) != ".git")
        {
            newDep.gitUrl += ".git";
        }
    }
    else
    {
        // Registry packages use the full name (e.g. "espressif/esp_tinyusb").
        newDep.name = pkg.name;
    }

    // Already listed? By name for the registry, by git URL for GitHub.
    bool found = false;
    for (auto& dep : dependencies)
    {
        bool match = false;
        if (pkg.source == PackageSourceType::GitHub && dep.source == PackageSourceType::GitHub)
        {
            match = (dep.gitUrl == newDep.gitUrl) || (dep.name == newDep.name);
        }
        else
        {
            match = (dep.name == newDep.name);
        }

        if (match)
        {
            dep = newDep;
            found = true;
            break;
        }
    }

    if (!found)
    {
        dependencies.push_back(newDep);
    }

    if (WriteYaml(yamlPath, dependencies))
    {
        callback(true, "Package installed successfully");
    }
    else
    {
        callback(false, "Failed to write idf_component.yml");
    }
}

void ESPIDFInstaller::Remove(const std::string& projectPath, const PackageEntry& pkg, PackageCallback callback)
{
    fs::path yamlPath = GetYamlPath(projectPath);

    std::vector<DependencyInfo> dependencies;
    if (!ParseYaml(yamlPath, dependencies))
    {
        callback(false, "Failed to read idf_component.yml");
        return;
    }

    // GitHub packages are matched by repo name.
    std::string matchName = pkg.name;
    if (pkg.source == PackageSourceType::GitHub)
    {
        size_t slashPos = matchName.find('/');
        if (slashPos != std::string::npos)
        {
            matchName = matchName.substr(slashPos + 1);
        }
    }

    auto it = std::remove_if(dependencies.begin(), dependencies.end(),
                             [&pkg, &matchName](const DependencyInfo& dep)
                             {
                                 if (pkg.source == PackageSourceType::GitHub && dep.source == PackageSourceType::GitHub)
                                 {
                                     return dep.name == matchName ||
                                            (!pkg.url.empty() && dep.gitUrl.find(pkg.url) != std::string::npos);
                                 }
                                 return dep.name == pkg.name;
                             });

    if (it == dependencies.end())
    {
        callback(false, "Package not found");
        return;
    }

    dependencies.erase(it, dependencies.end());

    if (WriteYaml(yamlPath, dependencies))
    {
        callback(true, "Package removed successfully");
    }
    else
    {
        callback(false, "Failed to write idf_component.yml");
    }
}

// ============================================================================
// Core dependencies (managed by the editor, hidden from users)
// ============================================================================

// Core dependencies for Espressif platforms (ESP32, ESP32-S3, etc.). Empty:
// the components are bundled in toolchains/esp-idf-components/ and copied to
// the project's builders/esp-idf/components/ folder when it is created, not
// managed through idf_component.yml.
static const std::vector<CoreDependency> kEspressifCoreDeps = {
    // Bundled; nothing to manage
};

std::vector<CoreDependency> ESPIDFInstaller::GetCoreDependencies() const
{
    return kEspressifCoreDeps;
}

bool ESPIDFInstaller::IsCoreDependency(const std::string& name) const
{
    for (const auto& coreDep : kEspressifCoreDeps)
    {
        if (coreDep.name == name)
        {
            return true;
        }

        // Registry packages like "espressif/esp_tinyusb" also match "esp_tinyusb"
        size_t slashPos = coreDep.name.find('/');
        if (slashPos != std::string::npos)
        {
            std::string shortName = coreDep.name.substr(slashPos + 1);
            if (shortName == name)
            {
                return true;
            }
        }

        // GitHub packages match by repo name or owner/repo.
        if (coreDep.isGitHub && !coreDep.gitUrl.empty())
        {
            std::string url = coreDep.gitUrl;
            if (url.size() > 4 && url.substr(url.size() - 4) == ".git")
            {
                url = url.substr(0, url.size() - 4);
            }
            size_t githubPos = url.find("github.com/");
            if (githubPos != std::string::npos)
            {
                std::string ownerRepo = url.substr(githubPos + 11);  // "lovyan03/LovyanGFX"
                if (ownerRepo == name)
                {
                    return true;
                }
                // or just the repo name
                size_t lastSlash = ownerRepo.find_last_of('/');
                if (lastSlash != std::string::npos)
                {
                    std::string repoName = ownerRepo.substr(lastSlash + 1);
                    if (repoName == name)
                    {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

void ESPIDFInstaller::EnsureCoreDependencies(const std::string& projectPath)
{
    fs::path yamlPath = GetYamlPath(projectPath);

    std::vector<DependencyInfo> dependencies;
    ParseYaml(yamlPath, dependencies);

    bool modified = false;

    for (const auto& coreDep : kEspressifCoreDeps)
    {
        // The name to match: repo name for GitHub, full name for the registry
        std::string matchName = coreDep.name;
        if (coreDep.isGitHub && !coreDep.gitUrl.empty())
        {
            // For GitHub the YAML uses the repo name (e.g. "LovyanGFX"),
            // which core deps already are named by.
        }

        bool found = false;
        for (auto& dep : dependencies)
        {
            bool isMatch = (dep.name == coreDep.name);

            // Registry packages also match by short name.
            if (!isMatch)
            {
                size_t slashPos = coreDep.name.find('/');
                if (slashPos != std::string::npos)
                {
                    std::string shortName = coreDep.name.substr(slashPos + 1);
                    isMatch = (dep.name == shortName);
                }
            }

            // GitHub packages also match by repo name.
            if (!isMatch && coreDep.isGitHub)
            {
                isMatch = (dep.name == coreDep.name);
            }

            if (isMatch)
            {
                found = true;
                if (dep.version != coreDep.version)
                {
                    dep.version = coreDep.version;
                    modified = true;
                }
                if (coreDep.isGitHub && dep.gitUrl != coreDep.gitUrl)
                {
                    dep.gitUrl = coreDep.gitUrl;
                    dep.source = PackageSourceType::GitHub;
                    modified = true;
                }
                break;
            }
        }

        if (!found)
        {
            DependencyInfo newDep;
            newDep.name = coreDep.name;
            newDep.version = coreDep.version;
            newDep.gitUrl = coreDep.gitUrl;
            newDep.source = coreDep.isGitHub ? PackageSourceType::GitHub : PackageSourceType::FrameworkRegistry;
            dependencies.push_back(newDep);
            modified = true;
        }
    }

    if (modified)
    {
        WriteYaml(yamlPath, dependencies);
    }
}

void ESPIDFInstaller::MergePackageDeps(const std::string& projectPath)
{
    // Scan project/packages/*/ for .deki-package.json files with dependencies.espidf.
    fs::path packagesDir = fs::path(DekiEditor::GetPackagesDirectory(projectPath));
    if (!fs::exists(packagesDir))
    {
        return;
    }

    // Collect the installed packages' espidf deps; the highest version wins.
    std::vector<DependencyInfo> packageDeps;

    try
    {
        for (const auto& entry : fs::directory_iterator(packagesDir))
        {
            if (!entry.is_directory())
            {
                continue;
            }

            fs::path metaPath = entry.path() / ".deki-package.json";
            if (!fs::exists(metaPath))
            {
                continue;
            }

            std::ifstream metaFile(metaPath);
            std::string content((std::istreambuf_iterator<char>(metaFile)), std::istreambuf_iterator<char>());

            size_t pdPos = content.find("\"dependencies\"");
            if (pdPos == std::string::npos)
            {
                continue;
            }

            size_t espidfPos = content.find("\"espidf\"", pdPos);
            if (espidfPos == std::string::npos)
            {
                continue;
            }

            size_t arrayStart = content.find('[', espidfPos);
            if (arrayStart == std::string::npos)
            {
                continue;
            }
            size_t arrayEnd = content.find(']', arrayStart);
            if (arrayEnd == std::string::npos)
            {
                continue;
            }

            std::string arrayContent = content.substr(arrayStart, arrayEnd - arrayStart + 1);

            // Each dep object in the array
            size_t searchPos = 0;
            while (true)
            {
                size_t objStart = arrayContent.find('{', searchPos);
                if (objStart == std::string::npos)
                {
                    break;
                }
                size_t objEnd = arrayContent.find('}', objStart);
                if (objEnd == std::string::npos)
                {
                    break;
                }

                std::string obj = arrayContent.substr(objStart, objEnd - objStart + 1);
                searchPos = objEnd + 1;

                auto extractField = [&obj](const std::string& key) -> std::string
                {
                    std::string needle = "\"" + key + "\"";
                    size_t pos = obj.find(needle);
                    if (pos == std::string::npos)
                    {
                        return "";
                    }
                    pos = obj.find(':', pos + needle.size());
                    if (pos == std::string::npos)
                    {
                        return "";
                    }
                    pos = obj.find('"', pos + 1);
                    if (pos == std::string::npos)
                    {
                        return "";
                    }
                    pos++;
                    size_t end = obj.find('"', pos);
                    if (end == std::string::npos)
                    {
                        return "";
                    }
                    return obj.substr(pos, end - pos);
                };

                DependencyInfo dep;
                dep.name = extractField("name");
                dep.version = extractField("version");
                dep.gitUrl = extractField("git");
                dep.source = dep.gitUrl.empty() ? PackageSourceType::FrameworkRegistry : PackageSourceType::GitHub;

                if (!dep.name.empty() && !dep.version.empty())
                {
                    // The same dep from another package: keep the higher version.
                    bool replaced = false;
                    for (auto& existing : packageDeps)
                    {
                        if (existing.name == dep.name)
                        {
                            if (DekiEditor::CompareVersions(dep.version, existing.version) > 0)
                            {
                                existing.version = dep.version;
                                existing.gitUrl = dep.gitUrl;
                                existing.source = dep.source;
                            }
                            replaced = true;
                            break;
                        }
                    }
                    if (!replaced)
                    {
                        packageDeps.push_back(dep);
                    }
                }
            }
        }
    }
    catch (const fs::filesystem_error&)
    {
        return;
    }

    if (packageDeps.empty())
    {
        return;
    }

    MergeDeps(projectPath, packageDeps);
}

void ESPIDFInstaller::MergeDeps(const std::string& projectPath, const std::vector<DependencyInfo>& deps)
{
    if (deps.empty())
    {
        return;
    }

    fs::path yamlPath = GetYamlPath(projectPath);
    std::vector<DependencyInfo> existingDeps;
    ParseYaml(yamlPath, existingDeps);

    // Add new deps, or upgrade existing ones to higher versions.
    bool modified = false;
    for (const auto& dep : deps)
    {
        DependencyInfo* match = nullptr;
        for (auto& existing : existingDeps)
        {
            if (existing.name == dep.name)
            {
                match = &existing;
                break;
            }
            // Short names match too ("esp_tinyusb" matches "espressif/esp_tinyusb").
            size_t slashPos = dep.name.find('/');
            if (slashPos != std::string::npos)
            {
                if (existing.name == dep.name.substr(slashPos + 1))
                {
                    match = &existing;
                    break;
                }
            }
        }

        if (!match)
        {
            existingDeps.push_back(dep);
            modified = true;
        }
        else if (DekiEditor::CompareVersions(dep.version, match->version) > 0)
        {
            match->version = dep.version;
            match->gitUrl = dep.gitUrl;
            match->source = dep.source;
            modified = true;
        }
    }

    if (modified)
    {
        WriteYaml(yamlPath, existingDeps);
    }
}

// ---------------------------------------------------------------------------
// Installer plugin entry points
//
// ESP-IDF component installation comes from this package, through the same
// probe the build backends use, so the editor's PackageManager knows no
// framework's package manager by name.
// ---------------------------------------------------------------------------

#include <deki-editor/PackageInstallerPlugin.h>

extern "C"
{
    DEKI_INSTALLER_API int DekiInstallerGetCount(void)
    {
        return 1;
    }

    DEKI_INSTALLER_API IPackageInstaller* DekiInstallerCreate(int index)
    {
        return index == 0 ? new ESPIDFInstaller() : nullptr;
    }

    DEKI_INSTALLER_API void DekiInstallerDestroy(IPackageInstaller* installer)
    {
        delete installer;  // in THIS module
    }

}  // extern "C"
