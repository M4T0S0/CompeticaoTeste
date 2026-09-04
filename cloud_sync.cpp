#include "cloud_sync.h"
#include "wifi.h"
#include "data_store.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <string.h>

static const char* PREFS_NAMESPACE = "cloudsync";

// Curto de propósito: a tentativa acontece dentro da janela de 3s de
// RESULT_DISPLAY_HOLD_US (ver cloud_sync.h) — nunca deve segurar o
// loop() por mais tempo que isso, mesmo se a nuvem estiver totalmente
// inacessível (ex.: IP errado, porta fechada).
static const uint32_t HTTP_TIMEOUT_MS = 1500;

static CloudSyncConfig g_config = { "", "" };

void cloudSyncInit() {
  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, true); // somente leitura
  memset(g_config.baseUrl, 0, sizeof(g_config.baseUrl));
  memset(g_config.deviceToken, 0, sizeof(g_config.deviceToken));
  prefs.getString("baseUrl", g_config.baseUrl, sizeof(g_config.baseUrl));
  prefs.getString("devToken", g_config.deviceToken, sizeof(g_config.deviceToken));
  prefs.end();

  Serial.print("[CLOUD] Modo Online: ");
  Serial.println((strlen(g_config.baseUrl) > 0 && strlen(g_config.deviceToken) > 0) ? "ativo" : "inativo");
}

CloudSyncConfig cloudSyncGetConfig() {
  return g_config;
}

void cloudSyncSetConfig(const CloudSyncConfig &cfg) {
  g_config = cfg;
  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putString("baseUrl", cfg.baseUrl);
  prefs.putString("devToken", cfg.deviceToken);
  prefs.end();
}

static bool configIsUsable() {
  return strlen(g_config.baseUrl) > 0 && strlen(g_config.deviceToken) > 0;
}

static void buildAuthHeader(char* buf, size_t bufSize) {
  snprintf(buf, bufSize, "Bearer %s", g_config.deviceToken);
}

bool cloudSyncTestAuth(char* outMsg, size_t outMsgSize) {
  if (!configIsUsable()) {
    strlcpy(outMsg, "Preencha a URL da nuvem e o Token do dispositivo antes de testar.", outMsgSize);
    return false;
  }
  if (!wifiIsStaConnected()) {
    strlcpy(outMsg, "Sem Wi-Fi (STA) conectado - conecte a uma rede com acesso a nuvem antes de testar.", outMsgSize);
    return false;
  }

  char url[160];
  snprintf(url, sizeof(url), "%s/api/v1/device/ping", g_config.baseUrl);

  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(url);
  char authHeader[96];
  buildAuthHeader(authHeader, sizeof(authHeader));
  http.addHeader("Authorization", authHeader);

  int code = http.GET();
  http.end();

  if (code == 200) {
    strlcpy(outMsg, "Conexao OK - token aceito.", outMsgSize);
    return true;
  }
  if (code == 401) {
    strlcpy(outMsg, "Token do dispositivo invalido (nao bate com o ADMIN_TOKEN da nuvem).", outMsgSize);
  } else if (code == 503) {
    strlcpy(outMsg, "Nuvem sem ADMIN_TOKEN configurado - painel de administracao desativado do lado dela.", outMsgSize);
  } else if (code > 0) {
    snprintf(outMsg, outMsgSize, "Nuvem respondeu com erro HTTP %d.", code);
  } else {
    snprintf(outMsg, outMsgSize, "Nao foi possivel conectar (erro %d). Confira a URL/porta.", code);
  }
  return false;
}

struct CloudPending { RunRecord record; RunContext ctx; bool used; };
static CloudPending g_cloudPending[MAX_CLOUD_PENDING];

static void cloudSyncEnqueue(const RunRecord &record, const RunContext &ctx) {
  for (int i = 0; i < MAX_CLOUD_PENDING; i++) {
    if (!g_cloudPending[i].used) {
      g_cloudPending[i] = { record, ctx, true };
      return;
    }
  }
  Serial.println("[CLOUD] Fila de pendencias cheia - resultado nao sera reenviado automaticamente.");
}

static bool cloudSyncSend(const RunRecord &record, const RunContext &ctx) {
  const Competition* comp = competitionById(ctx.competitionId);
  const char* competitionName = comp ? comp->name : ctx.competitionSlug;
  // Status local é "draft"|"finished" (data_store.h); a nuvem usa
  // "active"|"finished" pro mesmo conceito (ver database.py) - manda já
  // traduzido pra nuvem nunca precisar conhecer o vocabulário local.
  const char* competitionStatus = (comp && strcmp(comp->status, "finished") == 0) ? "finished" : "active";

  char url[160];
  snprintf(url, sizeof(url), "%s/api/v1/sync/runs", g_config.baseUrl);

  // Sem escapar aspas no nome do participante/competicao - mesma
  // convenção já usada em storage.cpp (buildRunJsonLine): assume que
  // nomes cadastrados não contêm aspas duplas.
  char body[512];
  snprintf(body, sizeof(body),
    "{\"competition_slug\":\"%s\",\"competition_name\":\"%s\",\"competition_status\":\"%s\","
    "\"event_id\":\"%s\",\"participant_name\":\"%s\",\"day\":%d,\"attempt\":%d,"
    "\"start_timestamp_us\":%lld,\"end_timestamp_us\":%lld,\"elapsed_us\":%lld,\"status\":\"%s\"}",
    ctx.competitionSlug, competitionName, competitionStatus, record.eventId,
    ctx.participantName, record.day, record.attempt,
    (long long)record.startTimestampUs, (long long)record.endTimestampUs, (long long)record.elapsedUs,
    record.status
  );

  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  char authHeader[96];
  buildAuthHeader(authHeader, sizeof(authHeader));
  http.addHeader("Authorization", authHeader);

  int code = http.POST((uint8_t*)body, strlen(body));
  http.end();

  bool ok = (code == 200 || code == 201);
  if (ok) {
    Serial.print("[CLOUD] Resultado sincronizado (");
    Serial.print(record.eventId);
    Serial.println(").");
  } else {
    Serial.print("[CLOUD] Falha ao sincronizar (");
    Serial.print(record.eventId);
    Serial.print("): HTTP ");
    Serial.println(code);
  }
  return ok;
}

void cloudSyncTryNow(const RunRecord &record, const RunContext &ctx) {
  if (!configIsUsable() || !wifiIsStaConnected() || !ctx.hasContext) {
    if (ctx.hasContext) cloudSyncEnqueue(record, ctx);
    return;
  }
  if (!cloudSyncSend(record, ctx)) cloudSyncEnqueue(record, ctx);
}

void cloudSyncFlushPending() {
  if (!configIsUsable() || !wifiIsStaConnected()) return;
  for (int i = 0; i < MAX_CLOUD_PENDING; i++) {
    if (!g_cloudPending[i].used) continue;
    if (cloudSyncSend(g_cloudPending[i].record, g_cloudPending[i].ctx)) {
      g_cloudPending[i].used = false;
    }
  }
}

void cloudSyncCompetitionStatus(int competitionId) {
  if (!configIsUsable()) return;
  if (!wifiIsStaConnected()) return;

  const Competition* comp = competitionById(competitionId);
  if (!comp) return;
  const char* competitionStatus = (strcmp(comp->status, "finished") == 0) ? "finished" : "active";

  char url[160];
  snprintf(url, sizeof(url), "%s/api/v1/sync/competition-status", g_config.baseUrl);

  char body[200];
  snprintf(body, sizeof(body),
    "{\"competition_slug\":\"%s\",\"competition_name\":\"%s\",\"competition_status\":\"%s\"}",
    comp->slug, comp->name, competitionStatus
  );

  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  char authHeader[96];
  buildAuthHeader(authHeader, sizeof(authHeader));
  http.addHeader("Authorization", authHeader);

  int code = http.POST((uint8_t*)body, strlen(body));
  http.end();

  if (code == 200 || code == 201) {
    Serial.println("[CLOUD] Status da competicao sincronizado.");
  } else {
    Serial.print("[CLOUD] Falha ao sincronizar status da competicao: HTTP ");
    Serial.println(code);
  }
}
