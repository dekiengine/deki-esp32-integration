#include <algorithm>
#include <utility>
#include <cstdlib>
#include "ESPIDFToolchain.h"
#include <deki-editor/BuildLoad.h>
#include <deki-editor/SafeNames.h>
#include <deki-editor/EditorPaths.h>
#include <deki-editor/build/PlatformConfig.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace DekiEditor
{

std::string ESPIDFToolchain::GetDekiEditorDir() const
{
    return EditorPaths::GetEditorDataDir();
}

std::string ESPIDFToolchain::GetToolchainsDir() const
{
    return EditorPaths::GetToolchainsDir();
}

std::string ESPIDFToolchain::ReadEngineVersion(const std::string& projectPath) const
{
    fs::path dekiJsonPath = fs::path(projectPath) / "deki.json";
    if (!fs::exists(dekiJsonPath))
    {
        return "";
    }

    try
    {
        std::ifstream file(dekiJsonPath);
        if (!file.is_open())
        {
            return "";
        }

        nlohmann::json config = nlohmann::json::parse(file);
        if (config.contains("engineVersion"))
        {
            return config["engineVersion"].get<std::string>();
        }
    }
    catch (const std::exception&)
    {
    }

    return "";
}

std::string ESPIDFToolchain::GetIDFPath() const
{
    std::string idfDir = GetToolchainsDir() + "/espressif/esp-idf";

    // Normally the SDK sits directly in its install directory: the installer
    // lifts a wrapping folder out of the archive. The search below finds an
    // SDK still inside such a folder.
#ifdef _WIN32
    if (fs::exists(fs::path(idfDir) / "export.bat"))
    {
        return idfDir;
    }
#else
    if (fs::exists(fs::path(idfDir) / "export.sh"))
    {
        return idfDir;
    }
#endif

    try
    {
        if (fs::exists(idfDir))
        {
            for (const auto& entry : fs::directory_iterator(idfDir))
            {
                if (entry.is_directory())
                {
                    std::string name = entry.path().filename().string();
                    if (name.find("esp-idf") != std::string::npos)
                    {
                        return entry.path().string();
                    }
                }
            }
        }
    }
    catch (const std::exception&)
    {
    }

    return idfDir;
}

namespace
{
// "v6.1", "6.1", "6.1.0" all name the same release.
std::string CanonicalIdfVersion(std::string v)
{
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V'))
    {
        v.erase(0, 1);
    }
    int dots = 0;
    for (char c : v)
    {
        dots += (c == '.');
    }
    while (dots++ < 2)
    {
        v += ".0";
    }
    return v;
}
}  // namespace

std::string ESPIDFToolchain::InstalledVersion() const
{
    std::ifstream f(fs::path(GetIDFPath()) / "tools" / "cmake" / "version.cmake");
    if (!f.is_open())
    {
        return {};
    }

    std::string major, minor, patch, line;
    while (std::getline(f, line))
    {
        auto grab = [&line](const char* key, std::string& out)
        {
            const std::string k = std::string("set(") + key + " ";
            const size_t at = line.find(k);
            if (at == std::string::npos)
            {
                return;
            }
            const size_t start = at + k.size();
            const size_t end = line.find(')', start);
            if (end != std::string::npos)
            {
                out = line.substr(start, end - start);
            }
        };
        grab("IDF_VERSION_MAJOR", major);
        grab("IDF_VERSION_MINOR", minor);
        grab("IDF_VERSION_PATCH", patch);
    }
    if (major.empty() || minor.empty())
    {
        return {};
    }
    return major + "." + minor + "." + (patch.empty() ? "0" : patch);
}

bool ESPIDFToolchain::IsInstalled() const
{
    std::string idfPath = GetIDFPath();

#ifdef _WIN32
    std::string exportScript = idfPath + "/export.bat";
#else
    std::string exportScript = idfPath + "/export.sh";
#endif

    if (!fs::exists(exportScript))
    {
        return false;
    }
    // No known pin means the definition failed to load, which the builder has
    // already reported; judging the version against nothing would hide that.
    if (m_RequiredVersion.empty())
    {
        return true;
    }
    return InstalledVersion() == CanonicalIdfVersion(m_RequiredVersion);
}

std::string ESPIDFToolchain::GetStatus() const
{
    if (IsInstalled())
    {
        return "ESP-IDF " + InstalledVersion() + " installed at " + GetIDFPath();
    }

    const std::string have = InstalledVersion();
    if (!have.empty() && !m_RequiredVersion.empty())
    {
        return "ESP-IDF " + have + " is installed, but this package is built against " +
               CanonicalIdfVersion(m_RequiredVersion) +
               "; update the ESP-IDF SDK component (Build panel, or --install-toolchain esp-idf)";
    }
    return "ESP-IDF not installed";
}

void ESPIDFToolchain::PrepareForBuild(BuildOutputCallback outputCallback, const std::string& buildDir)
{
    try
    {
        fs::path appDescObj = fs::path(buildDir) / "build" / "esp-idf" / "esp_app_format" / "CMakeFiles" /
                              "__idf_esp_app_format.dir" / "esp_app_desc.c.obj";

        if (fs::exists(appDescObj))
        {
            fs::remove(appDescObj);
            if (outputCallback)
            {
                outputCallback("Deleted esp_app_desc.c.obj to refresh compile timestamp", false);
            }
        }
    }
    catch (const std::exception&)
    {
    }
}

// ============================================================================
// ExecuteIDF
// ============================================================================

#ifdef _WIN32

int ESPIDFToolchain::ExecuteIDF(const std::string& command, const std::string& workDir, const std::string& enginePath,
                                BuildOutputCallback outputCallback, const ESPIDFExecContext& ctx)
{
    std::string idfPath = GetIDFPath();

    std::string logEnabled = ctx.enableLogging ? "1" : "";
    std::string internalLogEnabled = ctx.enableInternalLogging ? "1" : "";

    // Forward slashes: the generated CMake reads this as $ENV{DEKI_ENGINE_PATH}
    // inside idf_component_register(SRCS ...), and ESP-IDF 6 re-parses those
    // arguments as CMake code, where the "\U" of "C:\Users" is an invalid
    // escape. Forward slashes are a path to CMake on every platform.
    std::string enginePathForCMake = enginePath;
    std::replace(enginePathForCMake.begin(), enginePathForCMake.end(), '\\', '/');

    // What the build's environment adds to the editor's: given to cmd as its
    // environment block, never written into its command line as `set` lines.
    // A `set "PATH=...%PATH%..."` breaks on a PATH with a quote in it (Windows
    // allows one, and a Windows Kits installer leaves one): the quote closes
    // the set early, the rest of the line, export.bat and idf.py included,
    // becomes the value of PATH, and cmd exits 0 having run nothing.
    std::vector<std::pair<std::string, std::string>> childEnv;
    childEnv.emplace_back("DEKI_ENGINE_PATH", enginePathForCMake);
    // idf.py asks for it on a console that is not UTF-8, which the editor's
    // pipe is not.
    childEnv.emplace_back("PYTHONUTF8", "1");

    // PATH as export.bat starts from: export.bat prepends its own entries and
    // keeps these behind them.
    std::string pathFirst;  // before the inherited PATH
    std::string pathLast;   // after it

    // The toolchain's own Python first (see ESPIDFToolchainDefinition.h):
    // export.bat runs `python` to set up ESP-IDF's environment, and on a
    // machine with no Python of its own that finds the Microsoft Store's stub,
    // which exits with 9009. First, unlike the compiler below: it must be the
    // python export.bat finds.
    {
        const fs::path pythonDir = fs::path(GetToolchainsDir()) / "espressif" / "python" / "tools";
        if (fs::exists(pythonDir / "python.exe"))
        {
            std::string dir = pythonDir.string();
            std::replace(dir.begin(), dir.end(), '/', '\\');
            pathFirst += dir + ";" + dir + "\\Scripts;";
            if (outputCallback)
            {
                outputCallback("[Toolchain] Python: " + dir, false);
            }
        }
    }

    // The reflection generator's compiler, passed down explicitly.
    //
    // A stripped firmware build regenerates the reflection tables, which needs
    // GCC 16. Inside the ESP-IDF environment PATH belongs to Espressif, so a
    // search for g++ finds the cross compiler or nothing. The editor was built
    // with that compiler, so it knows where it is.
    {
        std::string gxx16;
        if (const char* fromEnv = std::getenv("DEKI_GXX16"); fromEnv && *fromEnv)
        {
            gxx16 = fromEnv;
        }
        else
        {
            for (const char* candidate :
                 { "C:/msys64/mingw64/bin/g++.exe", "C:/mingw64/bin/g++.exe", "C:/MinGW/bin/g++.exe" })
            {
                if (fs::exists(candidate))
                {
                    gxx16 = candidate;
                    break;
                }
            }
        }
        if (!gxx16.empty())
        {
            childEnv.emplace_back("DEKI_GXX16", gxx16);

            // And its own directory on PATH, or the compiler cannot run.
            //
            // g++ is only a driver: the real compiler, cc1plus, lives in
            // lib/gcc/..., not beside g++, so Windows finds its DLLs through
            // PATH. With only Espressif's PATH, cc1plus fails to start and
            // prints nothing, so the build fails with no diagnostics.
            //
            // Appended, not prepended: ESP-IDF's own cmake, ninja and python
            // must still come first, and nothing in its directories supplies
            // the libraries cc1plus needs.
            std::string compilerDir = fs::path(gxx16).parent_path().string();
            std::replace(compilerDir.begin(), compilerDir.end(), '/', '\\');
            if (!compilerDir.empty())
            {
                pathLast += ";" + compilerDir;
            }

            if (outputCallback)
            {
                outputCallback("[Codegen] DEKI_GXX16=" + gxx16 + " (PATH += " + compilerDir + ")", false);
            }
        }
    }

    childEnv.emplace_back("DEKI_LOG_ENABLED", logEnabled);
    childEnv.emplace_back("DEKI_LOG_INTERNAL_ENABLED", internalLogEnabled);

    if (ctx.hasPlatformConfig && ctx.platformConfig)
    {
        bool usePsram = ctx.platformConfig->externalMemorySize > 0;
        childEnv.emplace_back("DEKI_USE_PSRAM", usePsram ? "1" : "");
        // Checked here as well as in ValidatePlatform: CMake reads it, and
        // not every path to here runs the validation first.
        const std::string displayBus = ctx.platformConfig->Option("displayBus");
        if (IsValidDisplayBus(displayBus))
        {
            childEnv.emplace_back("DEKI_DISPLAY_BUS", displayBus);
        }
    }
    else
    {
        if (outputCallback)
        {
            outputCallback("[Platform Config] Not set - using defaults from target header", false);
        }
    }

    if (ctx.packageDefines && !ctx.packageDefines->empty())
    {
        // These come from package.json files and become compiler defines:
        // only identifier-shaped ones get through (SafeNames::IsSafeDefine).
        std::string definesList;
        for (const auto& def : *ctx.packageDefines)
        {
            std::string reason;
            if (!SafeNames::IsSafeDefine(def, reason))
            {
                if (outputCallback)
                {
                    outputCallback("[Package Config] skipping define '" + def + "': " + reason, true);
                }
                continue;
            }
            if (!definesList.empty())
            {
                definesList += ";";
            }
            definesList += def;
        }
        childEnv.emplace_back("DEKI_PACKAGE_DEFINES", definesList);

        if (outputCallback)
        {
            outputCallback("[Package Config] Auto-detected defines:", false);
            for (const auto& def : *ctx.packageDefines)
            {
                outputCallback("  " + def, false);
            }
        }
    }

    // export.bat by its full path, not by name after a cd: with
    // NoDefaultCurrentDirectoryInExePath set, cmd refuses to run anything
    // from the working directory ("export.bat is not recognised").
    // /S: with this many quoted segments cmd otherwise strips the wrong pair
    // and reports "the filename, directory name, or volume label syntax is
    // incorrect". /S makes it take everything between the outermost quotes
    // as is.
    // Backslashes for the script's own path: export.bat works out from %~dp0
    // whether it runs under CMD, and with a forward-slash path it decides it
    // does not and refuses ("This .bat file is for Windows CMD.EXE shell
    // only").
    std::string idfPathNative = idfPath;
    std::replace(idfPathNative.begin(), idfPathNative.end(), '/', '\\');

    std::string fullCommand = "cmd /s /c \"cd /d \"" + idfPathNative + "\" && call \"" + idfPathNative +
                              "\\export.bat\" && cd /d \"" + workDir + "\" && " + command + "\"";

    // The environment block: the editor's own, less MSYSTEM, with PATH and
    // childEnv applied. MSYSTEM goes because export.bat refuses to run when it
    // sees it, taking it to mean an MSYS shell, not CMD: every child of an
    // MSYS2 or Git Bash session inherits it, so an editor launched from such a
    // shell would fail every firmware build with "This .bat file is for
    // Windows CMD.EXE shell only", though the child really is CMD.
    std::vector<std::pair<std::string, std::string>> environment;
    if (LPCH block = ::GetEnvironmentStringsA())
    {
        for (const char* entry = block; *entry; entry += std::strlen(entry) + 1)
        {
            const char* eq = std::strchr(entry + 1, '=');  // past a leading '=' (cmd's "=C:" entries)
            if (eq)
            {
                environment.emplace_back(std::string(entry, eq), std::string(eq + 1));
            }
        }
        ::FreeEnvironmentStringsA(block);
    }
    auto sameName = [](const std::string& a, const std::string& b)
    { return _stricmp(a.c_str(), b.c_str()) == 0; };  // Windows names ignore case
    auto setVariable = [&](const std::string& name, const std::string& value)
    {
        for (auto& [existing, existingValue] : environment)
        {
            if (sameName(existing, name))
            {
                existingValue = value;
                return;
            }
        }
        environment.emplace_back(name, value);
    };
    environment.erase(std::remove_if(environment.begin(), environment.end(),
                                     [&](const auto& v) { return sameName(v.first, "MSYSTEM"); }),
                      environment.end());
    {
        std::string path;
        for (const auto& [name, value] : environment)
        {
            if (sameName(name, "PATH"))
            {
                path = value;
            }
        }
        // A PATH with an odd number of quotes is broken for cmd: from the
        // lone quote on it reads the entries as one quoted run, so nothing
        // behind it is found (export.bat then reports git missing). A
        // Windows Kits installer leaves such an entry. Its quotes are left
        // out of the build's PATH, and the build says so, naming the entry;
        // the fix belongs in Windows' environment variables.
        if (std::count(path.begin(), path.end(), '"') % 2 != 0)
        {
            std::string repaired;
            size_t start = 0;
            while (start <= path.size())
            {
                size_t end = path.find(';', start);
                if (end == std::string::npos)
                {
                    end = path.size();
                }
                std::string entry = path.substr(start, end - start);
                if (std::count(entry.begin(), entry.end(), '"') % 2 != 0)
                {
                    if (outputCallback)
                    {
                        outputCallback("[Toolchain] PATH entry " + entry +
                                           " has a stray quote, which stops cmd finding anything after it: the "
                                           "build uses it without the quote. Fix it in Windows' environment "
                                           "variables.",
                                       true);
                    }
                    entry.erase(std::remove(entry.begin(), entry.end(), '"'), entry.end());
                }
                repaired += (start == 0 ? "" : ";") + entry;
                start = end + 1;
            }
            path = repaired;
        }
        setVariable("PATH", pathFirst + path + pathLast);
    }
    for (const auto& [name, value] : childEnv)
    {
        setVariable(name, value);
    }
    // Sorted by name, ignoring case, as CreateProcess expects; each NAME=value
    // ends in a 0, and the block in one more.
    std::sort(environment.begin(), environment.end(),
              [](const auto& a, const auto& b) { return _stricmp(a.first.c_str(), b.first.c_str()) < 0; });
    std::vector<char> environmentBlock;
    for (const auto& [name, value] : environment)
    {
        const std::string entry = name + "=" + value;
        environmentBlock.insert(environmentBlock.end(), entry.begin(), entry.end());
        environmentBlock.push_back('\0');
    }
    environmentBlock.push_back('\0');

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
    {
        return -1;
    }

    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::vector<char> cmdBuf(fullCommand.begin(), fullCommand.end());
    cmdBuf.push_back('\0');

    // Below-normal priority, which idf.py, ninja and the compilers inherit.
    DekiEditor::PrepareBuildJobs();
    BOOL success = CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW | DekiEditor::BuildPriorityFlag(), environmentBlock.data(), nullptr,
                                  &si, &pi);

    CloseHandle(hWritePipe);

    if (!success)
    {
        CloseHandle(hReadPipe);
        return -1;
    }

    char buffer[4096];
    DWORD bytesRead;
    std::string lineBuffer;

    while (true)
    {
        if (ctx.cancelRequested)
        {
            TerminateProcess(pi.hProcess, 1);
            break;
        }

        BOOL readSuccess = ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr);
        if (!readSuccess || bytesRead == 0)
        {
            break;
        }

        buffer[bytesRead] = '\0';
        lineBuffer += buffer;

        size_t pos;
        while ((pos = lineBuffer.find('\n')) != std::string::npos)
        {
            std::string line = lineBuffer.substr(0, pos);
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            lineBuffer = lineBuffer.substr(pos + 1);

            if (outputCallback && !line.empty())
            {
                bool isError =
                    (line.find("error:") != std::string::npos) || (line.find("Error:") != std::string::npos) ||
                    (line.find("ERROR") != std::string::npos) || (line.find("fatal error") != std::string::npos) ||
                    (line.find("FAILED") != std::string::npos);
                outputCallback(line, isError);
            }
        }
    }

    if (outputCallback && !lineBuffer.empty())
    {
        bool isError =
            (lineBuffer.find("error:") != std::string::npos) || (lineBuffer.find("Error:") != std::string::npos);
        outputCallback(lineBuffer, isError);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hReadPipe);

    return static_cast<int>(exitCode);
}

#else

int ESPIDFToolchain::ExecuteIDF(const std::string& command, const std::string& workDir, const std::string& enginePath,
                                BuildOutputCallback outputCallback, const ESPIDFExecContext& ctx)
{
    std::string idfPath = GetIDFPath();

    std::string logEnabled = ctx.enableLogging ? "1" : "";
    std::string internalLogEnabled = ctx.enableInternalLogging ? "1" : "";

    std::string envExports = "export DEKI_ENGINE_PATH=\"" + enginePath + "\" && ";
    envExports += "export DEKI_LOG_ENABLED=\"" + logEnabled + "\" && ";
    envExports += "export DEKI_LOG_INTERNAL_ENABLED=\"" + internalLogEnabled + "\" && ";

    if (ctx.hasPlatformConfig && ctx.platformConfig)
    {
        bool usePsram = ctx.platformConfig->externalMemorySize > 0;
        envExports += "export DEKI_USE_PSRAM=\"" + std::string(usePsram ? "1" : "") + "\" && ";
        const std::string displayBus = ctx.platformConfig->Option("displayBus");
        if (IsValidDisplayBus(displayBus))
        {
            envExports += "export DEKI_DISPLAY_BUS=\"" + displayBus + "\" && ";
        }
    }

    if (ctx.packageDefines && !ctx.packageDefines->empty())
    {
        // Same allowlist as the Windows branch: these land inside a shell line.
        std::string definesList;
        for (const auto& def : *ctx.packageDefines)
        {
            std::string reason;
            if (!SafeNames::IsSafeDefine(def, reason))
            {
                if (outputCallback)
                {
                    outputCallback("[Package Config] skipping define '" + def + "': " + reason, true);
                }
                continue;
            }
            if (!definesList.empty())
            {
                definesList += ";";
            }
            definesList += def;
        }
        envExports += "export DEKI_PACKAGE_DEFINES=\"" + definesList + "\" && ";
    }

    std::string fullCommand =
        "cd \"" + idfPath + "\" && . ./export.sh && cd \"" + workDir + "\" && " + envExports + command;

    DekiEditor::PrepareBuildJobs();
    FILE* pipe = popen(DekiEditor::AtBuildPriority(fullCommand).c_str(), "r");
    if (!pipe)
    {
        return -1;
    }

    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
    {
        if (ctx.cancelRequested)
        {
            pclose(pipe);
            return 1;
        }

        std::string line(buffer);
        if (!line.empty() && line.back() == '\n')
        {
            line.pop_back();
        }

        if (outputCallback && !line.empty())
        {
            bool isError = (line.find("error:") != std::string::npos) || (line.find("Error:") != std::string::npos) ||
                           (line.find("ERROR") != std::string::npos) ||
                           (line.find("fatal error") != std::string::npos) ||
                           (line.find("FAILED") != std::string::npos);
            outputCallback(line, isError);
        }
    }

    return pclose(pipe);
}

#endif

}  // namespace DekiEditor
