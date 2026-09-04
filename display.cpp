#include "display.h"
#include "config.h"
#include "timer.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "esp_timer.h"

static Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);
static bool displayOk = false;
static int64_t lastRunningUpdateUs = 0;
static int64_t lastWifiSetupUpdateUs = 0;

void displayInit() {
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  Wire.setClock(400000);

  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
    Serial.println("[DISPLAY] ERRO: SSD1306 nao encontrado no barramento I2C.");
    displayOk = false;
    return;
  }

  displayOk = true;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.display();
  Serial.println("[DISPLAY] OLED inicializado.");
}

static void drawCentered(const char* text, int16_t y, uint8_t textSize) {
  oled.setTextSize(textSize);
  int16_t x1, y1;
  uint16_t w, h;
  oled.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (OLED_WIDTH - (int16_t)w) / 2;
  if (x < 0) x = 0;
  oled.setCursor(x, y);
  oled.print(text);
}

void displayShowReady() {
  if (!displayOk) return;
  oled.clearDisplay();
  drawCentered("LIBERADO", 20, 2);
  drawCentered("aguardando robo...", 48, 1);
  oled.display();
}

void displayShowLocked() {
  if (!displayOk) return;
  oled.clearDisplay();
  drawCentered("AGUARDANDO", 16, 1);
  drawCentered("LIBERACAO", 30, 1);
  drawCentered("do juiz", 44, 1);
  oled.display();
}

void displayShowConnecting() {
  if (!displayOk) return;
  oled.clearDisplay();
  drawCentered("CONECTANDO...", 20, 1);
  drawCentered("Aguarde", 36, 1);
  oled.display();
}

void displayShowWifiSetup(const char* portalSsid, const char* portalIp, int secondsRemaining) {
  if (!displayOk) return;

  int64_t now = esp_timer_get_time();
  if (now - lastWifiSetupUpdateUs < (int64_t)DISPLAY_UPDATE_INTERVAL_US) return;
  lastWifiSetupUpdateUs = now;

  char timeoutStr[24];
  snprintf(timeoutStr, sizeof(timeoutStr), "Timeout em %ds", secondsRemaining);

  oled.clearDisplay();
  drawCentered(portalSsid, 0, 1);
  drawCentered("Conecte-se em:", 18, 1);
  drawCentered(portalIp, 30, 1);
  drawCentered(timeoutStr, 50, 1);
  oled.display();
}

void displayShowIpReveal(const char* ip) {
  if (!displayOk) return;
  oled.clearDisplay();
  drawCentered("Conecte-se em:", 12, 1);
  drawCentered(ip, 32, 2);
  oled.display();
}

void displayShowTrainingIdle() {
  if (!displayOk) return;
  oled.clearDisplay();
  drawCentered("MODO", 12, 2);
  drawCentered("TREINO", 36, 2);
  oled.display();
}

void displayShowRunning(int64_t elapsedUs, const char* robotName, const char* teamName) {
  if (!displayOk) return;

  int64_t now = esp_timer_get_time();
  if (now - lastRunningUpdateUs < (int64_t)DISPLAY_UPDATE_INTERVAL_US) return;
  lastRunningUpdateUs = now;

  char timeStr[16];
  formatElapsedTime(elapsedUs, timeStr, sizeof(timeStr));

  oled.clearDisplay();

  if (robotName == nullptr) {
    // Modo Treino: só o tempo, bem grande, nada mais na tela.
    drawCentered(timeStr, 24, 2);
  } else {
    drawCentered(robotName, 0, 1);
    drawCentered(teamName ? teamName : "", 12, 1);
    drawCentered(timeStr, 32, 2);
  }

  oled.display();
}

void displayShowFinished(int64_t elapsedUs, const char* robotName, const char* teamName) {
  if (!displayOk) return;

  char timeStr[16];
  formatElapsedTime(elapsedUs, timeStr, sizeof(timeStr));

  oled.clearDisplay();

  if (robotName == nullptr) {
    drawCentered("FIM", 0, 1);
    drawCentered(timeStr, 24, 2);
  } else {
    drawCentered(robotName, 0, 1);
    drawCentered(teamName ? teamName : "", 12, 1);
    drawCentered("FIM", 26, 1);
    drawCentered(timeStr, 38, 2);
  }

  oled.display();
}

void displayShowError(const char* message) {
  if (!displayOk) return;
  oled.clearDisplay();
  drawCentered("ERRO", 0, 1);
  oled.setTextSize(1);
  oled.setCursor(0, 20);
  oled.println(message);
  oled.display();
}