// arduino_mkr/stromzaehler/mbus.h
#pragma once
#include <stdint.h>
#include <stddef.h>

static const size_t MBUS_FRAME_MAX = 512;

void           mbus_reset();
bool           mbus_process_byte(uint8_t b);  // returns true when frame is complete
const uint8_t* mbus_frame();
size_t         mbus_frame_len();
