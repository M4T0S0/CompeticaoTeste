#pragma once
#include <stdint.h>
#include <stddef.h>

// =====================================================================
// STORAGE.H — Persistência local (Fase 4; flash interna a partir da Fase 20)
//
// Formato: JSON Lines (.jsonl) — um objeto JSON por linha, append-only.
// Escolhido em vez de um único array JSON: uma linha corrompida (ex.:
// corte de energia no meio da escrita) não invalida as linhas
// anteriores — cada linha é independente e pode ser pulada
// individualmente ao reprocessar o arquivo (ver Fase 10 - sincronização).
//
// Fase 20: a fonte de verdade é a flash interna do ESP32 (LittleFS) —
// sem cartão removível, sem fio solto, muito mais confiável pra
// sustentar a operação em tempo real. O cartão SD virou puramente um
// destino de exportação periódica (ver sd_export.h), nunca mais lido
// por nada aqui.
//
// Falha da flash NUNCA bloqueia a cronometragem (ver Fase 1, seção 19)
// — todas as funções aqui são seguras de chamar mesmo se storageInit()
// falhar; elas só logam o erro e retornam false/no-op (e o resultado
// ainda assim não se perde, ver a fila de pendências em RAM abaixo).
// =====================================================================

struct RunRecord {
  char eventId[48];     // identificador único do evento (ver storageGenerateEventId)
  int competitionId;    // -1 = ainda não selecionável (chega na Fase 8)
  int participantId;    // -1 = idem
  int day;               // -1 = idem
  int attempt;            // -1 = idem
  int64_t startTimestampUs;
  int64_t endTimestampUs;
  int64_t elapsedUs;
  char status[24];      // "pending_validation" | "invalid" | "dnf" (ver adendo Fase 1)
};

// Contexto legível (Fase 8), buscado do servidor uma única vez ao
// iniciar a corrida (não a cada polling) — usado para nomear a
// pasta/arquivo no SD E para preencher os IDs reais enviados ao
// servidor (ver adendo Fase 10 — necessário para reenvio tardio
// associar à corrida certa, não à que estiver armada na hora do reenvio).
struct RunContext {
  char competitionSlug[32];   // nome da competição, seguro para pasta (ex.: "Desafio_de_Verao_2026")
  char participantName[40];   // nome do participante, para o CSV legível
  int competitionId;
  int participantId;
  int day;
  int attempt;
  bool hasContext;             // false = sem contexto (ex.: rede indisponível) -> usa fallback plano/IDs -1
};

// Uma linha do resultado consolidado do dia — usada para reescrever o
// resultados.csv (nome já resolvido, legível) a partir de dados
// atualizados (ex.: depois que o juiz validou uma corrida).
struct DayResultRow {
  char participantName[40];
  int64_t elapsedUs;
  char status[24];
};

// Uma linha lida de volta de /<slug>/Dia_N/resultados.jsonl — mesmos
// campos de RunRecord, sem o nome do participante (o .jsonl nunca guarda
// nome, só IDs — ver storageAppendRun). Usada por quem precisa reler o
// que já foi gravado: ranking (soma/melhor tempo), day-status (contagem
// de tomadas) e validação de corrida (mudar o status de uma linha).
struct StoredRun {
  char eventId[48];
  int competitionId;
  int participantId;
  int day;
  int attempt;
  int64_t startTimestampUs;
  int64_t endTimestampUs;
  int64_t elapsedUs;
  char status[24];
};

void storageInit();

// =====================================================================
// Fila de pendências em RAM (Fase 19) — quando o SD falha ao persistir
// uma corrida (mau contato, cartão cheio etc.), o resultado NÃO é mais
// descartado: fica aqui até o SD voltar a responder. É isso que permite
// o juiz validar uma corrida mesmo sem SD (ver web_server.cpp) e o
// espelhamento pra nuvem (cloud_sync.cpp) continuar recebendo o
// resultado mesmo que a gravação local tenha falhado.
//
// Tamanho fixo pequeno de propósito: corridas não acontecem rápido o
// bastante para esgotar isto em uso normal — é rede de segurança para
// uma falha transitória, não uma fila de produção.
// =====================================================================
#define MAX_PENDING_RUNS 8

int storagePendingCount();

// Quantas pendências (ainda não persistidas no SD) existem para esta
// combinação competição/participante/dia — usado por
// competitionDayStatus()/maybeAdvanceAttempt() (data_store.cpp) para
// nunca oferecer/rearmar uma tomada que já está em andamento só porque
// o SD ainda não confirmou a tomada anterior (ver Fase 19).
int storagePendingCountFor(int competitionId, int participantId, int day);

// Ponteiros diretos para a entrada N da fila (para handleGetRuns/
// handlePatchRunValidation lerem/mutarem sem copiar). nullptr se index
// for inválido ou a posição não estiver em uso.
RunRecord* storagePendingRecordAt(int index);
RunContext* storagePendingContextAt(int index);

// Muda o status de uma pendência (validação do juiz enquanto ainda não
// foi possível persistir no SD). Retorna false se index for inválido.
bool storagePendingSetStatus(int index, const char* status);

// Tenta persistir de novo cada pendência da fila (chama o mesmo caminho
// de escrita de storageAppendRun) — remove da fila só as que tiverem
// sucesso. Seguro de chamar a qualquer momento, mesmo com a fila vazia
// ou o SD ainda fora do ar.
void storageFlushPending();

// Persiste um resultado no SD (append). Se ctx.hasContext for true,
// grava em /<competitionSlug>/Dia_<day>/resultados.jsonl (fonte de
// verdade) E resultados.csv (leitura humana). Se hasContext for false,
// cai no arquivo plano original (SD_QUEUE_FILENAME) — comportamento
// da Fase 4, preservado como fallback.
bool storageAppendRun(const RunRecord &record, const RunContext &ctx);

// Reescreve por completo o resultados.csv do dia informado, a partir de
// uma lista de linhas já resolvidas (nome + tempo + status ATUAL). Usa
// arquivo temporário + troca (rename) para nunca deixar o CSV num
// estado parcial/corrompido em caso de corte de energia no meio da
// escrita (ver Fase 1, seção 19).
bool storageRewriteDayCsv(const char* competitionSlug, int day, const DayResultRow* rows, int count);

// Lê de volta todas as linhas de /<slug>/Dia_<day>/resultados.jsonl para
// 'outRows' (até 'maxRows' — ver MAX_RUNS_PER_DAY em config.h). Retorna
// quantas linhas foram lidas (0 se o arquivo não existe ou o SD está
// indisponível). Usada por ranking.cpp e data_store.cpp — nunca lê o
// .jsonl diretamente fora deste módulo, para manter o formato do
// arquivo encapsulado aqui.
int storageReadDayRuns(const char* competitionSlug, int day, StoredRun* outRows, int maxRows);

// Buffer de trabalho ÚNICO e COMPARTILHADO para ler as linhas de um dia
// (StoredRun[MAX_RUNS_PER_DAY] já usa ~16 KB — cada função que
// declarasse sua própria cópia estourava a DRAM disponível do ESP32,
// que também precisa de espaço para WiFi/BT e o heap do WebServer/
// ArduinoJson). Só existe UMA instância, exposta aqui; é seguro
// reutilizar entre chamadas desde que cada chamador termine de usar o
// conteúdo antes de chamar outra função que também leia um dia (ver
// comentário em ranking.cpp/data_store.cpp/web_server.cpp).
StoredRun* storageDayRunsScratch();

// Reescreve por completo o resultados.jsonl do dia (mesmo padrão
// temp+rename de storageRewriteDayCsv) — usada quando o juiz valida uma
// corrida (PATCH .../validation) e o status de UMA linha precisa mudar
// permanentemente na fonte de verdade, não só no CSV legível.
bool storageRewriteDayJsonl(const char* competitionSlug, int day, const StoredRun* rows, int count);

// Indica se a flash interna (LittleFS) está operacional — fonte de
// verdade a partir da Fase 20 (ver docs/fase20-notas-bancada.md).
bool storageIsOk();

// Gera um event_id único baseado no chip ID do ESP32 + timestamp de
// início da corrida.
void storageGenerateEventId(int64_t startTimestampUs, char* buffer, size_t bufferSize);