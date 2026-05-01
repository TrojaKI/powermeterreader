// arduino_mkr/stromzaehler/dlms.cpp
// Sagemcom T210D / NÖ Netz frame layout (validated vs github.com/FKW9/esp-smartmeter-netznoe):
//   Frame1[256B]: 68 FA FA 68 | 53 FF 00 01 67 | DB 08 sys_title[8] | 81 Fx sec_ctrl inv_ctr[4] | ciphertext[228] | CS 16
//   Frame2[ 26B]: 68 14 14 68 | 53 FF 11 01 67 |                                                   ciphertext[ 15] | CS 16
//   AES-128-GCM, empty AAD, no auth-tag.
#include "dlms.h"
#include <Arduino.h>
#include <string.h>
#include <AES.h>
#include <GCM.h>

static const uint8_t CI_GENERAL_CIPHERING = 0xDB;
static const uint8_t TAG_OCTET_STRING     = 0x09;
static const uint8_t TAG_UINT32           = 0x06;
static const uint8_t TAG_UINT16           = 0x12;

// Fixed offsets for Sagemcom T210D (from reference implementation)
static const size_t OFF_CI         =  9;  // CI=0xDB
static const size_t OFF_SYS_TITLE  = 11;  // 8 bytes
static const size_t OFF_INV_CTR    = 22;  // 4 bytes (frame counter)
static const size_t OFF_CIPHER_F1  = 26;  // ciphertext start in frame1
static const size_t OFF_CIPHER_F2  =  9;  // ciphertext start in frame2

struct ObisEntry { uint8_t code[6]; int divisor; };
static const ObisEntry OBIS_TABLE[11] = {
    {{1,0,1,8,0,255},    1},
    {{1,0,2,8,0,255},    1},
    {{1,0,1,7,0,255},    1},
    {{1,0,2,7,0,255},    1},
    {{1,0,32,7,0,255},  10},
    {{1,0,52,7,0,255},  10},
    {{1,0,72,7,0,255},  10},
    {{1,0,31,7,0,255}, 100},
    {{1,0,51,7,0,255}, 100},
    {{1,0,71,7,0,255}, 100},
    {{1,0,13,7,0,255},1000},
};

static void hex_to_bytes(const char *hex, uint8_t *out, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char hi = hex[i*2], lo = hex[i*2+1];
        uint8_t h = (hi>='a') ? hi-'a'+10 : (hi>='A') ? hi-'A'+10 : hi-'0';
        uint8_t l = (lo>='a') ? lo-'a'+10 : (lo>='A') ? lo-'A'+10 : lo-'0';
        out[i] = (h<<4)|l;
    }
}

static int32_t find_obis(const uint8_t *data, size_t len, const uint8_t *code) {
    for (size_t i = 0; i+8 < len; i++) {
        if (data[i] != TAG_OCTET_STRING || data[i+1] != 0x06) continue;
        if (memcmp(data+i+2, code, 6) != 0) continue;
        size_t vi = i+8;
        if (vi >= len) return -1;
        if (data[vi] == TAG_UINT32 && vi+4 < len)
            return (int32_t)(((uint32_t)data[vi+1]<<24)|((uint32_t)data[vi+2]<<16)
                            |((uint32_t)data[vi+3]<<8)|data[vi+4]);
        if (data[vi] == TAG_UINT16 && vi+2 < len)
            return (int32_t)(((uint16_t)data[vi+1]<<8)|data[vi+2]);
    }
    return -1;
}

bool dlms_decrypt(const uint8_t *f1, size_t f1_len,
                  const uint8_t *f2, size_t f2_len,
                  const char *guek_hex,
                  uint8_t *plain_out, size_t &plain_len) {
    if (f1_len < OFF_CIPHER_F1 + 2) {
        Serial.println("DLMS: frame1 too short");
        return false;
    }
    if (f1[OFF_CI] != CI_GENERAL_CIPHERING) {
        Serial.print("DLMS: bad CI 0x"); Serial.println(f1[OFF_CI], HEX);
        return false;
    }

    // IV = sys_title(8) + inv_ctr(4)
    uint8_t iv[12];
    memcpy(iv,     f1 + OFF_SYS_TITLE, 8);
    memcpy(iv + 8, f1 + OFF_INV_CTR,   4);

    uint32_t fc = ((uint32_t)iv[8]<<24)|((uint32_t)iv[9]<<16)
                 |((uint32_t)iv[10]<<8)|iv[11];
    static uint32_t last_fc = 0;
    if (fc <= last_fc) {
        Serial.print("DLMS: replay fc="); Serial.println(fc);
        return false;
    }

    // Combine ciphertext from both frames (strip CS+stop = 2 trailing bytes)
    static uint8_t ciph[256];
    size_t len1 = f1_len - OFF_CIPHER_F1 - 2;
    size_t len2 = (f2 && f2_len > OFF_CIPHER_F2 + 2)
                    ? f2_len - OFF_CIPHER_F2 - 2 : 0;
    memcpy(ciph, f1 + OFF_CIPHER_F1, len1);
    if (len2) memcpy(ciph + len1, f2 + OFF_CIPHER_F2, len2);
    size_t total = len1 + len2;

    uint8_t guek[16];
    hex_to_bytes(guek_hex, guek, 16);

    GCM<AES128> gcm;
    gcm.setKey(guek, 16);
    gcm.setIV(iv, 12);
    // NÖ Netz / Sagemcom T210D: empty AAD, no auth tag appended
    gcm.decrypt(plain_out, ciph, total);

    last_fc = fc;
    plain_len = total;
    Serial.print("DLMS OK fc="); Serial.print(fc);
    Serial.print(" plain="); Serial.println(plain_len);
    return true;
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
    for (int i = (int)len-2; i >= 0; i--) {
        if (data[i] != TAG_OCTET_STRING) continue;
        size_t slen = data[i+1];
        if (slen < 4 || slen > 31 || (size_t)(i+2+slen) > len) continue;
        bool ok = true;
        for (size_t j = 0; j < slen; j++) {
            if (data[i+2+j] < 0x20 || data[i+2+j] > 0x7E) { ok=false; break; }
        }
        if (ok) {
            memcpy(out.meter_serial, data+i+2, slen);
            out.meter_serial[slen] = '\0';
            break;
        }
    }
    return true;
}
