#include "ESPIDFHttpClient.h"
#include "DekiLogSystem.h"

#if defined(ESP32)
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#endif

namespace {

#if defined(ESP32)
// esp_http_client event handler: appends response body to a std::string passed
// via user_data. Avoids the need to pre-size a response buffer.
esp_err_t HttpEventCb(esp_http_client_event_t* evt)
{
    if (!evt) return ESP_OK;
    auto* out = static_cast<std::string*>(evt->user_data);
    if (evt->event_id == HTTP_EVENT_ON_DATA && out && evt->data && evt->data_len > 0) {
        out->append(static_cast<const char*>(evt->data), static_cast<size_t>(evt->data_len));
    }
    return ESP_OK;
}

IDekiHttpClient::Response Perform(const std::string& url,
                                  esp_http_client_method_t method,
                                  const IDekiHttpClient::HeaderList& headers,
                                  const std::string* jsonBody,
                                  uint32_t timeoutMs)
{
    IDekiHttpClient::Response out;
    std::string body;

    esp_http_client_config_t cfg = {};
    cfg.url               = url.c_str();
    cfg.method            = method;
    cfg.timeout_ms        = static_cast<int>(timeoutMs);
    cfg.event_handler     = &HttpEventCb;
    cfg.user_data         = &body;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.disable_auto_redirect = false;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        DEKI_LOG_ERROR("[http] esp_http_client_init failed (url=%s)", url.c_str());
        return out;
    }

    for (const auto& h : headers) {
        esp_http_client_set_header(client, h.first.c_str(), h.second.c_str());
    }
    if (jsonBody) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, jsonBody->c_str(),
                                       static_cast<int>(jsonBody->size()));
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        out.status = esp_http_client_get_status_code(client);
        out.body   = std::move(body);
    } else {
        DEKI_LOG_ERROR("[http] perform failed: %s (url=%s)", esp_err_to_name(err), url.c_str());
    }

    esp_http_client_cleanup(client);
    return out;
}
#endif  // ESP32

}  // namespace

std::string ESPIDFHttpClient::FetchUrl(const std::string& url)
{
#if defined(ESP32)
    Response r = Perform(url, HTTP_METHOD_GET, {}, nullptr, 15000);
    return (r.status >= 200 && r.status < 300) ? std::move(r.body) : std::string();
#else
    (void)url;
    return {};
#endif
}

IDekiHttpClient::Response ESPIDFHttpClient::Get(const std::string& url,
                                                const HeaderList&  headers,
                                                uint32_t           timeoutMs)
{
#if defined(ESP32)
    return Perform(url, HTTP_METHOD_GET, headers, nullptr, timeoutMs);
#else
    (void)url; (void)headers; (void)timeoutMs;
    return {};
#endif
}

IDekiHttpClient::Response ESPIDFHttpClient::PostJson(const std::string& url,
                                                     const std::string& body,
                                                     const HeaderList&  headers,
                                                     uint32_t           timeoutMs)
{
#if defined(ESP32)
    return Perform(url, HTTP_METHOD_POST, headers, &body, timeoutMs);
#else
    (void)url; (void)body; (void)headers; (void)timeoutMs;
    return {};
#endif
}
