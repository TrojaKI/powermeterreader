// arduino_mkr/stromzaehler/web.cpp
#include "web.h"
#include "version.h"
#include <WiFiStorage.h>
#include <stdio.h>
#include <string.h>

void WebServer::begin() {
    _server.begin();
}

void WebServer::handle(const MeterData &d, const SysStats &s) {
    _last  = d;
    _stats = s;

    WiFiClient c = _server.available();
    if (!c) return;

    char method[8] = {};
    char path[32]  = {};
    int  content_length = 0;

    // read HTTP headers until blank line
    char line[128];
    int  li    = 0;
    bool first = true;
    unsigned long t = millis();
    while (c.connected() && millis() - t < 2000) {
        if (!c.available()) continue;
        char ch = c.read();
        if (ch == '\r') continue;
        if (ch == '\n') {
            line[li] = '\0';
            if (first) {
                sscanf(line, "%7s %31s", method, path);
                first = false;
            } else if (strncasecmp(line, "Content-Length:", 15) == 0) {
                content_length = atoi(line + 15);
            } else if (li == 0) {
                break;  // blank line = end of headers
            }
            li = 0;
        } else if (li < 127) {
            line[li++] = ch;
        }
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
        serve_status(c);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/update") == 0) {
        serve_update_form(c);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/update") == 0) {
        handle_ota_upload(c, content_length);
    } else {
        send_header(c, 404, "text/plain");
        c.println("Not Found");
    }

    c.stop();
}

void WebServer::send_header(WiFiClient &c, int code, const char *ct) {
    c.print("HTTP/1.1 "); c.print(code); c.print(" OK\r\n");
    c.print("Content-Type: "); c.print(ct); c.print("\r\n");
    c.print("Connection: close\r\n\r\n");
}

void WebServer::serve_status(WiFiClient &c) {
    send_header(c, 200, "text/html; charset=utf-8");
    c.print(
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<meta http-equiv='refresh' content='30'>"
        "<title>Stromz\xC3\xA4hler</title>"
        "<style>body{font-family:sans-serif;max-width:520px;margin:2em auto}"
        "table{border-collapse:collapse;width:100%}"
        "td,th{padding:6px 12px;border:1px solid #ccc;text-align:right}"
        "th{background:#f0f0f0;text-align:left}"
        "th small{display:block;font-weight:normal;color:#888;font-size:0.8em}"
        "a{display:inline-block;margin-top:1em;padding:6px 12px;"
        "background:#0078d4;color:#fff;text-decoration:none;border-radius:4px}"
        "</style></head><body>"
        "<h2>N\xC3\x96 Netz Stromz\xC3\xA4hler</h2>"
        "<table>"
    );

    #define ROW(obis, label, fmt, val, unit) \
        { char _b[24]; snprintf(_b, sizeof(_b), fmt, val); \
          c.print("<tr><th>"); c.print(label); \
          c.print("<small>"); c.print(obis); c.print("</small>"); \
          c.print("</th><td>"); c.print(_b); \
          c.print(" " unit "</td></tr>"); }

    ROW("1.8.0",  "Energie Bezug",   "%.3f", _last.energy_consumed_wh / 1000.0f, "kWh")
    ROW("2.8.0",  "Energie Einsp.",  "%.3f", _last.energy_fed_wh       / 1000.0f, "kWh")
    ROW("1.7.0",  "Leistung Bezug",  "%lu",  (unsigned long)_last.power_active_w,     "W")
    ROW("2.7.0",  "Leistung Einsp.", "%lu",  (unsigned long)_last.power_reactive_w,   "W")
    ROW("32.7.0", "Spannung L1",     "%.1f", _last.voltage_l1,  "V")
    ROW("52.7.0", "Spannung L2",     "%.1f", _last.voltage_l2,  "V")
    ROW("72.7.0", "Spannung L3",     "%.1f", _last.voltage_l3,  "V")
    ROW("31.7.0", "Strom L1",        "%.2f", _last.current_l1,  "A")
    ROW("51.7.0", "Strom L2",        "%.2f", _last.current_l2,  "A")
    ROW("71.7.0", "Strom L3",        "%.2f", _last.current_l3,  "A")
    ROW("13.7.0", "Leistungsfaktor", "%.3f", _last.power_factor, "")
    #undef ROW

    c.print("<tr><th>Z\xC3\xA4hlernummer</th><td>");
    c.print(_last.meter_serial[0] ? _last.meter_serial : "-");
    c.print("</td></tr></table>");

    // diagnostic panel
    c.print("<h3 style='margin-top:1.5em'>Diagnose</h3><table>");
    {
        char _b[32];
        snprintf(_b, sizeof(_b), "%lu", (unsigned long)_stats.rx_bytes);
        c.print("<tr><th>M-Bus Bytes</th><td>"); c.print(_b); c.print("</td></tr>");
        snprintf(_b, sizeof(_b), "%lu", (unsigned long)_stats.rx_frames);
        c.print("<tr><th>M-Bus Frames</th><td>"); c.print(_b); c.print("</td></tr>");
        c.print("<tr><th>MQTT</th><td>");
        c.print(_stats.mqtt_ok ? "verbunden" : "getrennt");
        c.print("</td></tr>");
        if (_stats.t_last_pub > 0) {
            uint32_t ago = (millis() - _stats.t_last_pub) / 1000;
            snprintf(_b, sizeof(_b), "vor %lus", (unsigned long)ago);
            c.print("<tr><th>Letzter Publish</th><td>"); c.print(_b); c.print("</td></tr>");
        } else {
            c.print("<tr><th>Letzter Publish</th><td>-</td></tr>");
        }
        snprintf(_b, sizeof(_b), "%lus", (unsigned long)(millis() / 1000));
        c.print("<tr><th>Uptime</th><td>"); c.print(_b); c.print("</td></tr>");
    }
    c.print("</table>");
    c.print("<p style='color:#888;font-size:0.85em;margin-top:0.5em'>"
            "Firmware " FW_VERSION "</p>");
    c.print("<a href='/update'>Firmware Update</a>");
    c.print("</body></html>");
}

void WebServer::serve_update_form(WiFiClient &c) {
    send_header(c, 200, "text/html; charset=utf-8");
    c.print(
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<title>OTA Update</title>"
        "<style>body{font-family:sans-serif;max-width:400px;margin:2em auto}"
        "button{padding:8px 16px;background:#0078d4;color:#fff;border:none;"
        "border-radius:4px;cursor:pointer}"
        "#status{margin-top:1em;display:none}</style>"
        "</head><body>"
        "<h2>Firmware Update</h2>"
        "<p>Firmware-Datei (.bin) w\xC3\xA4hlen und hochladen.<br>"
        "Das Board startet automatisch neu.</p>"
        "<input type='file' id='f' accept='.bin'><br><br>"
        "<button onclick='upload()'>Upload</button>"
        "<div id='status'></div>"
        "<script>"
        "function upload(){"
        "  var f=document.getElementById('f').files[0];"
        "  if(!f){alert('Keine Datei gewählt');return;}"
        "  var s=document.getElementById('status');"
        "  s.style.display='block';s.textContent='Uploading '+f.name+'...';"
        "  fetch('/update',{method:'POST',"
        "    headers:{'Content-Type':'application/octet-stream'},"
        "    body:f})"
        "  .then(r=>r.text()).then(t=>{s.textContent=t;})"
        "  .catch(e=>{s.textContent='Fehler: '+e;});"
        "}"
        "</script></body></html>"
    );
}

void WebServer::handle_ota_upload(WiFiClient &c, int content_length) {
    if (content_length <= 0) {
        send_header(c, 400, "text/plain");
        c.println("Content-Length missing");
        return;
    }

    Serial.print("OTA upload: "); Serial.print(content_length); Serial.println(" bytes");

    WiFiStorage.remove("UPDATE.BIN");
    WiFiStorageFile f = WiFiStorage.open("UPDATE.BIN");

    int remaining = content_length;
    uint8_t buf[256];
    unsigned long t = millis();

    while (remaining > 0 && millis() - t < 30000) {
        int avail = c.available();
        if (avail <= 0) continue;
        int chunk = min(avail, min(remaining, (int)sizeof(buf)));
        int got = c.readBytes(buf, chunk);
        if (got > 0) {
            f.write(buf, got);
            remaining -= got;
            t = millis();  // reset timeout on progress
        }
    }
    f.close();

    if (remaining != 0) {
        Serial.println("OTA: upload incomplete");
        send_header(c, 500, "text/plain");
        c.println("Upload incomplete — try again");
        return;
    }

    Serial.println("OTA: upload complete, rebooting");
    send_header(c, 200, "text/plain");
    c.println("Upload OK — rebooting...");
    c.flush();
    c.stop();
    delay(500);
    NVIC_SystemReset();
}
