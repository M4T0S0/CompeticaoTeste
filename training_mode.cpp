#include "training_mode.h"
#include "config.h"
#include "state_machine.h"
#include "race_control.h"
#include "sensors.h"
#include "timer.h"
#include "display.h"
#include "wifi.h"
#include "peripherals.h"
#include <Arduino.h>

bool trainingModeActive = false;

enum class TrainingState { WAITING, RUNNING, HOLD };
static TrainingState tState = TrainingState::WAITING;
static int64_t holdUntilUs = 0;

void trainingModeInit() {
  pinMode(PIN_MODE_SELECT, INPUT_PULLUP);
}

static void enterTraining() {
  trainingModeActive = true;
  tState = TrainingState::WAITING;
  ledSetGreen(false);
  ledSetRed(false);
  displayShowTrainingIdle();
  Serial.println("[MODO] TREINO ativo.");
}

static void exitTraining() {
  trainingModeActive = false;
  transitionTo(SystemState::WAITING_START);
  // webServerHandle() nao roda durante o Treino, entao raceControlIsReleased()
  // nao pode ter mudado nesse meio tempo - seguro redesenhar direto aqui, sem
  // esperar o loop() principal perceber uma mudanca que nao existe.
  if (raceControlIsReleased()) displayShowReady(); else displayShowLocked();
  Serial.println("[MODO] COMPETICAO ativo.");
}

void trainingModePoll() {
  bool wantTraining = (digitalRead(PIN_MODE_SELECT) == LOW);
  if (wantTraining == trainingModeActive) return; // chave não mudou

  if (wantTraining && currentState == SystemState::WAITING_START && !raceControlIsArmed()) {
    wifiAbortPortal(); // nao fica esperando rede pra entrar em Treino
    enterTraining();
  } else if (!wantTraining && tState == TrainingState::WAITING) {
    exitTraining();
  }
  // troca pedida no meio de uma corrida (competição ou treino) fica
  // pendente e é aplicada assim que o sistema voltar pro estado ocioso
}

void trainingLoop() {
  sensorsUpdate();
  peripheralsUpdate();
  int64_t ts;

  switch (tState) {
    case TrainingState::WAITING:
      sensor2Flush();
      if (sensor1Consume(ts)) {
        timerStart();
        buzzerBeep();
        ledSetGreen(true);
        ledSetRed(false);
        tState = TrainingState::RUNNING;
      }
      break;

    case TrainingState::RUNNING:
      displayShowRunning(timerElapsedUs(), nullptr, nullptr);
      if (sensor2Consume(ts)) {
        timerStop();
        buzzerBeep();
        ledSetGreen(false);
        ledSetRed(true);
        displayShowFinished(timerElapsedUs(), nullptr, nullptr);
        holdUntilUs = nowUs() + (int64_t)RESULT_DISPLAY_HOLD_US;
        tState = TrainingState::HOLD;
      }
      break;

    case TrainingState::HOLD:
      if (nowUs() >= holdUntilUs) {
        ledSetRed(false);
        sensor1Flush();
        tState = TrainingState::WAITING;
        displayShowTrainingIdle();
      }
      break;
  }
}