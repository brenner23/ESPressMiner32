#include "handover.h"
#include "stats.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <rom/crc.h>

namespace handover {

struct Data {
    uint32_t magic;
    uint32_t uptimeS;
    double   bestDiff;
    uint32_t blocks;
    uint32_t restarts;
    uint32_t crc;
};

static const uint32_t MAGIC = 0x48414E44;   // "HAND"
static RTC_NOINIT_ATTR Data s_rtc;
static uint32_t s_offset = 0;
static uint32_t s_restarts = 0;

static uint32_t calc_crc(const Data& d)
{
    return crc32_le(0, (const uint8_t*)&d, offsetof(Data, crc));
}

void restore()
{
    const esp_reset_reason_t r = esp_reset_reason();
    const bool powerLoss = (r == ESP_RST_POWERON || r == ESP_RST_BROWNOUT || r == ESP_RST_UNKNOWN);
    const bool valid = s_rtc.magic == MAGIC && s_rtc.crc == calc_crc(s_rtc);
    if (!powerLoss && valid) {
        s_offset   = s_rtc.uptimeS;
        s_restarts = s_rtc.restarts + 1;
        g_stats.bestDiff    = s_rtc.bestDiff;
        g_stats.blocksFound = s_rtc.blocks;
        Serial.printf("[Handover] restart %lu since power-on: uptime %lu s, best diff %.4f, blocks %lu taken over\n",
                      (unsigned long)s_restarts, (unsigned long)s_offset, s_rtc.bestDiff, (unsigned long)s_rtc.blocks);
    } else {
        Serial.println(powerLoss ? "[Handover] power-on - starting from 0" : "[Handover] no saved values - starting from 0");
    }
    s_rtc.magic = 0;   // gelesen -> loeschen (save() legt die Werte danach neu ab)
}

void save(uint32_t uptimeS)
{
    Data d;
    d.magic    = MAGIC;
    d.uptimeS  = uptimeS;
    d.bestDiff = g_stats.bestDiff;
    d.blocks   = g_stats.blocksFound;
    d.restarts = s_restarts;
    d.crc      = calc_crc(d);
    s_rtc = d;
}

uint32_t uptime_offset() { return s_offset; }
uint32_t restarts() { return s_restarts; }

}  // namespace handover
