#ifndef DATA_SYNC_SERVICE_H
#define DATA_SYNC_SERVICE_H

#include "structs.h"

// Mini C3: every network step has an id, so a step that hangs can be skipped
// (button) or caught by the watchdog in loop().
enum SyncStep {
    STEP_NONE = 0,
    STEP_LOCATION, STEP_TIME, STEP_CALENDAR, STEP_WEATHER, STEP_AQI,
    STEP_DAYLIGHT, STEP_MOON, STEP_ISS, STEP_FLIGHT, STEP_POPULATION,
    STEP_STOCK, STEP_CRYPTO, STEP_CURRENCY, STEP_NEWS,
    STEP_COUNT
};

class DataSyncService {
public:
    // --- step tracking / skipping (shared with the main loop) ---
    static volatile int currentStep;
    static volatile uint32_t stepStartedAt;
    static const uint32_t STEP_TIMEOUT_MS = 45000;   // watchdog: longer than this = stuck

    static void initSkipMask();                    // call once at boot
    static bool isStepSkipped(int step);
    static uint32_t skipMask();
    static void clearSkipMask();                   // e.g. when the config is saved
    static const char* stepName(int step);
    // Marks the step to be skipped and restarts the board (a blocked network
    // call cannot be interrupted any other way). The skip lasts until the
    // board is powered off or the config is saved.
    static void skipStepAndReboot(int step);
    void requestSkip(int step) { skipRequestedStep = step; }
    // Undo a skip and fetch that screen on the next background update
    static void unskipStep(int step);
    void forceFetch(ScreenType screen) { forcedMask |= (1UL << screen); lastFailedAttempt = 0; }

    // Full sync in its own task, so the caller can keep the web/OTA alive and
    // watch the skip button meanwhile
    void startFullSync(AppState& state);
    bool isFullSyncRunning() const { return fullSyncRunning; }

    bool isGlobalSyncDue(const Config& config, bool nightModeLatched) const;
    void markGlobalSynced();
    bool isDue(ScreenType screen, const Config& config, bool nightModeLatched) const;
    void markFetched(ScreenType screen);
    // millis() of the last update of that screen (0 = never). Info panel.
    unsigned long lastFetch(ScreenType screen) const;
    void runFullSync(AppState& state);
    void maybeStartBackgroundSync(AppState& state, bool nightModeLatched);

    // Mini C3 (sleep): is the background task using WiFi right now?
    bool isBusy() const { return isBackgroundSyncRunning; }
    // Retry right away instead of waiting the 30 s back-off (after waking up)
    void clearRetryBackoff() { lastFailedAttempt = 0; }

private:
    static const int NIGHT_INTERVAL_MULTIPLIER = 10;
    static const unsigned long RETRY_BACKOFF_MS = 30000;
    static const int NEWS_INTERVAL_MIN = 20;

    FetchTrackers trackers;

    volatile bool isBackgroundSyncRunning = false;
    TaskHandle_t backgroundSyncTaskHandle = NULL;
    AppState* activeState = nullptr;
    bool activeNightModeLatched = false;
    unsigned long lastFailedAttempt = 0;

    unsigned long getGlobalIntervalMs(const Config& config, bool nightModeLatched) const;
    bool isFetchDue(unsigned long lastFetch, int customMin, const Config& config, bool nightModeLatched) const;

    volatile bool fullSyncRunning = false;
    volatile uint32_t forcedMask = 0;   // screens to fetch right away (forceFetch)
    volatile int skipRequestedStep = STEP_NONE;
    AppState* fullSyncState = nullptr;

    bool enterStep(int step);
    void leaveStep();
    void showSyncStatus(std::initializer_list<String> lines);
    static void fullSyncTaskTrampoline(void* parameter);

    static void backgroundSyncTaskTrampoline(void* parameter);
    void runBackgroundSyncBody();
};

#endif
