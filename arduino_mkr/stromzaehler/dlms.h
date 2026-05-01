// arduino_mkr/stromzaehler/dlms.h
#pragma once
#include <stdint.h>
#include <stddef.h>

struct MeterData {
    uint32_t energy_consumed_wh;
    uint32_t energy_fed_wh;
    uint32_t power_active_w;
    uint32_t power_reactive_w;
    float    voltage_l1;
    float    voltage_l2;
    float    voltage_l3;
    float    current_l1;
    float    current_l2;
    float    current_l3;
    float    power_factor;
    char     meter_serial[32];
};

// Sagemcom T210D / NÖ Netz: DLMS PDU spans two M-Bus frames.
// frame1: 256B main frame, frame2: 26B continuation (NULL = single-frame mode)
// plain_out must be at least 256 bytes
bool dlms_decrypt(const uint8_t *frame1, size_t frame1_len,
                  const uint8_t *frame2, size_t frame2_len,
                  const char *guek_hex,
                  uint8_t *plain_out, size_t &plain_len);

bool dlms_parse(const uint8_t *data, size_t len, MeterData &out);
