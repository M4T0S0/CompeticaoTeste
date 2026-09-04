#include "wifi.h"
#include "config.h"
#include "display.h"
#include "web_server.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h> // biblioteca "WiFiManager" (tzapu) - Library Manager
#include "esp_timer.h"

static WiFiManager wm;
static bool staConnected = false;
static bool systemApReady = false;
static uint32_t portalStartMs = 0;
static int64_t lastStatusCheckUs = 0;

static bool bootResetButtonHeld() {
  pinMode(WIFI_RESET_BUTTON_PIN, INPUT_PULLUP);
  if (digitalRead(WIFI_RESET_BUTTON_PIN) != LOW) return false;

  uint32_t startMs = millis();
  while (digitalRead(WIFI_RESET_BUTTON_PIN) == LOW) {
    if (millis() - startMs >= WIFI_RESET_HOLD_MS) return true;
  }
  return false;
}

// Garante o AP PRÓPRIO do sistema ativo (Fase 1, seção 2 - híbrido
// obrigatório). Só pode ser chamado quando o portal do WiFiManager NÃO
// estiver mais ocupando o rádio (senão os dois brigam pela mesma
// interface AP) — por isso não é mais chamado direto de dentro de
// wifiInit() quando o portal abre; wifiUpdate() chama assim que o
// portal (wm.getConfigPortalActive()) fechar.
static void ensureSystemAP() {
  if (systemApReady) return;
  WiFi.mode(WIFI_AP_STA);
  bool apOk = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL);
  if (apOk) {
    systemApReady = true;
    webServerRestart(); // AP mudou de interface - reancora o listener HTTP nela
    Serial.print("[WIFI] Access Point do sistema ativo. SSID: ");
    Serial.print(WIFI_AP_SSID);
    Serial.print("  IP: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("[WIFI] ERRO: falha ao iniciar o Access Point do sistema.");
  }
}

// Chamado pelo WiFiManager assim que o portal de configuração abre.
static void configModeCallback(WiFiManager *myWM) {
  (void)myWM;
  portalStartMs = millis();
  Serial.print("[WIFI] Portal de configuracao aberto. Rede: ");
  Serial.print(WIFI_CONFIG_PORTAL_SSID);
  Serial.print("  Acesse: ");
  Serial.println(WiFi.softAPIP());
}

void wifiInit() {
  if (bootResetButtonHeld()) {
    Serial.println("[WIFI] Botao mantido pressionado no boot - apagando rede STA salva.");
    wm.resetSettings();
  }

  // O portal (config AP) não bloqueia mais - só a tentativa inicial na
  // rede salva (limitada a WIFI_STA_CONNECT_TIMEOUT_S) continua sendo
  // uma espera curta e aceitável no boot.
  wm.setConfigPortalBlocking(false);
  wm.setConnectTimeout(WIFI_STA_CONNECT_TIMEOUT_S);
  wm.setConfigPortalTimeout(WIFI_CONFIG_PORTAL_TIMEOUT_S);
  wm.setAPCallback(configModeCallback);

  displayShowConnecting();

  Serial.println("[WIFI] Conectando a rede salva...");
  bool connected = wm.autoConnect(WIFI_CONFIG_PORTAL_SSID, WIFI_CONFIG_PORTAL_PASSWORD);
  staConnected = connected;

  if (connected) {
    Serial.print("[WIFI] STA conectado. IP: ");
    Serial.println(WiFi.localIP());
    ensureSystemAP();
  } else if (wm.getConfigPortalActive()) {
    Serial.println("[WIFI] Rede salva indisponivel - portal aberto em background (nao bloqueia mais o boot).");
    // AP proprio fica pendente ate o portal fechar - ver wifiUpdate()
  } else {
    Serial.println("[WIFI] Sem rede salva e portal nao abriu. Habilitando AP proprio.");
    ensureSystemAP();
  }
}

void wifiUpdate() {
  if (wm.getConfigPortalActive()) {
    wm.process(); // mantem o portal vivo sem bloquear
  } else if (!systemApReady) {
    // portal acabou de fechar (conectou, expirou, ou foi abortado pela
    // chave de Modo Treino) e o AP proprio ainda nao tinha sido criado
    ensureSystemAP();
  }

  int64_t now = esp_timer_get_time();
  if (now - lastStatusCheckUs < (int64_t)WIFI_STATUS_CHECK_INTERVAL_US) return;
  lastStatusCheckUs = now;

  bool nowConnected = (WiFi.status() == WL_CONNECTED);
  if (nowConnected != staConnected) {
    staConnected = nowConnected;
    if (staConnected) {
      Serial.print("[WIFI] STA (re)conectado. IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("[WIFI] STA desconectado (rede externa indisponivel ou fora de alcance).");
    }
  }
}

bool wifiIsStaConnected() { return staConnected; }

bool wifiPortalActive() { return wm.getConfigPortalActive(); }

void wifiPortalUpdate() {
  if (wm.getConfigPortalActive()) wm.process();
}

int wifiPortalSecondsRemaining() {
  if (!wm.getConfigPortalActive()) return 0;
  int elapsedS = (int)((millis() - portalStartMs) / 1000);
  int remaining = (int)WIFI_CONFIG_PORTAL_TIMEOUT_S - elapsedS;
  return remaining > 0 ? remaining : 0;
}

void wifiAbortPortal() {
  if (wm.getConfigPortalActive()) {
    wm.stopConfigPortal();
    Serial.println("[WIFI] Portal abortado (chave de Modo Treino).");
  }
}

void wifiGetStatusString(char* buffer, size_t bufferSize) {
  if (wm.getConfigPortalActive()) {
    snprintf(buffer, bufferSize, "%s", WiFi.softAPIP().toString().c_str());
  } else if (staConnected) {
    snprintf(buffer, bufferSize, "STA:%s", WiFi.localIP().toString().c_str());
  } else {
    snprintf(buffer, bufferSize, "AP:%s", WiFi.softAPIP().toString().c_str());
  }
}