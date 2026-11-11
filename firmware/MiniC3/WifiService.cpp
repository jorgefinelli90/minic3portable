#include "WifiService.h"

#include <ArduinoJson.h>
#include <esp_wifi.h>
#include <WiFi.h>

#include "MiniC3.h"

static const char* WIFI_NS = "minic3_wifi";
static const uint32_t ROAM_AFTER_MS = 15000;     // offline this long -> look for another saved network
static const uint32_t ROAM_EVERY_MS = 30000;     // and again every 30 s while still offline
static const uint32_t SCAN_TIMEOUT_MS = 12000;

static uint8_t joinFails[WifiService::MAX_NETWORKS] = {0};   // RAM only: bad password / out of range
static int tryingIndex = -1;
static uint32_t roamScanAt = 0;

// survives ESP.restart() (not a power cycle)
RTC_NOINIT_ATTR static uint32_t rtcPortalMagic;
static const uint32_t PORTAL_MAGIC = 0xAD0F17E5;

void WifiService::requestPortalOnBoot() { rtcPortalMagic = PORTAL_MAGIC; }

bool WifiService::takePortalRequest() {
    bool r = (rtcPortalMagic == PORTAL_MAGIC);
    rtcPortalMagic = 0;
    return r;
}

void WifiService::begin() {
    prefs.begin(WIFI_NS, true);
    n = constrain((int)prefs.getUChar("n", 0), 0, MAX_NETWORKS);
    for (int i = 0; i < n; i++) {
        nets[i].ssid = prefs.getString(("s" + String(i)).c_str(), "");
        nets[i].pass = prefs.getString(("p" + String(i)).c_str(), "");
    }
    prefs.end();

    // drop empty entries (should not happen)
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (nets[i].ssid.length()) nets[k++] = nets[i];
    }
    n = k;

    // The network saved by the portal / older firmware lives in the WiFi
    // driver's own storage: bring it into the list so it is not lost
    WiFi.setHostname(DEVICE_HOSTNAME);
    WiFi.mode(WIFI_STA);
    wifi_config_t conf;
    memset(&conf, 0, sizeof(conf));
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
        char ssid[33] = {0};
        char pass[65] = {0};
        memcpy(ssid, conf.sta.ssid, 32);
        memcpy(pass, conf.sta.password, 64);
        if (ssid[0] && !has(String(ssid))) {
            if (n < MAX_NETWORKS) {
                nets[n].ssid = ssid;
                nets[n].pass = pass;
                n++;
                save();
                Serial.printf("WiFi: importada la red guardada \"%s\"\n", ssid);
            }
        }
    }

    Serial.printf("WiFi: %d redes guardadas\n", n);
    for (int i = 0; i < n; i++) Serial.printf("  %d. %s\n", i + 1, nets[i].ssid.c_str());
}

int WifiService::indexOf(const String& ssid) const {
    for (int i = 0; i < n; i++) {
        if (nets[i].ssid == ssid) return i;
    }
    return -1;
}

void WifiService::save() {
    prefs.begin(WIFI_NS, false);
    prefs.putUChar("n", (uint8_t)n);
    for (int i = 0; i < MAX_NETWORKS; i++) {
        String ks = "s" + String(i);
        String kp = "p" + String(i);
        if (i < n) {
            prefs.putString(ks.c_str(), nets[i].ssid);
            prefs.putString(kp.c_str(), nets[i].pass);
        } else {
            prefs.remove(ks.c_str());
            prefs.remove(kp.c_str());
        }
    }
    prefs.end();
}

void WifiService::remember(const String& ssid, const String& pass) {
    if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64) return;
    int idx = indexOf(ssid);
    if (idx == 0 && nets[0].pass == pass) return;   // nothing changes: no flash write

    if (idx < 0) idx = (n < MAX_NETWORKS) ? n++ : MAX_NETWORKS - 1;   // full: the oldest goes
    for (int i = idx; i > 0; i--) {
        nets[i] = nets[i - 1];
        joinFails[i] = joinFails[i - 1];
    }
    nets[0].ssid = ssid;
    nets[0].pass = pass;
    joinFails[0] = 0;
    save();
    Serial.printf("WiFi: guardada \"%s\" (%d redes)\n", ssid.c_str(), n);
}

void WifiService::rememberCurrent() {
    if (WiFi.status() != WL_CONNECTED) return;
    remember(WiFi.SSID(), WiFi.psk());
}

bool WifiService::forget(int index) {
    if (index < 0 || index >= n) return false;
    Serial.printf("WiFi: olvidada \"%s\"\n", nets[index].ssid.c_str());
    for (int i = index; i < n - 1; i++) {
        nets[i] = nets[i + 1];
        joinFails[i] = joinFails[i + 1];
    }
    n--;
    nets[n].ssid = "";
    nets[n].pass = "";
    joinFails[n] = 0;
    save();
    return true;
}

bool WifiService::joinAndWait(const Network& net, uint32_t waitMs) {
    WiFi.disconnect(false, false);
    delay(50);
    WiFi.begin(net.ssid.c_str(), net.pass.c_str());
    uint32_t t0 = millis();
    while (millis() - t0 < waitMs) {
        wl_status_t s = WiFi.status();
        if (s == WL_CONNECTED) return true;
        // wrong password shows up as CONNECT_FAILED after a few seconds
        if (s == WL_CONNECT_FAILED && millis() - t0 > 4000) break;
        delay(100);
    }
    return WiFi.status() == WL_CONNECTED;
}

bool WifiService::connectBest(uint32_t timeoutMs, void (*onTry)(const String& ssid)) {
    if (n == 0) return false;
    uint32_t t0 = millis();

    WiFi.setHostname(DEVICE_HOSTNAME);
    WiFi.mode(WIFI_STA);

    // Which saved networks are around, strongest first
    int order[MAX_NETWORKS];
    int rssi[MAX_NETWORKS];
    int found = 0;
    int r = WiFi.scanNetworks(false, true);
    for (int i = 0; i < n; i++) {
        int best = -1000;
        for (int j = 0; j < r; j++) {
            if (WiFi.SSID(j) == nets[i].ssid && WiFi.RSSI(j) > best) best = WiFi.RSSI(j);
        }
        if (best > -1000) {
            int pos = found++;
            while (pos > 0 && rssi[pos - 1] < best) {
                order[pos] = order[pos - 1];
                rssi[pos] = rssi[pos - 1];
                pos--;
            }
            order[pos] = i;
            rssi[pos] = best;
        }
    }
    WiFi.scanDelete();
    Serial.printf("WiFi: %d redes a la vista, %d guardadas cerca\n", r, found);

    // None in the scan (hidden network, or the scan missed it): try the latest anyway
    if (found == 0) {
        order[0] = 0;
        found = 1;
    }

    for (int k = 0; k < found && millis() - t0 < timeoutMs; k++) {
        const Network& net = nets[order[k]];
        if (onTry) onTry(net.ssid);
        Serial.printf("WiFi: probando \"%s\"\n", net.ssid.c_str());
        if (joinAndWait(net, 15000)) {
            Serial.printf("WiFi: conectado a \"%s\"\n", net.ssid.c_str());
            remember(net.ssid, net.pass);   // goes to the front
            wasConnected = true;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
//  Running: reconnect / move to another saved network
// ---------------------------------------------------------------------------

void WifiService::startRoamScan() {
    if (tryingIndex >= 0 && tryingIndex < n && joinFails[tryingIndex] < 250) joinFails[tryingIndex]++;
    WiFi.disconnect(false, false);   // stop the automatic retries so the scan can run
    delay(20);
    int r = WiFi.scanNetworks(true, true);
    if (r == WIFI_SCAN_FAILED) {
        Serial.println("WiFi: no se pudo escanear");
        if (n > 0) {
            tryingIndex = 0;
            WiFi.begin(nets[0].ssid.c_str(), nets[0].pass.c_str());
        }
        return;
    }
    roamScanning = true;
    roamScanAt = millis();
}

void WifiService::finishRoamScan() {
    int r = WiFi.scanComplete();
    if (r == WIFI_SCAN_RUNNING && millis() - roamScanAt < SCAN_TIMEOUT_MS) return;
    roamScanning = false;

    // best saved network in range; ones that keep failing go last
    int pick = -1;
    int pickScore = -100000;
    for (int i = 0; i < n; i++) {
        int best = -1000;
        for (int j = 0; j < r; j++) {
            if (WiFi.SSID(j) == nets[i].ssid && WiFi.RSSI(j) > best) best = WiFi.RSSI(j);
        }
        if (best <= -1000) continue;
        int score = best - (joinFails[i] >= 2 ? 1000 : 0);
        if (score > pickScore) {
            pickScore = score;
            pick = i;
        }
    }
    WiFi.scanDelete();

    if (pick < 0) pick = 0;   // nothing around: keep trying the latest
    if (n == 0) return;
    tryingIndex = pick;
    Serial.printf("WiFi: buscando otra red -> \"%s\"\n", nets[pick].ssid.c_str());
    WiFi.begin(nets[pick].ssid.c_str(), nets[pick].pass.c_str());
}

bool WifiService::tick() {
    if (paused) return false;
    uint32_t now = millis();

    // switch asked from the web page (after its reply went out)
    int pc = pendingConnect;
    if (pc >= 0) {
        pendingConnect = -1;
        if (pc < n && !roamScanning && !webScanning) {
            Serial.printf("WiFi: cambiando a \"%s\"\n", nets[pc].ssid.c_str());
            delay(300);
            WiFi.disconnect(false, false);
            delay(50);
            tryingIndex = pc;
            WiFi.begin(nets[pc].ssid.c_str(), nets[pc].pass.c_str());
            wasConnected = false;
            disconnectedSince = now;
            lastRoam = now;
            return false;
        }
    }

    bool conn = (WiFi.status() == WL_CONNECTED);

    if (conn) {
        if (roamScanning) {
            WiFi.scanDelete();
            roamScanning = false;
        }
        if (!wasConnected) {
            wasConnected = true;
            disconnectedSince = 0;
            int idx = indexOf(WiFi.SSID());
            if (idx >= 0) joinFails[idx] = 0;
            rememberCurrent();
            Serial.printf("WiFi: conectado a \"%s\", %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            return true;
        }
        return false;
    }

    if (wasConnected) {
        wasConnected = false;
        disconnectedSince = now;
        lastRoam = now;
        tryingIndex = indexOf(WiFi.SSID());
        Serial.println("WiFi: se corto la conexion");
    }
    if (disconnectedSince == 0) {
        disconnectedSince = now;
        lastRoam = now;
    }

    if (roamScanning) {
        finishRoamScan();
    } else if (n > 0 && !webScanning && now - disconnectedSince > ROAM_AFTER_MS && now - lastRoam > ROAM_EVERY_MS) {
        lastRoam = now;
        startRoamScan();
    }
    return false;
}

void WifiService::markOffline() {
    wasConnected = false;
    disconnectedSince = millis();
    lastRoam = millis() - ROAM_EVERY_MS + ROAM_AFTER_MS;   // first look around ~15 s after waking
}

void WifiService::reconnect() {
    WiFi.setHostname(DEVICE_HOSTNAME);
    WiFi.mode(WIFI_STA);
    if (n > 0) {
        tryingIndex = 0;
        WiFi.begin(nets[0].ssid.c_str(), nets[0].pass.c_str());
    } else {
        WiFi.begin();
    }
    markOffline();
}

// ---------------------------------------------------------------------------
//  Web page scan
// ---------------------------------------------------------------------------

int WifiService::scanResults(String& json) {
    if (roamScanning) return -1;

    if (!webScanning) {
        int r = WiFi.scanNetworks(true, false);
        if (r == WIFI_SCAN_FAILED) {
            json = "[]";
            return 0;
        }
        webScanning = true;
        webScanAt = millis();
        return -1;
    }

    int r = WiFi.scanComplete();
    if (r == WIFI_SCAN_RUNNING && millis() - webScanAt < SCAN_TIMEOUT_MS) return -1;
    webScanning = false;
    if (r < 0) r = 0;

    DynamicJsonDocument doc(3072);
    JsonArray arr = doc.to<JsonArray>();
    int added = 0;
    for (int j = 0; j < r && added < 20; j++) {
        String s = WiFi.SSID(j);
        if (s.length() == 0) continue;
        bool dup = false;
        for (int k = 0; k < j; k++) {
            if (WiFi.SSID(k) == s) { dup = true; break; }
        }
        if (dup) continue;
        JsonObject o = arr.createNestedObject();
        o["ssid"] = s;
        o["rssi"] = WiFi.RSSI(j);
        o["open"] = (WiFi.encryptionType(j) == WIFI_AUTH_OPEN);
        o["saved"] = has(s);
        added++;
    }
    WiFi.scanDelete();
    json = "";
    serializeJson(doc, json);
    return added;
}
