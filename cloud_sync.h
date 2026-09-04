#pragma once
#include <stdint.h>
#include <stddef.h>
#include "storage.h"

// =====================================================================
// CLOUD_SYNC.H — Espelhamento opcional dos resultados para o Módulo
// Nuvem (Fase 18)
//
// O SD continua sendo a ÚNICA fonte de verdade (ver storage.h) — isto
// aqui só tenta mandar uma CÓPIA de cada resultado já salvo para a
// nuvem, quando o modo "Online" está habilitado e configurado. Qualquer
// falha (Wi-Fi fora, nuvem fora do ar, timeout) só é logada — o
// resultado já está seguro no SD de qualquer forma (falha segura, ver
// Fase 1 seção 19).
//
// Sem task/núcleo dedicado: como o Objetivo 2 (segundo núcleo) está
// pausado (ver docs/fase13-notas-bancada.md, seção 8), a tentativa de
// sincronização roda dentro do próprio loop() principal, no ÚNICO
// instante em que isso é seguro — uma única vez, logo após salvar o
// resultado no SD, durante a janela de exibição do resultado final
// (RESULT_DISPLAY_HOLD_US, ver o .ino), quando nenhuma corrida nova
// pode começar (raceReleased só volta a true depois que o juiz arma e
// libera a próxima). O timeout HTTP é propositalmente curto para nunca
// segurar essa janela por muito mais tempo do que ela já dura sozinha.
//
// Sem fila de reenvio nesta primeira versão: se a tentativa falhar
// (ex.: internet caiu bem naquele instante), aquele resultado
// específico só fica no SD e não é reenviado automaticamente depois —
// ver docs/fase18-notas-bancada.md, pendência formal para uma v2.
// =====================================================================

struct CloudSyncConfig {
  // Sem checkbox "habilitar" separado (Fase 19, adendo) — o modo Online
  // fica ativo sempre que baseUrl E deviceToken estiverem preenchidos;
  // Offline é simplesmente deixar os dois em branco. Ver configIsUsable()
  // em cloud_sync.cpp.
  char baseUrl[80];        // ex.: "http://192.168.15.23:8001" (sem barra no final)
  // Token único do dispositivo (Fase 19) — o mesmo ADMIN_TOKEN da nuvem,
  // reaproveitado como "senha" que autoriza este ESP32 a sincronizar.
  // Substitui o par Competition ID + Access Token por competição da
  // Fase 18: agora QUALQUER competição local sincroniza automaticamente
  // (a nuvem cria/encontra a competição correspondente pelo slug, ver
  // cloudSyncTryNow) — sem precisar configurar nada por competição.
  char deviceToken[64];
};

// Carrega a configuração salva na NVS (Preferences) — chamar uma vez no
// setup(), depois de storageInit()/dataStoreInit() (não depende deles,
// mas segue a mesma convenção de inicializar módulos de dados no boot).
void cloudSyncInit();

CloudSyncConfig cloudSyncGetConfig();

// Salva a nova configuração (NVS) — chamado pelo handler HTTP local
// POST /api/v1/cloud-config (tela de Configurações da página do juiz).
void cloudSyncSetConfig(const CloudSyncConfig &cfg);

// Confirma que o token do dispositivo é aceito pela nuvem, chamando
// GET /api/v1/device/ping (timeout curto). Só é chamado sob demanda
// (botão "Testar conexão" da página local) — nunca automaticamente.
// Preenche outMsg com o motivo em caso de falha.
bool cloudSyncTestAuth(char* outMsg, size_t outMsgSize);

// Tenta sincronizar UM resultado agora (bloqueante, timeout curto) — só
// deve ser chamado no instante seguro descrito acima. No-op silencioso
// se o modo Online não estiver habilitado/configurado, o Wi-Fi (STA)
// não estiver conectado, ou a corrida não tiver contexto resolvido.
void cloudSyncTryNow(const RunRecord &record, const RunContext &ctx);

// Avisa a nuvem que o status de UMA competição local mudou (draft/
// finished), sem nenhuma corrida envolvida — usado em "avançar dia"/
// "encerrar competição" (ver handlePostAdvanceDay em web_server.cpp).
// Sem isto, uma competição que termina sem mais nenhuma corrida depois
// nunca teria outra chance de avisar a nuvem (cloudSyncTryNow só roda
// atrelado a uma corrida) — o dashboard ficaria "Em andamento" pra
// sempre mesmo já encerrada localmente. Mesmas regras de no-op
// silencioso de cloudSyncTryNow (offline, sem Wi-Fi etc.).
void cloudSyncCompetitionStatus(int competitionId);

// Fila de pendências em RAM (mesmo padrão de storage.h) — corridas que
// não sincronizaram (Wi-Fi fora, nuvem fora do ar, ou modo Online ainda
// não configurado no momento) ficam aqui até a próxima chamada de
// cloudSyncFlushPending() dar certo. Perdida em reboot (RAM), não é
// fila de produção — mas cobre o caso "esqueci de configurar/rede caiu".
#define MAX_CLOUD_PENDING 8

void cloudSyncFlushPending();
