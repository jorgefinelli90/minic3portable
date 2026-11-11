#include "MessageService.h"

#include "MiniC3.h"

void MessageService::begin(MessageData* d) {
    data = d;
    load();
}

uint32_t MessageService::add(const String& fromIn, const String& textIn, const String& time) {
    if (!data) return 0;
    String text = textIn;
    text.trim();
    if (text.length() == 0) return 0;
    if (text.length() > MAX_TEXT) text = text.substring(0, MAX_TEXT);
    text.replace("\r", "");
    text.replace("\n", " ");

    String from = fromIn;
    from.trim();
    if (from.length() == 0) from = "Anonimo";
    if (from.length() > MAX_FROM) from = from.substring(0, MAX_FROM);

    StateLock lock;
    // newest first: shift the others down
    for (int i = MAX_MESSAGES - 1; i > 0; i--) data->items[i] = data->items[i - 1];
    Message& m = data->items[0];
    m.id = data->next_id++;
    m.from = from;
    m.text = text;
    m.time = time;
    m.read = false;
    if (data->count < MAX_MESSAGES) data->count++;
    save();

    alertPending = true;
    Serial.printf("Mensaje #%lu de %s: %s\n", (unsigned long)m.id, from.c_str(), text.c_str());
    return m.id;
}

int MessageService::unreadCount() const {
    if (!data) return 0;
    int n = 0;
    for (int i = 0; i < data->count; i++) if (!data->items[i].read) n++;
    return n;
}

void MessageService::markAllRead() {
    if (!data || unreadCount() == 0) return;
    for (int i = 0; i < data->count; i++) data->items[i].read = true;
    save();
}

bool MessageService::isRead(uint32_t id) const {
    if (!data) return false;
    for (int i = 0; i < data->count; i++) {
        if (data->items[i].id == id) return data->items[i].read;
    }
    return true;   // no longer in the list: it was read long ago
}

bool MessageService::remove(int index) {
    if (!data || index < 0 || index >= data->count) return false;
    StateLock lock;
    Serial.printf("Mensaje #%lu borrado\n", (unsigned long)data->items[index].id);
    for (int i = index; i < data->count - 1; i++) data->items[i] = data->items[i + 1];
    data->count--;
    data->items[data->count] = Message();
    save();
    return true;
}

void MessageService::removeAll() {
    if (!data) return;
    StateLock lock;
    for (int i = 0; i < MAX_MESSAGES; i++) data->items[i] = Message();
    data->count = 0;
    save();
    Serial.println("Mensajes: todos borrados");
}

void MessageService::markRead(int index) {
    if (!data || index < 0 || index >= data->count || data->items[index].read) return;
    data->items[index].read = true;
    save();
}

void MessageService::save() {
    if (!prefs.begin("minic3_msg", false)) return;
    prefs.putInt("count", data->count);
    prefs.putUInt("next", data->next_id);
    for (int i = 0; i < data->count; i++) {
        String k = String(i);
        prefs.putUInt(("i" + k).c_str(), data->items[i].id);
        prefs.putString(("f" + k).c_str(), data->items[i].from);
        prefs.putString(("t" + k).c_str(), data->items[i].text);
        prefs.putString(("h" + k).c_str(), data->items[i].time);
        prefs.putBool(("r" + k).c_str(), data->items[i].read);
    }
    prefs.end();
}

void MessageService::load() {
    if (!prefs.begin("minic3_msg", true)) return;   // nothing saved yet
    int count = prefs.getInt("count", 0);
    if (count < 0) count = 0;
    if (count > MAX_MESSAGES) count = MAX_MESSAGES;
    data->count = count;
    data->next_id = prefs.getUInt("next", 1);
    for (int i = 0; i < count; i++) {
        String k = String(i);
        data->items[i].id = prefs.getUInt(("i" + k).c_str(), 0);
        data->items[i].from = prefs.getString(("f" + k).c_str(), "");
        data->items[i].text = prefs.getString(("t" + k).c_str(), "");
        data->items[i].time = prefs.getString(("h" + k).c_str(), "");
        data->items[i].read = prefs.getBool(("r" + k).c_str(), true);
    }
    prefs.end();
    Serial.printf("MessageService: %d mensajes guardados, %d sin leer\n", count, unreadCount());
}
