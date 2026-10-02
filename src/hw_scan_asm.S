# ESPressMiner32 Master 002 - Hash-Schleife fuer den SHA-Beschleuniger in Assembler
#
# Xtensa LX6 (ESP32), Windowed-ABI. Aufruf aus C:
#     uint32_t hw_scan_asm(HwScanArgs* a);       (Struktur siehe sha256_hw.cpp)
#
# Ablauf pro Nonce (wie die C-Version in sha256_hw.cpp, Kommentare dort):
#   Block 1 laeuft  -> Block-2-Daten schreiben, bis tStart+d0 warten, CONTINUE
#   Block 2         -> Tail, Padding fuer Hash 2, d1 warten, LOAD, l1, START
#   Hash 2          -> Tail, T[8..15] fuer Block 1 zuruecksetzen, d2, LOAD, l2
#   Schleifenende   -> H7 lesen, T[0..7] = Block 1, START der naechsten Nonce,
#                      danach (waehrend Block 1 rechnet) Kandidatenpruefung
#
# Unterschiede zur C-Version:
#   - Block 1, Woerter 0..7 liegen dauerhaft in a8..a15 (Block 1 ist je Job konstant):
#     Das Zurueckschreiben am Schleifenende braucht keine Speicherzugriffe mehr.
#   - Block 1 wartet taktgenau: Restzeit = Ziel - CCOUNT, dann Zaehlschleife,
#     statt CCOUNT in einer Schleife abzufragen (Raster ~5 Takte).
#   - Wartewerte werden vor dem Steuerbefehl geladen, nicht danach.
#
# Register in der Schleife:
#   a2  = SHA_TEXT_BASE (0x3FF03000)       a3, a6 = frei (Zwischenwerte)
#   a4  = aktuelle Nonce                    a7     = 1 (Wert fuer Steuerbefehle)
#   a5  = verbleibende Nonces               a8..a15 = Block 1, Woerter 0..7
#
# Spalten:  Marke | Befehl | Operanden | Kommentar

            .set     T_START,  0x90         # SHA_256_START  (Offset zu SHA_TEXT_BASE)
            .set     T_CONT,   0x94         # SHA_256_CONTINUE
            .set     T_LOAD,   0x98         # SHA_256_LOAD
            .set     TAIL,     32           # Takte, bis T[8..15] ueberschrieben werden darf

# HwScanArgs (Offsets, mit static_assert in sha256_hw.cpp abgesichert)
            .set     A_B1,     0
            .set     A_B2,     4
            .set     A_NONCE,  8
            .set     A_COUNT,  12
            .set     A_MASK,   16
            .set     A_CANDS,  20
            .set     A_MAX,    24
            .set     A_FOUND,  28
            .set     A_D0,     32
            .set     A_D1,     36
            .set     A_D2,     40
            .set     A_L1,     44
            .set     A_L2,     48
            .set     A_GUARD,  52

# Stackrahmen (Kopien der Argumente, ein Ladebefehl statt zwei)
            .set     F_CTX,    0
            .set     F_B1,     4
            .set     F_B2,     8
            .set     F_MASK,   12
            .set     F_CANDS,  16
            .set     F_MAX,    20
            .set     F_FOUND,  24
            .set     F_TS,     28           # tStart: CCOUNT beim START von Block 1
            .set     F_D0,     32
            .set     F_D1,     36
            .set     F_D2,     40
            .set     F_L1,     44
            .set     F_L2,     48
            .set     F_GUARD,  52

            .section .iram1.hw_scan_asm, "ax"
            .global  hw_scan_asm
            .type    hw_scan_asm, @function
            .align   4

hw_scan_asm:
            entry    a1, 96                 # a2 = HwScanArgs*

# --- Prolog: Argumente in den Rahmen kopieren -------------------------------
            s32i     a2, a1, F_CTX
            l32i     a3, a2, A_B1
            s32i     a3, a1, F_B1
            l32i     a6, a2, A_B2
            s32i     a6, a1, F_B2
            l32i     a6, a2, A_MASK
            s32i     a6, a1, F_MASK
            l32i     a6, a2, A_CANDS
            s32i     a6, a1, F_CANDS
            l32i     a6, a2, A_MAX
            s32i     a6, a1, F_MAX
            l32i     a6, a2, A_D0
            s32i     a6, a1, F_D0
            l32i     a6, a2, A_D1
            s32i     a6, a1, F_D1
            l32i     a6, a2, A_D2
            s32i     a6, a1, F_D2
            l32i     a6, a2, A_L1
            s32i     a6, a1, F_L1
            l32i     a6, a2, A_L2
            s32i     a6, a1, F_L2
            l32i     a6, a2, A_GUARD
            s32i     a6, a1, F_GUARD
            movi.n   a6, 0
            s32i     a6, a1, F_FOUND
            l32i     a4, a2, A_NONCE
            l32i     a5, a2, A_COUNT
            beqz     a5, .Ldone             # nichts zu tun

            l32i     a8,  a3, 0             # Block 1, Woerter 0..7 -> Register
            l32i     a9,  a3, 4
            l32i     a10, a3, 8
            l32i     a11, a3, 12
            l32i     a12, a3, 16
            l32i     a13, a3, 20
            l32i     a14, a3, 24
            l32i     a15, a3, 28

            movi     a2, 0x3FF              # a2 = 0x3FF03000 (SHA_TEXT_BASE)
            slli     a2, a2, 20
            addmi    a2, a2, 0x3000
            movi.n   a7, 1

# --- Pipeline-Start: Block 1 der ersten Nonce ------------------------------
            l32i     a6, a3, 32             # T[8..15] = Block 1, Woerter 8..15
            s32i     a6, a2, 32
            l32i     a6, a3, 36
            s32i     a6, a2, 36
            l32i     a6, a3, 40
            s32i     a6, a2, 40
            l32i     a6, a3, 44
            s32i     a6, a2, 44
            l32i     a6, a3, 48
            s32i     a6, a2, 48
            l32i     a6, a3, 52
            s32i     a6, a2, 52
            l32i     a6, a3, 56
            s32i     a6, a2, 56
            l32i     a6, a3, 60
            s32i     a6, a2, 60
            s32i     a8,  a2, 0             # T[0..7] = Block 1, Woerter 0..7
            s32i     a9,  a2, 4
            s32i     a10, a2, 8
            s32i     a11, a2, 12
            s32i     a12, a2, 16
            s32i     a13, a2, 20
            s32i     a14, a2, 24
            s32i     a15, a2, 28
            s32i     a7,  a2, T_START       # START Block 1
            rsr      a3, ccount
            s32i     a3, a1, F_TS
            movi     a6, 8                  # Schutzabstand: Engine liest T[0..7] erst nach START
            loopnez  a6, 1f
            nop.n
1:

# === Schleife: Block 1 der Nonce a4 laeuft ==================================
.Lloop:
            l32i     a6, a1, F_GUARD        # optionaler Abstand START -> erster Write
            loopnez  a6, 1f
            nop.n
1:
# --- Block 2 schreiben (Header-Bytes 64..79 + Padding, 640 Bit) -------------
            l32i     a3, a1, F_B2
            l32i     a6, a3, 0
            s32i     a6, a2, 0              # T[0] = Merkle-Ende
            l32i     a6, a3, 4
            s32i     a6, a2, 4              # T[1] = Zeit
            l32i     a6, a3, 8
            s32i     a6, a2, 8              # T[2] = Bits
            ssai     8                      # T[3] = Nonce, Bytes getauscht
            extui    a6, a4, 16, 16
            src      a6, a6, a4
            src      a6, a6, a6
            src      a6, a4, a6
            s32i     a6, a2, 12
            movi.n   a6, 1                  # T[4] = 0x80000000 (Padding)
            slli     a6, a6, 31
            s32i     a6, a2, 16
            movi.n   a6, 0                  # T[5..14] = 0
            s32i     a6, a2, 20
            s32i     a6, a2, 24
            s32i     a6, a2, 28
            s32i     a6, a2, 32
            s32i     a6, a2, 36
            s32i     a6, a2, 40
            s32i     a6, a2, 44
            s32i     a6, a2, 48
            s32i     a6, a2, 52
            s32i     a6, a2, 56
            movi     a6, 640                # T[15] = Laenge 640 Bit
            s32i     a6, a2, 60

# --- bis tStart + d0 warten (taktgenau), dann CONTINUE ----------------------
            l32i     a3, a1, F_TS
            l32i     a6, a1, F_D0
            add      a3, a3, a6             # Zielzeitpunkt
            rsr      a6, ccount
            sub      a6, a3, a6             # Resttakte (<= 0: schon erreicht)
            loopgtz  a6, 1f
            nop.n
1:          s32i     a7, a2, T_CONT         # CONTINUE: Block 2 rechnet

# --- Block 2 rechnet: Padding fuer Hash 2 vorbereiten -----------------------
            movi     a6, TAIL
            loopnez  a6, 1f
            nop.n
1:          movi.n   a6, 1                  # T[8] = 0x80000000
            slli     a6, a6, 31
            s32i     a6, a2, 32
            movi     a6, 256                # T[15] = Laenge 256 Bit (T[9..14] sind 0)
            s32i     a6, a2, 60
            l32i     a3, a1, F_L1           # Wartewert fuer nach dem LOAD schon jetzt laden
            l32i     a6, a1, F_D1
            loopnez  a6, 1f
            nop.n
1:          s32i     a7, a2, T_LOAD         # LOAD: Hash-1-Digest -> T[0..7]
            loopnez  a3, 1f
            nop.n
1:          s32i     a7, a2, T_START        # START: Hash 2 rechnet

# --- Hash 2 rechnet: T[8..15] fuer Block 1 der naechsten Nonce --------------
            movi     a6, TAIL
            loopnez  a6, 1f
            nop.n
1:          l32i     a3, a1, F_B1
            l32i     a6, a3, 32
            s32i     a6, a2, 32
            l32i     a6, a3, 36
            s32i     a6, a2, 36
            l32i     a6, a3, 40
            s32i     a6, a2, 40
            l32i     a6, a3, 44
            s32i     a6, a2, 44
            l32i     a6, a3, 48
            s32i     a6, a2, 48
            l32i     a6, a3, 52
            s32i     a6, a2, 52
            l32i     a6, a3, 56
            s32i     a6, a2, 56
            l32i     a6, a3, 60
            s32i     a6, a2, 60
            l32i     a3, a1, F_L2           # Wartewert fuer nach dem LOAD schon jetzt laden
            l32i     a6, a1, F_D2
            loopnez  a6, 1f
            nop.n
1:          s32i     a7, a2, T_LOAD         # LOAD: Hash-2-Ergebnis -> T[0..7]
            loopnez  a3, 1f
            nop.n

# --- Schleifenende: kritischer Pfad, die Engine steht hier still ------------
1:          memw                            # LOAD muss angekommen sein
            l32i     a6, a2, 28             # a6 = H7
            s32i     a8,  a2, 0             # T[0..7] = Block 1 aus Registern
            s32i     a9,  a2, 4
            s32i     a10, a2, 8
            s32i     a11, a2, 12
            s32i     a12, a2, 16
            s32i     a13, a2, 20
            s32i     a14, a2, 24
            s32i     a15, a2, 28
            s32i     a7,  a2, T_START       # START Block 1 der naechsten Nonce
            rsr      a3, ccount
            s32i     a3, a1, F_TS

# --- waehrenddessen: Kandidat? (H7 & mask) == 0 ------------------------------
            l32i     a3, a1, F_MASK
            and      a3, a3, a6
            bnez     a3, .Lnext
            l32i     a3, a1, F_FOUND
            l32i     a6, a1, F_MAX
            bge      a3, a6, .Lnext         # Kandidatenliste voll
            l32i     a6, a1, F_CANDS
            addx4    a6, a3, a6
            s32i     a4, a6, 0              # cands[found] = Nonce
            addi.n   a3, a3, 1
            s32i     a3, a1, F_FOUND
.Lnext:
            addi.n   a4, a4, 1
            addi.n   a5, a5, -1
            bnez     a5, .Lloop

# --- Ende: Kandidatenzahl zurueckgeben ---------------------------------------
.Ldone:
            l32i     a3, a1, F_CTX
            l32i     a6, a1, F_FOUND
            s32i     a6, a3, A_FOUND
            l32i     a2, a3, A_COUNT        # Rueckgabe: Anzahl gerechneter Nonces
            retw.n

            .size    hw_scan_asm, . - hw_scan_asm
