#pragma once
#include <stdint.h>

// =====================================================================
// DISPLAY.H — Display OLED (SSD1306, I2C)
//
// Todas as funções são no-op seguro se o display não tiver sido
// encontrado no barramento I2C.
// =====================================================================

void displayInit();

// Tela ociosa, liberada pelo juiz - aguardando o robô passar pelo Sensor 1.
void displayShowReady();

// Tela ociosa, travada - juiz ainda não liberou. Nunca mostra IP (ver
// displayShowIpReveal para a única janela em que o IP aparece).
void displayShowLocked();

// Tela mostrada durante a tentativa inicial de conexão STA, antes de
// se saber se vai conectar direto ou abrir o portal.
void displayShowConnecting();

// Tela do portal de configuração Wi-Fi aberto - IP pra acessar e o
// timeout configurado (WIFI_CONFIG_PORTAL_TIMEOUT_S). Não é uma
// contagem regressiva ao vivo (o portal ainda bloqueia o loop() nesta
// versão) - fica pra quando mexermos nisso depois.
void displayShowWifiSetup(const char* portalSsid, const char* portalIp, int secondsRemaining);

// Mostrada uma única vez, por IP_REVEAL_DURATION_US, assim que o
// sistema termina de decidir sua rede (conectou ou caiu no AP próprio)
// — depois disso o IP nunca mais aparece na tela (ver displayShowLocked).
void displayShowIpReveal(const char* ip);

// Tela do Modo Treino ociosa (sem corrida em andamento).
void displayShowTrainingIdle();

// Chamada repetidamente durante RUNNING (throttled internamente). Em
// Modo Treino, chamar com robotName == nullptr: mostra só o tempo,
// bem grande, sem nome/equipe.
void displayShowRunning(int64_t elapsedUs, const char* robotName, const char* teamName);

// Tela final, chamada uma única vez ao concluir a corrida. Mesma regra
// de robotName == nullptr para o Modo Treino.
void displayShowFinished(int64_t elapsedUs, const char* robotName, const char* teamName);

void displayShowError(const char* message);