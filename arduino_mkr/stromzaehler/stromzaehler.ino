// arduino_mkr/stromzaehler/stromzaehler.ino
// NÖ Netz Smart Meter reader — Arduino MKR WIFI 1010
#include "config.h"
#include "mbus.h"
#include "dlms.h"
#include "mqtt_ha.h"
#include <WiFiNINA.h>
#include <ArduinoOTA.h>

static WiFiClient wifiClient;
static MqttHa     mqttHa(wifiClient);

static void wifi_connect() {
    if (WiFi.status() == WL_CONNECTED) return;
    Serial.print("WiFi connecting");
    WiFi.begin(wifi_ssid(), wifi_pass());
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
        delay(500); Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("\nIP: "); Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi failed — will retry in loop");
    }
}

void setup() {
    Serial.begin(115200);              // USB debug
    Serial1.begin(2400, SERIAL_8E1);   // M-Bus: 2400 8E1 (required by M-Bus standard)

    wifi_connect();
    ArduinoOTA.begin(WiFi.localIP(), "stromzaehler", OTA_PASSWORD, WiFiStorage);
    mqttHa.begin(MQTT_BROKER, MQTT_PORT, MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS);
}

void loop() {
    ArduinoOTA.poll();
    wifi_connect();
    mqttHa.loop();

    static uint8_t plain[400];
    while (Serial1.available()) {
        uint8_t b = (uint8_t)Serial1.read();
        if (!mbus_process_byte(b)) continue;

        size_t plain_len = 0;
        if (dlms_decrypt(mbus_frame(), mbus_frame_len(), GUEK_HEX, plain, plain_len)) {
            MeterData data;
            if (dlms_parse(plain, plain_len, data)) {
                mqttHa.publish(data);
                Serial.print("P+: ");   Serial.print(data.power_active_w);
                Serial.print(" W  E: "); Serial.print(data.energy_consumed_wh);
                Serial.print(" Wh  U1: "); Serial.print(data.voltage_l1, 1);
                Serial.println(" V");
            }
        }
        mbus_reset();
    }
}
