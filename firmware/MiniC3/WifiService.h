#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#include <Arduino.h>
#include <Preferences.h>

// Mini C3 v1.5.0: several saved WiFi networks.
//
// - Up to MAX_NETWORKS are kept in NVS ("minic3_wifi"), most recent first.
//   They are separate from the rest of the settings, so changing or adding a
//   network never touches the screens, Spotify or the messages.
// - At boot it scans and joins the strongest saved network in range.
// - While running, if the WiFi drops for a while it scans again and moves to
//   any saved network that is around (e.g. you carried it to another room or
//   to the office).
// - Networks are added from the setup portal (MiniC3-Setup) or from the web
//   page /wifi, and forgotten from that page.
class WifiService {
public:
    static const int MAX_NETWORKS = 5;

    struct Network {
        String ssid;
        String pass;
    };

    // Loads the list. The network the board already had saved (from older
    // firmware / the portal) is imported, so nothing has to be typed again.
    void begin();

    // Scans and joins the best saved network. Blocks up to ~timeoutMs.
    bool connectBest(uint32_t timeoutMs = 30000, void (*onTry)(const String& ssid) = nullptr);

    // Adds (or moves to the front) a network and saves it
    void remember(const String& ssid, const String& pass);
    // Saves the network the board is connected to right now
    void rememberCurrent();
    bool forget(int index);

    int count() const { return n; }
    String ssidAt(int i) const { return (i >= 0 && i < n) ? nets[i].ssid : String(""); }
    bool has(const String& ssid) const { return indexOf(ssid) >= 0; }

    // Call every loop: reconnects / moves to another saved network when the
    // WiFi is gone. Returns true once each time the connection comes back
    // (the caller restarts mDNS, OTA...)
    bool tick();
    void setPaused(bool p) { paused = p; }   // sleep, OTA, setup portal
    // After waking up: WiFi back on, starting with the latest network
    void reconnect();
    // Offline right now (e.g. the setup portal timed out): start looking
    void markOffline();
    bool isOnline() const { return wasConnected; }

    // Info panel "Agregar WiFi": restart straight into the setup network
    static void requestPortalOnBoot();
    static bool takePortalRequest();   // true once after such a restart

    // Asked from the web page: switch now (done from tick, after the reply)
    void requestConnect(int index) { pendingConnect = index; }

    // Web page scan (asynchronous). Returns -1 while it is still scanning.
    int scanResults(String& json);

private:
    Network nets[MAX_NETWORKS];
    int n = 0;
    Preferences prefs;

    bool paused = false;
    bool wasConnected = false;
    uint32_t disconnectedSince = 0;
    uint32_t lastRoam = 0;
    bool roamScanning = false;
    volatile int pendingConnect = -1;
    bool webScanning = false;
    uint32_t webScanAt = 0;

    int indexOf(const String& ssid) const;
    void save();
    void startRoamScan();
    void finishRoamScan();
    bool joinAndWait(const Network& net, uint32_t waitMs);
};

#endif
