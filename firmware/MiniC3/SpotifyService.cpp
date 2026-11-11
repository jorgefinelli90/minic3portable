#include "SpotifyService.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>

#include "esp32c3/rom/tjpgd.h"   // JPEG decoder that lives in the C3 ROM (no flash cost)
#include "MiniC3.h"

static const char* SCOPES = "user-read-playback-state user-modify-playback-state user-read-currently-playing";

// ---------------------------------------------------------------------------
//  small helpers
// ---------------------------------------------------------------------------

static String urlEncode(const String& s) {
    const char* hex = "0123456789ABCDEF";
    String out;
    out.reserve(s.length() * 3);
    for (unsigned int i = 0; i < s.length(); i++) {
        char c = s[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') out += c;
        else { out += '%'; out += hex[(c >> 4) & 0xF]; out += hex[c & 0xF]; }
    }
    return out;
}

static String base64Url(const uint8_t* data, size_t len) {
    const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    String out;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = data[i] << 16;
        if (i + 1 < len) v |= data[i + 1] << 8;
        if (i + 2 < len) v |= data[i + 2];
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        if (i + 1 < len) out += tbl[(v >> 6) & 63];
        if (i + 2 < len) out += tbl[v & 63];
    }
    return out;
}

static String queryParam(const String& url, const String& key) {
    int q = url.indexOf('?');
    String rest = q >= 0 ? url.substring(q + 1) : url;
    int p = 0;
    while (p < (int)rest.length()) {
        int amp = rest.indexOf('&', p);
        if (amp < 0) amp = rest.length();
        String pair = rest.substring(p, amp);
        int eq = pair.indexOf('=');
        if (eq > 0 && pair.substring(0, eq) == key) return pair.substring(eq + 1);
        p = amp + 1;
    }
    return "";
}

// ---------------------------------------------------------------------------

void SpotifyService::begin(SpotifyState* state) {
    st = state;
    if (prefs.begin("minic3_sp", true)) {
        clientId = prefs.getString("cid", "");
        refreshToken = prefs.getString("rt", "");
        prefs.end();
    }
    needUser = isConfigured();
    {
        StateLock lock;
        st->status = isConfigured() ? SP_CONNECTING : SP_NOT_CONFIGURED;
    }
    queue = xQueueCreate(8, sizeof(Msg));
    xTaskCreate(taskTrampoline, "Spotify", 12288, this, 1, NULL);
    Serial.printf("Spotify: %s\n", isConfigured() ? "cuenta vinculada" : "sin vincular (minic3.local/spotify)");
}

void SpotifyService::save() {
    if (!prefs.begin("minic3_sp", false)) return;
    prefs.putString("cid", clientId);
    prefs.putString("rt", refreshToken);
    prefs.end();
}

void SpotifyService::setOnScreen(bool on) {
    if (on && !onScreen) pollNow = true;
    onScreen = on;
}

void SpotifyService::send(Command cmd, int arg) {
    if (!queue) return;
    Msg m{cmd, arg};
    xQueueSend(queue, &m, 0);
}

void SpotifyService::setError(const String& e) {
    StateLock lock;
    st->error = e;
}

// ---------------------------------------------------------------------------
//  Linking
// ---------------------------------------------------------------------------

String SpotifyService::startAuth(const String& cid) {
    clientId = cid;
    clientId.trim();

    const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    codeVerifier = "";
    for (int i = 0; i < 64; i++) codeVerifier += chars[esp_random() % 66];

    uint8_t hash[32];
    mbedtls_sha256((const unsigned char*)codeVerifier.c_str(), codeVerifier.length(), hash, 0);
    String challenge = base64Url(hash, 32);

    return String("https://accounts.spotify.com/authorize?response_type=code") +
           "&client_id=" + urlEncode(clientId) +
           "&scope=" + urlEncode(SCOPES) +
           "&redirect_uri=" + urlEncode(REDIRECT_URI) +
           "&code_challenge_method=S256&code_challenge=" + challenge;
}

bool SpotifyService::finishAuth(const String& pastedUrl, String& error) {
    String err = queryParam(pastedUrl, "error");
    if (err.length()) { error = "Spotify respondio: " + err; return false; }
    String code = queryParam(pastedUrl, "code");
    if (code.length() == 0) { error = "No encuentro el codigo en esa direccion"; return false; }
    if (codeVerifier.length() == 0 || clientId.length() == 0) { error = "Empeza de nuevo desde el paso 2"; return false; }

    String body = "grant_type=authorization_code&code=" + urlEncode(code) +
                  "&redirect_uri=" + urlEncode(REDIRECT_URI) +
                  "&client_id=" + urlEncode(clientId) +
                  "&code_verifier=" + urlEncode(codeVerifier);
    if (!tokenRequest(body, error)) return false;
    if (refreshToken.length() == 0) { error = "Spotify no devolvio el token de refresco"; return false; }

    save();
    codeVerifier = "";
    failures = 0;

    needUser = true;   // the task asks who it is (only the task talks to the API)
    {
        StateLock lock;
        st->status = SP_CONNECTING;
        st->error = "";
    }
    pollNow = true;
    return true;
}

void SpotifyService::unlink() {
    refreshToken = "";
    accessToken = "";
    save();
    StateLock lock;
    st->status = SP_NOT_CONFIGURED;
    st->user = "";
    st->artValid = false;
}

bool SpotifyService::tokenRequest(const String& body, String& error) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(8);
    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    if (!http.begin(client, "https://accounts.spotify.com/api/token")) { error = "sin conexion"; return false; }
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int code = http.POST(body);
    String resp = http.getString();
    http.end();

    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, resp)) { error = "respuesta rara (" + String(code) + ")"; return false; }
    if (code != 200) {
        String desc = doc["error_description"] | "";
        if (desc.length() == 0) desc = doc["error"] | "error";
        error = desc + " (" + String(code) + ")";
        return false;
    }
    accessToken = doc["access_token"] | "";
    int expires = doc["expires_in"] | 3600;
    accessExpiresAt = millis() + (uint32_t)(expires - 60) * 1000UL;
    String rt = doc["refresh_token"] | "";
    if (rt.length() && rt != refreshToken) {   // Spotify may rotate it
        refreshToken = rt;
        save();
    }
    return accessToken.length() > 0;
}

bool SpotifyService::ensureToken() {
    if (accessToken.length() && (int32_t)(accessExpiresAt - millis()) > 0) return true;
    if (!isConfigured()) return false;
    String err;
    String body = "grant_type=refresh_token&refresh_token=" + urlEncode(refreshToken) + "&client_id=" + urlEncode(clientId);
    if (!tokenRequest(body, err)) {
        Serial.println("Spotify: no pude renovar el token: " + err);
        setError(err.startsWith("invalid_grant") || err.indexOf("revoked") >= 0 ? "Volve a vincular" : "Sin conexion");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  Web API
// ---------------------------------------------------------------------------

int SpotifyService::api(const char* method, const String& path, String* response) {
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!ensureToken()) return -1;

        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(6);
        HTTPClient http;
        http.setReuse(false);           // "Connection: close": the read always ends
        http.setConnectTimeout(6000);
        http.setTimeout(6000);
        if (!http.begin(client, "https://api.spotify.com/v1" + path)) return -1;
        http.addHeader("Authorization", "Bearer " + accessToken);

        int code;
        if (strcmp(method, "GET") == 0) {
            code = http.GET();
        } else {
            http.addHeader("Content-Length", "0");
            code = http.sendRequest(method, (uint8_t*)nullptr, 0);
        }
        if (response && code > 0) *response = http.getString();
        http.end();

        if (code == 401 && attempt == 0) {   // token expired early: renew once
            accessToken = "";
            continue;
        }
        return code;
    }
    return -1;
}

void SpotifyService::poll() {
    String body;
    int code = api("GET", "/me/player?additional_types=episode", &body);

    if (code == 204 || (code == 200 && body.length() == 0)) {
        StateLock lock;
        st->status = SP_IDLE;
        st->playing = false;
        st->error = "";
        failures = 0;
        return;
    }
    if (code == 429) { failures = 5; return; }   // rate limited: slow down
    if (code != 200) {
        failures++;
        if (failures >= 3) {
            StateLock lock;
            st->status = SP_ERROR;
            if (st->error.length() == 0) st->error = code < 0 ? "Sin conexion" : "Error " + String(code);
        }
        return;
    }

    DynamicJsonDocument filter(512);
    filter["is_playing"] = true;
    filter["progress_ms"] = true;
    filter["shuffle_state"] = true;
    filter["repeat_state"] = true;
    filter["currently_playing_type"] = true;
    filter["device"]["name"] = true;
    filter["device"]["volume_percent"] = true;
    JsonObject fi = filter.createNestedObject("item");
    fi["id"] = true;
    fi["name"] = true;
    fi["duration_ms"] = true;
    fi["artists"][0]["name"] = true;
    fi["album"]["name"] = true;
    fi["album"]["images"][0]["url"] = true;
    fi["album"]["images"][0]["width"] = true;
    fi["show"]["name"] = true;
    fi["images"][0]["url"] = true;
    fi["images"][0]["width"] = true;

    DynamicJsonDocument doc(6144);
    DeserializationError e = deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (e) {
        Serial.printf("Spotify: JSON %s\n", e.c_str());
        return;
    }
    failures = 0;

    JsonObject item = doc["item"];
    String trackId = item["id"] | "";
    String artist;
    for (JsonObject a : item["artists"].as<JsonArray>()) {
        if (artist.length()) artist += ", ";
        artist += a["name"].as<String>();
        if (artist.length() > 40) break;
    }
    bool episode = String(doc["currently_playing_type"] | "") == "episode";
    if (episode) artist = item["show"]["name"] | "";

    // cover: the smallest image of at least 60 px (Spotify sends 640/300/64)
    JsonArray imgs = episode ? item["images"].as<JsonArray>() : item["album"]["images"].as<JsonArray>();
    String artUrl;
    int best = 100000;
    for (JsonObject im : imgs) {
        int w = im["width"] | 0;
        if (w >= 60 && w < best) { best = w; artUrl = im["url"].as<String>(); }
    }

    bool needArt;
    {
        StateLock lock;
        st->status = SP_ACTIVE;
        st->error = "";
        st->playing = doc["is_playing"] | false;
        st->progressMs = doc["progress_ms"] | 0;
        st->fetchedAt = millis();
        st->shuffle = doc["shuffle_state"] | false;
        st->repeat = doc["repeat_state"] | "off";
        st->device = doc["device"]["name"] | "";
        st->volume = doc["device"]["volume_percent"].isNull() ? -1 : (int)doc["device"]["volume_percent"];
        st->title = item["name"] | (episode ? "Podcast" : "Anuncio");
        st->artist = artist;
        st->album = episode ? String("Podcast") : String(item["album"]["name"] | "");
        st->durationMs = item["duration_ms"] | 0;
        st->trackId = trackId;
        needArt = trackId.length() && st->artTrack != trackId;
        if (needArt) st->artValid = false;
    }
    if (needArt && artUrl.length()) fetchArt(artUrl, trackId);
}

void SpotifyService::runCommand(const Msg& m) {
    int code = -1;
    switch (m.cmd) {
        case CMD_TOGGLE: {
            bool playing;
            { StateLock lock; playing = st->playing; }   // already flipped on screen
            code = api("PUT", playing ? "/me/player/play" : "/me/player/pause");
            break;
        }
        case CMD_NEXT: code = api("POST", "/me/player/next"); break;
        case CMD_PREV: code = api("POST", "/me/player/previous"); break;
        case CMD_VOLUME: code = api("PUT", "/me/player/volume?volume_percent=" + String(constrain(m.arg, 0, 100))); break;
    }
    if (code == 403) setError("Requiere Premium");
    else if (code == 404) setError("Sin dispositivo activo");
    else if (code >= 200 && code < 300) setError("");
    // look again shortly to show the result
    lastPoll = millis() - 2000;
}

// ---------------------------------------------------------------------------
//  Cover: download 64x64 JPEG, decode with the ROM decoder, dither to 32x32
// ---------------------------------------------------------------------------

struct JpegSource {
    const uint8_t* data;
    size_t size;
    size_t pos;
    uint8_t* gray;   // decoded (scaled) image, 1 byte per pixel
    int gw, gh;
};

static UINT jpegInput(JDEC* jd, BYTE* buf, UINT n) {
    JpegSource* src = (JpegSource*)jd->device;
    if (src->pos + n > src->size) n = src->size - src->pos;
    if (buf) memcpy(buf, src->data + src->pos, n);
    src->pos += n;
    return n;
}

static UINT jpegOutput(JDEC* jd, void* bitmap, JRECT* r) {
    JpegSource* src = (JpegSource*)jd->device;
    const uint8_t* rgb = (const uint8_t*)bitmap;
    for (int y = r->top; y <= r->bottom; y++) {
        for (int x = r->left; x <= r->right; x++) {
            uint8_t R = *rgb++, G = *rgb++, B = *rgb++;
            if (x < src->gw && y < src->gh) src->gray[y * src->gw + x] = (uint8_t)((R * 77 + G * 150 + B * 29) >> 8);
        }
    }
    return 1;
}

void SpotifyService::fetchArt(const String& url, const String& trackId) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(6);
    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(6000);
    http.setTimeout(6000);
    if (!http.begin(client, url)) return;
    int code = http.GET();
    int size = http.getSize();
    if (code != 200 || size <= 0 || size > 60000) { http.end(); return; }

    uint8_t* jpg = (uint8_t*)malloc(size);
    if (!jpg) { http.end(); return; }
    NetworkClient* s = http.getStreamPtr();
    int got = 0;
    uint32_t deadline = millis() + 6000;
    while (got < size && millis() < deadline) {
        int a = s->available();
        if (a > 0) got += s->readBytes(jpg + got, min(a, size - got));
        else if (!http.connected()) break;
        else delay(2);
    }
    http.end();
    if (got != size) { free(jpg); return; }

    const size_t WORK = 4096;
    void* work = malloc(WORK);
    JpegSource src{jpg, (size_t)size, 0, nullptr, 0, 0};
    JDEC jd;
    bool ok = false;
    uint8_t out[128];

    if (work && jd_prepare(&jd, jpegInput, work, WORK, &src) == JDR_OK) {
        // scale 1/1, 1/2, 1/4 or 1/8: the smallest that still gives >= 32 px
        uint8_t scale = 0;
        while (scale < 3 && (jd.width >> (scale + 1)) >= 32 && (jd.height >> (scale + 1)) >= 32) scale++;
        src.gw = jd.width >> scale;
        src.gh = jd.height >> scale;
        src.gray = (uint8_t*)calloc(src.gw * src.gh, 1);
        if (src.gray && jd_decomp(&jd, jpegOutput, scale) == JDR_OK) {
            // resample to 32x32 (box average)
            int g[32 * 32];
            int lo = 255, hi = 0;
            for (int y = 0; y < 32; y++) {
                for (int x = 0; x < 32; x++) {
                    int x0 = x * src.gw / 32, x1 = max(x0 + 1, (x + 1) * src.gw / 32);
                    int y0 = y * src.gh / 32, y1 = max(y0 + 1, (y + 1) * src.gh / 32);
                    int sum = 0, n = 0;
                    for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) { sum += src.gray[yy * src.gw + xx]; n++; }
                    int v = sum / n;
                    g[y * 32 + x] = v;
                    lo = min(lo, v); hi = max(hi, v);
                }
            }
            // stretch the contrast, then Atkinson dithering (looks good on OLEDs)
            int range = max(1, hi - lo);
            for (int i = 0; i < 32 * 32; i++) g[i] = (g[i] - lo) * 255 / range;
            memset(out, 0, sizeof(out));
            for (int y = 0; y < 32; y++) {
                for (int x = 0; x < 32; x++) {
                    int old = g[y * 32 + x];
                    int nw = old >= 128 ? 255 : 0;
                    if (nw) out[y * 4 + x / 8] |= (0x80 >> (x & 7));
                    int err = (old - nw) / 8;
                    auto spread = [&](int dx, int dy) {
                        int xx = x + dx, yy = y + dy;
                        if (xx >= 0 && xx < 32 && yy < 32) g[yy * 32 + xx] += err;
                    };
                    spread(1, 0); spread(2, 0); spread(-1, 1); spread(0, 1); spread(1, 1); spread(0, 2);
                }
            }
            ok = true;
        }
        free(src.gray);
    }
    free(work);
    free(jpg);

    if (ok) {
        StateLock lock;
        if (st->trackId == trackId) {
            memcpy(st->art, out, sizeof(out));
            st->artValid = true;
            st->artTrack = trackId;
        }
    } else {
        StateLock lock;
        st->artTrack = trackId;   // do not retry this cover over and over
    }
}

// ---------------------------------------------------------------------------
//  Task
// ---------------------------------------------------------------------------

void SpotifyService::taskTrampoline(void* p) {
    static_cast<SpotifyService*>(p)->taskLoop();
}

void SpotifyService::taskLoop() {
    for (;;) {
        if (!isConfigured() || WiFi.status() != WL_CONNECTED || gOtaInProgress) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (needUser) {
            needUser = false;
            busy = true;
            String me;
            if (api("GET", "/me", &me) == 200) {
                DynamicJsonDocument doc(1024);
                if (!deserializeJson(doc, me)) {
                    StateLock lock;
                    st->user = doc["display_name"] | "";
                }
            }
            busy = false;
        }

        Msg m;
        if (xQueueReceive(queue, &m, 0) == pdTRUE) {
            busy = true;
            runCommand(m);
            busy = false;
            continue;
        }

        uint32_t interval;
        uint8_t status;
        { StateLock lock; status = st->status; }
        if (!onScreen) interval = 60000;                    // off screen: just keep a rough idea
        else if (status == SP_ACTIVE) interval = 2500;
        else interval = 5000;
        if (failures >= 3) interval = max(interval, (uint32_t)10000);

        if (pollNow || millis() - lastPoll >= interval) {
            pollNow = false;
            busy = true;
            poll();
            busy = false;
            lastPoll = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
