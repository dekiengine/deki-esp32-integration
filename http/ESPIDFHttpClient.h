#pragma once

#include "IDekiHttpClient.h"  // from deki-http

/**
 * @brief ESP-IDF implementation of IDekiHttpClient.
 *
 * Registered with DekiHttp::SetCurrent at ESP32 boot (see ESP32BackendInit
 * in ESP32HALPackage.cpp). Backed by esp_http_client + esp_crt_bundle_attach
 * for TLS validation against the Mozilla CA bundle.
 *
 * Blocking; called from low-priority FreeRTOS tasks by the providers that
 * consume DekiHttp::Get / PostJson.
 */
class ESPIDFHttpClient : public IDekiHttpClient
{
public:
    ESPIDFHttpClient()           = default;
    ~ESPIDFHttpClient() override = default;

    std::string FetchUrl(const std::string& url) override;

    Response Get(const std::string& url,
                 const HeaderList&  headers,
                 uint32_t           timeoutMs) override;

    Response PostJson(const std::string& url,
                      const std::string& body,
                      const HeaderList&  headers,
                      uint32_t           timeoutMs) override;
};
