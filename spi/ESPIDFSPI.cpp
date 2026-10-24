#include "ESPIDFSPI.h"

#if __has_include("IDekiSPI.h")
#include <deki/LogSystem.h>
#include <cstring>

#if defined(ESP32)
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#endif

namespace DekiEsp32
{

#if defined(ESP32)

namespace
{

// What the callbacks drive travels in the transaction's user field, so they
// need no allocation and nothing else: bit 0 is the DC level and bit 1 says
// DC is driven; bit 2 says chip select is driven here, bit 3 that the
// transfer keeps it low after; the DC pin is bits 8-15, chip select's 16-23.
constexpr uintptr_t kDcDrive = 2;
constexpr uintptr_t kCsDrive = 4;
constexpr uintptr_t kCsKeep = 8;

void* EncodeUser(int32_t dcPin, int8_t dcLevel, int32_t csPin, bool keepCs)
{
    uintptr_t user = 0;
    if (dcPin >= 0 && dcLevel >= 0)
    {
        user |= (static_cast<uintptr_t>(dcPin) << 8) | kDcDrive | (dcLevel ? 1u : 0u);
    }
    if (csPin >= 0)
    {
        user |= (static_cast<uintptr_t>(csPin) << 16) | kCsDrive | (keepCs ? kCsKeep : 0u);
    }
    return reinterpret_cast<void*>(user);
}

// Runs in the driver's interrupt, just before the transaction is clocked.
void IRAM_ATTR BeforeTransfer(spi_transaction_t* t)
{
    const auto user = reinterpret_cast<uintptr_t>(t->user);
    if (user & kDcDrive)
    {
        gpio_set_level(static_cast<gpio_num_t>((user >> 8) & 0xFF), static_cast<uint32_t>(user & 1));
    }
    if (user & kCsDrive)
    {
        gpio_set_level(static_cast<gpio_num_t>((user >> 16) & 0xFF), 0);
    }
}

// Runs in the driver's interrupt once the transaction is done.
void IRAM_ATTR AfterTransfer(spi_transaction_t* t)
{
    const auto user = reinterpret_cast<uintptr_t>(t->user);
    if ((user & kCsDrive) && !(user & kCsKeep))
    {
        gpio_set_level(static_cast<gpio_num_t>((user >> 16) & 0xFF), 1);
    }
}

TickType_t ToTicks(uint32_t timeoutMs)
{
    return timeoutMs == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
}

}  // namespace

ESPIDFSPIDevice::ESPIDFSPIDevice(void* handle, void* readHandle, int32_t softCsPin, const DekiSpi::DeviceConfig& config,
                                 size_t maxTransferBytes)
    : m_Handle(handle),
      m_ReadHandle(readHandle),
      m_SoftCsPin(softCsPin),
      m_DcPin(config.dcPin),
      m_HalfDuplex(config.halfDuplex),
      m_MaxTransferBytes(maxTransferBytes),
      m_SlotCount(config.queueDepth > 0 ? config.queueDepth : 1)
{
    if (!m_Slots.Allocate(m_SlotCount * sizeof(spi_transaction_ext_t), Deki::Memory::Internal))
    {
        DEKI_LOG_ERROR("ESPIDFSPI: no memory for %u transfer slots", (unsigned)m_SlotCount);
    }
}

ESPIDFSPIDevice::~ESPIDFSPIDevice()
{
    Wait(0, UINT32_MAX);
    while (m_AcquireDepth > 0)
    {
        Release();
    }
    if (m_Handle)
    {
        spi_bus_remove_device(static_cast<spi_device_handle_t>(m_Handle));
    }
    if (m_ReadHandle)
    {
        spi_bus_remove_device(static_cast<spi_device_handle_t>(m_ReadHandle));
    }
}

// Fills `out` for `t`. A write of up to four bytes travels in the
// transaction itself: the driver would otherwise copy a buffer that is not in
// DMA memory (a command's parameters on the stack) into one it allocates, for
// every command, and a queued one outlives the caller's buffer. `smallRx`
// does the same for a read, which only a blocking transfer can copy back.
static bool Fill(spi_transaction_ext_t& out, const DekiSpi::Transfer& t, bool halfDuplex, int32_t dcPin,
                 int32_t softCsPin, bool smallRx)
{
    memset(&out, 0, sizeof(out));
    spi_transaction_t& b = out.base;
    if (t.lines == DekiSpi::Lines::Dual)
    {
        b.flags |= SPI_TRANS_MODE_DIO;
    }
    else if (t.lines == DekiSpi::Lines::Quad)
    {
        b.flags |= SPI_TRANS_MODE_QIO;
    }
    b.flags |= SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
    out.command_bits = t.cmdBits;
    out.address_bits = t.addrBits;
    b.cmd = t.cmd;
    b.addr = t.addr;
    if (t.keepCs && softCsPin < 0)
    {
        b.flags |= SPI_TRANS_CS_KEEP_ACTIVE;
    }

    b.length = t.txBytes * 8;
    if (!halfDuplex && t.rxBytes > t.txBytes)
    {
        return false;  // full duplex reads only while it writes
    }
    b.rxlength = t.rxBytes * 8;
    if (t.tx && t.txBytes <= 4)
    {
        b.flags |= SPI_TRANS_USE_TXDATA;
        memcpy(b.tx_data, t.tx, t.txBytes);
    }
    else
    {
        b.tx_buffer = t.tx;
        // A buffer in PSRAM is read by DMA where it is (the S3's GDMA reaches
        // external memory), after the driver writes the CPU's cache back, and
        // not copied into internal RAM first. Aligned as AllocateDma
        // aligns it, nothing is copied at all.
        if (esp_ptr_external_ram(t.tx))
        {
            b.flags |= SPI_TRANS_DMA_USE_PSRAM;
        }
    }
    if (smallRx && t.rx && t.rxBytes <= 4)
    {
        b.flags |= SPI_TRANS_USE_RXDATA;
    }
    else
    {
        b.rx_buffer = t.rx;
    }
    b.user = EncodeUser(dcPin, t.dc, softCsPin, t.keepCs);
    return true;
}

bool ESPIDFSPIDevice::Transmit(const DekiSpi::Transfer& t)
{
    if (!m_Handle || (t.keepCs && m_AcquireDepth == 0))
    {
        return false;
    }
    // The driver refuses a polling transaction while queued ones are pending.
    if (!Wait(0, UINT32_MAX))
    {
        return false;
    }
    spi_transaction_ext_t trans;
    if (!Fill(trans, t, m_HalfDuplex, m_DcPin, m_SoftCsPin, true))
    {
        return false;
    }
    // A read at the read clock goes through the read handle. The bus is
    // held by this device's other handle, if by any: let it go meanwhile,
    // chip select high, and take it back.
    const bool read = m_ReadHandle && t.rxBytes > 0;
    if (read && m_AcquireDepth > 0)
    {
        spi_device_release_bus(static_cast<spi_device_handle_t>(m_Handle));
    }
    const esp_err_t err =
        spi_device_polling_transmit(static_cast<spi_device_handle_t>(read ? m_ReadHandle : m_Handle), &trans.base);
    if (read && m_AcquireDepth > 0)
    {
        spi_device_acquire_bus(static_cast<spi_device_handle_t>(m_Handle), portMAX_DELAY);
    }
    if (err != ESP_OK)
    {
        return false;
    }
    if (trans.base.flags & SPI_TRANS_USE_RXDATA)
    {
        memcpy(t.rx, trans.base.rx_data, t.rxBytes);
    }
    return true;
}

bool ESPIDFSPIDevice::Queue(const DekiSpi::Transfer& t)
{
    if (!m_Handle || !m_Slots || (t.keepCs && m_AcquireDepth == 0))
    {
        return false;
    }
    if (t.txBytes > m_MaxTransferBytes || t.rxBytes > m_MaxTransferBytes)
    {
        DEKI_LOG_ERROR("ESPIDFSPI: a %u-byte transfer is longer than the bus takes (%u)",
                       (unsigned)(t.txBytes > t.rxBytes ? t.txBytes : t.rxBytes), (unsigned)m_MaxTransferBytes);
        return false;
    }
    // Every slot in use: the oldest transfer must finish before its slot is
    // filled again.
    if (m_InFlight >= m_SlotCount && !Wait(m_SlotCount - 1, UINT32_MAX))
    {
        return false;
    }
    auto* slot = reinterpret_cast<spi_transaction_ext_t*>(m_Slots.Data()) + m_NextSlot;
    if (!Fill(*slot, t, m_HalfDuplex, m_DcPin, m_SoftCsPin, false))
    {
        return false;
    }
    if (spi_device_queue_trans(static_cast<spi_device_handle_t>(m_Handle), &slot->base, portMAX_DELAY) != ESP_OK)
    {
        return false;
    }
    m_NextSlot = (m_NextSlot + 1) % m_SlotCount;
    ++m_InFlight;
    return true;
}

bool ESPIDFSPIDevice::Wait(size_t leaveInFlight, uint32_t timeoutMs)
{
    while (m_InFlight > leaveInFlight)
    {
        spi_transaction_t* done = nullptr;
        if (spi_device_get_trans_result(static_cast<spi_device_handle_t>(m_Handle), &done, ToTicks(timeoutMs)) !=
            ESP_OK)
        {
            return false;
        }
        --m_InFlight;
    }
    return true;
}

bool ESPIDFSPIDevice::Acquire()
{
    if (!m_Handle)
    {
        return false;
    }
    if (m_AcquireDepth == 0 &&
        spi_device_acquire_bus(static_cast<spi_device_handle_t>(m_Handle), portMAX_DELAY) != ESP_OK)
    {
        return false;
    }
    ++m_AcquireDepth;
    return true;
}

void ESPIDFSPIDevice::Release()
{
    if (m_AcquireDepth == 0)
    {
        return;
    }
    if (--m_AcquireDepth == 0)
    {
        // The bus goes to the next device only once this one's transfers are
        // out.
        Wait(0, UINT32_MAX);
        spi_device_release_bus(static_cast<spi_device_handle_t>(m_Handle));
    }
}

uint32_t ESPIDFSPIDevice::GetClockHz() const
{
    int khz = 0;
    if (!m_Handle || spi_device_get_actual_freq(static_cast<spi_device_handle_t>(m_Handle), &khz) != ESP_OK)
    {
        return 0;
    }
    return static_cast<uint32_t>(khz) * 1000u;
}

#else  // !ESP32: a desktop build compiles the backend to stubs.

ESPIDFSPIDevice::ESPIDFSPIDevice(void* handle, void* readHandle, int32_t softCsPin, const DekiSpi::DeviceConfig& config,
                                 size_t maxTransferBytes)
    : m_Handle(handle),
      m_ReadHandle(readHandle),
      m_SoftCsPin(softCsPin),
      m_DcPin(config.dcPin),
      m_HalfDuplex(config.halfDuplex),
      m_MaxTransferBytes(maxTransferBytes)
{
}
ESPIDFSPIDevice::~ESPIDFSPIDevice() = default;
bool ESPIDFSPIDevice::Transmit(const DekiSpi::Transfer&)
{
    return false;
}
bool ESPIDFSPIDevice::Queue(const DekiSpi::Transfer&)
{
    return false;
}
bool ESPIDFSPIDevice::Wait(size_t, uint32_t)
{
    return true;
}
bool ESPIDFSPIDevice::Acquire()
{
    return false;
}
void ESPIDFSPIDevice::Release()
{
}
uint32_t ESPIDFSPIDevice::GetClockHz() const
{
    return 0;
}

#endif

// ---- Bus -------------------------------------------------------------------

ESPIDFSPIBus::~ESPIDFSPIBus()
{
    Shutdown();
}

void ESPIDFSPIBus::Configure(const Deki::PackageConfig& config)
{
    m_PinSclk = config.GetPin("SCLK", -1);
    m_PinMosi = config.GetPin("MOSI", -1);
    m_PinMiso = config.GetPin("MISO", -1);
    m_PinIo2 = config.GetPin("IO2", -1);
    m_PinIo3 = config.GetPin("IO3", -1);
    m_Host = config.GetInt("spiHost", 1);
    const int maxBytes = config.GetInt("maxTransferBytes", 32768);
    m_MaxTransferBytes = maxBytes > 0 ? static_cast<size_t>(maxBytes) : 32768;
}

bool ESPIDFSPIBus::Initialize()
{
#if defined(ESP32)
    if (m_Started)
    {
        return true;
    }
    if (m_PinSclk < 0)
    {
        m_LastError = "ESPIDFSPI: no clock pin configured";
        m_State = Deki::PackageState::Error;
        return false;
    }

    spi_bus_config_t cfg = {};
    cfg.sclk_io_num = m_PinSclk;
    cfg.mosi_io_num = m_PinMosi;
    cfg.miso_io_num = m_PinMiso;
    cfg.quadwp_io_num = m_PinIo2;
    cfg.quadhd_io_num = m_PinIo3;
    // Unused, not GPIO 0: zeroed, spi_common would route GPIO 0 (the BOOT
    // button on most boards) to the bus as an octal data line.
    cfg.data4_io_num = -1;
    cfg.data5_io_num = -1;
    cfg.data6_io_num = -1;
    cfg.data7_io_num = -1;
    cfg.max_transfer_sz = static_cast<int>(m_MaxTransferBytes);
    cfg.flags = SPICOMMON_BUSFLAG_MASTER | (IsQuad() ? SPICOMMON_BUSFLAG_QUAD : 0);

    const auto host = static_cast<spi_host_device_t>(m_Host);
    const esp_err_t err = spi_bus_initialize(host, &cfg, SPI_DMA_CH_AUTO);
    if (err == ESP_ERR_INVALID_STATE)
    {
        // Another driver started it first. Its devices and ours share it; it
        // is that driver's to free.
        DEKI_LOG_WARNING("ESPIDFSPI: SPI host %d was already started by another driver; joining it. Transfers are "
                         "limited to what that driver set up.",
                         m_Host);
        m_OwnsBus = false;
    }
    else if (err != ESP_OK)
    {
        m_LastError = std::string("ESPIDFSPI: spi_bus_initialize: ") + esp_err_to_name(err);
        m_State = Deki::PackageState::Error;
        return false;
    }
    else
    {
        m_OwnsBus = true;
    }

    m_Started = true;
    m_State = Deki::PackageState::Initialized;
    DEKI_LOG_INFO("ESPIDFSPI: host %d up (SCLK=%d MOSI=%d MISO=%d IO2=%d IO3=%d, transfers up to %u bytes)", m_Host,
                  m_PinSclk, m_PinMosi, m_PinMiso, m_PinIo2, m_PinIo3, (unsigned)m_MaxTransferBytes);
    return true;
#else
    m_LastError = "ESPIDFSPI: hardware path only built for ESP32";
    m_State = Deki::PackageState::Error;
    return false;
#endif
}

void ESPIDFSPIBus::Shutdown()
{
    // Devices before the bus: freeing a bus that still has devices fails.
    m_Devices.clear();
#if defined(ESP32)
    if (m_Started && m_OwnsBus)
    {
        spi_bus_free(static_cast<spi_host_device_t>(m_Host));
    }
#endif
    m_Started = false;
    m_OwnsBus = false;
    m_State = Deki::PackageState::Uninitialized;
}

DekiSpi::IDekiSPIDevice* ESPIDFSPIBus::AddDevice(const DekiSpi::DeviceConfig& config)
{
#if defined(ESP32)
    if (!m_Started)
    {
        m_LastError = "ESPIDFSPI: AddDevice before the bus is started";
        return nullptr;
    }
    if (config.dcPin >= 0)
    {
        gpio_config_t dc = {};
        dc.pin_bit_mask = 1ULL << config.dcPin;
        dc.mode = GPIO_MODE_OUTPUT;
        gpio_config(&dc);
    }

    // Read at a clock of its own: a second handle for the reads, and chip
    // select driven by the callbacks for both, held high between transfers.
    const bool readHandle = config.readClockHz > 0 && config.readClockHz != config.clockHz;
    const int32_t softCs = readHandle ? config.csPin : -1;
    if (softCs >= 0)
    {
        gpio_config_t cs = {};
        cs.pin_bit_mask = 1ULL << softCs;
        cs.mode = GPIO_MODE_OUTPUT;
        gpio_config(&cs);
        gpio_set_level(static_cast<gpio_num_t>(softCs), 1);
    }

    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = static_cast<int>(config.clockHz);
    dev.mode = config.mode;
    dev.spics_io_num = readHandle ? -1 : config.csPin;
    dev.queue_size = config.queueDepth > 0 ? config.queueDepth : 1;
    // Half duplex and no dummy cycle: a write-only device then runs at
    // 80 MHz on GPIO-matrix pins, which the driver refuses in full duplex.
    dev.flags = config.halfDuplex ? (SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY) : 0;
    if (config.threeWire)
    {
        dev.flags |= SPI_DEVICE_3WIRE;
    }
    dev.pre_cb = (config.dcPin >= 0 || softCs >= 0) ? &BeforeTransfer : nullptr;
    dev.post_cb = softCs >= 0 ? &AfterTransfer : nullptr;

    spi_device_handle_t handle = nullptr;
    esp_err_t err = spi_bus_add_device(static_cast<spi_host_device_t>(m_Host), &dev, &handle);
    if (err != ESP_OK)
    {
        m_LastError = std::string("ESPIDFSPI: spi_bus_add_device: ") + esp_err_to_name(err);
        DEKI_LOG_ERROR("%s", m_LastError.c_str());
        return nullptr;
    }
    spi_device_handle_t read = nullptr;
    if (readHandle)
    {
        spi_device_interface_config_t r = dev;
        r.clock_speed_hz = static_cast<int>(config.readClockHz);
        r.queue_size = 1;
        err = spi_bus_add_device(static_cast<spi_host_device_t>(m_Host), &r, &read);
        if (err != ESP_OK)
        {
            spi_bus_remove_device(handle);
            m_LastError = std::string("ESPIDFSPI: spi_bus_add_device (reads): ") + esp_err_to_name(err);
            DEKI_LOG_ERROR("%s", m_LastError.c_str());
            return nullptr;
        }
    }
    m_Devices.push_back(std::make_unique<ESPIDFSPIDevice>(handle, read, softCs, config, m_MaxTransferBytes));
    return m_Devices.back().get();
#else
    (void)config;
    return nullptr;
#endif
}

}  // namespace DekiEsp32

#endif  // __has_include("IDekiSPI.h")
