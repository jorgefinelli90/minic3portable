// ===========================================================================
//  Mini C3 — handheld firmware
//  Based on Tinytosh by Vladimir Gitsarev (MIT) — github.com/VladimirGitsarev/Tinytosh
//  Adapted for: Waveshare ESP32-C3-Zero, OLED 1.3" SH1106, 4 arrows + touch A,
//  browsable flight list and OTA updates.
// ===========================================================================

#include <ArduinoOTA.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "AirQualityService.h"
#include "BambuService.h"
#include "CalendarService.h"
#include "ConfigManager.h"
#include "CryptoService.h"
#include "CurrencyService.h"
#include "DataSyncService.h"
#include "DaylightService.h"
#include "DisplayService.h"
#include "GameService.h"
#include "HardwareService.h"
#include "images.h"
#include "InfoPanel.h"
#include "IssService.h"
#include "MiniC3.h"
#include "MessageService.h"
#include "MessageInbox.h"
#include "MoonService.h"
#include "NewsService.h"
#include "NightModeService.h"
#include "OtaService.h"
#include "PcMonitorService.h"
#include "PopulationService.h"
#include "SpotifyService.h"
#include "StockService.h"
#include "structs.h"
#include "TimeService.h"
#include "TrucoService.h"
#include "WeatherService.h"
#include "WebServerService.h"
#include "WifiService.h"

SET_LOOP_TASK_STACK_SIZE(20 * 1024);

const char* PREF_NAMESPACE = "minic3_config";

// Shared between the main loop and the background sync task (see MiniC3.h)
SemaphoreHandle_t gStateMutex = nullptr;
volatile bool gOtaInProgress = false;

// Global Data Structure
AppState appState;

// Forward declarations of callbacks
void updateAllDataCallback();
void handleInput(InputEvent event, bool repeat);
void updateScreen();

// Service Instances
ConfigManager configManager(PREF_NAMESPACE);
DisplayService displayService(128, 64, -1);
WebServerService webServerService(80, updateAllDataCallback);
HardwareService hardwareService(handleInput);
OtaService otaService;
TimeService timeService;
CalendarService calendarService;
WeatherService weatherService;
AirQualityService airQualityService;
DaylightService daylightService;
MoonService moonService;
IssService issService;
PopulationService populationService;
CryptoService cryptoService;
CurrencyService currencyService;
NewsService newsService;
MessageService messageService;
SpotifyService spotifyService;
StockService stockService;
PcMonitorService pcMonitorService;
BambuService bambuService;
DataSyncService dataSyncService;
NightModeService nightModeService;
WifiService wifiService;
GameService gameService;
InfoPanel infoPanel;
MessageInbox messageInbox;
TrucoService trucoService;

unsigned long lastScreenSwitch = 0;

// Sleep: hold ↑ 3 s to turn off, any other key turns it back on
volatile bool sleepRequested = false;
volatile bool fullSyncRequested = false;

// The first full data load already ran (if it booted offline, it runs as
// soon as a saved network shows up)
bool initialLoadDone = false;

// WiFi turned off to save battery (games, truco pad): see updateRadioSaving()
const uint32_t TRUCO_RADIO_OFF_MS = 60000;
bool radioSaving = false;
uint32_t trucoScreenSince = 0;

// v1.12.0 battery saver (device panel > Ajustes, or the web panel): dims the
// screen after 20 s without keys, sleeps after 3 min (5 s warning first), and
// runs the CPU at 80 MHz and the WiFi in its deepest modem sleep while you are
// not playing.
const uint32_t SAVER_DIM_MS = 20000;
const uint32_t SAVER_SLEEP_MS = 180000;
const uint32_t SAVER_WARN_MS = 5000;
uint32_t lastUserInput = 0;
bool saverCountdown = false;   // the "going to sleep" bar is on screen
bool saverDimmed = false;

uint32_t idleMs() {
  uint32_t last = lastUserInput;
  uint32_t web = gLastWebActivity;
  if ((int32_t)(web - last) > 0) last = web;
  return millis() - last;
}

bool saverDimWanted() {
  return appState.config.power_saver && idleMs() >= SAVER_DIM_MS;
}


// ---------------------------------------------------------------------------
//  Input
// ---------------------------------------------------------------------------

// Any input first wakes the panel if night mode turned it off; that press is
// consumed. Returns true when it did.
bool wakeIfScreenOff() {
  int activeAction = TimeService::getActiveNightAction(appState.config);

  if (nightModeService.wasScreenOff(activeAction)) {
    Serial.println("🌙 Night Mode: Waking display temporarily on Primary Screen.");
    displayService.jumpToFirstEnabledScreen(appState);
    displayService.setContrast(true);
    nightModeService.recordInteraction();
    lastScreenSwitch = millis();
    return true;
  }

  if (nightModeService.isLatched()) {
    displayService.setContrast(activeAction != 0);
  }
  return false;
}

// Options of the device panel (A on a screen where A has no other use)
void runInfoAction(InfoPanel::Action act) {
  switch (act) {
    case InfoPanel::ACT_RELOAD:
      // like saving in the web panel: forgets skipped steps and loads everything again
      updateAllDataCallback();
      break;

    case InfoPanel::ACT_RESTART:
    case InfoPanel::ACT_ADD_WIFI: {
      bool addWifi = (act == InfoPanel::ACT_ADD_WIFI);
      if (addWifi) WifiService::requestPortalOnBoot();
      DataSyncService::clearSkipMask();   // after this restart every service is tried again
      displayService.showOLEDStatus({"\n", "\n", "Reiniciando...", "\n", addWifi ? "Despues abre la red" : "", addWifi ? SETUP_AP_SSID : ""}, true);
      Serial.println(addWifi ? "Info: reinicio para agregar WiFi" : "Info: reinicio pedido");
      Serial.flush();
      delay(900);
      ESP.restart();
      break;
    }

    case InfoPanel::ACT_SLEEP:
      sleepRequested = true;   // done from loop(), outside the state lock
      break;

    case InfoPanel::ACT_BRIGHTNESS:
      // 1 → 2 → … → 5 → 1, seen right away
      appState.config.brightness = appState.config.brightness % 5 + 1;
      displayService.setBrightness(appState.config.brightness);
      displayService.setContrast(false);
      configManager.saveConfig(appState.config);
      infoPanel.redraw(appState);
      break;

    case InfoPanel::ACT_SAVER:
      appState.config.power_saver = !appState.config.power_saver;
      configManager.saveConfig(appState.config);
      Serial.println(appState.config.power_saver ? "Ahorro: modo ahorro SI" : "Ahorro: modo ahorro NO");
      infoPanel.redraw(appState);
      break;

    default:
      break;
  }
}

void toggleAutoCycle() {
  appState.config.screen_auto_cycle = !appState.config.screen_auto_cycle;

  if (appState.config.screen_auto_cycle) {
    Serial.println("🔄 Auto Cycle: ENABLED");
    displayService.drawInfoScreen(icon_unlock, "Auto-ciclo SI");
  } else {
    Serial.println("🔒 Auto Cycle: DISABLED (Screen Locked)");
    displayService.drawInfoScreen(icon_lock, "Auto-ciclo NO");
  }
  displayService.display.display();

  configManager.saveConfig(appState.config);
  delay(900);
}


// The on-board RGB LED blinks while there is an unread message
void updateMessageLed(bool forceOff) {
  static bool ledOn = false;
  static uint32_t lastToggle = 0;
  bool want = false;
  if (!forceOff && !gOtaInProgress && messageService.unreadCount() > 0) {
    uint32_t phase = millis() % 1200;            // 2 short blinks every 1.2 s
    want = (phase < 120) || (phase > 240 && phase < 360);
  }
  if (want != ledOn || (forceOff && millis() - lastToggle > 1000)) {
    ledOn = want;
    lastToggle = millis();
    if (want) rgbLedWrite(MINI_RGB_LED_PIN, 40, 40, 40);   // white: same on any LED color order
    else rgbLedWrite(MINI_RGB_LED_PIN, 0, 0, 0);
  }
}

// A message arrived (from the web page): animation, then show it
void showIncomingMessage() {
  messageService.alertPending = false;
  gameService.pause();
  infoPanel.close();
  StateLock lock;
  int activeAction = TimeService::getActiveNightAction(appState.config);
  if (nightModeService.wasScreenOff(activeAction)) nightModeService.recordInteraction();
  lastUserInput = millis();   // the battery saver gives you time to read it
  saverDimmed = false;
  displayService.setContrast(false);
  displayService.playMessageAlert();
  messageInbox.openReader(0);
  displayService.showMessage(appState, 0);
  lastScreenSwitch = millis();
  nightModeService.recordInteraction();
}

// Returns true when the key was used by the Spotify screen
bool handleSpotifyInput(InputEvent event) {
  SpotifyState& sp = appState.spotify;
  bool active = (sp.status == SP_ACTIVE);
  uint32_t now = millis();

  if (displayService.isSpotifyVolumeMode()) {
    if (event == INPUT_UP || event == INPUT_DOWN) {
      int v = constrain((sp.volume < 0 ? 50 : sp.volume) + (event == INPUT_UP ? 10 : -10), 0, 100);
      sp.volume = v;   // shown right away; the board confirms on the next update
      displayService.spotifyVolume(true, v);
      spotifyService.send(SpotifyService::CMD_VOLUME, v);
      return true;
    }
    if (event == INPUT_TOUCH_TAP || event == INPUT_TOUCH_HOLD) {
      displayService.spotifyVolume(false);
      return true;
    }
    displayService.spotifyVolume(false);   // any other key closes it and acts normally
  }

  switch (event) {
    case INPUT_TOUCH_TAP:
      if (!active && sp.status != SP_IDLE) return true;
      if (active) {
        uint32_t prog = sp.progressMs + (sp.playing ? now - sp.fetchedAt : 0);
        sp.progressMs = prog;
        sp.fetchedAt = now;
        sp.playing = !sp.playing;
      } else {
        sp.playing = true;   // idle: try to resume on the last device
      }
      displayService.spotifyPop(sp.playing ? 1 : 2);
      spotifyService.send(SpotifyService::CMD_TOGGLE);
      return true;

    case INPUT_TOUCH_HOLD:
      if (!active) return true;
      if (sp.volume < 0) {
        sp.error = "Sin control de volumen";
        return true;
      }
      displayService.spotifyVolume(true, sp.volume);
      return true;

    case INPUT_RIGHT:
      if (!active) return false;
      displayService.spotifyPop(3, +1);
      spotifyService.send(SpotifyService::CMD_NEXT);
      return true;

    case INPUT_LEFT:
      if (!active) return false;
      displayService.spotifyPop(4, -1);
      spotifyService.send(SpotifyService::CMD_PREV);
      return true;

    default:
      return false;   // ↑ / ↓ move between screens as usual
  }
}

void handleInput(InputEvent event, bool repeat) {
  if (gOtaInProgress) return;

  // Battery saver: any key counts as activity. During the "going to sleep"
  // warning the key only cancels it.
  lastUserInput = millis();
  if (saverCountdown) {
    saverCountdown = false;
    nightModeService.recordInteraction();   // redraw the normal screen
    return;
  }
  if (saverDimmed && !nightModeService.isLatched()) {
    saverDimmed = false;
    StateLock lock;
    displayService.setContrast(false);   // back to the chosen brightness at once
  }

  if (event == INPUT_UP_LONG) {
    if (gameService.upHoldBlocked()) return;   // Pong / Asteroides: ↑ is a game key
    sleepRequested = true;   // done from loop(), outside the state lock
    return;
  }

  StateLock lock;

  if (!repeat && wakeIfScreenOff()) return;

  // Device panel open: the keys move inside it
  if (infoPanel.isOpen()) {
    InfoPanel::Action act = infoPanel.handleInput(event, appState);
    lastScreenSwitch = millis();
    if (!infoPanel.isOpen()) nightModeService.recordInteraction();   // redraw the screen
    runInfoAction(act);
    return;
  }

  // Unread message: any key counts as "opened" (the light stops) and does
  // nothing else. If the message is not on screen, it takes you to it.
  if (messageService.unreadCount() > 0) {
    messageService.markAllRead();
    updateMessageLed(true);
    if (displayService.getCurrentScreen() != SCREEN_MESSAGES) {
      messageInbox.openReader(0);
      displayService.showMessage(appState, 0);
    }
    lastScreenSwitch = millis();
    nightModeService.recordInteraction();
    return;
  }

  // Messages: inbox (list, reading, options to delete)
  if (displayService.getCurrentScreen() == SCREEN_MESSAGES && !displayService.isShowingDetails() && messageInbox.handleInput(event, appState.messages)) {
    lastScreenSwitch = millis();
    nightModeService.recordInteraction();
    return;
  }

  // Spotify screen: ← / → change song, A play/pause, A held = volume (↑ / ↓)
  if (displayService.getCurrentScreen() == SCREEN_SPOTIFY && handleSpotifyInput(event)) {
    lastScreenSwitch = millis();
    nightModeService.recordInteraction();
    return;
  }

  // Truco: ← / → add (tap) or remove (hold) a point, A held = new game
  if (displayService.getCurrentScreen() == SCREEN_TRUCO && !displayService.isShowingDetails() && trucoService.handleInput(event)) {
    lastScreenSwitch = millis();
    nightModeService.recordInteraction();
    return;
  }

  // Games screen: the menu, or every key while a game runs
  if (displayService.getCurrentScreen() == SCREEN_GAMES && gameService.handleInput(event)) {
    lastScreenSwitch = millis();
    nightModeService.recordInteraction();
    return;
  }

  switch (event) {
    case INPUT_UP:
      // ↑ = forward through the screens
      displayService.switchToNextScreen(appState, true);
      break;

    case INPUT_DOWN:
      // ↓ = back
      displayService.switchToPreviousScreen(appState, true);
      break;

    case INPUT_RIGHT:
      // → = more data about this screen (pages; after the last one, back to the screen)
      displayService.cycleDetail(appState);
      break;

    case INPUT_LEFT:
      // ← = next stock / crypto / currency / headline (wraps around);
      //     on screens without items it closes the details
      if (!displayService.cycleItem(appState) && !repeat) {
        displayService.closeDetails(appState);
      }
      break;

    case INPUT_TOUCH_TAP:
      if ((displayService.getCurrentScreen() == SCREEN_NEWS || displayService.getCurrentScreen() == SCREEN_MESSAGES) && !displayService.isShowingDetails()) break;   // news/messages: A only pauses
      infoPanel.open(appState);
      break;

    case INPUT_TOUCH_HOLD:
      if ((displayService.getCurrentScreen() == SCREEN_NEWS || displayService.getCurrentScreen() == SCREEN_MESSAGES) && !displayService.isShowingDetails()) break;   // holding A pauses the scroll
      toggleAutoCycle();
      break;
  }

  lastScreenSwitch = millis();
  nightModeService.recordInteraction();
}

// ---------------------------------------------------------------------------
//  Sleep
//  The ESP32-C3 can only wake from deep sleep with GPIO 0-5, and → (6) is
//  outside that range, so this uses light sleep: everything stays in RAM,
//  WiFi and the screen are switched off, and only ← or → wake it up right
//  where it was. The touch pad and ↓ do not wake it (in a pocket the touch pad
//  fires by itself).
// ---------------------------------------------------------------------------

const gpio_num_t WAKE_PINS_LOW[] = {(gpio_num_t)PIN_LEFT, (gpio_num_t)PIN_RIGHT};

void goToSleep() {
  Serial.println("Sleep: apagando...");
  gameService.pause();

  {
    StateLock lock;
    displayService.drawInfoScreen(nullptr, "Chau!");
    displayService.display.display();
  }

  // Wait until ↑ is released, or it would count as a key later
  uint32_t t0 = millis();
  while (digitalRead(PIN_UP) == LOW && millis() - t0 < 10000) delay(10);
  delay(400);

  // Let a running background update finish (it is using WiFi)
  t0 = millis();
  while ((dataSyncService.isBusy() || spotifyService.isBusy()) && millis() - t0 < 20000) delay(50);

  {
    StateLock lock;
    displayService.display.oled_command(0xAE);   // panel off
  }
  rgbLedWrite(MINI_RGB_LED_PIN, 0, 0, 0);

  wifiService.setPaused(true);
  ArduinoOTA.end();
  MDNS.end();
  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);

  // Wake sources: only ← and → (they go LOW)
  for (gpio_num_t pin : WAKE_PINS_LOW) {
    gpio_sleep_sel_dis(pin);                      // keep the pull-up while asleep
    gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL);
  }
  esp_sleep_enable_gpio_wakeup();

  Serial.flush();
  delay(20);

  esp_light_sleep_start();

  // ---- awake again ----
  for (gpio_num_t pin : WAKE_PINS_LOW) gpio_wakeup_disable(pin);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);

  Serial.println("Sleep: despierto");
  hardwareService.resync();   // the key that woke it does nothing else

  {
    StateLock lock;
    displayService.display.oled_command(0xAF);  // panel on
    displayService.setContrast(false);
    displayService.playBootAnimation();         // same intro as when it boots
    displayService.drawCurrentScreen(appState);
    displayService.display.display();
  }

  infoPanel.close();
  lastScreenSwitch = millis();
  lastUserInput = millis();
  saverDimmed = false;
  nightModeService.recordInteraction();

  // WiFi comes back in the background: the latest network first, and if it
  // is not around (you took it somewhere else) any other saved one. Not while
  // a game or the truco pad keeps it off to save battery.
  if (!radioSaving) {
    wifiService.setPaused(false);
    wifiService.reconnect();
  }
}

// --- Power saving while playing -----------------------------------------------
// Games and the truco pad do not need the network, and the WiFi is what uses
// most of the battery. It is turned off while a game runs and after 1 minute on
// the truco pad, and comes back by itself when you leave. Bluetooth is never
// turned on by this firmware, so it uses nothing.

bool radioSaveWanted() {
  int s = displayService.getCurrentScreen();
  if (s == SCREEN_GAMES && gameService.inGame()) return true;
  if (s == SCREEN_TRUCO && trucoScreenSince && millis() - trucoScreenSince >= TRUCO_RADIO_OFF_MS) return true;
  return false;
}

void updateRadioSaving() {
  if (displayService.getCurrentScreen() == SCREEN_TRUCO) {
    if (!trucoScreenSince) trucoScreenSince = millis();
  } else {
    trucoScreenSince = 0;
  }

  bool want = radioSaveWanted();
  if (want && !radioSaving) {
    // an update that is running finishes first (cutting it could hang it)
    if (dataSyncService.isBusy() || spotifyService.isBusy()) return;
    radioSaving = true;
    wifiService.setPaused(true);
    ArduinoOTA.end();
    MDNS.end();
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
    Serial.println("Ahorro: WiFi apagado");
    if (displayService.getCurrentScreen() == SCREEN_TRUCO) trucoService.notice("WiFi apagado: ahorro");
  } else if (!want && radioSaving) {
    radioSaving = false;
    wifiService.setPaused(false);
    wifiService.reconnect();   // onNetworkBack() runs when it connects
    Serial.println("Ahorro: WiFi de vuelta");
  }
}

// --- Battery saver (v1.12.0) ---------------------------------------------------
// Runs every loop: brightness level, CPU speed and WiFi power save.
void updatePowerSaver() {
  displayService.setBrightness(appState.config.brightness);
  bool saver = appState.config.power_saver;

  // 80 MHz is plenty for the screens and the network; games always get 160
  uint32_t mhz = (saver && !gameService.inGame()) ? 80 : 160;
  if (getCpuFrequencyMhz() != mhz) setCpuFrequencyMhz(mhz);

  // WiFi modem sleep: the deepest one with the saver (the radio sleeps between
  // beacons; the web panel answers a bit slower). Set again after each reconnect.
  static int appliedPs = -1;
  if (WiFi.status() == WL_CONNECTED) {
    if (appliedPs != (saver ? 1 : 0)) {
      WiFi.setSleep(saver ? WIFI_PS_MAX_MODEM : WIFI_PS_MIN_MODEM);
      appliedPs = saver ? 1 : 0;
    }
  } else {
    appliedPs = -1;
  }
}

// After 3 min without keys (and nobody using the web panel) it goes to sleep,
// like holding ↑. The last 5 s show a bar; any key cancels it. Returns true
// while the bar is on screen.
bool updateSaverSleep() {
  static uint32_t lastDraw = 0;
  uint32_t idle = idleMs();

  if (!appState.config.power_saver || idle < SAVER_SLEEP_MS - SAVER_WARN_MS) {
    if (saverCountdown) {
      saverCountdown = false;
      nightModeService.recordInteraction();
    }
    return false;
  }
  if (idle >= SAVER_SLEEP_MS) {
    saverCountdown = false;
    Serial.println("Ahorro: 3 min sin tocar, a dormir");
    sleepRequested = true;   // loop() does it on the next turn
    return true;
  }

  if (!saverCountdown) gameService.pause();   // a game left alone stays paused
  saverCountdown = true;
  if (millis() - lastDraw >= 100) {
    lastDraw = millis();
    uint32_t left = SAVER_SLEEP_MS - idle;
    StateLock lock;
    auto& d = displayService.display;
    displayService.setContrast(false);   // so it can be read
    d.clearDisplay();
    d.setFont();
    d.setTextSize(1);
    d.setTextWrap(false);
    d.setTextColor(SH110X_WHITE);
    d.setCursor(13, 8);
    d.print("Modo ahorro: me");
    d.setCursor(13, 18);
    d.print("duermo en " + String((left + 999) / 1000) + " s");
    d.drawRect(4, 32, 120, 10, SH110X_WHITE);
    int fill = (int)((116UL * left) / SAVER_WARN_MS);
    if (fill > 0) d.fillRect(6, 34, fill, 6, SH110X_WHITE);
    d.setCursor(16, 52);
    d.print("Toca una tecla");
    d.display();
  }
  return true;
}

// Each time the WiFi (re)connects: after a sleep, after a drop, or after
// moving to another saved network. mDNS (minic3.local), OTA and data again.
void onNetworkBack() {
  appState.config.ip_address = WiFi.localIP().toString();
  MDNS.end();
  if (MDNS.begin(DEVICE_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
  }
  ArduinoOTA.end();
  otaService.begin();
  dataSyncService.clearRetryBackoff();
  if (!initialLoadDone) fullSyncRequested = true;
  Serial.println("WiFi: de vuelta en " + WiFi.SSID() + ", " + appState.config.ip_address);
}

// While ↑ is held: a bar that fills up until it turns off
bool drawPowerOffBar() {
  static bool shown = false;
  static uint32_t lastDraw = 0;

  uint32_t held = gameService.upHoldBlocked() ? 0 : hardwareService.upHeldMs();
  const uint32_t showAfter = 700;

  if (held > showAfter) {
    if (millis() - lastDraw >= 50) {
      lastDraw = millis();
      int pct = (int)(((held - showAfter) * 100UL) / (HardwareService::UP_SLEEP_MS - showAfter));
      StateLock lock;
      displayService.drawOtaProgress(pct, "Apagando...", "Solta para cancelar");
    }
    shown = true;
    return true;
  }

  if (shown) {
    shown = false;
    nightModeService.recordInteraction();   // redraw the normal screen
  }
  return false;
}

// ---------------------------------------------------------------------------
//  Data
// ---------------------------------------------------------------------------

bool isLoadingData() {
  return dataSyncService.isFullSyncRunning();
}

// Full data load. It runs in its own task; meanwhile this keeps the web
// panel / OTA alive and watches for "skip" (→ or A) and for a step that hangs.
void updateAllData() {
  // A background update may be running (e.g. after saving in the panel): let it end
  uint32_t t0 = millis();
  while (dataSyncService.isBusy() && millis() - t0 < 20000) {
    otaService.handle();
    delay(20);
  }

  nightModeService.reset();
  initialLoadDone = true;
  dataSyncService.startFullSync(appState);

  bool rightWas = true, touchWas = true;   // ignore a key already held when it starts
  uint32_t skipPressedAt = 0;
  int skipStep = STEP_NONE;

  while (dataSyncService.isFullSyncRunning()) {
    otaService.handle();
    {
      StateLock lock(0);   // only if the sync task is not using the state right now
      if (lock.locked) webServerService.handleClient();
    }
    if (gOtaInProgress) { delay(1); continue; }

    bool right = digitalRead(PIN_RIGHT) == LOW;
    bool touch = digitalRead(PIN_TOUCH) == HIGH;
    bool pressed = (right && !rightWas) || (touch && !touchWas);
    rightWas = right;
    touchWas = touch;

    int step = DataSyncService::currentStep;

    if (pressed && step != STEP_NONE && skipPressedAt == 0) {
      Serial.printf("Saltar: %s\n", DataSyncService::stepName(step));
      dataSyncService.requestSkip(step);
      skipPressedAt = millis();
      skipStep = step;
      StateLock lock;
      displayService.showOLEDStatus({"\n", "\n", "Salteando:", String(DataSyncService::stepName(step)), "\n", "Un momento..."}, true);
    }

    if (skipPressedAt != 0) {
      if (step != skipStep) {
        skipPressedAt = 0;                                   // it moved on by itself
      } else if (millis() - skipPressedAt > 4000) {
        DataSyncService::skipStepAndReboot(step);            // really stuck
      }
    }

    if (step != STEP_NONE && millis() - DataSyncService::stepStartedAt > DataSyncService::STEP_TIMEOUT_MS) {
      DataSyncService::skipStepAndReboot(step);              // watchdog
    }

    delay(10);
  }

  hardwareService.resync();   // the skip key does nothing else
  StateLock lock;
  displayService.jumpToFirstEnabledScreen(appState);
  lastScreenSwitch = millis();
  nightModeService.recordInteraction();
}

// Saved from the web panel: reload everything from loop(), not inside the
// web request (the request answers right away)
void updateAllDataCallback() {
  DataSyncService::clearSkipMask();
  fullSyncRequested = true;
}

// ---------------------------------------------------------------------------

void setup() {
  gStateMutex = xSemaphoreCreateRecursiveMutex();

  pcMonitorService.begin();
  delay(100);
  rgbLedWrite(MINI_RGB_LED_PIN, 0, 0, 0);

  configManager.loadConfig(appState.config);
  messageService.begin(&appState.messages);
  gameService.begin();
  trucoService.begin();
  DataSyncService::initSkipMask();

  // Fixed hardware: ignore any pins stored by older configs
  appState.config.sda_pin = PIN_SDA;
  appState.config.scl_pin = PIN_SCL;

  hardwareService.begin();

  displayService.begin(PIN_SDA, PIN_SCL);
  displayService.playBootAnimation();   // 4.5 s intro

  displayService.showOLEDStatus({"\n", "\n", "Mini C3", "v" FW_VERSION, "\n", "Config cargada"}, true);
  bambuService.begin(&appState.config, &appState.bambu);

  // --- WiFi -------------------------------------------------------------
  // Saved networks are kept apart from the settings: changing the WiFi never
  // erases screens, Spotify or messages. Touching A during the intro opens
  // the setup network to add one.
  bool addNetwork = displayService.introTouched || WifiService::takePortalRequest();
  wifiService.begin();
  bool online = false;

  if (!addNetwork && wifiService.count() > 0) {
    displayService.showOLEDStatus({"\n", "\n", "Conectando...", "\n", "\n", "Buscando WiFi"}, true);
    online = wifiService.connectBest(45000, [](const String& ssid) {
      displayService.showOLEDStatus({"\n", "\n", "Conectando a:", oledAscii(ssid), "\n", "Buscando WiFi"}, true);
    });
  }

  if (!online) {
    static bool portalForAdd;
    portalForAdd = addNetwork;
    WiFiManager wm;
    wm.setHostname(DEVICE_HOSTNAME);
    wm.setConnectTimeout(20);
    wm.setConnectRetries(2);
    wm.setConfigPortalTimeout(300);   // after 5 minutes it goes on without WiFi
    wm.setAPCallback([](WiFiManager* m) {
      displayService.showOLEDStatus({portalForAdd ? "Agregar WiFi" : "Sin WiFi conocido", "\n", "Conectate a la red:", SETUP_AP_SSID, "Clave:", SETUP_AP_PASS, "\n", "y abri 192.168.4.1"}, true);
    });
    WiFi.disconnect(false, false);   // stop retrying the saved ones while the setup network is up
    online = wm.startConfigPortal(SETUP_AP_SSID, SETUP_AP_PASS);
    if (online) wifiService.rememberCurrent();
  }

  appState.config.device_id = DEVICE_HOSTNAME;
  if (online) {
    String ipAddress = WiFi.localIP().toString();
    Serial.println("WiFi: conectado a " + WiFi.SSID() + ", IP " + ipAddress);
    appState.config.ip_address = ipAddress;
    displayService.showOLEDStatus({
        "WiFi conectado!",
        "",
        oledAscii(WiFi.SSID()),
        "IP: " + ipAddress,
        String(DEVICE_HOSTNAME) + ".local",
        "Cargando..."
    }, true);
    delay(2000);
  } else {
    Serial.println("WiFi: sin conexion, sigue sin WiFi");
    if (wifiService.count() > 0) {
      wifiService.reconnect();   // keeps looking for a saved network in the background
      displayService.showOLEDStatus({"\n", "Sin WiFi", "\n", "Sigo buscando tus", "redes guardadas."}, true);
    } else {
      displayService.showOLEDStatus({"\n", "Sin WiFi", "\n", "Para configurarlo:", "reinicia y toca A", "en la presentacion."}, true);
    }
    delay(2500);
  }

  // Web panel (also serves /ota) and OTA start BEFORE loading data, so an
  // update can always get in even if a data source misbehaves
  webServerService.setAppState(&appState);
  webServerService.begin();
  otaService.begin();
  spotifyService.begin(&appState.spotify);

  if (WiFi.status() == WL_CONNECTED) updateAllData();
  else displayService.jumpToFirstEnabledScreen(appState);
}

void loop() {
  // Network updates first: while one runs nothing else should touch the screen
  otaService.handle();

  {
    StateLock lock;
    webServerService.handleClient();
  }

  if (gOtaInProgress) {
    delay(1);
    return;
  }

  if (messageService.alertPending) showIncomingMessage();
  spotifyService.setOnScreen(displayService.getCurrentScreen() == SCREEN_SPOTIFY);
  updateMessageLed(false);

  bambuService.loop();

  // Inputs (the handlers take the state lock themselves). On the truco screen
  // ← / → act on release (tap) or when held, so holding can remove a point.
  hardwareService.setArrowHoldMode(displayService.getCurrentScreen() == SCREEN_TRUCO && !infoPanel.isOpen() && !displayService.isShowingDetails());
  hardwareService.tick();
  if (hardwareService.anyKeyDown()) lastUserInput = millis();   // a key held (games)
  updatePowerSaver();

  if (sleepRequested) {
    sleepRequested = false;
    goToSleep();
    return;
  }
  updateRadioSaving();
  if (wifiService.tick()) onNetworkBack();

  if (fullSyncRequested) {
    fullSyncRequested = false;
    updateAllData();
    return;
  }

  // Watchdog for the background updates: a step that hangs is skipped
  // (restarts the board, the skip lasts until it is powered off)
  int bgStep = DataSyncService::currentStep;
  if (bgStep != STEP_NONE && millis() - DataSyncService::stepStartedAt > DataSyncService::STEP_TIMEOUT_MS) {
    DataSyncService::skipStepAndReboot(bgStep);
  }

  if (pcMonitorService.handleSerial(appState)) {
    Serial.println("Config updated via USB! Saving and applying...");
    configManager.saveConfig(appState.config);
    updateAllData();
  }

  updateScreen();

  // Lets the background sync task grab the state lock when it needs it
  delay(1);
}

void updateScreen() {
  if (drawPowerOffBar()) return;
  if (updateSaverSleep()) return;

  // A game is running: it gets the screen to itself (~60 frames/s, only
  // what changed goes to the panel). The WiFi is off meanwhile (battery).
  if (gameService.inGame() && displayService.getCurrentScreen() == SCREEN_GAMES && !infoPanel.isOpen()) {
    if (!radioSaving && !radioSaveWanted()) dataSyncService.maybeStartBackgroundSync(appState, nightModeService.isLatched());
    nightModeService.recordInteraction();   // no dimming / night screen-off while playing
    lastScreenSwitch = millis();            // and no auto cycle
    StateLock lock;
    gameService.tickGame(displayService.display, hardwareService);
    return;
  }

  // 1. Night Latch Logic
  int activeAction = TimeService::getActiveNightAction(appState.config);
  bool justExitedNightMode = nightModeService.update(activeAction, displayService.isOnFirstEnabledScreen(appState));

  if (justExitedNightMode) {
    // If we are exiting mode 2 or 3 (display was off), reset to the main screen
    if (appState.config.night_action >= 2) {
      displayService.jumpToFirstEnabledScreen(appState);
    }
    lastScreenSwitch = millis();
  }

  // 2. Scheduled Data Refresh (Non-Blocking via FreeRTOS Task). Not while the
  // WiFi is off to save battery (games, truco pad).
  if (!radioSaving && !radioSaveWanted()) dataSyncService.maybeStartBackgroundSync(appState, nightModeService.isLatched());

  // Device panel: refreshes itself and closes after 20 s without keys
  if (infoPanel.isOpen()) {
    StateLock lock;
    if (infoPanel.tick(appState)) return;
    nightModeService.recordInteraction();   // closed: redraw the normal screen now
    lastScreenSwitch = millis();
  }

  // 3. Auto Screen Switching Logic (waits while a headline is being read)
  if (appState.config.screen_auto_cycle && !nightModeService.isLatched() && !displayService.isReadingNews(appState) && !(displayService.getCurrentScreen() == SCREEN_MESSAGES && messageInbox.isBusy()) && !displayService.isSpotifyVolumeMode() && !(displayService.getCurrentScreen() == SCREEN_TRUCO && trucoService.inProgress())) {
    unsigned long intervalMs = appState.config.screen_interval_sec * 1000;

    if (millis() - lastScreenSwitch >= intervalMs) {
      StateLock lock;
      displayService.switchToNextScreen(appState);
      lastScreenSwitch = millis();
    }
  }

  // 4. Screen Redraw & Visual Action Logic
  static bool screenClearedForNight = false;

  bool isTemporarilyAwake = nightModeService.isTemporarilyAwake(activeAction);
  bool shouldDrawScreen = !nightModeService.isScreenOffAction(activeAction) || isTemporarilyAwake;

  if (!shouldDrawScreen) {
    if (!screenClearedForNight) {
      displayService.display.clearDisplay();
      displayService.display.display();
      screenClearedForNight = true;
      Serial.println("💤 Night Mode: Display turned OFF to save power. Waiting for interaction or morning.");
    }
  } else {
    if (screenClearedForNight) {
      screenClearedForNight = false;
      Serial.println("💡 Night Mode: Display turned back ON.");
    }

    // News: the headline scrolls up by itself (hold A to pause)
    {
      StateLock lock;
      displayService.tickNews(appState, hardwareService.touchDown());
      if (displayService.getCurrentScreen() == SCREEN_MESSAGES && !displayService.isShowingDetails() && !infoPanel.isOpen()) {
        messageInbox.tick(displayService.display, appState.messages, hardwareService.touchDown());
      }
      displayService.tickSpotify(appState);
      if (displayService.getCurrentScreen() == SCREEN_GAMES && !displayService.isShowingDetails() && !infoPanel.isOpen() && !gameService.inGame()) {
        gameService.tickMenu(displayService.display);
      }
      if (displayService.getCurrentScreen() == SCREEN_TRUCO && !displayService.isShowingDetails() && !infoPanel.isOpen()) {
        trucoService.tick(displayService.display);
      }
    }

    unsigned long refreshInterval = nightModeService.getRefreshIntervalMs(activeAction);

    if (nightModeService.isRedrawDue(refreshInterval)) {
      // battery saver: dimmed after a while without keys
      bool saverDim = saverDimWanted();
      saverDimmed = saverDim;
      if (nightModeService.isLatched()) {
        if (activeAction == 1 || isTemporarilyAwake) {
          displayService.setContrast(true);
        } else if (activeAction == 0) {
          displayService.setContrast(saverDim);
        }
      } else {
        displayService.setContrast(saverDim);
      }

      StateLock lock;
      displayService.drawCurrentScreen(appState);
      displayService.display.display();
      nightModeService.markRedrawn();
    }
  }
}
