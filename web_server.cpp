#include "web_server.h"
#include "web_page.h"
#include "config.h"
#include "data_store.h"
#include "race_control.h"
#include "storage.h"
#include "ranking.h"
#include "data_version.h"
#include "cloud_sync.h"
#include "peripherals.h"
#include "timer.h"
#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <string.h>

static WebServer server(HTTP_SERVER_PORT);

// ---------------------------------------------------------------------
// Autenticação da página do juiz (Fase 23) — protege a página local
// inteira (HTML + API) contra qualquer um que descubra o IP do ESP32 ou
// entre no Access Point próprio dele e tente mexer numa corrida alheia.
// Usuário fixo ("juiz"), só a senha é configurável — HTTP Basic Auth,
// suporte nativo da biblioteca WebServer. Sem senha configurada (padrão
// de fábrica): sem exigência nenhuma, mesmo comportamento de sempre —
// nunca tranca o próprio dono fora por engano.
//
// Limitação conhecida e aceita: Basic Auth manda a senha em Base64 (não
// criptografado) sobre HTTP puro — quem estiver "farejando" o tráfego da
// rede consegue ler. Isso barra o cenário real (alguém oportunista
// encontrar a página e mexer sem estar autorizado), não um atacante
// sofisticado analisando pacotes — proporcional ao contexto de uma
// competição, não um sistema bancário (mesma lógica já usada para o
// CORS/ADMIN_TOKEN da nuvem, ver cloud/api/auth.py).
// ---------------------------------------------------------------------

static const char* JUDGE_AUTH_USERNAME = "juiz";
static char g_judgePassword[32] = "";

static void judgeAuthInit() {
  Preferences prefs;
  prefs.begin("judgeauth", true);
  prefs.getString("password", g_judgePassword, sizeof(g_judgePassword));
  prefs.end();
}

static void judgeAuthSetPassword(const char* password) {
  strlcpy(g_judgePassword, password, sizeof(g_judgePassword));
  Preferences prefs;
  prefs.begin("judgeauth", false);
  prefs.putString("password", g_judgePassword);
  prefs.end();
}

// Chamado no topo de handleRoot()/handleApi() - retorna false (e já
// manda o 401 pedindo login) se uma senha estiver configurada e as
// credenciais não baterem. true = pode continuar processando a rota.
static bool checkJudgeAuth() {
  if (strlen(g_judgePassword) == 0) return true; // sem senha configurada - comportamento de sempre
  if (server.authenticate(JUDGE_AUTH_USERNAME, g_judgePassword)) return true;
  server.requestAuthentication();
  return false;
}

// ---------------------------------------------------------------------
// Helpers de resposta
// ---------------------------------------------------------------------

static void sendJsonDoc(int code, JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

static void sendError(int code, const char* detail) {
  JsonDocument doc;
  doc["detail"] = detail;
  sendJsonDoc(code, doc);
}

static void sendNoContent() {
  server.send(204, "application/json", "");
}

// Quebra o path em segmentos, sem regex — mesmo espírito de parsing
// manual já usado no resto do firmware (ver json_utils.cpp): o
// WebServer nativo do core ESP32 não tem roteamento com parâmetros
// (":id"), então isso substitui a necessidade de uma biblioteca de
// regex extra só para separar "/api/v1/competitions/3/participants".
static int splitPath(const String &uri, String* outSegs, int maxSegs) {
  int count = 0;
  int start = (uri.length() > 0 && uri[0] == '/') ? 1 : 0;
  while (start <= (int)uri.length() && count < maxSegs) {
    int slash = uri.indexOf('/', start);
    String seg = (slash < 0) ? uri.substring(start) : uri.substring(start, slash);
    if (seg.length() > 0) outSegs[count++] = seg;
    if (slash < 0) break;
    start = slash + 1;
  }
  return count;
}

static bool readJsonBody(JsonDocument &doc) {
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  return !err;
}

// ---------------------------------------------------------------------
// Cache de respostas caras (ranking / day-status / lista de corridas —
// as únicas rotas que varrem o cartão SD, ver ranking.cpp/data_store.cpp
// /storage.cpp). A página do juiz consulta essas rotas a cada ~1.5s
// mesmo quando nada mudou; sem isso, o loop() principal (que também
// cuida dos sensores e do display) ficava perceptivelmente lento
// reabrindo/relendo arquivos do SD a cada poll. Cada cache guarda a
// última resposta JÁ SERIALIZADA para a última competição consultada,
// junto da versão dos dados (ver data_version.h) em que foi calculada —
// só recalcula quando algo relevante realmente mudou.
// ---------------------------------------------------------------------

struct JsonCache {
  int competitionId = -1;
  uint32_t generation = 0xFFFFFFFF;
  String json;
};

static bool cacheServe(JsonCache &cache, int competitionId) {
  if (cache.competitionId != competitionId || cache.generation != dataVersionCurrent()) {
    return false;
  }
  server.send(200, "application/json", cache.json);
  return true;
}

static void cacheFill(JsonCache &cache, int competitionId, JsonDocument &doc) {
  cache.competitionId = competitionId;
  cache.generation = dataVersionCurrent();
  serializeJson(doc, cache.json);
  server.send(200, "application/json", cache.json);
}

// ---------------------------------------------------------------------
// Participantes
// ---------------------------------------------------------------------

static void writeParticipantJson(JsonObject o, const Participant* p) {
  o["id"] = p->id;
  o["name"] = p->name;
  o["external_code"] = p->externalCode;
  o["team_id"] = p->teamId;
}

static void handleGetParticipants() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  int n = participantCount();
  for (int i = 0; i < n; i++) {
    writeParticipantJson(arr.add<JsonObject>(), participantAt(i));
  }
  sendJsonDoc(200, doc);
}

static void handlePostParticipant() {
  JsonDocument in;
  readJsonBody(in);
  if (!in["name"].is<const char*>() || strlen((const char*)in["name"]) == 0) {
    sendError(400, "name e obrigatorio");
    return;
  }
  if (!in["team_id"].is<int>() || !teamById((int)in["team_id"])) {
    sendError(400, "team_id invalido ou nao informado");
    return;
  }
  const char* name = in["name"];
  const char* code = in["external_code"] | "";
  int teamId = in["team_id"];

  int id = participantCreate(name, code, teamId);
  if (id == -2) {
    sendError(409, "Ja existe um robo com esse nome nesta equipe");
    return;
  }
  if (id < 0) {
    sendError(400, "Capacidade de participantes atingida");
    return;
  }
  JsonDocument out;
  writeParticipantJson(out.to<JsonObject>(), participantById(id));
  sendJsonDoc(201, out);
}

static void writeTeamJson(JsonObject o, const Team* t) {
  o["id"] = t->id;
  o["name"] = t->name;
}

static void handleGetTeams() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  int n = teamCount();
  for (int i = 0; i < n; i++) {
    writeTeamJson(arr.add<JsonObject>(), teamAt(i));
  }
  sendJsonDoc(200, doc);
}

static void handlePostTeam() {
  JsonDocument in;
  readJsonBody(in);
  if (!in["name"].is<const char*>() || strlen((const char*)in["name"]) == 0) {
    sendError(400, "name e obrigatorio");
    return;
  }
  int id = teamCreate((const char*)in["name"]);
  if (id == -2) {
    sendError(409, "Ja existe uma equipe com esse nome");
    return;
  }
  if (id < 0) {
    sendError(400, "Capacidade de equipes atingida");
    return;
  }
  JsonDocument out;
  writeTeamJson(out.to<JsonObject>(), teamById(id));
  sendJsonDoc(201, out);
}

static void handleDeleteParticipant(int id) {
  if (!participantDelete(id)) {
    sendError(404, "Participante nao encontrado");
    return;
  }
  sendNoContent();
}

// ---------------------------------------------------------------------
// Competições
// ---------------------------------------------------------------------

static void writeCompetitionJson(JsonObject o, const Competition* c) {
  o["id"] = c->id;
  o["name"] = c->name;
  o["num_days"] = c->numDays;
  o["attempts_per_day"] = c->attemptsPerDay;
  o["status"] = c->status;
  o["current_day"] = c->currentDay;
}

static void handleGetCompetitions() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  int n = competitionCount();
  for (int i = 0; i < n; i++) {
    writeCompetitionJson(arr.add<JsonObject>(), competitionAt(i));
  }
  sendJsonDoc(200, doc);
}

static void handlePostCompetition() {
  JsonDocument in;
  readJsonBody(in);
  if (!in["name"].is<const char*>() || strlen((const char*)in["name"]) == 0) {
    sendError(400, "name e obrigatorio");
    return;
  }
  const char* name = in["name"];
  int numDays = in["num_days"] | 1;
  int attemptsPerDay = in["attempts_per_day"] | 1;

  int id = competitionCreate(name, numDays, attemptsPerDay);
  if (id < 0) {
    sendError(400, "Capacidade de competicoes atingida");
    return;
  }
  JsonDocument out;
  writeCompetitionJson(out.to<JsonObject>(), competitionById(id));
  sendJsonDoc(201, out);
}

static void handleGetCompetition(int id) {
  const Competition* c = competitionById(id);
  if (!c) {
    sendError(404, "Competicao nao encontrada");
    return;
  }
  JsonDocument out;
  writeCompetitionJson(out.to<JsonObject>(), c);
  sendJsonDoc(200, out);
}


static void handlePatchCompetition(int id) {
  JsonDocument in;
  readJsonBody(in);
  int numDays = in["num_days"] | 1;
  int attemptsPerDay = in["attempts_per_day"] | 1;

  int result = competitionUpdate(id, numDays, attemptsPerDay);
  if (result == -1) {
    sendError(404, "Competicao nao encontrada");
    return;
  }
  if (result == -2) {
    sendError(409, "Esta competicao ja tem corridas registradas, nao e possivel editar dias/tomadas");
    return;
  }
  JsonDocument out;
  writeCompetitionJson(out.to<JsonObject>(), competitionById(id));
  sendJsonDoc(200, out);
}

static void handleGetCompetitionParticipants(int competitionId) {
  if (!competitionById(competitionId)) {
    sendError(404, "Competicao nao encontrada");
    return;
  }
  int ids[MAX_PARTICIPANTS];
  int n = linkedParticipantIds(competitionId, ids, MAX_PARTICIPANTS);

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    const Participant* p = participantById(ids[i]);
    if (!p) continue;
    writeParticipantJson(arr.add<JsonObject>(), p);
  }
  sendJsonDoc(200, doc);
}

static void handlePostCompetitionParticipant(int competitionId) {
  JsonDocument in;
  readJsonBody(in);
  if (!in["participant_id"].is<int>()) {
    sendError(400, "participant_id e obrigatorio");
    return;
  }
  int participantId = in["participant_id"];

  if (!competitionById(competitionId)) {
    sendError(404, "Competicao nao encontrada");
    return;
  }
  if (!participantById(participantId)) {
    sendError(404, "Participante nao encontrado");
    return;
  }

  bool already = linkExists(competitionId, participantId);
  linkCreate(competitionId, participantId);

  JsonDocument out;
  out["competition_id"] = competitionId;
  out["participant_id"] = participantId;
  out["already_linked"] = already;
  sendJsonDoc(201, out);
}

static void handleDeleteCompetitionParticipant(int competitionId, int participantId) {
  if (participantHasRunsInCompetition(competitionId, participantId)) {
    sendError(409, "Robo ja possui voltas registradas nesta competicao, nao pode ser removido");
    return;
  }
  linkDelete(competitionId, participantId);
  sendNoContent();
}

static JsonCache g_dayStatusCache;

static void handleGetDayStatus(int competitionId) {
  const Competition* c = competitionById(competitionId);
  if (!c) {
    sendError(404, "Competicao nao encontrada");
    return;
  }
  if (cacheServe(g_dayStatusCache, competitionId)) return;

  static DayStatusParticipant rows[MAX_PARTICIPANTS];
  bool dayComplete = false;
  bool isLastDay = false;
  int n = competitionDayStatus(competitionId, rows, MAX_PARTICIPANTS, dayComplete, isLastDay);

  JsonDocument doc;
  doc["current_day"] = c->currentDay;
  doc["num_days"] = c->numDays;
  doc["attempts_per_day"] = c->attemptsPerDay;
  doc["day_complete"] = dayComplete;
  doc["is_last_day"] = isLastDay;
  JsonArray parts = doc["participants"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject o = parts.add<JsonObject>();
    o["participant_id"] = rows[i].participantId;
    o["name"] = rows[i].name;
    o["attempts_done"] = rows[i].attemptsDone;
    o["attempts_per_day"] = c->attemptsPerDay;
    o["done"] = rows[i].done;
    const Participant* p = participantById(rows[i].participantId);
    const Team* t = p ? teamById(p->teamId) : nullptr;
    o["team_name"] = t ? t->name : "";
  }
  cacheFill(g_dayStatusCache, competitionId, doc);
}

static void handlePostAdvanceDay(int competitionId) {
  bool finished = false;
  int currentDay = -1;
  if (!competitionAdvanceDay(competitionId, finished, currentDay)) {
    sendError(404, "Competicao nao encontrada");
    return;
  }

  // Avisa a nuvem do novo status (Fase 19, adendo 2) - sem isto, uma
  // competição que termina aqui (sem mais nenhuma corrida depois pra
  // "carregar" o aviso via cloudSyncTryNow) nunca teria outra chance de
  // avisar - o dashboard ficaria "Em andamento" pra sempre.
  cloudSyncCompetitionStatus(competitionId);

  JsonDocument doc;
  doc["finished"] = finished;
  doc["current_day"] = currentDay;
  sendJsonDoc(200, doc);
}

static JsonCache g_rankingCache;

static void handleGetRanking(int competitionId) {
  if (!competitionById(competitionId)) {
    sendError(404, "Competicao nao encontrada");
    return;
  }
  if (cacheServe(g_rankingCache, competitionId)) return;

  static RankingRow rows[MAX_RANKING_ROWS];
  int n = computeRanking(competitionId, rows, MAX_RANKING_ROWS);

  JsonDocument doc;
  doc["competition_id"] = competitionId;
  doc["criteria"] = "best_time";
  JsonArray arr = doc["ranking"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["position"] = rows[i].position;
    o["participant_id"] = rows[i].participantId;
    o["participant_name"] = rows[i].participantName;
    o["external_code"] = rows[i].externalCode;
    o["best_elapsed_us"] = rows[i].bestElapsedUs;
    const Participant* pp = participantById(rows[i].participantId);
    const Team* tt = pp ? teamById(pp->teamId) : nullptr;
    o["team_name"] = tt ? tt->name : "";
    JsonArray attempts = o["attempts"].to<JsonArray>();
    for (int j = 0; j < rows[i].attemptCount; j++) {
      const AttemptCell &c = rows[i].attempts[j];
      if (!c.present) { attempts.add(nullptr); continue; }
      JsonObject a = attempts.add<JsonObject>();
      a["day"] = c.day;
      a["attempt"] = c.attempt;
      a["elapsed_us"] = c.elapsedUs;
      switch (c.statusCode) {
        case 'V': a["status"] = "valid"; break;
        case 'I': a["status"] = "invalid"; break;
        case 'D': a["status"] = "dnf"; break;
        case 'P': a["status"] = "pending_validation"; break;
        default:  a["status"] = "unknown"; break;
      }
      a["is_best"] = c.isBest;
    }
  }
  cacheFill(g_rankingCache, competitionId, doc);
}

// runId opaco (não é um id de banco — não existe mais banco): codifica
// competição/dia/linha-no-jsonl-do-dia para permitir localizar de volta
// a corrida exata em PATCH .../validation sem precisar de um índice
// separado. Ver storage.h (StoredRun) e handlePatchRunValidation abaixo.
static int encodeRunId(int competitionId, int day, int lineIndex) {
  return competitionId * 1000000 + day * 10000 + lineIndex;
}

static JsonCache g_runsCache;

static void handleGetRuns(int competitionId) {
  const Competition* c = competitionById(competitionId);
  if (!c) {
    sendError(404, "Competicao nao encontrada");
    return;
  }
  if (cacheServe(g_runsCache, competitionId)) return;

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  StoredRun* rows = storageDayRunsScratch(); // buffer compartilhado (ver storage.h) — cada dia é totalmente convertido para JSON antes do próximo
  int totalEmitted = 0;
  for (int day = 1; day <= c->numDays && totalEmitted < MAX_RUNS_LIST_TOTAL; day++) {
    int n = storageReadDayRuns(c->slug, day, rows, MAX_RUNS_PER_DAY);
    for (int i = 0; i < n && totalEmitted < MAX_RUNS_LIST_TOTAL; i++) {
      const Participant* p = participantById(rows[i].participantId);
      JsonObject o = arr.add<JsonObject>();
      o["id"] = encodeRunId(competitionId, day, i);
      o["event_id"] = rows[i].eventId;
      o["competition_id"] = rows[i].competitionId;
      o["participant_id"] = rows[i].participantId;
      o["participant_name"] = p ? p->name : "?";
      o["day"] = rows[i].day;
      o["attempt"] = rows[i].attempt;
      o["start_timestamp_us"] = rows[i].startTimestampUs;
      o["end_timestamp_us"] = rows[i].endTimestampUs;
      o["elapsed_us"] = rows[i].elapsedUs;
      o["status"] = rows[i].status;
      totalEmitted++;
    }
  }

  // Mescla corridas presas na fila de pendências (SD indisponível no
  // momento, ver Fase 19) - sem isso o juiz nunca veria essa corrida pra
  // validar. Id negativo: nunca colide com encodeRunId() (sempre >= 0).
  for (int i = 0; i < MAX_PENDING_RUNS && totalEmitted < MAX_RUNS_LIST_TOTAL; i++) {
    RunRecord* rec = storagePendingRecordAt(i);
    RunContext* pctx = storagePendingContextAt(i);
    if (!rec || !pctx || pctx->competitionId != competitionId) continue;

    const Participant* p = participantById(pctx->participantId);
    JsonObject o = arr.add<JsonObject>();
    o["id"] = -(i + 1);
    o["event_id"] = rec->eventId;
    o["competition_id"] = pctx->competitionId;
    o["participant_id"] = pctx->participantId;
    o["participant_name"] = p ? p->name : pctx->participantName;
    o["day"] = pctx->day;
    o["attempt"] = pctx->attempt;
    o["start_timestamp_us"] = rec->startTimestampUs;
    o["end_timestamp_us"] = rec->endTimestampUs;
    o["elapsed_us"] = rec->elapsedUs;
    o["status"] = rec->status;
    totalEmitted++;
  }

  cacheFill(g_runsCache, competitionId, doc);
}

static void handlePatchPendingValidation(int pendingIndex) {
  RunRecord* record = storagePendingRecordAt(pendingIndex);
  RunContext* ctx = storagePendingContextAt(pendingIndex);
  if (!record || !ctx) {
    sendError(404, "Corrida nao encontrada");
    return;
  }

  JsonDocument in;
  readJsonBody(in);
  const char* status = in["status"] | "";
  bool isRetry = (strcmp(status, "retry") == 0);
  bool validStatus = (strcmp(status, "valid") == 0 || strcmp(status, "invalid") == 0 || strcmp(status, "dnf") == 0 || isRetry);
  if (!validStatus) {
    sendError(400, "status deve ser 'valid', 'invalid', 'dnf' ou 'retry'");
    return;
  }
  const char* storedStatus = isRetry ? "retried" : status;

  storagePendingSetStatus(pendingIndex, status);

  // Copia os campos ANTES do flush — se persistir com sucesso agora, a
  // fila libera esta posição (ver storageFlushPending()) e os ponteiros
  // 'record'/'ctx' deixam de ser válidos.
  int respParticipantId = ctx->participantId;
  int respCompetitionId = ctx->competitionId;
  int respDay = ctx->day;
  int respAttempt = record->attempt;
  int64_t respElapsedUs = record->elapsedUs;
  char respEventId[48];
  strlcpy(respEventId, record->eventId, sizeof(respEventId));
  char respParticipantName[40];
  strlcpy(respParticipantName, ctx->participantName, sizeof(respParticipantName));

  if (isRetry) {
    // Mesma regra do caminho normal (handlePatchRunValidation): rearma
    // a MESMA tentativa e para aqui - NUNCA avança pra próxima (esse
    // era o bug: antes desta correção, retry sempre caía direto no
    // maybeAdvanceAttempt() abaixo, que arma a próxima tomada em vez de
    // refazer esta).
    ledSetRed(false);
    raceControlArm(respCompetitionId, respParticipantId, respDay, respAttempt);
    JsonDocument out;
    out["id"] = -(pendingIndex + 1);
    out["status"] = "retried";
    out["rearmed"] = true;
    sendJsonDoc(200, out);
    return;
  }

  // Reenvia pra nuvem com o veredito final (Fase 19, adendo) - mesmo
  // motivo do caminho normal (handlePatchRunValidation) - ANTES do
  // flush, enquanto 'record'/'ctx' ainda apontam pra esta posição.
  cloudSyncTryNow(*record, *ctx);

  storageFlushPending(); // melhor esforço - persiste de vez se o SD já voltou

  // Tomada consumida (independente do veredito) -> arma a próxima
  // automaticamente, se houver (mesma regra do caminho normal, ver
  // handlePatchRunValidation abaixo).
  maybeAdvanceAttempt(respCompetitionId, respParticipantId, respDay);

  JsonDocument out;
  out["id"] = -(pendingIndex + 1);
  out["event_id"] = respEventId;
  out["competition_id"] = respCompetitionId;
  out["participant_id"] = respParticipantId;
  out["participant_name"] = respParticipantName;
  out["day"] = respDay;
  out["attempt"] = respAttempt;
  out["elapsed_us"] = respElapsedUs;
  out["status"] = status;
  sendJsonDoc(200, out);
}

static void handlePatchRunValidation(int runId) {
  // Id negativo = corrida presa na fila de pendências em RAM, ainda não
  // persistida no SD (ver handleGetRuns/Fase 19) - caminho totalmente
  // separado, não decodifica pelo esquema de encodeRunId() abaixo.
  if (runId < 0) {
    handlePatchPendingValidation(-runId - 1);
    return;
  }

  int competitionId = runId / 1000000;
  int rem = runId % 1000000;
  int day = rem / 10000;
  int lineIndex = rem % 10000;

  const Competition* c = competitionById(competitionId);
  if (!c) {
    sendError(404, "Corrida nao encontrada");
    return;
  }

  JsonDocument in;
  readJsonBody(in);
  const char* status = in["status"] | "";
  bool isRetry = (strcmp(status, "retry") == 0);
  bool validStatus = (strcmp(status, "valid") == 0 || strcmp(status, "invalid") == 0 || strcmp(status, "dnf") == 0 || isRetry);
  if (!validStatus) {
    sendError(400, "status deve ser 'valid', 'invalid', 'dnf' ou 'retry'");
    return;
  }
  const char* storedStatus = isRetry ? "retried" : status;

  StoredRun* rows = storageDayRunsScratch(); // buffer compartilhado (ver storage.h)
  int n = storageReadDayRuns(c->slug, day, rows, MAX_RUNS_PER_DAY);
  if (lineIndex < 0 || lineIndex >= n) {
    sendError(404, "Corrida nao encontrada");
    return;
  }

  strlcpy(rows[lineIndex].status, storedStatus, sizeof(rows[lineIndex].status));
  storageRewriteDayJsonl(c->slug, day, rows, n);

  // Regrava também o CSV legível, com os nomes já resolvidos localmente
  // (antes vinha do servidor via day-status-sync — ver adendo Fase 8).
  static DayResultRow csvRows[MAX_RUNS_PER_DAY];
  for (int i = 0; i < n; i++) {
    const Participant* p = participantById(rows[i].participantId);
    strlcpy(csvRows[i].participantName, p ? p->name : "?", sizeof(csvRows[i].participantName));
    csvRows[i].elapsedUs = rows[i].elapsedUs;
    strlcpy(csvRows[i].status, rows[i].status, sizeof(csvRows[i].status));
  }
  storageRewriteDayCsv(c->slug, day, csvRows, n);

  if (isRetry) {
    ledSetRed(false); // juiz decidiu - não fica mais esperando veredito
    raceControlArm(competitionId, rows[lineIndex].participantId, rows[lineIndex].day, rows[lineIndex].attempt);
    JsonDocument out;
    out["id"] = runId;
    out["status"] = "retried";
    out["rearmed"] = true;
    sendJsonDoc(200, out);
    return;
  }

  // Copia os campos da linha para variáveis locais ANTES de chamar
  // maybeAdvanceAttempt() — essa função também lê o dia via
  // storageDayRunsScratch() (mesmo buffer compartilhado), então 'rows'
  // não pode mais ser usado com segurança depois desta chamada.
  int respParticipantId = rows[lineIndex].participantId;
  int respCompetitionId = rows[lineIndex].competitionId;
  int respDay = rows[lineIndex].day;
  int respAttempt = rows[lineIndex].attempt;
  int64_t respElapsedUs = rows[lineIndex].elapsedUs;
  int64_t respStartTimestampUs = rows[lineIndex].startTimestampUs;
  int64_t respEndTimestampUs = rows[lineIndex].endTimestampUs;
  char respEventId[48];
  strlcpy(respEventId, rows[lineIndex].eventId, sizeof(respEventId));
  char respStatus[24];
  strlcpy(respStatus, rows[lineIndex].status, sizeof(respStatus));

  ledSetRed(false); // veredito (valid/invalid/dnf) dado - não fica mais esperando
  const Participant* validatedParticipant = participantById(respParticipantId);

  // Reenvia pra nuvem com o veredito final (Fase 19, adendo) - sem isso,
  // a corrida ficaria "pending_validation" pra sempre do lado de lá,
  // já que cloudSyncTryNow só era chamado uma vez, no instante em que a
  // corrida termina (antes do juiz validar). Mesma janela seguraque o
  // resto (não existe corrida armada enquanto uma validação está
  // pendente - ver derivePhase() em web_page.h).
  RunRecord syncRecord;
  strlcpy(syncRecord.eventId, respEventId, sizeof(syncRecord.eventId));
  syncRecord.competitionId = respCompetitionId;
  syncRecord.participantId = respParticipantId;
  syncRecord.day = respDay;
  syncRecord.attempt = respAttempt;
  syncRecord.startTimestampUs = respStartTimestampUs;
  syncRecord.endTimestampUs = respEndTimestampUs;
  syncRecord.elapsedUs = respElapsedUs;
  strlcpy(syncRecord.status, respStatus, sizeof(syncRecord.status));

  RunContext syncCtx;
  strlcpy(syncCtx.competitionSlug, c->slug, sizeof(syncCtx.competitionSlug));
  strlcpy(syncCtx.participantName, validatedParticipant ? validatedParticipant->name : "?", sizeof(syncCtx.participantName));
  syncCtx.competitionId = respCompetitionId;
  syncCtx.participantId = respParticipantId;
  syncCtx.day = respDay;
  syncCtx.attempt = respAttempt;
  syncCtx.hasContext = true;
  cloudSyncTryNow(syncRecord, syncCtx);

  // Tomada consumida (independente do veredito) -> arma a próxima
  // automaticamente, se houver (mesma regra do servidor antigo).
  maybeAdvanceAttempt(competitionId, respParticipantId, day);

  const Participant* p = validatedParticipant;
  JsonDocument out;
  out["id"] = runId;
  out["event_id"] = respEventId;
  out["competition_id"] = respCompetitionId;
  out["participant_id"] = respParticipantId;
  out["participant_name"] = p ? p->name : "?";
  out["day"] = respDay;
  out["attempt"] = respAttempt;
  out["elapsed_us"] = respElapsedUs;
  out["status"] = respStatus;
  sendJsonDoc(200, out);
}

// ---------------------------------------------------------------------
// Configuração do modo Online (Fase 18) — tela de Configurações da
// página local do juiz.
// ---------------------------------------------------------------------

static void writeCloudConfigJson(JsonObject o) {
  CloudSyncConfig cfg = cloudSyncGetConfig();
  o["base_url"] = cfg.baseUrl;
  o["device_token"] = cfg.deviceToken;
  // "enabled" calculado, não configurável (Fase 19, adendo) - Online é
  // simplesmente ter os dois campos preenchidos.
  o["online_active"] = (strlen(cfg.baseUrl) > 0 && strlen(cfg.deviceToken) > 0);
}

static void handleGetCloudConfig() {
  JsonDocument doc;
  writeCloudConfigJson(doc.to<JsonObject>());
  sendJsonDoc(200, doc);
}

static void handlePostCloudConfig() {
  JsonDocument in;
  readJsonBody(in);

  CloudSyncConfig cfg;
  strlcpy(cfg.baseUrl, in["base_url"] | "", sizeof(cfg.baseUrl));
  strlcpy(cfg.deviceToken, in["device_token"] | "", sizeof(cfg.deviceToken));

  cloudSyncSetConfig(cfg);

  JsonDocument out;
  writeCloudConfigJson(out.to<JsonObject>());
  sendJsonDoc(200, out);
}

static void handlePostCloudConfigTest() {
  char msg[96];
  bool ok = cloudSyncTestAuth(msg, sizeof(msg));
  JsonDocument doc;
  doc["ok"] = ok;
  doc["message"] = msg;
  sendJsonDoc(200, doc);
}

// ---------------------------------------------------------------------
// Exportação (JSON e CSV) — buffer de leitura de corridas COMPARTILHADO
// entre as duas rotas abaixo. Nunca rodam ao mesmo tempo (um único
// request por vez), então não faz sentido cada handler ter o seu -
// isso já causou overflow de DRAM (~33KB pra duas cópias de 150
// StoredRun cada, quando 1 já basta).
// ---------------------------------------------------------------------
static StoredRun exportDayRunsBuf[MAX_RUNS_PER_DAY];

// ---------------------------------------------------------------------
// Exportação em CSV — uma linha por corrida, achatada (equipe/robô já
// resolvidos por nome), pronta pra abrir direto no Excel/Sheets. Ao
// contrário do /export (JSON, estrutura relacional completa), este é
// só a tabela de resultados.
// ---------------------------------------------------------------------

static void handleGetExportCsv() {
  String out;
  out.reserve(4096);
  out += "equipe,robo,competicao,dia,tentativa,tempo,status\n";

  char timeStr[16];

  for (int i = 0; i < competitionCount(); i++) {
    const Competition* c = competitionAt(i);
    for (int day = 1; day <= c->numDays; day++) {
      int n = storageReadDayRuns(c->slug, day, exportDayRunsBuf, MAX_RUNS_PER_DAY);
      for (int j = 0; j < n; j++) {
        const Participant* part = participantById(exportDayRunsBuf[j].participantId);
        const Team* team = part ? teamById(part->teamId) : nullptr;
        formatElapsedTime(exportDayRunsBuf[j].elapsedUs, timeStr, sizeof(timeStr));

        out += "\"";
        out += (team ? team->name : "");
        out += "\",\"";
        out += (part ? part->name : "");
        out += "\",\"";
        out += c->name;
        out += "\",";
        out += String(exportDayRunsBuf[j].day);
        out += ",";
        out += String(exportDayRunsBuf[j].attempt);
        out += ",";
        out += timeStr;
        out += ",";
        out += exportDayRunsBuf[j].status;
        out += "\n";
      }
    }
  }

  server.sendHeader("Content-Disposition", "attachment; filename=\"cronometro_export.csv\"");
  server.send(200, "text/csv", out);
}

// ---------------------------------------------------------------------
// Exportação pra download (substitui o SD) — junta tudo (equipes,
// participantes, competições e todas as corridas) num único JSON pro
// navegador baixar.
// ---------------------------------------------------------------------

static void handleGetExport() {
  JsonDocument doc;

  JsonArray teamsArr = doc["teams"].to<JsonArray>();
  for (int i = 0; i < teamCount(); i++) writeTeamJson(teamsArr.add<JsonObject>(), teamAt(i));

  JsonArray partsArr = doc["participants"].to<JsonArray>();
  for (int i = 0; i < participantCount(); i++) writeParticipantJson(partsArr.add<JsonObject>(), participantAt(i));

  JsonArray compsArr = doc["competitions"].to<JsonArray>();
  for (int i = 0; i < competitionCount(); i++) {
    const Competition* c = competitionAt(i);
    JsonObject co = compsArr.add<JsonObject>();
    co["id"] = c->id;
    co["name"] = c->name;
    co["status"] = c->status;
    co["num_days"] = c->numDays;
    co["attempts_per_day"] = c->attemptsPerDay;

    JsonArray runsArr = co["runs"].to<JsonArray>();
    for (int day = 1; day <= c->numDays; day++) {
      int n = storageReadDayRuns(c->slug, day, exportDayRunsBuf, MAX_RUNS_PER_DAY);
      for (int j = 0; j < n; j++) {
        JsonObject ro = runsArr.add<JsonObject>();
        ro["participant_id"] = exportDayRunsBuf[j].participantId;
        ro["day"] = exportDayRunsBuf[j].day;
        ro["attempt"] = exportDayRunsBuf[j].attempt;
        ro["elapsed_us"] = exportDayRunsBuf[j].elapsedUs;
        ro["status"] = exportDayRunsBuf[j].status;
      }
    }
  }

  String out;
  serializeJson(doc, out);
  server.sendHeader("Content-Disposition", "attachment; filename=\"cronometro_export.json\"");
  server.send(200, "application/json", out);
}

// ---------------------------------------------------------------------
// Senha da página do juiz (Fase 23) — ver checkJudgeAuth() acima.
// ---------------------------------------------------------------------

static void handleGetJudgeAuth() {
  JsonDocument doc;
  doc["enabled"] = strlen(g_judgePassword) > 0;
  doc["username"] = JUDGE_AUTH_USERNAME;
  sendJsonDoc(200, doc);
}

static void handlePostJudgeAuth() {
  JsonDocument in;
  readJsonBody(in);
  const char* password = in["password"] | "";
  judgeAuthSetPassword(password);

  JsonDocument out;
  out["enabled"] = strlen(g_judgePassword) > 0;
  sendJsonDoc(200, out);
}

// ---------------------------------------------------------------------
// Arm / controle de corrida
// ---------------------------------------------------------------------

static void writeArmStateJson(JsonObject o) {
  bool armed = raceControlIsArmed();
  o["armed"] = armed;
  if (!armed) return;

  o["released"] = raceControlIsReleased();
  o["started"] = raceControlIsStarted();
  o["competition_id"] = raceControlCompetitionId();
  o["participant_id"] = raceControlParticipantId();
  o["day"] = raceControlDay();
  o["attempt"] = raceControlAttempt();

  const Participant* p = participantById(raceControlParticipantId());
  o["participant_name"] = p ? p->name : "?";
  const Competition* c = competitionById(raceControlCompetitionId());
  o["competition_name"] = c ? c->name : "?";
}

static void handleGetArm() {
  JsonDocument doc;
  writeArmStateJson(doc.to<JsonObject>());
  sendJsonDoc(200, doc);
}

static void handlePostArm() {
  JsonDocument in;
  readJsonBody(in);
  int competitionId = in["competition_id"] | -1;
  int participantId = in["participant_id"] | -1;
  int day = in["day"] | -1;
  int attempt = in["attempt"] | -1;

  if (!competitionById(competitionId)) {
    sendError(404, "Competicao nao encontrada");
    return;
  }
  if (!participantById(participantId)) {
    sendError(404, "Participante nao encontrado");
    return;
  }

  raceControlArm(competitionId, participantId, day, attempt);

  JsonDocument out;
  writeArmStateJson(out.to<JsonObject>());
  sendJsonDoc(200, out);
}

static void handlePostArmRelease() {
  if (!raceControlIsArmed()) {
    sendError(400, "Nenhuma corrida armada para liberar");
    return;
  }
  raceControlRelease();
  JsonDocument doc;
  writeArmStateJson(doc.to<JsonObject>());
  sendJsonDoc(200, doc);
}

static void handlePostArmRetry() {
  if (!raceControlRequestRetry()) {
    sendError(400, "A corrida ainda nao comecou (aguardando o competidor passar pelo sensor)");
    return;
  }
  JsonDocument doc;
  doc["retry_requested"] = true;
  sendJsonDoc(200, doc);
}

static void handlePostArmAbort() {
  if (!raceControlRequestAbort()) {
    sendError(400, "A corrida ainda nao comecou (aguardando o competidor passar pelo sensor)");
    return;
  }
  JsonDocument doc;
  doc["abort_requested"] = true;
  sendJsonDoc(200, doc);
}

static void handleDeleteArm() {
  raceControlDisarm();
  JsonDocument doc;
  doc["disarmed"] = true;
  sendJsonDoc(200, doc);
}

// ---------------------------------------------------------------------
// Roteador (chamado via server.onNotFound — ver comentário de splitPath)
// ---------------------------------------------------------------------

static void handleApi() {
  if (!checkJudgeAuth()) return; // ver Fase 23 - protege toda a API local, não só a página HTML

  String segs[8];
  int segCount = splitPath(server.uri(), segs, 8);
  HTTPMethod method = server.method();

  if (segCount >= 3 && segs[0] == "api" && segs[1] == "v1") {
    const String &resource = segs[2];

    if (resource == "participants") {
      if (segCount == 3 && method == HTTP_GET) return handleGetParticipants();
      if (segCount == 3 && method == HTTP_POST) return handlePostParticipant();
      if (segCount == 4 && method == HTTP_DELETE) return handleDeleteParticipant(segs[3].toInt());
    } else if (resource == "teams") {
      if (segCount == 3 && method == HTTP_GET) return handleGetTeams();
      if (segCount == 3 && method == HTTP_POST) return handlePostTeam();
    } else if (resource == "competitions") {
      if (segCount == 3 && method == HTTP_GET) return handleGetCompetitions();
      if (segCount == 3 && method == HTTP_POST) return handlePostCompetition();
      if (segCount == 4 && method == HTTP_GET) return handleGetCompetition(segs[3].toInt());
      if (segCount == 4 && method == HTTP_PATCH) return handlePatchCompetition(segs[3].toInt());
      if (segCount == 5) {
        int id = segs[3].toInt();
        const String &sub = segs[4];
        if (sub == "participants" && method == HTTP_GET) return handleGetCompetitionParticipants(id);
        if (sub == "participants" && method == HTTP_POST) return handlePostCompetitionParticipant(id);
        if (sub == "day-status" && method == HTTP_GET) return handleGetDayStatus(id);
        if (sub == "advance-day" && method == HTTP_POST) return handlePostAdvanceDay(id);
        if (sub == "ranking" && method == HTTP_GET) return handleGetRanking(id);
        if (sub == "runs" && method == HTTP_GET) return handleGetRuns(id);
      }
      if (segCount == 6 && segs[4] == "participants" && method == HTTP_DELETE) {
        return handleDeleteCompetitionParticipant(segs[3].toInt(), segs[5].toInt());
      }
    } else if (resource == "runs") {
      if (segCount == 5 && segs[4] == "validation" && method == HTTP_PATCH) {
        return handlePatchRunValidation(segs[3].toInt());
      }
    } else if (resource == "arm") {
      if (segCount == 3 && method == HTTP_GET) return handleGetArm();
      if (segCount == 3 && method == HTTP_POST) return handlePostArm();
      if (segCount == 3 && method == HTTP_DELETE) return handleDeleteArm();
      if (segCount == 4 && segs[3] == "release" && method == HTTP_POST) return handlePostArmRelease();
      if (segCount == 4 && segs[3] == "abort" && method == HTTP_POST) return handlePostArmAbort();
      if (segCount == 4 && segs[3] == "retry" && method == HTTP_POST) return handlePostArmRetry();
    } else if (resource == "cloud-config") {
      if (segCount == 3 && method == HTTP_GET) return handleGetCloudConfig();
      if (segCount == 3 && method == HTTP_POST) return handlePostCloudConfig();
      if (segCount == 4 && segs[3] == "test" && method == HTTP_POST) return handlePostCloudConfigTest();
    } else if (resource == "export") {
      if (segCount == 3 && method == HTTP_GET) {
        if (server.hasArg("format") && server.arg("format") == "csv") return handleGetExportCsv();
        return handleGetExport();
      }
    } else if (resource == "judge-auth") {
      if (segCount == 3 && method == HTTP_GET) return handleGetJudgeAuth();
      if (segCount == 3 && method == HTTP_POST) return handlePostJudgeAuth();
    }
  }

  sendError(404, "Rota nao encontrada");
}

static void handleRoot() {
  if (!checkJudgeAuth()) return; // ver Fase 23
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void webServerInit() {
  judgeAuthInit();
  server.on("/", HTTP_GET, handleRoot);
  server.onNotFound(handleApi);
  server.begin(HTTP_SERVER_PORT);

  Serial.print("[WEB] Servidor HTTP local iniciado na porta ");
  Serial.println(HTTP_SERVER_PORT);
}

void webServerHandle() {
  server.handleClient();
}

void webServerRestart() {
  server.begin();
}