#pragma once

// =====================================================================
// WEB_SERVER.H — API + página do juiz, hospedadas no próprio ESP32
//
// Substitui api.cpp (cliente HTTP) + network_task.cpp (polling) +
// server/main.py + server/routers/*.py. Roda dentro do loop() principal
// (ver decisão de arquitetura no plano/README) — sem task própria, sem
// mutex: é a única linha de execução que toca SD/estado de corrida.
//
// Mesma filosofia do resto do firmware: falha de rede/HTTP nunca trava
// a cronometragem — webServerHandle() só processa UMA requisição já
// enfileirada pelo TCP/IP stack por chamada e retorna, nunca bloqueia
// esperando por uma requisição que ainda não chegou.
// =====================================================================

void webServerRestart(); // reinicia o listener HTTP após mudança de interface de rede

void webServerInit();

// Chamar a cada iteração do loop() — processa no máximo uma requisição
// HTTP pendente e retorna (não bloqueia).
void webServerHandle();
