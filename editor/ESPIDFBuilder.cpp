#include "ESPIDFBuilder.h"
#include <deki-editor/build/BuilderWidgets.h>
#include <deki-editor/FeatureResolver.h>
#include <deki-editor/build/SimulationBootScene.h>
#include <deki-editor/SafeNames.h>
#include <algorithm>
#include <deki-editor/Paths.h>
#include <deki-editor/build/ProjectPaths.h>
#include <deki-editor/EditorTheme.h>
#include <deki-editor/EditorUI.h>
#include <deki-editor/build/CMakeGenUtils.h>
#include <deki-editor/build/BuilderDefinition.h>
#include <deki-editor/EditorPaths.h>
#include <deki-editor/EditorSettings.h>
#include <deki-editor/build/PlatformConfig.h>
#include "ESPIDFInstaller.h"
#include "ESPIDFToolchainDefinition.h"
#include <deki-editor/SerialPorts.h>
#include <deki/LogSystem.h>
#include "imgui.h"
#include <filesystem>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

namespace DekiEditor
{

// The chips idf.py accepts as set-target. Lives here, not in the editor's
// platform type: the editor is target-agnostic, and a new Espressif chip
// should be a change to this builder, not an editor release.
const std::vector<std::string>& SupportedIdfTargets()
{
    static const std::vector<std::string> kTargets = { "esp32",   "esp32s2", "esp32s3",
                                                       "esp32c3", "esp32c6", "esp32h2" };
    return kTargets;
}

// "SPI", "PARALLEL_8BIT", "SOFTWARE".
bool IsValidDisplayBus(const std::string& bus)
{
    if (bus.empty() || bus.size() > 32) return false;
    for (unsigned char c : bus)
    {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

std::vector<std::string> ESPIDFBuilder::ValidatePlatform(const PlatformConfig& config) const
{
    std::vector<std::string> problems;

    // idfTarget reaches the shell line that runs idf.py and the generated
    // CMake, and a platform JSON can arrive in a board pack, so it is
    // allowlisted rather than trusted. It used to be silently cleared at load,
    // which turned a typo into a confusing downstream failure.
    if (config.Option("idfTarget").empty())
    {
        problems.push_back("idfTarget is not set; an ESP-IDF platform must name its chip");
    }
    else
    {
        const auto& targets = SupportedIdfTargets();
        if (std::find(targets.begin(), targets.end(), config.Option("idfTarget")) == targets.end())
        {
            std::string known;
            for (const auto& t : targets)
                known += (known.empty() ? "" : ", ") + t;
            problems.push_back("idfTarget '" + config.Option("idfTarget") +
                               "' is not a supported ESP-IDF target; use one of: " + known);
        }
    }

    // displayBus reaches the shell line that exports the build environment
    // (DEKI_DISPLAY_BUS), so it is an identifier or the build is refused. The
    // editor's platform loader used to check this, by knowing that a platform
    // has a display bus; the setting and its rule are this backend's now.
    const std::string displayBus = config.Option("displayBus");
    if (!displayBus.empty() && !IsValidDisplayBus(displayBus))
        problems.push_back("displayBus '" + displayBus + "' is not an identifier (A-Z, 0-9, '_')");

    return problems;
}

ESPIDFBuilder::ESPIDFBuilder()
{
    // The definition travels inside this backend; see ESPIDFToolchainDefinition.h.
    // It used to be read from beside the editor's executable, and a miss was
    // silent: the builder came up with no toolchain components at all and the
    // Build panel simply showed nothing to install.
    BuilderDefinition def;
    std::string error;
    if (ParseBuilderDefinition(kESPIDFToolchainDefinition, def, error))
    {
        m_ToolchainMgr.Initialize(def);
        for (const auto& comp : def.components)
            if (comp.id == "esp-idf")
                m_Toolchain.SetRequiredVersion(comp.fallback.version);
    }
    else
        DEKI_LOG_ERROR("ESP-IDF backend: its own toolchain definition does not parse (%s); "
                       "no toolchain component can be installed or detected",
                       error.c_str());
}

ESPIDFBuilder::~ESPIDFBuilder()
{
    Cancel();
    if (m_BuildThread.joinable())
        m_BuildThread.join();
}

// ============================================================================
// Toolchain — delegates to ESPIDFToolchain
// ============================================================================

std::string ESPIDFBuilder::GetIDFPath() const
{
    return m_Toolchain.GetIDFPath();
}
bool ESPIDFBuilder::IsToolchainInstalled() const
{
    return m_Toolchain.IsInstalled();
}
std::string ESPIDFBuilder::GetToolchainStatus() const
{
    return m_Toolchain.GetStatus();
}

ESPIDFExecContext ESPIDFBuilder::MakeExecContext()
{
    return { m_BuildOptions.enableLogging, m_BuildOptions.enableInternalLogging,
             m_HasPlatformConfig, &m_PlatformConfig,
             &m_PackageDefines, m_CancelRequested };
}

// ============================================================================
// Identity
// ============================================================================

std::vector<DeployTarget> ESPIDFBuilder::EnumerateDeployTargets() const
{
    std::vector<DeployTarget> targets;
    if (m_BuildOptions.simulate)
        return targets;  // runs on this machine, in QEMU
    for (const std::string& port : EnumerateSerialPorts())
        targets.push_back({ port, port });
    return targets;
}

std::vector<std::pair<std::string, std::string>> ESPIDFBuilder::DescribePlatform(const PlatformConfig& config) const
{
    std::vector<std::pair<std::string, std::string>> rows;
    if (!config.Option("mcuChip").empty())
        rows.emplace_back("Chip", config.Option("mcuChip"));
    if (config.OptionU32("flashSize") > 0)
        rows.emplace_back("Flash", std::to_string(config.OptionU32("flashSize") / (1024 * 1024)) + " MB");
    if (config.externalMemorySize > 0)
        rows.emplace_back("PSRAM", std::to_string(config.externalMemorySize / (1024 * 1024)) + " MB");
    if (!config.Option("displayDriver").empty())
        rows.emplace_back("Driver", config.Option("displayDriver"));
    return rows;
}

// QEMU gives these chips a screen (the ESP32QemuDisplaySetup panel).
static bool QemuHasScreen(const std::string& idfTarget)
{
    return idfTarget == "esp32s3" || idfTarget == "esp32";
}

bool ESPIDFBuilder::SupportsSimulation() const
{
    if (!m_HasPlatformConfig)
        return false;
    const std::string chip = !m_PlatformConfig.Option("idfTarget").empty() ? m_PlatformConfig.Option("idfTarget")
                                                                           : m_PlatformConfig.Option("mcuChip");
    return QemuHasScreen(chip);
}

// Every component of `type` in the scene's object tree.
static void ForEachComponent(nlohmann::json& objects, const char* type,
                             const std::function<void(nlohmann::json& object, nlohmann::json& component)>& fn)
{
    if (!objects.is_array())
        return;
    for (nlohmann::json& object : objects)
    {
        if (object.contains("components") && object["components"].is_array())
            for (nlohmann::json& component : object["components"])
                if (component.value("type", std::string()) == type)
                    fn(object, component);
        if (object.contains("children"))
            ForEachComponent(object["children"], type, fn);
    }
}

bool ESPIDFBuilder::MakeSimulationBootScene(std::string& scene, const PlatformConfig& config,
                                            std::vector<std::string>& notes, std::string& error) const
{
    nlohmann::json json;  // null: the platform has no boot scene of its own
    if (!scene.empty())
    {
        json = nlohmann::json::parse(scene, nullptr, /*allow_exceptions*/ false);
        if (json.is_discarded())
        {
            error = "the boot scene is not valid JSON";
            return false;
        }
    }

    // The QEMU screen at the board's own size, after the board's steps, so it
    // is the display the game renders to. It has two formats; a board whose
    // screen has another gets the nearer one.
    const bool wide = config.colorFormat == "ARGB8888" || config.colorFormat == "RGB888";
    const char* format = wide ? "ARGB8888" : "RGB565";
    nlohmann::json steps = nlohmann::json::array();
    steps.push_back({ { "name", "QEMU Display" },
                      { "type", "DekiEsp32::ESP32QemuDisplaySetup" },
                      { "properties",
                        { { "width", config.screenWidth }, { "height", config.screenHeight }, { "format", format } } } });
    if (!AddSimulationSteps(json, steps, error))
        return false;
    notes.push_back("QEMU Display (" + std::to_string(config.screenWidth) + "x" +
                    std::to_string(config.screenHeight) + " " + format + ") added after the board's steps");

    // QEMU has no SPI controller for a card, but it has the SD host. A card
    // wired for SPI is moved to the SD host in 1-bit mode, the SD pinout of
    // the same wires: CMD on the MOSI pin, D0 on the MISO pin, the clock kept.
    ForEachComponent(json["objects"], "DekiSdCard::SDCardComponent",
                     [&notes](nlohmann::json& object, nlohmann::json& component)
                     {
                         nlohmann::json& props = component["properties"];
                         if (props.value("mode", std::string("SPI")) != "SPI")
                             return;
                         const int cmd = props.value("mosiPin", 23);
                         const int d0 = props.value("misoPin", 19);
                         props["mode"] = "SDMMC_1BIT";
                         props["cmdPin"] = cmd;
                         props["d0Pin"] = d0;
                         notes.push_back(object.value("name", std::string("SD card")) +
                                         ": on QEMU's SD host (SDMMC 1-bit, CMD " + std::to_string(cmd) + ", D0 " +
                                         std::to_string(d0) + "); QEMU has no SPI for it");
                     });

    scene = json.dump(2);
    return true;
}

std::string ESPIDFBuilder::GetBuildDirectory(const std::string& projectPath) const
{
    // A simulated build beside the board's, never in it. Still one level under
    // generated/build: the generated CMake finds the project three levels up.
    if (m_HasPlatformConfig && !m_PlatformConfig.id.empty() && m_BuildOptions.simulate)
        return (ProjectPaths::Build(projectPath) / (m_PlatformConfig.id + "_qemu")).string();
    if (m_HasPlatformConfig && !m_PlatformConfig.id.empty())
        return (ProjectPaths::Build(projectPath) / m_PlatformConfig.id).string();
    return (ProjectPaths::Build(projectPath) / "esp-idf").string();
}

// ============================================================================
// Build helpers
// ============================================================================

// Determine the ESP-IDF target chip from the platform config.
static std::string GetIdfTarget(const PlatformConfig& config)
{
    if (!config.Option("idfTarget").empty()) return config.Option("idfTarget");
    if (!config.Option("mcuChip").empty()) return config.Option("mcuChip");
    static const std::vector<std::string> knownChips = {
        "esp32s3", "esp32s2", "esp32c3", "esp32c6", "esp32h2", "esp32"
    };
    for (const auto& chip : knownChips)
        if (config.id.find(chip) != std::string::npos) return chip;
    return "";
}

// Read CONFIG_IDF_TARGET value from an existing sdkconfig file.
static std::string ReadSdkConfigTarget(const std::string& buildDir)
{
    std::ifstream f(fs::path(buildDir) / "sdkconfig");
    std::string line;
    while (std::getline(f, line))
    {
        if (line.rfind("CONFIG_IDF_TARGET=", 0) == 0)
        {
            std::string val = line.substr(18);
            if (val.size() >= 2 && val.front() == '"')
                val = val.substr(1, val.size() - 2);
            return val;
        }
    }
    return "";
}

// ============================================================================
// Core operations — use RunOnBuildThread()
// ============================================================================

void ESPIDFBuilder::Build(const std::string& projectPath, BuildOutputCallback outputCallback,
                          BuildProgressCallback progressCallback)
{
    RunOnBuildThread([this, projectPath, outputCallback, progressCallback]()
                     { DoBuild(projectPath, outputCallback, progressCallback); });
}

void ESPIDFBuilder::Deploy(const std::string& projectPath, const std::string& port,
                           BuildOutputCallback outputCallback, BuildProgressCallback progressCallback)
{
    if (m_BuildOptions.simulate)
    {
        RunOnBuildThread([this, projectPath, outputCallback, progressCallback]()
                         { DoRunQemu(projectPath, outputCallback, progressCallback); });
        return;
    }
    RunOnBuildThread([this, projectPath, port, outputCallback, progressCallback]()
                     { DoFlash(projectPath, port, outputCallback, progressCallback); });
}

void ESPIDFBuilder::Clean(const std::string& projectPath, BuildOutputCallback outputCallback,
                          BuildProgressCallback progressCallback)
{
    RunOnBuildThread([this, projectPath, outputCallback, progressCallback]()
                     { DoClean(projectPath, outputCallback, progressCallback); });
}


// ============================================================================
// Internal worker functions
// ============================================================================

void ESPIDFBuilder::DoBuild(const std::string& projectPath, BuildOutputCallback outputCallback,
                            BuildProgressCallback progressCallback)
{
    SetProgress(BuildState::Building, "Starting build...", 0.0f);
    if (progressCallback) progressCallback(GetProgress());

    if (!IsToolchainInstalled())
    {
        SetError("ESP-IDF is not installed. Please download and install it first.");
        if (progressCallback) progressCallback(GetProgress());
        return;
    }

    std::string buildDir = GetBuildDirectory(projectPath);
    std::string enginePath = GetEnginePath(projectPath);
    if (outputCallback) outputCallback("Building firmware with ESP-IDF...", false);
    if (outputCallback) outputCallback("Build directory: " + buildDir, false);
    if (outputCallback) outputCallback("Engine path: " + enginePath, false);

    // Regenerate build files before every build so platform config changes are always applied
    if (m_HasPlatformConfig)
    {
        if (!GenerateBuildFiles(projectPath, m_PlatformConfig, m_PackageDefines))
        {
            SetError("Failed to generate build files.");
            if (progressCallback) progressCallback(GetProgress());
            return;
        }
    }

    m_Toolchain.PrepareForBuild(outputCallback, buildDir);

    {
        ESPIDFInstaller installer;
        // User-installed package deps
        installer.MergePackageDeps(projectPath);
        // Built-in engine package deps (framework from PlatformConfig)
        if (m_HasPlatformConfig)
        {
            auto engineDeps = ReadEnginePackageDeps(enginePath, projectPath, GetPlatformKey());
            if (!engineDeps.empty())
                installer.MergeDeps(projectPath, engineDeps);
        }
    }

    if (m_HasPlatformConfig)
    {
        std::string expectedTarget = GetIdfTarget(m_PlatformConfig);
        bool cmakeCacheExists = fs::exists(fs::path(buildDir) / "build" / "CMakeCache.txt");
        bool sdkconfigExists = fs::exists(fs::path(buildDir) / "sdkconfig");
        std::string currentTarget = sdkconfigExists ? ReadSdkConfigTarget(buildDir) : "";

        bool needsReconfigure = !cmakeCacheExists || !sdkconfigExists || (!expectedTarget.empty() && currentTarget != expectedTarget);

        if (needsReconfigure)
        {
            if (!expectedTarget.empty() && sdkconfigExists && currentTarget != expectedTarget)
            {
                // Target changed — remove sdkconfig and the entire build/ directory.
                // Removing only CMakeCache.txt is insufficient: subprojects like the bootloader
                // have their own CMakeCache and will fail if the toolchain file doesn't match.
                fs::remove(fs::path(buildDir) / "sdkconfig");
                try
                {
                    fs::remove_all(fs::path(buildDir) / "build");
                }
                catch (...)
                { /* best effort: a stale build dir is rebuilt anyway, and a failed remove is not fatal */
                }
                if (outputCallback) outputCallback(
                    "Target changed (" + currentTarget + " -> " + expectedTarget + "), cleaning stale config...", false);
            }
            else
            {
                if (outputCallback) outputCallback("Reconfiguring CMake...", false);
            }

            int reconfigureCode = m_Toolchain.ExecuteIDF("idf.py reconfigure", buildDir, enginePath, outputCallback, MakeExecContext());
            if (reconfigureCode != 0)
            {
                SetError("CMake reconfigure failed with exit code " + std::to_string(reconfigureCode));
                if (progressCallback) progressCallback(GetProgress());
                return;
            }
        }
        else
        {
            if (outputCallback) outputCallback("Build config up to date, skipping reconfigure.", false);
        }
    }

    int exitCode = m_Toolchain.ExecuteIDF("idf.py build", buildDir, enginePath, outputCallback, MakeExecContext());

    if (m_CancelRequested)
    {
        SetProgress(BuildState::Idle, "Build cancelled", 0.0f);
        if (outputCallback) outputCallback("Build cancelled by user.", true);
    }
    else if (exitCode == 0)
    {
        SetProgress(BuildState::Completed, "Build successful!", 1.0f);
        if (outputCallback) outputCallback("Build completed successfully!", false);
    }
    else
    {
        SetError("Build failed with exit code " + std::to_string(exitCode));
        if (outputCallback) outputCallback("Build failed!", true);
    }

    if (progressCallback) progressCallback(GetProgress());
}

// Makes QEMU's flash and efuse images for the build and runs QEMU with a
// window, passing its serial output on, until the window is closed (Cancel
// stops it too: the editor's job ends QEMU with the script). Python because
// it runs in the ESP-IDF environment, where esptool and QEMU are on the path,
// and it keeps Windows and Linux alike. The
// efuse image comes from ESP-IDF's own QEMU support, so it matches the ESP-IDF
// installed. PSRAM: QEMU takes 2..16 MB here (idf.py qemu's 32 MB leaves no
// room to map the flash). The SD card is made fresh from the build's
// simulation/sd_card: FAT16, written here because ESP-IDF's fatfsgen sizes
// its FAT as if every sector were a cluster and stops at 16 MB.
static const char* kRunQemuScript = R"PY(import os, shutil, struct, subprocess, sys, time
chip, flash_size, psram_mb, card_dir = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]

# The simulated SD card: a FAT16 image of card_dir (long names, 512-byte
# sectors), a power of two in size as QEMU wants, 64 MB to 2 GB.
def make_card(card_dir, out_path):
    SS = 512
    entries_of = {}
    content = 0
    for root, dirs, files in os.walk(card_dir):
        dirs.sort()
        entries_of[root] = sorted(dirs) + sorted(files)
        content += sum(os.path.getsize(os.path.join(root, f)) for f in files)
    size = 64 << 20
    while size < 2 * content + (8 << 20):
        size *= 2
    if size > 2 << 30:
        sys.exit("The simulated SD card holds up to 1 GB; these assets are %d MB" % (content >> 20))
    total = size // SS
    reserved, fats, root_entries = 4, 2, 512
    root_secs = root_entries * 32 // SS
    spc = 1
    while True:
        fat_secs = ((total // spc + 2) * 2 + SS - 1) // SS
        clusters = (total - reserved - fats * fat_secs - root_secs) // spc
        if clusters <= 65524:
            break
        spc *= 2
    cs = spc * SS
    fat_start = reserved * SS
    root_start = (reserved + fats * fat_secs) * SS
    data_start = root_start + root_secs * SS
    fat = [0] * (clusters + 2)
    fat[0], fat[1] = 0xFFF8, 0xFFFF
    state = {"next": 2, "serial": 0}

    out = open(out_path, "wb")
    out.truncate(size)

    def alloc(nbytes):
        n = max(1, (nbytes + cs - 1) // cs)
        first = state["next"]
        if first + n > clusters + 2:
            sys.exit("The simulated SD card is full")
        for c in range(first, first + n - 1):
            fat[c] = c + 1
        fat[first + n - 1] = 0xFFFF
        state["next"] += n
        return first

    def at(cluster):
        return data_start + (cluster - 2) * cs

    def short_name():
        state["serial"] += 1
        return ("DK%06X" % state["serial"]).encode("ascii") + b"   "

    def dirent(name11, attr, cluster, fsize):
        return struct.pack("<11sBBBHHHHHHHI", name11, attr, 0, 0, 0, 0x21, 0x21, 0, 0, 0x21, cluster, fsize)

    def named(name, attr, cluster, fsize):
        sfn = short_name()
        check = 0
        for b in sfn:
            check = (((check & 1) << 7) + (check >> 1) + b) & 0xFF
        chars = [ord(c) for c in name] + [0]
        count = (len(chars) + 12) // 13
        chars += [0xFFFF] * (count * 13 - len(chars))
        raw = b""
        for i in range(count, 0, -1):
            part = chars[(i - 1) * 13 : i * 13]
            raw += struct.pack("<B10sBBB12sH4s", i | (0x40 if i == count else 0),
                               struct.pack("<5H", *part[0:5]), 0x0F, 0, check,
                               struct.pack("<6H", *part[5:11]), 0, struct.pack("<2H", *part[11:13]))
        return raw + dirent(sfn, attr, cluster, fsize)

    def entry_bytes(name):
        return ((len(name) + 1 + 12) // 13 + 1) * 32

    def write_dir(path, cluster, parent):
        raw = b""
        if cluster:
            raw += dirent(b".          ", 0x10, cluster, 0) + dirent(b"..         ", 0x10, parent, 0)
        for name in entries_of.get(path, []):
            full = os.path.join(path, name)
            if os.path.isdir(full):
                children = entries_of.get(full, [])
                child = alloc(64 + sum(entry_bytes(n) for n in children))
                raw += named(name, 0x10, child, 0)
                write_dir(full, child, cluster)
            else:
                fsize = os.path.getsize(full)
                first = alloc(fsize) if fsize else 0
                if fsize:
                    with open(full, "rb") as f:
                        out.seek(at(first))
                        out.write(f.read())
                raw += named(name, 0x20, first, fsize)
        if cluster:
            out.seek(at(cluster))
        else:
            if len(raw) > root_secs * SS:
                sys.exit("Too many files at the top of the simulated SD card")
            out.seek(root_start)
        out.write(raw)

    write_dir(card_dir, 0, 0)

    boot = bytearray(SS)
    boot[0:3] = b"\xEB\x3C\x90"
    boot[3:11] = b"DEKI    "
    struct.pack_into("<HBHBHHBHHHII", boot, 11, SS, spc, reserved, fats, root_entries,
                     total if total < 65536 else 0, 0xF8, fat_secs, 63, 255, 0, total if total >= 65536 else 0)
    struct.pack_into("<BBBI11s8s", boot, 36, 0x80, 0, 0x29, 0x44454B49, b"DEKI SD    ", b"FAT16   ")
    boot[510:512] = b"\x55\xAA"
    out.seek(0)
    out.write(boot)
    table = struct.pack("<%dH" % len(fat), *fat)
    for i in range(fats):
        out.seek(fat_start + i * fat_secs * SS)
        out.write(table)
    out.close()
    return size, content

if not os.path.isdir(card_dir):
    sys.exit("No simulated SD card at " + card_dir + ": build with Simulate in QEMU first")
card_size, card_content = make_card(card_dir, "qemu_sd.img")
print("SD card: %d MB, %d KB of files (%s)" % (card_size >> 20, card_content >> 10, os.path.abspath(card_dir)))
subprocess.check_call([sys.executable, "-m", "esptool", "--chip=" + chip, "merge-bin",
                       "--output=qemu_flash.bin", "--pad-to-size=" + flash_size, "@flash_args"])
if not os.path.exists("qemu_efuse.bin"):
    sys.path.insert(0, os.path.join(os.environ["IDF_PATH"], "tools"))
    from idf_py_actions.qemu_ext import QEMU_TARGETS
    with open("qemu_efuse.bin", "wb") as f:
        f.write(QEMU_TARGETS[chip].default_efuse)
qemu = shutil.which("qemu-system-xtensa")
if not qemu:
    print("QEMU is not installed. Install ESP-IDF's qemu-xtensa tool: "
          "idf_tools.py install qemu-xtensa", file=sys.stderr)
    sys.exit(2)
args = [qemu, "-M", chip]
if psram_mb:
    args += ["-m", str(psram_mb) + "M"]
args += ["-drive", "file=qemu_flash.bin,if=mtd,format=raw",
         "-drive", "file=qemu_efuse.bin,if=none,format=raw,id=efuse",
         "-global", "driver=nvram." + chip + ".efuse,property=drive,value=efuse",
         "-global", "driver=timer." + chip + ".timg,property=wdt_disable,value=true",
         "-drive", "file=qemu_sd.img,if=sd,format=raw",
         "-nic", "user,model=open_eth", "-display", "sdl",
         "-serial", "file:qemu.log", "-monitor", "tcp:127.0.0.1:4555,server,nowait"]
if os.path.exists("qemu.log"):
    os.remove("qemu.log")
qemu_proc = subprocess.Popen(args, stdin=subprocess.DEVNULL)
print("QEMU started: " + " ".join(args))
print("Monitor on 127.0.0.1:4555 (screendump <file>.ppm). Close the QEMU window to stop.", flush=True)
# The serial output, as it comes, until QEMU closes.
pos = 0
while True:
    done = qemu_proc.poll() is not None
    if os.path.exists("qemu.log"):
        with open("qemu.log", "rb") as log:
            log.seek(pos)
            data = log.read()
        pos += len(data)
        if data:
            sys.stdout.write(data.decode("utf-8", "replace"))
            sys.stdout.flush()
    if done:
        break
    time.sleep(0.2)
sys.exit(qemu_proc.returncode)
)PY";

void ESPIDFBuilder::DoRunQemu(const std::string& projectPath, BuildOutputCallback outputCallback,
                              BuildProgressCallback progressCallback)
{
    SetProgress(BuildState::Deploying, "Starting QEMU...", 0.0f);
    if (progressCallback) progressCallback(GetProgress());

    if (!IsToolchainInstalled())
    {
        SetError("ESP-IDF is not installed. Please download and install it first.");
        if (progressCallback) progressCallback(GetProgress());
        return;
    }

    const fs::path imageDir = fs::path(GetBuildDirectory(projectPath)) / "build";
    std::error_code ec;
    if (!fs::exists(imageDir / "flash_args", ec))
    {
        SetError("No simulated build to run: build with Simulate in QEMU first");
        if (outputCallback) outputCallback("No simulated build in " + imageDir.string(), true);
        if (progressCallback) progressCallback(GetProgress());
        return;
    }

    const std::string chip = !m_PlatformConfig.Option("idfTarget").empty() ? m_PlatformConfig.Option("idfTarget")
                                                                          : m_PlatformConfig.Option("mcuChip");
    const uint32_t flashMB = std::max<uint32_t>(4, m_PlatformConfig.OptionU32("flashSize") / (1024 * 1024));
    const uint32_t configuredFlash = flashMB >= 16 ? 16 : (flashMB >= 8 ? 8 : 4);
    const uint32_t psramMB = m_PlatformConfig.externalMemorySize / (1024 * 1024);
    if (psramMB != 0 && psramMB != 2 && psramMB != 4 && psramMB != 8 && psramMB != 16)
    {
        SetError("QEMU simulates 2, 4, 8 or 16 MB of PSRAM; this board has " + std::to_string(psramMB) + " MB");
        if (progressCallback) progressCallback(GetProgress());
        return;
    }

    {
        std::ofstream script(imageDir / "run_qemu.py", std::ios::binary | std::ios::trunc);
        script << kRunQemuScript;
    }
    const std::string cardDir =
        (fs::path(GetBuildDirectory(projectPath)) / kSimulationSceneDir / kSimulationCardDir).generic_string();
    const std::string command = "python run_qemu.py " + chip + " " + std::to_string(configuredFlash) + "MB " +
                                std::to_string(psramMB) + " \"" + cardDir + "\"";
    SetProgress(BuildState::Deploying, "Running in QEMU", 0.5f);
    if (progressCallback) progressCallback(GetProgress());
    const int exitCode =
        m_Toolchain.ExecuteIDF(command, imageDir.string(), GetEnginePath(projectPath), outputCallback, MakeExecContext());
    if (exitCode == 0)
    {
        SetProgress(BuildState::Completed, "QEMU closed", 1.0f);
        if (outputCallback) outputCallback("QEMU closed.", false);
    }
    else
    {
        SetError("QEMU stopped with exit code " + std::to_string(exitCode));
        if (outputCallback) outputCallback("QEMU stopped with exit code " + std::to_string(exitCode) + ".", true);
    }
    if (progressCallback) progressCallback(GetProgress());
}

void ESPIDFBuilder::DoFlash(const std::string& projectPath, const std::string& port,
                            BuildOutputCallback outputCallback, BuildProgressCallback progressCallback)
{
    SetProgress(BuildState::Deploying, "Flashing firmware...", 0.0f);
    if (progressCallback) progressCallback(GetProgress());

    if (!IsToolchainInstalled())
    {
        SetError("ESP-IDF is not installed. Please download and install it first.");
        if (progressCallback) progressCallback(GetProgress());
        return;
    }

    std::string buildDir = GetBuildDirectory(projectPath);
    std::string enginePath = GetEnginePath(projectPath);
    // The port is user input (a text field, the CLI, an MCP call) that lands in
    // a shell line beside idf.py; COM<n> or /dev/<name> only.
    {
        std::string reason;
        if (!SafeNames::IsSafeSerialPort(port, reason))
        {
            if (outputCallback) outputCallback("Refusing to flash: " + reason + " ('" + port + "')", true);
            return;
        }
    }
    std::string command = "idf.py -p " + port + " flash";
    if (outputCallback) outputCallback("Flashing to " + port + "...", false);

    int exitCode = m_Toolchain.ExecuteIDF(command, buildDir, enginePath, outputCallback, MakeExecContext());

    if (m_CancelRequested)
    {
        SetProgress(BuildState::Idle, "Flash cancelled", 0.0f);
        if (outputCallback) outputCallback("Flash cancelled by user.", true);
    }
    else if (exitCode == 0)
    {
        SetProgress(BuildState::Completed, "Flash successful!", 1.0f);
        if (outputCallback) outputCallback("Flash completed successfully!", false);
    }
    else
    {
        SetError("Flash failed with exit code " + std::to_string(exitCode));
        if (outputCallback) outputCallback("Flash failed!", true);
    }

    if (progressCallback) progressCallback(GetProgress());
}

void ESPIDFBuilder::DoClean(const std::string& projectPath, BuildOutputCallback outputCallback,
                            BuildProgressCallback progressCallback)
{
    SetProgress(BuildState::Building, "Cleaning build...", 0.0f);
    if (progressCallback) progressCallback(GetProgress());

    if (!IsToolchainInstalled())
    {
        SetError("ESP-IDF is not installed. Please download and install it first.");
        if (progressCallback) progressCallback(GetProgress());
        return;
    }

    std::string buildDir = GetBuildDirectory(projectPath);
    std::string enginePath = GetEnginePath(projectPath);
    if (outputCallback) outputCallback("Cleaning build directory...", false);

    int exitCode = m_Toolchain.ExecuteIDF("idf.py fullclean", buildDir, enginePath, outputCallback, MakeExecContext());

    if (exitCode == 0)
    {
        SetProgress(BuildState::Completed, "Clean successful!", 1.0f);
        if (outputCallback) outputCallback("Clean completed!", false);
    }
    else
    {
        SetError("Clean failed with exit code " + std::to_string(exitCode));
        if (outputCallback) outputCallback("Clean failed!", true);
    }

    if (progressCallback) progressCallback(GetProgress());
}

// ============================================================================
// Build file generation
// ============================================================================

// The flash size the generated sdkconfig selects: 16, 8 or 4 MB.
static uint32_t ConfiguredFlashBytes(const PlatformConfig& config)
{
    const uint32_t mb = config.OptionU32("flashSize") / (1024 * 1024);
    return (mb >= 16 ? 16u : mb >= 8 ? 8u : 4u) * 1024u * 1024u;
}

// The default table's data partition (LittleFS, F:/) starts after the app and
// runs to the end of the flash.
static constexpr uint32_t kDataPartitionOffset = 0x310000;

// Roughly what a folder takes in LittleFS: whole 4 KB blocks per file, a
// block of metadata each, and the two superblocks.
static uint64_t LittleFsFootprint(const fs::path& dir)
{
    constexpr uint64_t kBlock = 4096;
    uint64_t total = 2 * kBlock;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        if (!it->is_regular_file(ec))
            continue;
        const uint64_t size = it->file_size(ec);
        total += (size + kBlock - 1) / kBlock * kBlock + kBlock;
    }
    return total;
}

bool ESPIDFBuilder::GenerateBuildFiles(const std::string& projectPath,
                                       const PlatformConfig& config,
                                       const std::vector<std::string>& packageDefines)
{
    fs::path buildersPath = fs::path(GetBuildDirectory(projectPath));
    fs::path mainPath = buildersPath / "main";
    fs::path boardPath = buildersPath / config.id;

    // Create required directories
    try
    {
        fs::create_directories(mainPath);
        fs::create_directories(boardPath);
    }
    catch (const std::exception&)
    {
        return false;
    }

    // Generate build files (always overwrite — these are managed by the editor)
    if (!GenerateRootCMakeLists(buildersPath.string(), config))
        return false;

    if (!GenerateMainCMakeLists(mainPath.string(), projectPath, config, packageDefines))
        return false;

    // Snapshot sdkconfig.defaults before regeneration to detect changes
    fs::path sdkDefaultsPath = fs::path(boardPath) / "sdkconfig.defaults";
    std::string oldDefaults;
    if (fs::exists(sdkDefaultsPath))
    {
        std::ifstream f(sdkDefaultsPath, std::ios::binary);
        oldDefaults.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }

    if (!GenerateSdkConfigDefaults(boardPath.string(), config))
        return false;

    // ESP-IDF ignores sdkconfig.defaults once sdkconfig exists.
    // If defaults changed, delete stale sdkconfig so it gets regenerated.
    {
        std::string newDefaults;
        if (fs::exists(sdkDefaultsPath))
        {
            std::ifstream f(sdkDefaultsPath, std::ios::binary);
            newDefaults.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }
        if (newDefaults != oldDefaults)
        {
            fs::path sdkconfigPath = buildersPath / "sdkconfig";
            if (fs::exists(sdkconfigPath))
                fs::remove(sdkconfigPath);
        }
    }

    if (!GeneratePartitionsCsv(boardPath.string(), config))
        return false;

    // What goes into the data partition has to fit it: the boot payload, and
    // on internal storage every asset. A board's own partitionTable is its
    // own business; mklittlefs still refuses what does not fit.
    if (config.Option("partitionTable").empty())
    {
        const uint64_t need = LittleFsFootprint(GetBootPayloadDirectory(projectPath));
        const uint64_t have = ConfiguredFlashBytes(config) - kDataPartitionOffset;
        if (need > have)
        {
            DEKI_LOG_ERROR("The game needs about %.1f MB of internal storage and '%s' has %.1f MB. "
                           "Keep its assets on external storage, or use a board with more flash.",
                           need / (1024.0 * 1024.0), config.id.c_str(), have / (1024.0 * 1024.0));
            return false;
        }
    }

    // Load dependency version pins from deki-packages.json
    std::map<std::string, std::string> depOverrides;
    {
        fs::path packagesJson = fs::path(projectPath) / GetPackageManifestFileName();
        if (fs::exists(packagesJson))
        {
            try
            {
                std::ifstream f(packagesJson);
                nlohmann::json jd;
                f >> jd;
                if (jd.contains("dependencies") && jd["dependencies"].is_object())
                    for (auto& [n, v] : jd["dependencies"].items())
                        depOverrides[n] = v.get<std::string>();
            }
            catch (const std::exception& e)
            {
                // Dependency overrides silently lost here would produce a build
                // against the wrong package versions.
                DEKI_LOG_WARNING("ESPIDFBuilder: could not parse dependency overrides (%s); "
                                 "building with the manifest versions",
                                 e.what());
            }
        }
    }

    // Always regenerate idf_component.yml to include package dependencies
    {
        std::ostringstream file;
        file << "# Deki Game - Component Dependencies\n";
        file << "dependencies:\n";
        file << "  joltwallet/littlefs:\n";
        file << "    version: \"*\"\n";
        for (const auto& dep : EditorSettings::GetPlatformDependencies("espidf", depOverrides))
        {
            file << "  " << dep.name << ":\n";
            if (!dep.gitUrl.empty())
                file << "    git: " << dep.gitUrl << "\n";
            if (!dep.version.empty())
                file << "    version: \"" << dep.version << "\"\n";
        }

        const fs::path manifest = mainPath / "idf_component.yml";
        std::string previous;
        {
            std::ifstream in(manifest, std::ios::binary);
            previous.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        if (previous != file.str())
        {
            std::ofstream out(manifest, std::ios::binary | std::ios::trunc);
            if (!out.is_open())
                return false;
            out << file.str();

            // A changed manifest invalidates the component manager's lock. It
            // does not notice on its own: a git dependency stays at the commit
            // the lock recorded even when the manifest asks for another
            // version, so bumping LovyanGFX from 1.2.19 to 1.2.29 went on
            // building 1.2.19. The lock is generated output in this build
            // directory, not anything the user wrote. An unchanged manifest
            // keeps it, so an ordinary rebuild re-resolves nothing.
            std::error_code ec;
            fs::remove(buildersPath / "dependencies.lock", ec);
        }
    }

    // Apply any version overrides to an existing idf_component.yml via MergeDeps
    if (!depOverrides.empty())
    {
        ESPIDFInstaller installer;
        std::vector<DependencyInfo> overrideDeps;
        for (const auto& dep : EditorSettings::GetPlatformDependencies("espidf", depOverrides))
        {
            if (depOverrides.count(dep.name))
            {
                DependencyInfo d;
                d.name = dep.name;
                d.version = dep.version;
                d.gitUrl = dep.gitUrl;
                d.source = dep.gitUrl.empty() ? PackageSourceType::FrameworkRegistry
                                              : PackageSourceType::Custom;
                overrideDeps.push_back(d);
            }
        }
        if (!overrideDeps.empty())
            installer.MergeDeps(projectPath, overrideDeps);
    }

    return true;
}

bool ESPIDFBuilder::GenerateRootCMakeLists(const std::string& espIdfPath, const PlatformConfig& config)
{
    std::string idfTarget = GetIdfTarget(config);
    std::ostringstream file;

    file << "# Generated by Deki Editor — do not edit\n";
    file << "# Platform: " << config.displayName << "\n";
    file << "cmake_minimum_required(VERSION 3.16)\n";
    file << "\n";
    file << "# Set target and sdkconfig defaults before including ESP-IDF\n";
    file << "set(IDF_TARGET \"" << idfTarget << "\")\n";
    file << "set(SDKCONFIG_DEFAULTS \"${CMAKE_CURRENT_SOURCE_DIR}/" << config.id << "/sdkconfig.defaults\")\n";
    file << "\n";
    file << "# Include ESP-IDF CMake functions\n";
    file << "include($ENV{IDF_PATH}/tools/cmake/project.cmake)\n";
    file << "\n";
    file << "project(DekiGame)\n";

    return CMakeGen::WriteIfChanged(fs::path(espIdfPath) / "CMakeLists.txt", file.str());
}

bool ESPIDFBuilder::GenerateMainCMakeLists(const std::string& mainPath, const std::string& projectPath,
                                           const PlatformConfig& config,
                                           const std::vector<std::string>& packageDefines)
{
    // Scan packages and resolve active set (needed before REQUIRES generation)
    std::string enginePath = GetEnginePath(projectPath);
    auto allPackages = CMakeGen::ScanPackageManifests(projectPath);

    // A firmware needs its hardware abstraction layer whether or not any scene
    // names one of its components. The platform says which package that is, so
    // a board on a framework this editor has never heard of names its own
    // instead of needing a line added here.
    std::vector<std::string> allDefines = packageDefines;
    for (const auto& id : config.requiresPackages)
    {
        std::string define;
        for (const auto& pkg : allPackages)
            if (pkg.id == id || pkg.dirName == id)
                define = pkg.buildDefine;

        if (define.empty())
        {
            DEKI_LOG_WARNING("Platform '%s' requires package '%s', which is not installed",
                             config.id.c_str(), id.c_str());
            continue;
        }
        if (std::find(allDefines.begin(), allDefines.end(), define) == allDefines.end())
            allDefines.push_back(define);
    }
    auto activeIds = CMakeGen::ResolveActivePackages(allPackages, allDefines, config.Capabilities());

    // The transform width: the widest any active package or the project
    // declares (CMakeGenUtils, COMPATIBILITY.md).
    std::string transformWhy;
    const std::vector<std::string> transformDefines = CMakeGen::TransformDefines(CMakeGen::ResolveProjectTransformWidth(
        allPackages, CMakeGen::ReadProjectTags(projectPath), &transformWhy));

    // Content this target needs but cannot have. Packages left out because
    // nothing here uses them are normal and silent; a scene on THIS target
    // naming a component from a package this target cannot build is a mistake,
    // and saying so now beats a compiler or linker error later.
    const auto conflicts = FindTargetContentConflicts(projectPath, config.id);
    if (!conflicts.empty())
    {
        DEKI_LOG_ERROR("Cannot build for '%s':", config.id.c_str());
        for (const auto& c : conflicts)
            DEKI_LOG_ERROR("  %s", c.Describe().c_str());
        DEKI_LOG_ERROR("  Remove it from this target's content, or use a platform that provides "
                       "what it needs (set_platform_provides).");
        return false;
    }

    // What this build leaves out (services/FeatureResolver).
    // A simulated build also counts its own boot scene, which holds the QEMU
    // screen (the editor writes it there; the board's is not changed).
    std::vector<std::string> simulationScenes;
    if (m_BuildOptions.simulate)
        simulationScenes.push_back((fs::path(GetBuildDirectory(projectPath)) / kSimulationSceneDir).string());
    const StripPlan strip = ComputeStripPlan(projectPath, config.id, simulationScenes);
    for (const auto& w : strip.warnings)
        DEKI_LOG_WARNING("%s", w.c_str());

    // Build REQUIRES list from platform config + active package ESP-IDF deps.
    // Not named `requires`: that is a keyword since C++20, and the firmware
    // has been compiling at gnu++2b (which ESP-IDF forces) for a while.
    std::string idfRequires = "esp_psram";
    for (const auto& lib : config.OptionList("requiredLibraries"))
    {
        if (idfRequires.find(lib) == std::string::npos)
            idfRequires += " " + lib;
    }
    for (const auto& pkg : allPackages)
    {
        if (activeIds.count(pkg.id) == 0) continue;
        const auto mine = pkg.frameworkDeps.find(GetFrameworkId());
        if (mine == pkg.frameworkDeps.end()) continue;
        for (const auto& dep : mine->second)
        {
            if (idfRequires.find(dep.name) == std::string::npos)
                idfRequires += " " + dep.name;
        }
    }

    std::ostringstream file;

    file << "# Generated by Deki Editor — do not edit\n";
    file << "# Platform: " << config.displayName << "\n";
    file << "idf_component_register(\n";
    file << "    SRCS \"$ENV{DEKI_ENGINE_PATH}/entry/Main.cpp\"\n";
    file << "    INCLUDE_DIRS \".\"\n";
    file << "    REQUIRES " << idfRequires << "\n";
    file << ")\n";
    file << "\n";
    file << "# =============================================================================\n";
    file << "# Deki Engine Integration\n";
    file << "# =============================================================================\n";
    file << "\n";
    file << "# Engine path from environment (set by Deki Editor when building)\n";
    file << "set(DEKI_ENGINE_PATH $ENV{DEKI_ENGINE_PATH})\n";
    file << "if(NOT DEKI_ENGINE_PATH)\n";
    file << "    message(FATAL_ERROR \"DEKI_ENGINE_PATH not set. Build from Deki Editor.\")\n";
    file << "endif()\n";
    file << "\n";
    file << "# Project root: three levels up from generated/build/<platform>\n";
    file << "get_filename_component(DEKI_PROJECT_PATH \"${CMAKE_SOURCE_DIR}/../../..\" ABSOLUTE)\n";
    file << "\n";

    CMakeGen::EmitEngineCoreSourceCollection(file);
    // When something is stripped, the reflection tables the editor left in each
    // package's generated/ (made with every feature present) would reference
    // components this build does not compile, so this build regenerates its own
    // with the same exclusions. When nothing is stripped, today's path: the
    // package copies are compiled as they are.
    file << "message(STATUS \"Deki stripping: " << CMakeGen::EscapeCMakeString(strip.summary) << "\")\n";
    CMakeGen::EmitActivePackageSources(file, allPackages, activeIds, strip.SourceExcludeRegexes(),
                                       strip.AnythingStripped());
    CMakeGen::EmitGeneratedFileSources(file, enginePath, allPackages, activeIds);
    if (strip.AnythingStripped())
    {
        std::string dirs, tags, prefixes, outdirs;
        for (const auto& pkg : allPackages)
        {
            if (activeIds.count(pkg.id) == 0 || pkg.packagePrefix.empty())
                continue;
            dirs += " \"${DEKI_PROJECT_PATH}/packages/" + pkg.dirName + "\"";
            tags += " \"" + pkg.dirName + "\"";
            prefixes += " \"" + pkg.packagePrefix + "\"";
            outdirs += " \"${CMAKE_BINARY_DIR}/refl/" + pkg.dirName + "/generated\"";
        }
        file << "\n# Reflection regenerated for this configuration (stripping is on)\n";
        file << "if(NOT DEKI_GXX16)\n    set(DEKI_GXX16 \"$ENV{DEKI_GXX16}\")\nendif()\n";
        file << "if(NOT DEKI_GXX16)\n    find_program(DEKI_GXX16 NAMES g++-16 g++)\nendif()\n";
        file << "if(NOT DEKI_GXX16)\n"
                "    message(FATAL_ERROR \"Stripping regenerates the reflection tables and needs GCC 16.1+: "
                "set DEKI_GXX16 (variable or environment), or turn stripping off in the Build panel.\")\n"
                "endif()\n";
        file << "include(\"${DEKI_ENGINE_PATH}/cmake/DekiReflectionCodegen.cmake\")\n";
        file << "deki_reflection_codegen(\n";
        file << "    UNIT_PREFIX \"firmware\"\n";
        file << "    PACKAGE_DIRS" << dirs << "\n";
        file << "    PACKAGE_TAGS" << tags << "\n";
        file << "    PACKAGE_PREFIXES" << prefixes << "\n";
        file << "    PACKAGE_OUTDIRS" << outdirs << "\n";
        file << "    GENERATOR_SRC \"${DEKI_ENGINE_PATH}/tools/reflection_codegen.cpp\" GXX \"${DEKI_GXX16}\"\n";
        file << "    INCLUDE_DIRS ${DEKI_PACKAGE_INCLUDE_DIRS} \"${DEKI_ENGINE_PATH}/include\" "
                "\"${DEKI_ENGINE_PATH}/third_party\" \"${DEKI_PROJECT_PATH}/packages\" \"${DEKI_PROJECT_PATH}/src\"\n";
        {
            const auto codegenExcludes = strip.CodegenExcludeRegexes();
            if (!codegenExcludes.empty())
            {
                file << "    EXCLUDE_REGEX";
                for (const auto& rx : codegenExcludes)
                    file << " \"" << CMakeGen::EscapeCMakeString(rx) << "\"";
                file << "\n";
            }
        }
        // The generator parses the package headers as a host program: the
        // configuration's defines, none of the ESP-IDF ones.
        file << "    DEFINES \"DEKI_FAST_ATTR=\"";
        for (const auto& define : transformDefines)
            file << " " << define;
        for (const auto& define : allDefines)
            file << " " << define;
        file << ")\n";
        file << "set(_DEKI_FW_GEN_SRCS \"\")\n";
        file << "foreach(_OUTDIR" << outdirs << ")\n";
        file << "    file(GLOB _g \"${_OUTDIR}/*.gen.cpp\")\n";
        file << "    list(APPEND _DEKI_FW_GEN_SRCS ${_g})\n";
        file << "endforeach()\n";
        file << "target_sources(${COMPONENT_LIB} PRIVATE ${_DEKI_FW_GEN_SRCS})\n";
        // Generate before compiling what is generated. Without this edge ninja
        // is free to compile the previous build's .gen.cpp first, and did: a
        // changed generator never ran, because the stale tables it would have
        // replaced failed to compile before it got the chance. The package DLL
        // build has carried the same edge all along (BuildFileGenerator).
        file << "foreach(_TAG" << tags << ")\n";
        file << "    string(MAKE_C_IDENTIFIER \"${_TAG}\" _RC_TAGID)\n";
        file << "    add_dependencies(${COMPONENT_LIB} ${DEKI_RC_TARGETS_${_RC_TAGID}})\n";
        file << "endforeach()\n";
        file << "foreach(_OUTDIR" << outdirs << ")\n";
        file << "    get_filename_component(_REFL_DIR \"${_OUTDIR}\" DIRECTORY)\n";
        file << "    target_include_directories(${COMPONENT_LIB} PRIVATE \"${_REFL_DIR}\")\n";
        file << "endforeach()\n";
    }

    // Generate package init file that calls each package's RegisterComponents()
    // and registers project game components with ComponentFactory
    fs::path projectSrcPath = fs::path(GetSourceDirectory(projectPath));
    std::string packageInitPath = CMakeGen::GeneratePackageInitFile(
        mainPath, allPackages, activeIds, projectSrcPath);
    file << "\n# Package registration (calls each package's RegisterComponents for firmware builds)\n";
    file << "list(APPEND DEKI_ENGINE_SOURCES \"" << std::filesystem::path(packageInitPath).filename().string() << "\")\n";

    file << "\n";
    file << "target_sources(${COMPONENT_LIB} PRIVATE ${DEKI_ENGINE_SOURCES})\n";

    CMakeGen::EmitIncludePaths(file, "${COMPONENT_LIB}",
                               "${DEKI_PROJECT_PATH}/src");

    // Project sources
    file << "\n";
    file << "# =============================================================================\n";
    file << "# Project Sources\n";
    file << "# =============================================================================\n";
    CMakeGen::EmitProjectSourceCollection(file, "${DEKI_PROJECT_PATH}/src", true);
    file << "\n";
    file << "if(PROJECT_SOURCES)\n";
    file << "    target_sources(${COMPONENT_LIB} PRIVATE ${PROJECT_SOURCES})\n";
    file << "endif()\n";

    // Platform defines (ESP-IDF specific: IRAM_ATTR handling)
    file << "\n";
    file << "# =============================================================================\n";
    file << "# Platform Defines (" << config.displayName << ")\n";
    file << "# =============================================================================\n";
    CMakeGen::EmitPlatformDefines(file, "${COMPONENT_LIB}", config, allDefines);

    // ESP32 define (previously provided by arduino-esp32, now needed explicitly for engine guards)
    file << "\ntarget_compile_definitions(${COMPONENT_LIB} PRIVATE ESP32)\n";
    file << "message(STATUS \"Deki transform: " << CMakeGen::EscapeCMakeString(transformWhy) << "\")\n";
    for (const auto& define : transformDefines)
        file << "target_compile_definitions(${COMPONENT_LIB} PRIVATE " << define << ")\n";

    // Enable logging when requested by build options (DEKI_LOG_ENABLED env var set by ExecuteIDF)
    file << "\nif(DEFINED ENV{DEKI_LOG_ENABLED} AND NOT \"$ENV{DEKI_LOG_ENABLED}\" STREQUAL \"\")\n";
    file << "    target_compile_definitions(${COMPONENT_LIB} PRIVATE DEKI_LOG_ENABLED)\n";
    file << "endif()\n";

    // Enable internal logging when requested (verbose engine diagnostics)
    file << "\nif(DEFINED ENV{DEKI_LOG_INTERNAL_ENABLED} AND NOT \"$ENV{DEKI_LOG_INTERNAL_ENABLED}\" STREQUAL \"\")\n";
    file << "    target_compile_definitions(${COMPONENT_LIB} PRIVATE DEKI_LOG_INTERNAL_ENABLED)\n";
    file << "endif()\n";

    // ESP-IDF specific: IRAM_ATTR
    if (config.OptionBool("useIramAttr", true))
    {
        file << "\n";
        file << "target_compile_definitions(${COMPONENT_LIB} PRIVATE \"DEKI_FAST_ATTR=IRAM_ATTR\")\n";
        file << "\n";
        file << "# Inject esp_attr.h to define IRAM_ATTR before engine headers expand DEKI_FAST_ATTR\n";
        file << "target_compile_options(${COMPONENT_LIB} PRIVATE -include esp_attr.h)\n";
    }

    // LittleFS data partition — create image from spiffs_data/ and flash with firmware
    file << "\n";
    file << "# =============================================================================\n";
    file << "# LittleFS Data Partition (boot.scene, project_data.bin, assets/)\n";
    file << "# =============================================================================\n";
    file << "set(SPIFFS_DATA_DIR \"${CMAKE_CURRENT_SOURCE_DIR}/../spiffs_data\")\n";
    file << "if(EXISTS ${SPIFFS_DATA_DIR})\n";
    file << "    littlefs_create_partition_image(spiffs ${SPIFFS_DATA_DIR} FLASH_IN_PROJECT)\n";
    file << "endif()\n";

    return CMakeGen::WriteIfChanged(fs::path(mainPath) / "CMakeLists.txt", file.str());
}

bool ESPIDFBuilder::GenerateSdkConfigDefaults(const std::string& boardPath, const PlatformConfig& config)
{
    fs::path filePath = fs::path(boardPath) / "sdkconfig.defaults";

    std::ostringstream file;

    std::string idfTarget = config.Option("idfTarget");
    if (idfTarget.empty())
        idfTarget = config.Option("mcuChip");

    file << "# Generated by Deki Editor — do not edit\n";
    file << "# Platform: " << config.displayName << "\n";
    file << "\n";
    file << "# Target\n";
    file << "CONFIG_IDF_TARGET=\"" << idfTarget << "\"\n";
    file << "\n";

    // PSRAM configuration. PSRAM is what this target calls the engine's
    // external memory region, so the platform's externalMemorySize is its size.
    if (config.externalMemorySize > 0)
    {
        file << "# PSRAM Configuration\n";
        file << "CONFIG_SPIRAM=y\n";

        // PSRAM SPI mode: "oct" for Octal SPI, default is Quad
        if (config.Option("psramMode", "quad") == "oct")
        {
            file << "CONFIG_SPIRAM_MODE_OCT=y\n";
        }
        file << "CONFIG_SPIRAM_SPEED_80M=y\n";
        file << "CONFIG_SPIRAM_BOOT_INIT=y\n";
        file << "\n";
    }

    // CPU frequency
    uint32_t cpuMhz = config.OptionU32("cpuFreqHz") / 1000000;
    if (cpuMhz == 0) cpuMhz = 240;  // Default

    file << "# CPU Configuration\n";
    if (cpuMhz >= 240)
        file << "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y\n";
    else if (cpuMhz >= 160)
        file << "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_160=y\n";
    else if (cpuMhz >= 80)
        file << "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_80=y\n";
    file << "\n";

    // FreeRTOS
    file << "# FreeRTOS\n";
    file << "CONFIG_FREERTOS_HZ=1000\n";
    file << "\n";

    // Flash size
    uint32_t flashMB = config.OptionU32("flashSize") / (1024 * 1024);
    if (flashMB == 0) flashMB = 4;  // Default

    file << "# Flash Configuration\n";
    if (flashMB >= 16)
    {
        file << "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y\n";
        file << "CONFIG_ESPTOOLPY_FLASHMODE_QIO=y\n";
    }
    else if (flashMB >= 8)
    {
        file << "CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y\n";
    }
    else
    {
        file << "CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y\n";
    }
    file << "\n";

    // Custom partition table
    file << "# Partition Table\n";
    file << "CONFIG_PARTITION_TABLE_CUSTOM=y\n";
    file << "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"" << config.id << "/partitions.csv\"\n";
    file << "CONFIG_PARTITION_TABLE_FILENAME=\"" << config.id << "/partitions.csv\"\n";
    file << "\n";

    // Stack configuration
    file << "# Stack Sizes\n";
    file << "CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384\n";
    file << "\n";

    // FAT filesystem — long filename support required for SD card assets
    // Asset files use GUID-based names (36 chars) and asset_table.bin (15 chars),
    // both exceeding the 8.3 DOS filename limit.
    file << "# FAT Filesystem (SD card)\n";
    file << "CONFIG_FATFS_LFN_HEAP=y\n";
    file << "CONFIG_FATFS_MAX_LFN=255\n";

    // The board's own options, last so they override anything chosen above.
    // This file is rewritten on every build, so without this a board had no way
    // to say anything the editor did not already know how to emit, and editing
    // it by hand lasted exactly one build. Boards differ in ways this generator
    // should not have to enumerate.
    const std::vector<std::string> extraSdkconfig = config.OptionList("sdkconfig");
    if (!extraSdkconfig.empty())
    {
        file << "\n# From the platform's \"sdkconfig\"\n";
        for (const auto& line : extraSdkconfig)
            file << line << "\n";
    }

    // A simulated build runs in QEMU, which reads the flash in DIO at 40 MHz:
    // a board's QIO build boots there but cannot mount its LittleFS partition.
    // Its PSRAM is quad: QEMU (esp_develop_9.2.2) crashes booting an octal
    // PSRAM build with 8 MB. The game sees the same PSRAM size. Its console is
    // UART0, the serial port QEMU has (a board's may be USB).
    // Last, so it wins over the board's own lines. The board's build is not
    // affected; this one has its own directory.
    if (m_BuildOptions.simulate)
    {
        file << "\n# Simulated in QEMU\n";
        file << "CONFIG_ESPTOOLPY_FLASHMODE_DIO=y\n";
        file << "CONFIG_ESPTOOLPY_FLASHFREQ_40M=y\n";
        file << "CONFIG_ESP_CONSOLE_UART_DEFAULT=y\n";
        if (config.externalMemorySize > 0)
            file << "CONFIG_SPIRAM_MODE_QUAD=y\n";
    }

    return CMakeGen::WriteIfChanged(filePath, file.str());
}

bool ESPIDFBuilder::GeneratePartitionsCsv(const std::string& boardPath, const PlatformConfig& config)
{
    fs::path filePath = fs::path(boardPath) / "partitions.csv";

    std::ostringstream file;

    const std::string partitionTable = config.Option("partitionTable");
    if (!partitionTable.empty())
    {
        // Use custom partition table from platform config
        file << partitionTable;
    }
    else
    {
        // Default partition table. The data partition (F:/: the boot payload,
        // and the assets when they are kept inside) takes the rest of the
        // flash, so a 16 MB board has about 13 MB for them.
        const uint32_t dataSize = ConfiguredFlashBytes(config) - kDataPartitionOffset;
        file << "# Deki Game Partition Table\n";
        file << "# Name,   Type, SubType, Offset,   Size,     Flags\n";
        file << "nvs,      data, nvs,     0x9000,   0x5000,\n";
        file << "otadata,  data, ota,     0xe000,   0x2000,\n";
        file << "app0,     app,  ota_0,   0x10000,  0x300000,\n";
        file << "spiffs,   data, spiffs,  0x" << std::hex << kDataPartitionOffset << ", 0x" << dataSize << std::dec
             << ",\n";
    }

    return CMakeGen::WriteIfChanged(filePath, file.str());
}

// ============================================================================
// ESP-IDF Platform Editor UI
// ============================================================================

class ESPIDFEditorUI : public IPlatformEditorUI
{
public:
    ESPIDFEditorUI(const PlatformConfig& config, const std::vector<std::string>& targets)
        : m_Targets(targets)
    {
        // Chip
        const std::string chip = config.Option("mcuChip");
        strncpy(m_McuChip, chip.c_str(), sizeof(m_McuChip) - 1);
        m_ChipIndex = 0;
        for (int i = 0; i < (int)m_Targets.size(); i++)
        {
            if (m_Targets[i] == chip)
            {
                m_ChipIndex = i;
                break;
            }
        }

        // Display
        m_ScreenWidth = config.screenWidth > 0 ? config.screenWidth : 320;
        m_ScreenHeight = config.screenHeight > 0 ? config.screenHeight : 240;

        // Memory
        m_FlashSizeMB = static_cast<int>(config.OptionU32("flashSize") / (1024 * 1024));
        m_PsramSizeMB = static_cast<int>(config.externalMemorySize / (1024 * 1024));
        m_PsramModeIndex = (config.Option("psramMode", "quad") == "oct") ? 1 : 0;
        m_CpuFreqMHz = static_cast<int>(config.OptionU32("cpuFreqHz") / 1000000);

        // Compiler
        m_Defines = config.defines;
        m_RequiredLibraries = config.OptionList("requiredLibraries");
        m_UseIramAttr = config.OptionBool("useIramAttr", true);
    }

    void Draw() override
    {
        auto& ui = DekiEditor::EditorUI::Get();

        // --- Chip ---
        if (!m_Targets.empty() && DekiEditor::SchematicSectionBegin("Chip"))
        {
            DekiEditor::BeginPropertyContext();
            ui.PropertyRow("MCU Chip");
            std::vector<const char*> chips;
            for (const std::string& t : m_Targets)
                chips.push_back(t.c_str());
            int chip = m_ChipIndex;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (DekiEditor::SchematicCombo("##McuChip", &chip, chips.data(), (int)chips.size()) &&
                chip >= 0 && chip < (int)m_Targets.size())
            {
                m_ChipIndex = chip;
                strncpy(m_McuChip, m_Targets[chip].c_str(), sizeof(m_McuChip) - 1);
            }
            DekiEditor::EndPropertyContext();
            DekiEditor::SchematicSectionEnd();
        }

        // --- Display ---
        if (DekiEditor::SchematicSectionBegin("Display"))
        {
            DekiEditor::BeginPropertyContext();
            ui.PropertyRow("Resolution");
            const float gap = ImGui::GetStyle().ItemSpacing.x;
            const float fieldW = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("x").x - gap * 2.0f) * 0.5f;
            ImGui::SetNextItemWidth(fieldW);
            DekiEditor::SchematicDragInt("##ScreenWidth", &m_ScreenWidth, 1.0f, 1, 16384);
            ImGui::SameLine(0.0f, gap);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(DekiEditor::Palette::Dim, "x");
            ImGui::SameLine(0.0f, gap);
            ImGui::SetNextItemWidth(fieldW);
            DekiEditor::SchematicDragInt("##ScreenHeight", &m_ScreenHeight, 1.0f, 1, 16384);
            if (m_ScreenWidth < 1) m_ScreenWidth = 1;
            if (m_ScreenHeight < 1) m_ScreenHeight = 1;
            DekiEditor::EndPropertyContext();
            DekiEditor::SchematicSectionEnd();
        }

        // --- Memory ---
        if (DekiEditor::SchematicSectionBegin("Memory", 0))
        {
            DekiEditor::BeginPropertyContext();
            // A number field with the common values as presets beside it.
            auto presetRow = [&ui](const char* label, const char* id, int* v, std::initializer_list<int> presets)
            {
                ui.PropertyRow(label);
                ImGui::PushID(id);
                const float gap = ImGui::GetStyle().ItemSpacing.x * 0.5f;
                float presetsW = 0.0f;
                for (int p : presets)
                    presetsW += ImGui::CalcTextSize(std::to_string(p).c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f + gap;
                ImGui::SetNextItemWidth(std::max(ImGui::GetFrameHeight() * 2.0f, ImGui::GetContentRegionAvail().x - presetsW));
                DekiEditor::SchematicDragInt("##v", v, 0.1f, 0, 4096);
                if (*v < 0) *v = 0;
                for (int p : presets)
                {
                    ImGui::SameLine(0.0f, gap);
                    const std::string text = std::to_string(p);
                    if (ImGui::Button(text.c_str()))
                        *v = p;
                }
                ImGui::PopID();
            };
            presetRow("Flash (MB)", "flash", &m_FlashSizeMB, { 4, 8, 16 });
            presetRow("PSRAM (MB)", "psram", &m_PsramSizeMB, { 0, 2, 8 });
            if (m_PsramSizeMB > 0)
            {
                ui.PropertyRow("PSRAM Mode");
                static const char* psramModes[] = { "Quad SPI", "Octal SPI" };
                ImGui::SetNextItemWidth(-FLT_MIN);
                DekiEditor::SchematicCombo("##PsramMode", &m_PsramModeIndex, psramModes, 2);
            }
            presetRow("CPU (MHz)", "cpu", &m_CpuFreqMHz, { 80, 160, 240 });
            DekiEditor::EndPropertyContext();
            DekiEditor::SchematicSectionEnd();
        }

        // --- Compiler ---
        if (DekiEditor::SchematicSectionBegin("Compiler & Build", 0))
        {
            DekiEditor::SchematicCheckbox("Use IRAM for fast functions (IRAM_ATTR)", &m_UseIramAttr);
            DrawStringListEditor("Preprocessor Defines", m_Defines, m_NewItemBuf, sizeof(m_NewItemBuf));
            DrawStringListEditor("Required Libraries", m_RequiredLibraries, m_NewItemBuf2, sizeof(m_NewItemBuf2));
            DekiEditor::SchematicSectionEnd();
        }
    }

    void ApplyToConfig(PlatformConfig& config) const override
    {
        config.SetOption("mcuChip", m_McuChip);
        config.SetOption("idfTarget", m_McuChip);  // ESP-IDF target = chip
        config.framework = "espidf";

        config.screenWidth = m_ScreenWidth;
        config.screenHeight = m_ScreenHeight;

        config.SetOptionU32("flashSize", static_cast<uint32_t>(m_FlashSizeMB) * 1024 * 1024);
        config.externalMemorySize = static_cast<uint32_t>(m_PsramSizeMB) * 1024 * 1024;
        config.SetOption("psramMode", (m_PsramModeIndex == 1) ? "oct" : "quad");
        config.SetOptionU32("cpuFreqHz", static_cast<uint32_t>(m_CpuFreqMHz) * 1000000);

        config.defines = m_Defines;
        config.SetOptionList("requiredLibraries", m_RequiredLibraries);
        config.SetOptionBool("useIramAttr", m_UseIramAttr);
    }

private:
    std::vector<std::string> m_Targets;
    int m_ChipIndex = 0;
    char m_McuChip[64] = "";

    int m_ScreenWidth = 320;
    int m_ScreenHeight = 240;

    int m_FlashSizeMB = 0;
    int m_PsramSizeMB = 0;
    int m_PsramModeIndex = 0;  // 0=Quad, 1=Octal
    int m_CpuFreqMHz = 0;

    std::vector<std::string> m_Defines;
    std::vector<std::string> m_RequiredLibraries;
    char m_NewItemBuf[256] = "";
    char m_NewItemBuf2[256] = "";
    bool m_UseIramAttr = true;
};

std::unique_ptr<IPlatformEditorUI> ESPIDFBuilder::CreateEditorUI(const PlatformConfig& config) const
{
    return std::make_unique<ESPIDFEditorUI>(config, SupportedIdfTargets());
}

}  // namespace DekiEditor

// ---------------------------------------------------------------------------
// Builder plugin entry points
//
// This backend was compiled into DekiEditor.exe. It lives in the package for
// the target it serves now and reaches the editor through the same plugin ABI
// a third-party or NDA'd backend uses - so that path is the only path,
// exercised on every build, and cannot quietly rot.
//
// In editor/ so the editor-side package DLL picks it up and firmware builds,
// which filter editor/ out, do not.
// ---------------------------------------------------------------------------

#include <deki-editor/build/BuilderPlugin.h>

extern "C" {

DEKI_BUILDER_API const DekiBuilderAbi* DekiBuilder_GetAbi(void)
{
    static const DekiBuilderAbi abi =
        DekiBuilder_ThisAbi((uint32_t)sizeof(DekiEditor::PlatformConfig),
                            (uint32_t)sizeof(DekiEditor::CMakeGen::PackageEntry));
    return &abi;
}

DEKI_BUILDER_API const char* DekiBuilder_GetName(void) { return "ESP-IDF Builder"; }
DEKI_BUILDER_API const char* DekiBuilder_GetVersion(void) { return "1.0.0"; }
DEKI_BUILDER_API int DekiBuilder_GetBuilderCount(void) { return 1; }

DEKI_BUILDER_API DekiEditor::ITargetBuilder* DekiBuilder_CreateBuilder(int index)
{
    return index == 0 ? new DekiEditor::ESPIDFBuilder() : nullptr;
}

DEKI_BUILDER_API void DekiBuilder_DestroyBuilder(DekiEditor::ITargetBuilder* builder)
{
    delete builder;  // in THIS module: its vtable and operator delete live here
}

}  // extern "C"
