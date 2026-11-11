#include "StockService.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// Mini C3: the original read the whole Yahoo response (one day of 1-minute
// candles, tens of KB) with getString() over HTTP/1.1. If Yahoo keeps the
// connection open, that read never ends and the sync stays stuck on
// "Actualizando acciones". Now:
//  - range=1d&interval=1d -> tiny response (we only need the "meta" block)
//  - HTTP/1.0             -> no chunked encoding, the server closes the connection
//  - JSON parsed straight from the stream, every read bounded by the timeout
//  - browser-like User-Agent (Yahoo rejects the default one)
bool StockService::fetchStock(const String& symbol, StockData &data) {
    String safeSymbol = symbol;
    safeSymbol.toUpperCase();
    safeSymbol.trim();
    if (safeSymbol.length() == 0) return false;

    for (int i = 0; i < 2; i++) {
        String url = String(STOCK_API_URLS[i]) + safeSymbol + "?range=1d&interval=1d";
        Serial.println("StockService: Fetching stock data from Yahoo Finance (Attempt " + String(i + 1) + ") -> " + url);

        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(8);   // seconds, per read

        HTTPClient http;
        http.setReuse(false);
        http.useHTTP10(true);
        http.setConnectTimeout(8000);
        http.setTimeout(8000);

        if (!http.begin(client, url)) {
            Serial.println("StockService: begin() failed");
            continue;
        }
        http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36");
        http.addHeader("Accept", "application/json");

        int httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
            DynamicJsonDocument filter(256);
            filter["chart"]["result"][0]["meta"]["symbol"] = true;
            filter["chart"]["result"][0]["meta"]["shortName"] = true;
            filter["chart"]["result"][0]["meta"]["regularMarketPrice"] = true;
            filter["chart"]["result"][0]["meta"]["chartPreviousClose"] = true;
            filter["chart"]["result"][0]["meta"]["previousClose"] = true;

            DynamicJsonDocument doc(1024);
            DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
            http.end();

            if (!error) {
                JsonArray resultArray = doc["chart"]["result"];
                if (!resultArray.isNull() && resultArray.size() > 0) {
                    JsonObject meta = resultArray[0]["meta"];

                    data.symbol = safeSymbol;
                    data.name = meta.containsKey("shortName") ? meta["shortName"].as<String>() : safeSymbol;
                    data.price = meta["regularMarketPrice"].as<float>();
                    float prev = meta["chartPreviousClose"].as<float>();
                    if (prev <= 0) prev = meta["previousClose"].as<float>();
                    data.previous_close = prev;

                    if (data.previous_close > 0) {
                        data.percent_change = ((data.price - data.previous_close) / data.previous_close) * 100.0;
                    } else {
                        data.percent_change = 0.0;
                    }

                    data.updated = true;

                    Serial.printf("StockService: Success! %s (%s): $%.2f Change: %+.2f%%\n", data.symbol.c_str(), data.name.c_str(), data.price, data.percent_change);
                    return true;
                } else {
                    Serial.println("StockService: ERROR! 'result' array missing or empty in JSON.");
                }
            } else {
                Serial.printf("StockService: JSON parsing failed: %s\n", error.c_str());
            }
        } else {
            Serial.printf("StockService: API failed, HTTP Code: %d\n", httpCode);
            http.end();
        }

        if (i == 0) {
            Serial.println("StockService: Primary API failed. Switching to fallback...");
        }
    }

    Serial.println("StockService: All endpoints failed.");
    return false;
}
