#include "HardwareService.h"

#include "MiniC3.h"

HardwareService::HardwareService(InputCallback callback) : onInput(callback) {}

void HardwareService::begin() {
    // Order matches InputEvent: UP, DOWN, LEFT, RIGHT, then the touch pad
    const uint8_t pins[KEY_COUNT] = {PIN_UP, PIN_DOWN, PIN_LEFT, PIN_RIGHT, PIN_TOUCH};

    for (int i = 0; i < KEY_COUNT; i++) {
        Key& k = keys[i];
        k.pin = pins[i];
        k.activeLow = (i != TOUCH_INDEX);
        k.repeats = false;   // Mini C3 v1.2.1: one press = one step (the slide took long enough to trigger a repeat)
        pinMode(k.pin, k.activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
        k.raw = false;
        k.stable = false;
        k.rawSince = millis();
        k.pressedAt = 0;
        k.lastRepeat = 0;
        k.holdFired = false;
        k.cancelled = false;
        k.longFired = false;
        k.deferred = false;
    }

    // If a key reads "pressed" at boot (e.g. held while powering on) wait for
    // its release instead of firing an event.
    for (int i = 0; i < KEY_COUNT; i++) {
        if (readPressed(keys[i])) {
            keys[i].raw = keys[i].stable = true;
            keys[i].cancelled = true;
        }
    }

    analogReadResolution(12);
    analogSetPinAttenuation(PIN_BATTERY, ADC_11db);

    Serial.printf("Hardware: UP=%d DOWN=%d LEFT=%d RIGHT=%d TOUCH=%d BAT=%d\n",
                  PIN_UP, PIN_DOWN, PIN_LEFT, PIN_RIGHT, PIN_TOUCH, PIN_BATTERY);
}

bool HardwareService::readPressed(const Key& k) const {
    int level = digitalRead(k.pin);
    return k.activeLow ? (level == LOW) : (level == HIGH);
}

bool HardwareService::anyArrowDown() const {
    for (int i = 0; i < TOUCH_INDEX; i++) {
        if (keys[i].stable) return true;
    }
    return false;
}

void HardwareService::tick() {
    uint32_t now = millis();

    for (int i = 0; i < KEY_COUNT; i++) {
        Key& k = keys[i];
        bool pressed = readPressed(k);

        if (pressed != k.raw) {
            k.raw = pressed;
            k.rawSince = now;
        }

        // --- debounced edges ---
        if (k.raw != k.stable && (now - k.rawSince) >= DEBOUNCE_MS) {
            k.stable = k.raw;

            if (k.stable) {
                k.pressedAt = now;
                k.lastRepeat = now;
                k.holdFired = false;
                k.longFired = false;

                if (i == TOUCH_INDEX) {
                    bool guarded = anyArrowDown() || (now - lastArrowActivity) < TOUCH_GUARD_MS;
                    k.cancelled = guarded;
                } else {
                    k.cancelled = false;
                    lastArrowActivity = now;
                    // An arrow pressed while the pad is being touched cancels the touch
                    if (keys[TOUCH_INDEX].stable) keys[TOUCH_INDEX].cancelled = true;
                    k.deferred = arrowHoldMode && (i == INPUT_LEFT || i == INPUT_RIGHT);
                    if (!k.deferred && onInput) onInput((InputEvent)i, false);
                }
            } else {
                if (i == TOUCH_INDEX) {
                    if (!k.cancelled && !k.holdFired && onInput) onInput(INPUT_TOUCH_TAP, false);
                } else {
                    lastArrowActivity = now;
                    // hold mode: a short press acts when it is released
                    if (k.deferred && !k.holdFired && !k.cancelled && onInput) onInput((InputEvent)i, false);
                    k.deferred = false;
                }
                k.cancelled = false;
            }
            continue;
        }

        if (!k.stable || k.cancelled) continue;

        // --- held keys ---
        if (i == TOUCH_INDEX) {
            if (!k.holdFired && (now - k.pressedAt) >= TOUCH_HOLD_MS) {
                k.holdFired = true;
                if (onInput) onInput(INPUT_TOUCH_HOLD, false);
            }
        } else {
            lastArrowActivity = now;
            if (k.deferred && !k.holdFired && (now - k.pressedAt) >= ARROW_HOLD_MS) {
                k.holdFired = true;
                if (onInput) onInput(i == INPUT_LEFT ? INPUT_LEFT_LONG : INPUT_RIGHT_LONG, false);
            }
            if (i == INPUT_UP && !k.longFired && (now - k.pressedAt) >= UP_SLEEP_MS) {
                k.longFired = true;
                if (onInput) onInput(INPUT_UP_LONG, false);
            }
            if (k.repeats && (now - k.pressedAt) >= REPEAT_DELAY_MS && (now - k.lastRepeat) >= REPEAT_EVERY_MS) {
                k.lastRepeat = now;
                if (onInput) onInput((InputEvent)i, true);
            }
        }
    }
}

bool HardwareService::touchDown() const {
    return keys[TOUCH_INDEX].stable;
}

uint32_t HardwareService::upHeldMs() const {
    const Key& k = keys[INPUT_UP];
    if (!k.stable || k.cancelled || k.longFired) return 0;
    return millis() - k.pressedAt;
}

void HardwareService::resync() {
    uint32_t now = millis();
    for (int i = 0; i < KEY_COUNT; i++) {
        Key& k = keys[i];
        bool pressed = readPressed(k);
        k.raw = k.stable = pressed;
        k.rawSince = now;
        k.cancelled = pressed;      // ignored until released
        k.deferred = false;
        k.holdFired = false;
        k.longFired = false;
    }
    lastArrowActivity = now;
}

bool HardwareService::anyKeyDown() {
    for (int i = 0; i < KEY_COUNT; i++) {
        if (readPressed(keys[i])) return true;
    }
    return false;
}

int HardwareService::readBatteryMv() {
    uint32_t sum = 0;
    const int samples = 8;
    for (int i = 0; i < samples; i++) sum += analogReadMilliVolts(PIN_BATTERY);
    int mv = (int)(sum / samples) * 2;   // 100k/100k divider
    if (mv < 2500) return 0;             // nothing wired (pin floating / at GND)
    return mv;
}

int HardwareService::batteryPercent(int mv) {
    if (mv <= 0) return -1;
    // Typical 1-cell LiPo discharge curve (resting voltage)
    static const int table[][2] = {
        {4200, 100}, {4100, 90}, {4000, 80}, {3920, 70}, {3850, 60}, {3800, 50},
        {3750, 40}, {3700, 30}, {3650, 20}, {3550, 10}, {3400, 5}, {3300, 0}
    };
    const int n = sizeof(table) / sizeof(table[0]);
    if (mv >= table[0][0]) return 100;
    if (mv <= table[n - 1][0]) return 0;
    for (int i = 1; i < n; i++) {
        if (mv >= table[i][0]) {
            int v0 = table[i][0], p0 = table[i][1];
            int v1 = table[i - 1][0], p1 = table[i - 1][1];
            return p0 + (mv - v0) * (p1 - p0) / (v1 - v0);
        }
    }
    return 0;
}
