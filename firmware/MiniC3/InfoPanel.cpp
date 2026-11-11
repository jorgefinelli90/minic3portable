#include "InfoPanel.h"

#include <time.h>
#include <WiFi.h>

#include "DataSyncService.h"
#include "DisplayService.h"
#include "MiniC3.h"
#include "OtaService.h"
#include "WifiService.h"

extern DisplayService displayService;
extern HardwareService hardwareService;
extern OtaService otaService;
extern DataSyncService dataSyncService;
extern WifiService wifiService;

static const char* const PAGE_TITLES[InfoPanel::PAGE_COUNT] = {"Red", "Equipo", "Servicios", "Acciones"};
static const char* const ACTION_NAMES[] = {"Recargar datos", "Reiniciar", "Agregar WiFi", "Apagar", "Cerrar"};

// 5x5 status icons (bit 4 = left pixel)
static const uint8_t ICONS[5][5] = {
    {0b00001, 0b00010, 0b10100, 0b01000, 0b00000},   // ok: check
    {0b10001, 0b01010, 0b00100, 0b01010, 0b10001},   // error: cross
    {0b00000, 0b00000, 0b10101, 0b00000, 0b00000},   // waiting: ...
    {0b10100, 0b01010, 0b00101, 0b01010, 0b10100},   // skipped: >>
    {0b00000, 0b00000, 0b01110, 0b00000, 0b00000},   // off: -
};

static uint8_t slideOld[1024];
static uint8_t slideNew[1024];

static String ago(unsigned long t) {
    if (t == 0) return "";
    unsigned long s = (millis() - t) / 1000;
    if (s < 60) return "recien";
    if (s < 3600) return "hace " + String(s / 60) + "m";
    return "hace " + String(s / 3600) + "h";
}

static String agoShort(unsigned long t) {
    if (t == 0) return "";
    unsigned long s = (millis() - t) / 1000;
    if (s < 60) return "<1m";
    if (s < 3600) return String(s / 60) + "m";
    return String(s / 3600) + "h";
}

static String fit(const String& s, unsigned n) {
    if (s.length() <= n) return s;
    String cut = s.substring(0, n - 1);
    cut.trim();
    return cut + ".";
}

// ---------------------------------------------------------------------------

void InfoPanel::open(const AppState& state) {
    isOpenNow = true;
    page = PAGE_NET;
    sel = 0;
    scroll = 0;
    confirmSel = -1;
    slideDir = 0;
    lastInput = millis();
    draw(state);
}

InfoPanel::Action InfoPanel::handleInput(InputEvent e, const AppState& state) {
    lastInput = millis();

    switch (e) {
        case INPUT_RIGHT:
            page = (page + 1) % PAGE_COUNT;
            slideDir = 1;
            confirmSel = -1;
            break;

        case INPUT_LEFT:
            page = (page + PAGE_COUNT - 1) % PAGE_COUNT;
            slideDir = -1;
            confirmSel = -1;
            break;

        case INPUT_UP:
        case INPUT_DOWN: {
            int d = (e == INPUT_DOWN) ? 1 : -1;
            if (page == PAGE_ACTIONS) {
                sel = (sel + ACTION_COUNT + d) % ACTION_COUNT;
                confirmSel = -1;
            } else if (page == PAGE_SERVICES) {
                Row rows[MAX_ROWS];
                int n = buildServices(state, rows);
                scroll = constrain(scroll + d, 0, max(0, n - VISIBLE_ROWS));
            }
            break;
        }

        case INPUT_TOUCH_TAP:
            if (page != PAGE_ACTIONS) {
                close();
                return ACT_NONE;
            }
            switch (sel) {
                case 0:
                    close();
                    return ACT_RELOAD;
                case 1:
                case 2:
                    // restart / add WiFi: second A within 3 s to confirm
                    if (confirmSel == sel && millis() - confirmAt < CONFIRM_MS) {
                        close();
                        return sel == 1 ? ACT_RESTART : ACT_ADD_WIFI;
                    }
                    confirmSel = sel;
                    confirmAt = millis();
                    break;
                case 3:
                    close();
                    return ACT_SLEEP;
                default:
                    close();
                    return ACT_NONE;
            }
            break;

        case INPUT_TOUCH_HOLD:
            close();
            return ACT_NONE;

        default:
            break;
    }

    draw(state);
    return ACT_NONE;
}

bool InfoPanel::tick(const AppState& state) {
    if (!isOpenNow) return false;
    uint32_t now = millis();
    if (now - lastInput > IDLE_CLOSE_MS) {
        close();
        return false;
    }
    if (confirmSel >= 0 && now - confirmAt >= CONFIRM_MS) {
        confirmSel = -1;
        draw(state);
    } else if (now - lastDraw >= (confirmSel >= 0 ? 150u : 1000u)) {
        draw(state);
    }
    return true;
}

// ---------------------------------------------------------------------------
//  Drawing
// ---------------------------------------------------------------------------

void InfoPanel::draw(const AppState& state) {
    lastDraw = millis();
    auto& d = displayService.display;

    if (slideDir != 0) {
        // the new page slides in from the side of the arrow
        memcpy(slideOld, d.getBuffer(), 1024);
        drawPage(state);
        memcpy(slideNew, d.getBuffer(), 1024);
        bool fromRight = slideDir > 0;
        slideDir = 0;
        for (int offset = 16; offset <= 128; offset += 16) {
            uint8_t* buf = d.getBuffer();
            for (int p = 0; p < 8; p++) {
                int st = p * 128;
                if (p == 0 || offset == 128) {
                    memcpy(&buf[st], &slideNew[st], 128);   // header stays put
                    continue;
                }
                if (fromRight) {
                    memcpy(&buf[st], &slideOld[st + offset], 128 - offset);
                    memcpy(&buf[st + (128 - offset)], &slideNew[st], offset);
                } else {
                    memcpy(&buf[st + offset], &slideOld[st], 128 - offset);
                    memcpy(&buf[st], &slideNew[st + (128 - offset)], offset);
                }
            }
            d.displayChanged();
            delay(12);
        }
        return;
    }

    drawPage(state);
    d.displayChanged();
}

void InfoPanel::header(const char* title) {
    auto& d = displayService.display;
    d.fillRect(0, 0, 128, 10, SH110X_WHITE);
    d.setTextColor(SH110X_BLACK);
    d.setCursor(2, 1);
    d.print(title);
    // < 2/4 >
    String pg = String(page + 1) + "/" + String(PAGE_COUNT);
    int w = pg.length() * 6;
    int x = 120 - w;
    d.setCursor(x, 1);
    d.print(pg);
    d.fillTriangle(x - 7, 4, x - 4, 1, x - 4, 7, SH110X_BLACK);
    d.fillTriangle(125, 4, 122, 1, 122, 7, SH110X_BLACK);
    d.setTextColor(SH110X_WHITE);
}

void InfoPanel::rightText(const String& s, int y, int right) {
    auto& d = displayService.display;
    d.setCursor(right - (int)s.length() * 6 + 1, y);
    d.print(s);
}

void InfoPanel::statusIcon(int x, int y, uint8_t st) {
    auto& d = displayService.display;
    for (int r = 0; r < 5; r++) {
        for (int b = 0; b < 5; b++) {
            if (ICONS[st][r] & (0x10 >> b)) d.drawPixel(x + b, y + r, SH110X_WHITE);
        }
    }
}

void InfoPanel::signalBars(int x, int y, int rssi) {
    auto& d = displayService.display;
    int level = rssi > -55 ? 4 : rssi > -67 ? 3 : rssi > -75 ? 2 : rssi > -85 ? 1 : 0;
    for (int i = 0; i < 4; i++) {
        int h = 2 + i * 2;
        int bx = x + i * 3;
        if (i < level) d.fillRect(bx, y + 8 - h, 2, h, SH110X_WHITE);
        else d.drawPixel(bx, y + 7, SH110X_WHITE);
    }
}

int InfoPanel::buildServices(const AppState& state, Row* rows) {
    const Config& c = state.config;
    int n = 0;
    auto add = [&](const char* name, uint8_t st, const String& detail) {
        if (n < MAX_ROWS) rows[n++] = {name, st, detail};
    };
    auto skipped = [](int step) { return DataSyncService::isStepSkipped(step); };
    // data that comes with a "last update" time: ok if there is data, error if
    // it was tried and there is none, waiting if it was never tried
    auto timed = [&](const char* name, int step, ScreenType scr, bool hasData) {
        unsigned long t = dataSyncService.lastFetch(scr);
        if (skipped(step)) add(name, ST_SKIPPED, "salteado");
        else if (hasData) add(name, ST_OK, ago(t));
        else if (t == 0) add(name, ST_WAIT, "esperando");
        else add(name, ST_ERROR, "sin datos");
    };

    bool online = (WiFi.status() == WL_CONNECTED);
    add("WiFi", online ? ST_OK : ST_ERROR, online ? fit(oledAscii(WiFi.SSID()), 10) : String("sin red"));

    bool timeOk = time(nullptr) > 1700000000;
    if (skipped(STEP_TIME)) add("Hora", ST_SKIPPED, "salteado");
    else add("Hora", timeOk ? ST_OK : ST_ERROR, timeOk ? "ok" : "sin hora");

    if (c.show_calendar && c.calendar_show_holidays) {
        if (skipped(STEP_CALENDAR)) add("Feriados", ST_SKIPPED, "salteado");
        else if (state.calendar.count > 0) add("Feriados", ST_OK, String(state.calendar.count) + " dias");
        else add("Feriados", state.calendar.last_fetch_year < 0 ? ST_WAIT : ST_ERROR, state.calendar.last_fetch_year < 0 ? "esperando" : "sin datos");
    }
    if (c.show_weather) timed("Clima", STEP_WEATHER, SCREEN_WEATHER, !isnan(state.weather.temp));
    if (c.show_daylight) {
        if (skipped(STEP_DAYLIGHT)) add("Sol", ST_SKIPPED, "salteado");
        else add("Sol", state.daylight.sunrise_mins >= 0 ? ST_OK : ST_WAIT, state.daylight.sunrise_mins >= 0 ? "ok" : "esperando");
    }
    if (c.show_moon) {
        if (skipped(STEP_MOON)) add("Luna", ST_SKIPPED, "salteado");
        else add("Luna", state.moon.fracillum >= 0 ? ST_OK : ST_WAIT, state.moon.fracillum >= 0 ? "ok" : "esperando");
    }
    if (c.show_iss) timed("EEI", STEP_ISS, SCREEN_ISS, !isnan(state.iss.latitude));

    auto list = [&](const char* name, int step, ScreenType scr, int ok, int total) {
        unsigned long t = dataSyncService.lastFetch(scr);
        if (skipped(step)) add(name, ST_SKIPPED, "salteado");
        else if (total > 0 && ok == total) add(name, ST_OK, String(ok) + "/" + String(total) + " " + agoShort(t));
        else if (t == 0) add(name, ST_WAIT, "esperando");
        else add(name, ST_ERROR, String(ok) + "/" + String(total) + " ok");
    };
    if (c.show_stock) {
        int ok = 0;
        for (int i = 0; i < c.stock_count; i++) ok += state.stocks[i].updated ? 1 : 0;
        list("Acciones", STEP_STOCK, SCREEN_STOCK, ok, c.stock_count);
    }
    if (c.show_crypto) {
        int ok = 0;
        for (int i = 0; i < c.crypto_count; i++) ok += state.cryptos[i].updated ? 1 : 0;
        list("Cripto", STEP_CRYPTO, SCREEN_CRYPTO, ok, c.crypto_count);
    }
    if (c.show_currency) {
        int ok = 0;
        for (int i = 0; i < c.currency_count; i++) ok += state.currencies[i].updated ? 1 : 0;
        list("Monedas", STEP_CURRENCY, SCREEN_CURRENCY, ok, c.currency_count);
    }

    if (c.show_news) {
        const NewsData& nw = state.news;
        if (skipped(STEP_NEWS)) add("Noticias", ST_SKIPPED, "salteado");
        else if (nw.fetch_status == 1 && nw.count > 0) add("Noticias", ST_OK, String(nw.count) + " notas");
        else if (nw.fetch_status == 2 || (nw.fetch_status == 1 && nw.count == 0)) add("Noticias", ST_ERROR, "sin datos");
        else add("Noticias", ST_WAIT, "esperando");
    }

    if (c.show_spotify) {
        const SpotifyState& sp = state.spotify;
        switch (sp.status) {
            case SP_ACTIVE: add("Spotify", ST_OK, sp.playing ? "sonando" : "en pausa"); break;
            case SP_IDLE: add("Spotify", ST_OK, "conectado"); break;
            case SP_CONNECTING: add("Spotify", ST_WAIT, "conectando"); break;
            case SP_NOT_CONFIGURED: add("Spotify", ST_OFF, "sin cuenta"); break;
            default: add("Spotify", ST_ERROR, fit(oledAscii(sp.error.length() ? sp.error : String("error")), 10)); break;
        }
    }
    return n;
}

void InfoPanel::drawPage(const AppState& state) {
    auto& d = displayService.display;
    d.clearDisplay();
    d.setFont();
    d.setTextSize(1);
    d.setTextWrap(false);
    d.setTextColor(SH110X_WHITE);
    header(PAGE_TITLES[page]);

    const int Y[VISIBLE_ROWS] = {13, 23, 33, 43, 53};
    bool online = (WiFi.status() == WL_CONNECTED);

    switch (page) {
        case PAGE_NET: {
            d.setCursor(0, Y[0]);
            if (online) {
                d.print("Red " + fit(oledAscii(WiFi.SSID()), 17));
                int rssi = WiFi.RSSI();
                d.setCursor(0, Y[1]);
                d.print("Senal");
                signalBars(36, Y[1] - 1, rssi);
                rightText(String(rssi) + " dBm", Y[1]);
                d.setCursor(0, Y[2]);
                d.print("IP " + WiFi.localIP().toString());
            } else {
                d.print("Sin WiFi");
                d.setCursor(0, Y[1]);
                d.print(wifiService.count() > 0 ? "Buscando tus redes" : "Sin redes guardadas");
                d.setCursor(0, Y[2]);
                d.print(String(wifiService.count()) + " redes guardadas");
            }
            d.setCursor(0, Y[3]);
            d.print(String(DEVICE_HOSTNAME) + ".local");
            d.setCursor(0, Y[4]);
            d.print(otaService.isReady() ? "OTA: lista" : "OTA: sin WiFi");
            break;
        }

        case PAGE_DEVICE: {
            d.setCursor(0, Y[0]);
            d.print("Firmware v" FW_VERSION);

            int mv = hardwareService.readBatteryMv();
            d.setCursor(0, Y[1]);
            if (mv > 0) {
                int pct = HardwareService::batteryPercent(mv);
                d.print("Bat " + String(mv / 1000.0f, 2) + "V " + String(pct) + "%");
                d.drawRect(104, Y[1], 20, 8, SH110X_WHITE);
                d.fillRect(124, Y[1] + 2, 2, 4, SH110X_WHITE);
                int fill = (pct * 16) / 100;
                if (fill > 0) d.fillRect(106, Y[1] + 2, fill, 4, SH110X_WHITE);
            } else {
                d.print("Bateria: sin medir");
            }

            unsigned long up = millis() / 60000;   // minutes
            String upStr = (up >= 1440) ? String(up / 1440) + "d " + String((up % 1440) / 60) + "h"
                         : (up >= 60) ? String(up / 60) + "h " + String(up % 60) + "m"
                         : String(up) + " min";
            d.setCursor(0, Y[2]);
            d.print("Prendida " + upStr);

            d.setCursor(0, Y[3]);
            d.print("Memoria " + String(ESP.getFreeHeap() / 1024) + " KB libres");

            d.setCursor(0, Y[4]);
            d.print(String("Auto-ciclo: ") + (state.config.screen_auto_cycle ? "SI" : "NO"));
            break;
        }

        case PAGE_SERVICES: {
            Row rows[MAX_ROWS];
            int n = buildServices(state, rows);
            scroll = constrain(scroll, 0, max(0, n - VISIBLE_ROWS));
            for (int i = 0; i < VISIBLE_ROWS && scroll + i < n; i++) {
                const Row& r = rows[scroll + i];
                statusIcon(1, Y[i] + 1, r.status);
                d.setCursor(9, Y[i]);
                d.print(r.name);
                rightText(r.detail, Y[i], 123);
            }
            // scroll bar
            if (n > VISIBLE_ROWS) {
                int track = 52;
                int h = max(6, track * VISIBLE_ROWS / n);
                int y = 12 + (track - h) * scroll / (n - VISIBLE_ROWS);
                d.drawFastVLine(127, 12, track, SH110X_WHITE);
                d.fillRect(126, y, 2, h, SH110X_WHITE);
            }
            break;
        }

        case PAGE_ACTIONS: {
            bool blink = (millis() / 300) % 2 == 0;
            for (int i = 0; i < ACTION_COUNT; i++) {
                int y = Y[i];
                bool selected = (i == sel);
                bool confirming = (i == confirmSel);
                if (selected) {
                    d.fillRect(0, y - 2, 128, 11, SH110X_WHITE);
                    d.setTextColor(SH110X_BLACK);
                }
                d.setCursor(4, y);
                d.print(selected ? "> " : "  ");
                if (confirming) d.print(blink ? "A de nuevo: SI" : (i == 1 ? "Reiniciar?" : "Agregar WiFi?"));
                else d.print(ACTION_NAMES[i]);
                d.setTextColor(SH110X_WHITE);
            }
            break;
        }
    }
}
