#pragma once
#include <stddef.h>

// =====================================================================
// DEVICE.H — Identidade do dispositivo (Fase 6)
//
// Identificador curto (6 dígitos hex, derivado do chip ID do ESP32),
// usado no event_id (ver storage.h, Fase 4) — garante que o mesmo
// evento gerado por dois dispositivos diferentes nunca colida.
// =====================================================================

void deviceGetShortId(char* buffer, size_t bufferSize);
