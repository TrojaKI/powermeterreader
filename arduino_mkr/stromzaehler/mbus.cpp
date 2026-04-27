// arduino_mkr/stromzaehler/mbus.cpp
#include "mbus.h"
#include <string.h>

static uint8_t  s_buf[MBUS_FRAME_MAX];
static size_t   s_len      = 0;
static bool     s_synced   = false;
static uint16_t s_expected = 0;  // uint16_t: L+6 can exceed 255

void mbus_reset() {
    s_len = 0; s_synced = false; s_expected = 0;
}

bool mbus_process_byte(uint8_t b) {
    if (!s_synced) {
        if (b != 0x68) return false;
        s_buf[0] = b; s_len = 1; s_synced = true;
        return false;
    }

    if (s_len >= MBUS_FRAME_MAX) { mbus_reset(); return false; }
    s_buf[s_len++] = b;

    if (s_len == 4) {
        if (s_buf[1] != s_buf[2] || s_buf[3] != 0x68) { mbus_reset(); return false; }
        s_expected = (uint16_t)s_buf[1] + 6;
        if (s_expected > MBUS_FRAME_MAX) { mbus_reset(); return false; }
    }

    if (s_expected > 0 && s_len == s_expected) {
        if (s_buf[s_len - 1] != 0x16) { mbus_reset(); return false; }
        return true;
    }
    return false;
}

const uint8_t* mbus_frame()     { return s_buf; }
size_t         mbus_frame_len() { return s_len; }
