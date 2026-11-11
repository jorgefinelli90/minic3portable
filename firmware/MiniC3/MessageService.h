#ifndef MESSAGE_SERVICE_H
#define MESSAGE_SERVICE_H

#include <Arduino.h>
#include <Preferences.h>

#include "structs.h"

// Mini C3: messages sent from any phone/PC on the same WiFi
// (http://minic3.local/mensaje). Kept in flash, so they survive a restart.
class MessageService {
public:
    static const int MAX_FROM = 20;
    static const int MAX_TEXT = 500;

    void begin(MessageData* data);

    // Called by the web server. Returns the message id (0 = rejected).
    uint32_t add(const String& from, const String& text, const String& time);

    int unreadCount() const;
    void markAllRead();
    bool isRead(uint32_t id) const;   // false if unknown

    // Inbox (v1.9.0): delete one (index 0 = newest) or all of them.
    // A deleted message counts as read on the page that sent it.
    bool remove(int index);
    void removeAll();
    void markRead(int index);

    // Set when a message arrives; the main loop shows the alert and clears it
    volatile bool alertPending = false;

private:
    MessageData* data = nullptr;
    Preferences prefs;
    void save();
    void load();
};

#endif
