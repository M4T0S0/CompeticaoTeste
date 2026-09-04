#pragma once

// =====================================================================
// STATE_MACHINE.H
// Conjunto completo de estados do sistema (ver Fase 1). SYNCING existiu
// enquanto o resultado precisava de uma tentativa de envio HTTP separada
// da gravação no SD (versão com servidor externo) — na versão standalone
// a gravação no SD já É definitiva, então esse estado foi removido.
// =====================================================================

enum class SystemState {
  BOOT,
  INIT_HARDWARE,
  INIT_NETWORK,
  IDLE,
  ARMED,
  WAITING_START,
  RUNNING,
  FINISHED,
  SAVING,
  ERROR
};

// Estado atual do sistema. 'volatile' porque pode ser lido/observado
// em contextos que futuramente incluirão ISRs ou tasks concorrentes.
extern volatile SystemState currentState;

// Converte o estado para string legível (debug / OLED / logs).
const char* stateToString(SystemState state);

// Realiza a transição de estado, com log da transição.
// Centralizar aqui evita mudanças de estado "soltas" espalhadas pelo código.
void transitionTo(SystemState newState);
