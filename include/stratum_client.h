#pragma once
#include <Arduino.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <vector>

// Manuellen Pool-Wechsel anfordern (thread-sicher, vom Webserver aufgerufen)
void stratum_request_switch();
// Primary und Secondary dauerhaft tauschen (speichern + sofort mit dem neuen Primary verbinden)
void stratum_request_swap();

class StratumClient {
public:
    void begin();   // Pools aus g_settings (Primary + optional Secondary)
    void run();     // Endlosschleife, laeuft im eigenen Task

private:
    void selectPool(int idx);
    bool poolConfigured(int idx) const;
    void onConnectFailed();

    int        _poolIdx = 0;
    int        _failCount = 0;
    uint32_t   _poolSinceMs = 0;

    bool connectPool();
    void disconnect(const char* reason);
    bool send(JsonDocument& doc);
    void pollInput();
    void handleLine(char* line, size_t len);
    void handleNotify(JsonArrayConst params);
    void buildAndPublishJob();
    void submitShares();


    WiFiClient _client;
    String     _host;
    uint16_t   _port = 3333;
    String     _user;
    String     _pass;

    String     _rx;
    uint32_t   _nextId = 100;
    uint32_t   _lastRxMs = 0;

    String     _extranonce1;
    int        _en2Size = 4;
    uint32_t   _en2Counter = 0;

    struct Notify {
        bool   valid = false;
        String jobId, prevhash, coinb1, coinb2, version, nbits, ntime;
        std::vector<String> branches;
    } _notify;
};
