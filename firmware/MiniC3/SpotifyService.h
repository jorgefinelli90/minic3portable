#ifndef SPOTIFY_SERVICE_H
#define SPOTIFY_SERVICE_H

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "structs.h"

// Mini C3: Spotify Web API, straight from the board (no server).
//
// Linking (once, from http://minic3.local/spotify):
//   Authorization Code + PKCE, so no client secret is stored. Spotify only
//   accepts http redirects to 127.0.0.1, so after accepting, the browser lands
//   on http://127.0.0.1:8888/callback?code=... (a page that does not load) and
//   that address is pasted back into the setup page.
//
// After that a small task polls /me/player while the Spotify screen is open
// and runs the commands (play/pause, next, previous, volume).
class SpotifyService {
public:
    enum Command : uint8_t { CMD_TOGGLE, CMD_NEXT, CMD_PREV, CMD_VOLUME };

    static constexpr const char* REDIRECT_URI = "http://127.0.0.1:8888/callback";

    void begin(SpotifyState* state);

    bool isConfigured() const { return refreshToken.length() > 0 && clientId.length() > 0; }
    bool isBusy() const { return busy; }
    String getClientId() const { return clientId; }

    // The screen is (not) on display: fast polling only while it is
    void setOnScreen(bool on);
    void pollSoon() { pollNow = true; }

    // Queue a command for the task (returns immediately)
    void send(Command cmd, int arg = 0);

    // --- linking (called from the web server) ---
    String startAuth(const String& clientId);                  // returns the URL to open
    bool finishAuth(const String& pastedUrl, String& error);   // exchanges the code
    void unlink();

private:
    struct Msg { Command cmd; int arg; };

    SpotifyState* st = nullptr;
    Preferences prefs;
    QueueHandle_t queue = nullptr;

    String clientId = "";
    String refreshToken = "";
    String accessToken = "";
    uint32_t accessExpiresAt = 0;
    String codeVerifier = "";

    volatile bool onScreen = false;
    volatile bool pollNow = true;
    volatile bool busy = false;
    volatile bool needUser = false;
    uint32_t lastPoll = 0;
    uint8_t failures = 0;

    static void taskTrampoline(void* p);
    void taskLoop();

    bool ensureToken();
    bool tokenRequest(const String& body, String& error);
    void poll();
    void runCommand(const Msg& m);
    int api(const char* method, const String& path, String* response = nullptr);
    void fetchArt(const String& url, const String& trackId);

    void setError(const String& e);
    void save();
};

#endif
