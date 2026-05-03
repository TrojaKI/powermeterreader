#include "stromzaehler.h"
#include "esphome/core/log.h"
#include <cstdio>
#include <cstring>

namespace esphome {
namespace stromzaehler {

static const char *TAG = "stromzaehler";

// OBIS table: code[6] + divisor
// Divisors match Python decoder.py: V÷10, A÷100, PF÷1000
const ObisEntry StromzaehlerComponent::OBIS_TABLE[11] = {
    {{1, 0,  1, 8, 0, 255}, 1},    // energy_consumed_wh
    {{1, 0,  2, 8, 0, 255}, 1},    // energy_fed_wh
    {{1, 0,  1, 7, 0, 255}, 1},    // power_active_w
    {{1, 0,  2, 7, 0, 255}, 1},    // power_reactive_w
    {{1, 0, 32, 7, 0, 255}, 10},   // voltage_l1
    {{1, 0, 52, 7, 0, 255}, 10},   // voltage_l2
    {{1, 0, 72, 7, 0, 255}, 10},   // voltage_l3
    {{1, 0, 31, 7, 0, 255}, 100},  // current_l1
    {{1, 0, 51, 7, 0, 255}, 100},  // current_l2
    {{1, 0, 71, 7, 0, 255}, 100},  // current_l3
    {{1, 0, 13, 7, 0, 255}, 1000}, // power_factor
};

// Convert hex string "aabbcc..." to bytes
void StromzaehlerComponent::set_guek_hex(const char *hex) {
    for (int i = 0; i < GUEK_LEN; i++) {
        uint8_t hi = hex[2*i];
        uint8_t lo = hex[2*i+1];
        hi = (hi >= 'a') ? hi - 'a' + 10 : (hi >= 'A') ? hi - 'A' + 10 : hi - '0';
        lo = (lo >= 'a') ? lo - 'a' + 10 : (lo >= 'A') ? lo - 'A' + 10 : lo - '0';
        guek_[i] = (hi << 4) | lo;
    }
}

void StromzaehlerComponent::setup() {
    ESP_LOGI(TAG, "NÖ Netz Stromzähler component initialized");
    buf_len_ = 0;
    synced_ = false;
    last_frame_counter_ = 0;
}

void StromzaehlerComponent::loop() {
    while (available()) {
        uint8_t byte;
        read_byte(&byte);
        if (!process_byte(byte)) continue;

        if (buf_len_ == 256) {
            // Sagemcom T210D frame1 — buffer and wait for frame2
            memcpy(frame1_buf_, buf_, buf_len_);
            frame1_len_ = buf_len_;
            has_frame1_ = true;
        } else if (buf_len_ == 26 && has_frame1_) {
            // frame2 arrived — decrypt combined
            has_frame1_ = false;
            uint8_t plain[FRAME_BUFFER_SIZE];
            size_t plain_len = 0;
            if (decrypt(frame1_buf_, frame1_len_, buf_, buf_len_, plain, &plain_len)) {
                MeterData data;
                if (parse(plain, plain_len, data)) {
                    publish_data(data);
                }
            }
        } else {
            has_frame1_ = false;
            ESP_LOGW(TAG, "Unexpected frame size %zu — dropping", buf_len_);
        }
        buf_len_ = 0;
        synced_ = false;
    }
}

// State machine: accumulate bytes into buf_ until frame complete
// M-Bus long frame: 68 L L 68 C A [payload...] CS 16
// Total frame length = L + 6 (header: 68 L L 68 C A) + 2 (CS + 16)
bool StromzaehlerComponent::process_byte(uint8_t byte) {
    if (!synced_) {
        if (byte == MBUS_START) {
            buf_[0] = byte;
            buf_len_ = 1;
            synced_ = true;
        }
        return false;
    }

    if (buf_len_ >= FRAME_BUFFER_SIZE) {
        ESP_LOGW(TAG, "Buffer overflow, resetting");
        buf_len_ = 0;
        synced_ = false;
        return false;
    }

    buf_[buf_len_++] = byte;

    // After 4 bytes we know the full expected length: 68 L L 68 → total = L + 6
    if (buf_len_ == 4) {
        if (buf_[1] != buf_[2] || buf_[3] != MBUS_START) {
            ESP_LOGW(TAG, "Invalid M-Bus header");
            buf_len_ = 0;
            synced_ = false;
            return false;
        }
        expected_len_ = buf_[1] + 6;  // L + (68 L L 68 C A) + (CS 16)
    }

    if (buf_len_ >= 4 && buf_len_ == expected_len_) {
        if (buf_[buf_len_ - 1] != MBUS_STOP) {
            ESP_LOGW(TAG, "Missing stop byte 0x16, got 0x%02x", buf_[buf_len_ - 1]);
            buf_len_ = 0;
            synced_ = false;
            return false;
        }
        return true;  // frame complete
    }

    return false;
}

bool StromzaehlerComponent::decrypt(const uint8_t *f1, size_t f1_len,
                                    const uint8_t *f2, size_t f2_len,
                                    uint8_t *plain_out, size_t *plain_len) {
    // Fixed offsets for Sagemcom T210D / NÖ Netz (validated vs arduino_mkr reference)
    static const size_t OFF_CI        =  9;
    static const size_t OFF_SYS_TITLE = 11;
    static const size_t OFF_INV_CTR   = 22;
    static const size_t OFF_CIPHER_F1 = 26;
    static const size_t OFF_CIPHER_F2 =  9;

    if (f1_len < OFF_CIPHER_F1 + 2) {
        ESP_LOGW(TAG, "Frame1 too short: %zu", f1_len);
        return false;
    }
    if (f1[OFF_CI] != CI_GENERAL_CIPHERING) {
        ESP_LOGW(TAG, "Bad CI 0x%02x at offset %zu", f1[OFF_CI], OFF_CI);
        return false;
    }

    uint8_t iv[IV_LEN];
    memcpy(iv,     f1 + OFF_SYS_TITLE, 8);
    memcpy(iv + 8, f1 + OFF_INV_CTR,   4);

    uint32_t fc = ((uint32_t)iv[8] << 24) | ((uint32_t)iv[9] << 16)
                | ((uint32_t)iv[10] << 8) | iv[11];
    if (fc <= last_frame_counter_) {
        ESP_LOGW(TAG, "Replay: fc=%u <= last=%u", fc, last_frame_counter_);
        return false;
    }

    // Combine ciphertext from both frames (strip CS+stop = last 2 bytes each)
    static uint8_t ciph[256];
    size_t len1 = f1_len - OFF_CIPHER_F1 - 2;
    size_t len2 = (f2 && f2_len > OFF_CIPHER_F2 + 2) ? f2_len - OFF_CIPHER_F2 - 2 : 0;
    memcpy(ciph,         f1 + OFF_CIPHER_F1, len1);
    if (len2) memcpy(ciph + len1, f2 + OFF_CIPHER_F2, len2);
    size_t total = len1 + len2;

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, guek_, 128) != 0) {
        ESP_LOGE(TAG, "GCM setkey failed");
        mbedtls_gcm_free(&gcm);
        return false;
    }
    // NÖ Netz / Sagemcom T210D: empty AAD, no auth tag appended
    mbedtls_gcm_starts(&gcm, MBEDTLS_GCM_DECRYPT, iv, IV_LEN, nullptr, 0);
    mbedtls_gcm_update(&gcm, total, ciph, plain_out);
    uint8_t dummy_tag[16];
    mbedtls_gcm_finish(&gcm, dummy_tag, sizeof(dummy_tag));
    mbedtls_gcm_free(&gcm);

    last_frame_counter_ = fc;
    *plain_len = total;
    ESP_LOGI(TAG, "Decrypted OK fc=%u plain=%zu", fc, total);
    return true;
}

// Find OBIS pattern (09 06 [6 bytes]) and return following uint32 or uint16 value
int32_t StromzaehlerComponent::find_obis(const uint8_t *data, size_t len,
                                          const uint8_t *obis_code) {
    // Pattern: TAG_OCTET_STRING(0x09) 0x06 [6 obis bytes]
    const size_t pat_len = 8;
    for (size_t i = 0; i + pat_len < len; i++) {
        if (data[i] != TAG_OCTET_STRING || data[i+1] != 0x06) continue;
        if (memcmp(data + i + 2, obis_code, 6) != 0) continue;

        size_t vpos = i + pat_len;
        if (vpos >= len) return -1;
        if (data[vpos] == TAG_UINT32 && vpos + 5 <= len) {
            return (int32_t)(((uint32_t)data[vpos+1] << 24) |
                             ((uint32_t)data[vpos+2] << 16) |
                             ((uint32_t)data[vpos+3] <<  8) |
                              (uint32_t)data[vpos+4]);
        }
        if (data[vpos] == TAG_UINT16 && vpos + 3 <= len) {
            return (int32_t)(((uint16_t)data[vpos+1] << 8) | data[vpos+2]);
        }
        return -1;
    }
    return -1;
}

bool StromzaehlerComponent::parse(const uint8_t *data, size_t len, MeterData &out) {
    // Indexed into OBIS_TABLE — same order as table definition
    int32_t vals[11];
    for (int i = 0; i < 11; i++) {
        vals[i] = find_obis(data, len, OBIS_TABLE[i].code);
    }

    out.energy_consumed_wh = vals[0] >= 0 ? (uint32_t)vals[0] : 0;
    out.energy_fed_wh      = vals[1] >= 0 ? (uint32_t)vals[1] : 0;
    out.power_active_w     = vals[2] >= 0 ? (uint32_t)vals[2] : 0;
    out.power_reactive_w   = vals[3] >= 0 ? (uint32_t)vals[3] : 0;
    out.voltage_l1 = vals[4] >= 0 ? vals[4] / 10.f  : 0.f;
    out.voltage_l2 = vals[5] >= 0 ? vals[5] / 10.f  : 0.f;
    out.voltage_l3 = vals[6] >= 0 ? vals[6] / 10.f  : 0.f;
    out.current_l1 = vals[7] >= 0 ? vals[7] / 100.f : 0.f;
    out.current_l2 = vals[8] >= 0 ? vals[8] / 100.f : 0.f;
    out.current_l3 = vals[9] >= 0 ? vals[9] / 100.f : 0.f;
    out.power_factor = vals[10] >= 0 ? vals[10] / 1000.f : 0.f;

    // Meter serial: last octet string of printable ASCII digits
    out.meter_serial[0] = '\0';
    for (int i = (int)len - 2; i >= 1; i--) {
        if (data[i-1] != TAG_OCTET_STRING) continue;
        uint8_t slen = data[i];
        if (slen < 4 || (size_t)(i + 1 + slen) > len) continue;
        bool all_digits = true;
        for (int j = 0; j < slen; j++) {
            if (data[i+1+j] < 0x30 || data[i+1+j] > 0x39) { all_digits = false; break; }
        }
        if (all_digits && slen < 32) {
            memcpy(out.meter_serial, data + i + 1, slen);
            out.meter_serial[slen] = '\0';
            break;
        }
    }

    ESP_LOGI(TAG, "P+=%.0fW P-=%.0fW E+=%uWh U1=%.1fV",
             (float)out.power_active_w, (float)out.power_reactive_w,
             out.energy_consumed_wh, out.voltage_l1);
    return true;
}

void StromzaehlerComponent::publish_data(const MeterData &d) {
    if (s_energy_consumed_) s_energy_consumed_->publish_state(d.energy_consumed_wh);
    if (s_energy_fed_)      s_energy_fed_->publish_state(d.energy_fed_wh);
    if (s_power_active_)    s_power_active_->publish_state(d.power_active_w);
    if (s_power_reactive_)  s_power_reactive_->publish_state(d.power_reactive_w);
    if (s_voltage_l1_)      s_voltage_l1_->publish_state(d.voltage_l1);
    if (s_voltage_l2_)      s_voltage_l2_->publish_state(d.voltage_l2);
    if (s_voltage_l3_)      s_voltage_l3_->publish_state(d.voltage_l3);
    if (s_current_l1_)      s_current_l1_->publish_state(d.current_l1);
    if (s_current_l2_)      s_current_l2_->publish_state(d.current_l2);
    if (s_current_l3_)      s_current_l3_->publish_state(d.current_l3);
    if (s_power_factor_)    s_power_factor_->publish_state(d.power_factor);
    if (s_meter_serial_ && d.meter_serial[0])
        s_meter_serial_->publish_state(d.meter_serial);
}

}  // namespace stromzaehler
}  // namespace esphome
