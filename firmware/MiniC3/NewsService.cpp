#include "NewsService.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "MiniC3.h"

static const char* BROWSER_UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36";

bool NewsService::fetchNews(NewsData& data, const String& updateTime) {
    static const Feed feeds[] = {
        {"Google Noticias", "https://news.google.com/rss?hl=es-419&gl=AR&ceid=AR:es-419", true},
        {"Clarin", "https://www.clarin.com/rss/lo-ultimo/", false},
        {"La Nacion", "https://www.lanacion.com.ar/arc/outboundfeeds/rss/?outputType=xml", false},
    };

    NewsItem items[MAX_NEWS];
    String lastError = "";

    for (const Feed& feed : feeds) {
        int count = 0;
        String error;
        if (fetchFeed(feed, items, count, error) && count > 0) {
            StateLock lock;
            for (int i = 0; i < MAX_NEWS; i++) data.items[i] = (i < count) ? items[i] : NewsItem();
            data.count = count;
            data.feed = feed.name;
            data.update_time = updateTime;
            data.fetch_status = 1;
            data.fetch_error = "";
            Serial.printf("NewsService: %d titulares de %s\n", count, feed.name);
            return true;
        }
        lastError = String(feed.name) + ": " + error;
        Serial.println("NewsService: fallo " + lastError);
    }

    StateLock lock;
    if (data.count == 0) data.fetch_status = 2;   // keep old headlines if we had some
    data.fetch_error = lastError;
    return false;
}

bool NewsService::fetchFeed(const Feed& feed, NewsItem* out, int& count, String& error) {
    count = 0;
    Serial.printf("NewsService: %s -> %s\n", feed.name, feed.url);

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(8);

    HTTPClient http;
    http.useHTTP10(true);   // plain body (no chunks) and the server closes the connection
    http.setReuse(false);
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (!http.begin(client, feed.url)) {
        error = "begin";
        return false;
    }
    http.setUserAgent(BROWSER_UA);
    http.addHeader("Accept", "application/rss+xml, application/xml, text/xml");

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        error = (code < 0) ? String("sin conexion") : String("HTTP ") + code;
        http.end();
        return false;
    }

    Stream* s = http.getStreamPtr();
    uint32_t deadline = millis() + FEED_DEADLINE_MS;
    RssTitleParser parser(out, MAX_NEWS, feed.sourceInTitle, feed.name);

    while (!parser.full() && millis() < deadline) {
        int c = s->available() ? s->read() : -1;
        if (c < 0) {
            if (!http.connected()) break;
            delay(2);
            continue;
        }
        parser.push((char)c);
    }
    count = parser.count();

    http.end();
    if (count == 0) error = "sin titulares";
    return count > 0;
}

void RssTitleParser::push(char c) {
    if (full()) return;
    if (inTag) {
        bool inCdata = tag.startsWith("![CDATA[");
        if (c == '>' && (!inCdata || tag.endsWith("]]"))) {
            inTag = false;
            closeTag();
        } else if (tag.length() < 400) {
            tag += c;
        }
        return;
    }
    if (c == '<') {
        inTag = true;
        tag = "";
        return;
    }
    if (capture && text.length() < 400) text += c;
}

void RssTitleParser::closeTag() {
    if (tag.startsWith("![CDATA[")) {
        if (capture) text += tag.substring(8, tag.length() - 2);
    } else if (tag == "item" || tag.startsWith("item ")) {
        inItem = true;
    } else if (tag == "/item") {
        inItem = false;
        capture = false;
    } else if (inItem && (tag == "title" || tag.startsWith("title "))) {
        capture = true;
        text = "";
    } else if (capture && tag == "/title") {
        capture = false;
        String title = decodeEntities(text);
        title.trim();
        String source = sourceInTitle ? String("") : String(feedName);
        if (sourceInTitle) {
            int cut = title.lastIndexOf(" - ");
            if (cut > 10) {
                source = title.substring(cut + 3);
                title = title.substring(0, cut);
            }
        }
        if (title.length() > 0) {
            out[found].title = title;
            out[found].source = source;
            found++;
        }
    }
}

void RssTitleParser::appendUtf8(String& out, uint32_t cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

String RssTitleParser::decodeEntities(const String& in) {
    String out;
    out.reserve(in.length());
    for (unsigned int i = 0; i < in.length(); i++) {
        char c = in[i];
        if (c != '&') { out += c; continue; }
        int semi = in.indexOf(';', i);
        if (semi < 0 || semi - (int)i > 10) { out += c; continue; }
        String ent = in.substring(i + 1, semi);
        if (ent == "amp") out += '&';
        else if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (ent == "nbsp") out += ' ';
        else if (ent.startsWith("#x") || ent.startsWith("#X")) appendUtf8(out, strtoul(ent.c_str() + 2, nullptr, 16));
        else if (ent.startsWith("#")) appendUtf8(out, strtoul(ent.c_str() + 1, nullptr, 10));
        else { out += '&'; out += ent; out += ';'; }
        i = semi;
    }
    return out;
}
