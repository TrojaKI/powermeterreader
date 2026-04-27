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
    void loop();                  // call every loop() iteration
    void publish(const MeterData &d);

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
