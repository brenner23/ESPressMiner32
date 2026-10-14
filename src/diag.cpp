// Netz-Diagnose fuer den 10-Minuten-Fehlerschub (nur Test-Build, -DDIAG_NET=1)
//
// lwIP "raw"-PCBs fuer ICMP, IGMP, TCP und UDP sehen jedes ankommende
// IPv4-Paket NACH dem Entschluesseln (inkl. Broadcast/Multicast). Der
// Callback gibt 0 zurueck -> das Paket laeuft danach ganz normal weiter.
// Gleiche Pakete (Absender, Protokoll, Ports) innerhalb einer Sekunde werden
// zu einem Eintrag mit Zaehler zusammengefasst.
// Ein kleiner Task prueft alle 20 ms den HW-Fehlerzaehler.

#include "diag.h"

#if DIAG_NET
#include "stats.h"
#include <lwip/raw.h>
#include <lwip/ip.h>
#include <lwip/tcpip.h>

struct DiagEntry {
    uint32_t seq, ms, count;
    uint32_t src, dst;          // IPv4, Netzwerk-Byte-Reihenfolge
    uint16_t sport, dport, len;
    uint8_t  proto;             // 1 ICMP, 2 IGMP, 6 TCP, 17 UDP, 0 = HW-Fehler
    uint8_t  extra;             // ICMP-Typ / TCP-Flags / Anzahl neuer HW-Fehler
};

static const int N = 400;
static DiagEntry s_ring[N];
static uint32_t  s_seq = 0;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void add(uint8_t proto, uint32_t src, uint32_t dst, uint16_t sp, uint16_t dp, uint16_t len, uint8_t extra)
{
    const uint32_t now = millis();
    portENTER_CRITICAL(&s_mux);
    // juengsten Eintrag zusammenfassen, wenn gleich und < 1 s alt
    if (s_seq) {
        DiagEntry& last = s_ring[(s_seq - 1) % N];
        if (proto && last.proto == proto && last.src == src && last.dst == dst && last.sport == sp &&
            last.dport == dp && now - last.ms < 1000) {
            last.count++;
            portEXIT_CRITICAL(&s_mux);
            return;
        }
    }
    DiagEntry& e = s_ring[s_seq % N];
    e = { ++s_seq, now, 1, src, dst, sp, dp, len, proto, extra };
    portEXIT_CRITICAL(&s_mux);
}

static u8_t on_raw(void*, struct raw_pcb*, struct pbuf* p, const ip_addr_t*)
{
    if (p->len >= 20) {
        const uint8_t* b = (const uint8_t*)p->payload;
        if ((b[0] >> 4) == 4) {
            const int ihl = (b[0] & 0x0f) * 4;
            const uint8_t proto = b[9];
            uint32_t src, dst;
            memcpy(&src, b + 12, 4);
            memcpy(&dst, b + 16, 4);
            uint16_t sp = 0, dp = 0;
            uint8_t extra = 0;
            if ((proto == 6 || proto == 17) && p->len >= ihl + 4) {
                sp = (b[ihl] << 8) | b[ihl + 1];
                dp = (b[ihl + 2] << 8) | b[ihl + 3];
                if (proto == 6 && p->len >= ihl + 14) extra = b[ihl + 13];   // TCP-Flags
            } else if (proto == 1 && p->len > ihl) {
                extra = b[ihl];                                              // ICMP-Typ (8 = Ping)
            }
            add(proto, src, dst, sp, dp, (b[2] << 8) | b[3], extra);
        }
    }
    return 0;      // nicht verbraucht -> lwIP verarbeitet das Paket normal weiter
}

static void err_task(void*)
{
    uint32_t last = g_stats.hwErrors;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        const uint32_t e = g_stats.hwErrors;
        if (e != last) {
            add(0, 0, 0, 0, 0, 0, (uint8_t)min<uint32_t>(255, e - last));
            last = e;
        }
    }
}

void diag_begin()
{
    static bool started = false;
    if (started) return;
    started = true;
    LOCK_TCPIP_CORE();
    for (uint8_t proto : { (uint8_t)1, (uint8_t)2, (uint8_t)6, (uint8_t)17 }) {
        struct raw_pcb* pcb = raw_new(proto);
        if (!pcb) continue;
        ip_set_option(pcb, SOF_BROADCAST);          // auch Broadcasts sehen
        raw_bind(pcb, IP_ADDR_ANY);
        raw_recv(pcb, on_raw, nullptr);
    }
    UNLOCK_TCPIP_CORE();
    xTaskCreatePinnedToCore(err_task, "diagErr", 2048, nullptr, 1, nullptr, 0);
    Serial.println("[DIAG] network diagnostics active: GET /api/diag");
}

static void ip_str(char* out, uint32_t a)
{
    const uint8_t* b = (const uint8_t*)&a;
    sprintf(out, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

String diag_json(uint32_t since)
{
    static const char* PN[] = { "HWERR", "ICMP", "IGMP" };
    String out;
    out.reserve(16000);
    out = "{\"uptimeMs\":" + String(millis()) + ",\"seq\":" + String(s_seq) + ",\"entries\":[";
    portENTER_CRITICAL(&s_mux);
    const uint32_t newest = s_seq;
    portEXIT_CRITICAL(&s_mux);
    uint32_t first = since + 1;
    if (newest >= N && first < newest - N + 1) first = newest - N + 1;
    bool comma = false;
    char line[160], s[16], d[16];
    for (uint32_t q = first; q <= newest; q++) {
        portENTER_CRITICAL(&s_mux);
        DiagEntry e = s_ring[(q - 1) % N];
        portEXIT_CRITICAL(&s_mux);
        if (e.seq != q) continue;
        ip_str(s, e.src);
        ip_str(d, e.dst);
        const char* pn = e.proto == 6 ? "TCP" : e.proto == 17 ? "UDP" : e.proto < 3 ? PN[e.proto] : "IP";
        snprintf(line, sizeof(line),
                 "%s{\"seq\":%lu,\"ms\":%lu,\"n\":%lu,\"p\":\"%s\",\"src\":\"%s\",\"dst\":\"%s\",\"sp\":%u,\"dp\":%u,\"len\":%u,\"x\":%u}",
                 comma ? "," : "", (unsigned long)e.seq, (unsigned long)e.ms, (unsigned long)e.count, pn, s, d,
                 e.sport, e.dport, e.len, e.extra);
        out += line;
        comma = true;
    }
    out += "]}";
    return out;
}

#else
void   diag_begin() {}
String diag_json(uint32_t) { return "{\"error\":\"diagnostics not built in (DIAG_NET)\"}"; }
#endif
