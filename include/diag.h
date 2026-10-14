#pragma once
// Netz-Diagnose (nur Test-Build, -DDIAG_NET=1): protokolliert jedes ankommende
// IP-Paket (ICMP/Ping, IGMP, TCP, UDP) mit Absender und jeden HW-Fehler mit
// Zeitstempel in einen Ringpuffer. Abruf: GET /api/diag?since=<seq>

#include <Arduino.h>

void   diag_begin();                       // nach WLAN-Verbindung aufrufen
String diag_json(uint32_t sinceSeq);       // Eintraege mit seq > sinceSeq
