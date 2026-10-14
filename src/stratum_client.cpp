#include "stratum_client.h"
#include "config.h"
#include "mining_job.h"
#include "sha256_sw.h"
#include "stats.h"
#include "miner.h"
#include "utils.h"
#include "settings.h"

#include <WiFi.h>

enum : uint32_t {
    ID_SUBSCRIBE = 1,
    ID_AUTHORIZE = 2,
    ID_SUGGEST   = 3,
};

static const size_t RX_MAX = 16 * 1024;

// Fehlversuche, nach denen auf den anderen Pool gewechselt wird
static const int      POOL_MAX_FAILS          = 3;
// Wie lange auf dem Secondary Pool gemint wird, bevor der Primary erneut versucht wird
static const uint32_t SECONDARY_RETRY_PRIMARY = 10UL * 60UL * 1000UL;

// Manueller Pool-Wechsel (vom Webserver angefordert)
static volatile bool s_switchRequest = false;
void stratum_request_switch() { s_switchRequest = true; }

void StratumClient::begin()
{
    selectPool(0);
}

bool StratumClient::poolConfigured(int idx) const
{
    return g_settings.pools[idx].url.length() > 0;
}

void StratumClient::selectPool(int idx)
{
    const PoolConfig& pc = g_settings.pools[idx];
    String u = pc.url;
    int scheme = u.indexOf("://");
    if (scheme >= 0) u = u.substring(scheme + 3);
    int colon = u.lastIndexOf(':');
    if (colon >= 0) {
        _host = u.substring(0, colon);
        _port = (uint16_t)u.substring(colon + 1).toInt();
    } else {
        _host = u;
        _port = 3333;
    }
    _user = pc.user;
    _pass = pc.pass;
    _poolIdx = idx;
    _failCount = 0;
    _poolSinceMs = millis();
    g_stats.activePool = idx;
    Serial.printf("[Stratum] %s Pool: %s\n", idx == 0 ? "Primary" : "Secondary", pc.url.c_str());
}

void StratumClient::onConnectFailed()
{
    _failCount++;
    int other = 1 - _poolIdx;
    if (_failCount >= POOL_MAX_FAILS && poolConfigured(other)) {
        Serial.printf("[Stratum] %d failed attempts - switching pool\n", _failCount);
        selectPool(other);
    }
}

bool StratumClient::send(JsonDocument& doc)
{
    String line;
    serializeJson(doc, line);
    line += '\n';
    return _client.print(line) == line.length();
}

bool StratumClient::connectPool()
{
    Serial.printf("[Stratum] Connecting to %s:%u ...\n", _host.c_str(), _port);
    if (!_client.connect(_host.c_str(), _port, 5000)) {
        Serial.println("[Stratum] Connection failed");
        return false;
    }
    _client.setNoDelay(true);
    _rx = "";
    _lastRxMs = millis();
    _notify.valid = false;
    g_stats.poolConnected = true;
    g_stats.poolAuthorized = false;

    JsonDocument doc;
    doc["id"] = ID_SUBSCRIBE;
    doc["method"] = "mining.subscribe";
    doc["params"].add(MINER_NAME "/" MINER_VERSION);
    if (!send(doc)) return false;

    doc.clear();
    doc["id"] = ID_SUGGEST;
    doc["method"] = "mining.suggest_difficulty";
    doc["params"].add(POOL_SUGGEST_DIFFICULTY);
    send(doc);

    doc.clear();
    doc["id"] = ID_AUTHORIZE;
    doc["method"] = "mining.authorize";
    doc["params"].add(_user);
    doc["params"].add(_pass);
    return send(doc);
}

void StratumClient::disconnect(const char* reason)
{
    Serial.printf("[Stratum] Disconnected: %s\n", reason);
    _client.stop();
    jobs::invalidate();
    g_stats.poolConnected = false;
    g_stats.poolAuthorized = false;
}

void StratumClient::pollInput()
{
    while (_client.available()) {
        char buf[512];
        int n = _client.read((uint8_t*)buf, sizeof(buf));
        if (n <= 0) break;
        _lastRxMs = millis();

        for (int i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                if (_rx.length()) handleLine((char*)_rx.c_str(), _rx.length());
                _rx = "";
            } else if (buf[i] != '\r') {
                _rx += buf[i];
                if (_rx.length() > RX_MAX) {
                    Serial.println("[Stratum] Line too long, discarded");
                    _rx = "";
                }
            }
        }
    }
}

void StratumClient::handleLine(char* line, size_t len)
{
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, line, len);
    if (err) {
        Serial.printf("[Stratum] JSON error: %s\n", err.c_str());
        return;
    }

    const char* method = doc["method"];
    if (method) {
        if (strcmp(method, "mining.notify") == 0) {
            handleNotify(doc["params"].as<JsonArrayConst>());
        } else if (strcmp(method, "mining.set_difficulty") == 0) {
            double d = doc["params"][0] | 0.0;
            if (d > 0) {
                jobs::setDifficulty(d);
                char s[16];
                format_difficulty(d, s, sizeof(s));
                Serial.printf("[Stratum] New pool difficulty: %s\n", s);
            }
        } else {
            Serial.printf("[Stratum] Ignoring method %s\n", method);
        }
        return;
    }

    uint32_t id = doc["id"] | 0u;
    bool hasError = !doc["error"].isNull();

    if (id == ID_SUBSCRIBE) {
        if (hasError || !doc["result"].is<JsonArray>()) {
            disconnect("subscribe rejected");
            onConnectFailed();
            return;
        }
        _extranonce1 = doc["result"][1].as<const char*>();
        _en2Size = doc["result"][2] | 4;
        if (_en2Size < 1 || _en2Size > 16) _en2Size = 4;
        Serial.printf("[Stratum] Subscribed: extranonce1=%s, extranonce2_size=%d\n",
                      _extranonce1.c_str(), _en2Size);
    } else if (id == ID_AUTHORIZE) {
        bool ok = doc["result"] | false;
        g_stats.poolAuthorized = ok;
        Serial.printf("[Stratum] Authorization %s\n", ok ? "OK" : "FAILED");
        if (ok) {
            _failCount = 0;
        } else {
            String e;
            serializeJson(doc["error"], e);
            Serial.printf("[Stratum] Error: %s\n", e.c_str());
            disconnect("authorization failed");
            onConnectFailed();
        }
    } else if (id == ID_SUGGEST) {
        // Antwort ist optional und wird ignoriert
    } else if (id >= 100) {
        bool ok = doc["result"] | false;
        if (ok) {
            g_stats.accepted++;
            Serial.println("[Stratum] Share accepted");
        } else {
            g_stats.rejected++;
            String e;
            serializeJson(doc["error"], e);
            Serial.printf("[Stratum] Share rejected: %s\n", e.c_str());
        }
    }
}

void StratumClient::handleNotify(JsonArrayConst p)
{
    if (p.size() < 9) return;

    _notify.jobId    = p[0].as<const char*>();
    _notify.prevhash = p[1].as<const char*>();
    _notify.coinb1   = p[2].as<const char*>();
    _notify.coinb2   = p[3].as<const char*>();
    _notify.branches.clear();
    for (JsonVariantConst b : p[4].as<JsonArrayConst>()) _notify.branches.push_back(b.as<const char*>());
    _notify.version  = p[5].as<const char*>();
    _notify.nbits    = p[6].as<const char*>();
    _notify.ntime    = p[7].as<const char*>();
    _notify.valid    = true;
    g_stats.jobsReceived++;
    // Chip-Temperatur nur bei neuem Job lesen: Core 0 ist hier ohnehin mit Netzwerk
    // beschaeftigt, ein zusaetzlicher Registerzugriff alle 30-60 s faellt nicht ins Gewicht.
    // Interner Sensor des klassischen ESP32: undokumentiert, grob (+-10 Grad Versatz).
    thermal::update(temperatureRead());   // + Ueberhitzungsschutz

    buildAndPublishJob();
}

void StratumClient::buildAndPublishJob()
{
    if (!_notify.valid) return;

    // extranonce2 (Big-Endian Zaehler, links mit Nullen aufgefuellt)
    uint8_t en2[16] = {};
    uint32_t ctr = _en2Counter++;
    for (int i = 0; i < 4 && i < _en2Size; i++) en2[_en2Size - 1 - i] = (uint8_t)(ctr >> (8 * i));
    char en2Hex[40];
    bytes_to_hex(en2, _en2Size, en2Hex);

    // Coinbase-Transaktion zusammensetzen und hashen
    String cbHex = _notify.coinb1 + _extranonce1 + en2Hex + _notify.coinb2;
    size_t cbMax = cbHex.length() / 2;
    uint8_t* cb = (uint8_t*)malloc(cbMax);
    if (!cb) return;
    size_t cbLen = hex_to_bytes(cbHex.c_str(), cb, cbMax);
    uint8_t merkle[32];
    sha256d(cb, cbLen, merkle);
    free(cb);

    // Merkle-Root
    for (const String& br : _notify.branches) {
        uint8_t buf[64];
        memcpy(buf, merkle, 32);
        hex_to_bytes(br.c_str(), buf + 32, 32);
        sha256d(buf, 64, merkle);
    }

    static MiningJob job;   // gross -> nicht auf den Stack
    memset(&job, 0, sizeof(job));
    strncpy(job.jobId, _notify.jobId.c_str(), sizeof(job.jobId) - 1);
    strncpy(job.extranonce2, en2Hex, sizeof(job.extranonce2) - 1);
    strncpy(job.ntime, _notify.ntime.c_str(), sizeof(job.ntime) - 1);

    uint32_t version = strtoul(_notify.version.c_str(), nullptr, 16);
    uint32_t nbits   = strtoul(_notify.nbits.c_str(), nullptr, 16);
    uint32_t ntime   = strtoul(_notify.ntime.c_str(), nullptr, 16);

    uint8_t prev[32];
    hex_to_bytes(_notify.prevhash.c_str(), prev, 32);

    uint8_t* h = job.header;
    write_le32(h + 0, version);
    for (int w = 0; w < 8; w++)
        for (int b = 0; b < 4; b++) h[4 + 4 * w + b] = prev[4 * w + 3 - b];
    memcpy(h + 36, merkle, 32);
    write_le32(h + 68, ntime);
    write_le32(h + 72, nbits);
    write_le32(h + 76, 0);

    for (int i = 0; i < 16; i++) job.blk1[i] = read_be32(h + 4 * i);
    for (int i = 0; i < 3; i++)  job.blk2[i] = read_be32(h + 64 + 4 * i);
    memcpy(job.midstate, SHA256_H0, 32);
    sha256_transform(job.midstate, job.blk1);

    job.networkDiff = nbits_difficulty(nbits);
    g_stats.networkDiff = job.networkDiff;

    jobs::publish(job);
}

void StratumClient::submitShares()
{
    ShareSubmit s;
    while (jobs::popShare(s)) {
        if (!_client.connected()) continue;

        char nonceHex[9];
        snprintf(nonceHex, sizeof(nonceHex), "%08lx", (unsigned long)s.nonce);

        JsonDocument doc;
        doc["id"] = _nextId++;
        doc["method"] = "mining.submit";
        JsonArray p = doc["params"].to<JsonArray>();
        p.add(_user);
        p.add(s.jobId);
        p.add(s.extranonce2);
        p.add(s.ntime);
        p.add(nonceHex);
        if (send(doc)) g_stats.submitted++;

        char d[16];
        format_difficulty(s.difficulty, d, sizeof(d));
        Serial.printf("[Stratum] Share sent: job %s nonce %s diff %s\n", s.jobId, nonceHex, d);
    }
}

void StratumClient::run()
{
    uint32_t lastAttempt = 0;

    for (;;) {
        // Manueller Wechsel auf den anderen konfigurierten Pool
        if (s_switchRequest) {
            s_switchRequest = false;
            int other = 1 - _poolIdx;
            if (poolConfigured(other)) {
                disconnect("pool switched manually");
                selectPool(other);
            }
        }

        // Abwechseln-Automatik: alle poolAltMin Minuten den Pool tauschen
        uint32_t altMs = (uint32_t)g_settings.poolAltMin * 60000UL;
        if (altMs && poolConfigured(0) && poolConfigured(1) &&
            millis() - _poolSinceMs > altMs) {
            disconnect("pool switch (alternating)");
            selectPool(1 - _poolIdx);
        }

        if (WiFi.status() != WL_CONNECTED) {
            if (_client.connected()) disconnect("Wi-Fi lost");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (!_client.connected()) {
            if (g_stats.poolConnected) {
                bool wasAuthorized = g_stats.poolAuthorized;
                disconnect("connection closed by pool");
                if (!wasAuthorized) onConnectFailed();
            }
            if (millis() - lastAttempt >= STRATUM_RECONNECT_MS || lastAttempt == 0) {
                lastAttempt = millis();
                if (!connectPool()) {
                    _client.stop();
                    onConnectFailed();
                }
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        pollInput();
        submitShares();

        if (jobs::takeNewWorkRequest()) buildAndPublishJob();

        if (millis() - _lastRxMs > STRATUM_RX_TIMEOUT_MS) {
            disconnect("Timeout");
            onConnectFailed();
        }

        // Im Failover-Modus (kein Abwechseln): vom Secondary regelmaessig zurueck zum Primary
        if (!altMs && _poolIdx == 1 && poolConfigured(0) &&
            millis() - _poolSinceMs > SECONDARY_RETRY_PRIMARY) {
            disconnect("retrying the primary pool");
            selectPool(0);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
