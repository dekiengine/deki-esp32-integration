#include "ESPIDFWiFi.h"
#include "../NvsFlash.h"
#include <deki/LogSystem.h>

#include <cstring>

#if defined(ESP32)
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#endif

namespace DekiEsp32
{

namespace
{

#if defined(ESP32)
static bool s_StackInited = false;
static bool s_Connected = false;
static EventGroupHandle_t s_WifiEvents = nullptr;
constexpr int BIT_CONNECTED = BIT0;
constexpr int BIT_DISCONNECT = BIT1;

void WifiEventHandler(void* /*arg*/, esp_event_base_t base, int32_t id, void* /*data*/)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        s_Connected = false;
        xEventGroupSetBits(s_WifiEvents, BIT_DISCONNECT);
        // Best-effort reconnect, limited by Connect's timeoutMs.
        esp_wifi_connect();
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        s_Connected = true;
        xEventGroupSetBits(s_WifiEvents, BIT_CONNECTED);
    }
}

bool InitStackOnce()
{
    if (s_StackInited)
    {
        return true;
    }

    if (!EnsureNvsFlash())
    {
        return false;
    }
    if (esp_netif_init() != ESP_OK)
    {
        DEKI_LOG_ERROR("[wifi] esp_netif_init failed");
        return false;
    }
    esp_err_t le = esp_event_loop_create_default();
    if (le != ESP_OK && le != ESP_ERR_INVALID_STATE)
    {
        DEKI_LOG_ERROR("[wifi] esp_event_loop_create_default failed");
        return false;
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK)
    {
        DEKI_LOG_ERROR("[wifi] esp_wifi_init failed");
        return false;
    }

    s_WifiEvents = xEventGroupCreate();
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiEventHandler, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiEventHandler, nullptr);

    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK)
    {
        DEKI_LOG_ERROR("[wifi] esp_wifi_set_mode(STA) failed");
        return false;
    }

    s_StackInited = true;
    return true;
}
#endif  // ESP32

}  // namespace

bool ESPIDFWiFi::Initialize()
{
    m_State = Deki::PackageState::Initialized;
    return true;
}

void ESPIDFWiFi::Shutdown()
{
#if defined(ESP32)
    if (!s_StackInited)
    {
        return;
    }
    esp_wifi_stop();
    esp_wifi_deinit();
    s_StackInited = false;
    s_Connected = false;
#endif
    m_State = Deki::PackageState::Uninitialized;
}

bool ESPIDFWiFi::Connect(const char* ssid, const char* password, uint32_t timeoutMs)
{
#if defined(ESP32)
    if (!ssid || ssid[0] == '\0')
    {
        m_LastError = "empty ssid";
        DEKI_LOG_ERROR("[wifi] Connect: empty ssid");
        return false;
    }
    if (!InitStackOnce())
    {
        m_LastError = "stack init failed";
        return false;
    }

    wifi_config_t wc = {};
    std::strncpy(reinterpret_cast<char*>(wc.sta.ssid), ssid, sizeof(wc.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(wc.sta.password), password ? password : "", sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = WIFI_AUTH_OPEN;  // accept whatever the AP advertises

    if (esp_wifi_set_config(WIFI_IF_STA, &wc) != ESP_OK)
    {
        m_LastError = "esp_wifi_set_config failed";
        DEKI_LOG_ERROR("[wifi] esp_wifi_set_config failed");
        return false;
    }

    // Returns ESP_OK when WiFi is already running; anything else is a real
    // failure, and waiting for a connection would only run out the timeout.
    const esp_err_t startErr = esp_wifi_start();
    if (startErr != ESP_OK)
    {
        m_LastError = std::string("esp_wifi_start failed: ") + esp_err_to_name(startErr);
        DEKI_LOG_ERROR("[wifi] esp_wifi_start failed: %s", esp_err_to_name(startErr));
        return false;
    }

    xEventGroupClearBits(s_WifiEvents, BIT_CONNECTED | BIT_DISCONNECT);
    EventBits_t bits = xEventGroupWaitBits(s_WifiEvents, BIT_CONNECTED, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeoutMs));

    if (bits & BIT_CONNECTED)
    {
        DEKI_LOG_INFO("[wifi] connected to '%s'", ssid);
        return true;
    }
    m_LastError = "connect timeout";
    DEKI_LOG_ERROR("[wifi] Connect timeout (%lums) ssid='%s'", static_cast<unsigned long>(timeoutMs), ssid);
    return false;
#else
    (void)ssid;
    (void)password;
    (void)timeoutMs;
    m_LastError = "Connect not supported on this platform";
    return false;
#endif
}

void ESPIDFWiFi::Disconnect()
{
#if defined(ESP32)
    if (s_StackInited)
    {
        esp_wifi_disconnect();
    }
    s_Connected = false;
#endif
}

bool ESPIDFWiFi::IsConnected() const
{
#if defined(ESP32)
    return s_Connected;
#else
    return false;
#endif
}

int ESPIDFWiFi::ScanAPs(DekiWifi::DekiAP* out, int maxCount)
{
#if defined(ESP32)
    if (!out || maxCount <= 0)
    {
        return -1;
    }
    if (!InitStackOnce())
    {
        return -1;
    }

    wifi_scan_config_t scan_cfg = {};
    scan_cfg.show_hidden = false;
    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK)
    {
        DEKI_LOG_ERROR("[wifi] esp_wifi_scan_start failed");
        return -1;
    }

    uint16_t apCount = 0;
    esp_wifi_scan_get_ap_num(&apCount);
    if (apCount == 0)
    {
        return 0;
    }

    if (apCount > static_cast<uint16_t>(maxCount))
    {
        apCount = static_cast<uint16_t>(maxCount);
    }

    wifi_ap_record_t records[64];
    if (apCount > 64)
    {
        apCount = 64;
    }
    if (esp_wifi_scan_get_ap_records(&apCount, records) != ESP_OK)
    {
        DEKI_LOG_ERROR("[wifi] esp_wifi_scan_get_ap_records failed");
        return -1;
    }

    int written = 0;
    for (int i = 0; i < apCount && written < maxCount; ++i)
    {
        DekiWifi::DekiAP& a = out[written++];
        std::memcpy(a.bssid, records[i].bssid, 6);
        std::strncpy(a.ssid, reinterpret_cast<const char*>(records[i].ssid), sizeof(a.ssid) - 1);
        a.rssi = records[i].rssi;
        a.channel = records[i].primary;
    }
    return written;
#else
    (void)out;
    (void)maxCount;
    return -1;
#endif
}

}  // namespace DekiEsp32
