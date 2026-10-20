#pragma once
#include <stdint.h>

namespace miner {
    void hw_task(void* arg);   // Hardware-SHA-Miner (Core 1)
    void sw_task(void* arg);   // Software-SHA-Miner (Core 0)
    extern volatile bool g_paused;   // ab 1.3.1.002: true = beide Miner warten (z. B. Firmware-Upload)
}

// Ueberhitzungsschutz (Grenzwerte in config.h: TEMP_*)
namespace thermal {
    void        update(float celsius);   // neuer Messwert (bei Pool-Job)
    void        poll();                  // Statistik-Task: liest selbst, wenn > 60 s nichts kam
    int         level();                 // 0 normal, 1 SW-Miner pausiert, 2 Mining gestoppt
    const char* text();
}

// Ab 1.3.0.008: Schutzfenster gegen die periodische Stoerung aus dem Netz (config.h: HW_GUARD_*)
namespace guard {
    void task(void* arg);                     // Core 0, beobachtet HW-Fehler und schaltet die Fenster
    extern volatile uint32_t g_simErrors;     // nur Diag-Build: gespielte Fehler fuer den Test
}
