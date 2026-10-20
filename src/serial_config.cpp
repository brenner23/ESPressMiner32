#include "serial_config.h"
#include "config.h"
#include "settings.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

static const int      RESET_BUTTON_PIN = 0;
static const uint32_t BOOT_HOLD_MS     = 5000;
static const size_t   CMD_LINE_MAX         = 1024;

static void reply(JsonDocument& doc)
{
    Serial.print("@CM ");
    serializeJson(doc, Serial);
    Serial.println();
}

static void reply_error(const char* msg)
{
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = msg;
    reply(doc);
}

static void restart_soon()
{
    Serial.flush();
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP.restart();
}

static void handle_line(const String& line)
{
    JsonDocument in;
    if (deserializeJson(in, line)) {
        reply_error("Invalid JSON");
        return;
    }
    const char* cmd = in["cmd"] | "";
    JsonDocument out;

    if (strcmp(cmd, "info") == 0) {
        out["ok"]         = true;
        out["name"]       = MINER_NAME;
        out["version"]    = MINER_VERSION;
        char mac[18];
        uint64_t m = ESP.getEfuseMac();
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", (uint8_t)m, (uint8_t)(m >> 8),
                 (uint8_t)(m >> 16), (uint8_t)(m >> 24), (uint8_t)(m >> 32), (uint8_t)(m >> 40));
        out["mac"]        = mac;
        out["ip"]         = WiFi.localIP().toString();
        out["configured"] = settings_configured();
        reply(out);
    } else if (strcmp(cmd, "get") == 0) {
        out["ok"] = true;
        settings_to_json(out["config"].to<JsonObject>());
        reply(out);
    } else if (strcmp(cmd, "set") == 0) {
        Settings s = g_settings;
        settings_apply_json(in.as<JsonVariantConst>(), s);
        if (const char* err = settings_validate(s)) {
            reply_error(err);
            return;
        }
        if (!settings_save(s)) {
            reply_error("Saving failed");
            return;
        }
        out["ok"] = true;
        out["msg"] = "Saved - restarting";
        reply(out);
        restart_soon();
    } else if (strcmp(cmd, "reset") == 0) {
        settings_clear();
        out["ok"] = true;
        out["msg"] = "Settings erased - restarting into setup portal";
        reply(out);
        restart_soon();
    } else if (strcmp(cmd, "restart") == 0) {
        out["ok"] = true;
        reply(out);
        restart_soon();
    } else {
        reply_error("Unknown command (info, get, set, reset, restart)");
    }
}

static void serial_config_task(void*)
{
    String   line;
    uint32_t bootPressedSince = 0;
    pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

    for (;;) {
        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r') {
                line.trim();
                if (line.startsWith("{")) handle_line(line);
                line = "";
            } else if (line.length() < CMD_LINE_MAX) {
                line += c;
            }
        }

        // BOOT-Taste lange gedrueckt -> Werkseinstellungen
        if (BOOT_RESET_ENABLE && digitalRead(RESET_BUTTON_PIN) == LOW) {
            if (!bootPressedSince) bootPressedSince = millis();
            if (millis() - bootPressedSince > BOOT_HOLD_MS) {
                Serial.println("[Setup] BOOT button held - erasing settings");
                settings_clear();
                restart_soon();
            }
        } else {
            bootPressedSince = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void serial_config_start()
{
    xTaskCreatePinnedToCore(serial_config_task, "serialCfg", 6144, nullptr, 1, nullptr, 0);
}
