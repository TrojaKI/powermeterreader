// arduino_mkr/stromzaehler/dlms.cpp
#include "dlms.h"
#include <string.h>
#include <AES.h>
#include <GCM.h>

static const uint8_t CI_GENERAL_CIPHERING = 0xDB;
static const uint8_t TAG_OCTET_STRING     = 0x09;
static const uint8_t TAG_UINT32           = 0x06;
static const uint8_t TAG_UINT16           = 0x12;

struct ObisEntry { uint8_t code[6]; int divisor; };
static const ObisEntry OBIS_TABLE[11] = {
    {{1,0,1,8,0,255},    1},  // energy_consumed_wh
    {{1,0,2,8,0,255},    1},  // energy_fed_wh
    {{1,0,1,7,0,255},    1},  // power_active_w
    {{1,0,2,7,0,255},    1},  // power_reactive_w
    {{1,0,32,7,0,255},  10},  // voltage_l1 (÷10)
    {{1,0,52,7,0,255},  10},  // voltage_l2
    {{1,0,72,7,0,255},  10},  // voltage_l3
    {{1,0,31,7,0,255}, 100},  // current_l1 (÷100)
    {{1,0,51,7,0,255}, 100},  // current_l2
    {{1,0,71,7,0,255}, 100},  // current_l3
    {{1,0,13,7,0,255},1000},  // power_factor (÷1000)
};

static void hex_to_bytes(const char *hex, uint8_t *out, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char hi = hex[i * 2], lo = hex[i * 2 + 1];
        uint8_t h = (hi >= 'a') ? hi-'a'+10 : (hi >= 'A') ? hi-'A'+10 : hi-'0';
        uint8_t l = (lo >= 'a') ? lo-'a'+10 : (lo >= 'A') ? lo-'A'+10 : lo-'0';
        out[i] = (h << 4) | l;
    }
}

static int32_t find_obis(const uint8_t *data, size_t len, const uint8_t *code) {
    for (size_t i = 0; i + 8 < len; i++) {
        if (data[i] != TAG_OCTET_STRING || data[i+1] != 0x06) continue;
        if (memcmp(data + i + 2, code, 6) != 0) continue;
        size_t vi = i + 8;
        if (vi >= len) return -1;
        if (data[vi] == TAG_UINT32 && vi + 4 < len)
            return (int32_t)(((uint32_t)data[vi+1]<<24)|((uint32_t)data[vi+2]<<16)
                            |((uint32_t)data[vi+3]<<8)|data[vi+4]);
        if (data[vi] == TAG_UINT16 && vi + 2 < len)
            return (int32_t)(((uint16_t)data[vi+1]<<8)|data[vi+2]);
    }
    return -1;
}

bool dlms_decrypt(const uint8_t *frame, size_t /*frame_len*/,
                  const char *guek_hex,
                  uint8_t *plain_out, size_t &plain_len) {
    uint8_t guek[16];
    hex_to_bytes(guek_hex, guek, 16);

    const uint8_t *p = frame + 4;  // skip M-Bus header: start, L, L, start
    if (*p++ != CI_GENERAL_CIPHERING) return false;

    const uint8_t *sys_title = p; p += 8;

    // BER-encoded length field
    size_t cipher_total;
    if      (*p == 0x82) { p++; cipher_total = ((size_t)p[0]<<8)|p[1]; p += 2; }
    else if (*p == 0x81) { p++; cipher_total = *p++; }
    else                 { cipher_total = *p++; }

    uint8_t  sec_ctrl  = *p++;
    uint32_t frame_ctr = ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)
                        |((uint32_t)p[2]<<8)|p[3];
    p += 4;

    static uint32_t last_fc = 0;
    if (frame_ctr <= last_fc) return false;  // replay protection

    uint8_t iv[12];
    memcpy(iv, sys_title, 8);
    iv[8]=(frame_ctr>>24)&0xFF; iv[9]=(frame_ctr>>16)&0xFF;
    iv[10]=(frame_ctr>>8)&0xFF; iv[11]=frame_ctr&0xFF;

    uint8_t aad[9];
    aad[0] = sec_ctrl;
    memcpy(aad + 1, sys_title, 8);

    // cipher_total = sec_ctrl(1) + frame_ctr(4) + ciphertext + tag(16)
    size_t        data_len = cipher_total - 5 - 16;
    const uint8_t *ciph    = p;
    const uint8_t *tag_ptr = p + data_len;

    // Try with standard AAD (sec_ctrl + sys_title)
    {
        GCM<AES128> gcm;
        gcm.setKey(guek, 16);
        gcm.setIV(iv, 12);
        gcm.addAuthData(aad, 9);
        gcm.decrypt(plain_out, ciph, data_len);
        if (gcm.checkTag(tag_ptr, 16)) {
            last_fc = frame_ctr; plain_len = data_len; return true;
        }
    }
    // Fallback: empty AAD (some meter variants)
    {
        GCM<AES128> gcm;
        gcm.setKey(guek, 16);
        gcm.setIV(iv, 12);
        gcm.decrypt(plain_out, ciph, data_len);
        if (gcm.checkTag(tag_ptr, 16)) {
            last_fc = frame_ctr; plain_len = data_len; return true;
        }
    }
    return false;
}

bool dlms_parse(const uint8_t *data, size_t len, MeterData &out) {
    memset(&out, 0, sizeof(out));

    for (int i = 0; i < 11; i++) {
        int32_t raw = find_obis(data, len, OBIS_TABLE[i].code);
        if (raw < 0) continue;

        switch (i) {
            case 0: out.energy_consumed_wh = (uint32_t)raw; break;
            case 1: out.energy_fed_wh      = (uint32_t)raw; break;
            case 2: out.power_active_w     = (uint32_t)raw; break;
            case 3: out.power_reactive_w   = (uint32_t)raw; break;
            case 4: out.voltage_l1   = (float)raw / 10.f;   break;
            case 5: out.voltage_l2   = (float)raw / 10.f;   break;
            case 6: out.voltage_l3   = (float)raw / 10.f;   break;
            case 7: out.current_l1   = (float)raw / 100.f;  break;
            case 8: out.current_l2   = (float)raw / 100.f;  break;
            case 9: out.current_l3   = (float)raw / 100.f;  break;
            case 10: out.power_factor = (float)raw / 1000.f; break;
        }
    }

    // meter serial: last octet string of printable ASCII chars
    for (int i = (int)len - 2; i >= 0; i--) {
        if (data[i] != TAG_OCTET_STRING) continue;
        size_t slen = data[i + 1];
        if (slen < 4 || slen > 31 || (size_t)(i + 2 + slen) > len) continue;
        bool ok = true;
        for (size_t j = 0; j < slen; j++) {
            if (data[i+2+j] < 0x20 || data[i+2+j] > 0x7E) { ok = false; break; }
        }
        if (ok) {
            memcpy(out.meter_serial, data + i + 2, slen);
            out.meter_serial[slen] = '\0';
            break;
        }
    }
    return true;
}
