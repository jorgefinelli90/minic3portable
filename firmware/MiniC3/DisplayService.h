#ifndef DISPLAY_SERVICE_H
#define DISPLAY_SERVICE_H

#include <Adafruit_SH110X.h>
#include <Wire.h>

#include "structs.h"

// UTF-8 -> plain ASCII for the OLED fonts ("Martín" -> "Martin")
String oledAscii(const String& input);

// SH1106 driver whose display() always pushes the whole frame.
// Adafruit's GrayOLED only sends the "dirty" window touched by drawing calls;
// the screen transitions write straight into getBuffer(), so without this the
// animation frames would never reach the panel.
class MiniOled : public Adafruit_SH1106G {
public:
    using Adafruit_SH1106G::Adafruit_SH1106G;
    void display(void) {
        window_x1 = 0;
        window_y1 = 0;
        window_x2 = WIDTH - 1;
        window_y2 = HEIGHT - 1;
        Adafruit_SH1106G::display();
        memcpy(shown, getBuffer(), sizeof(shown));
        shownValid = true;
    }
    // Mini C3 v1.5.0 (games): sends only the bytes that changed since the
    // last frame. A full frame takes ~25 ms over I2C; a game frame where only
    // the ball and the paddle move takes 2-4 ms.
    void displayChanged(void) {
        if (!shownValid || !i2c_dev) {
            display();
            return;
        }
        uint8_t* buf = getBuffer();
        const uint8_t dc = 0x40;
        uint16_t maxbuff = i2c_dev->maxBufferSize() - 1;
        bool any = false;
        for (uint8_t p = 0; p < 8; p++) {
            int base = p * 128;
            int x1 = -1, x2 = -1;
            for (int x = 0; x < 128; x++) {
                if (buf[base + x] != shown[base + x]) {
                    if (x1 < 0) x1 = x;
                    x2 = x;
                }
            }
            if (x1 < 0) continue;
            if (!any) {
                i2c_dev->setSpeed(i2c_preclk);
                any = true;
            }
            uint8_t col = (uint8_t)(x1 + _page_start_offset);
            uint8_t cmd[] = {0x00, (uint8_t)(0xB0 + p), (uint8_t)(0x10 + (col >> 4)), (uint8_t)(col & 0x0F)};
            i2c_dev->write(cmd, 4);
            uint8_t* ptr = buf + base + x1;
            int remaining = x2 - x1 + 1;
            while (remaining > 0) {
                uint8_t n = (uint8_t)min(remaining, (int)maxbuff);
                i2c_dev->write(ptr, n, true, &dc, 1);
                ptr += n;
                remaining -= n;
            }
            memcpy(shown + base + x1, buf + base + x1, x2 - x1 + 1);
        }
        if (any) i2c_dev->setSpeed(i2c_postclk);
        window_x1 = 1024;
        window_y1 = 1024;
        window_x2 = -1;
        window_y2 = -1;
    }
    // 180° rotation done by the panel itself (segment + COM scan direction),
    // so the raw-buffer transitions keep sliding the right way
    void setFlipped(bool flipped) {
        oled_command(flipped ? 0xA0 : 0xA1);
        oled_command(flipped ? 0xC0 : 0xC8);
    }
private:
    uint8_t shown[1024];       // what the panel shows right now
    bool shownValid = false;
};

class DisplayService {
public:
    MiniOled display;

    DisplayService(int width, int height, int reset_pin);
    void begin(int sda, int scl);

    void showOLEDStatus(std::initializer_list<String> lines, bool clear = true);
    void drawTimeScreen(const Config& config, String timeStr, String dateStr);
    void drawCalendarScreen(const Config& config, const CalendarData& calendar);
    void drawWeatherScreen(const Config& config, const WeatherData& data, const String& currentTime);
    void drawAQIScreen(const Config& config, const AirQualityData& data, const String& currentTime);
    void drawDaylightScreen(const Config& config, const DaylightData& data);
    void drawMoonScreen(const Config& config, const MoonData& data);
    void drawIssScreen(const Config& config, const IssData& data);
    void drawPopulationScreen(const Config& config, const PopulationData& data);
    void drawCryptoScreen(const Config& config, const CryptoData& data);
    void drawCurrencyScreen(const Config& config, const CurrencyData& data, int multiplier);
    void drawStockScreen(const Config& config, const StockData& data);
    void drawPcScreen(const PcStats& pcStats);
    void drawMediaScreen(const PcMedia& media);
    void drawBambuScreen(const BambuData& bambu);
    void drawInfoScreen(const unsigned char* image = nullptr, String text = "Sin datos");
    void drawNewsScreen(const NewsData& data, int index);
    void drawMessagesScreen(const MessageData& data, int index);
    bool tickMessages(const AppState& state, bool paused);
    bool isReadingMessage(const AppState& state) const;
    void showMessage(const AppState& state, int index);   // jump to the messages screen
    void playMessageAlert();                              // "Te llego un mensaje!"
    void playBootAnimation();
    // A was touched during the intro (at boot: open the WiFi setup network)
    bool introTouched = false;
    // Spotify
    void drawSpotifyScreen(const SpotifyState& sp);
    bool tickSpotify(const AppState& state);
    void spotifyPop(uint8_t icon, int slideDir = 0);   // 1 play 2 pause 3 next 4 previous
    void spotifyVolume(bool on, int value = -1);
    bool isSpotifyVolumeMode() const { return spVolumeMode; }                             // 4.5 s intro (boot and wake up)
    // News: the headline scrolls up by itself; call every loop. paused = A held.
    bool tickNews(const AppState& state, bool paused);
    // true while a headline is still being read (auto cycle waits)
    bool isReadingNews(const AppState& state) const;

    // Mini C3 extras
    void drawOtaProgress(int percent, const String& line1, const String& line2 = "");

    void drawScreen(int screenIndex, const AppState& state, int subIndex = 0);
    void animateTransition(int prevScreen, int prevSub, int nextScreen, int nextSub, const AppState& state);

    bool isScreenEnabled(const AppState& state, int screenIndex);
    void drawCurrentScreen(const AppState& state);
    // skipSubScreens = true jumps straight to the next/previous screen (↑ / ↓);
    // false steps through stocks / crypto / currencies first (auto cycle)
    void switchToNextScreen(const AppState& state, bool skipSubScreens = false);
    void switchToPreviousScreen(const AppState& state, bool skipSubScreens = false);
    // ← / →: next/previous item inside the screen (flights, stocks, crypto,
    // currencies). Returns false when the screen has nothing to page through.
    // ←: next item inside the screen (flight, stock, crypto, currency, news), wraps around
    bool cycleItem(const AppState& state);
    // →: more data about what is on screen: main -> details 1 -> 2 ... -> main
    bool cycleDetail(const AppState& state);
    // back from the details to the normal screen (returns false if already there)
    bool closeDetails(const AppState& state);
    bool isShowingDetails() const { return detailPage > 0; }
    void jumpToFirstEnabledScreen(const AppState& state);
    bool isOnFirstEnabledScreen(const AppState& state);
    int getCurrentScreen() const { return currentScreen; }


    void setContrast(bool dim);

private:
    uint8_t screenBufferOld[1024];
    uint8_t screenBufferNew[1024];

    int currentScreen = 0;
    int currentSubScreen = 0;
    int detailPage = 0;

    // News: every headline in one list that scrolls up continuously
    static const int NEWS_FEED_MAX_LINES = 120;
    static const int NEWS_TOP = 13;
    static const uint32_t NEWS_START_DELAY_MS = 500;
    static const uint32_t NEWS_STEP_MS = 70;       // 1 px every 70 ms, about 1.4 lines per second
    enum { NEWS_WAIT_START, NEWS_SCROLLING };
    String newsFeedLines[NEWS_FEED_MAX_LINES];
    uint8_t newsFeedFlags[NEWS_FEED_MAX_LINES];
    uint8_t newsFeedItem[NEWS_FEED_MAX_LINES];
    int newsItemStartLine[MAX_NEWS] = {0};
    int newsFeedLineCount = 0;
    String newsFeedSignature = "";
    bool newsActive = false;
    int newsScrollY = 0;
    int newsScrolled = 0;
    int newsPhase = NEWS_WAIT_START;
    uint32_t newsPhaseAt = 0;
    bool newsPaused = false;
    bool newsLoopedOnce = false;
    void buildNewsFeed(const NewsData& data);

    // Messages scroll (same rhythm as the news)
    static const int MSG_MAX_LINES = 30;
    static const int MSG_TOP = 13;
    enum { MSG_WAIT_END = 10 };
    int msgShown = -1;
    int msgScrollY = 0;
    int msgPhase = 0;
    uint32_t msgPhaseAt = 0;
    bool msgPaused = false;
    bool msgReadOnce = false;
    int buildMessageLines(const Message& m, String* lines, int maxLines);

    // Spotify screen state
    String spDrawnTrack = "";
    uint32_t spTrackShownAt = 0;
    uint32_t spLastDraw = 0;
    int spSlideDir = 1;
    uint8_t spPopIcon = 0;
    uint32_t spPopAt = 0;
    bool spVolumeMode = false;
    uint32_t spVolumeAt = 0;
    int spVolumeShown = 0;
    String spLastError = "";
    uint32_t spErrorAt = 0;
    void newsNextItem(const AppState& state);

    struct DetailRow { String label; String value; };
    static const int MAX_DETAIL_ROWS = 24;
    static const int DETAIL_ROWS_PER_PAGE = 5;
    int buildDetailRows(int screen, const AppState& state, int sub, DetailRow* rows, int maxRows, String& title);
    int detailPageCount(int screen, const AppState& state, int sub);
    void drawDetails(int screen, const AppState& state, int sub, int page);

    static const int CONTRAST_DIM = 1;
    static const int CONTRAST_MAX = 255;

    int getFirstEnabledScreen(const AppState& state);
    int subItemCount(const AppState& state, int screenIndex);
    void enterScreen(int screenIndex);

    int getNextAnimationEffect(uint16_t mask);
    void animateHorizontal(int prev, int pSub, int next, int nSub, const AppState& state, bool reverse = false);
    void animateVertical(int prev, int pSub, int next, int nSub, const AppState& state);
    void animateDissolve(int prev, int pSub, int next, int nSub, const AppState& state);
    void animateCurtain(int prev, int pSub, int next, int nSub, const AppState& state);
    void animateBlinds(int prev, int pSub, int next, int nSub, const AppState& state);

    const unsigned char* getWeatherBitmap(int wmo_code, bool is_day);
    const unsigned char* getAQIBitmap(int val, bool is_eu);
};

#endif
