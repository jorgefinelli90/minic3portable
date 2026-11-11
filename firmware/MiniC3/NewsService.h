#ifndef NEWS_SERVICE_H
#define NEWS_SERVICE_H

#include <Arduino.h>

#include "structs.h"

// Streaming RSS reader: feed it one character at a time, it collects the
// <title> of each <item> (handles CDATA and HTML entities)
class RssTitleParser {
public:
    RssTitleParser(NewsItem* out, int maxItems, bool sourceInTitle, const char* feedName)
        : out(out), maxItems(maxItems), sourceInTitle(sourceInTitle), feedName(feedName) {}
    void push(char c);
    int count() const { return found; }
    bool full() const { return found >= maxItems; }

    static String decodeEntities(const String& in);

private:
    NewsItem* out;
    int maxItems;
    bool sourceInTitle;
    const char* feedName;
    int found = 0;
    bool inTag = false, inItem = false, capture = false;
    String tag, text;
    void closeTag();
    static void appendUtf8(String& out, uint32_t cp);
};

// Mini C3: Argentine headlines from public RSS feeds (no API key).
// Tries the feeds in order and keeps the first one that answers.
class NewsService {
public:
    bool fetchNews(NewsData& data, const String& updateTime);

private:
    struct Feed {
        const char* name;
        const char* url;
        bool sourceInTitle;   // Google News: "Headline - Source"
    };

    static const uint32_t FEED_DEADLINE_MS = 15000;

    bool fetchFeed(const Feed& feed, NewsItem* out, int& count, String& error);
};

#endif
