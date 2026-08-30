#include "ESPIDFBLE.h"
#include "DekiLogSystem.h"

#include <cstring>
#include <cstdlib>

#if defined(ESP32)

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "os/os_mbuf.h"
#include "esp_nimble_hci.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include <vector>

namespace {

// =============================================================================
// Package-wide state. NimBLE is a singleton stack, the ESPIDFBLE class is a
// thin facade on top, so we keep state here in a single anonymous namespace.
// =============================================================================

bool                 s_StackInited      = false;
bool                 s_HostReady        = false;
uint8_t              s_OwnAddrType      = 0;

// Scan
DekiBLEScanCb        s_ScanCb           = nullptr;
void*                s_ScanUser         = nullptr;
bool                 s_ScanActive       = false;

// Advertise
bool                 s_Advertising      = false;
uint8_t              s_AdvRawBuf[31];
uint8_t              s_AdvRawLen        = 0;

// GATT server: persisted service table (NimBLE references this after registration).
// Each translated service has a flat char array terminated by a zero entry.
struct GattCharRecord {
    DekiBLEUUID       deki_uuid;
    ble_uuid_any_t    nimble_uuid;
    uint16_t          val_handle;     // populated by NimBLE via ble_gatts_add_svcs callback
    uint8_t           props;
    uint16_t          max_len;
    uint8_t           idx_in_service;
};

struct GattServiceRecord {
    DekiBLEUUID       deki_uuid;
    ble_uuid_any_t    nimble_uuid;
    std::vector<GattCharRecord> chars;
    std::vector<ble_gatt_chr_def>   chr_defs;  // terminated entry appended
};

std::vector<GattServiceRecord>  s_GattServices;
std::vector<ble_gatt_svc_def>   s_GattSvcDefs;   // flat array passed to NimBLE; terminated

// GATT server callbacks
DekiBLECharWriteCb   s_CharWriteCb      = nullptr;
void*                s_CharWriteUser    = nullptr;
DekiBLECharReadCb    s_CharReadCb       = nullptr;
void*                s_CharReadUser     = nullptr;
DekiBLEConnCb        s_ConnCb           = nullptr;
void*                s_ConnUser         = nullptr;

// GATT client async wait state
SemaphoreHandle_t    s_ClientSem        = nullptr;
struct ClientOpState {
    int               status;
    DekiBLEConnHandle conn_handle;
    uint16_t          attr_handle_first;
    uint8_t           attr_handle_count;
    uint8_t*          read_buf;
    size_t            read_buf_max;
    size_t            read_buf_written;
} s_ClientOp;

// GATT notify dispatch
DekiBLENotifyCb      s_NotifyCb         = nullptr;
void*                s_NotifyUser       = nullptr;

// =============================================================================
// Helpers
// =============================================================================

void ToNimbleUuid(const DekiBLEUUID& in, ble_uuid_any_t& out)
{
    if (in.is16bit) {
        out.u.type = BLE_UUID_TYPE_16;
        out.u16.value = in.short_id;
    } else {
        out.u.type = BLE_UUID_TYPE_128;
        // NimBLE expects little-endian; DekiBLEUUID is big-endian canonical.
        for (int i = 0; i < 16; ++i) out.u128.value[i] = in.bytes[15 - i];
    }
}

void FromNimbleUuid(const ble_uuid_t* in, DekiBLEUUID& out)
{
    if (!in) { out = {}; return; }
    if (in->type == BLE_UUID_TYPE_16) {
        const ble_uuid16_t* u = (const ble_uuid16_t*)in;
        out.is16bit  = true;
        out.short_id = u->value;
        // Splice into Bluetooth base UUID: 00000000-0000-1000-8000-00805F9B34FB
        static const uint8_t base[16] = {
            0x00,0x00,0x00,0x00, 0x00,0x00, 0x10,0x00,
            0x80,0x00, 0x00,0x80,0x5F,0x9B,0x34,0xFB
        };
        std::memcpy(out.bytes, base, 16);
        out.bytes[2] = (u->value >> 8) & 0xFF;
        out.bytes[3] = u->value & 0xFF;
    } else if (in->type == BLE_UUID_TYPE_128) {
        const ble_uuid128_t* u = (const ble_uuid128_t*)in;
        out.is16bit = false;
        out.short_id = 0;
        for (int i = 0; i < 16; ++i) out.bytes[i] = u->value[15 - i];
    }
}

DekiBLEAddrType FromNimbleAddrType(uint8_t t)
{
    switch (t) {
        case BLE_ADDR_PUBLIC:        return DekiBLEAddrType::Public;
        case BLE_ADDR_RANDOM:        return DekiBLEAddrType::RandomStatic;
        case BLE_ADDR_PUBLIC_ID:     return DekiBLEAddrType::Public;
        case BLE_ADDR_RANDOM_ID:     return DekiBLEAddrType::RandomPrivateResolvable;
    }
    return DekiBLEAddrType::Public;
}

uint8_t ToNimbleAddrType(DekiBLEAddrType t)
{
    switch (t) {
        case DekiBLEAddrType::Public:                     return BLE_ADDR_PUBLIC;
        case DekiBLEAddrType::RandomStatic:               return BLE_ADDR_RANDOM;
        case DekiBLEAddrType::RandomPrivateResolvable:    return BLE_ADDR_RANDOM_ID;
        case DekiBLEAddrType::RandomPrivateNonResolvable: return BLE_ADDR_RANDOM;
    }
    return BLE_ADDR_PUBLIC;
}

// =============================================================================
// NimBLE host callbacks
// =============================================================================

void OnHostSync()
{
    int rc = ble_hs_id_infer_auto(0, &s_OwnAddrType);
    if (rc != 0) {
        DEKI_LOG_ERROR("[ble] ble_hs_id_infer_auto rc=%d", rc);
        return;
    }
    s_HostReady = true;
    DEKI_LOG_INFO("[ble] host synced, own_addr_type=%u", s_OwnAddrType);
}

void OnHostReset(int reason)
{
    DEKI_LOG_ERROR("[ble] host reset, reason=%d", reason);
    s_HostReady = false;
}

void HostTask(void* /*param*/)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

int GapEvent(struct ble_gap_event* ev, void* /*arg*/);

// Translate a NimBLE scan event into DekiBLEDevice, then dispatch to user cb.
void DispatchScanResult(const struct ble_gap_disc_desc& disc)
{
    if (!s_ScanCb) return;

    DekiBLEDevice dev = {};
    std::memcpy(dev.addr.bytes, disc.addr.val, 6);
    dev.addr.type = FromNimbleAddrType(disc.addr.type);
    dev.rssi = disc.rssi;

    uint8_t copyLen = disc.length_data > 31 ? 31 : disc.length_data;
    std::memcpy(dev.adv_data, disc.data, copyLen);
    dev.adv_len = copyLen;

    // Parse common AD types out of the raw payload.
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, disc.data, disc.length_data) == 0) {
        if (fields.name && fields.name_len) {
            size_t n = fields.name_len;
            if (n > sizeof(dev.name) - 1) n = sizeof(dev.name) - 1;
            std::memcpy(dev.name, fields.name, n);
            dev.name[n] = '\0';
        }
        if (fields.mfg_data && fields.mfg_data_len >= 2) {
            dev.manufacturer_id = (uint16_t)fields.mfg_data[0]
                                | ((uint16_t)fields.mfg_data[1] << 8);
            uint8_t mlen = fields.mfg_data_len - 2;
            if (mlen > sizeof(dev.manufacturer_data)) mlen = sizeof(dev.manufacturer_data);
            std::memcpy(dev.manufacturer_data, fields.mfg_data + 2, mlen);
            dev.manufacturer_data_len = mlen;
        }
        // Parse up to 4 service UUIDs across 16/32/128 lists.
        uint8_t out_i = 0;
        for (uint8_t i = 0; i < fields.num_uuids16 && out_i < 4; ++i) {
            ble_uuid16_t u = { .u = { .type = BLE_UUID_TYPE_16 }, .value = fields.uuids16[i].value };
            FromNimbleUuid((const ble_uuid_t*)&u, dev.service_uuids[out_i++]);
        }
        for (uint8_t i = 0; i < fields.num_uuids128 && out_i < 4; ++i) {
            ble_uuid128_t u;
            u.u.type = BLE_UUID_TYPE_128;
            std::memcpy(u.value, fields.uuids128[i].value, 16);
            FromNimbleUuid((const ble_uuid_t*)&u, dev.service_uuids[out_i++]);
        }
        dev.service_uuid_count = out_i;
    }

    s_ScanCb(dev, s_ScanUser);
}

void DispatchConnEvent(uint16_t conn_handle, const ble_addr_t* peer, bool connected)
{
    if (!s_ConnCb) return;
    DekiBLEAddress addr = {};
    if (peer) {
        std::memcpy(addr.bytes, peer->val, 6);
        addr.type = FromNimbleAddrType(peer->type);
    }
    s_ConnCb(conn_handle, addr, connected, s_ConnUser);
}

int GapEvent(struct ble_gap_event* ev, void* /*arg*/)
{
    switch (ev->type) {
        case BLE_GAP_EVENT_DISC:
            DispatchScanResult(ev->disc);
            return 0;

        case BLE_GAP_EVENT_DISC_COMPLETE:
            s_ScanActive = false;
            return 0;

        case BLE_GAP_EVENT_CONNECT: {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(ev->connect.conn_handle, &desc) == 0) {
                DispatchConnEvent(ev->connect.conn_handle, &desc.peer_id_addr,
                                  ev->connect.status == 0);
            }
            // Wake any blocked Connect()
            s_ClientOp.status = ev->connect.status;
            s_ClientOp.conn_handle = ev->connect.conn_handle;
            if (s_ClientSem) xSemaphoreGive(s_ClientSem);
            return 0;
        }

        case BLE_GAP_EVENT_DISCONNECT:
            DispatchConnEvent(ev->disconnect.conn.conn_handle,
                              &ev->disconnect.conn.peer_id_addr, false);
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            s_Advertising = false;
            return 0;

        case BLE_GAP_EVENT_NOTIFY_RX: {
            if (s_NotifyCb && ev->notify_rx.om) {
                uint8_t buf[256];
                uint16_t out_len = 0;
                int rc = ble_hs_mbuf_to_flat(ev->notify_rx.om, buf, sizeof(buf), &out_len);
                if (rc == 0) {
                    s_NotifyCb(ev->notify_rx.conn_handle, ev->notify_rx.attr_handle,
                               buf, out_len, s_NotifyUser);
                }
            }
            return 0;
        }

        default:
            return 0;
    }
}

// =============================================================================
// GATT server access callback
// =============================================================================

int GattAccess(uint16_t conn_handle, uint16_t attr_handle,
               struct ble_gatt_access_ctxt* ctxt, void* /*arg*/)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (!s_CharWriteCb) return 0;
        uint8_t buf[512];
        uint16_t out_len = 0;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &out_len);
        if (rc != 0) return BLE_ATT_ERR_UNLIKELY;
        s_CharWriteCb(conn_handle, attr_handle, buf, out_len, s_CharWriteUser);
        return 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t buf[512];
        int written = 0;
        if (s_CharReadCb) {
            written = s_CharReadCb(conn_handle, attr_handle, buf, sizeof(buf), s_CharReadUser);
        }
        if (written < 0) return BLE_ATT_ERR_UNLIKELY;
        int rc = os_mbuf_append(ctxt->om, buf, (uint16_t)written);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}

uint8_t PropsToNimble(uint8_t deki)
{
    uint8_t f = 0;
    if (deki & DekiBLECharProp_Read)        f |= BLE_GATT_CHR_F_READ;
    if (deki & DekiBLECharProp_Write)       f |= BLE_GATT_CHR_F_WRITE;
    if (deki & DekiBLECharProp_WriteNoResp) f |= BLE_GATT_CHR_F_WRITE_NO_RSP;
    if (deki & DekiBLECharProp_Notify)      f |= BLE_GATT_CHR_F_NOTIFY;
    if (deki & DekiBLECharProp_Indicate)    f |= BLE_GATT_CHR_F_INDICATE;
    return f;
}

bool InitStackOnce()
{
    if (s_StackInited) return true;

    esp_err_t e = esp_nimble_hci_and_controller_init();
    if (e != ESP_OK) {
        DEKI_LOG_ERROR("[ble] esp_nimble_hci_and_controller_init failed (%d)", e);
        return false;
    }
    nimble_port_init();

    ble_hs_cfg.sync_cb       = OnHostSync;
    ble_hs_cfg.reset_cb      = OnHostReset;
    ble_hs_cfg.store_status_cb = NULL;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    if (!s_ClientSem) {
        s_ClientSem = xSemaphoreCreateBinary();
    }

    nimble_port_freertos_init(HostTask);

    // Wait briefly for the host to sync. NimBLE issues the sync callback once
    // the controller is up; without it we cannot pick an own_addr_type.
    for (int i = 0; i < 50 && !s_HostReady; ++i) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (!s_HostReady) {
        DEKI_LOG_ERROR("[ble] host did not sync within 1s");
        return false;
    }

    s_StackInited = true;
    return true;
}

}  // namespace

// =============================================================================
// IDekiBLE -- lifecycle
// =============================================================================

bool ESPIDFBLE::Initialize()
{
    if (!InitStackOnce()) {
        m_LastError = "nimble init failed";
        m_State = PackageState::Error;
        return false;
    }
    m_State = PackageState::Initialized;
    return true;
}

void ESPIDFBLE::Shutdown()
{
    if (!s_StackInited) {
        m_State = PackageState::Uninitialized;
        return;
    }
    if (s_Advertising) { ble_gap_adv_stop(); s_Advertising = false; }
    if (s_ScanActive)  { ble_gap_disc_cancel(); s_ScanActive = false; }
    int rc = nimble_port_stop();
    if (rc == 0) {
        nimble_port_deinit();
        esp_nimble_hci_and_controller_deinit();
    }
    s_StackInited = false;
    s_HostReady   = false;
    m_State = PackageState::Uninitialized;
}

// =============================================================================
// IDekiBLE -- scan
// =============================================================================

bool ESPIDFBLE::StartScan(uint16_t intervalMs, uint16_t windowMs, bool active, uint32_t durationMs)
{
    if (!InitStackOnce()) return false;
    if (s_ScanActive) ble_gap_disc_cancel();

    struct ble_gap_disc_params dp = {};
    dp.itvl          = (uint16_t)((intervalMs * 1000) / 625);  // 0.625ms units
    dp.window        = (uint16_t)((windowMs   * 1000) / 625);
    dp.passive       = active ? 0 : 1;
    dp.filter_duplicates = 0;
    dp.limited       = 0;

    int32_t dur = durationMs == 0 ? BLE_HS_FOREVER : (int32_t)durationMs;
    int rc = ble_gap_disc(s_OwnAddrType, dur, &dp, GapEvent, this);
    if (rc != 0) {
        m_LastError = "ble_gap_disc failed";
        DEKI_LOG_ERROR("[ble] ble_gap_disc rc=%d", rc);
        return false;
    }
    s_ScanActive = true;
    return true;
}

void ESPIDFBLE::StopScan()
{
    if (s_ScanActive) {
        ble_gap_disc_cancel();
        s_ScanActive = false;
    }
}

void ESPIDFBLE::SetScanCallback(DekiBLEScanCb cb, void* user)
{
    s_ScanCb   = cb;
    s_ScanUser = user;
}

// =============================================================================
// IDekiBLE -- advertise
// =============================================================================

bool ESPIDFBLE::StartAdvertising(const DekiBLEAdvData& data)
{
    if (!InitStackOnce()) return false;
    if (s_Advertising) ble_gap_adv_stop();

    if (data.raw_override && data.raw_override_len > 0 && data.raw_override_len <= 31) {
        std::memcpy(s_AdvRawBuf, data.raw_override, data.raw_override_len);
        s_AdvRawLen = data.raw_override_len;
        int rc = ble_gap_adv_set_data(s_AdvRawBuf, s_AdvRawLen);
        if (rc != 0) {
            m_LastError = "ble_gap_adv_set_data raw failed";
            return false;
        }
    } else {
        struct ble_hs_adv_fields fields = {};
        fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
        if (data.local_name) {
            fields.name        = (uint8_t*)data.local_name;
            fields.name_len    = (uint8_t)std::strlen(data.local_name);
            fields.name_is_complete = 1;
        }
        // Marshal manufacturer data with leading manufacturer_id (LE).
        uint8_t mfg_buf[29];
        if (data.manufacturer_id != 0xFFFF && data.manufacturer_data_len > 0
            && data.manufacturer_data_len <= 27)
        {
            mfg_buf[0] = data.manufacturer_id & 0xFF;
            mfg_buf[1] = (data.manufacturer_id >> 8) & 0xFF;
            std::memcpy(mfg_buf + 2, data.manufacturer_data, data.manufacturer_data_len);
            fields.mfg_data     = mfg_buf;
            fields.mfg_data_len = data.manufacturer_data_len + 2;
        }
        // Service UUIDs: only 128-bit emitted here (16-bit can be added if asked).
        std::vector<ble_uuid128_t> u128s;
        for (uint8_t i = 0; i < data.service_uuid_count && i < 4; ++i) {
            ble_uuid_any_t any;
            ToNimbleUuid(data.service_uuids[i], any);
            if (any.u.type == BLE_UUID_TYPE_128) u128s.push_back(any.u128);
        }
        if (!u128s.empty()) {
            fields.uuids128         = u128s.data();
            fields.num_uuids128     = (uint8_t)u128s.size();
            fields.uuids128_is_complete = 1;
        }

        int rc = ble_gap_adv_set_fields(&fields);
        if (rc != 0) {
            m_LastError = "ble_gap_adv_set_fields failed";
            DEKI_LOG_ERROR("[ble] adv_set_fields rc=%d", rc);
            return false;
        }
    }

    struct ble_gap_adv_params ap = {};
    ap.conn_mode = data.connectable ? BLE_GAP_CONN_MODE_UND : BLE_GAP_CONN_MODE_NON;
    ap.disc_mode = BLE_GAP_DISC_MODE_GEN;
    uint16_t itvl = (uint16_t)((data.interval_ms * 1000) / 625);
    ap.itvl_min = itvl;
    ap.itvl_max = itvl;

    int rc = ble_gap_adv_start(s_OwnAddrType, NULL, BLE_HS_FOREVER, &ap, GapEvent, this);
    if (rc != 0) {
        m_LastError = "ble_gap_adv_start failed";
        DEKI_LOG_ERROR("[ble] adv_start rc=%d", rc);
        return false;
    }
    s_Advertising = true;
    return true;
}

void ESPIDFBLE::StopAdvertising()
{
    if (s_Advertising) { ble_gap_adv_stop(); s_Advertising = false; }
}

bool ESPIDFBLE::IsAdvertising() const
{
    return s_Advertising;
}

// =============================================================================
// IDekiBLE -- GATT server
// =============================================================================

bool ESPIDFBLE::BuildGattServer(DekiBLEServiceSpec* services, uint8_t count)
{
    if (!InitStackOnce()) return false;
    if (!services || count == 0) return false;

    s_GattServices.clear();
    s_GattServices.resize(count);
    s_GattSvcDefs.clear();
    s_GattSvcDefs.resize(count + 1);

    for (uint8_t s = 0; s < count; ++s) {
        GattServiceRecord& rec = s_GattServices[s];
        rec.deki_uuid = services[s].uuid;
        ToNimbleUuid(rec.deki_uuid, rec.nimble_uuid);

        rec.chars.resize(services[s].char_count);
        rec.chr_defs.resize(services[s].char_count + 1);

        for (uint8_t c = 0; c < services[s].char_count; ++c) {
            GattCharRecord& cr = rec.chars[c];
            cr.deki_uuid     = services[s].chars[c].uuid;
            cr.props         = services[s].chars[c].props;
            cr.max_len       = services[s].chars[c].max_len;
            cr.idx_in_service = c;
            ToNimbleUuid(cr.deki_uuid, cr.nimble_uuid);

            ble_gatt_chr_def& d = rec.chr_defs[c];
            std::memset(&d, 0, sizeof(d));
            d.uuid        = (const ble_uuid_t*)&cr.nimble_uuid;
            d.access_cb   = GattAccess;
            d.arg         = nullptr;
            d.flags       = PropsToNimble(cr.props);
            d.val_handle  = &cr.val_handle;
        }
        // Terminator
        std::memset(&rec.chr_defs[services[s].char_count], 0, sizeof(ble_gatt_chr_def));

        ble_gatt_svc_def& sd = s_GattSvcDefs[s];
        std::memset(&sd, 0, sizeof(sd));
        sd.type            = BLE_GATT_SVC_TYPE_PRIMARY;
        sd.uuid            = (const ble_uuid_t*)&rec.nimble_uuid;
        sd.characteristics = rec.chr_defs.data();
    }
    // Terminator
    std::memset(&s_GattSvcDefs[count], 0, sizeof(ble_gatt_svc_def));

    int rc = ble_gatts_count_cfg(s_GattSvcDefs.data());
    if (rc != 0) { m_LastError = "ble_gatts_count_cfg failed"; return false; }
    rc = ble_gatts_add_svcs(s_GattSvcDefs.data());
    if (rc != 0) { m_LastError = "ble_gatts_add_svcs failed"; return false; }
    rc = ble_gatts_start();
    if (rc != 0) { m_LastError = "ble_gatts_start failed"; return false; }

    // Copy resolved value handles back into caller's specs.
    for (uint8_t s = 0; s < count; ++s) {
        for (uint8_t c = 0; c < services[s].char_count; ++c) {
            services[s].chars[c].value_handle = s_GattServices[s].chars[c].val_handle;
        }
    }
    return true;
}

bool ESPIDFBLE::NotifyValue(DekiBLEConnHandle conn, DekiBLECharHandle handle,
                            const void* data, size_t len)
{
    if (!s_StackInited) return false;
    struct os_mbuf* om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
    if (!om) return false;
    int rc = ble_gatts_notify_custom(conn, handle, om);
    return rc == 0;
}

void ESPIDFBLE::SetCharWriteCallback(DekiBLECharWriteCb cb, void* user)
{
    s_CharWriteCb   = cb;
    s_CharWriteUser = user;
}

void ESPIDFBLE::SetCharReadCallback(DekiBLECharReadCb cb, void* user)
{
    s_CharReadCb   = cb;
    s_CharReadUser = user;
}

void ESPIDFBLE::SetConnectionCallback(DekiBLEConnCb cb, void* user)
{
    s_ConnCb   = cb;
    s_ConnUser = user;
}

// =============================================================================
// IDekiBLE -- GATT client
// =============================================================================

bool ESPIDFBLE::Connect(const DekiBLEAddress& addr, uint32_t timeoutMs)
{
    if (!InitStackOnce()) return false;

    ble_addr_t peer;
    peer.type = ToNimbleAddrType(addr.type);
    std::memcpy(peer.val, addr.bytes, 6);

    xSemaphoreTake(s_ClientSem, 0);  // drain
    s_ClientOp = {};

    int rc = ble_gap_connect(s_OwnAddrType, &peer, timeoutMs, NULL, GapEvent, this);
    if (rc != 0) {
        m_LastError = "ble_gap_connect failed";
        DEKI_LOG_ERROR("[ble] connect rc=%d", rc);
        return false;
    }
    if (xSemaphoreTake(s_ClientSem, pdMS_TO_TICKS(timeoutMs + 500)) != pdTRUE) {
        m_LastError = "connect timed out waiting for event";
        return false;
    }
    return s_ClientOp.status == 0;
}

void ESPIDFBLE::DisconnectClient(DekiBLEConnHandle conn)
{
    if (!s_StackInited) return;
    ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
}

namespace {

int DiscChrCb(uint16_t conn_handle, const struct ble_gatt_error* error,
              const struct ble_gatt_chr* chr, void* /*arg*/)
{
    (void)conn_handle;
    if (error && error->status == BLE_HS_EDONE) {
        if (s_ClientSem) xSemaphoreGive(s_ClientSem);
        return 0;
    }
    if (chr) {
        if (s_ClientOp.attr_handle_count == 0)
            s_ClientOp.attr_handle_first = chr->val_handle;
        if (s_ClientOp.attr_handle_count < 0xFF) s_ClientOp.attr_handle_count++;
    }
    return 0;
}

int ReadCb(uint16_t conn_handle, const struct ble_gatt_error* error,
           struct ble_gatt_attr* attr, void* /*arg*/)
{
    (void)conn_handle;
    s_ClientOp.status = error ? error->status : 0;
    if (error == nullptr || error->status == 0) {
        uint16_t out_len = 0;
        ble_hs_mbuf_to_flat(attr->om, s_ClientOp.read_buf,
                            (uint16_t)s_ClientOp.read_buf_max, &out_len);
        s_ClientOp.read_buf_written = out_len;
    }
    if (s_ClientSem) xSemaphoreGive(s_ClientSem);
    return 0;
}

int WriteCb(uint16_t conn_handle, const struct ble_gatt_error* error,
            struct ble_gatt_attr* /*attr*/, void* /*arg*/)
{
    (void)conn_handle;
    s_ClientOp.status = error ? error->status : 0;
    if (s_ClientSem) xSemaphoreGive(s_ClientSem);
    return 0;
}

}  // namespace

bool ESPIDFBLE::DiscoverService(DekiBLEConnHandle conn, const DekiBLEUUID& service,
                                DekiBLECharHandle* outFirstHandle, uint8_t* outCount)
{
    if (!s_StackInited) return false;
    ble_uuid_any_t any;
    ToNimbleUuid(service, any);

    xSemaphoreTake(s_ClientSem, 0);
    s_ClientOp = {};

    int rc = ble_gattc_disc_all_chrs(conn, 0x0001, 0xFFFF, DiscChrCb, nullptr);
    // Note: filtering by service UUID requires first discovering the service handle
    // range. For simplicity we discover all chrs and trust the caller to match by
    // UUID downstream; revisit if filtering becomes necessary.
    (void)any;
    if (rc != 0) {
        m_LastError = "ble_gattc_disc_all_chrs failed";
        return false;
    }
    if (xSemaphoreTake(s_ClientSem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        m_LastError = "discover timed out";
        return false;
    }
    if (outFirstHandle) *outFirstHandle = s_ClientOp.attr_handle_first;
    if (outCount)       *outCount       = s_ClientOp.attr_handle_count;
    return s_ClientOp.attr_handle_count > 0;
}

bool ESPIDFBLE::ReadRemote(DekiBLEConnHandle conn, DekiBLECharHandle handle,
                           uint8_t* out, size_t* len)
{
    if (!s_StackInited || !out || !len) return false;
    xSemaphoreTake(s_ClientSem, 0);
    s_ClientOp = {};
    s_ClientOp.read_buf     = out;
    s_ClientOp.read_buf_max = *len;

    int rc = ble_gattc_read(conn, handle, ReadCb, nullptr);
    if (rc != 0) { m_LastError = "ble_gattc_read failed"; return false; }
    if (xSemaphoreTake(s_ClientSem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        m_LastError = "read timed out";
        return false;
    }
    if (s_ClientOp.status != 0) return false;
    *len = s_ClientOp.read_buf_written;
    return true;
}

bool ESPIDFBLE::WriteRemote(DekiBLEConnHandle conn, DekiBLECharHandle handle,
                            const void* data, size_t len, bool with_response)
{
    if (!s_StackInited) return false;
    if (!with_response) {
        int rc = ble_gattc_write_no_rsp_flat(conn, handle, data, (uint16_t)len);
        return rc == 0;
    }
    xSemaphoreTake(s_ClientSem, 0);
    s_ClientOp = {};
    struct os_mbuf* om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
    if (!om) return false;
    int rc = ble_gattc_write(conn, handle, om, WriteCb, nullptr);
    if (rc != 0) { m_LastError = "ble_gattc_write failed"; return false; }
    if (xSemaphoreTake(s_ClientSem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        m_LastError = "write timed out";
        return false;
    }
    return s_ClientOp.status == 0;
}

bool ESPIDFBLE::Subscribe(DekiBLEConnHandle conn, DekiBLECharHandle handle, bool enable)
{
    if (!s_StackInited) return false;
    // CCCD is the descriptor immediately after the characteristic value handle.
    uint16_t cccd_handle = handle + 1;
    uint8_t  val[2] = { (uint8_t)(enable ? 0x01 : 0x00), 0x00 };
    int rc = ble_gattc_write_no_rsp_flat(conn, cccd_handle, val, 2);
    return rc == 0;
}

void ESPIDFBLE::SetNotifyCallback(DekiBLENotifyCb cb, void* user)
{
    s_NotifyCb   = cb;
    s_NotifyUser = user;
}

#else  // !ESP32 -- desktop / editor stubs

bool ESPIDFBLE::Initialize() { m_State = PackageState::Initialized; return true; }
void ESPIDFBLE::Shutdown()   { m_State = PackageState::Uninitialized; }

bool ESPIDFBLE::StartScan(uint16_t, uint16_t, bool, uint32_t)
{
    m_LastError = "BLE scan not supported on this platform";
    return false;
}
void ESPIDFBLE::StopScan() {}
void ESPIDFBLE::SetScanCallback(DekiBLEScanCb, void*) {}

bool ESPIDFBLE::StartAdvertising(const DekiBLEAdvData&)
{
    m_LastError = "BLE advertising not supported on this platform";
    return false;
}
void ESPIDFBLE::StopAdvertising() {}
bool ESPIDFBLE::IsAdvertising() const { return false; }

bool ESPIDFBLE::BuildGattServer(DekiBLEServiceSpec*, uint8_t)
{
    m_LastError = "BLE GATT server not supported on this platform";
    return false;
}
bool ESPIDFBLE::NotifyValue(DekiBLEConnHandle, DekiBLECharHandle, const void*, size_t) { return false; }
void ESPIDFBLE::SetCharWriteCallback(DekiBLECharWriteCb, void*) {}
void ESPIDFBLE::SetCharReadCallback (DekiBLECharReadCb,  void*) {}
void ESPIDFBLE::SetConnectionCallback(DekiBLEConnCb, void*) {}

bool ESPIDFBLE::Connect(const DekiBLEAddress&, uint32_t)
{
    m_LastError = "BLE Connect not supported on this platform";
    return false;
}
void ESPIDFBLE::DisconnectClient(DekiBLEConnHandle) {}
bool ESPIDFBLE::DiscoverService(DekiBLEConnHandle, const DekiBLEUUID&, DekiBLECharHandle*, uint8_t*) { return false; }
bool ESPIDFBLE::ReadRemote(DekiBLEConnHandle, DekiBLECharHandle, uint8_t*, size_t*) { return false; }
bool ESPIDFBLE::WriteRemote(DekiBLEConnHandle, DekiBLECharHandle, const void*, size_t, bool) { return false; }
bool ESPIDFBLE::Subscribe(DekiBLEConnHandle, DekiBLECharHandle, bool) { return false; }
void ESPIDFBLE::SetNotifyCallback(DekiBLENotifyCb, void*) {}

#endif  // ESP32
