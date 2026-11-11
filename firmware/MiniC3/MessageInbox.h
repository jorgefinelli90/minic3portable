#ifndef MESSAGE_INBOX_H
#define MESSAGE_INBOX_H

#include <Adafruit_GFX.h>
#include <Arduino.h>

#include "HardwareService.h"
#include "structs.h"

class MiniOled;

// Mini C3 v1.9.0: the messages screen as an inbox.
//
//   list     ← / →  choose (← up, → down) · A  open · ↑ / ↓ other screens
//   reading  the text scrolls by itself (hold A to pause) · ↑ / ↓ scroll by hand
//            ← back to the list · A options
//   options  Borrar · Borrar todos (asks again) · Volver   (↑ / ↓ choose, A ok, ← cancel)
class MessageInbox {
public:
    // Open a message straight away (a new one just arrived)
    void openReader(int index);
    // Back to the list (when the screen is entered from another one)
    void reset();

    // Keys on the messages screen. Returns true when it used the key.
    bool handleInput(InputEvent e, const MessageData& data);

    void draw(Adafruit_GFX& d, const MessageData& data);
    // Every loop on the messages screen: scrolling, animations
    void tick(MiniOled& oled, const MessageData& data, bool touchHeld);

    // Reading a message (the auto cycle must not leave)
    bool isBusy() const { return mode != M_LIST; }

private:
    enum Mode : uint8_t { M_LIST, M_READ, M_OPTIONS };
    static const int ROW_H = 17;
    static const int VISIBLE_ROWS = 3;
    static const int MAX_LINES = 36;
    static const int LINE_H = 10;
    static const int TEXT_TOP = 13;

    Mode mode = M_LIST;
    int sel = 0;              // selected message (0 = newest)
    int firstRow = 0;
    int reading = 0;          // message open
    int optSel = 0;
    bool confirmAll = false;
    uint32_t confirmAt = 0;

    int scrollY = 0;
    bool manual = false;      // scrolled with ↑ / ↓: no automatic scroll
    bool paused = false;
    uint8_t phase = 0;        // 0 waiting, 1 scrolling, 2 at the end
    uint32_t phaseAt = 0;

    String toast = "";
    uint32_t toastUntil = 0;
    int slideDir = 0;         // page change: +1 from the right, -1 from the left
    bool dirty = true;
    uint32_t lastTick = 0;
    uint32_t lastDraw = 0;
    bool keepModeOnEnter = false;

    int wrap(const Message& m, String* lines, int maxLines) const;
    int maxScroll(const MessageData& data) const;
    void startReading(int index);
    void drawList(Adafruit_GFX& d, const MessageData& data);
    void drawReader(Adafruit_GFX& d, const MessageData& data);
    void drawOptions(Adafruit_GFX& d);
};

#endif
