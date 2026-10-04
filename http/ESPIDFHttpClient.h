#pragma once

#include "IDekiHttpClient.h"  // from deki-http

namespace DekiEsp32
{

/// DekiHttp::IDekiHttpClient on ESP-IDF's esp_http_client, with
/// esp_crt_bundle_attach checking TLS against the Mozilla CA bundle.
///
/// Registered with DekiHttp::SetCurrent at ESP32 boot (see ESP32BackendInit
/// in ESP32HALPackage.cpp). Blocking; the providers that use DekiHttp::Get /
/// PostJson call it from low-priority FreeRTOS tasks.
class ESPIDFHttpClient : public DekiHttp::IDekiHttpClient
{
public:
    ESPIDFHttpClient() = default;
    ~ESPIDFHttpClient() override = default;

    std::string FetchUrl(const std::string& url) override;

    DekiHttp::IDekiHttpClient::Response
    Get(const std::string& url, const DekiHttp::IDekiHttpClient::HeaderList& headers, uint32_t timeoutMs) override;

    DekiHttp::IDekiHttpClient::Response PostJson(const std::string& url, const std::string& body,
                                                 const DekiHttp::IDekiHttpClient::HeaderList& headers,
                                                 uint32_t timeoutMs) override;
};

}  // namespace DekiEsp32
