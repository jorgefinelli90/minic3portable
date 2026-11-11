#ifndef WEB_SERVER_SERVICE_H
#define WEB_SERVER_SERVICE_H

#include <WebServer.h>
#include <WiFiManager.h>
#include "structs.h"

typedef void (*ConfigSaveCallback)();

class WebServerService {
public:
    WebServerService(int port, ConfigSaveCallback callback);
    void begin();
    void handleClient();
    void setAppState(AppState* appState);
    
    void handleRoot();
    void handleSave();
    void handleUpdate();
    void handlePcStats();
    void handleUsbReconnect();
    void handleOtaPage();
    void handleOtaUpload();
    void handleOtaDone();
    void handleMessagePage();
    void handleMessagePost();
    void handleMessageStatus();
    void handleSpotifyPage();
    void handleSpotifyStart();
    void handleSpotifyFinish();
    void handleSpotifyUnlink();
    void handleWifiPage();
    void handleWifiScan();
    void handleWifiAdd();
    void handleWifiForget();
    void handleWifiConnect();
    
private:
    static const unsigned long PC_DATA_TIMEOUT_MS = 10000;
    static const int HTTP_OK = 200;
    static const int HTTP_REDIRECT = 302;
    static const int HTTP_BAD_REQUEST = 400;
    static const int HTTP_FORBIDDEN = 403;
    static constexpr const char* LOCAL_DOMAIN_NAME = "minic3";

    WebServer server;
    WiFiManager wm;
    ConfigSaveCallback saveCallback;
    
    AppState* state;

    bool wifiAuth();   // same user / password as the OTA page

    // web OTA upload state
    bool otaAuthOk = false;
    bool otaFirstChunk = true;
    String otaError = "";
    size_t otaWritten = 0;
    int otaExpected = 0;
};

#endif