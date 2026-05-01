// arduino_mkr/stromzaehler/mqtt_ha.cpp
#include "mqtt_ha.h"
#include "version.h"
#include <stdio.h>

MqttHa::MqttHa(WiFiClient &wc) : _client(wc) {}

void MqttHa::begin(const char *broker, uint16_t port, const char *cid,
                   const char *user, const char *pass) {
    _broker = broker; _port = port; _cid = cid; _user = user; _pass = pass;
    _client.setServer(_broker, _port);
    _client.setBufferSize(600);  // plain hex dump: 243B × 2 = 486 chars
}

void MqttHa::loop() {
    if (!_client.connected()) {
        reconnect();
        _discovery_sent = false;
    }
    _client.loop();
    if (_client.connected() && !_discovery_sent) {
        send_discovery();
        _discovery_sent = true;
    }
}

void MqttHa::reconnect() {
    for (int i = 0; i < 3 && !_client.connected(); i++) {
        Serial.print("MQTT connect... ");
        bool ok = (_user && _user[0] != '\0')
            ? _client.connect(_cid, _user, _pass)
            : _client.connect(_cid);
        if (ok) { Serial.println("OK"); return; }
        Serial.print("failed rc="); Serial.println(_client.state());
        delay(3000);
    }
}

static void disc_sensor(PubSubClient &c, const char *field, const char *name,
                         const char *unit, const char *dev_class,
                         const char *state_class, int decimals) {
    char topic[80], payload[480];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/stromzaehler_%s/config", field);
    snprintf(payload, sizeof(payload),
        "{\"name\":\"%s\","
         "\"state_topic\":\"stromzaehler/%s\","
         "\"unit_of_measurement\":\"%s\","
         "\"device_class\":\"%s\","
         "\"state_class\":\"%s\","
         "\"suggested_display_precision\":%d,"
         "\"unique_id\":\"smkr_%s\","
         "\"device\":{\"identifiers\":[\"stromzaehler_mkr\"],"
                     "\"name\":\"N\\u00d6 Netz Stromz\\u00e4hler\","
                     "\"model\":\"Arduino MKR WIFI 1010\","
                     "\"sw_version\":\"" FW_VERSION "\"}}",
        name, field, unit, dev_class, state_class, decimals, field);
    c.publish(topic, payload, true);
}

void MqttHa::send_discovery() {
    disc_sensor(_client, "energy_consumed_wh", "Energie Bezug",      "kWh","energy",       "total_increasing", 3);
    disc_sensor(_client, "energy_fed_wh",      "Energie Einspeisung","kWh","energy",       "total_increasing", 3);
    disc_sensor(_client, "power_active_w",     "Leistung Bezug",     "W", "power",        "measurement",      0);
    disc_sensor(_client, "power_reactive_w",   "Leistung Einsp.",    "W", "power",        "measurement",      0);
    disc_sensor(_client, "voltage_l1",         "Spannung L1",        "V", "voltage",      "measurement",      1);
    disc_sensor(_client, "voltage_l2",         "Spannung L2",        "V", "voltage",      "measurement",      1);
    disc_sensor(_client, "voltage_l3",         "Spannung L3",        "V", "voltage",      "measurement",      1);
    disc_sensor(_client, "current_l1",         "Strom L1",           "A", "current",      "measurement",      2);
    disc_sensor(_client, "current_l2",         "Strom L2",           "A", "current",      "measurement",      2);
    disc_sensor(_client, "current_l3",         "Strom L3",           "A", "current",      "measurement",      2);
    disc_sensor(_client, "power_factor",       "Leistungsfaktor",    "",  "power_factor", "measurement",      3);

    // Text sensor for meter serial (no unit/device_class)
    char topic[80];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/stromzaehler_meter_serial/config");
    _client.publish(topic,
        "{\"name\":\"Z\\u00e4hlernummer\","
         "\"state_topic\":\"stromzaehler/meter_serial\","
         "\"unique_id\":\"smkr_meter_serial\","
         "\"device\":{\"identifiers\":[\"stromzaehler_mkr\"],"
                     "\"name\":\"N\\u00d6 Netz Stromz\\u00e4hler\","
                     "\"model\":\"Arduino MKR WIFI 1010\","
                     "\"sw_version\":\"" FW_VERSION "\"}}",
        true);
}

void MqttHa::publish(const MeterData &d) {
    if (!_client.connected()) return;
    char buf[32];

    #define PUB(field, fmt, val) \
        snprintf(buf, sizeof(buf), fmt, val); \
        _client.publish("stromzaehler/" field, buf, true);

    PUB("energy_consumed_wh", "%.3f", (float)d.energy_consumed_wh / 1000.0f)
    PUB("energy_fed_wh",      "%.3f", (float)d.energy_fed_wh       / 1000.0f)
    PUB("power_active_w",     "%lu", (unsigned long)d.power_active_w)
    PUB("power_reactive_w",   "%lu", (unsigned long)d.power_reactive_w)
    PUB("voltage_l1",  "%.1f", d.voltage_l1)
    PUB("voltage_l2",  "%.1f", d.voltage_l2)
    PUB("voltage_l3",  "%.1f", d.voltage_l3)
    PUB("current_l1",  "%.2f", d.current_l1)
    PUB("current_l2",  "%.2f", d.current_l2)
    PUB("current_l3",  "%.2f", d.current_l3)
    PUB("power_factor","%.3f", d.power_factor)
    #undef PUB

    if (d.meter_serial[0] != '\0')
        _client.publish("stromzaehler/meter_serial", d.meter_serial, true);
}

void MqttHa::publish_debug(const char *msg) {
    if (!_client.connected()) return;
    _client.publish("stromzaehler/debug", msg, false);
}

void MqttHa::publish_hex(const char *subtopic, const uint8_t *data, size_t len) {
    if (!_client.connected()) return;
    static const char H[] = "0123456789ABCDEF";
    static char hex[490];
    size_t n = len < 244 ? len : 243;
    for (size_t i = 0; i < n; i++) { hex[i*2]=H[data[i]>>4]; hex[i*2+1]=H[data[i]&0xF]; }
    hex[n*2] = '\0';
    char topic[48];
    snprintf(topic, sizeof(topic), "stromzaehler/%s", subtopic);
    _client.publish(topic, hex, false);
}
