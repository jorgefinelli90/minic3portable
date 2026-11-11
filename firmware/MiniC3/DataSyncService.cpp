#include "DataSyncService.h"

String oledAscii(const String& input);   // DisplayService.cpp

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <time.h>
#include <WiFi.h>

#include "MiniC3.h"

#include "AirQualityService.h"
#include "CalendarService.h"
#include "ConfigManager.h"
#include "CryptoService.h"
#include "CurrencyService.h"
#include "DaylightService.h"
#include "DisplayService.h"
#include "IssService.h"
#include "MoonService.h"
#include "NewsService.h"
#include "PopulationService.h"
#include "StockService.h"
#include "TimeService.h"
#include "WeatherService.h"


extern ConfigManager configManager;
extern DisplayService displayService;
extern TimeService timeService;
extern CalendarService calendarService;
extern WeatherService weatherService;
extern AirQualityService airQualityService;
extern DaylightService daylightService;
extern MoonService moonService;
extern IssService issService;
extern PopulationService populationService;
extern StockService stockService;
extern CryptoService cryptoService;
extern CurrencyService currencyService;
extern NewsService newsService;


// ---------------------------------------------------------------------------
//  Mini C3: step tracking, skip and watchdog
// ---------------------------------------------------------------------------

volatile int DataSyncService::currentStep = STEP_NONE;
volatile uint32_t DataSyncService::stepStartedAt = 0;

// Survives ESP.restart() but not a power cycle
RTC_NOINIT_ATTR static uint32_t rtcSkipMask;
RTC_NOINIT_ATTR static uint32_t rtcSkipMagic;
static const uint32_t SKIP_MAGIC = 0x5C1F0C3A;

void DataSyncService::initSkipMask() {
    esp_reset_reason_t why = esp_reset_reason();
    if (rtcSkipMagic != SKIP_MAGIC || why == ESP_RST_POWERON || why == ESP_RST_BROWNOUT) {
        rtcSkipMagic = SKIP_MAGIC;
        rtcSkipMask = 0;
    }
    if (rtcSkipMask) {
        Serial.printf("DataSync: pasos salteados en este encendido: 0x%04lX\n", (unsigned long)rtcSkipMask);
    }
}

bool DataSyncService::isStepSkipped(int step) {
    return step > STEP_NONE && step < STEP_COUNT && (rtcSkipMask & (1UL << step));
}

uint32_t DataSyncService::skipMask() { return rtcSkipMask; }

void DataSyncService::clearSkipMask() { rtcSkipMask = 0; }

void DataSyncService::unskipStep(int step) {
    if (step > STEP_NONE && step < STEP_COUNT) rtcSkipMask &= ~(1UL << step);
}

const char* DataSyncService::stepName(int step) {
    switch (step) {
        case STEP_LOCATION:   return "ubicacion";
        case STEP_TIME:       return "hora";
        case STEP_CALENDAR:   return "feriados";
        case STEP_WEATHER:    return "clima";
        case STEP_AQI:        return "calidad del aire";
        case STEP_DAYLIGHT:   return "sol";
        case STEP_MOON:       return "luna";
        case STEP_ISS:        return "estacion espacial";
        case STEP_FLIGHT:     return "vuelos";
        case STEP_POPULATION: return "poblacion";
        case STEP_STOCK:      return "acciones";
        case STEP_CRYPTO:     return "cripto";
        case STEP_CURRENCY:   return "monedas";
        case STEP_NEWS:       return "noticias";
        default:              return "?";
    }
}

void DataSyncService::skipStepAndReboot(int step) {
    if (step > STEP_NONE && step < STEP_COUNT) {
        rtcSkipMagic = SKIP_MAGIC;
        rtcSkipMask |= (1UL << step);
    }
    Serial.printf("DataSync: el paso '%s' quedo trabado -> lo salteo y reinicio\n", stepName(step));
    {
        StateLock lock(pdMS_TO_TICKS(500));
        displayService.showOLEDStatus({"\n", "\n", "Salteando:", String(stepName(step)), "\n", "Reiniciando..."}, true);
    }
    Serial.flush();
    delay(1200);
    ESP.restart();
}

bool DataSyncService::enterStep(int step) {
    if (isStepSkipped(step) || skipRequestedStep == step) {
        Serial.printf("DataSync: '%s' salteado\n", stepName(step));
        return false;
    }
    stepStartedAt = millis();
    currentStep = step;
    return true;
}

void DataSyncService::leaveStep() {
    currentStep = STEP_NONE;
}

void DataSyncService::showSyncStatus(std::initializer_list<String> lines) {
    StateLock lock;
    displayService.showOLEDStatus(lines, true);
    // Skip hint at the bottom
    displayService.display.setCursor(22, 55);
    displayService.display.print("-> o A: saltar");
    displayService.display.display();
}

void DataSyncService::startFullSync(AppState& state) {
    if (fullSyncRunning) return;
    fullSyncRunning = true;
    skipRequestedStep = STEP_NONE;
    fullSyncState = &state;
    if (xTaskCreate(fullSyncTaskTrampoline, "FullSync", 12288, this, 1, NULL) != pdPASS) {
        Serial.println("DataSync: no pude crear la tarea, sincronizo directo");
        runFullSync(state);
        fullSyncRunning = false;
    }
}

void DataSyncService::fullSyncTaskTrampoline(void* parameter) {
    DataSyncService* self = static_cast<DataSyncService*>(parameter);
    self->runFullSync(*self->fullSyncState);
    self->currentStep = STEP_NONE;
    self->skipRequestedStep = STEP_NONE;
    self->fullSyncRunning = false;
    vTaskDelete(NULL);
}

unsigned long DataSyncService::getGlobalIntervalMs(const Config& config, bool nightModeLatched) const {
    unsigned long multiplier = nightModeLatched ? NIGHT_INTERVAL_MULTIPLIER : 1;
    return config.refresh_interval_min * 60000UL * multiplier;
}

bool DataSyncService::isFetchDue(unsigned long lastFetch, int customMin, const Config& config, bool nightModeLatched) const {
    if (customMin <= 0) {
        return isGlobalSyncDue(config, nightModeLatched);
    }

    unsigned long multiplier = nightModeLatched ? NIGHT_INTERVAL_MULTIPLIER : 1;
    unsigned long intervalMs = (unsigned long)customMin * 60000UL * multiplier;
    return (millis() - lastFetch) > intervalMs;
}

bool DataSyncService::isGlobalSyncDue(const Config& config, bool nightModeLatched) const {
    return (millis() - trackers.lastDataUpdate) > getGlobalIntervalMs(config, nightModeLatched);
}

void DataSyncService::markGlobalSynced() {
    trackers.lastDataUpdate = millis();
}

bool DataSyncService::isDue(ScreenType screen, const Config& config, bool nightModeLatched) const {
    // A step skipped because it hung is not "due" (otherwise the background
    // task would be relaunched over and over)
    int step = STEP_NONE;
    switch (screen) {
        case SCREEN_WEATHER: step = STEP_WEATHER; break;
        case SCREEN_AIR_QUALITY: step = STEP_AQI; break;
        case SCREEN_ISS: step = STEP_ISS; break;
        case SCREEN_FLIGHT: step = STEP_FLIGHT; break;
        case SCREEN_STOCK: step = STEP_STOCK; break;
        case SCREEN_CRYPTO: step = STEP_CRYPTO; break;
        case SCREEN_CURRENCY: step = STEP_CURRENCY; break;
        case SCREEN_NEWS: step = STEP_NEWS; break;
        default: break;
    }
    if (isStepSkipped(step)) return false;
    if (forcedMask & (1UL << screen)) return true;

    switch (screen) {
        case SCREEN_WEATHER:
            return config.show_weather && isFetchDue(trackers.lastWeatherFetch, config.custom_weather_int_min, config, nightModeLatched);
        case SCREEN_AIR_QUALITY:
            return config.show_aqi && isFetchDue(trackers.lastAqiFetch, config.custom_aqi_int_min, config, nightModeLatched);
        case SCREEN_ISS:
            return config.show_iss && isFetchDue(trackers.lastIssFetch, config.custom_iss_int_min, config, nightModeLatched);
        case SCREEN_FLIGHT:
            return false;   // Mini C3 v1.11.0: flights removed
        case SCREEN_STOCK:
            return config.show_stock && isFetchDue(trackers.lastStockFetch, config.custom_stock_int_min, config, nightModeLatched);
        case SCREEN_CRYPTO:
            return config.show_crypto && isFetchDue(trackers.lastCryptoFetch, config.custom_crypto_int_min, config, nightModeLatched);
        case SCREEN_CURRENCY:
            return config.show_currency && isFetchDue(trackers.lastCurrencyFetch, config.custom_currency_int_min, config, nightModeLatched);
        case SCREEN_NEWS:
            return config.show_news && isFetchDue(trackers.lastNewsFetch, NEWS_INTERVAL_MIN, config, nightModeLatched);
        default:
            return false;
    }
}

unsigned long DataSyncService::lastFetch(ScreenType screen) const {
    switch (screen) {
        case SCREEN_WEATHER: return trackers.lastWeatherFetch;
        case SCREEN_ISS: return trackers.lastIssFetch;
        case SCREEN_FLIGHT: return trackers.lastFlightFetch;
        case SCREEN_STOCK: return trackers.lastStockFetch;
        case SCREEN_CRYPTO: return trackers.lastCryptoFetch;
        case SCREEN_CURRENCY: return trackers.lastCurrencyFetch;
        case SCREEN_NEWS: return trackers.lastNewsFetch;
        default: return trackers.lastDataUpdate;
    }
}

void DataSyncService::markFetched(ScreenType screen) {
    unsigned long now = millis();
    forcedMask &= ~(1UL << screen);
    switch (screen) {
        case SCREEN_WEATHER: trackers.lastWeatherFetch = now; break;
        case SCREEN_AIR_QUALITY: trackers.lastAqiFetch = now; break;
        case SCREEN_ISS: trackers.lastIssFetch = now; break;
        case SCREEN_FLIGHT: trackers.lastFlightFetch = now; break;
        case SCREEN_STOCK: trackers.lastStockFetch = now; break;
        case SCREEN_CRYPTO: trackers.lastCryptoFetch = now; break;
        case SCREEN_CURRENCY: trackers.lastCurrencyFetch = now; break;
        case SCREEN_NEWS: trackers.lastNewsFetch = now; break;
        default: break;
    }
}

void DataSyncService::runFullSync(AppState& state) {
    Config& config = state.config;

    // 1. Location Detection
    if (config.auto_detect && enterStep(STEP_LOCATION)) {
        showSyncStatus({"\n", "\n", "Buscando ubicacion...", "\n", "Un momento..."});
        if (timeService.fetchLocationData(config)) {
            Serial.println("Location updated via IP");
        }
        leaveStep();
    }

    // 2. Sync Time (Depends on Location/Timezone)
    if (enterStep(STEP_TIME)) {
        showSyncStatus({"\n", "\n", "Sincronizando hora...", "\n", "Zona horaria:", config.timezone});
        timeService.syncNTP(config.timezone);
        leaveStep();
    }

    struct tm timeinfo;
    getLocalTime(&timeinfo);
    int current_year = timeinfo.tm_year + 1900;
    int current_yday = timeinfo.tm_yday;

    // 3. Fetch Calendar (Holidays - Depends on Country (Country Code))
    if (config.show_calendar && config.calendar_show_holidays && state.calendar.last_fetch_year != current_year && enterStep(STEP_CALENDAR)) {
        showSyncStatus({"\n", "\n", "Cargando feriados...", "\n", "Pais:", oledAscii(config.country)});
        calendarService.fetchHolidays(config.country_code, state.calendar);
        leaveStep();
    }

    // 4. Fetch Weather (Depends on Lat/Lon)
    if (config.show_weather && enterStep(STEP_WEATHER)) {
        showSyncStatus({"\n", "\n", "Actualizando clima...", "\n", "Lugar:", oledAscii(config.city)});
        String updateTime = TimeService::getCurrentTime(config.time_format);
        weatherService.fetchWeather(config, state.weather, updateTime);
        markFetched(SCREEN_WEATHER);
        leaveStep();
    }

    // 5. Fetch Air Quality (Depends on Lat/Lon)
    // (calidad del aire: quitada en v1.4.1)

    // 6. Fetch Daylight (Depends on Lat/Lon)
    if (config.show_daylight && state.daylight.last_fetch_yday != current_yday && enterStep(STEP_DAYLIGHT)) {
        showSyncStatus({"\n", "\n", "Datos del sol...", "\n", "Lugar:", oledAscii(config.city)});
        daylightService.fetchDaylight(config, state.daylight);
        leaveStep();
    }

    // 7. Fetch Moon (Depends on Lat/Lon)
    if (config.show_moon && state.moon.last_fetch_yday != current_yday && enterStep(STEP_MOON)) {
        showSyncStatus({"\n", "\n", "Actualizando luna...", "\n", "Lugar:", oledAscii(config.city)});
        moonService.fetchMoon(config, state.moon);
        leaveStep();
    }

    // 8. Fetch ISS (Independent)
    if (config.show_iss && enterStep(STEP_ISS)) {
        showSyncStatus({"\n", "\n", "Buscando la EEI...", "\n", "Siguiendo:", "Estacion Espacial"});
        issService.fetchIss(config, state.iss);
        markFetched(SCREEN_ISS);
        leaveStep();
    }


    // 10. Fetch Population (Depends on Country Code)
    // (poblacion: quitada en v1.4.1)

    // 11. Fetch Stocks (Independent)
    if (config.show_stock && enterStep(STEP_STOCK)) {
        for (int i = 0; i < config.stock_count && skipRequestedStep != STEP_STOCK; i++) {
            stepStartedAt = millis();
            showSyncStatus({"\n", "\n", "Actualizando acciones", "\n", "Accion:", config.stock_symbols[i]});
            stockService.fetchStock(config.stock_symbols[i], state.stocks[i]);
        }
        markFetched(SCREEN_STOCK);
        leaveStep();
    }

    // 12. Fetch Crypto (Independent)
    if (config.show_crypto && enterStep(STEP_CRYPTO)) {
        for (int i = 0; i < config.crypto_count && skipRequestedStep != STEP_CRYPTO; i++) {
            stepStartedAt = millis();
            showSyncStatus({"\n", "\n", "Actualizando cripto", "\n", "ID:", String(config.crypto_ids[i])});
            cryptoService.fetchPrice(config.crypto_ids[i], state.cryptos[i]);
        }
        markFetched(SCREEN_CRYPTO);
        leaveStep();
    }

    // 13. Fetch Currency (Independent)
    if (config.show_currency && enterStep(STEP_CURRENCY)) {
        for (int i = 0; i < config.currency_count && skipRequestedStep != STEP_CURRENCY; i++) {
            stepStartedAt = millis();
            String baseUpper = String(config.currency_bases[i]);
            baseUpper.toUpperCase();
            String targetUpper = String(config.currency_targets[i]);
            targetUpper.toUpperCase();

            showSyncStatus({"\n", "\n", "Actualizando monedas", "\n", "Monedas:", baseUpper + " -> " + targetUpper});
            currencyService.fetchRate(config.currency_bases[i], config.currency_targets[i], state.currencies[i]);
        }
        markFetched(SCREEN_CURRENCY);
        leaveStep();
    }

    // 14. News (Mini C3)
    if (config.show_news && enterStep(STEP_NEWS)) {
        showSyncStatus({"\n", "\n", "Buscando noticias...", "\n", "Argentina"});
        newsService.fetchNews(state.news, TimeService::getCurrentTime(config.time_format));
        markFetched(SCREEN_NEWS);
        leaveStep();
    }

    {
        StateLock lock;
        displayService.showOLEDStatus({"\n", "\n", "Datos listos", "\n", "\n", "Mini C3 listo"}, true);
    }

    // 14. Save Everything
    configManager.saveConfig(config);

    markGlobalSynced();
}

void DataSyncService::maybeStartBackgroundSync(AppState& state, bool nightModeLatched) {
    bool dueGlobal = isGlobalSyncDue(state.config, nightModeLatched);
    bool anyScreenDue = dueGlobal
        || isDue(SCREEN_WEATHER, state.config, nightModeLatched)
        || isDue(SCREEN_ISS, state.config, nightModeLatched)
        || isDue(SCREEN_FLIGHT, state.config, nightModeLatched)
        || isDue(SCREEN_STOCK, state.config, nightModeLatched)
        || isDue(SCREEN_CRYPTO, state.config, nightModeLatched)
        || isDue(SCREEN_CURRENCY, state.config, nightModeLatched)
        || isDue(SCREEN_NEWS, state.config, nightModeLatched);

    if (!anyScreenDue) return;

    if (lastFailedAttempt != 0 && (millis() - lastFailedAttempt) < RETRY_BACKOFF_MS) return;

    if (!isBackgroundSyncRunning) {
        isBackgroundSyncRunning = true;
        activeState = &state;
        activeNightModeLatched = nightModeLatched;

        Serial.println("Spawning background data update task...");
        xTaskCreate(
            backgroundSyncTaskTrampoline,
            "BgUpdateTask",
            8192,
            this,
            1,
            &backgroundSyncTaskHandle
        );
    }

}

void DataSyncService::backgroundSyncTaskTrampoline(void* parameter) {
    static_cast<DataSyncService*>(parameter)->runBackgroundSyncBody();
}

void DataSyncService::runBackgroundSyncBody() {
    Serial.println("Background Task: Starting API updates...");

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Background Task: WiFi offline. Will retry shortly.");
        lastFailedAttempt = millis();
        isBackgroundSyncRunning = false;
        backgroundSyncTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }

    AppState& state = *activeState;
    Config& config = state.config;
    bool nightModeLatched = activeNightModeLatched;

    struct tm timeinfo;
    getLocalTime(&timeinfo);
    int current_year = timeinfo.tm_year + 1900;
    int current_yday = timeinfo.tm_yday;

    // Daily/yearly screens are always re-checked here; they no-op internally unless the day/year rolled over.
    if (config.show_daylight && state.daylight.last_fetch_yday != current_yday && enterStep(STEP_DAYLIGHT)) { daylightService.fetchDaylight(config, state.daylight); leaveStep(); }
    if (config.show_moon && state.moon.last_fetch_yday != current_yday && enterStep(STEP_MOON)) { moonService.fetchMoon(config, state.moon); leaveStep(); }
    if (config.show_calendar && config.calendar_show_holidays && state.calendar.last_fetch_year != current_year && enterStep(STEP_CALENDAR)) { calendarService.fetchHolidays(config.country_code, state.calendar); leaveStep(); }

    // Custom-sync-eligible screens each check their own due status here.
    if (isDue(SCREEN_WEATHER, config, nightModeLatched) && enterStep(STEP_WEATHER)) {
        weatherService.fetchWeather(config, state.weather, TimeService::getCurrentTime(config.time_format));
        markFetched(SCREEN_WEATHER);
        leaveStep();
    }
    if (isDue(SCREEN_ISS, config, nightModeLatched) && enterStep(STEP_ISS)) {
        issService.fetchIss(config, state.iss);
        markFetched(SCREEN_ISS);
        leaveStep();
    }
    if (isDue(SCREEN_STOCK, config, nightModeLatched) && enterStep(STEP_STOCK)) {
        for (int i = 0; i < config.stock_count; i++) { stepStartedAt = millis(); stockService.fetchStock(config.stock_symbols[i], state.stocks[i]); }
        markFetched(SCREEN_STOCK);
        leaveStep();
    }
    if (isDue(SCREEN_CRYPTO, config, nightModeLatched) && enterStep(STEP_CRYPTO)) {
        for (int i = 0; i < config.crypto_count; i++) { stepStartedAt = millis(); cryptoService.fetchPrice(config.crypto_ids[i], state.cryptos[i]); }
        markFetched(SCREEN_CRYPTO);
        leaveStep();
    }
    if (isDue(SCREEN_CURRENCY, config, nightModeLatched) && enterStep(STEP_CURRENCY)) {
        for (int i = 0; i < config.currency_count; i++) { stepStartedAt = millis(); currencyService.fetchRate(config.currency_bases[i], config.currency_targets[i], state.currencies[i]); }
        markFetched(SCREEN_CURRENCY);
        leaveStep();
    }

    if (isDue(SCREEN_NEWS, config, nightModeLatched) && enterStep(STEP_NEWS)) {
        newsService.fetchNews(state.news, TimeService::getCurrentTime(config.time_format));
        markFetched(SCREEN_NEWS);
        leaveStep();
    }

    if (isGlobalSyncDue(config, nightModeLatched)) markGlobalSynced();
    lastFailedAttempt = 0;

    Serial.println("Background Task: Updates complete.");
    isBackgroundSyncRunning = false;
    backgroundSyncTaskHandle = NULL;
    vTaskDelete(NULL);
}
