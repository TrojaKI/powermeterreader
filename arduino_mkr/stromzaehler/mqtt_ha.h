// arduino_mkr/stromzaehler/mqtt_ha.h
#pragma once
#include "dlms.h"
#include <PubSubClient.h>
#include <WiFiNINA.h>

class MqttHa {
 public:
    explicit MqttHa(WiFiClient &wc);
    void begin(const char *broker, uint16_t port, const char *client_id,
               const char *user = "", const char *pass = "");
    void loop();
    bool connected() { return _client.connected(); }
    void publish(const MeterData &d);
    void publish_debug(const char *msg);  // → stromzaehler/debug (not retained)
    void publish_hex(const char *subtopic, const uint8_t *data, size_t len);  // → stromzaehler/<subtopic>

 private:
    void reconnect();
    void send_discovery();

    PubSubClient _client;
    const char  *_broker{nullptr};
    uint16_t     _port{1883};
    const char  *_cid{nullptr};
    const char  *_user{nullptr};
    const char  *_pass{nullptr};
    bool         _discovery_sent{false};
};
