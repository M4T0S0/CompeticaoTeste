#pragma once
#include <stdint.h>

// =====================================================================
// DATA_STORE.H — "Banco de dados" local (versão standalone)
//
// Substitui server/database.py + routers/competitions.py +
// routers/participants.py. Participantes, competições e o vínculo entre
// eles são pequenos o bastante para viver inteiros em RAM (tetos em
// config.h) — persistidos em três arquivos CSV na flash interna
// (LittleFS, ver storage.h/cpp e Fase 20), com nome e cabeçalho de
// coluna em português (/participantes.csv, /competicoes.csv,
// /vinculos.csv), carregados de volta no boot (dataStoreInit(), chamado
// depois de storageInit()). É esse carregamento que "recupera uma
// competição anterior" ao religar o ESP32: nada se perde, porque o
// estado inteiro já estava na flash. O cartão SD (se presente) recebe
// uma cópia periódica desses mesmos arquivos, mas nunca é lido de volta
// em tempo real (ver sd_export.h).
//
// Corridas em si (resultados) NÃO entram aqui — continuam em
// /<slug>/Dia_N/resultados.jsonl, geridas por storage.h/cpp.
// =====================================================================

struct Team {
  int id;
  char name[40];
};

struct Participant {
  int id;
  char name[40];
  char externalCode[24];
  int teamId;
};

struct Competition {
  int id;
  char name[48];
  char slug[32];        // sempre único (nome normalizado + "_" + id)
  int numDays;
  int attemptsPerDay;
  char status[16];       // "draft" | "finished"
  int currentDay;
};

struct DayStatusParticipant {
  int participantId;
  char name[40];
  int attemptsDone;
  bool done;
};

void dataStoreInit();

// ------------------- Participantes -------------------
// Retorna o id novo, -1 se capacidade cheia, -2 se já existe robô com esse
// nome NESSA MESMA equipe (case-insensitive) — nomes repetidos entre
// equipes diferentes são permitidos.
int participantCreate(const char* name, const char* externalCode, int teamId);

// Retorna o id novo, -1 se capacidade cheia, -2 se já existe equipe com esse nome (case-insensitive).
int teamCreate(const char* name);
int teamCount();
const Team* teamAt(int index);
const Team* teamById(int id);
bool teamDelete(int id);
int participantCount();
const Participant* participantAt(int index);
const Participant* participantById(int id);
// Remove o participante e todos os vínculos dele com competições
// (histórico de corridas no SD é preservado, só fica "orfão").
bool participantDelete(int id);

// ------------------- Competições -------------------
// Retorna o novo id, ou -1 se a capacidade (MAX_COMPETITIONS) foi atingida.
int competitionCreate(const char* name, int numDays, int attemptsPerDay);
int competitionCount();
const Competition* competitionAt(int index);
const Competition* competitionById(int id);

// Avança para o próximo dia, ou marca como finalizada se já era o
// último. Desarma a corrida atual se ela pertencer a esta competição
// (mesma rede de segurança do servidor antigo — nunca deixa nada
// "vazar" para a competição seguinte). Retorna false se o id não existe.
bool competitionAdvanceDay(int id, bool &outFinished, int &outCurrentDay);


// Edita dias/tomadas de uma competição já criada — só permitido se ela
// ainda não tiver nenhuma corrida registrada (proteção contra editar no
// meio da prova e bagunçar contagens já feitas). Retorna 0 em sucesso,
// -1 se a competição não existe, -2 se já existem corridas registradas.
int competitionUpdate(int id, int numDays, int attemptsPerDay);

// ------------------- Vínculo participante <-> competição -------------------
bool linkExists(int competitionId, int participantId);
// Idempotente: se já existe, não duplica. Retorna false só se
// competição/participante não existirem ou a capacidade (MAX_LINKS)
// tiver sido atingida.
bool linkCreate(int competitionId, int participantId);
bool linkDelete(int competitionId, int participantId);
// true se o participante já tem alguma volta registrada (ou pendente)
// nesta competição — usado para bloquear exclusão (ver handleDeleteCompetitionParticipant).
bool participantHasRunsInCompetition(int competitionId, int participantId);
// Preenche outIds com os ids dos participantes vinculados (na ordem em
// que foram vinculados); retorna quantos.
int linkedParticipantIds(int competitionId, int* outIds, int maxOut);

// ------------------- Status do dia -------------------
// Preenche outRows (até maxRows) com a contagem de tomadas de cada
// participante vinculado no dia ATUAL da competição; retorna quantos
// participantes foram preenchidos. dayComplete = todos os vinculados já
// completaram attemptsPerDay tomadas (e há pelo menos um vinculado).
int competitionDayStatus(int competitionId, DayStatusParticipant* outRows, int maxRows,
                          bool &dayComplete, bool &isLastDay);

// Se o participante ainda tiver tomadas restantes no dia (contando
// TODAS as tomadas já registradas, independente do status — mesma
// regra do servidor antigo), arma (travada) a próxima automaticamente
// via race_control. Chamada depois que uma corrida termina já resolvida
// (abortada) ou é validada pelo juiz.
void maybeAdvanceAttempt(int competitionId, int participantId, int day);
