#pragma once
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <mbedtls/gcm.h>
#include <cstdint>
#include <cstring>

namespace esphome {
namespace stromzaehler {

static const size_t FRAME_BUFFER_SIZE = 512;
static const size_t GUEK_LEN = 16;
static const size_t GCM_TAG_LEN = 16;
static const size_t IV_LEN = 12;       // 8B sys_title + 4B frame_counter
static const size_t AAD_LEN = 9;       // 1B security_control + 8B sys_title
static const uint8_t MBUS_START = 0x68;
static const uint8_t MBUS_STOP = 0x16;
static const uint8_t CI_GENERAL_CIPHERING = 0xDB;
static const uint8_t TAG_OCTET_STRING = 0x09;
static const uint8_t TAG_UINT32 = 0x06;
static const uint8_t TAG_UINT16 = 0x12;

struct MeterData {
    uint32_t energy_consumed_wh{0};
    uint32_t energy_fed_wh{0};
    uint32_t power_active_w{0};
    uint32_t power_reactive_w{0};
    float voltage_l1{0.f};
    float voltage_l2{0.f};
    float voltage_l3{0.f};
    float current_l1{0.f};
    float current_l2{0.f};
    float current_l3{0.f};
    float power_factor{0.f};
    char meter_serial[32]{};
};

// OBIS entry: 6-byte code + divisor for scaling
struct ObisEntry {
    uint8_t code[6];
    int divisor;
};

class StromzaehlerComponent : public Component, public uart::UARTDevice {
 public:
    StromzaehlerComponent(uart::UARTComponent *uart_parent) : UARTDevice(uart_parent) {}

    void set_guek_hex(const char *hex);
    void setup() override;
    void loop() override;
    float get_setup_priority() const override { return setup_priority::DATA; }

    void set_energy_consumed_sensor(sensor::Sensor *s)   { s_energy_consumed_ = s; }
    void set_energy_fed_sensor(sensor::Sensor *s)        { s_energy_fed_ = s; }
    void set_power_active_sensor(sensor::Sensor *s)      { s_power_active_ = s; }
    void set_power_reactive_sensor(sensor::Sensor *s)    { s_power_reactive_ = s; }
    void set_voltage_l1_sensor(sensor::Sensor *s)        { s_voltage_l1_ = s; }
    void set_voltage_l2_sensor(sensor::Sensor *s)        { s_voltage_l2_ = s; }
    void set_voltage_l3_sensor(sensor::Sensor *s)        { s_voltage_l3_ = s; }
    void set_current_l1_sensor(sensor::Sensor *s)        { s_current_l1_ = s; }
    void set_current_l2_sensor(sensor::Sensor *s)        { s_current_l2_ = s; }
    void set_current_l3_sensor(sensor::Sensor *s)        { s_current_l3_ = s; }
    void set_power_factor_sensor(sensor::Sensor *s)      { s_power_factor_ = s; }
    void set_meter_serial_sensor(text_sensor::TextSensor *s) { s_meter_serial_ = s; }

 protected:
    bool process_byte(uint8_t byte);           // returns true when full frame ready
    bool decrypt(const uint8_t *frame, size_t frame_len,
                 uint8_t *plain_out, size_t *plain_len);
    bool parse(const uint8_t *data, size_t len, MeterData &out);
    int32_t find_obis(const uint8_t *data, size_t len, const uint8_t *obis_code);
    void publish_data(const MeterData &d);

    uint8_t guek_[GUEK_LEN]{};
    uint8_t buf_[FRAME_BUFFER_SIZE]{};
    size_t  buf_len_{0};
    bool    synced_{false};         // true after first 0x68 seen
    uint8_t expected_len_{0};       // M-Bus frame length from header
    uint32_t last_frame_counter_{0};

    sensor::Sensor      *s_energy_consumed_{nullptr};
    sensor::Sensor      *s_energy_fed_{nullptr};
    sensor::Sensor      *s_power_active_{nullptr};
    sensor::Sensor      *s_power_reactive_{nullptr};
    sensor::Sensor      *s_voltage_l1_{nullptr};
    sensor::Sensor      *s_voltage_l2_{nullptr};
    sensor::Sensor      *s_voltage_l3_{nullptr};
    sensor::Sensor      *s_current_l1_{nullptr};
    sensor::Sensor      *s_current_l2_{nullptr};
    sensor::Sensor      *s_current_l3_{nullptr};
    sensor::Sensor      *s_power_factor_{nullptr};
    text_sensor::TextSensor *s_meter_serial_{nullptr};

    static const ObisEntry OBIS_TABLE[11];
};

}  // namespace stromzaehler
}  // namespace esphome
