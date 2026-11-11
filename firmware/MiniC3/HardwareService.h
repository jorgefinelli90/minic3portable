#ifndef HARDWARE_SERVICE_H
#define HARDWARE_SERVICE_H

#include <Arduino.h>

// Mini C3 inputs: 4 arrow switches (to GND) + TTP223 touch ("A", active HIGH)
enum InputEvent {
    INPUT_UP,
    INPUT_DOWN,
    INPUT_LEFT,
    INPUT_RIGHT,
    INPUT_TOUCH_TAP,   // touch released before TOUCH_HOLD_MS
    INPUT_TOUCH_HOLD,  // touch held TOUCH_HOLD_MS (fires once)
    INPUT_UP_LONG,     // ↑ held UP_SLEEP_MS (fires once) -> sleep
    INPUT_LEFT_LONG,   // ← / → held ARROW_HOLD_MS, only in "arrow hold mode"
    INPUT_RIGHT_LONG
};

// repeat = true when the event comes from holding ← / →
typedef void (*InputCallback)(InputEvent event, bool repeat);

class HardwareService {
public:
    explicit HardwareService(InputCallback callback);

    void begin();
    void tick();

    // Battery voltage in mV (divider 100k/100k on PIN_BATTERY). 0 = not connected.
    int readBatteryMv();
    static int batteryPercent(int mv);

    // How long ↑ has been held right now (0 if not pressed). Used to draw the
    // "hold to turn off" bar.
    uint32_t upHeldMs() const;
    // Touch pad held right now (news: hold A to pause the scroll)
    bool touchDown() const;
    // An arrow held right now (games: the paddle moves while it is held)
    bool isDown(InputEvent key) const { return key < TOUCH_INDEX && keys[key].stable; }
    static const uint32_t UP_SLEEP_MS = 3000;

    // Arrow hold mode (truco screen): ← / → act when released (tap) or after
    // ARROW_HOLD_MS held (INPUT_LEFT_LONG / INPUT_RIGHT_LONG), instead of
    // acting as soon as they go down
    void setArrowHoldMode(bool on) { arrowHoldMode = on; }
    static const uint32_t ARROW_HOLD_MS = 600;

    // After waking up: keys that are still down must be released before they
    // count again (the key that woke the device does nothing else)
    void resync();
    bool anyKeyDown();

private:
    static const uint32_t DEBOUNCE_MS = 25;
    static const uint32_t REPEAT_DELAY_MS = 450;
    static const uint32_t REPEAT_EVERY_MS = 160;
    static const uint32_t TOUCH_HOLD_MS = 700;
    // Ignore the touch pad while an arrow is down and shortly after (the thumb
    // can brush it when pressing ↑ / →)
    static const uint32_t TOUCH_GUARD_MS = 200;

    struct Key {
        uint8_t pin;
        bool activeLow;
        bool repeats;
        bool raw;           // last raw reading (true = pressed)
        bool stable;        // debounced state (true = pressed)
        uint32_t rawSince;  // when raw last changed
        uint32_t pressedAt;
        uint32_t lastRepeat;
        bool holdFired;
        bool cancelled;
        bool longFired;
        bool deferred;      // pressed in arrow hold mode
    };

    static const int KEY_COUNT = 5;
    static const int TOUCH_INDEX = 4;

    Key keys[KEY_COUNT];
    InputCallback onInput;
    uint32_t lastArrowActivity = 0;
    bool arrowHoldMode = false;

    bool readPressed(const Key& k) const;
    bool anyArrowDown() const;
};

#endif
