// arduino_mkr/stromzaehler/stromzaehler.ino
// NÖ Netz Smart Meter reader — Arduino MKR WIFI 1010 + Sagemcom T210D
#include "config.h"
#include "version.h"
#include "mbus.h"
#include "dlms.h"
#include "mqtt_ha.h"
#include "web.h"
#include <WiFiNINA.h>

static WiFiClient wifiClient;
static MqttHa     mqttHa(wifiClient);
static WebServer  webServer;

static void wifi_connect() {
    if (WiFi.status() == WL_CONNECTED) return;
    if (WiFi.status() == WL_NO_MODULE) {
        Serial.println("WiFi module not responding — NINA SPI failure");
        return;
    }
    Serial.print("WiFi connecting to: "); Serial.println(wifi_ssid());
    WiFi.begin(wifi_ssid(), wifi_pass());
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
        delay(500); Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("\nIP: "); Serial.println(WiFi.localIP());
    } else {
        // 1=NO_SSID 3=CONNECTED 4=CONNECT_FAILED 6=DISCONNECTED 7=AP_SCAN
        Serial.print("\nWiFi failed — status="); Serial.println(WiFi.status());
    }
}

void setup() {
    Serial.begin(115200);
    Serial1.begin(2400, SERIAL_8E1);  // M-Bus: even parity required

    Serial.print("Stromzaehler fw="); Serial.println(FW_VERSION);
    wifi_connect();
    mqttHa.begin(MQTT_BROKER, MQTT_PORT, MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS);
    webServer.begin();
    Serial.print("Web: http://"); Serial.println(WiFi.localIP());
}

void loop() {
    wifi_connect();
    mqttHa.loop();

    static MeterData last_data{};
    static uint8_t   plain[256];
    static uint8_t   frame1_buf[256];
    static size_t    frame1_len    = 0;
    static bool      has_frame1    = false;
    static uint32_t  rx_bytes      = 0;
    static uint32_t  rx_frames     = 0;
    static uint32_t  t_report      = 0;
    static uint32_t  t_last_pub    = 0;

    while (Serial1.available()) {
        uint8_t b = (uint8_t)Serial1.read();
        rx_bytes++;
        if (!mbus_process_byte(b)) continue;

        size_t flen = mbus_frame_len();
        rx_frames++;

        Serial.print("MBUS "); Serial.print(flen); Serial.print("B: ");
        for (size_t i = 0; i < min(flen, (size_t)12); i++) {
            if (mbus_frame()[i] < 0x10) Serial.print("0");
            Serial.print(mbus_frame()[i], HEX); Serial.print(" ");
        }
        Serial.println();

        if (flen == 256) {
            memcpy(frame1_buf, mbus_frame(), 256);
            frame1_len = 256;
            has_frame1 = true;
        } else if (flen == 26 && has_frame1) {
            has_frame1 = false;
            size_t plain_len = 0;

            // build debug hex prefix (first 12 bytes of each frame)
            char dbg[140];
            auto hex12 = [](const uint8_t *b, size_t n, char *out) {
                static const char H[] = "0123456789ABCDEF";
                for (size_t i = 0; i < n; i++) { *out++=H[b[i]>>4]; *out++=H[b[i]&0xF]; }
                *out = '\0';
            };
            char h1[25], h2[25];
            hex12(frame1_buf,    12, h1);
            hex12(mbus_frame(), 12, h2);

            bool do_publish = (millis() - t_last_pub >= PUBLISH_INTERVAL_MS);

            if (dlms_decrypt(frame1_buf, frame1_len,
                             mbus_frame(), flen,
                             GUEK_HEX, plain, plain_len)) {
                MeterData data;
                if (dlms_parse(plain, plain_len, data)) {
                    last_data = data;
                    if (do_publish) {
                        mqttHa.publish(data);
                        mqttHa.publish_hex("debug/plain", plain, plain_len);
                        t_last_pub = millis();
                    }
                    snprintf(dbg, sizeof(dbg), "OK P+:%luW E:%.3fkWh U1:%.1fV f1:%s f2:%s",
                             (unsigned long)data.power_active_w,
                             data.energy_consumed_wh / 1000.0f,
                             data.voltage_l1, h1, h2);
                    Serial.println(dbg);
                } else {
                    snprintf(dbg, sizeof(dbg), "PARSE_FAIL f1:%s f2:%s", h1, h2);
                    Serial.println(dbg);
                    mqttHa.publish_debug(dbg);
                }
            } else {
                snprintf(dbg, sizeof(dbg), "DECRYPT_FAIL f1:%s f2:%s", h1, h2);
                Serial.println(dbg);
                mqttHa.publish_debug(dbg);
            }
        } else {
            has_frame1 = false;
        }

        mbus_reset();
    }

    if (millis() - t_report > 10000) {
        Serial.print("rx="); Serial.print(rx_bytes);
        Serial.print(" frames="); Serial.println(rx_frames);
        t_report = millis();
    }

    webServer.handle(last_data);
}
