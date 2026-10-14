#pragma once

namespace miner {
    void hw_task(void* arg);   // Hardware-SHA-Miner (Core 1)
    void sw_task(void* arg);   // Software-SHA-Miner (Core 0)
}

// Ueberhitzungsschutz (Grenzwerte in config.h: TEMP_*)
namespace thermal {
    void        update(float celsius);   // neuer Messwert (bei Pool-Job)
    void        poll();                  // Statistik-Task: liest selbst, wenn > 60 s nichts kam
    int         level();                 // 0 normal, 1 SW-Miner pausiert, 2 Mining gestoppt
    const char* text();
}
