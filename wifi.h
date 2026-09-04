#pragma once
#include <stdint.h>
#include <stddef.h>

// =====================================================================
// WIFI.H — Conectividade Wi-Fi: AP + STA simultâneo, portal NÃO-bloqueante
//
// wifiInit() ainda pode bloquear por até WIFI_STA_CONNECT_TIMEOUT_S
// (tentativa na rede salva) — mas o PORTAL de configuração (até
// WIFI_CONFIG_PORTAL_TIMEOUT_S) não bloqueia mais: se precisar abrir,
// wifiInit() retorna na hora e o portal segue rodando em background,
// bombeado por wifiPortalUpdate() a cada loop(). Isso existe pra que a
// chave de Modo Treino (ver training_mode.h) consiga abortar a espera
// por rede instantaneamente, em vez de travar o boot por até 2 minutos.
// =====================================================================

void wifiInit();

// Chamar a cada iteração do loop(). Throttled internamente — só checa
// o status STA de fato a cada WIFI_STATUS_CHECK_INTERVAL_US. Também é
// aqui que o AP próprio do sistema é (re)habilitado assim que o portal
// de configuração (se tiver aberto) terminar.
void wifiUpdate();

bool wifiIsStaConnected();

// true enquanto o portal de configuração (rede temporária pra digitar
// a senha do Wi-Fi) estiver aberto.
bool wifiPortalActive();

// Chamar a cada iteração do loop() enquanto wifiPortalActive() for true
// — processa o portal sem bloquear.
void wifiPortalUpdate();

// Segundos restantes até o portal desistir sozinho. 0 se não houver
// portal ativo.
int wifiPortalSecondsRemaining();

// Fecha o portal imediatamente (ex.: chave girada para Modo Treino —
// não faz sentido continuar esperando configuração de rede).
void wifiAbortPortal();

// Preenche 'buffer' com o IP mais relevante no momento: o do portal se
// estiver aberto, senão o STA se conectado, senão o AP próprio do
// sistema. Buffer deve ter pelo menos 24 bytes.
void wifiGetStatusString(char* buffer, size_t bufferSize);