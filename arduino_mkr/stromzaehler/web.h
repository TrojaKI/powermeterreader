// arduino_mkr/stromzaehler/web.h
#pragma once
#include "dlms.h"
#include <WiFiNINA.h>

struct SysStats {
    uint32_t rx_bytes   = 0;
    uint32_t rx_frames  = 0;
    uint32_t t_last_pub = 0;  // millis() of last MQTT publish, 0=never
    bool     mqtt_ok    = false;
};

class WebServer {
 public:
    void begin();
    void handle(const MeterData &d, const SysStats &s);

 private:
    WiFiServer _server{80};
    MeterData  _last{};
    SysStats   _stats{};

    void serve_status(WiFiClient &c);
    void serve_update_form(WiFiClient &c);
    void handle_ota_upload(WiFiClient &c, int content_length);
    void send_header(WiFiClient &c, int code, const char *ct);
};
