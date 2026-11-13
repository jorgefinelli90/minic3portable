#ifndef INFO_PANEL_H
#define INFO_PANEL_H

#include <Arduino.h>

#include "HardwareService.h"
#include "structs.h"

// Mini C3 v1.6.0: device panel (tap A on a screen where A has no other use).
//
//   ← / →   page:  Red · Equipo · Servicios · Ajustes · Acciones
//   ↑ / ↓   scroll the services / choose a setting or an action
//   A       on Ajustes changes the setting (v1.12.0: brillo, modo ahorro);
//           on Acciones runs the option; on the other pages closes
//   A held  closes · 20 s without touching anything closes
class InfoPanel {
public:
    // ACT_BRIGHTNESS / ACT_SAVER leave the panel open: the sketch changes the
    // setting, saves it and calls redraw()
    enum Action : uint8_t { ACT_NONE, ACT_RELOAD, ACT_RESTART, ACT_ADD_WIFI, ACT_SLEEP, ACT_BRIGHTNESS, ACT_SAVER };

    void open(const AppState& state);
    void close() { isOpenNow = false; }
    bool isOpen() const { return isOpenNow; }

    // A key while the panel is open. Returns what the main sketch has to do
    // (the panel is already closed when it returns an action).
    Action handleInput(InputEvent e, const AppState& state);

    // Call every loop while open: refreshes once a second (signal, battery,
    // uptime...) and closes itself after a while. Returns false once closed.
    bool tick(const AppState& state);
    void redraw(const AppState& state) { draw(state); }

    static const int PAGE_COUNT = 5;

private:
    enum { PAGE_NET, PAGE_DEVICE, PAGE_SERVICES, PAGE_SETTINGS, PAGE_ACTIONS };
    static const int ACTION_COUNT = 5;
    static const int SETTING_COUNT = 2;
    static const int VISIBLE_ROWS = 5;
    static const uint32_t IDLE_CLOSE_MS = 20000;
    static const uint32_t CONFIRM_MS = 3000;

    struct Row {
        String name;
        uint8_t status;   // ST_*
        String detail;
    };
    enum { ST_OK, ST_ERROR, ST_WAIT, ST_SKIPPED, ST_OFF };
    static const int MAX_ROWS = 14;

    bool isOpenNow = false;
    int page = PAGE_NET;
    int sel = 0;              // selected action
    int setSel = 0;           // selected setting
    int scroll = 0;           // first service row shown
    int confirmSel = -1;      // action waiting for a second A
    uint32_t confirmAt = 0;
    uint32_t lastInput = 0;
    uint32_t lastDraw = 0;
    int slideDir = 0;         // page change animation: +1 / -1

    void draw(const AppState& state);
    void drawPage(const AppState& state);
    void header(const char* title);
    int buildServices(const AppState& state, Row* rows);
    void statusIcon(int x, int y, uint8_t st);
    void signalBars(int x, int y, int rssi);
    void rightText(const String& s, int y, int right = 125);
};

#endif
