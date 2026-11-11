#include "MessageInbox.h"

#include <Adafruit_SH110X.h>

#include "DisplayService.h"
#include "MessageService.h"
#include "MiniC3.h"

extern MessageService messageService;

static const uint32_t START_DELAY_MS = 1000;
static const uint32_t STEP_MS = 70;
static const uint32_t END_WAIT_MS = 2500;
static const char* const OPTIONS[3] = {"Volver", "Borrar", "Borrar todos"};

static uint8_t slideOld[1024];
static uint8_t slideNew[1024];

static String fit(const String& s, unsigned n) {
    if (s.length() <= n) return s;
    String cut = s.substring(0, n - 1);
    cut.trim();
    return cut + ".";
}

static String shortTime(const String& t) {
    return t.length() > 5 ? t.substring(t.length() - 5) : t;
}

// ---------------------------------------------------------------------------

void MessageInbox::openReader(int index) {
    startReading(index);
    keepModeOnEnter = true;
    lastTick = millis();
}

void MessageInbox::reset() {
    mode = M_LIST;
    sel = 0;
    firstRow = 0;
    confirmAll = false;
    slideDir = 0;
    dirty = true;
}

void MessageInbox::startReading(int index) {
    mode = M_READ;
    reading = index < 0 ? 0 : index;
    sel = reading;
    scrollY = 0;
    manual = false;
    phase = 0;
    phaseAt = millis();
    confirmAll = false;
    dirty = true;
    messageService.markRead(reading);
}

int MessageInbox::wrap(const Message& m, String* lines, int maxLines) const {
    String text = oledAscii(m.text);
    int n = 0;
    int pos = 0;
    int len = text.length();
    while (pos < len && n < maxLines - 2) {
        int end = min(pos + 21, len);
        if (end < len && text[end] != ' ') {
            int sp = text.lastIndexOf(' ', end);
            if (sp > pos) end = sp;
        }
        String line = text.substring(pos, end);
        line.trim();
        lines[n++] = line;
        pos = end;
        while (pos < len && text[pos] == ' ') pos++;
    }
    lines[n++] = "";
    lines[n++] = "   - A: opciones -";
    return n;
}

int MessageInbox::maxScroll(const MessageData& data) const {
    if (reading >= data.count) return 0;
    String lines[MAX_LINES];
    int n = wrap(data.items[reading], lines, MAX_LINES);
    int contentH = TEXT_TOP + n * LINE_H;
    return contentH > 64 ? contentH - 62 : 0;
}

bool MessageInbox::handleInput(InputEvent e, const MessageData& data) {
    uint32_t now = millis();
    lastTick = now;   // keys mean the screen is on display
    int n = data.count;
    dirty = true;

    switch (mode) {
        case M_LIST:
            if (n <= 0) return false;
            switch (e) {
                case INPUT_LEFT:  sel = (sel + n - 1) % n; return true;
                case INPUT_RIGHT: sel = (sel + 1) % n; return true;
                case INPUT_TOUCH_TAP:
                    startReading(sel);
                    slideDir = 1;
                    return true;
                default:
                    return false;   // ↑ / ↓ other screens, A held auto cycle
            }

        case M_READ:
            switch (e) {
                case INPUT_UP:
                    manual = true;
                    scrollY = max(0, scrollY - LINE_H);
                    break;
                case INPUT_DOWN:
                    manual = true;
                    scrollY = min(maxScroll(data), scrollY + LINE_H);
                    break;
                case INPUT_LEFT:
                    mode = M_LIST;
                    sel = reading;
                    slideDir = -1;
                    break;
                case INPUT_RIGHT:
                case INPUT_TOUCH_TAP:
                    mode = M_OPTIONS;
                    optSel = 0;
                    confirmAll = false;
                    break;
                default:
                    break;   // A held: pauses (read straight from the pad)
            }
            return true;

        case M_OPTIONS:
            switch (e) {
                case INPUT_UP:
                    optSel = (optSel + 2) % 3;
                    confirmAll = false;
                    break;
                case INPUT_DOWN:
                    optSel = (optSel + 1) % 3;
                    confirmAll = false;
                    break;
                case INPUT_LEFT:
                    mode = M_READ;
                    break;
                case INPUT_RIGHT:
                case INPUT_TOUCH_TAP:
                    if (optSel == 0) {
                        mode = M_READ;
                    } else if (optSel == 1) {
                        messageService.remove(reading);
                        mode = M_LIST;
                        sel = min(reading, max(0, data.count - 1));
                        slideDir = -1;
                        toast = "Mensaje borrado";
                        toastUntil = now + 1200;
                    } else if (!confirmAll) {
                        confirmAll = true;
                        confirmAt = now;
                    } else {
                        messageService.removeAll();
                        mode = M_LIST;
                        sel = 0;
                        slideDir = -1;
                        toast = "Todo borrado";
                        toastUntil = now + 1200;
                    }
                    break;
                default:
                    break;
            }
            return true;
    }
    return false;
}

void MessageInbox::tick(MiniOled& oled, const MessageData& data, bool touchHeld) {
    uint32_t now = millis();
    if (now - lastTick > 500 && !keepModeOnEnter) reset();   // came back to the screen
    keepModeOnEnter = false;
    lastTick = now;

    if (data.count <= 0 && mode != M_LIST) {
        mode = M_LIST;
        dirty = true;
    }
    if (sel >= data.count) sel = max(0, data.count - 1);
    if (confirmAll && now - confirmAt > 3000) {
        confirmAll = false;
        dirty = true;
    }
    if (toastUntil && (int32_t)(now - toastUntil) >= 0) {
        toastUntil = 0;
        dirty = true;
    }
    if (confirmAll && now - lastDraw > 150) dirty = true;   // blink

    if (mode == M_READ) {
        if (touchHeld != paused) {
            paused = touchHeld;
            dirty = true;
        }
        int ms = maxScroll(data);
        if (!manual && ms > 0) {
            if (paused) {
                phaseAt = now;
            } else if (phase == 0) {
                if (now - phaseAt >= START_DELAY_MS) {
                    phase = 1;
                    phaseAt = now;
                }
            } else if (phase == 1) {
                int steps = (now - phaseAt) / STEP_MS;
                if (steps > 0) {
                    phaseAt += min(steps, 4) * STEP_MS;
                    scrollY += min(steps, 4);
                    dirty = true;
                    if (scrollY >= ms) {
                        scrollY = ms;
                        phase = 2;
                        phaseAt = now;
                    }
                }
            } else if (now - phaseAt >= END_WAIT_MS) {
                scrollY = 0;
                phase = 0;
                phaseAt = now;
                dirty = true;
            }
        }
    }

    if (!dirty && slideDir == 0) return;
    dirty = false;

    if (slideDir != 0) {
        // the new view slides in (opening: from the right, back: from the left)
        memcpy(slideOld, oled.getBuffer(), 1024);
        draw(oled, data);
        memcpy(slideNew, oled.getBuffer(), 1024);
        bool fromRight = slideDir > 0;
        slideDir = 0;
        for (int offset = 32; offset <= 128; offset += 32) {
            uint8_t* buf = oled.getBuffer();
            for (int p = 0; p < 8; p++) {
                int st = p * 128;
                if (offset == 128) {
                    memcpy(&buf[st], &slideNew[st], 128);
                } else if (fromRight) {
                    memcpy(&buf[st], &slideOld[st + offset], 128 - offset);
                    memcpy(&buf[st + (128 - offset)], &slideNew[st], offset);
                } else {
                    memcpy(&buf[st + offset], &slideOld[st], 128 - offset);
                    memcpy(&buf[st], &slideNew[st + (128 - offset)], offset);
                }
            }
            oled.displayChanged();
            delay(8);
        }
        return;
    }

    draw(oled, data);
    oled.displayChanged();
}

// ---------------------------------------------------------------------------
//  Drawing
// ---------------------------------------------------------------------------

void MessageInbox::draw(Adafruit_GFX& d, const MessageData& data) {
    lastDraw = millis();
    d.fillScreen(SH110X_BLACK);
    d.setFont();
    d.setTextSize(1);
    d.setTextWrap(false);
    d.setTextColor(SH110X_WHITE);

    if (mode == M_LIST || data.count <= 0) drawList(d, data);
    else drawReader(d, data);
    if (mode == M_OPTIONS && data.count > 0) drawOptions(d);

    if (toastUntil) {
        int w = toast.length() * 6 + 10;
        int x = (128 - w) / 2;
        d.fillRect(x, 26, w, 14, SH110X_BLACK);
        d.drawRect(x, 26, w, 14, SH110X_WHITE);
        d.setCursor(x + 5, 30);
        d.print(toast);
    }
}

void MessageInbox::drawList(Adafruit_GFX& d, const MessageData& data) {
    int n = data.count;

    d.fillRect(0, 0, 128, 10, SH110X_WHITE);
    d.setTextColor(SH110X_BLACK);
    d.setCursor(2, 1);
    d.print("MENSAJES");
    if (n > 0) {
        String pos = String(sel + 1) + "/" + String(n);
        d.setCursor(126 - (int)pos.length() * 6 + 1, 1);
        d.print(pos);
    }
    d.setTextColor(SH110X_WHITE);

    if (n <= 0) {
        // little envelope
        d.drawRect(4, 18, 16, 11, SH110X_WHITE);
        d.drawLine(4, 18, 12, 24, SH110X_WHITE);
        d.drawLine(19, 18, 12, 24, SH110X_WHITE);
        d.setCursor(26, 20);
        d.print("Sin mensajes");
        d.setCursor(0, 40);
        d.print("Manda uno desde:");
        d.setCursor(0, 52);
        d.print(String(DEVICE_HOSTNAME) + ".local/mensaje");
        return;
    }

    if (sel < firstRow) firstRow = sel;
    if (sel >= firstRow + VISIBLE_ROWS) firstRow = sel - VISIBLE_ROWS + 1;
    if (firstRow > max(0, n - VISIBLE_ROWS)) firstRow = max(0, n - VISIBLE_ROWS);

    for (int r = 0; r < VISIBLE_ROWS; r++) {
        int i = firstRow + r;
        if (i >= n) break;
        const Message& m = data.items[i];
        int y = 11 + r * ROW_H;
        bool s = (i == sel);
        uint16_t fg = s ? SH110X_BLACK : SH110X_WHITE;
        if (s) d.fillRect(0, y, 126, ROW_H - 1, SH110X_WHITE);
        if (!m.read) d.fillCircle(3, y + 4, 2, fg);
        d.setTextColor(fg);
        d.setCursor(9, y + 1);
        d.print(fit(oledAscii(m.from), 13));
        String t = shortTime(m.time);
        d.setCursor(125 - (int)t.length() * 6 + 1, y + 1);
        d.print(t);
        d.setCursor(2, y + 9);
        d.print(fit(oledAscii(m.text), 20));
        d.setTextColor(SH110X_WHITE);
    }

    if (n > VISIBLE_ROWS) {
        int track = 52;
        int h = max(6, track * VISIBLE_ROWS / n);
        int y = 11 + (track - h) * firstRow / (n - VISIBLE_ROWS);
        d.drawFastVLine(127, 11, track, SH110X_WHITE);
        d.fillRect(126, y, 2, h, SH110X_WHITE);
    }
}

void MessageInbox::drawReader(Adafruit_GFX& d, const MessageData& data) {
    if (reading >= data.count) reading = data.count - 1;
    const Message& m = data.items[reading];
    String lines[MAX_LINES];
    int n = wrap(m, lines, MAX_LINES);
    int ms = maxScroll(data);
    if (scrollY > ms) scrollY = ms;

    for (int i = 0; i < n; i++) {
        int y = TEXT_TOP + i * LINE_H - scrollY;
        if (y < 4 || y > 63) continue;
        d.setCursor(0, y);
        d.print(lines[i]);
    }

    d.fillRect(0, 0, 128, 11, SH110X_BLACK);
    d.fillRect(0, 0, 128, 10, SH110X_WHITE);
    d.setTextColor(SH110X_BLACK);
    d.setCursor(2, 1);
    d.print(fit(oledAscii(m.from), 14));
    String right = paused ? String("PAUSA") : shortTime(m.time);
    d.setCursor(126 - (int)right.length() * 6 + 1, 1);
    d.print(right);
    d.setTextColor(SH110X_WHITE);

    if (ms > 0) {
        int track = 52;
        int h = max(6, track * 52 / (52 + ms));
        int y = 11 + (track - h) * scrollY / ms;
        d.fillRect(126, y, 2, h, SH110X_WHITE);
    }
}

void MessageInbox::drawOptions(Adafruit_GFX& d) {
    d.fillRect(18, 13, 92, 46, SH110X_BLACK);
    d.drawRect(18, 13, 92, 46, SH110X_WHITE);
    d.fillRect(18, 13, 92, 10, SH110X_WHITE);
    d.setTextColor(SH110X_BLACK);
    d.setCursor(40, 14);
    d.print("OPCIONES");
    d.setTextColor(SH110X_WHITE);
    bool blink = (millis() / 300) % 2 == 0;
    for (int i = 0; i < 3; i++) {
        int y = 26 + i * 11;
        if (i == optSel) {
            d.fillRect(20, y - 2, 88, 11, SH110X_WHITE);
            d.setTextColor(SH110X_BLACK);
        }
        d.setCursor(24, y);
        if (i == 2 && confirmAll) d.print(blink ? "A de nuevo: SI" : "Borrar todos?");
        else d.print(OPTIONS[i]);
        d.setTextColor(SH110X_WHITE);
    }
}
