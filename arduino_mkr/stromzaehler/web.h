// arduino_mkr/stromzaehler/web.h
#pragma once
#include "dlms.h"
#include <WiFiNINA.h>

class WebServer {
 public:
    void begin();
    void handle(const MeterData &d);  // call every loop(); caches last reading

 private:
    WiFiServer _server{80};
    MeterData  _last{};

    void serve_status(WiFiClient &c);
    void serve_update_form(WiFiClient &c);
    void handle_ota_upload(WiFiClient &c, int content_length);
    void send_header(WiFiClient &c, int code, const char *ct);
};
