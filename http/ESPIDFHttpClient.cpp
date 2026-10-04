#include "ESPIDFHttpClient.h"
#include <deki/LogSystem.h>
#include <algorithm>

#if defined(ESP32)
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#endif

namespace DekiEsp32
{

namespace
{

#if defined(ESP32)
// Largest response body kept. With no limit a large response grew the string
// until an allocation failed, which aborts on the device (no exceptions).
constexpr size_t kMaxBodyBytes = 512 * 1024;

struct BodySink
{
    std::string text;
    bool tooLarge = false;
};

// esp_http_client event handler: appends response body to the BodySink passed
// via user_data. Avoids the need to pre-size a response buffer.
esp_err_t HttpEventCb(esp_http_client_event_t* evt)
{
    if (!evt)
    {
        return ESP_OK;
    }
    auto* out = static_cast<BodySink*>(evt->user_data);
    if (evt->event_id != HTTP_EVENT_ON_DATA || !out || !evt->data || evt->data_len <= 0 || out->tooLarge)
    {
        return ESP_OK;
    }

    const size_t len = static_cast<size_t>(evt->data_len);
    const size_t needed = out->text.size() + len;
    if (needed > kMaxBodyBytes)
    {
        out->tooLarge = true;
    }
    else if (needed > out->text.capacity())
    {
        // Growing copies into a block up to twice the size; refuse rather
        // than let that allocation fail.
        const size_t grown = std::max(needed, out->text.capacity() * 2) + 1;
        // The string grows through malloc, so this asks malloc's heap for its
        // largest free block.
        const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);  // deki-alloc-ok: a query
        if (largest < grown + 16 * 1024)
        {
            out->tooLarge = true;
        }
    }
    if (out->tooLarge)
    {
        out->text.clear();
        out->text.shrink_to_fit();
        return ESP_OK;
    }
    out->text.append(static_cast<const char*>(evt->data), len);
    return ESP_OK;
}

DekiHttp::IDekiHttpClient::Response Perform(const std::string& url, esp_http_client_method_t method,
                                            const DekiHttp::IDekiHttpClient::HeaderList& headers,
                                            const std::string* jsonBody, uint32_t timeoutMs)
{
    DekiHttp::IDekiHttpClient::Response out;
    BodySink body;

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.method = method;
    cfg.timeout_ms = static_cast<int>(timeoutMs);
    cfg.event_handler = &HttpEventCb;
    cfg.user_data = &body;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.disable_auto_redirect = false;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
    {
        DEKI_LOG_ERROR("[http] esp_http_client_init failed (url=%s)", url.c_str());
        return out;
    }

    for (const auto& h : headers)
    {
        esp_http_client_set_header(client, h.first.c_str(), h.second.c_str());
    }
    if (jsonBody)
    {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, jsonBody->c_str(), static_cast<int>(jsonBody->size()));
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK && body.tooLarge)
    {
        // A transport error, as far as the caller can tell: the body is gone.
        DEKI_LOG_ERROR("[http] response too large for this board's memory (limit %u KB), dropped (url=%s)",
                       static_cast<unsigned>(kMaxBodyBytes / 1024), url.c_str());
    }
    else if (err == ESP_OK)
    {
        out.status = esp_http_client_get_status_code(client);
        out.body = std::move(body.text);
    }
    else
    {
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
    DekiHttp::IDekiHttpClient::Response r = Perform(url, HTTP_METHOD_GET, {}, nullptr, 15000);
    return (r.status >= 200 && r.status < 300) ? std::move(r.body) : std::string();
#else
    (void)url;
    return {};
#endif
}

DekiHttp::IDekiHttpClient::Response
ESPIDFHttpClient::Get(const std::string& url, const DekiHttp::IDekiHttpClient::HeaderList& headers, uint32_t timeoutMs)
{
#if defined(ESP32)
    return Perform(url, HTTP_METHOD_GET, headers, nullptr, timeoutMs);
#else
    (void)url;
    (void)headers;
    (void)timeoutMs;
    return {};
#endif
}

DekiHttp::IDekiHttpClient::Response ESPIDFHttpClient::PostJson(const std::string& url, const std::string& body,
                                                               const DekiHttp::IDekiHttpClient::HeaderList& headers,
                                                               uint32_t timeoutMs)
{
#if defined(ESP32)
    return Perform(url, HTTP_METHOD_POST, headers, &body, timeoutMs);
#else
    (void)url;
    (void)body;
    (void)headers;
    (void)timeoutMs;
    return {};
#endif
}

}  // namespace DekiEsp32
