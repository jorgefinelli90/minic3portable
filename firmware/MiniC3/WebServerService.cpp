#include "WebServerService.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Update.h>

#include "IssService.h"
#include "JsonSerializer.h"
#include "MiniC3.h"
#include "OtaService.h"
#include "MessageService.h"
#include "SpotifyService.h"
#include "TimeService.h"
#include "WifiService.h"

extern MessageService messageService;
extern SpotifyService spotifyService;
extern WifiService wifiService;

// MiniC3.ino: true while the first full data load is running (state is being written)
bool isLoadingData();
#include "PcMonitorService.h"
#include "TimeService.h"
#include "WeatherService.h"
#include "zones.h"

WebServerService::WebServerService(int port, ConfigSaveCallback callback) : 
  server(port), saveCallback(callback) {}

void WebServerService::setAppState(AppState* appState) {
  state = appState;
}

void WebServerService::begin() {
  server.on("/", HTTP_GET, [this](){ this->handleRoot(); }); 
  server.on("/save", HTTP_POST, [this](){ this->handleSave(); });
  server.on("/update", HTTP_GET, [this](){ this->handleUpdate(); }); 
  server.on("/pc-stats", HTTP_POST, [this](){ this->handlePcStats(); });
  server.on("/usb-reconnect", HTTP_POST, [this](){ this->handleUsbReconnect(); });
  server.on("/ota", HTTP_GET, [this](){ this->handleOtaPage(); });
  server.on("/ota", HTTP_POST, [this](){ this->handleOtaDone(); }, [this](){ this->handleOtaUpload(); });
  server.on("/mensaje", HTTP_GET, [this](){ this->handleMessagePage(); });
  server.on("/mensaje", HTTP_POST, [this](){ this->handleMessagePost(); });
  server.on("/api/mensaje", HTTP_GET, [this](){ this->handleMessageStatus(); });
  server.on("/spotify", HTTP_GET, [this](){ this->handleSpotifyPage(); });
  server.on("/spotify/start", HTTP_POST, [this](){ this->handleSpotifyStart(); });
  server.on("/spotify/finish", HTTP_POST, [this](){ this->handleSpotifyFinish(); });
  server.on("/spotify/unlink", HTTP_POST, [this](){ this->handleSpotifyUnlink(); });
  server.on("/wifi", HTTP_GET, [this](){ this->handleWifiPage(); });
  server.on("/wifi/scan", HTTP_GET, [this](){ this->handleWifiScan(); });
  server.on("/wifi/add", HTTP_POST, [this](){ this->handleWifiAdd(); });
  server.on("/wifi/forget", HTTP_POST, [this](){ this->handleWifiForget(); });
  server.on("/wifi/connect", HTTP_POST, [this](){ this->handleWifiConnect(); });
  
  server.begin();
  Serial.println("WebServerService: HTTP Server started."); 
  
  String uniqueName = state->config.device_id;

  if (MDNS.begin(uniqueName.c_str())) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("WebServerService: mDNS Responder Started: http://%s.local\n", uniqueName.c_str());
  }
}

void WebServerService::handleClient() {
    server.handleClient();
}

volatile uint32_t gLastWebActivity = 0;   // v1.12.0: the battery saver does not sleep while the panel is in use

void WebServerService::handleRoot() {
  gLastWebActivity = millis();
  if (isLoadingData()) {
    server.send(503, "text/html",
      "<!doctype html><html lang='es'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<meta http-equiv='refresh' content='5'><title>Mini C3</title></head>"
      "<body style='font-family:system-ui;background:#000;color:#eee;text-align:center;padding:40px 16px'>"
      "<h2>Mini C3 est&aacute; cargando datos&hellip;</h2><p>Esta p&aacute;gina se recarga sola.</p>"
      "<p><a style='color:#fff' href='/ota'>Actualizar firmware (OTA)</a></p></body></html>");
    return;
  }

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(HTTP_OK, "text/html", ""); 

  String chunk;
  chunk.reserve(4096);
  auto add = [&](const String& str) {
    chunk += str;
    if (chunk.length() > 2048) {
      server.sendContent(chunk);
      chunk = "";
    }
  };

  Config& config = state->config;
  WeatherData& weather = state->weather;
  AirQualityData& aqi = state->aqi;
  DaylightData& daylight = state->daylight;
  MoonData& moon = state->moon;
  PopulationData& population = state->population;
  PcStats& pc = state->pc;
  PcMedia& media = state->media;
  
  bool weatherValid = !isnan(weather.temp);
  bool aqiValid = (aqi.aqi != -1);
  bool daylightValid = daylight.sunrise_mins != -1;
  bool moonValid = (moon.curphase != "N/A");
  bool issValid = IssService::isValid(state->iss);
  bool popValid = (population.world_pop_base != -1 || population.country_pop_base != -1);
  bool pcValid = pc.cpu_percent > 0.1;

  add("<html lang='es'><head><title>Mini C3 | Panel</title>");
  add("<meta name='viewport' content='width=device-width, initial-scale=1'><meta charset='UTF-8'>");
  add("<style>");
  add(":root { --base-bg: #000000; --base-surface: #111111; --base-primary: #ffffff; --base-text: #ffffff; ");
  add("--text-main: var(--base-text); --text-muted: color-mix(in srgb, var(--base-text) 55%, var(--base-bg) 45%); --text-faint: color-mix(in srgb, var(--base-text) 25%, var(--base-bg) 75%); --text-on-primary: var(--base-bg); ");
  add("--surface-main: var(--base-surface); --surface-hover: color-mix(in srgb, var(--base-surface) 92%, var(--base-text) 8%); --surface-active: color-mix(in srgb, var(--base-surface) 85%, var(--base-text) 15%); ");
  add("--border-subtle: color-mix(in srgb, var(--base-surface) 80%, var(--base-text) 20%); --border-strong: color-mix(in srgb, var(--base-primary) 60%, transparent); ");
  add("--primary-main: var(--base-primary); --primary-hover: color-mix(in srgb, var(--base-primary) 80%, var(--base-text) 20%); --primary-glow: color-mix(in srgb, var(--base-primary) 15%, transparent); --shadow-base: color-mix(in srgb, var(--base-bg) 90%, black 10%); ");
  add("--font-sans: system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; --font-tech: 'SF Mono', ui-monospace, 'Cascadia Code', 'Source Code Pro', Menlo, Consolas, monospace; }");
  
  add("* { box-sizing: border-box; } html, body { margin: 0; padding: 0; }");
  add("body { font-family: var(--font-sans); font-weight: 500; background-color: var(--base-bg); color: var(--text-main); padding: 20px; line-height: 1.6; }");
  add(".container { max-width: 800px; margin: 0 auto; }");
  add(".app-header { font-family: var(--font-tech); font-weight: 900; font-size: 2.8rem; color: var(--text-main); text-align: center; padding-bottom: 20px; letter-spacing: 2px; text-shadow: 0 0 20px var(--primary-glow); text-transform: uppercase; }");
  
  add(".mt-0 { margin-top: 0 !important; } .mt-25 { margin-top: 25px; } .mb-20 { margin-bottom: 20px; } .hidden { display: none !important; } hr { border: 0; border-top: 1px solid var(--border-subtle); margin: 20px 0; }");
  add(".panel, .tile, .telemetry-tile, .identity-box, .no-data-tile, select, input, .checkbox-wrapper, button, fieldset, .anim-grid, .sortable-list, .log-container { border-radius: 6px; }");
  
  add(".panel { background: var(--surface-main); border: 1px solid var(--border-subtle); padding: 25px; margin-bottom: 24px; box-shadow: 0 10px 30px var(--shadow-base); }");
  add(".header-panel { border: 1px solid var(--border-strong); box-shadow: 0 0 20px var(--primary-glow); }");
  add(".panel-title { font-family: var(--font-tech); font-weight: 800; color: var(--text-main); font-size: 1.25rem; letter-spacing: -0.5px; text-transform: uppercase; margin-top: 0; margin-bottom: 15px; }");
  
  add(".tile, .telemetry-tile { background: linear-gradient(145deg, var(--surface-hover), var(--surface-main)); border: 1px solid var(--border-subtle); padding: 20px; text-align: center; box-shadow: 0 4px 15px var(--shadow-base); transition: all 0.2s ease; }");
  add(".tile:hover { transform: translateY(-2px); border-color: var(--border-strong); box-shadow: 0 8px 25px var(--primary-glow); }");
  add(".tile-icon { font-size: 2.2rem; margin-bottom: 10px; }");
  add(".tile-label { font-size: 0.75rem; color: var(--text-muted); letter-spacing: 1px; text-transform: uppercase; margin-top: 5px; font-weight: 600; }");
  add(".tile-value { font-family: var(--font-tech); font-weight: 800; font-size: 1.6rem; color: var(--primary-main); line-height: 1.2; text-shadow: 0 0 10px var(--primary-glow); } .date-val { font-size: 1.1rem; }");
  
  add("#time-display { font-family: var(--font-tech); font-weight: 900; font-size: 4.5rem; text-align: center; color: var(--text-main); letter-spacing: -2px; text-shadow: 0 0 20px var(--primary-glow); }");
  add("#location-info { text-align: center; margin: 0; color: var(--text-muted); font-weight: 500; font-size: 1.1rem; }");
  add("#greetings-text { display: none; text-align: center; color: var(--primary-main); font-weight: 600; margin-top: 5px; margin-bottom: 20px; }");
  add(".identity-box { background: var(--surface-active); border: 1px solid var(--border-subtle); padding: 15px; margin-top: 20px; text-align: center; }");
  add(".id-text { font-family: var(--font-tech); font-weight: 800; font-size: 1.2rem; color: var(--text-main); margin-bottom: 4px; } .ip-text { font-family: var(--font-tech); font-size: 0.85rem; color: var(--text-muted); margin-bottom: 8px; }");
  add(".status-badge { font-size: 0.8rem; font-weight: 700; letter-spacing: 1px; text-transform: uppercase; text-align: center; } #status-text { color: var(--text-muted); } #tinytosh-link-status { color: var(--primary-main); }");
  add(".no-data-tile { background: var(--surface-active); border: 1px dashed var(--border-strong); padding: 30px; color: var(--primary-main); text-align: center; font-weight: 600; margin-top: 15px; grid-column: 1 / -1; }");
  
  add(".dashboard-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px; margin-top: 20px; } .dashboard-grid > div { min-width: 0; }");
  add(".section-label { font-size: 0.8rem; color: var(--text-muted); margin-bottom: 5px; display: block; letter-spacing: 1px; text-transform: uppercase; font-weight: 700;} label { display: block; margin-top: 15px; font-weight: 600; color: var(--text-main); } .help-text { font-size: 0.85em; color: var(--text-muted); margin-top: 6px; }");
  
  add("select, input[type='text'], input[type='number'], input[type='time'], input[type='color'] { display: block; width: 100% !important; padding: 12px 15px; margin: 8px 0; background-color: var(--surface-active); color: var(--text-main); border: 1px solid var(--border-subtle); font-family: var(--font-tech); font-size: 14px; font-weight: 600; transition: all 0.2s; text-transform: uppercase; }");
  add("select:focus, input:focus { border-color: var(--primary-main); outline: none; box-shadow: 0 0 0 3px var(--primary-glow); } input:disabled, select:disabled { opacity: 0.5; cursor: not-allowed; }");
  add("select { appearance: none; -webkit-appearance: none; background-image: url('data:image/svg+xml;utf8,<svg fill=\"%23ffffff\" height=\"24\" viewBox=\"0 0 24 24\" width=\"24\" xmlns=\"http://www.w3.org/2000/svg\"><path d=\"M7 10l5 5 5-5z\"/></svg>'); background-repeat: no-repeat; background-position: right 12px center; }");
  add("input[type='color'] { padding: 0; cursor: pointer; height: 45px; border: 2px solid var(--border-subtle); } input[type='color']::-webkit-color-swatch-wrapper { padding: 0; } input[type='color']::-webkit-color-swatch { border: none; }");
  
  add("input[type='checkbox'], input[type='radio'] { accent-color: var(--primary-main); cursor: pointer; width: 18px; height: 18px; }");
  add(".checkbox-wrapper { display: flex; align-items: center; margin-bottom: 20px; padding: 15px; background: var(--surface-hover); border: 1px solid var(--border-subtle); } .checkbox-wrapper label { margin-left: 12px; font-weight: 600; color: var(--text-main); font-size: 0.95rem; cursor: pointer; }");
  add(".checkbox-label { display: flex; align-items: center; gap: 10px; margin-top: 12px; cursor: pointer; font-weight: 600; } .radio-group { display: flex; gap: 20px; margin-top: 10px; } .radio-label { display: flex; align-items: center; gap: 8px; cursor: pointer; margin-top: 0; font-weight: 500; }");
  
  add("button { background-color: var(--primary-main); color: var(--text-on-primary); padding: 16px; border: none; cursor: pointer; margin-top: 8px; width: 100%; font-family: var(--font-tech); font-size: 1.1rem; font-weight: 800; text-transform: uppercase; letter-spacing: 1px; transition: all 0.2s ease; box-shadow: 0 4px 15px var(--primary-glow); }");
  add("button:hover { background-color: var(--primary-hover); transform: translateY(-1px); box-shadow: 0 6px 20px var(--primary-glow); } button:active { transform: translateY(1px); } button:disabled { opacity: 0.5; cursor: not-allowed; transform: none; box-shadow: none; }");
  add(".btn-secondary { background-color: var(--surface-active); color: var(--text-main); border: 1px solid var(--border-subtle); box-shadow: none; } .btn-secondary:hover { background-color: var(--primary-main); color: var(--text-on-primary); border-color: var(--primary-main); }");
  
  add("fieldset { border: 1px solid var(--border-strong); padding: 25px; margin-top: 20px; background: var(--surface-hover); } legend { font-family: var(--font-tech); color: var(--primary-main); font-weight: 800; padding: 0 12px; font-size: 0.9rem; text-transform: uppercase; letter-spacing: 1px; }");
  
  add(".anim-label { margin-top: 20px; margin-bottom: 10px; font-weight: 700; display: block; } .anim-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px; margin-bottom: 15px; padding: 20px; background: var(--surface-hover); border: 1px solid var(--border-subtle); } .anim-item { display: flex; align-items: center; gap: 8px; cursor: pointer; font-size: 0.95em; margin-top: 0; padding: 5px 0; font-weight: 500; }");
  
  add(".sortable-list { list-style: none; padding: 0; margin: 15px 0 0; border: 1px solid var(--border-subtle); background: var(--surface-main); overflow: hidden; } .sortable-item { background: var(--surface-active); border-bottom: 1px solid var(--border-subtle); padding: 15px 20px; display: flex; align-items: center; gap: 15px; cursor: grab; color: var(--text-main); font-weight: 600; transition: background 0.2s; } .sortable-item:active { cursor: grabbing; } .sortable-item:last-child { border-bottom: none; } .sortable-item.disabled { opacity: 0.4; background: var(--surface-main); }");
  add(".order-ctrl { display: flex; flex-direction: column; margin-right: 15px; gap: 4px; } .move-btn { cursor: pointer; color: var(--text-muted); font-size: 0.7rem; padding: 4px 8px; border: 1px solid var(--border-subtle); border-radius: 4px; background: var(--surface-main); transition: all 0.2s; } .move-btn:hover { color: var(--text-on-primary); background: var(--primary-main); border-color: var(--primary-main); } .sortable-item.disabled .order-ctrl { display: none; }");
  add(".update-footer { text-align: center; font-size: 0.8rem; color: var(--text-muted); margin-top: 20px; font-family: var(--font-tech); }");

  add(".multi-row { display: flex; gap: 10px; align-items: flex-end; margin-bottom: 12px; } .multi-row .input-wrapper { flex: 1; min-width: 0; } .multi-row .input-wrapper select { margin: 0; } .btn-remove { width: 45px !important; height: 45px !important; margin: 0 !important; padding: 0 !important; flex-shrink: 0; font-size: 1.5rem !important; display: flex; align-items: center; justify-content: center; background-color: var(--surface-active); color: var(--text-muted); border: 1px solid var(--border-subtle); box-shadow: none; font-family: var(--font-sans); font-weight: 400; } .btn-remove:hover { background-color: var(--primary-main); color: var(--text-on-primary); border-color: var(--primary-main); }");
  add("@media (max-width: 400px) { .dashboard-grid { grid-template-columns: 1fr; } #time-display { font-size: 3.5rem; } }");
  add("</style></head><body><div class='container'>");

  add("<div class='app-header'>Mini C3</div>");
  add("<div class='panel header-panel'><div id='time-display'>" + TimeService::getCurrentTimeShort(config.time_format) + "</div>"); 
  add("<h2 id='location-info'>📍 --, -- (--)</h2>"); 
  add("<div id='greetings-text' class='greetings-text'></div>");
  
  String pairedPc = config.active_pc_id;
  int lastDash = pairedPc.lastIndexOf(':');
  if (lastDash > 3) pairedPc = pairedPc.substring(0, lastDash);

  bool isPcPaired = (pairedPc != "" && (millis() - pc.last_update < 5000));
  String pcStatus = isPcPaired ? ("🔒 Paired to " + pairedPc) : "";
  String ipAddress = WiFi.localIP().toString();

  add("<div class='identity-box'>");
  add("<div class='id-text'>" + config.device_id + "</div>");
  add("<div class='ip-text'>IP: " + ipAddress + "</div>");
  add("<div id='pc-link-status' class='status-badge'>" + pcStatus + "</div>");
  add("</div></div>");

  add("<form method='get' action='/save'>");
  
  add("<div class='panel'><h3 class='panel-title'>Mini C3</h3>");
  add("<p class='help-text mt-0'>Firmware v" FW_VERSION " &middot; ESP32-C3-Zero &middot; OLED SH1106 1.3&quot;</p>");
  add("<p class='help-text'>OLED SDA 8 / SCL 9 &middot; &uarr; 3 &middot; &darr; 4 &middot; &larr; 5 &middot; &rarr; 6 &middot; t&aacute;ctil A 7 &middot; bater&iacute;a 0</p>");
  add("<p class='help-text'>&uarr;/&darr; cambian de pantalla &middot; &larr;/&rarr; recorren vuelos, acciones, cripto y monedas &middot; A: radar/detalle en vuelos, info en el resto &middot; A mantenido: auto-ciclo</p>");
  add("<a href='/wifi' style='display:inline-block;margin:12px 8px 0 0;padding:10px 16px;border-radius:10px;background:var(--accent);color:var(--bg);font-weight:700;text-decoration:none'>📶 WiFi</a>");
  add("<a href='/spotify' style='display:inline-block;margin:12px 8px 0 0;padding:10px 16px;border-radius:10px;background:var(--accent);color:var(--bg);font-weight:700;text-decoration:none'>🎵 Spotify</a>");
  add("<a href='/mensaje' style='display:inline-block;margin:12px 8px 0 0;padding:10px 16px;border-radius:10px;background:var(--accent);color:var(--bg);font-weight:700;text-decoration:none'>✉️ Mandar un mensaje</a>");
  add("<a href='/ota' style='display:inline-block;margin-top:12px;padding:10px 16px;border-radius:10px;background:var(--accent);color:var(--bg);font-weight:700;text-decoration:none'>Actualizar firmware (OTA)</a>");
  add("</div>");

  add("<div class='panel'><h3 class='panel-title'>Ajustes generales</h3>");
  add("<label class='mt-0'>Colores:</label>");
  add("<p class='help-text mt-0'>Cambiá los 4 colores base para recolorear todo el panel.</p>");
  add("<div class='dashboard-grid mt-0'>");
  add("  <div><label class='mt-0'>Fondo de la app:</label><input type='color' name='theme_bg' id='theme_bg' value='" + config.theme_bg + "'></div>");
  add("  <div><label class='mt-0'>Superficies / paneles:</label><input type='color' name='theme_card' id='theme_card' value='" + config.theme_card + "'></div>");
  add("  <div><label class='mt-0'>Color de acento:</label><input type='color' name='theme_accent' id='theme_accent' value='" + config.theme_accent + "'></div>");
  add("  <div><label class='mt-0'>Texto principal:</label><input type='color' name='theme_text' id='theme_text' value='" + config.theme_text + "'></div>");
  add("</div><hr>");

  add("<label>Actualizar datos cada (min):</label><input type='number' name='refresh_min' value='" + String(config.refresh_interval_min) + "'>");
  add("<label class='checkbox-label mt-0' style='margin-top: 10px !important;'><input type='checkbox' id='autoCycle' name='auto_cycle' value='1' " + String(config.screen_auto_cycle ? "checked" : "") + "> Cambiar de pantalla automáticamente</label>");
  add("<p class='help-text mt-0'>Si está desactivado, las pantallas solo cambian con los botones.</p>");
  add("<label>Cambiar de pantalla cada (seg):</label><input type='number' id='screenIntInput' name='screen_int' value='" + String(config.screen_interval_sec) + "'>");
  add("<label>Brillo de la pantalla: <b id='brightnessVal'>" + String(config.brightness) + "</b> de 5</label><input type='range' min='1' max='5' step='1' id='brightness' name='brightness' value='" + String(config.brightness) + "' oninput=\"document.getElementById('brightnessVal').innerText=this.value\" style='width:100%'>");
  add("<label class='checkbox-label mt-0' style='margin-top: 10px !important;'><input type='checkbox' id='powerSaver' name='power_saver' value='1' " + String(config.power_saver ? "checked" : "") + "> Modo ahorro de batería</label>");
  add("<p class='help-text mt-0'>Con el modo ahorro, la pantalla baja al mínimo a los 20 s sin tocar nada y la consola se suspende sola a los 3 minutos (la despiertan ← o →). Además el WiFi y el procesador van más despacio cuando no estás jugando. También se cambia en la consola: tocá A, página Ajustes.</p>");

  add("<label class='anim-label'>Animaciones activas:</label>");
  add("<p class='help-text mt-0' style='margin-bottom: 10px;'>Si destildás 'Ninguna', elegí qué animaciones usar.</p>");
  add("<div class='anim-grid'>");
  
  const char* animLabels[] = {
    "🚫 Ninguna", "↔️ Deslizar horizontal", "↕️ Deslizar vertical", "👾 Disolver (ruido)", "🎭 Telón", "🎹 Persiana"
  };

  add("<input type='hidden' id='finalMask' name='anim_mask' value='" + String(config.anim_mask) + "'>");

  bool isNone = (config.anim_mask == 0);
  add("<label class='anim-item'>"); 
  add("<input type='checkbox' id='animNone' " + String(isNone ? "checked" : "") + ">" + String(animLabels[0]) + "</label>");

  for (int i = 1; i <= 5; i++) {
    bool isSet = (config.anim_mask & (1 << i));
    String checked = isSet ? "checked" : "";
    add("<label class='anim-item'><input type='checkbox' class='anim-chk' value='" + String(1 << i) + "' " + checked + ">" + String(animLabels[i]) + "</label>");
  }
  add("</div>");

  add("<label>Formato de hora:</label><div class='radio-group'>");
  add("<label class='radio-label'><input type='radio' name='time_format' value='24' " + String(config.time_format == "24" ? "checked" : "") + "> 24 horas</label>");
  add("<label class='radio-label'><input type='radio' name='time_format' value='12' " + String(config.time_format == "12" ? "checked" : "") + "> 12 horas</label></div>");
  add("<p class='help-text'>El formato vale para la pantalla y para este panel.</p>");

  add("<hr>");

  add("<label class='checkbox-label mt-0'><input type='checkbox' id='autoDetect' name='auto_detect' value='1' " + String(config.auto_detect ? "checked" : "") + "> Detectar ubicación automáticamente (por IP)</label>");
  add("<p class='help-text mt-0'>Usa tu IP para averiguar ciudad, coordenadas y zona horaria.</p>");

  add("<fieldset id='manualFields' class='collapsible'>");
  add("<legend>Ubicación manual</legend>");
  add("<label>Ciudad:</label><input type='text' name='city' value='" + config.city + "'>");
  
  add("<div class='dashboard-grid mt-0'>");
  add("  <div><label class='mt-0'>Latitud:</label><input type='number' step='any' name='latitude' value='" + String(config.latitude, 4) + "'></div>");
  add("  <div><label class='mt-0'>Longitud:</label><input type='number' step='any' name='longitude' value='" + String(config.longitude, 4) + "'></div>");
  add("</div>");

  add("<div class='dashboard-grid mt-0'>");
  add("  <div><label class='mt-0'>País:</label><select name='country_code'>");
  for(auto c : allCountries) {
      add("<option value='" + String(c.code) + "'" + (String(config.country_code) == String(c.code) ? " selected" : "") + ">" + String(c.name) + "</option>");
  }
  add("  </select></div>");

  add("  <div><label class='mt-0'>Zona horaria:</label><select name='timezone'>");
  
  const char* tzCursor = POSIX_TIMEZONE_MAP;
  while ((tzCursor = strchr(tzCursor, '"')) != nullptr) {
      const char* keyStart = tzCursor + 1;
      const char* keyEnd = strchr(keyStart, '"');
      if (!keyEnd) break;

      const char* colon = keyEnd + 1;
      while (*colon == ' ' || *colon == '\t') colon++;
      if (*colon != ':') {
          tzCursor = keyEnd + 1;
          continue;
      }

      String key;
      key.reserve(keyEnd - keyStart);
      for (const char* p = keyStart; p < keyEnd; p++) key += *p;
      add("<option value='" + key + "'" + (key == config.timezone ? " selected" : "") + ">" + key + "</option>");

      const char* valueStart = strchr(colon, '"');
      if (!valueStart) break;
      const char* valueEnd = strchr(valueStart + 1, '"');
      if (!valueEnd) break;
      tzCursor = valueEnd + 1;
  }
  
  add("  </select></div>");
  add("</div></fieldset><hr>");
  
  add("<label class='checkbox-label mt-0'><input type='checkbox' id='nightMode' name='night_mode' value='1' " + String(config.night_mode ? "checked" : "") + "> Activar modo noche</label>");
  add("<p class='help-text mt-0'>Definí un horario tranquilo para pausar animaciones, bajar el brillo o ahorrar consultas.</p>");

  add("<fieldset id='nightFields' class='collapsible'>");
  add("<legend>Horario nocturno</legend>");
  add("<label class='mt-0'>Qué hace la pantalla:</label><select name='night_action' id='nightActionSelect' style='margin-top: 8px;'>");
  add("<option value='0' " + String(config.night_action == 0 ? "selected" : "") + ">Sin cambio visual</option>");
  add("<option value='1' " + String(config.night_action == 1 ? "selected" : "") + ">Bajar el brillo</option>");
  add("<option value='2' " + String(config.night_action == 2 ? "selected" : "") + ">Apagar la pantalla</option>");
  add("<option value='3' " + String(config.night_action == 3 ? "selected" : "") + ">Bajar brillo y después apagar</option>");
  add("</select>");

  add("<div id='dimStartContainer' style='display: none;'>");
  add("  <label>Bajar brillo desde:</label><input type='time' name='night_dim_start' value='" + String(config.night_dim_start) + "'>");
  add("</div>");

  add("<div class='dashboard-grid'>"); 
  add("  <div><label class='mt-0'>Desde:</label><input type='time' name='night_start' value='" + String(config.night_start) + "'></div>");
  add("  <div><label class='mt-0'>Hasta:</label><input type='time' name='night_end' value='" + String(config.night_end) + "'></div>");
  add("</div></fieldset></div>");

  add("<div class='panel'><h3 class='panel-title'>Orden de las pantallas</h3>");
  add("<p class='help-text mt-0' style='margin-bottom: 15px;'>Arrastrá para reordenar. Las pantallas desactivadas quedan abajo.</p>");
  add("<ul id='sortable-list' class='sortable-list'>");
  
  for (int screenId = 0; screenId < NUM_SCREENS; screenId++) {
    String targetId = "";
    switch(screenId) {
      case SCREEN_TIME: targetId = "showTime"; break;
      case SCREEN_CALENDAR: targetId = "showCalendar"; break;
      case SCREEN_WEATHER: targetId = "showWeather"; break;
      case SCREEN_AIR_QUALITY: targetId = "showAQI"; break;
      case SCREEN_DAYLIGHT: targetId = "showDaylight"; break;
      case SCREEN_MOON: targetId = "showMoon"; break;
      case SCREEN_ISS: targetId = "showIss"; break;
      case SCREEN_FLIGHT: targetId = "showFlight"; break;
      case SCREEN_POPULATION: targetId = "showPopulation"; break;
      case SCREEN_CRYPTO: targetId = "showCrypto"; break;
      case SCREEN_CURRENCY: targetId = "showCurrency"; break;
      case SCREEN_STOCK: targetId = "showStock"; break;
      case SCREEN_PC_MONITOR: targetId = "showPc"; break;
      case SCREEN_PC_MEDIA: targetId = "showMedia"; break;
      case SCREEN_BAMBU: targetId = "showBambu"; break;
      case SCREEN_NEWS: targetId = "showNews"; break;
      case SCREEN_MESSAGES: targetId = "showMessages"; break;
      case SCREEN_SPOTIFY: targetId = "showSpotify"; break;
      case SCREEN_GAMES: targetId = "showGames"; break;
      case SCREEN_TRUCO: targetId = "showTruco"; break;
    }
    
    // Mini C3 v1.4.1: air quality and population were removed (kept hidden so the order stays complete)
    bool removedScreen = (screenId == SCREEN_AIR_QUALITY || screenId == SCREEN_POPULATION || screenId == SCREEN_FLIGHT);
    add("<li class='sortable-item' data-id='" + String(screenId) + "' data-target='" + targetId + "' draggable='true'" + String(removedScreen ? " style='display:none'" : "") + ">");
    add("<span class='drag-handle'>☰</span>" + String(SCREEN_NAMES[screenId]) + "</li>");
  }
  add("</ul><input type='hidden' name='screen_order' id='screenOrderInput' value=''></div>");

  add("<div id='dynamic-panels-container'>");
  for (int i = 0; i < NUM_SCREENS; i++) {
      int screenId = config.screen_order[i];

      switch (screenId) {
          case SCREEN_TIME: {
              add("<div class='panel' id='panel-" + String(screenId) + "'>");
              add("<label class='checkbox-label mt-0'><input type='checkbox' id='showTime' name='show_time' value='1' " + String(config.show_time ? "checked" : "") + "> Pantalla de hora</label>");
              add("<div id='timeContent' class='collapsible'>");
              
              add("<div class='dashboard-grid'>");
              add("<div class='tile'><div class='tile-icon'>🕒</div><div class='tile-value' id='preview-time'>" + TimeService::getCurrentTimeShort(config.time_format) + "</div><div class='tile-label'>Hora</div></div>");
              add("<div class='tile'><div class='tile-icon'>🌐</div><div class='tile-value' id='preview-tz' style='font-size:1.2rem'>" + config.timezone + "</div><div class='tile-label'>Zona horaria</div></div>");
              add("</div>");
              
              add("<label class='checkbox-label'><input type='checkbox' name='date_display' value='1' " + String(config.date_display ? "checked" : "") + "> Mostrar la fecha debajo de la hora</label>");
              add("</div></div>");
              break;
          }

          case SCREEN_CALENDAR: {
              add("<div class='panel' id='panel-" + String(screenId) + "'>");
              add("<label class='checkbox-label mt-0'><input type='checkbox' id='showCalendar' name='show_calendar' value='1' " + String(config.show_calendar ? "checked" : "") + "> Pantalla de calendario</label>");
              add("<div id='calendarContent' class='collapsible'>");
              
              String holText = (state->calendar.count > 0) ? String(state->calendar.count) : "Sin feriados";
              
              add("<div class='dashboard-grid'>");
              add("<div class='tile'><div class='tile-icon'>📅</div><div class='tile-value date-val' id='preview-date'>" + TimeService::getFullDate() + "</div><div class='tile-label'>Fecha</div></div>");
              add("<div class='tile'><div class='tile-icon'>🎉</div><div class='tile-value' id='preview-hol' style='font-size:1.2rem'>" + holText + "</div><div class='tile-label'>Feriados cargados</div></div>");
              add("</div>");
              
              add("<label>La semana empieza el:</label><div class='radio-group'>");
              add("<label class='radio-label'><input type='radio' name='cal_start' value='mon' " + String(config.calendar_start_day == "mon" ? "checked" : "") + "> Lunes</label>");
              add("<label class='radio-label'><input type='radio' name='cal_start' value='sun' " + String(config.calendar_start_day == "sun" ? "checked" : "") + "> Domingo</label></div>");
              
              add("<label class='checkbox-label'><input type='checkbox' name='cal_hol' value='1' " + String(config.calendar_show_holidays ? "checked" : "") + "> Mostrar feriados nacionales</label>");
              add("<label class='checkbox-label'><input type='checkbox' name='cal_min' value='1' " + String(config.calendar_minimal ? "checked" : "") + "> Modo minimalista (sin grilla)</label>");
              add("</div></div>");
              break;
          }
          
          case SCREEN_WEATHER: {
              add("<div class='panel' id='panel-" + String(screenId) + "'>");
              add("<label class='checkbox-label mt-0'><input type='checkbox' id='showWeather' name='show_weather' value='1' " + String(config.show_weather ? "checked" : "") + "> Pantalla de clima</label>");
              add("<div id='weatherContent' class='collapsible'>");
              
              if (!weatherValid) {
                  add("<div id='weather-no-data' class='no-data-tile'>☁️ El clima aparece después de actualizar</div><div id='weather-grid' class='hidden'>");
              } else {
                  add("<div id='weather-no-data' class='no-data-tile hidden'>☁️ El clima aparece después de actualizar</div><div id='weather-grid'>");
              }
              
              add("<div class='dashboard-grid'>");
              add("<div class='tile'><div class='tile-icon' id='icon-temp'>" + WeatherService::getWeatherIcon(weather.weather_code) + "</div><div class='tile-value' id='value-temp'>" + String(weather.temp, 1) + " °" + config.temp_unit + "</div><div class='tile-label'>Temperatura</div></div>");
              {
                  const char* weatherPreview[][3] = {{"feels", "🤒", "Sensación térmica"}, {"humidity", "💧", "Humedad"}, {"wind", "💨", "Viento"}, {"precipitation", "🌧️", "Lluvia"}, {"pressure", "📊", "Presión"}, {"visibility", "👁️", "Visibilidad"}};
                  for (int i = 0; i < 6; i++) {
                      bool selected = false;
                      for (int j = 0; j < 6; j++) if (config.weather_values[j] == weatherPreview[i][0]) selected = true;
                      if (!selected) continue;
                      String key = weatherPreview[i][0];
                      String val;
                      if (key == "feels") val = String(weather.apparent_temperature, 1) + " °" + config.temp_unit;
                      else if (key == "humidity") val = String(weather.humidity) + "%";
                      else if (key == "wind") val = String(weather.wind_speed, 1) + " km/h";
                      else if (key == "precipitation") val = String(weather.precipitation_probability, 0) + "%";
                      else if (key == "pressure") val = String(weather.pressure, 0) + " hPa";
                      else if (key == "visibility") val = String(weather.visibility / 1000.0, 1) + " km";
                      add("<div class='tile'><div class='tile-icon'>" + String(weatherPreview[i][1]) + "</div><div class='tile-value' id='value-" + key + "'>" + val + "</div><div class='tile-label'>" + String(weatherPreview[i][2]) + "</div></div>");
                  }
              }
              add("</div><div class='update-footer' id='weather-upd'>Actualizado: " + weather.update_time + "</div></div>");
              
              add("<label>Unidad de temperatura:</label><div class='radio-group'>");
              add("<label class='radio-label'><input type='radio' name='temp_unit' value='C' " + String(config.temp_unit == "C" ? "checked" : "") + "> °C</label>");
              add("<label class='radio-label'><input type='radio' name='temp_unit' value='F' " + String(config.temp_unit == "F" ? "checked" : "") + "> °F</label></div>");
              
              add("<label class='checkbox-label'><input type='checkbox' name='round_temps' value='1' " + String(config.round_temps ? "checked" : "") + "> Redondear temperaturas</label>");

              add("<label>Diseño:</label><div class='radio-group'>");
              add("<label class='radio-label'><input type='radio' name='weather_show_header' value='1' onchange='updateValueLimits(\"weather\", 6)' " + String(config.weather_show_header ? "checked" : "") + "> Con encabezado (lugar y hora)</label>");
              add("<label class='radio-label'><input type='radio' name='weather_show_header' value='0' onchange='updateValueLimits(\"weather\", 6)' " + String(!config.weather_show_header ? "checked" : "") + "> Sin encabezado (más datos)</label></div>");

              {
                  const char* weatherOptions[][2] = {{"feels", "Sensación térmica"}, {"humidity", "Humedad"}, {"wind", "Viento"}, {"precipitation", "Probabilidad de lluvia"}, {"pressure", "Presión"}, {"visibility", "Visibilidad"}};
                  add("<label>Extra Values (" + String(config.weather_show_header ? "up to 3" : "up to 6") + "):</label><div class='radio-group' style='flex-wrap:wrap; row-gap:8px;'>");
                  for (int i = 0; i < 6; i++) {
                      bool checked = false;
                      for (int j = 0; j < 6; j++) if (config.weather_values[j] == weatherOptions[i][0]) checked = true;
                      add("<label class='checkbox-label'><input type='checkbox' class='weather-val-chk' data-key='" + String(weatherOptions[i][0]) + "' name='wv_" + String(weatherOptions[i][0]) + "' onchange='updateValueLimits(\"weather\", 6)' " + String(checked ? "checked" : "") + "> " + String(weatherOptions[i][1]) + "</label>");
                  }
                  add("</div>");
              }
              add("<hr>");
              add("<label class='checkbox-label' id='customWeatherSyncLbl'><input type='checkbox' id='customWeatherSyncChk' name='custom_weather_sync_ui' value='1' " + String(config.custom_weather_int_min > 0 ? "checked" : "") + "> Actualización propia</label>");
              add("<div id='customWeatherSyncFields' class='collapsible" + String(config.custom_weather_int_min > 0 ? "" : " hidden") + "'>");
              add("<label class='mt-0'>Actualizar cada (min):</label><input type='number' min='1' id='customWeatherSyncInt' name='custom_weather_int_min' value='" + String(config.custom_weather_int_min > 0 ? config.custom_weather_int_min : config.refresh_interval_min) + "'>");
              add("</div>");
              add("</div></div>");
              break;
          }

          case SCREEN_AIR_QUALITY: {
              add("<div class='panel' id='panel-" + String(screenId) + "' style='display:none'>");
              add("<label class='checkbox-label mt-0'><input type='checkbox' id='showAQI' name='show_aqi' value='1' " + String(config.show_aqi ? "checked" : "") + "> Pantalla de calidad del aire</label>");
              add("<div id='aqiContent' class='collapsible'>");

              if (!aqiValid) {
                  add("<div id='aqi-no-data' class='no-data-tile'>🍃 La calidad del aire aparece después de actualizar</div><div id='aqi-grid' class='hidden'>");
              } else {
                  add("<div id='aqi-no-data' class='no-data-tile hidden'>🍃 La calidad del aire aparece después de actualizar</div><div id='aqi-grid'>");
              }
              
              add("<div class='dashboard-grid'>");
              add("<div class='tile'><div class='tile-icon'>🍃</div><div class='tile-value' id='value-aqi'>" + String(aqi.aqi) + "</div><div class='tile-label'>" + "Aire: " + aqi.status + "</div></div>");
              {
                  const char* aqiPreview[][3] = {{"pm25", "🌫️", "PM 2.5"}, {"pm10", "🏭", "PM 10"}, {"no2", "🧪", "Dióxido de nitrógeno"}, {"co", "🚗", "Monóxido de carbono"}, {"co2", "🏗️", "Dióxido de carbono"}, {"so2", "🌋", "Dióxido de azufre"}, {"o3", "☀️", "Ozono"}, {"dust", "🏜️", "Polvo"}, {"uv", "🕶️", "Índice UV"}, {"ch4", "🐄", "Metano"}};
                  for (int i = 0; i < 10; i++) {
                      bool selected = false;
                      for (int j = 0; j < 6; j++) if (config.aqi_values[j] == aqiPreview[i][0]) selected = true;
                      if (!selected) continue;
                      String key = aqiPreview[i][0];
                      float rawVal;
                      String unit = " <small>µg</small>";
                      if (key == "pm25") rawVal = aqi.pm25;
                      else if (key == "pm10") rawVal = aqi.pm10;
                      else if (key == "no2") rawVal = aqi.no2;
                      else if (key == "co") rawVal = aqi.co;
                      else if (key == "co2") { rawVal = aqi.co2; unit = " <small>ppm</small>"; }
                      else if (key == "so2") rawVal = aqi.so2;
                      else if (key == "o3") rawVal = aqi.o3;
                      else if (key == "dust") rawVal = aqi.dust;
                      else if (key == "uv") { rawVal = aqi.uv; unit = ""; }
                      else { rawVal = aqi.ch4; unit = " <small>ppb</small>"; }
                      add("<div class='tile'><div class='tile-icon'>" + String(aqiPreview[i][1]) + "</div><div class='tile-value' id='value-" + key + "'>" + String(rawVal, 1) + unit + "</div><div class='tile-label'>" + String(aqiPreview[i][2]) + "</div></div>");
                  }
              }
              add("</div><div class='update-footer' id='aqi-upd'>Actualizado: " + weather.update_time + "</div></div>");

              add("<label>Escala de calidad del aire:</label><div class='radio-group'>");
              add("<label class='radio-label'><input type='radio' name='aqi_type' value='US' " + String(config.aqi_type == "US" ? "checked" : "") + "> EE. UU.</label>");
              add("<label class='radio-label'><input type='radio' name='aqi_type' value='EU' " + String(config.aqi_type == "EU" ? "checked" : "") + "> Europea</label></div>");
              add("<p class='help-text mt-0'>Europea: escala 0-100+ | EE. UU.: escala 0-500</p>");

              add("<label>Diseño:</label><div class='radio-group'>");
              add("<label class='radio-label'><input type='radio' name='aqi_show_header' value='1' onchange='updateValueLimits(\"aqi\", 6)' " + String(config.aqi_show_header ? "checked" : "") + "> Con encabezado (lugar y hora)</label>");
              add("<label class='radio-label'><input type='radio' name='aqi_show_header' value='0' onchange='updateValueLimits(\"aqi\", 6)' " + String(!config.aqi_show_header ? "checked" : "") + "> Sin encabezado (más datos)</label></div>");

              {
                  const char* aqiOptions[][2] = {{"pm25", "PM 2.5"}, {"pm10", "PM 10"}, {"no2", "Dióxido de nitrógeno"}, {"co", "Monóxido de carbono"}, {"co2", "Dióxido de carbono"}, {"so2", "Dióxido de azufre"}, {"o3", "Ozono"}, {"dust", "Polvo"}, {"uv", "Índice UV"}, {"ch4", "Metano"}};
                  add("<label>Extra Values (" + String(config.aqi_show_header ? "up to 3" : "up to 6") + "):</label><div class='radio-group' style='flex-wrap:wrap; row-gap:8px;'>");
                  for (int i = 0; i < 10; i++) {
                      bool checked = false;
                      for (int j = 0; j < 6; j++) if (config.aqi_values[j] == aqiOptions[i][0]) checked = true;
                      add("<label class='checkbox-label'><input type='checkbox' class='aqi-val-chk' data-key='" + String(aqiOptions[i][0]) + "' name='av_" + String(aqiOptions[i][0]) + "' onchange='updateValueLimits(\"aqi\", 6)' " + String(checked ? "checked" : "") + "> " + String(aqiOptions[i][1]) + "</label>");
                  }
                  add("</div>");
              }
              add("<hr>");
              add("<label class='checkbox-label' id='customAqiSyncLbl'><input type='checkbox' id='customAqiSyncChk' name='custom_aqi_sync_ui' value='1' " + String(config.custom_aqi_int_min > 0 ? "checked" : "") + "> Actualización propia</label>");
              add("<div id='customAqiSyncFields' class='collapsible" + String(config.custom_aqi_int_min > 0 ? "" : " hidden") + "'>");
              add("<label class='mt-0'>Actualizar cada (min):</label><input type='number' min='1' id='customAqiSyncInt' name='custom_aqi_int_min' value='" + String(config.custom_aqi_int_min > 0 ? config.custom_aqi_int_min : config.refresh_interval_min) + "'>");
              add("</div>");
              add("</div></div>");
              break;
          }

          case SCREEN_DAYLIGHT: {
              add("<div class='panel' id='panel-" + String(screenId) + "'>");
              add("<label class='checkbox-label mt-0'><input type='checkbox' id='showDaylight' name='show_daylight' value='1' " + String(config.show_daylight ? "checked" : "") + "> Pantalla de sol (amanecer y atardecer)</label>");
              add("<div id='daylightContent' class='collapsible'>");

              if (!daylightValid) {
                  add("<div id='daylight-no-data' class='no-data-tile'>☀️ Los datos del sol aparecen después de actualizar</div><div id='daylight-grid' class='hidden'>");
              } else {
                  add("<div id='daylight-no-data' class='no-data-tile hidden'>☀️ Los datos del sol aparecen después de actualizar</div><div id='daylight-grid'>");
              }
              
              add("<div class='dashboard-grid'>");
              add("<div class='tile'><div class='tile-icon'>🌅</div><div class='tile-value' id='val-sunrise'>" + TimeService::formatMinsFromMidnight(daylight.sunrise_mins, config.time_format) + "</div><div class='tile-label'>Amanecer</div></div>");
              add("<div class='tile'><div class='tile-icon'>🌇</div><div class='tile-value' id='val-sunset'>" + TimeService::formatMinsFromMidnight(daylight.sunset_mins, config.time_format) + "</div><div class='tile-label'>Atardecer</div></div>");
              add("<div class='tile'><div class='tile-icon'>☀️</div><div class='tile-value' id='val-noon'>" + TimeService::formatMinsFromMidnight(daylight.noon_mins, config.time_format) + "</div><div class='tile-label'>Mediodía solar</div></div>");
              add("<div class='tile'><div class='tile-icon'>⏱️</div><div class='tile-value' id='val-length'>" + TimeService::formatDurationMins(daylight.length_mins) + "</div><div class='tile-label'>Duración del día</div></div>");
              add("</div></div>");
              
              add("<label class='checkbox-label'><input type='checkbox' name='daylight_min' value='1' " + String(config.daylight_minimal ? "checked" : "") + "> Modo minimalista (sin línea de tiempo)</label>");
              add("</div></div>");
              break;
          }

          case SCREEN_MOON: {
            add("<div class='panel' id='panel-" + String(screenId) + "'>");
            add("<label class='checkbox-label mt-0'><input type='checkbox' id='showMoon' name='show_moon' value='1' " + String(config.show_moon ? "checked" : "") + "> Pantalla de luna</label>");
            add("<div id='moonContent' class='collapsible'>");
            if (!moonValid) {
              add("<div id='moon-no-data' class='no-data-tile'>  Los datos de la luna aparecen después de actualizar</div><div id='moon-grid' class='hidden'>");
            } else {
              add("<div id='moon-no-data' class='no-data-tile hidden'>  Los datos de la luna aparecen después de actualizar</div><div id='moon-grid'>");
            }
            
            add("<div class='dashboard-grid'>");
            add("<div class='tile'><div class='tile-icon'>🌙</div><div class='tile-value' id='val-moon-phase' style='font-size:1.1rem; line-height:1.2;'>" + moon.curphase + "</div><div class='tile-label'>Fase</div></div>");
            add("<div class='tile'><div class='tile-icon'>✨</div><div class='tile-value' id='val-moon-illum'>" + ((moon.fracillum != -1) ? String(moon.fracillum) + "%" : String("--%")) + "</div><div class='tile-label'>Iluminación</div></div>");
            add("<div class='tile'><div class='tile-icon'>🌔</div><div class='tile-value' id='val-moon-rise'>" + TimeService::formatMinsFromMidnight(moon.rise_mins, config.time_format) + "</div><div class='tile-label'>Sale la luna</div></div>");
            add("<div class='tile'><div class='tile-icon'>🌘</div><div class='tile-value' id='val-moon-set'>" + TimeService::formatMinsFromMidnight(moon.set_mins, config.time_format) + "</div><div class='tile-label'>Se pone la luna</div></div>");
            add("</div></div>");
            add("<label class='checkbox-label'><input type='checkbox' name='moon_min' value='1' " + String(config.moon_minimal ? "checked" : "") + "> Modo minimalista (solo la fase)</label>");
            add("</div></div>");
            break;
          }

          case SCREEN_ISS: {
            add("<div class='panel' id='panel-" + String(screenId) + "'>");
            add("<label class='checkbox-label mt-0'><input type='checkbox' id='showIss' name='show_iss' value='1' " + String(config.show_iss ? "checked" : "") + "> Pantalla de la Estación Espacial (EEI)</label>");
            add("<div id='issContent' class='collapsible'>");
            if (!issValid) {
              add("<div id='iss-no-data' class='no-data-tile'>🛰️ La EEI aparece después de actualizar</div><div id='iss-grid' class='hidden'>");
            } else {
              add("<div id='iss-no-data' class='no-data-tile hidden'>🛰️ La EEI aparece después de actualizar</div><div id='iss-grid'>");
            }

            add("<div class='dashboard-grid'>");
            add("<div class='tile'><div class='tile-icon'>🌍</div><div class='tile-value' id='val-iss-over' style='font-size:1.1rem; line-height:1.2;'>--</div><div class='tile-label'>Ahora sobrevuela</div></div>");
            add("<div class='tile'><div class='tile-icon'>🚀</div><div class='tile-value' id='val-iss-alt'>--</div><div class='tile-label'>Altitud</div></div>");
            add("<div class='tile'><div class='tile-icon'>⚡</div><div class='tile-value' id='val-iss-speed'>--</div><div class='tile-label'>Velocidad</div></div>");
            add("<div class='tile'><div class='tile-icon'>📏</div><div class='tile-value' id='val-iss-dist'>--</div><div class='tile-label'>Distancia a vos</div></div>");
            add("</div></div>");

            add("<label>Modo:</label><div class='radio-group'>");
            add("<label class='radio-label'><input type='radio' name='iss_mode' value='map' " + String(config.iss_mode != "text" ? "checked" : "") + "> Mapa (mundo con la órbita)</label>");
            add("<label class='radio-label'><input type='radio' name='iss_mode' value='text' " + String(config.iss_mode == "text" ? "checked" : "") + "> Texto (minimalista)</label></div>");

            add("<label>Unidades:</label><div class='radio-group'>");
            add("<label class='radio-label'><input type='radio' name='iss_units' value='kilometers' " + String(config.iss_units != "miles" ? "checked" : "") + "> Kilómetros (km, km/h)</label>");
            add("<label class='radio-label'><input type='radio' name='iss_units' value='miles' " + String(config.iss_units == "miles" ? "checked" : "") + "> Millas (mi, mph)</label></div>");
            add("<p class='help-text mt-0'>Entre actualizaciones, la posición sigue la órbita calculada.</p>");
            add("<hr>");
            add("<label class='checkbox-label' id='customIssSyncLbl'><input type='checkbox' id='customIssSyncChk' name='custom_iss_sync_ui' value='1' " + String(config.custom_iss_int_min > 0 ? "checked" : "") + "> Actualización propia</label>");
            add("<div id='customIssSyncFields' class='collapsible" + String(config.custom_iss_int_min > 0 ? "" : " hidden") + "'>");
            add("<label class='mt-0'>Actualizar cada (min):</label><input type='number' min='1' id='customIssSyncInt' name='custom_iss_int_min' value='" + String(config.custom_iss_int_min > 0 ? config.custom_iss_int_min : config.refresh_interval_min) + "'>");
            add("</div>");
            add("</div></div>");
            break;
          }

          case SCREEN_FLIGHT: {
              // Mini C3 v1.11.0: flights removed (hidden so the order stays complete)
              add("<div class='panel' id='panel-" + String(screenId) + "' style='display:none'>");
              add("<input type='checkbox' id='showFlight' name='show_flight' value='1'><div id='flightContent'></div></div>");
              break;
          }

          case SCREEN_POPULATION: {
            add("<div class='panel' id='panel-" + String(screenId) + "' style='display:none'>");
            add("<label class='checkbox-label mt-0'><input type='checkbox' id='showPopulation' name='show_population' value='1' " + String(config.show_population ? "checked" : "") + "> Pantalla de población</label>");
            add("<div id='popContent' class='collapsible'>");
            
            if (!popValid) {
              add("<div id='pop-no-data' class='no-data-tile'>🌍 La población aparece después de actualizar</div><div id='pop-grid' class='hidden'>");
            } else {
              add("<div id='pop-no-data' class='no-data-tile hidden'>🌍 La población aparece después de actualizar</div><div id='pop-grid'>");
            }
            
            add("<div class='dashboard-grid'>");
            add("<div class='tile pop-wld-tile'><div class='tile-icon'>🌍</div><div class='tile-value' id='val-pop-wld' style='font-size:1.4rem'>--</div><div class='tile-label' id='lbl-pop-wld'>Población mundial</div></div>");
            add("<div class='tile pop-wld-tile'><div class='tile-icon'>📈</div><div class='tile-value' id='val-pop-wld-gr' style='font-size:1.4rem'>--</div><div class='tile-label'>Crecimiento mundial</div></div>");
            
            String ctrStr = config.country_code != "" ? config.country_code : "CTR";
            ctrStr.toUpperCase();
            add("<div class='tile pop-ctr-tile'><div class='tile-icon'>📍</div><div class='tile-value' id='val-pop-ctr' style='font-size:1.4rem'>--</div><div class='tile-label' id='lbl-pop-ctr'>" + ctrStr + " Population</div></div>");
            add("<div class='tile pop-ctr-tile'><div class='tile-icon'>📈</div><div class='tile-value' id='val-pop-ctr-gr' style='font-size:1.4rem'>--</div><div class='tile-label'>" + ctrStr + " Growth</div></div>");
            add("</div></div>");
            
            add("<label class='checkbox-label'><input type='checkbox' id='popWldChk' name='pop_show_world' value='1' " + String(config.pop_show_world ? "checked" : "") + "> Mostrar población mundial</label>");
            add("<label class='checkbox-label'><input type='checkbox' id='popCtrChk' name='pop_show_country' value='1' " + String(config.pop_show_country ? "checked" : "") + "> Mostrar población del país</label>");
            add("</div></div>");
            break;
          }

          case SCREEN_STOCK: {
              add("<div class='panel' id='panel-" + String(screenId) + "'>");
              add("<label class='checkbox-label mt-0'><input type='checkbox' id='showStock' name='show_stock' value='1' " + String(config.show_stock ? "checked" : "") + "> Pantalla de acciones</label>");
              add("<div id='stockContent' class='collapsible'>");
              
              add("<div id='stock-no-data' class='no-data-tile'>📈 Las acciones aparecen después de actualizar</div><div id='stock-grid' class='hidden'>");
              add("<div class='dashboard-grid'>");
              add("<div class='tile'><div class='tile-icon'>📊</div><div class='tile-value' id='stock-price' style='font-size:1.0rem; line-height:1.5;'>--</div><div class='tile-label'>PRECIOS</div></div>");
              add("<div class='tile'><div class='tile-icon'>📈</div><div class='tile-value' id='stock-change' style='font-size:1.0rem; line-height:1.5;'>--</div><div class='tile-label'>CAMBIO 24 H</div></div>");
              add("</div><div class='update-footer' id='stock-upd'>Actualizado: --</div></div>");

              add("<div id='stock-list-container'></div>");
              add("<button type='button' class='btn-blue' onclick='addStockRow()'>+ Agregar acción / ETF</button>");
              add("<label class='checkbox-label'><input type='checkbox' name='stock_fn' value='1' " + String(config.stock_fn ? "checked" : "") + "> Mostrar el nombre completo de la empresa</label>");
              add("<hr>");
              add("<label class='checkbox-label' id='customStockSyncLbl'><input type='checkbox' id='customStockSyncChk' name='custom_stock_sync_ui' value='1' " + String(config.custom_stock_int_min > 0 ? "checked" : "") + "> Actualización propia</label>");
              add("<div id='customStockSyncFields' class='collapsible" + String(config.custom_stock_int_min > 0 ? "" : " hidden") + "'>");
              add("<label class='mt-0'>Actualizar cada (min):</label><input type='number' min='1' id='customStockSyncInt' name='custom_stock_int_min' value='" + String(config.custom_stock_int_min > 0 ? config.custom_stock_int_min : config.refresh_interval_min) + "'>");
              add("</div>");
              add("</div></div>");
              break;
          }

          case SCREEN_CRYPTO: {
              add("<div class='panel' id='panel-" + String(screenId) + "'>");
              add("<label class='checkbox-label mt-0'><input type='checkbox' id='showCrypto' name='show_crypto' value='1' " + String(config.show_crypto ? "checked" : "") + "> Pantalla de cripto</label>");
              add("<div id='cryptoContent' class='collapsible'>");
              
              add("<div id='crypto-no-data' class='no-data-tile'>💰 Las cripto aparecen después de actualizar</div><div id='crypto-grid' class='hidden'>");
              add("<div class='dashboard-grid'>");
              add("<div class='tile'><div class='tile-icon'>₿</div><div class='tile-value' id='crypto-price' style='font-size:1.0rem; line-height:1.5;'>--</div><div class='tile-label'>PRECIOS</div></div>");
              add("<div class='tile'><div class='tile-icon'>📈</div><div class='tile-value' id='crypto-change' style='font-size:1.0rem; line-height:1.5;'>--</div><div class='tile-label'>CAMBIO 24 H</div></div>");
              add("</div><div class='update-footer' id='crypto-upd'>Actualizado: --</div></div>");
              
              add("<div id='crypto-list-container'></div>");
              add("<button type='button' class='btn-blue' onclick='addCryptoRow()'>+ Agregar cripto</button>");
              add("<label class='checkbox-label'><input type='checkbox' name='crypto_fn' value='1' " + String(config.crypto_fn ? "checked" : "") + "> Mostrar el nombre completo de la moneda</label>");
              add("<hr>");
              add("<label class='checkbox-label' id='customCryptoSyncLbl'><input type='checkbox' id='customCryptoSyncChk' name='custom_crypto_sync_ui' value='1' " + String(config.custom_crypto_int_min > 0 ? "checked" : "") + "> Actualización propia</label>");
              add("<div id='customCryptoSyncFields' class='collapsible" + String(config.custom_crypto_int_min > 0 ? "" : " hidden") + "'>");
              add("<label class='mt-0'>Actualizar cada (min):</label><input type='number' min='1' id='customCryptoSyncInt' name='custom_crypto_int_min' value='" + String(config.custom_crypto_int_min > 0 ? config.custom_crypto_int_min : config.refresh_interval_min) + "'>");
              add("</div>");
              add("</div></div>");
              break;
          }

          case SCREEN_CURRENCY: {
              add("<div class='panel' id='panel-" + String(screenId) + "'>");
              add("<label class='checkbox-label mt-0'><input type='checkbox' id='showCurrency' name='show_currency' value='1' " + String(config.show_currency ? "checked" : "") + "> Pantalla de cotizaciones</label>");
              add("<div id='currencyContent' class='collapsible'>");
              
              add("<div id='currency-no-data' class='no-data-tile'>💱 Las cotizaciones aparecen después de actualizar</div><div id='currency-grid' class='hidden'>");
              add("<div class='dashboard-grid'>");
              add("<div class='tile'><div class='tile-icon'>💵</div><div class='tile-value' id='currency-base-val' style='font-size:1.0rem; line-height:1.5;'>--</div><div class='tile-label'>BASE</div></div>");
              add("<div class='tile'><div class='tile-icon'>💱</div><div class='tile-value' id='currency-target-val' style='font-size:1.0rem; line-height:1.5;'>--</div><div class='tile-label'>COTIZACIÓN</div></div>");
              add("</div><div class='update-footer' id='currency-upd'>Actualizado: --</div></div>");

              add("<div id='currency-list-container'></div>");
              add("<button type='button' class='btn-blue' onclick='addCurrencyRow()'>+ Agregar par de monedas</button>");
              add("<label class='checkbox-label'><input type='checkbox' name='currency_fn' value='1' " + String(config.currency_fn ? "checked" : "") + "> Mostrar el nombre completo de la moneda</label>");
              add("<hr>");
              add("<label class='checkbox-label' id='customCurrencySyncLbl'><input type='checkbox' id='customCurrencySyncChk' name='custom_currency_sync_ui' value='1' " + String(config.custom_currency_int_min > 0 ? "checked" : "") + "> Actualización propia</label>");
              add("<div id='customCurrencySyncFields' class='collapsible" + String(config.custom_currency_int_min > 0 ? "" : " hidden") + "'>");
              add("<label class='mt-0'>Actualizar cada (min):</label><input type='number' min='1' id='customCurrencySyncInt' name='custom_currency_int_min' value='" + String(config.custom_currency_int_min > 0 ? config.custom_currency_int_min : config.refresh_interval_min) + "'>");
              add("</div>");
              add("</div></div>");
              break;
          }

          case SCREEN_PC_MONITOR: {
              add("<div class='panel' id='panel-" + String(screenId) + "'>");
              add("<label class='checkbox-label mt-0'><input type='checkbox' id='showPc' name='show_pc' value='1' " + String(config.show_pc ? "checked" : "") + "> Pantalla de monitoreo de la PC</label>");
              add("<div id='pcContent' class='collapsible'>");
              
              if (!pcValid) {
                  add("<div id='pc-no-data' class='no-data-tile'>🖥️ Los datos de la PC aparecen cuando se conecta</div><div id='pc-grid' class='hidden'>");
              } else {
                  add("<div id='pc-no-data' class='no-data-tile hidden'>🖥️ Los datos de la PC aparecen cuando se conecta</div><div id='pc-grid'>");
              }
              
              add("<div class='dashboard-grid'>");
              add("<div class='tile'><div class='tile-icon'>📊</div><div class='tile-value' id='pc-cpu'>" + String((int)round(pc.cpu_percent)) + "%</div><div class='tile-label'>Uso de CPU</div></div>");
              add("<div class='tile'><div class='tile-icon'>🧠</div><div class='tile-value' id='pc-ram'>" + String((int)round(pc.mem_percent)) + "%</div><div class='tile-label'>Uso de RAM</div></div>");
              add("<div class='tile'><div class='tile-icon'>💽</div><div class='tile-value' id='pc-disk'>" + String((int)round(pc.disk_percent)) + "%</div><div class='tile-label'>Uso de disco</div></div>");
              add("<div class='tile'><div class='tile-icon'>⬇️</div><div class='tile-value' id='pc-net'>" + String((int)round(pc.net_down_kb)) + " KB/s</div><div class='tile-label'>Bajada</div></div>");      
              add("</div></div>");
              add("<label class='checkbox-label'><input type='checkbox' name='hide_empty_pc' value='1' " + String(config.hide_empty_pc ? "checked" : "") + "> Ocultar si no hay datos</label>");
              add("<p class='help-text mt-0'>La pantalla se saltea si no hay datos.</p>");
              add("</div></div>");
              break;
          }

          case SCREEN_PC_MEDIA: {
              add("<div class='panel' id='panel-" + String(screenId) + "'>");
              add("<label class='checkbox-label mt-0'><input type='checkbox' id='showMedia' name='show_media' value='1' " + String(config.show_media ? "checked" : "") + "> Pantalla de música de la PC</label>");
              add("<div id='mediaContent' class='collapsible'>");

              bool isMediaValid = (media.name.length() > 0 && media.author.length() > 0);
              if (!isMediaValid) {
                  add("<div id='media-no-data' class='no-data-tile'>🎵 La música aparece cuando la PC reproduce algo</div><div id='media-grid' class='hidden'>");
              } else {
                  add("<div id='media-no-data' class='no-data-tile hidden'>🎵 La música aparece cuando la PC reproduce algo</div><div id='media-grid'>");
              }

              add("<div class='dashboard-grid'>");
              add("<div class='tile'><div class='tile-icon'>🎵</div><div class='tile-value' id='web-media-status' style='font-size:1.2rem'>" + media.status + "</div><div class='tile-label'>Estado</div></div>");
              add("<div class='tile'><div class='tile-icon'>🎧</div><div class='tile-value' id='web-media-name' style='font-size:1.2rem'>" + media.name + "</div><div class='tile-label'>Rumbo</div></div>");
              add("<div class='tile'><div class='tile-icon'>👤</div><div class='tile-value' id='web-media-author' style='font-size:1.2rem'>" + media.author + "</div><div class='tile-label'>Artista</div></div>");
              add("<div class='tile'><div class='tile-icon'>💿</div><div class='tile-value' id='web-media-album' style='font-size:1.2rem'>" + media.album + "</div><div class='tile-label'>Álbum</div></div>");
              add("</div></div>");
              add("<label class='checkbox-label'><input type='checkbox' name='hide_empty_media' value='1' " + String(config.hide_empty_media ? "checked" : "") + "> Ocultar si no hay datos</label>");
              add("<p class='help-text mt-0'>La pantalla se saltea si no hay datos.</p>");
              add("</div></div>");
              break;
          }

          case SCREEN_BAMBU: {
              add("<div class='panel' id='panel-" + String(screenId) + "'>");
              add("<label class='checkbox-label mt-0'><input type='checkbox' id='showBambu' name='show_bambu' value='1' " + String(config.show_bambu ? "checked" : "") + "> Pantalla de impresora Bambu</label>");
              add("<div id='bambuContent' class='collapsible'>");
              
              bool isBambuKnown = (state->bambu.status != "SYNCING");
              if (!isBambuKnown) {
                  add("<div id='bambu-no-data' class='no-data-tile'>🖨️ Los datos de la impresora aparecen cuando se conecta</div><div id='bambu-grid' class='hidden'>");
              } else {
                  add("<div id='bambu-no-data' class='no-data-tile hidden'>🖨️ Los datos de la impresora aparecen cuando se conecta</div><div id='bambu-grid'>");
              }
              
              add("<div class='dashboard-grid'>");
              add("<div class='tile'><div class='tile-icon'>🖨️</div><div class='tile-value' id='bambu-status' style='font-size:1.2rem'>" + state->bambu.status + "</div><div class='tile-label'>Estado</div></div>");
              add("<div class='tile'><div class='tile-icon'>⏳</div><div class='tile-value' id='bambu-prog' style='font-size:1.1rem'>" + String(state->bambu.progress) + "% | " + String(state->bambu.time_left) + "m<br><span style='font-size:0.9rem'>Layers: " + String(state->bambu.layer) + "/" + String(state->bambu.total_layers) + "</span></div><div class='tile-label'>Progreso</div></div>");
              add("<div class='tile'><div class='tile-icon'>🌡️</div><div class='tile-value' id='bambu-temps' style='font-size:1.1rem'>Pico: " + String(state->bambu.nozzle_temp, 1) + "/" + String(state->bambu.nozzle_target, 1) + "<br>Bed: " + String(state->bambu.bed_temp, 1) + "/" + String(state->bambu.bed_target, 1) + "</div><div class='tile-label'>Temperaturas</div></div>");
              add("<div class='tile'><div class='tile-icon'>💨</div><div class='tile-value' id='bambu-fans' style='font-size:1.2rem'>Pieza: " + String(state->bambu.fan_part) + " | Aux: " + String(state->bambu.fan_aux) + "</div><div class='tile-label'>Ventiladores</div></div>");
              add("</div></div>");

              add("<label class='checkbox-label'><input type='checkbox' name='hide_empty_bambu' value='1' " + String(config.hide_empty_bambu ? "checked" : "") + "> Ocultar si no hay datos</label>");
              add("<p class='help-text mt-0'>La pantalla se saltea si la impresora está apagada.</p>");

              add("<label>IP de la impresora:</label><input type='text' name='bambu_ip' placeholder='e.g. 192.168.0.100' value='" + config.bambu_ip + "'>");
              add("<label>Número de serie de la impresora:</label><input type='text' name='bambu_sn' placeholder='e.g. 00M...' value='" + config.bambu_sn + "'>");
              add("<label>Código de acceso de la impresora:</label><input type='text' name='bambu_code' placeholder='e.g. 1234abcd' value='" + config.bambu_code + "'>");
              add("</div></div>");
              break;
          }

          case SCREEN_SPOTIFY: {
              add("<div class='panel' id='panel-" + String(screenId) + "'>");
              add("<label class='checkbox-label mt-0'><input type='checkbox' id='showSpotify' name='show_spotify' value='1' " + String(config.show_spotify ? "checked" : "") + "> Pantalla de Spotify</label>");
              add("<div id='spotifyContent' class='collapsible'>");
              add(String("<p class='help-text mt-0'>") + (spotifyService.isConfigured() ? "Cuenta vinculada. " : "Todav&iacute;a no est&aacute; vinculada. ") + "Configuralo en <a href='/spotify'>" + String(DEVICE_HOSTNAME) + ".local/spotify</a>. En el equipo: A = play/pausa, &larr;/&rarr; = tema anterior/siguiente, A mantenida = volumen.</p>");
              add("</div></div>");
              break;
          }

          case SCREEN_GAMES: {
              add("<div class='panel' id='panel-" + String(screenId) + "'>");
              add("<label class='checkbox-label mt-0'><input type='checkbox' id='showGames' name='show_games' value='1' " + String(config.show_games ? "checked" : "") + "> Pantalla de juegos</label>");
              add("<div id='gamesContent' class='collapsible'>");
              add("<p class='help-text mt-0'>&larr; &rarr; eligen el juego y A lo arranca. <b>Arkanoid</b>: &larr; &rarr; mueven la paleta, A lanza y pausa, &darr; pausa. <b>Truco contra la compu</b> (sin flor, a 15): &larr; &rarr; eligen carta, A la tira, &darr; abre los cantos. En el men&uacute; del truco, A mantenida cambia el nivel (Normal / Dif&iacute;cil). <b>Tetris</b>: &larr; &rarr; mueven, A gira, &uarr; la tira, &darr; baja r&aacute;pido, A mantenida pausa. <b>Flappy</b>: A o &uarr; aletea, &darr; pausa. <b>Viborita</b>: las flechas la doblan, A pausa. <b>2048</b>: las flechas deslizan las fichas, A pausa. <b>Buscaminas</b>: flechas mueven el cursor, A toca abre (sobre un n&uacute;mero con sus banderas completas abre alrededor), A mantenida pone/saca bandera, A mantenida 1,5 s pausa. <b>Invasores</b>: &larr; &rarr; mueven la nave, A o &uarr; disparan, &darr; pausa. <b>Sim&oacute;n</b>: repet&iacute; la secuencia con las flechas (el LED se prende del color de cada una), A pausa. Mientras jug&aacute;s el WiFi se apaga para ahorrar bater&iacute;a.</p>");
              add("</div></div>");
              break;
          }

          case SCREEN_TRUCO: {
              add("<div class='panel' id='panel-" + String(screenId) + "'>");
              add("<label class='checkbox-label mt-0'><input type='checkbox' id='showTruco' name='show_truco' value='1' " + String(config.show_truco ? "checked" : "") + "> Anotador de truco</label>");
              add("<div id='trucoContent' class='collapsible'>");
              add("<p class='help-text mt-0'>&larr; suma para ellos, &rarr; para nosotros. Mantener la flecha borra un tanto. A mantenida: nueva partida. Con 0 a 0, A cambia entre partida a 15 y a 30. Los puntos quedan guardados aunque la apagues.</p>");
              add("</div></div>");
              break;
          }

          case SCREEN_MESSAGES: {
              add("<div class='panel' id='panel-" + String(screenId) + "'>");
              add("<label class='checkbox-label mt-0'><input type='checkbox' id='showMessages' name='show_messages' value='1' " + String(config.show_messages ? "checked" : "") + "> Pantalla de mensajes</label>");
              add("<div id='messagesContent' class='collapsible'>");
              add("<p class='help-text mt-0'>Cualquiera en tu WiFi puede mandar un mensaje desde <a href='/mensaje'>" + String(DEVICE_HOSTNAME) + ".local/mensaje</a>. Llega con una animaci&oacute;n y la luz de la placa titila hasta que toc&aacute;s una tecla. Se guardan los &uacute;ltimos 8. En la Mini C3: &larr; &rarr; eligen, A abre, y dentro del mensaje A muestra las opciones para borrarlo.</p>");
              add("</div></div>");
              break;
          }

          case SCREEN_NEWS: {
              add("<div class='panel' id='panel-" + String(screenId) + "'>");
              add("<label class='checkbox-label mt-0'><input type='checkbox' id='showNews' name='show_news' value='1' " + String(config.show_news ? "checked" : "") + "> Pantalla de noticias de Argentina</label>");
              add("<div id='newsContent' class='collapsible'>");
              add("<p class='help-text mt-0'>Titulares de Google Noticias (Argentina), con Clar&iacute;n y La Naci&oacute;n de respaldo. Se actualizan cada 20 minutos. En el equipo: &larr; pasa de noticia y &rarr; muestra el titular completo.</p>");
              if (state->news.count > 0) {
                  add("<ul style='margin:0;padding-left:18px'>");
                  for (int k = 0; k < state->news.count && k < 5; k++) {
                      String t = state->news.items[k].title;
                      t.replace("<", "&lt;");
                      add("<li style='margin:6px 0'>" + t + (state->news.items[k].source.length() ? " <span class='help-text'>(" + state->news.items[k].source + ")</span>" : String("")) + "</li>");
                  }
                  add("</ul>");
              } else {
                  add("<div class='no-data-tile'>📰 Las noticias aparecen despu&eacute;s de actualizar</div>");
              }
              add("</div></div>");
              break;
          }
      }
  }

  add("</div>");
  add("<button type='submit'>💾 Guardar y aplicar todo</button></form>");
  
  add("<script>");
  add("let formDirty = false;");

  add("function updateVisibility(){");
  add("  var pairs = [['autoDetect','manualFields',true], ['nightMode','nightFields',false], ['showTime', 'timeContent',false], ['showCalendar', 'calendarContent',false], ['showWeather','weatherContent',false], ['showDaylight','daylightContent',false], ['showMoon','moonContent',false], ['showIss','issContent',false], ['showFlight','flightContent',false], ['showPopulation','popContent',false], ['showPc','pcContent',false], ['showCrypto','cryptoContent',false], ['showCurrency','currencyContent',false], ['showStock','stockContent',false], ['showAQI','aqiContent',false], ['showMedia','mediaContent',false], ['showBambu','bambuContent',false], ['showNews','newsContent',false], ['showMessages','messagesContent',false], ['showSpotify','spotifyContent',false], ['showGames','gamesContent',false], ['showTruco','trucoContent',false], ['customWeatherSyncChk','customWeatherSyncFields',false], ['customAqiSyncChk','customAqiSyncFields',false], ['customStockSyncChk','customStockSyncFields',false], ['customCryptoSyncChk','customCryptoSyncFields',false], ['customCurrencySyncChk','customCurrencySyncFields',false], ['customIssSyncChk','customIssSyncFields',false], ['customFlightSyncChk','customFlightSyncFields',false]];");
  add("  pairs.forEach(p => {");
  add("    var ch = document.getElementById(p[0]); if(!ch) return;");
  add("    var target = document.getElementById(p[1]);");
  add("    var shouldHide = p[2] ? ch.checked : !ch.checked;");
  add("    target.className = shouldHide ? 'collapsible hidden' : 'collapsible';");
  add("    target.querySelectorAll('input, select').forEach(el => el.disabled = shouldHide);");
  add("  });");
  add("  var ac = document.getElementById('autoCycle');");
  add("  var si = document.getElementById('screenIntInput');");
  add("  if(ac && si) si.disabled = !ac.checked;");
  add("  updateFlightSecondaryVisibility();");
  add("  updateValueLimits('weather', 6);");
  add("  updateValueLimits('aqi', 6);");
  add("}");

  add("function updateFlightSecondaryVisibility(){");
  add("  var radarChk = document.getElementById('flightModeRadar');");
  add("  if(!radarChk) return;");
  add("  var shouldHide = !radarChk.checked;");
  add("  ['flightPrimaryGroup', 'flightSecondaryGroup'].forEach(id => {");
  add("    var group = document.getElementById(id);");
  add("    if(!group) return;");
  add("    group.className = shouldHide ? 'collapsible hidden' : 'collapsible';");
  add("    group.querySelectorAll('input').forEach(el => el.disabled = shouldHide);");
  add("  });");
  add("}");

  add("function updatePinSelects() {");
  add("  const selects = document.querySelectorAll('.hw-pin');");
  add("  const vals = Array.from(selects).map(s => s.value);");
  add("  selects.forEach(sel => {");
  add("    Array.from(sel.options).forEach(opt => {");
  add("      opt.disabled = vals.includes(opt.value) && opt.value !== sel.value;");
  add("    });");
  add("  });");
  add("}");
  add("document.querySelectorAll('.hw-pin').forEach(s => s.addEventListener('change', updatePinSelects));");
  add("updatePinSelects();");

  add("function updateValueLimits(prefix, maxNoHeader) {");
  add("  const headerRadio = document.querySelector('[name=\"'+prefix+'_show_header\"]:checked');");
  add("  const max = (headerRadio && headerRadio.value === '1') ? 3 : maxNoHeader;");
  add("  const boxes = document.querySelectorAll('.'+prefix+'-val-chk');");
  add("  const checked = Array.from(boxes).filter(cb => cb.checked);");
  add("  if (checked.length > max) checked.slice(max).forEach(cb => cb.checked = false);");
  add("  const checkedCount = Array.from(boxes).filter(cb => cb.checked).length;");
  add("  boxes.forEach(cb => { cb.disabled = !cb.checked && checkedCount >= max; });");
  add("}");

  add("function updateNightAction() {");
  add("  var action = document.getElementById('nightActionSelect').value;");
  add("  var dimCont = document.getElementById('dimStartContainer');");
  add("  if (action === '3') { dimCont.style.display = 'block'; } else { dimCont.style.display = 'none'; }");
  add("}");
  add("document.getElementById('nightActionSelect').addEventListener('change', updateNightAction);");
  add("updateNightAction();");

  add("window.updateRowControls = function(containerId, maxLimit) { const container = document.getElementById(containerId); if(!container) return; const rows = container.children; const addBtn = container.nextElementSibling; if(addBtn && addBtn.tagName === 'BUTTON') { addBtn.style.display = rows.length >= maxLimit ? 'none' : 'block'; } const removeBtns = container.querySelectorAll('.btn-remove'); removeBtns.forEach(btn => { btn.style.display = rows.length <= 1 ? 'none' : 'flex'; }); };");
  add("window.removeRow = function(btn, containerId) { btn.parentElement.remove(); formDirty = true; updateRowControls(containerId, 5); };");

  add("window.addStockRow = function(val = null) { const container = document.getElementById('stock-list-container'); if (!container || container.children.length >= 5) return; const div = document.createElement('div'); div.className = 'multi-row'; let opts = ''; ");
  for(auto s : topStocks) { add("opts += `<option value='" + String(s.ticker) + "'>" + String(s.name) + " - " + String(s.ticker) + "</option>`;"); }
  add("div.innerHTML = `<div class='input-wrapper'><label class='mt-0'>Acción:</label><select name='stock_symbols[]'>${opts}</select></div><button type='button' class='btn-remove' onclick=\"removeRow(this, 'stock-list-container')\">-</button>`; container.appendChild(div); if (val) div.querySelector('select').value = val; formDirty = true; updateRowControls('stock-list-container', 5); };");

  add("window.addCryptoRow = function(val = null) { const container = document.getElementById('crypto-list-container'); if (!container || container.children.length >= 5) return; const div = document.createElement('div'); div.className = 'multi-row'; let opts = ''; ");
  for(auto c : topCoins) { add("opts += `<option value='" + String(c.id) + "'>" + String(c.sym) + "</option>`;"); }
  add("div.innerHTML = `<div class='input-wrapper'><label class='mt-0'>Cripto:</label><select name='crypto_ids[]'>${opts}</select></div><button type='button' class='btn-remove' onclick=\"removeRow(this, 'crypto-list-container')\">-</button>`; container.appendChild(div); if (val) div.querySelector('select').value = val; formDirty = true; updateRowControls('crypto-list-container', 5); };");

  add("window.addCurrencyRow = function(bVal = null, tVal = null, mVal = null) { const container = document.getElementById('currency-list-container'); if (!container || container.children.length >= 5) return; const div = document.createElement('div'); div.className = 'multi-row'; let cOpts = ''; ");
  for(auto c : allCurrencies) {
    String codeStr = String(c.code);
    codeStr.toUpperCase();
    add("cOpts += `<option value='" + String(c.code) + "'>" + codeStr + "</option>`;");
  }
  add("div.innerHTML = `<div class='input-wrapper'><label class='mt-0'>Base:</label><select name='currency_bases[]'>${cOpts}</select></div><div class='input-wrapper'><label class='mt-0'>Destino:</label><select name='currency_targets[]'>${cOpts}</select></div><div class='input-wrapper'><label class='mt-0'>Multiplicar:</label><select name='currency_multipliers[]'><option value='1'>1</option><option value='10'>10</option><option value='100'>100</option><option value='1000'>1000</option></select></div><button type='button' class='btn-remove' onclick=\"removeRow(this, 'currency-list-container')\">-</button>`; container.appendChild(div); if (bVal) div.querySelector(\"select[name='currency_bases[]']\").value = bVal; if (tVal) div.querySelector(\"select[name='currency_targets[]']\").value = tVal; if (mVal) div.querySelector(\"select[name='currency_multipliers[]']\").value = mVal; formDirty = true; updateRowControls('currency-list-container', 5); };");

  add("['autoDetect', 'nightMode', 'showTime', 'showCalendar', 'showWeather', 'showDaylight', 'showMoon', 'showIss', 'showFlight', 'showPopulation', 'showPc', 'showCrypto', 'showCurrency', 'showStock', 'showAQI', 'showMedia', 'showBambu', 'showNews', 'showMessages', 'showSpotify', 'showGames', 'showTruco', 'autoCycle', 'customWeatherSyncChk', 'customAqiSyncChk', 'customStockSyncChk', 'customCryptoSyncChk', 'customCurrencySyncChk', 'customIssSyncChk', 'customFlightSyncChk', 'flightModeRadar', 'flightModeClosest'].forEach(id => { var el=document.getElementById(id); if(el) el.addEventListener('change', updateVisibility); });");
  add("updateVisibility();");

  add("const countryGreetings = {");
  add("  'BY': 'Жыве Беларусь ⚪🔴⚪', 'UA': 'Слава Україні 🇺🇦', 'RU': 'Россия Будет Свободной ⚪🔵⚪',");
  add("  'GB': 'Cheers, Britain 🇬🇧', 'US': 'Howdy, America 🇺🇸', 'PL': 'Dzień dobry, Polsko 🇵🇱',");
  add("  'CA': 'Hello, Canada 🇨🇦', 'AU': 'G\\'day, Australia 🇦🇺', 'FR': 'Bonjour, France 🇫🇷',");
  add("  'DE': 'Hallo, Deutschland 🇩🇪', 'IT': 'Viva l\\'Italia 🇮🇹', 'ES': 'Viva España 🇪🇸',");
  add("  'JP': 'Konnichiwa, Japan 🇯🇵', 'BR': 'Olá, Brasil 🇧🇷', 'IN': 'Namaste, India 🇮🇳',");
  add("  'MX': 'Viva México 🇲🇽', 'ZA': 'Sawubona, South Africa 🇿🇦', 'NZ': 'Kia Ora, New Zealand 🇳🇿',");
  add("  'IE': 'Dia dhuit, Ireland 🇮🇪', 'CH': 'Grüezi, Switzerland 🇨🇭', 'NL': 'Hallo, Nederland 🇳🇱',");
  add("  'KR': 'Annyeonghaseyo, Korea 🇰🇷', 'GR': 'Yassou, Greece 🇬🇷', 'AR': '¡Hola, Argentina! 🇦🇷'");
  add("};");

  add("function updateLiveHeader() {");
  add("  const cInput = document.querySelector('input[name=\"city\"]');");
  add("  const cSel = document.querySelector('select[name=\"country_code\"]');");
  add("  const tSel = document.querySelector('select[name=\"timezone\"]');");
  add("  const city = (cInput && cInput.value) ? cInput.value : '--';");
  add("  const cName = (cSel && cSel.selectedIndex >= 0) ? cSel.options[cSel.selectedIndex].text : '--';");
  add("  const cCode = cSel ? cSel.value : null;");
  add("  const tz = (tSel && tSel.value) ? tSel.value : '--';");
  add("  const locInfo = document.getElementById('location-info');");
  add("  if (locInfo && city !== '--') locInfo.innerText = '📍 ' + city + ', ' + cName + ' (' + tz + ')';");
  add("  const greetingElement = document.getElementById('greetings-text');");
  add("  if (greetingElement) {");
  add("    if (cCode && countryGreetings[cCode]) { greetingElement.innerText = countryGreetings[cCode]; greetingElement.style.display = 'block'; }");
  add("    else { greetingElement.style.display = 'none'; }");
  add("  }");
  add("}");
  
  add("document.querySelector('input[name=\"city\"]').addEventListener('input', updateLiveHeader);");
  add("document.querySelector('select[name=\"country_code\"]').addEventListener('change', updateLiveHeader);");
  add("document.querySelector('select[name=\"timezone\"]').addEventListener('change', updateLiveHeader);");

  add("function toggleNone() {");
  add("  const noneBox = document.getElementById('animNone');");
  add("  const others = document.querySelectorAll('.anim-chk');");
  add("  others.forEach(cb => {");
  add("    cb.disabled = noneBox.checked;");
  add("    if(noneBox.checked) cb.checked = false;");
  add("    cb.parentElement.style.opacity = noneBox.checked ? '0.5' : '1';");
  add("  });");
  add("}");

  add("function checkPopSafetyNet() {");
   add("  const wld = document.getElementById('popWldChk');");
   add("  const ctr = document.getElementById('popCtrChk');");
   add("  if (wld && ctr && !wld.checked && !ctr.checked) wld.checked = true;");
   add("  if (wld) document.querySelectorAll('.pop-wld-tile').forEach(el => el.classList.toggle('hidden', !wld.checked));");
   add("  if (ctr) document.querySelectorAll('.pop-ctr-tile').forEach(el => el.classList.toggle('hidden', !ctr.checked));");
   add("}");
   add("const wldCb = document.getElementById('popWldChk');");
   add("const ctrCb = document.getElementById('popCtrChk');");
   add("if(wldCb) wldCb.addEventListener('change', checkPopSafetyNet);");
   add("if(ctrCb) ctrCb.addEventListener('change', checkPopSafetyNet);");
   add("checkPopSafetyNet();");

  add("function checkSafetyNet() {");
  add("  if(!document.getElementById('animNone').checked) {");
  add("    let count = 0;");
  add("    document.querySelectorAll('.anim-chk').forEach(cb => { if(cb.checked) count++; });");
  add("    if(count === 0) {");
  add("      document.getElementById('animNone').checked = true;");
  add("      toggleNone();");
  add("    }");
  add("  }");
  add("}");

  add("const nb = document.getElementById('animNone');");
  add("if(nb) nb.addEventListener('change', toggleNone);");
  add("document.querySelectorAll('.anim-chk').forEach(cb => { cb.addEventListener('change', checkSafetyNet); });");
  add("toggleNone();");

  add("const list = document.getElementById('sortable-list');");
  add("const orderInput = document.getElementById('screenOrderInput');");

  add("function applyLiveTheme() { const root = document.documentElement; root.style.setProperty('--base-bg', document.getElementById('theme_bg').value); root.style.setProperty('--base-surface', document.getElementById('theme_card').value); root.style.setProperty('--base-primary', document.getElementById('theme_accent').value); root.style.setProperty('--base-text', document.getElementById('theme_text').value); }");
  add("['theme_bg', 'theme_card', 'theme_accent', 'theme_text'].forEach(id => { const el = document.getElementById(id); if (el) el.addEventListener('input', applyLiveTheme); });");

  add("function syncScreenOrder() {");
  add("  const items = [...list.querySelectorAll('.sortable-item')];");
  add("  let enabled = [], disabled = [];");
  add("  items.forEach(item => {");
  add("    const targetId = item.getAttribute('data-target');");
  add("    const cb = document.getElementById(targetId);");
  add("    if (cb && cb.checked) { item.classList.remove('disabled'); item.setAttribute('draggable', 'true'); enabled.push(item); }");
  add("    else { item.classList.add('disabled'); item.removeAttribute('draggable'); disabled.push(item); }");
  add("  });");
  add("  list.innerHTML = '';");
  add("  enabled.forEach(el => list.appendChild(el)); disabled.forEach(el => list.appendChild(el));");
  add("  updateOrderValue();");
  add("}");

  add("function reorderPhysicalPanels(orderCsv) {");
  add("  const container = document.getElementById('dynamic-panels-container');");
  add("  if (!container || !orderCsv) return;");
  add("  const orderArr = orderCsv.split(',');");
  add("  orderArr.forEach(id => { const panel = document.getElementById('panel-' + id); if (panel) container.appendChild(panel); });");
  add("}");

  add("function updateOrderValue() {");
  add("  const items = [...list.querySelectorAll('.sortable-item')];");
  add("  orderInput.value = items.map(item => item.getAttribute('data-id')).join(',');");
  add("  reorderPhysicalPanels(orderInput.value);");
  add("}");

  add("const panelCheckboxes = ['showTime', 'showCalendar', 'showWeather', 'showAQI', 'showDaylight', 'showMoon', 'showIss', 'showFlight', 'showPopulation', 'showCrypto', 'showCurrency', 'showStock', 'showPc', 'showMedia', 'showBambu', 'showNews', 'showMessages', 'showSpotify', 'showGames', 'showTruco'];");
  add("panelCheckboxes.forEach(id => { const el = document.getElementById(id); if (el) el.addEventListener('change', syncScreenOrder); });");

  add("function getDragAfterEl(y) {");
  add("  return [...list.querySelectorAll('.sortable-item:not(.dragging):not(.disabled)')].reduce((closest, child) => {");
  add("    const box = child.getBoundingClientRect();");
  add("    const offset = y - box.top - box.height / 2;");
  add("    if (offset < 0 && offset > closest.offset) return { offset: offset, element: child };");
  add("    else return closest;");
  add("  }, { offset: Number.NEGATIVE_INFINITY }).element;");
  add("}");

  add("function moveItem(y) {");
  add("  const draggable = document.querySelector('.dragging');");
  add("  if (!draggable) return;");
  add("  const afterEl = getDragAfterEl(y);");
  add("  if (afterEl == null) {");
  add("    const firstDis = list.querySelector('.disabled');");
  add("    if (firstDis) list.insertBefore(draggable, firstDis);");
  add("    else list.appendChild(draggable);");
  add("  } else { list.insertBefore(draggable, afterEl); }");
  add("}");

  add("list.addEventListener('dragstart', e => { if (e.target.classList.contains('disabled')) { e.preventDefault(); return; } e.target.classList.add('dragging'); });");
  add("list.addEventListener('dragend', e => { e.target.classList.remove('dragging'); updateOrderValue(); formDirty = true; });");
  add("list.addEventListener('dragover', e => { e.preventDefault(); moveItem(e.clientY); });");

  add("list.addEventListener('touchstart', e => {");
  add("  const item = e.target.closest('.sortable-item');");
  add("  if (!item || item.classList.contains('disabled')) return;");
  add("  item.classList.add('dragging');");
  add("}, {passive: false});");

  add("list.addEventListener('touchmove', e => {");
  add("  if (!document.querySelector('.dragging')) return;");
  add("  e.preventDefault(); moveItem(e.touches[0].clientY);");
  add("}, {passive: false});");

  add("list.addEventListener('touchend', e => {");
  add("  const dragging = document.querySelector('.dragging');");
  add("  if (dragging) { dragging.classList.remove('dragging'); updateOrderValue(); formDirty = true; }");
  add("});");

  add("syncScreenOrder();");
  
  add("const CONFIG_FIELD_MAP = {");
  add("  sda_pin: ['hardware','sda_pin'], scl_pin: ['hardware','scl_pin'], button_pin: ['hardware','button_pin'], button_type: ['hardware','button_type'],");
  add("  refresh_min: ['general','refresh_min'], time_format: ['general','time_format'], auto_detect: ['general','auto_detect'],");
  add("  latitude: ['general','latitude'], longitude: ['general','longitude'], country: ['general','country'], country_code: ['general','country_code'],");
  add("  city: ['general','city'], timezone: ['general','timezone'], date_display: ['general','date_display'],");
  add("  theme_bg: ['theme','bg'], theme_card: ['theme','card'], theme_accent: ['theme','accent'], theme_text: ['theme','text'],");
  add("  night_mode: ['night','mode'], night_start: ['night','start'], night_end: ['night','end'], night_action: ['night','action'], night_dim_start: ['night','dim_start'],");
  add("  auto_cycle: ['screens','auto_cycle'], screen_int: ['screens','interval_sec'], brightness: ['screens','brightness'], power_saver: ['screens','power_saver'], anim_mask: ['screens','anim_mask'], screen_order: ['screens','order'],");
  add("  show_time: ['screens','show_time'], show_calendar: ['screens','show_calendar'], show_weather: ['screens','show_weather'], show_aqi: ['screens','show_aqi'],");
  add("  show_daylight: ['screens','show_daylight'], show_moon: ['screens','show_moon'], show_iss: ['screens','show_iss'], show_flight: ['screens','show_flight'], show_population: ['screens','show_population'], show_pc: ['screens','show_pc'],");
  add("  show_media: ['screens','show_media'], show_stock: ['screens','show_stock'], show_crypto: ['screens','show_crypto'], show_currency: ['screens','show_currency'],");
  add("  show_bambu: ['screens','show_bambu'], show_news: ['screens','show_news'], show_messages: ['screens','show_messages'], show_spotify: ['screens','show_spotify'], show_games: ['screens','show_games'], show_truco: ['screens','show_truco'],");
  add("  hide_empty_pc: ['screens','hide_empty_pc'], hide_empty_media: ['screens','hide_empty_media'], hide_empty_bambu: ['screens','hide_empty_bambu'], hide_empty_flight: ['screens','hide_empty_flight'],");
  add("  cal_start: ['calendar','start_day'], cal_hol: ['calendar','show_holidays'], cal_min: ['calendar','minimal'],");
  add("  temp_unit: ['weather','temp_unit'], round_temps: ['weather','round_temps'], weather_show_header: ['weather','show_header'], custom_weather_int_min: ['weather','custom_sync_min'], weather_values: ['weather','values'],");
  add("  aqi_type: ['aqi','type'], aqi_show_header: ['aqi','show_header'], custom_aqi_int_min: ['aqi','custom_sync_min'], aqi_values: ['aqi','values'],");
  add("  daylight_min: ['daylight','minimal'],");
  add("  moon_min: ['moon','minimal'],");
  add("  iss_mode: ['iss','mode'], iss_units: ['iss','units'], custom_iss_int_min: ['iss','custom_sync_min'],");
  add("  flight_mode: ['flight','mode'], flight_radius_nm: ['flight','radius_nm'], flight_units: ['flight','units'], flight_primary_info: ['flight','primary_info'], flight_secondary_info: ['flight','secondary_info'], custom_flight_int_min: ['flight','custom_sync_min'],");
  add("  pop_show_world: ['population','show_world'], pop_show_country: ['population','show_country'],");
  add("  stock_fn: ['stocks','fn'], custom_stock_int_min: ['stocks','custom_sync_min'], stock_symbols: ['stocks','symbols'],");
  add("  crypto_fn: ['crypto','fn'], custom_crypto_int_min: ['crypto','custom_sync_min'], crypto_ids: ['crypto','ids'],");
  add("  currency_fn: ['currency','fn'], custom_currency_int_min: ['currency','custom_sync_min'], currency_bases: ['currency','bases'], currency_targets: ['currency','targets'], currency_multipliers: ['currency','multipliers'],");
  add("  bambu_ip: ['printer','ip'], bambu_sn: ['printer','sn'], bambu_code: ['printer','code'],");
  add("};");

  add("document.querySelector('form').addEventListener('submit', function(e) {");
  add("  e.preventDefault();");
  add("  let mask = 0; document.querySelectorAll('.anim-chk').forEach(cb => { if(cb.checked) mask += parseInt(cb.value); });");
  add("  const formData = new FormData(e.target);");
  add("  const jsonObj = {};");
  add("  formData.forEach((value, key) => {");
  add("    if (value === 'on') jsonObj[key] = 1;");
  add("    else if (!isNaN(value) && value.trim() !== '') jsonObj[key] = Number(value);");
  add("    else jsonObj[key] = value;");
  add("  });");

  add("  e.target.querySelectorAll('input[type=\"checkbox\"]').forEach(cb => { jsonObj[cb.name] = cb.checked ? 1 : 0; });");
  add("  jsonObj['stock_symbols'] = Array.from(e.target.querySelectorAll('select[name=\"stock_symbols[]\"]')).map(s => s.value);");
  add("  jsonObj['crypto_ids'] = Array.from(e.target.querySelectorAll('select[name=\"crypto_ids[]\"]')).map(s => Number(s.value));");
  add("  jsonObj['currency_bases'] = Array.from(e.target.querySelectorAll('select[name=\"currency_bases[]\"]')).map(s => s.value);");
  add("  jsonObj['currency_targets'] = Array.from(e.target.querySelectorAll('select[name=\"currency_targets[]\"]')).map(s => s.value);");
  add("  jsonObj['currency_multipliers'] = Array.from(e.target.querySelectorAll('select[name=\"currency_multipliers[]\"]')).map(s => Number(s.value));");
  add("  jsonObj['weather_values'] = Array.from(e.target.querySelectorAll('.weather-val-chk:checked')).map(cb => cb.dataset.key);");
  add("  jsonObj['aqi_values'] = Array.from(e.target.querySelectorAll('.aqi-val-chk:checked')).map(cb => cb.dataset.key);");
  add("  const customSyncPairs = [['customWeatherSyncChk','customWeatherSyncInt','custom_weather_int_min'], ['customAqiSyncChk','customAqiSyncInt','custom_aqi_int_min'], ['customStockSyncChk','customStockSyncInt','custom_stock_int_min'], ['customCryptoSyncChk','customCryptoSyncInt','custom_crypto_int_min'], ['customCurrencySyncChk','customCurrencySyncInt','custom_currency_int_min'], ['customIssSyncChk','customIssSyncInt','custom_iss_int_min'], ['customFlightSyncChk','customFlightSyncInt','custom_flight_int_min']];");
  add("  customSyncPairs.forEach(([chkId, intId, key]) => { const chk = document.getElementById(chkId); const intEl = document.getElementById(intId); jsonObj[key] = (chk && chk.checked && intEl) ? Number(intEl.value) : -1; });");
  add("  jsonObj['anim_mask'] = mask;");
  add("  jsonObj['screen_order'] = document.getElementById('screenOrderInput').value;");

  add("  if (typeof jsonObj['city'] === 'string') {");
  add("    jsonObj['city'] = jsonObj['city'].trim().toLowerCase().replace(/(^|[\\s'-])[a-z]/g, m => m.toUpperCase());");
  add("  }");

  add("  const btn = document.querySelector('button[type=\"submit\"]');");
  add("  btn.innerText = '⏳ Guardando...';");
  add("  btn.style.opacity = '0.7';");
  add("  btn.disabled = true;");
  
  add("  const grouped = {};");
  add("  Object.keys(jsonObj).forEach(k => { const m = CONFIG_FIELD_MAP[k]; if (m) { if (!grouped[m[0]]) grouped[m[0]] = {}; grouped[m[0]][m[1]] = jsonObj[k]; } });");

  add("  fetch('/save', { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(grouped) })");
  add("    .then(r => {");
  add("      if (r.ok) {");
  add("        btn.innerText = '✅ ¡Guardado!';");
  add("        btn.style.backgroundColor = 'var(--text-main)';");
  add("        formDirty = false;");
  add("        setTimeout(updateData, 1000);");
  add("      } else { throw new Error('Backend Error'); }");
  add("    })");
  add("    .catch(e => {");
  add("      btn.innerText = '❌ No se pudo guardar';");
  add("      btn.style.backgroundColor = 'var(--text-main)';");
  add("    })");
  add("    .finally(() => {");
  add("      setTimeout(() => {");
  add("        btn.innerText = '💾 Guardar y aplicar todo';");
  add("        btn.style.backgroundColor = 'var(--primary-main)';");
  add("        btn.style.opacity = '1';");
  add("        btn.disabled = false;");
  add("      }, 3000);");
  add("    });");
  add("});");
  
  add("formDirty = false;");
  add("document.querySelector('form').addEventListener('input', () => formDirty = true);");
  add("document.querySelector('form').addEventListener('change', () => formDirty = true);");

  add("function updateData() { fetch('/update').then(r => r.json()).then(d => {");
  add("  const set = (id, val, html=false) => { const el = document.getElementById(id); if(el) { if(html) el.innerHTML = val; else el.innerText = val; return true; } return false; };");
  add("  const hide = (id, state) => { const el = document.getElementById(id); if(el) el.classList.toggle('hidden', state); };");

  add("  const setVal = (name, val) => { const el = document.querySelector('[name=\"'+name+'\"]'); if(el && document.activeElement !== el) el.value = val; };");
  add("  const setCb = (id, val, byName=false) => { const el = byName ? document.querySelector('[name=\"'+id+'\"]') : document.getElementById(id); if(el) el.checked = (val === 1 || val === true || val === '1'); };");
  add("  const setRadio = (name, val) => { const el = document.querySelector('[name=\"'+name+'\"][value=\"'+val+'\"]'); if(el) el.checked = true; };");

  add("  if (d.config !== undefined && !formDirty) {");
  add("    const c = d.config;");
  add("    setVal('theme_bg', c.theme.bg || '#000000'); setVal('theme_card', c.theme.card || '#111111'); setVal('theme_accent', c.theme.accent || '#ffffff'); setVal('theme_text', c.theme.text || '#ffffff'); applyLiveTheme();");
  add("    setVal('sda_pin', c.hardware.sda_pin);");
  add("    setVal('scl_pin', c.hardware.scl_pin);");
  add("    setVal('button_pin', c.hardware.button_pin);");
  add("    setRadio('button_type', c.hardware.button_type);");
  add("    updatePinSelects();");

  add("    setVal('refresh_min', c.general.refresh_min);");
  add("    setCb('autoCycle', c.screens.auto_cycle);");
  add("    if (c.screens.brightness) { setVal('brightness', c.screens.brightness); const bv = document.getElementById('brightnessVal'); if (bv) bv.innerText = c.screens.brightness; }");
  add("    setCb('powerSaver', c.screens.power_saver);");
  add("    setVal('screen_int', c.screens.interval_sec);");
  add("    setRadio('time_format', c.general.time_format);");

  add("    setCb('autoDetect', c.general.auto_detect);");
  add("    setVal('latitude', c.general.latitude);");
  add("    setVal('longitude', c.general.longitude);");
  add("    setVal('country', c.general.country);");
  add("    setVal('country_code', c.general.country_code);");
  add("    setVal('city', c.general.city);");
  add("    setVal('timezone', c.general.timezone);");

  add("    setCb('nightMode', c.night.mode);");
  add("    setVal('night_start', c.night.start);");
  add("    setVal('night_end', c.night.end);");
  add("    setVal('night_action', c.night.action);");
  add("    setVal('night_dim_start', c.night.dim_start);");
  add("    updateNightAction();");

  add("    setCb('showTime', c.screens.show_time);");
  add("    setCb('date_display', c.general.date_display, true);");

  add("    setCb('showCalendar', c.screens.show_calendar);");
  add("    setRadio('cal_start', c.calendar.start_day);");
  add("    setCb('cal_hol', c.calendar.show_holidays, true);");
  add("    setCb('cal_min', c.calendar.minimal, true);");

  add("    setCb('showWeather', c.screens.show_weather);");
  add("    setRadio('temp_unit', c.weather.temp_unit);");
  add("    setCb('round_temps', c.weather.round_temps, true);");
  add("    setRadio('weather_show_header', c.weather.show_header ? 1 : 0);");
  add("    document.querySelectorAll('.weather-val-chk').forEach(cb => { cb.checked = (c.weather.values || []).includes(cb.dataset.key); });");
  add("    updateValueLimits('weather', 6);");
  add("    setCb('customWeatherSyncChk', c.weather.custom_sync_min > 0 ? 1 : 0);");
  add("    setVal('custom_weather_int_min', c.weather.custom_sync_min > 0 ? c.weather.custom_sync_min : c.general.refresh_min);");

  add("    setCb('showAQI', c.screens.show_aqi);");
  add("    setRadio('aqi_type', c.aqi.type);");
  add("    setRadio('aqi_show_header', c.aqi.show_header ? 1 : 0);");
  add("    document.querySelectorAll('.aqi-val-chk').forEach(cb => { cb.checked = (c.aqi.values || []).includes(cb.dataset.key); });");
  add("    updateValueLimits('aqi', 6);");
  add("    setCb('customAqiSyncChk', c.aqi.custom_sync_min > 0 ? 1 : 0);");
  add("    setVal('custom_aqi_int_min', c.aqi.custom_sync_min > 0 ? c.aqi.custom_sync_min : c.general.refresh_min);");

  add("    setCb('showDaylight', c.screens.show_daylight);");
  add("    setCb('daylight_min', c.daylight.minimal, true);");

  add("    setCb('showMoon', c.screens.show_moon);");
  add("    setCb('moon_min', c.moon.minimal, true);");

  add("    setCb('showIss', c.screens.show_iss);");
  add("    setRadio('iss_mode', c.iss.mode);");
  add("    setRadio('iss_units', c.iss.units);");
  add("    setCb('customIssSyncChk', c.iss.custom_sync_min > 0 ? 1 : 0);");
  add("    setVal('custom_iss_int_min', c.iss.custom_sync_min > 0 ? c.iss.custom_sync_min : c.general.refresh_min);");

  add("    setCb('showFlight', c.screens.show_flight);");
  add("    setRadio('flight_mode', c.flight.mode);");
  add("    setVal('flight_radius_nm', c.flight.radius_nm);");
  add("    setRadio('flight_units', c.flight.units);");
  add("    setRadio('flight_primary_info', c.flight.primary_info);");
  add("    setRadio('flight_secondary_info', c.flight.secondary_info);");
  add("    setCb('customFlightSyncChk', c.flight.custom_sync_min > 0 ? 1 : 0);");
  add("    setVal('custom_flight_int_min', c.flight.custom_sync_min > 0 ? c.flight.custom_sync_min : c.general.refresh_min);");

  add("    setCb('showPopulation', c.screens.show_population);");
  add("    setCb('pop_show_world', c.population.show_world, true);");
  add("    setCb('pop_show_country', c.population.show_country, true);");

  add("    setCb('showPc', c.screens.show_pc);");

  add("    setCb('showStock', c.screens.show_stock); setCb('stock_fn', c.stocks.fn, true);");
  add("    setCb('customStockSyncChk', c.stocks.custom_sync_min > 0 ? 1 : 0);");
  add("    setVal('custom_stock_int_min', c.stocks.custom_sync_min > 0 ? c.stocks.custom_sync_min : c.general.refresh_min);");
  add("    const stCont = document.getElementById('stock-list-container'); if (stCont) { stCont.innerHTML = ''; (c.stocks.symbols && c.stocks.symbols.length > 0 ? c.stocks.symbols : ['AAPL']).forEach(s => window.addStockRow(s)); }");
  add("    setCb('showCrypto', c.screens.show_crypto); setCb('crypto_fn', c.crypto.fn, true);");
  add("    setCb('customCryptoSyncChk', c.crypto.custom_sync_min > 0 ? 1 : 0);");
  add("    setVal('custom_crypto_int_min', c.crypto.custom_sync_min > 0 ? c.crypto.custom_sync_min : c.general.refresh_min);");
  add("    const crCont = document.getElementById('crypto-list-container'); if (crCont) { crCont.innerHTML = ''; (c.crypto.ids && c.crypto.ids.length > 0 ? c.crypto.ids : [90]).forEach(cId => window.addCryptoRow(cId)); }");
  add("    setCb('showCurrency', c.screens.show_currency); setCb('currency_fn', c.currency.fn, true);");
  add("    setCb('customCurrencySyncChk', c.currency.custom_sync_min > 0 ? 1 : 0);");
  add("    setVal('custom_currency_int_min', c.currency.custom_sync_min > 0 ? c.currency.custom_sync_min : c.general.refresh_min);");
  add("    const cuCont = document.getElementById('currency-list-container'); if (cuCont) { cuCont.innerHTML = ''; if (c.currency.bases && c.currency.bases.length > 0) { for(let i=0; i<c.currency.bases.length; i++) window.addCurrencyRow(c.currency.bases[i], c.currency.targets[i], c.currency.multipliers[i]); } else { window.addCurrencyRow('usd', 'eur', 1); } }");

  add("    setCb('showMedia', c.screens.show_media);");

  add("    setCb('showBambu', c.screens.show_bambu);");
  add("    setCb('showNews', c.screens.show_news);");
  add("    setCb('showMessages', c.screens.show_messages);");
  add("    setCb('showSpotify', c.screens.show_spotify);");
  add("    setCb('showGames', c.screens.show_games);");
  add("    setCb('showTruco', c.screens.show_truco);");
  add("    setVal('bambu_ip', c.printer.ip);");
  add("    setVal('bambu_sn', c.printer.sn);");
  add("    setVal('bambu_code', c.printer.code);");

  add("    setCb('hide_empty_pc', c.screens.hide_empty_pc, true);");
  add("    setCb('hide_empty_media', c.screens.hide_empty_media, true);");
  add("    setCb('hide_empty_bambu', c.screens.hide_empty_bambu, true);");
  add("    setCb('hide_empty_flight', c.screens.hide_empty_flight, true);");

  add("    const mask = c.screens.anim_mask;");
  add("    document.querySelectorAll('.anim-chk').forEach(cb => { cb.checked = (mask & parseInt(cb.value)) !== 0; });");
  add("    const noneBox = document.getElementById('animNone');");
  add("    if (noneBox) { noneBox.checked = (mask === 0); toggleNone(); }");

  add("    if (c.screens.order && !document.querySelector('.dragging')) {");
  add("      const orderArr = c.screens.order.split(',');");
  add("      const list = document.getElementById('sortable-list');");
  add("      if (list) {");
  add("        const items = [...list.querySelectorAll('.sortable-item')];");
  add("        orderArr.forEach(id => { const item = items.find(el => el.getAttribute('data-id') === id); if(item) list.appendChild(item); });");
  add("        updateOrderValue();");
  add("      }");
  add("    }");

  add("    updateVisibility();");
  add("    syncScreenOrder();");
  add("    formDirty = false;");
  add("  }");

  add("  const st = d.status || {};");
  add("  set('time-display', st.general && st.general.time);");
  add("  set('preview-time', st.general && st.general.time);");
  add("  set('preview-date', st.general && st.general.date);");

  add("  set('preview-tz', d.config && d.config.general.timezone);");
  add("  if (st.calendar && st.calendar.count !== undefined) { set('preview-hol', st.calendar.count > 0 ? st.calendar.count : 'Sin feriados'); }");

  add("  updateLiveHeader();");

  add("  const weatherFieldMap = { feels: 'apparent_temperature', humidity: 'humidity', wind: 'wind_speed', precipitation: 'precipitation_probability', pressure: 'pressure', visibility: 'visibility' };");
  add("  if (st.weather && st.weather.temp !== undefined && st.weather.temp !== 'nan') {");
  add("    const tempUnit = d.config ? d.config.weather.temp_unit : 'C';");
  add("    if (!set('value-temp', st.weather.temp + ' °' + tempUnit)) { location.reload(); return; }");
  add("    hide('weather-no-data', true); hide('weather-grid', false);");
  add("    const weatherUnitMap = { feels: ' °'+tempUnit, humidity: '%', wind: ' km/h', precipitation: '%', pressure: ' hPa', visibility: ' km' };");
  add("    Object.keys(weatherFieldMap).forEach(k => { const raw = st.weather[weatherFieldMap[k]]; if (raw !== undefined && raw !== 'nan') set('value-'+k, raw + weatherUnitMap[k]); });");
  add("    set('weather-upd', 'Actualizado: ' + st.weather.update_time);");
  add("  } else { hide('weather-no-data', false); hide('weather-grid', true); }");

  add("  const aqiUnitMap = { pm25: ' <small>µg</small>', pm10: ' <small>µg</small>', no2: ' <small>µg</small>', co: ' <small>µg</small>', co2: ' <small>ppm</small>', so2: ' <small>µg</small>', o3: ' <small>µg</small>', dust: ' <small>µg</small>', uv: '', ch4: ' <small>ppb</small>' };");
  add("  if (st.aqi && st.aqi.index !== undefined && st.aqi.index !== 'nan') {");
  add("    if (!set('value-aqi', st.aqi.index)) { location.reload(); return; }");
  add("    hide('aqi-no-data', true); hide('aqi-grid', false);");
  add("    const aqiLabel = document.querySelector('#value-aqi + .tile-label'); if(aqiLabel) aqiLabel.innerText = 'Aire: ' + st.aqi.status;");
  add("    Object.keys(aqiUnitMap).forEach(k => { const raw = st.aqi[k]; if (raw !== undefined && raw !== 'nan') set('value-'+k, raw + aqiUnitMap[k], true); });");
  add("    set('aqi-upd', 'Actualizado: ' + st.weather.update_time);");
  add("  } else { hide('aqi-no-data', false); hide('aqi-grid', true); }");

  add("  if (st.daylight && st.daylight.sunrise !== undefined) {");
  add("    hide('daylight-no-data', true); hide('daylight-grid', false);");
  add("    set('val-sunrise', st.daylight.sunrise);");
  add("    set('val-sunset', st.daylight.sunset);");
  add("    set('val-noon', st.daylight.solar_noon);");
  add("    set('val-length', st.daylight.day_length);");
  add("  } else { hide('daylight-no-data', false); hide('daylight-grid', true); }");

  add("  if (st.moon && st.moon.phase !== undefined) {");
  add("    hide('moon-no-data', true); hide('moon-grid', false);");
  add("    set('val-moon-phase', st.moon.phase);");
  add("    set('val-moon-illum', st.moon.illum + '%');");
  add("    set('val-moon-rise', st.moon.rise);");
  add("    set('val-moon-set', st.moon.set);");
  add("  } else { hide('moon-no-data', false); hide('moon-grid', true); }");

  add("  if (st.iss && st.iss.over !== undefined) {");
  add("    hide('iss-no-data', true); hide('iss-grid', false);");
  add("    set('val-iss-over', st.iss.over || '--');");
  add("    set('val-iss-alt', st.iss.altitude);");
  add("    set('val-iss-speed', st.iss.velocity);");
  add("    set('val-iss-dist', st.iss.distance);");
  add("  } else { hide('iss-no-data', false); hide('iss-grid', true); }");

  add("  if (st.population && (st.population.world_live !== undefined || st.population.country_live !== undefined)) {");
  add("    hide('pop-no-data', true); hide('pop-grid', false);");
  add("    const formatNum = (str) => { return str.replace(/\\B(?=(\\d{3})+(?!\\d))/g, ','); };");
  add("    if (st.population.world_live !== undefined) {");
  add("      set('val-pop-wld', formatNum(st.population.world_live));");
  add("      set('val-pop-wld-gr', (parseFloat(st.population.world_growth) > 0 ? '+' : '') + st.population.world_growth + '%');");
  add("      set('lbl-pop-wld', 'Población mundial');");
  add("    }");
  add("    if (st.population.country_live !== undefined) {");
  add("      set('val-pop-ctr', formatNum(st.population.country_live));");
  add("      set('val-pop-ctr-gr', (parseFloat(st.population.country_growth) > 0 ? '+' : '') + st.population.country_growth + '%');");
  add("      const cCode = (d.config && d.config.general.country_code) ? d.config.general.country_code.toUpperCase() : 'CTR';");
  add("      set('lbl-pop-ctr', cCode + ' Population');");
  add("      set('lbl-pop-ctr-gr', cCode + ' Growth');");
  add("    }");
  add("  } else { hide('pop-no-data', false); hide('pop-grid', true); }");

  add("  if (st.stocks && st.stocks.data && st.stocks.data.length > 0) {");
  add("    hide('stock-no-data', true); hide('stock-grid', false); let p='', c='';");
  add("    st.stocks.data.forEach(s => { p += s.symbol + ': $' + s.price + '<br>'; c += (parseFloat(s.change) >= 0 ? '+' : '') + s.change + '%<br>'; });");
  add("    set('stock-price', p, true); set('stock-change', c, true); set('stock-upd', 'Actualizado: ' + st.weather.update_time);");
  add("  } else { hide('stock-no-data', false); hide('stock-grid', true); }");

  add("  if (st.crypto && st.crypto.data && st.crypto.data.length > 0) {");
  add("    hide('crypto-no-data', true); hide('crypto-grid', false); let p='', c='';");
  add("    st.crypto.data.forEach(s => { p += s.symbol + ': $' + s.price + '<br>'; c += (parseFloat(s.change) >= 0 ? '+' : '') + s.change + '%<br>'; });");
  add("    set('crypto-price', p, true); set('crypto-change', c, true); set('crypto-upd', 'Actualizado: ' + st.weather.update_time);");
  add("  } else { hide('crypto-no-data', false); hide('crypto-grid', true); }");

  add("  if (st.currency && st.currency.data && st.currency.data.length > 0) {");
  add("    hide('currency-no-data', true); hide('currency-grid', false); let b='', t='';");
  add("    st.currency.data.forEach(s => { b += s.base_text + '<br>'; t += s.target_text + '<br>'; });");
  add("    set('currency-base-val', b, true); set('currency-target-val', t, true); set('currency-upd', 'Actualizado: ' + st.weather.update_time);");
  add("  } else { hide('currency-no-data', false); hide('currency-grid', true); }");

  add("  if (st.pc && st.pc.cpu !== undefined && st.pc.cpu !== '0.00' && st.pc.cpu !== '0') {");
  add("    if (!set('pc-cpu', Math.round(parseFloat(st.pc.cpu)) + '%')) { location.reload(); return; }");
  add("    hide('pc-no-data', true); hide('pc-grid', false);");
  add("    set('pc-net', Math.round(parseFloat(st.pc.net)) + ' KB/s');");
  add("    set('pc-ram', Math.round(parseFloat(st.pc.ram)) + '%');");
  add("    set('pc-disk', Math.round(parseFloat(st.pc.disk)) + '%');");
  add("  } else { hide('pc-no-data', false); hide('pc-grid', true); }");

  add("  if (st.media && st.media.name && st.media.name !== '' && st.media.author && st.media.author !== '') {");
  add("    hide('media-no-data', true); hide('media-grid', false);");
  add("    let s = st.media.status || 'stopped';");
  add("    set('web-media-status', s.charAt(0).toUpperCase() + s.slice(1));");
  add("    set('web-media-name', st.media.name);");
  add("    set('web-media-author', st.media.author);");
  add("    set('web-media-album', st.media.album || 'Desconocido');");
  add("  } else { hide('media-no-data', false); hide('media-grid', true); }");

  add("  if (st.printer !== undefined) {");
  add("    hide('bambu-no-data', true); hide('bambu-grid', false);");
  add("    set('bambu-status', st.printer.status);");
  add("    set('bambu-prog', st.printer.progress + '% | ' + st.printer.time + 'm<br><span style=\"font-size:0.9rem\">Capa: ' + st.printer.layer + '/' + st.printer.total_layers + '</span>', true);");
  add("    set('bambu-temps', 'Pico: ' + parseFloat(st.printer.nozzle).toFixed(1) + '/' + parseFloat(st.printer.nozzle_target).toFixed(1) + '<br>Cama: ' + parseFloat(st.printer.bed).toFixed(1) + '/' + parseFloat(st.printer.bed_target).toFixed(1), true);");
  add("    set('bambu-fans', 'Pieza: ' + st.printer.fan_part + ' | Aux: ' + st.printer.fan_aux);");
  add("  } else { hide('bambu-no-data', false); hide('bambu-grid', true); }");

  add("  if (st.flight !== undefined) {");
  add("    hide('flight-no-data', true); hide('flight-grid', false);");
  add("    set('flight-count', st.flight.count);");
  add("    var flClosest = st.flight.callsign ? st.flight.callsign : '--';");
  add("    if (st.flight.route !== undefined) { flClosest += \"<br><span style='font-size:0.8rem'>\" + st.flight.route + '</span>'; }");
  add("    set('flight-closest', flClosest, true);");
  add("  } else { hide('flight-no-data', false); hide('flight-grid', true); }");

  add("  if (st.pc && st.pc.status !== undefined) set('pc-link-status', st.pc.status);");
  add("}).catch(e => console.log('Sync error:', e)); } setInterval(updateData, 15000); updateData();");
  add("</script></div></body></html>");

  if (chunk.length() > 0) {
    server.sendContent(chunk);
  }
  server.sendContent(""); 
}

void WebServerService::handleSave() {
  gLastWebActivity = millis();
  if (!server.hasArg("plain")) {
    server.send(HTTP_BAD_REQUEST, "text/plain", "Body not received");
    return;
  }
  
  if (isLoadingData()) {
    server.send(503, "text/plain", "Cargando datos, probá de nuevo en unos segundos");
    return;
  }

  String body = server.arg("plain");
  
  if (JsonSerializer::parseConfig(body.c_str(), *state)) {
    if (saveCallback) saveCallback();
    server.send(HTTP_OK, "text/plain", "Saved");
  } else {
    server.send(HTTP_BAD_REQUEST, "text/plain", "Invalid JSON");
  }
}

void WebServerService::handleUpdate() {
  gLastWebActivity = millis();
  if (isLoadingData()) {
    server.send(503, "application/json", "{}");
    return;
  }
  String jsonResponse = JsonSerializer::buildAppStateJson(*state);
  server.send(HTTP_OK, "application/json", jsonResponse);
}

void WebServerService::handlePcStats() {
  if (!server.hasArg("plain")) {
    server.send(HTTP_BAD_REQUEST, "application/json", "{\"status\":\"error\", \"message\":\"Body not received\"}");
    return;
  }
  
  String body = server.arg("plain");
  
  DynamicJsonDocument doc(1024); 
  DeserializationError error = deserializeJson(doc, body);
  
  if (error) {
    server.send(HTTP_BAD_REQUEST, "application/json", "{\"status\":\"error\", \"message\":\"Invalid JSON\"}");
    return;
  }

  String incoming_pc_id = doc["pc_id"] | "";
  if (incoming_pc_id == "") {
    server.send(HTTP_BAD_REQUEST, "application/json", "{\"status\":\"error\", \"message\":\"Missing PC ID\"}");
    return;
  }

  if (millis() - state->pc.last_update > PC_DATA_TIMEOUT_MS || state->config.active_pc_id == incoming_pc_id || state->config.active_pc_id == "") {
    
    state->config.active_pc_id = incoming_pc_id;
    
    state->pc.cpu_percent = doc["cpu_percent"] | 0.0;
    state->pc.mem_percent = doc["mem_percent"] | 0.0;
    state->pc.disk_percent = doc["disk_percent"] | 0.0;
    state->pc.net_down_kb = doc["net_down_kb"] | 0.0;
    
    state->pc.last_update = millis();
    state->pc.is_wifi = true;

    state->media.status = doc["media_status"] | "stopped";
    state->media.name = doc["media_name"] | "";
    state->media.author = doc["media_author"] | "";
    state->media.album = doc["media_album"] | "";
    state->media.last_update = millis();

    server.send(HTTP_OK, "application/json", "{\"status\":\"ok\"}");
  } else {
    server.send(HTTP_FORBIDDEN, "application/json", "{\"status\":\"error\", \"message\":\"Device is already paired to another PC\"}");
  }
}

void WebServerService::handleUsbReconnect() {
  server.send(HTTP_OK, "application/json", "{\"status\":\"ok\"}");
  PcMonitorService::reconnectUsb();
}

// ---------------------------------------------------------------------------
//  OTA from the browser: GET /ota shows the page, POST /ota receives firmware.bin
// ---------------------------------------------------------------------------

void WebServerService::handleOtaPage() {
  gLastWebActivity = millis();
  if (!server.authenticate(OTA_WEB_USER, OTA_PASSWORD)) {
    server.requestAuthentication(BASIC_AUTH, "Mini C3", "Usuario: admin, clave: la de OTA");
    return;
  }

  String html;
  html.reserve(3000);
  html += F("<!doctype html><html lang='es'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>Mini C3 | OTA</title><style>");
  html += F("body{font-family:system-ui,sans-serif;background:#000;color:#eee;max-width:480px;margin:32px auto;padding:0 16px}");
  html += F("h2{margin:0 0 4px}p{color:#aaa;margin:6px 0 18px}.box{background:#111;border:1px solid #333;border-radius:14px;padding:18px}");
  html += F("input[type=file]{width:100%;margin:8px 0 14px;color:#ddd}button{width:100%;padding:12px;border:0;border-radius:10px;background:#fff;color:#000;font-weight:700;font-size:1rem}");
  html += F("button:disabled{opacity:.5}progress{width:100%;height:14px;margin-top:16px}#s{margin-top:10px;min-height:1.4em}a{color:#aaa}");
  html += F("</style></head><body><h2>Actualizar firmware</h2>");
  html += "<p>Mini C3 &middot; versi&oacute;n actual <b>v" FW_VERSION "</b></p>";
  html += F("<div class='box'><label>Archivo <code>firmware.bin</code> (no el <i>factory</i>)</label>");
  html += F("<input type='file' id='f' accept='.bin'><button id='b'>Subir y actualizar</button>");
  html += F("<progress id='p' max='100' value='0'></progress><div id='s'></div></div>");
  html += F("<p><a href='/'>&larr; Volver al panel</a></p>");
  html += F("<script>const f=document.getElementById('f'),b=document.getElementById('b'),p=document.getElementById('p'),s=document.getElementById('s');");
  html += F("b.onclick=()=>{const file=f.files[0];if(!file){s.textContent='Eleg\u00ed el firmware.bin';return;}");
  html += F("if(/factory/i.test(file.name)){s.textContent='Ese es el factory.bin: para OTA va firmware.bin';return;}");
  html += F("b.disabled=true;s.textContent='Subiendo...';const x=new XMLHttpRequest();x.open('POST','/ota');");
  html += F("x.upload.onprogress=e=>{if(e.lengthComputable)p.value=Math.round(e.loaded*100/e.total);};");
  html += F("x.onload=()=>{if(x.status==200){p.value=100;s.textContent='Listo. Reiniciando... (recarg\u00e1 en 10 s)';}else{s.textContent='Error: '+x.responseText;b.disabled=false;}};");
  html += F("x.onerror=()=>{s.textContent='Se cort\u00f3 la conexi\u00f3n';b.disabled=false;};");
  html += F("const fd=new FormData();fd.append('firmware',file,file.name);x.send(fd);};</script></body></html>");

  server.send(HTTP_OK, "text/html", html);
}

void WebServerService::handleOtaUpload() {
  HTTPUpload& up = server.upload();

  if (up.status == UPLOAD_FILE_START) {
    otaAuthOk = server.authenticate(OTA_WEB_USER, OTA_PASSWORD);
    otaError = "";
    otaWritten = 0;
    otaFirstChunk = true;
    otaExpected = server.clientContentLength();
    if (!otaAuthOk) return;

    Serial.printf("OTA web: recibiendo %s\n", up.filename.c_str());
    OtaService::uiStart("Navegador");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      otaError = String("No se pudo iniciar: ") + Update.errorString();
    }

  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!otaAuthOk || otaError.length() > 0) return;

    if (otaFirstChunk) {
      otaFirstChunk = false;
      // An application image carries esp_app_desc_t (magic 0xABCD5432) right
      // after the image + segment headers. A factory.bin starts with the
      // bootloader instead and would brick the update slot.
      bool isApp = up.currentSize >= 36 && up.buf[0] == 0xE9 &&
                   up.buf[32] == 0x32 && up.buf[33] == 0x54 && up.buf[34] == 0xCD && up.buf[35] == 0xAB;
      if (!isApp) {
        otaError = "Ese archivo no es un firmware.bin de aplicacion";
        Update.abort();
        return;
      }
    }

    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      otaError = String("Error escribiendo: ") + Update.errorString();
      Update.abort();
      return;
    }
    otaWritten += up.currentSize;
    OtaService::uiProgress(otaWritten, otaExpected > 0 ? (size_t)otaExpected : otaWritten);

  } else if (up.status == UPLOAD_FILE_END) {
    if (!otaAuthOk || otaError.length() > 0) return;
    if (!Update.end(true)) {
      otaError = String("Firmware invalido: ") + Update.errorString();
    } else {
      Serial.printf("OTA web: %u bytes OK\n", (unsigned)otaWritten);
    }

  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (Update.isRunning()) Update.abort();
    otaError = "Subida cortada";
  }
}

void WebServerService::handleOtaDone() {
  if (!otaAuthOk) {
    server.requestAuthentication(BASIC_AUTH, "Mini C3", "Usuario: admin, clave: la de OTA");
    return;
  }

  server.sendHeader("Connection", "close");
  if (otaError.length() > 0 || Update.hasError() || !Update.isFinished()) {
    String msg = otaError.length() > 0 ? otaError : String(Update.errorString());
    server.send(500, "text/plain", msg);
    OtaService::uiEnd(false, msg.substring(0, 20));
    return;
  }

  server.send(HTTP_OK, "text/plain", "OK");
  OtaService::uiEnd(true, "Reiniciando...");
  delay(800);
  ESP.restart();
}

// ---------------------------------------------------------------------------
//  Mensajes: page to send a message to the Mini C3 from the same WiFi
// ---------------------------------------------------------------------------

void WebServerService::handleMessagePage() {
  gLastWebActivity = millis();
  String html;
  html.reserve(4200);
  html += F("<!doctype html><html lang='es'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>Mini C3 | Mensaje</title><style>");
  html += F("*{box-sizing:border-box}body{font-family:system-ui,sans-serif;background:#0b0d10;color:#eee;max-width:480px;margin:0 auto;padding:28px 16px}");
  html += F("h2{margin:0 0 4px}p{color:#9aa;margin:4px 0 20px}.box{background:#15181d;border:1px solid #2a2f36;border-radius:16px;padding:18px}");
  html += F("label{display:block;font-size:.9rem;color:#bbb;margin:0 0 6px}input,textarea{width:100%;background:#0b0d10;color:#fff;border:1px solid #333;border-radius:10px;padding:12px;font-size:1rem;margin-bottom:14px;font-family:inherit}");
  html += F("textarea{min-height:130px;resize:vertical}.cnt{float:right;color:#777;font-size:.8rem}button{width:100%;padding:14px;border:0;border-radius:12px;background:#fff;color:#000;font-weight:700;font-size:1.05rem}");
  html += F("button:disabled{opacity:.5}#s{margin-top:14px;min-height:1.4em;font-weight:600}.ok{color:#7dd87d}.err{color:#f77}a{color:#9aa}");
  html += F("</style></head><body><h2>✉️ Mensaje para la Mini C3</h2><p>Le llega al instante y titila la luz hasta que lo leen.</p>");
  html += F("<div class='box'><label for='de'>De</label><input id='de' maxlength='20' placeholder='Tu nombre'>");
  html += F("<label for='tx'>Mensaje <span class='cnt' id='c'>0/500</span></label><textarea id='tx' maxlength='500' placeholder='Escribí acá...'></textarea>");
  html += F("<button id='b'>Enviar</button><div id='s'></div></div><p style='margin-top:18px'><a href='/'>&larr; Volver al panel</a></p>");
  html += F("<script>const de=document.getElementById('de'),tx=document.getElementById('tx'),b=document.getElementById('b'),s=document.getElementById('s'),c=document.getElementById('c');");
  html += F("try{de.value=localStorage.getItem('minic3_de')||'';}catch(e){}");
  html += F("tx.oninput=()=>{c.textContent=tx.value.length+'/500';};let poll=null;");
  html += F("function st(t,k){s.textContent=t;s.className=k||'';}");
  html += F("b.onclick=async()=>{const t=tx.value.trim();if(!t){st('Escribí un mensaje','err');return;}");
  html += F("try{localStorage.setItem('minic3_de',de.value);}catch(e){}b.disabled=true;st('Enviando...');");
  html += F("try{const r=await fetch('/mensaje',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'de='+encodeURIComponent(de.value)+'&texto='+encodeURIComponent(t)});");
  html += F("const j=await r.json();if(!r.ok||!j.id)throw 0;tx.value='';c.textContent='0/500';st('✓ Entregado','ok');");
  html += F("if(poll)clearInterval(poll);poll=setInterval(async()=>{try{const q=await fetch('/api/mensaje?id='+j.id);const k=await q.json();if(k.leido){st('✓✓ Leído','ok');clearInterval(poll);}}catch(e){}},2000);");
  html += F("}catch(e){st('No se pudo enviar','err');}b.disabled=false;};</script></body></html>");
  server.send(HTTP_OK, "text/html", html);
}

void WebServerService::handleMessagePost() {
  String text = server.arg("texto");
  if (text.length() == 0) text = server.arg("text");
  String from = server.arg("de");
  uint32_t id = messageService.add(from, text, TimeService::getCurrentTime(state->config.time_format));
  if (id == 0) {
    server.send(HTTP_BAD_REQUEST, "application/json", "{\"error\":\"mensaje vacio\"}");
    return;
  }
  server.send(HTTP_OK, "application/json", "{\"id\":" + String(id) + "}");
}

void WebServerService::handleMessageStatus() {
  uint32_t id = server.arg("id").toInt();
  bool read = messageService.isRead(id);
  server.send(HTTP_OK, "application/json", String("{\"leido\":") + (read ? "true" : "false") + "}");
}

// ---------------------------------------------------------------------------
//  Spotify: link the account (PKCE, no server)
// ---------------------------------------------------------------------------

void WebServerService::handleSpotifyPage() {
  gLastWebActivity = millis();
  const SpotifyState& sp = state->spotify;
  bool linked = spotifyService.isConfigured();

  String html;
  html.reserve(6500);
  html += F("<!doctype html><html lang='es'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>Mini C3 | Spotify</title><style>");
  html += F("*{box-sizing:border-box}body{font-family:system-ui,sans-serif;background:#0b0d10;color:#eee;max-width:560px;margin:0 auto;padding:28px 16px;line-height:1.45}");
  html += F("h2{margin:0 0 4px}h3{margin:0 0 8px;font-size:1rem}.box{background:#15181d;border:1px solid #2a2f36;border-radius:16px;padding:18px;margin:14px 0}");
  html += F(".n{display:inline-block;width:24px;height:24px;border-radius:50%;background:#1db954;color:#000;text-align:center;font-weight:800;margin-right:8px}");
  html += F("input{width:100%;background:#0b0d10;color:#fff;border:1px solid #333;border-radius:10px;padding:12px;font-size:.95rem;margin:8px 0}");
  html += F("button{width:100%;padding:13px;border:0;border-radius:12px;background:#1db954;color:#000;font-weight:800;font-size:1rem;margin-top:4px}button.sec{background:#333;color:#eee}");
  html += F("code{background:#000;padding:2px 6px;border-radius:6px;user-select:all}.mut{color:#9aa;font-size:.9rem}.ok{color:#1db954;font-weight:700}.err{color:#f77;font-weight:600}a{color:#1db954}ul{margin:6px 0;padding-left:20px}");
  html += F("</style></head><body><h2>🎵 Spotify en la Mini C3</h2>");

  if (linked) {
    html += F("<div class='box'><h3 class='ok'>✓ Cuenta vinculada</h3>");
    if (sp.user.length()) html += "<p>Usuario: <b>" + sp.user + "</b></p>";
    String st;
    switch (sp.status) {
      case SP_ACTIVE: st = String(sp.playing ? "Sonando: " : "En pausa: ") + sp.title + " &middot; " + sp.artist; break;
      case SP_IDLE: st = "Nada sonando ahora"; break;
      case SP_CONNECTING: st = "Conectando..."; break;
      default: st = "Error: " + sp.error; break;
    }
    html += "<p class='mut'>" + st + "</p>";
    html += F("<p class='mut'>En el equipo: <b>A</b> play/pausa &middot; <b>&larr; &rarr;</b> tema anterior/siguiente &middot; <b>A mantenida</b> volumen con &uarr; &darr;.</p>");
    html += F("<button class='sec' onclick=\"if(confirm('¿Desvincular Spotify?'))fetch('/spotify/unlink',{method:'POST'}).then(()=>location.reload())\">Desvincular</button></div>");
  } else {
    html += F("<p class='mut'>Se hace una sola vez y tarda 5 minutos. Necesit&aacute;s <b>Spotify Premium</b>: Spotify lo exige para las apps propias y para controlar la m&uacute;sica.</p>");
    html += F("<div class='box'><h3><span class='n'>1</span>Cre&aacute; tu app en Spotify</h3><ul>");
    html += F("<li>Entr&aacute; a <a href='https://developer.spotify.com/dashboard' target='_blank'>developer.spotify.com/dashboard</a> con tu cuenta y toc&aacute; <b>Create app</b>.</li>");
    html += F("<li>Nombre: <code>Mini C3</code> (la descripci&oacute;n, cualquiera).</li>");
    html += "<li>Redirect URI: <code>" + String(SpotifyService::REDIRECT_URI) + "</code> y toc&aacute; <b>Add</b>.</li>";
    html += F("<li>Marc&aacute; <b>Web API</b>, acept&aacute; y guard&aacute;. Despu&eacute;s copi&aacute; el <b>Client ID</b>.</li></ul></div>");
    html += F("<div class='box'><h3><span class='n'>2</span>Peg&aacute; el Client ID y autoriz&aacute;</h3>");
    html += "<input id='cid' placeholder='Client ID' value='" + spotifyService.getClientId() + "'>";
    html += F("<button id='go'>Autorizar en Spotify</button><p class='mut'>Se abre Spotify en otra pesta&ntilde;a: toc&aacute; <b>Aceptar</b>.</p></div>");
    html += F("<div class='box'><h3><span class='n'>3</span>Peg&aacute; la direcci&oacute;n a la que te lleva</h3>");
    html += F("<p class='mut'>Despu&eacute;s de aceptar, esa pesta&ntilde;a va a mostrar un error tipo <i>&laquo;no se puede acceder&raquo;</i>: es normal. Copi&aacute; la direcci&oacute;n completa de arriba (empieza con <code>http://127.0.0.1:8888/callback?code=</code>) y pegala ac&aacute;.</p>");
    html += F("<input id='url' placeholder='http://127.0.0.1:8888/callback?code=...'><button id='fin'>Vincular</button><p id='s'></p></div>");
    html += F("<script>const $=i=>document.getElementById(i),s=$('s');");
    html += F("$('go').onclick=async()=>{const c=$('cid').value.trim();if(c.length<20){alert('Peg\\u00e1 el Client ID');return;}");
    html += F("const w=window.open('about:blank','_blank');const r=await fetch('/spotify/start',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'client_id='+encodeURIComponent(c)});");
    html += F("const j=await r.json();if(w)w.location=j.url;else location.href=j.url;};");
    html += F("$('fin').onclick=async()=>{const u=$('url').value.trim();if(!u){s.textContent='Peg\\u00e1 la direcci\\u00f3n';s.className='err';return;}");
    html += F("s.textContent='Vinculando...';s.className='mut';try{const r=await fetch('/spotify/finish',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'url='+encodeURIComponent(u)});");
    html += F("const j=await r.json();if(j.ok){s.textContent='\\u2713 Listo'+(j.user?', '+j.user:'')+'! Mir\\u00e1 la Mini C3.';s.className='ok';setTimeout(()=>location.reload(),2500);}else{s.textContent=j.error;s.className='err';}}catch(e){s.textContent='No se pudo vincular';s.className='err';}};</script>");
  }
  html += F("<p><a href='/'>&larr; Volver al panel</a></p></body></html>");
  server.send(HTTP_OK, "text/html", html);
}

void WebServerService::handleSpotifyStart() {
  String cid = server.arg("client_id");
  cid.trim();
  if (cid.length() < 20) {
    server.send(HTTP_BAD_REQUEST, "application/json", "{\"error\":\"client id\"}");
    return;
  }
  String url = spotifyService.startAuth(cid);
  DynamicJsonDocument doc(1024);
  doc["url"] = url;
  String out;
  serializeJson(doc, out);
  server.send(HTTP_OK, "application/json", out);
}

void WebServerService::handleSpotifyFinish() {
  String error;
  bool ok = spotifyService.finishAuth(server.arg("url"), error);
  DynamicJsonDocument doc(512);
  doc["ok"] = ok;
  if (!ok) doc["error"] = error;
  else doc["user"] = state->spotify.user;
  String out;
  serializeJson(doc, out);
  server.send(HTTP_OK, "application/json", out);
}

void WebServerService::handleSpotifyUnlink() {
  spotifyService.unlink();
  server.send(HTTP_OK, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
//  WiFi: saved networks (Mini C3 v1.5.0)
// ---------------------------------------------------------------------------

static String htmlEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '&') out += "&amp;";
    else if (c == '\'') out += "&#39;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

bool WebServerService::wifiAuth() {
  if (server.authenticate(OTA_WEB_USER, OTA_PASSWORD)) return true;
  server.requestAuthentication(BASIC_AUTH, "Mini C3", "Usuario: admin, clave: la de OTA");
  return false;
}

void WebServerService::handleWifiPage() {
  gLastWebActivity = millis();
  if (!wifiAuth()) return;
  bool online = (WiFi.status() == WL_CONNECTED);
  String current = online ? WiFi.SSID() : String("");

  String html;
  html.reserve(7000);
  html += F("<!doctype html><html lang='es'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>Mini C3 | WiFi</title><style>");
  html += F("*{box-sizing:border-box}body{font-family:system-ui,sans-serif;background:#0b0d10;color:#eee;max-width:560px;margin:0 auto;padding:28px 16px;line-height:1.45}");
  html += F("h2{margin:0 0 4px}h3{margin:0 0 10px;font-size:1rem}.box{background:#15181d;border:1px solid #2a2f36;border-radius:16px;padding:18px;margin:14px 0}");
  html += F(".row{display:flex;align-items:center;gap:8px;padding:10px 0;border-top:1px solid #23272e}.row:first-of-type{border-top:0}.name{flex:1;font-weight:600;word-break:break-all}");
  html += F(".tag{font-size:.75rem;background:#1f6f3f;color:#cfe;padding:2px 8px;border-radius:99px;margin-left:6px;font-weight:600}.mut{color:#9aa;font-size:.9rem}");
  html += F("input{width:100%;background:#0b0d10;color:#fff;border:1px solid #333;border-radius:10px;padding:12px;font-size:.95rem;margin:6px 0}");
  html += F("button{padding:9px 14px;border:0;border-radius:10px;background:#4da3ff;color:#000;font-weight:700;font-size:.9rem}button.sec{background:#333;color:#eee}button.big{width:100%;padding:13px;font-size:1rem;margin-top:6px}");
  html += F(".net{cursor:pointer}.net:hover .name{color:#4da3ff}.bars{font-family:monospace;color:#4da3ff;width:38px}a{color:#4da3ff}.ok{color:#5d5}.err{color:#f77}");
  html += F("</style></head><body><h2>📶 WiFi de la Mini C3</h2>");
  html += F("<p class='mut'>Cambiar o agregar redes <b>no borra nada</b>: pantallas, Spotify y mensajes quedan como estaban. Al prender, la Mini C3 se conecta sola a la red guardada con mejor se&ntilde;al, y si se corta busca otra de la lista.</p>");

  html += F("<div class='box'><h3>Ahora</h3>");
  if (online) {
    html += "<p>Conectada a <b>" + htmlEscape(current) + "</b> &middot; se&ntilde;al " + String(WiFi.RSSI()) + " dBm &middot; IP " + WiFi.localIP().toString() + "</p>";
  } else {
    html += F("<p class='err'>Sin conexi&oacute;n</p>");
  }
  html += F("</div>");

  html += "<div class='box'><h3>Redes guardadas (" + String(wifiService.count()) + "/" + String(WifiService::MAX_NETWORKS) + ")</h3>";
  if (wifiService.count() == 0) html += F("<p class='mut'>Ninguna todav&iacute;a.</p>");
  for (int i = 0; i < wifiService.count(); i++) {
    String ss = wifiService.ssidAt(i);
    bool isCur = online && ss == current;
    html += "<div class='row'><span class='name'>" + htmlEscape(ss) + (isCur ? "<span class='tag'>conectada</span>" : "") + "</span>";
    if (!isCur) html += "<button class='sec' onclick='conn(" + String(i) + ")'>Conectar</button>";
    html += "<button class='sec' onclick='forget(" + String(i) + ")'>Olvidar</button></div>";
  }
  html += F("<p class='mut'>Se guardan hasta 5. Si agreg&aacute;s una m&aacute;s, se borra la m&aacute;s vieja.</p></div>");

  html += F("<div class='box'><h3>Agregar una red</h3>");
  html += F("<p class='mut'>Pod&eacute;s cargar la red de otro lugar antes de llevarla: cuando la tenga cerca se conecta sola.</p>");
  html += F("<div id='scan'><button class='sec' onclick='scan()'>Buscar redes cercanas</button></div>");
  html += F("<input id='ssid' placeholder='Nombre de la red (SSID)' maxlength='32' autocomplete='off'>");
  html += F("<input id='pass' type='password' placeholder='Clave (vac&iacute;a si es abierta)' maxlength='64' autocomplete='new-password'>");
  html += F("<label class='mut'><input type='checkbox' id='now' style='width:auto;margin:0 6px 0 0'>Conectarse ahora</label>");
  html += F("<button class='big' onclick='add()'>Guardar red</button><p id='s'></p></div>");

  html += F("<div class='box'><h3>Otra forma</h3><p class='mut'>Toc&aacute; la <b>A</b> mientras est&aacute; la presentaci&oacute;n al prenderla: abre la red <b>" SETUP_AP_SSID "</b> (clave <code>" SETUP_AP_PASS "</code>) y desde el celular, en 192.168.4.1, eleg&iacute;s el WiFi. La red nueva se suma a la lista.</p></div>");

  html += F("<script>const $=i=>document.getElementById(i),s=$('s');");
  html += F("function post(u,b){return fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b}).then(r=>r.json());}");
  html += F("function forget(i){if(!confirm('\u00bfOlvidar esta red?'))return;post('/wifi/forget','i='+i).then(()=>location.reload());}");
  html += F("function conn(i){if(!confirm('La Mini C3 se va a pasar a esa red. Si tu celular o compu no est\u00e1n en la misma, esta p\u00e1gina deja de responder. \u00bfSeguir?'))return;");
  html += F("post('/wifi/connect','i='+i).then(()=>{document.body.innerHTML='<h2>Cambiando de red...</h2><p class=mut>Si no vuelve, conect\u00e1 tu celular a la misma red y abr\u00ed <a href=/>minic3.local</a>. Si no logra conectarse, vuelve sola a una red conocida.</p>';});}");
  html += F("function bars(r){return r>-55?'\u2582\u2584\u2586\u2588':r>-67?'\u2582\u2584\u2586':r>-78?'\u2582\u2584':'\u2582';}");
  html += F("function scan(){$('scan').innerHTML='<p class=mut>Buscando...</p>';const go=()=>fetch('/wifi/scan').then(r=>r.json()).then(j=>{if(j.scanning){setTimeout(go,900);return;}");
  html += F("if(!j.list.length){$('scan').innerHTML='<p class=mut>No encontr\u00e9 redes. <a href=# onclick=scan()>Otra vez</a></p>';return;}");
  html += F("$('scan').innerHTML='';j.list.forEach(n=>{const d=document.createElement('div');d.className='row net';");
  html += F("d.innerHTML='<span class=bars>'+bars(n.rssi)+'</span><span class=name></span>'+(n.saved?'<span class=tag>guardada</span>':'')+(n.open?'<span class=mut>abierta</span>':'');");
  html += F("d.querySelector('.name').textContent=n.ssid;d.onclick=()=>{$('ssid').value=n.ssid;$('pass').value='';$('pass').focus();};$('scan').appendChild(d);});}).catch(()=>setTimeout(go,1500));go();}");
  html += F("function add(){const n=$('ssid').value;if(!n.trim()){s.textContent='Escrib\u00ed el nombre de la red';s.className='err';return;}");
  html += F("const p=$('pass').value;if(p.length>0&&p.length<8){s.textContent='Las claves WiFi tienen al menos 8 letras';s.className='err';return;}");
  html += F("post('/wifi/add','ssid='+encodeURIComponent(n)+'&pass='+encodeURIComponent(p)+'&now='+($('now').checked?1:0)).then(j=>{if(!j.ok){s.textContent=j.error;s.className='err';return;}");
  html += F("if($('now').checked){document.body.innerHTML='<h2>Conectando a la red nueva...</h2><p class=mut>Si la clave est\u00e1 mal, en unos segundos vuelve sola a la red de antes.</p>';}else{s.textContent='\u2713 Guardada';s.className='ok';setTimeout(()=>location.reload(),900);}});}");
  html += F("</script><p><a href='/'>&larr; Volver al panel</a></p></body></html>");
  server.send(HTTP_OK, "text/html", html);
}

void WebServerService::handleWifiScan() {
  if (!wifiAuth()) return;
  String list;
  int r = wifiService.scanResults(list);
  if (r < 0) {
    server.send(HTTP_OK, "application/json", "{\"scanning\":true}");
    return;
  }
  server.send(HTTP_OK, "application/json", "{\"scanning\":false,\"list\":" + list + "}");
}

void WebServerService::handleWifiAdd() {
  if (!wifiAuth()) return;
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() == 0 || ssid.length() > 32) {
    server.send(HTTP_OK, "application/json", "{\"ok\":false,\"error\":\"El nombre tiene que tener entre 1 y 32 letras\"}");
    return;
  }
  if (pass.length() > 64 || (pass.length() > 0 && pass.length() < 8)) {
    server.send(HTTP_OK, "application/json", "{\"ok\":false,\"error\":\"La clave tiene que tener entre 8 y 64 letras\"}");
    return;
  }
  wifiService.remember(ssid, pass);
  server.send(HTTP_OK, "application/json", "{\"ok\":true}");
  if (server.arg("now") == "1") wifiService.requestConnect(0);   // remember() puts it first
}

void WebServerService::handleWifiForget() {
  if (!wifiAuth()) return;
  bool ok = wifiService.forget(server.arg("i").toInt());
  server.send(HTTP_OK, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void WebServerService::handleWifiConnect() {
  if (!wifiAuth()) return;
  int i = server.arg("i").toInt();
  bool ok = (i >= 0 && i < wifiService.count());
  server.send(HTTP_OK, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  if (ok) wifiService.requestConnect(i);
}
