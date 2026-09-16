#pragma once

#include "IDekiHttpClient.h"  // from deki-http

namespace DekiEsp32
{

/**
 * @brief ESP-IDF implementation of DekiHttp::IDekiHttpClient.
 *
 * Registered with DekiHttp::SetCurrent at ESP32 boot (see ESP32BackendInit
 * in ESP32HALPackage.cpp). Backed by esp_http_client + esp_crt_bundle_attach
 * for TLS validation against the Mozilla CA bundle.
 *
 * Blocking; called from low-priority FreeRTOS tasks by the providers that
 * consume DekiHttp::Get / PostJson.
 */
class ESPIDFHttpClient : public DekiHttp::IDekiHttpClient
{
public:
    ESPIDFHttpClient()           = default;
    ~ESPIDFHttpClient() override = default;

    std::string FetchUrl(const std::string& url) override;

    DekiHttp::IDekiHttpClient::Response Get(const std::string& url,
                 const DekiHttp::IDekiHttpClient::HeaderList&  headers,
                 uint32_t           timeoutMs) override;

    DekiHttp::IDekiHttpClient::Response PostJson(const std::string& url,
                      const std::string& body,
                      const DekiHttp::IDekiHttpClient::HeaderList&  headers,
                      uint32_t           timeoutMs) override;
};

}  // namespace DekiEsp32
