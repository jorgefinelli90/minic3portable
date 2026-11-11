#include "DisplayService.h"

#include <Arduino.h>
#include <Fonts/Picopixel.h>
#include <time.h>
#include <WiFi.h>

#include "images.h"
#include "MiniC3.h"
#include "DataSyncService.h"
#include "GameService.h"
#include "TrucoService.h"
#include "MessageInbox.h"

extern GameService gameService;
extern MessageInbox messageInbox;
extern TrucoService trucoService;
#include "IssService.h"

// ---------------------------------------------------------------------------
//  Mini C3: text helpers
// ---------------------------------------------------------------------------
static String mapAccentedChar(uint32_t cp) {
        switch (cp) {
            case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5:
            case 0x100: case 0x102: case 0x104: return "A";
            case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5:
            case 0x101: case 0x103: case 0x105: return "a";
            case 0xC7: case 0x106: case 0x108: case 0x10A: case 0x10C: return "C";
            case 0xE7: case 0x107: case 0x109: case 0x10B: case 0x10D: return "c";
            case 0xC8: case 0xC9: case 0xCA: case 0xCB:
            case 0x112: case 0x114: case 0x116: case 0x118: case 0x11A: return "E";
            case 0xE8: case 0xE9: case 0xEA: case 0xEB:
            case 0x113: case 0x115: case 0x117: case 0x119: case 0x11B: return "e";
            case 0xCC: case 0xCD: case 0xCE: case 0xCF:
            case 0x128: case 0x12A: case 0x12C: case 0x12E: case 0x130: return "I";
            case 0xEC: case 0xED: case 0xEE: case 0xEF:
            case 0x129: case 0x12B: case 0x12D: case 0x12F: case 0x131: return "i";
            case 0xD1: case 0x143: case 0x145: case 0x147: return "N";
            case 0xF1: case 0x144: case 0x146: case 0x148: return "n";
            case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: case 0xD8:
            case 0x14C: case 0x14E: case 0x150: return "O";
            case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF8:
            case 0x14D: case 0x14F: case 0x151: return "o";
            case 0xD9: case 0xDA: case 0xDB: case 0xDC:
            case 0x168: case 0x16A: case 0x16C: case 0x16E: case 0x170: case 0x172: return "U";
            case 0xF9: case 0xFA: case 0xFB: case 0xFC:
            case 0x169: case 0x16B: case 0x16D: case 0x16F: case 0x171: case 0x173: return "u";
            case 0xDD: case 0x176: case 0x178: return "Y";
            case 0xFD: case 0xFF: case 0x177: return "y";
            case 0xD0: return "D";
            case 0xF0: return "d";
            case 0xDE: return "Th";
            case 0xFE: return "th";
            case 0xDF: return "ss";
            case 0xC6: return "AE";
            case 0xE6: return "ae";
            case 0x152: return "OE";
            case 0x153: return "oe";
            case 0x160: return "S";
            case 0x161: return "s";
            case 0x179: case 0x17B: case 0x17D: return "Z";
            case 0x17A: case 0x17C: case 0x17E: return "z";
            case 0x141: return "L";
            case 0x142: return "l";
            case 0x158: return "R";
            case 0x159: return "r";
            case 0x164: return "T";
            case 0x165: return "t";
            case 0x11E: return "G";
            case 0x11F: return "g";
            default: return "";
        }
    }

// UTF-8 -> plain ASCII for the OLED fonts ("Martín" -> "Martin")
String oledAscii(const String& input) {
        String out;
        out.reserve(input.length());

        for (size_t i = 0; i < input.length();) {
            uint8_t c = (uint8_t)input[i];

            if (c < 0x80) {
                out += (char)c;
                i++;
                continue;
            }

            uint32_t codepoint = 0;
            int extraBytes;
            if ((c & 0xE0) == 0xC0) { codepoint = c & 0x1F; extraBytes = 1; }
            else if ((c & 0xF0) == 0xE0) { codepoint = c & 0x0F; extraBytes = 2; }
            else if ((c & 0xF8) == 0xF0) { codepoint = c & 0x07; extraBytes = 3; }
            else { i++; continue; }  // stray continuation byte on its own

            size_t seqLen = 1;
            bool valid = true;
            for (int b = 0; b < extraBytes; b++) {
                if (i + seqLen >= input.length()) { valid = false; break; }
                uint8_t cont = (uint8_t)input[i + seqLen];
                if ((cont & 0xC0) != 0x80) { valid = false; break; }
                codepoint = (codepoint << 6) | (cont & 0x3F);
                seqLen++;
            }
            if (!valid) { i++; continue; }

            out += mapAccentedChar(codepoint);  // "" for anything with no ASCII fallback
            i += seqLen;
        }

        return out;
    }

#include "IssService.h"
#include "PopulationService.h"
#include "TimeService.h"
#include "WeatherService.h"

const unsigned char* DisplayService::getWeatherBitmap(int wmo_code, bool is_day) {
    if (wmo_code == 0) {
        if (is_day) {
            return icon_sun;
        }
        return icon_moon;
    }
    else if (wmo_code >= 1 && wmo_code <= 3) {
        if (is_day) {
            return icon_cloud;
        }
        return icon_cloud_moon; 
    }
    else if (wmo_code >= 45 && wmo_code <= 48) return icon_fog;
    else if (wmo_code >= 51 && wmo_code <= 67) return icon_rain;
    else if (wmo_code >= 71 && wmo_code <= 77) return icon_snow;
    else if (wmo_code >= 95) return icon_thunder;
    return icon_cloud;
}

const unsigned char* DisplayService::getAQIBitmap(int val, bool is_eu) {
    if (is_eu) {
        if (val <= 20) return icon_smile;    
        if (val <= 60) return icon_neutral;
        if (val <= 80) return icon_bad;
        return icon_dead;                   
    } else {
        if (val <= 50)  return icon_smile;
        if (val <= 100) return icon_neutral;
        if (val <= 150) return icon_bad;
        return icon_dead;
    }
}

DisplayService::DisplayService(int width, int height, int reset_pin) : 
    display(width, height, &Wire, reset_pin) {}

void DisplayService::begin(int sda, int scl) {
    Wire.begin(sda, scl);
    if(!display.begin(OLED_I2C_ADDR, true)) { 
        Serial.println(F("DisplayService: SH1106 init failed."));
    } else {
        Serial.println("DisplayService: Display initialized.");
        display.setFlipped(OLED_FLIP);
        display.clearDisplay();
        display.display();
    }
}

void DisplayService::showOLEDStatus(std::initializer_list<String> lines, bool clear) {
    if (clear) display.clearDisplay();

    display.setTextColor(SH110X_WHITE);
    display.setTextWrap(false);
    display.setTextSize(1);
    display.setFont(); 
    
    int lineHeight = 10;
    int cursorY = 0;
    int16_t x1, y1;
    uint16_t w, h;
    
    for (const String& line : lines) {
        if (line == "\n") { 
            cursorY += lineHeight / 2;
            continue;
        }

        display.getTextBounds(line.c_str(), 0, 0, &x1, &y1, &w, &h);
        int xStart = (display.width() - w) / 2;
        
        display.setCursor(xStart, cursorY);
        display.print(line);
        
        cursorY += lineHeight;
        
        if (cursorY >= display.height()) break;
    }
    
    display.display();
}

void DisplayService::drawTimeScreen(const Config& config, String timeStr, String dateStr) {
    display.clearDisplay();

    display.setTextColor(SH110X_WHITE);
    display.setTextWrap(false);
    display.setTextSize(1);
    display.setFont(); 

    int16_t x1, y1;
    uint16_t w, h;

    if (config.date_display) {
        display.setTextSize(3);
        display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
        display.setCursor((128 - w) / 2, 10);
        display.print(timeStr);

        display.setTextSize(1);
        display.getTextBounds(dateStr, 0, 0, &x1, &y1, &w, &h);
        display.setCursor((128 - w) / 2, 48);
        display.print(dateStr);
    } else {
        display.setTextSize(4);
        display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);

        int xPos = (128 - w) / 2;
        int yPos = (64 - h) / 2 - y1;

        display.setCursor(xPos, yPos);
        display.print(timeStr);
    }
}

void DisplayService::drawCalendarScreen(const Config& config, const CalendarData& calendar) {
    display.clearDisplay();

    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    int day = timeinfo.tm_mday;
    int mon = timeinfo.tm_mon; 
    int year = timeinfo.tm_year + 1900;
    int wday = timeinfo.tm_wday; 

    const char* months[] = {"ENERO", "FEBRERO", "MARZO", "ABRIL", "MAYO", "JUNIO", "JULIO", "AGOSTO", "SEPTIEMBRE", "OCTUBRE", "NOVIEMBRE", "DICIEMBRE"};
    const char* weekdays[] = {"Domingo", "Lunes", "Martes", "Miercoles", "Jueves", "Viernes", "Sabado"};

    // 1. Check if Today is a Holiday
    String todayHolidayName = "";
    if (config.calendar_show_holidays) {
        char todayBuf[16];
        snprintf(todayBuf, sizeof(todayBuf), "%04d-%02d-%02d", year, mon + 1, day);
        String todayStr = String(todayBuf);
        for (int i = 0; i < calendar.count; i++) {
            if (calendar.items[i].date == todayStr) {
                todayHolidayName = calendar.items[i].name;
                break;
            }
        }
    }

    // 2. Draw Main Text
    int colWidth = config.calendar_minimal ? 128 : 57;

    display.setTextColor(SH110X_WHITE);
    display.setTextWrap(false);
    display.setFont(); 

    int16_t x1, y1; uint16_t w, h;

    display.setTextSize(3);
    display.getTextBounds(String(day).c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor((colWidth - w) / 2, 3);
    display.print(day);

    display.setTextSize(1);
    display.getTextBounds(months[mon], 0, 0, &x1, &y1, &w, &h);
    display.setCursor((colWidth - w) / 2, 29);
    display.print(months[mon]);

    display.getTextBounds(String(year).c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor((colWidth - w) / 2, 53);
    display.print(year);

    if (todayHolidayName != "") {
        display.setFont(&Picopixel);
        
        String lines[2] = {"", ""};
        int l = 0, start = 0;
        todayHolidayName = oledAscii(todayHolidayName);
        todayHolidayName.toUpperCase();
        
        while (start < todayHolidayName.length() && l < 2) {
            int spaceIdx = todayHolidayName.indexOf(' ', start);
            if (spaceIdx == -1) spaceIdx = todayHolidayName.length();
            String word = todayHolidayName.substring(start, spaceIdx);
            
            String testLine = lines[l].length() == 0 ? word : lines[l] + " " + word;
            display.getTextBounds(testLine.c_str(), 0, 0, &x1, &y1, &w, &h);
            
            if (w > colWidth) {
                if (lines[l].length() == 0) { lines[l] = word; l++; }
                else { l++; if (l < 2) lines[l] = word; }
            } else {
                lines[l] = testLine;
            }
            start = spaceIdx + 1;
        }

        int numLines = (lines[1].length() > 0) ? 2 : 1;
        int cursorY = (numLines == 1) ? 47 : 44; 

        for (int i = 0; i < numLines; i++) {
            if (lines[i].length() > 0) {
                display.getTextBounds(lines[i].c_str(), 0, 0, &x1, &y1, &w, &h);
                bool truncated = false;
                while (w > colWidth && lines[i].length() > 0) {
                    lines[i] = lines[i].substring(0, lines[i].length() - 1);
                    display.getTextBounds((lines[i] + ".").c_str(), 0, 0, &x1, &y1, &w, &h);
                    truncated = true;
                }
                if (truncated) lines[i] += ".";

                display.setCursor((colWidth - w) / 2, cursorY);
                display.print(lines[i]);
                cursorY += 6; 
            }
        }
        display.setFont(); 
    } else {
        display.getTextBounds(weekdays[wday], 0, 0, &x1, &y1, &w, &h);
        display.setCursor((colWidth - w) / 2, 41);
        display.print(weekdays[wday]);
    }

    // 3. Stop if in Minimalistic Mode
    if (config.calendar_minimal) return;

    // 4. Right Column Grid Setup
    int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) daysInMonth[1] = 29; 
    int numDays = daysInMonth[mon];

    int firstWday = (wday - ((day - 1) % 7) + 7) % 7; 
    
    bool isMondayFirst = (config.calendar_start_day == "mon");
    int startCol = isMondayFirst ? ((firstWday + 6) % 7) : firstWday;

    int totalCells = startCol + numDays;
    int numLines = (totalCells + 6) / 7;

    display.setFont(&Picopixel);
    
    const char* headersMon[] = {"L", "M", "M", "J", "V", "S", "D"};
    const char* headersSun[] = {"D", "L", "M", "M", "J", "V", "S"};
    const char** headers = isMondayFirst ? headersMon : headersSun;
    
    int colCenters[] = {62, 72, 82, 92, 102, 112, 122}; 
    
    for (int i = 0; i < 7; i++) {
        display.getTextBounds(headers[i], 0, 0, &x1, &y1, &w, &h);
        display.setCursor(colCenters[i] - (w / 2), 6);
        display.print(headers[i]);
    }
    
    int lineY = (numLines == 6) ? 8 : 9; 
    display.drawLine(59, lineY, 126, lineY, SH110X_WHITE); 

    int rowY = (numLines == 6) ? 16 : 18; 
    int rowSpacing = (numLines == 6) ? 9 : 10;

    int currCol = startCol;

    for (int d = 1; d <= numDays; d++) {
        char dateBuf[16];
        snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", year, mon + 1, d);
        String dateStr = String(dateBuf);
        
        bool isHoliday = false;
        if (config.calendar_show_holidays) {
            for (int i = 0; i < calendar.count; i++) {
                if (calendar.items[i].date == dateStr) {
                    isHoliday = true;
                    break;
                }
            }
        }

        int xCenter = colCenters[currCol];

        if (isHoliday) {
            display.fillRect(xCenter - 4, rowY - 5, 9, 7, SH110X_WHITE);
            display.setTextColor(SH110X_BLACK); 
        } else {
            display.setTextColor(SH110X_WHITE);
        }

        String dStr = String(d);
        display.getTextBounds(dStr.c_str(), 0, 0, &x1, &y1, &w, &h);
        display.setCursor(xCenter - (w / 2), rowY);
        display.print(dStr);

        display.setTextColor(SH110X_WHITE); 

        if (d == day) {
            display.drawRect(xCenter - 5, rowY - 6, 11, 9, SH110X_WHITE);
        }

        currCol++;
        if (currCol > 6) {
            currCol = 0;
            rowY += rowSpacing;
        }
    }
}

void DisplayService::drawWeatherScreen(const Config& config, const WeatherData& data, const String& currentTime) {
    display.clearDisplay();

    display.setTextColor(SH110X_WHITE);
    display.setTextWrap(false);
    display.setTextSize(1);
    display.setFont();

    int16_t x1, y1;
    uint16_t w, h;

    bool valid = !isnan(data.temp) && data.weather_code != -1;

    bool showHeader = config.weather_show_header;

    String weatherKeys[6];
    int weatherKeyCount = 0;
    for (int i = 0; i < 6; i++) {
        if (config.weather_values[i].length() > 0) weatherKeys[weatherKeyCount++] = config.weather_values[i];
    }

    auto weatherIconFor = [&](const String& key) -> const unsigned char* {
        if (key == "feels") return icon_feel;
        if (key == "humidity") return icon_drop;
        if (key == "wind") return icon_wind;
        if (key == "precipitation") return icon_precipitation;
        if (key == "pressure") return icon_pressure;
        if (key == "visibility") return icon_visibility;
        return icon_drop;
    };

    auto weatherTextFor = [&](const String& key) -> String {
        if (!valid) return "--";
        if (key == "feels") return config.round_temps ? String((int)round(data.apparent_temperature)) : String(data.apparent_temperature, 1);
        if (key == "humidity") return String(data.humidity) + "%";
        if (key == "wind") return String((int)round(data.wind_speed)) + "km";
        if (key == "precipitation") return isnan(data.precipitation_probability) ? "--" : String((int)round(data.precipitation_probability)) + "%";
        if (key == "pressure") return isnan(data.pressure) ? "--" : String((int)round(data.pressure)) + "hPa";
        if (key == "visibility") return isnan(data.visibility) ? "--" : String(data.visibility / 1000.0, 1) + "km";
        return "--";
    };

    auto drawWeatherRow = [&](int count, int y) {
        for (int i = 0; i < count; i++) {
            const String& key = weatherKeys[i];
            int xIcon = 5 + i * 43;
            display.drawBitmap(xIcon, y, weatherIconFor(key), 8, 8, SH110X_WHITE);
            String val = weatherTextFor(key);
            int xVal = xIcon + 10;
            display.setCursor(xVal, y + 1);
            display.print(val);
            if (key == "feels") {
                display.getTextBounds(val.c_str(), 0, 0, &x1, &y1, &w, &h);
                display.drawBitmap(xVal + w, y + 1, degree_icon_small, 4, 4, SH110X_WHITE);
            }
        }
    };

    auto drawWeatherList = [&]() {
        int margin = 3;
        int rowH = 8;
        int xIcon = display.width() - margin - rowH;
        float gap = (float)(display.height() - 2 * margin - weatherKeyCount * rowH) / (weatherKeyCount + 1);
        for (int i = 0; i < weatherKeyCount; i++) {
            int y = (int)round(margin + gap * (i + 1) + rowH * i);
            const String& key = weatherKeys[i];
            String val = weatherTextFor(key);
            display.getTextBounds(val.c_str(), 0, 0, &x1, &y1, &w, &h);
            bool hasDegree = (key == "feels");
            int contentW = (int)w + (hasDegree ? 4 : 0);
            int xText = xIcon - 5 - contentW;
            display.setCursor(xText, y + 1);
            display.print(val);
            if (hasDegree) {
                display.drawBitmap(xText + w, y + 1, degree_icon_small, 4, 4, SH110X_WHITE);
            }
            display.drawBitmap(xIcon, y, weatherIconFor(key), 8, 8, SH110X_WHITE);
        }
    };

    if (showHeader) {
        // 1. Header (City and Time)
        display.setTextSize(1);
        String cityStr = valid ? oledAscii(config.city) : "Sin ubicacion";

        int yHeader = 2;

        display.getTextBounds(currentTime.c_str(), 0, 0, &x1, &y1, &w, &h);
        int xTime = display.width() - w - 2;

        int cityMaxWidth = xTime - 2 - 3;
        display.getTextBounds(cityStr.c_str(), 0, 0, &x1, &y1, &w, &h);
        bool cityTrimmed = false;
        if ((int)w > cityMaxWidth) {
            for (int i = cityStr.length(); i > 0; i--) {
                String trunc = cityStr.substring(0, i);
                display.getTextBounds(trunc.c_str(), 0, 0, &x1, &y1, &w, &h);
                if ((int)w + 8 <= cityMaxWidth) {
                    cityStr = trunc;
                    cityTrimmed = true;
                    break;
                }
            }
        }
        display.setCursor(2, yHeader);
        display.print(cityStr);
        if (cityTrimmed) {
            display.drawBitmap(2 + w, yHeader, icon_dots, 8, 8, SH110X_WHITE);
        }

        display.setCursor(xTime, yHeader);
        display.print(currentTime);

        int ySeparator = 14;
        display.drawFastHLine(0, ySeparator, display.width(), SH110X_WHITE);

        // 2. Main Temperature and Icon
        int yMiddleStart = 18;
        int middleHeight = 35;

        display.setTextSize(3);

        String tempValueStr = valid ? (config.round_temps ? String((int)round(data.temp)) : String(data.temp, 1)) : "--";

        display.getTextBounds(tempValueStr.c_str(), 0, 0, &x1, &y1, &w, &h);
        int yTemp = yMiddleStart + ((middleHeight - h) / 2);
        int xStartTemp = 5;

        display.setCursor(xStartTemp, yTemp);
        display.print(tempValueStr);

        int xDegree = xStartTemp + w + 2;
        display.drawBitmap(xDegree, yTemp, degree_icon, 12, 12, SH110X_WHITE);

        int iconSize = 24;
        int rightMargin = 5;
        int xRightEdge = display.width() - rightMargin;

        int xIcon = xRightEdge - iconSize;
        int yIcon = yMiddleStart + 1;

        const unsigned char* iconBitmap = valid ? getWeatherBitmap(data.weather_code, data.is_day) : icon_cloud;
        display.drawBitmap(xIcon, yIcon, iconBitmap, iconSize, iconSize, SH110X_WHITE);

        // 3. Weather Description
        display.setTextSize(1);
        String desc = valid ? WeatherService::getWeatherDescription(data.weather_code) : "Sin datos";
        display.getTextBounds(desc.c_str(), 0, 0, &x1, &y1, &w, &h);

        int xDesc = xRightEdge - w;
        int yDesc = yIcon + iconSize + 1;
        display.setCursor(xDesc, yDesc);
        display.print(desc);

        // 4. Footer Stats
        int yFooter = 56;
        int count = weatherKeyCount > 3 ? 3 : weatherKeyCount;
        drawWeatherRow(count, yFooter);
    } else {
        // 1. Main Temperature and Icon
        int numberX = 3;
        int topY = 3;

        display.setTextSize(3);
        String tempValueStr = valid ? (config.round_temps ? String((int)round(data.temp)) : String(data.temp, 1)) : "--";
        display.getTextBounds(tempValueStr.c_str(), 0, 0, &x1, &y1, &w, &h);
        display.setCursor(numberX, topY);
        display.print(tempValueStr);

        int degreeSize = 12;
        display.drawBitmap(numberX + (int)w + 2, topY, degree_icon, degreeSize, degreeSize, SH110X_WHITE);

        int numberW = (int)w;
        int iconSize = 24;
        int iconY = topY + h + 5;
        int iconX = numberX + (numberW - iconSize) / 2;
        const unsigned char* iconBitmap = valid ? getWeatherBitmap(data.weather_code, data.is_day) : icon_cloud;
        display.drawBitmap(iconX, iconY, iconBitmap, iconSize, iconSize, SH110X_WHITE);

        // 2. Weather Description
        display.setTextSize(1);
        display.setFont(&Picopixel);
        String desc = valid ? WeatherService::getWeatherDescription(data.weather_code) : "Sin datos";
        desc.toUpperCase();
        display.getTextBounds(desc.c_str(), 0, 0, &x1, &y1, &w, &h);
        display.setCursor(numberX + (numberW - (int)w) / 2, iconY + iconSize + 2 - y1);
        display.print(desc);
        display.setFont();

        // 3. Selected values
        drawWeatherList();
    }
}

void DisplayService::drawAQIScreen(const Config& config, const AirQualityData& data, const String& currentTime) {
    display.clearDisplay();

    display.setTextColor(SH110X_WHITE);
    display.setTextWrap(false);
    display.setTextSize(1);
    display.setFont(); 
    
    int16_t x1, y1;
    uint16_t w, h;
    
    bool valid = (data.aqi != -1);

    bool showHeader = config.aqi_show_header;

    String aqiKeys[6];
    int aqiKeyCount = 0;
    for (int i = 0; i < 6; i++) {
        if (config.aqi_values[i].length() > 0) aqiKeys[aqiKeyCount++] = config.aqi_values[i];
    }

    auto aqiIconFor = [&](const String& key) -> const unsigned char* {
        if (key == "pm25") return icon_small_particles;
        if (key == "pm10") return icon_big_particles;
        if (key == "no2") return icon_no2;
        if (key == "co") return icon_co;
        if (key == "co2") return icon_co2;
        if (key == "so2") return icon_so2;
        if (key == "o3") return icon_o3;
        if (key == "dust") return icon_dust;
        if (key == "uv") return icon_uv;
        if (key == "ch4") return icon_ch4;
        return icon_small_particles;
    };

    auto aqiHasUgUnit = [&](const String& key) -> bool {
        return key == "pm25" || key == "pm10" || key == "no2" || key == "co" || key == "so2" || key == "o3" || key == "dust";
    };

    auto aqiValueFor = [&](const String& key) -> float {
        if (key == "pm25") return data.pm25;
        if (key == "pm10") return data.pm10;
        if (key == "no2") return data.no2;
        if (key == "co") return data.co;
        if (key == "co2") return data.co2;
        if (key == "so2") return data.so2;
        if (key == "o3") return data.o3;
        if (key == "dust") return data.dust;
        if (key == "uv") return data.uv;
        if (key == "ch4") return data.ch4;
        return NAN;
    };

    auto drawAqiHeaderRow = [&](int position, const String& key, int y) {
        float rawVal = aqiValueFor(key);
        String val = (!valid || isnan(rawVal)) ? "--" : String((int)round(rawVal));
        bool hasUnit = aqiHasUgUnit(key);
        display.getTextBounds(val.c_str(), 0, 0, &x1, &y1, &w, &h);
        int totalWidth = 8 + 2 + w + (hasUnit ? 1 + 8 : 0);
        int xIcon;
        if (position == 0) xIcon = 2;
        else if (position == 1) xIcon = (display.width() / 2) - (totalWidth / 2);
        else xIcon = display.width() - totalWidth - 2;
        display.drawBitmap(xIcon, y, aqiIconFor(key), 8, 8, SH110X_WHITE);
        int xText = xIcon + 8 + 2;
        display.setCursor(xText, y + 1);
        display.print(val);
        if (hasUnit) {
            display.drawBitmap(xText + w + 1, y + 1, icon_ug, 8, 8, SH110X_WHITE);
        }
    };

    auto drawAqiList = [&]() {
        int margin = 3;
        int rowH = 8;
        int xIcon = display.width() - margin - rowH;
        float gap = (float)(display.height() - 2 * margin - aqiKeyCount * rowH) / (aqiKeyCount + 1);
        for (int i = 0; i < aqiKeyCount; i++) {
            int y = (int)round(margin + gap * (i + 1) + rowH * i);
            const String& key = aqiKeys[i];
            float rawVal = aqiValueFor(key);
            String val = (!valid || isnan(rawVal)) ? "--" : String((int)round(rawVal));
            bool hasUnit = aqiHasUgUnit(key);
            display.getTextBounds(val.c_str(), 0, 0, &x1, &y1, &w, &h);
            int xUnit = xIcon - 5 - 8;
            int textRightEdge = (hasUnit ? xUnit : xIcon) - 5;
            int xText = textRightEdge - w;
            display.setCursor(xText, y + 1);
            display.print(val);
            if (hasUnit) {
                display.drawBitmap(xUnit, y + 1, icon_ug, 8, 8, SH110X_WHITE);
            }
            display.drawBitmap(xIcon, y, aqiIconFor(key), 8, 8, SH110X_WHITE);
        }
    };

    if (showHeader) {
        // 1. Header
        display.setTextSize(1);
        String cityStr = valid ? oledAscii(config.city) : "Sin ubicacion";

        int yHeader = 2;

        display.getTextBounds(currentTime.c_str(), 0, 0, &x1, &y1, &w, &h);
        int xTime = display.width() - w - 2;

        int cityMaxWidth = xTime - 2 - 3;
        display.getTextBounds(cityStr.c_str(), 0, 0, &x1, &y1, &w, &h);
        bool cityTrimmed = false;
        if ((int)w > cityMaxWidth) {
            for (int i = cityStr.length(); i > 0; i--) {
                String trunc = cityStr.substring(0, i);
                display.getTextBounds(trunc.c_str(), 0, 0, &x1, &y1, &w, &h);
                if ((int)w + 8 <= cityMaxWidth) {
                    cityStr = trunc;
                    cityTrimmed = true;
                    break;
                }
            }
        }
        display.setCursor(2, yHeader);
        display.print(cityStr);
        if (cityTrimmed) {
            display.drawBitmap(2 + w, yHeader, icon_dots, 8, 8, SH110X_WHITE);
        }

        display.setCursor(xTime, yHeader);
        display.print(currentTime);

        int ySeparator = 14;
        display.drawFastHLine(0, ySeparator, display.width(), SH110X_WHITE);

        // 2. Main AQI Value and Status Icon
        int yMiddleStart = 18;
        int middleHeight = 35;

        display.setTextSize(3);
        String aqiStr = valid ? String(data.aqi) : "--";

        display.getTextBounds(aqiStr.c_str(), 0, 0, &x1, &y1, &w, &h);
        int yAqi = yMiddleStart + ((middleHeight - h) / 2);
        int xStartAqi = 5;

        display.setCursor(xStartAqi, yAqi);
        display.print(aqiStr);

        display.setTextSize(1);
        int xLabel = xStartAqi + w + 4;

        display.setCursor(xLabel, yAqi);
        display.print(config.aqi_type);

        display.setCursor(xLabel, yAqi + 9);
        display.print("ICA");

        int iconSize = 24;
        int rightMargin = 5;
        int xRightEdge = display.width() - rightMargin;
        int xIcon = xRightEdge - iconSize;
        int yIcon = yMiddleStart + 1;

        const unsigned char* aqiIcon = valid ? getAQIBitmap(data.aqi, config.aqi_type == "EU") : icon_neutral;
        display.drawBitmap(xIcon, yIcon, aqiIcon, iconSize, iconSize, SH110X_WHITE);

        // 3. Status Description
        display.setTextSize(1);
        String desc = valid ? data.status : "Sin datos";
        display.getTextBounds(desc.c_str(), 0, 0, &x1, &y1, &w, &h);

        int xDesc = xRightEdge - w;
        int yDesc = yIcon + iconSize + 1;
        display.setCursor(xDesc, yDesc);
        display.print(desc);

        // 4. Footer Stats
        int yFooter = 56;
        int count = aqiKeyCount > 3 ? 3 : aqiKeyCount;
        for (int i = 0; i < count; i++) drawAqiHeaderRow(i, aqiKeys[i], yFooter);
    } else {
        // 1. Main AQI Value and Type Label
        int numberX = 3;
        int topY = 3;

        String aqiStr = valid ? String(data.aqi) : "--";

        display.setTextSize(3);
        display.getTextBounds(aqiStr.c_str(), 0, 0, &x1, &y1, &w, &h);
        display.setCursor(numberX, topY);
        display.print(aqiStr);

        display.setTextSize(1);
        int xLabel = numberX + (int)w + 4;
        display.setCursor(xLabel, topY);
        display.print(config.aqi_type);
        display.setCursor(xLabel, topY + 9);
        display.print("ICA");

        int numberW = (int)w;
        int iconSize = 24;
        int iconY = topY + h + 5;
        int iconX = numberX + (numberW - iconSize) / 2;
        const unsigned char* aqiIcon = valid ? getAQIBitmap(data.aqi, config.aqi_type == "EU") : icon_neutral;
        display.drawBitmap(iconX, iconY, aqiIcon, iconSize, iconSize, SH110X_WHITE);

        // 2. Status Description
        display.setTextSize(1);
        display.setFont(&Picopixel);
        String desc = valid ? data.status : "Sin datos";
        desc.toUpperCase();
        display.getTextBounds(desc.c_str(), 0, 0, &x1, &y1, &w, &h);
        display.setCursor(numberX + (numberW - (int)w) / 2, iconY + iconSize + 2 - y1);
        display.print(desc);
        display.setFont();

        // 3. Selected values
        drawAqiList();
    }
}

void DisplayService::drawDaylightScreen(const Config& config, const DaylightData& data) {
    if (data.sunrise_mins == -1) {
        drawInfoScreen(icon_error, "Sin datos de sol"); 
        return; 
    }

    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextWrap(false);

    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    int currentMins = timeinfo.tm_hour * 60 + timeinfo.tm_min;

    // 1. Calculate Day/Night state and progress
    bool isDay = (currentMins >= data.sunrise_mins && currentMins < data.sunset_mins);
    int progress = 0;
    int mins_left = 0;

    if (isDay) {
        int total_daylight = data.sunset_mins - data.sunrise_mins;
        int daylight_passed = currentMins - data.sunrise_mins;
        if (total_daylight > 0) progress = (daylight_passed * 100) / total_daylight;
        mins_left = data.sunset_mins - currentMins;
    } else {
        int total_night = (1440 - data.sunset_mins) + data.sunrise_mins;
        int night_passed = (currentMins >= data.sunset_mins) ? 
                           (currentMins - data.sunset_mins) : 
                           ((1440 - data.sunset_mins) + currentMins);

        if (total_night > 0) progress = (night_passed * 100) / total_night;
        mins_left = total_night - night_passed;
    }

    // 2. Assign Times and Icons
    int leftMins = isDay ? data.sunrise_mins : data.sunset_mins;
    int rightMins = isDay ? data.sunset_mins : data.sunrise_mins;
    int centerMins = isDay ? data.noon_mins : (data.noon_mins + 720) % 1440; 
    int displayLengthMins = isDay ? data.length_mins : (1440 - data.length_mins); 
    
    const unsigned char* leftIcon = isDay ? icon_sun_rise : icon_sun_set;
    const unsigned char* rightIcon = isDay ? icon_sun_set : icon_sun_rise;
    const unsigned char* centerIcon = isDay ? icon_sun : icon_moon;

    // 3. Layout Positioning 
    int iconY = config.daylight_minimal ? 14 : 2; 
    int textY = iconY + 24 + 4;

    int16_t x1, y1; uint16_t w, h;

    // 4. Main Icons
    display.drawBitmap(19 - 12, iconY, leftIcon, 24, 24, 1);
    display.drawBitmap(64 - 12, iconY, centerIcon, 24, 24, 1);
    display.drawBitmap(110 - 12, iconY, rightIcon, 24, 24, 1);

    // 5. Main Times
    display.setFont(); 
    display.setTextSize(1);
    
    String leftStr = TimeService::formatMinsFromMidnight(leftMins, config.time_format, false);
    display.getTextBounds(leftStr.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor(19 - (w / 2), textY);
    display.print(leftStr);

    String centerStr = TimeService::formatMinsFromMidnight(centerMins, config.time_format, false);
    display.getTextBounds(centerStr.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor(64 - (w / 2), textY);
    display.print(centerStr);

    String rightStr = TimeService::formatMinsFromMidnight(rightMins, config.time_format, false);
    display.getTextBounds(rightStr.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor(110 - (w / 2), textY);
    display.print(rightStr);

    if (config.daylight_minimal) {
        return;
    }

    // 6. Progress Bar
    display.drawRect(2, 45, 124, 6, 1);
    int fillW = (int)((constrain(progress, 0, 100) / 100.0) * 120);
    if (fillW > 0) display.fillRect(4, 47, fillW, 2, 1);

    // 7. Progress Percentage
    String progStr = String(progress) + "%";
    display.getTextBounds(progStr.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor(64 - (w / 2), 55);
    display.print(progStr);

    // 8. Secondary Bottom Text
    String lengthStr = TimeService::formatDurationMins(displayLengthMins);
    lengthStr.replace(" ", "");
    display.setCursor(2, 55);
    display.print(lengthStr);

    String timeLeftStr = String(mins_left / 60) + "h" + String(mins_left % 60) + "m";
    display.getTextBounds(timeLeftStr.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor(126 - w, 55);
    display.print(timeLeftStr);
}

void DisplayService::drawMoonScreen(const Config& config, const MoonData& data) {
    if (data.fracillum == -1) {
        drawInfoScreen(icon_error, "Sin datos de luna");
        return;
    }

    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextWrap(false);
    display.setTextSize(1);
    display.setFont();

    int16_t x1, y1; uint16_t w, h;
    bool isMinimal = config.moon_minimal;

    // 1. Phase Name
    String phaseStr = data.curphase;
    if (phaseStr.equalsIgnoreCase("New Moon")) phaseStr = "Luna nueva";
    else if (phaseStr.equalsIgnoreCase("Waxing Crescent")) phaseStr = "Creciente";
    else if (phaseStr.equalsIgnoreCase("First Quarter")) phaseStr = "Cuarto creciente";
    else if (phaseStr.equalsIgnoreCase("Waxing Gibbous")) phaseStr = "Gibosa creciente";
    else if (phaseStr.equalsIgnoreCase("Full Moon")) phaseStr = "Luna llena";
    else if (phaseStr.equalsIgnoreCase("Waning Gibbous")) phaseStr = "Gibosa menguante";
    else if (phaseStr.equalsIgnoreCase("Last Quarter") || phaseStr.equalsIgnoreCase("Third Quarter")) phaseStr = "Cuarto menguante";
    else if (phaseStr.equalsIgnoreCase("Waning Crescent")) phaseStr = "Menguante";
    phaseStr.toUpperCase();
    display.getTextBounds(phaseStr.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor((128 - w) / 2, 55);
    display.print(phaseStr);

    int cx = 64;
    int cy = 31;
    int r = 17;

    if (!isMinimal) {
        // 2. Illumination
        String illumStr = String(data.fracillum) + "%";
        display.getTextBounds(illumStr.c_str(), 0, 0, &x1, &y1, &w, &h);
        display.setCursor((128 - w) / 2, 2);
        display.print(illumStr);

        // 3. Moonrise
        String riseStr = (data.rise_mins != -1) ? TimeService::formatMinsFromMidnight(data.rise_mins, config.time_format) : "--:--";
        display.setCursor(6, 29);
        display.print(riseStr);
        display.drawBitmap(17, 21, icon_up, 7, 4, 1);

        // 4. Moonset
        String setStr = (data.set_mins != -1) ? TimeService::formatMinsFromMidnight(data.set_mins, config.time_format) : "--:--";
        display.setCursor(92, 29);
        display.print(setStr);
        display.drawBitmap(103, 21, icon_down, 7, 4, 1);
    } else {
        cy = 27; 
        r = 22;  
    }

    // 5. Moon
    float f = data.fracillum / 100.0;

    bool isWaxing = (data.curphase.indexOf("Wax") >= 0 || data.curphase.indexOf("First") >= 0 || data.curphase.indexOf("New") >= 0);
    
    display.fillCircle(cx, cy, r, SH110X_WHITE);

    if (isWaxing) {
        display.fillRect(cx - r, cy - r, r, 2 * r + 1, SH110X_BLACK);
    } else {
        display.fillRect(cx, cy - r, r + 1, 2 * r + 1, SH110X_BLACK);
    }

    int ew = round(r * fabsf(1.0f - 2.0f * f));

    int ellipseColor = (f <= 0.5) ? SH110X_BLACK : SH110X_WHITE;

    for (int y = -r; y <= r; y++) {
        int dx = (ew * sqrt(r * r - y * y)) / r;
        display.drawFastHLine(cx - dx, cy + y, dx * 2 + 1, ellipseColor);
    }

    display.drawCircle(cx, cy, r, SH110X_WHITE);
}

void DisplayService::drawIssScreen(const Config& config, const IssData& data) {
    static const int maxTrackPoints = 256;
    static uint8_t trackX[maxTrackPoints];
    static uint8_t trackY[maxTrackPoints];
    static int trackCount = 0;
    static long trackStartTs = 0;
    static long trackEndTs = 0;

    if (!IssService::isValid(data)) {
        trackCount = 0;
        drawInfoScreen(icon_error, "Sin datos de la EEI");
        return;
    }

    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextWrap(false);
    display.setTextSize(1);
    display.setFont(&Picopixel);

    int16_t x1, y1; uint16_t w, h;

    float issLat, issLon;
    IssService::getCurrentPosition(data, issLat, issLon);
    String latStr = String(issLat, 2);
    String lonStr = String(issLon, 2);

    String location = oledAscii(IssService::getLocationName(data));
    location.toUpperCase();

    bool isMiles = (data.units == "miles");
    String labels[3] = {
        isMiles ? "ALT MI" : "ALT KM",
        isMiles ? "VEL MPH" : "VEL KM/H",
        isMiles ? "DIST MI" : "DIST KM"
    };
    String values[3] = {
        String((int)round(data.altitude)),
        String((int)round(data.velocity)),
        String((int)round(IssService::distanceFrom(data, config.latitude, config.longitude)))
    };

    auto trimToWidth = [&](String& text, int maxW) -> bool {
        display.getTextBounds(text.c_str(), 0, 0, &x1, &y1, &w, &h);
        if ((int)w <= maxW) return false;
        while (text.length() > 0) {
            text = text.substring(0, text.length() - 1);
            text.trim();
            display.getTextBounds(text.c_str(), 0, 0, &x1, &y1, &w, &h);
            if ((int)w + 8 <= maxW) break;
        }
        return true;
    };

    auto printCentered = [&](const String& text, int centerX, int y) {
        display.getTextBounds(text.c_str(), 0, 0, &x1, &y1, &w, &h);
        display.setCursor(centerX - (int)w / 2 - x1, y);
        display.print(text);
    };

    if (config.iss_mode == "text") {
        const int columnX[3] = {26, 64, 102};

        // 1. Country/ocean headline
        const int nameY = 6;
        display.setFont();
        String name = location.length() > 0 ? location : "ESTACION ESPACIAL";
        bool nameTrimmed = trimToWidth(name, 124);
        display.getTextBounds(name.c_str(), 0, 0, &x1, &y1, &w, &h);
        int nameX = (128 - ((int)w + (nameTrimmed ? 8 : 0))) / 2;
        display.setCursor(nameX, nameY);
        display.print(name);
        if (nameTrimmed) display.drawBitmap(nameX + w, nameY, icon_dots, 8, 8, SH110X_WHITE);

        // 2. Coordinates
        printCentered(latStr, columnX[0], 24);
        printCentered(lonStr, columnX[2], 24);
        display.setFont(&Picopixel);
        printCentered("LAT", columnX[0], 38);
        printCentered("LON", columnX[2], 38);
        display.drawBitmap(columnX[1] - 8, 23, icon_iss, 16, 16, SH110X_WHITE);

        // 3. Altitude, speed, distance
        for (int i = 0; i < 3; i++) {
            display.setFont();
            printCentered(values[i], columnX[i], 47);
            display.setFont(&Picopixel);
            printCentered(labels[i], columnX[i], 61);
        }
        return;
    }

    const int mapX = 1;
    const int mapY = 11;
    const int mapW = 84;
    const int mapH = 42;
    const int headerBaseline = 6;
    const int coordsBaseline = 61;
    const int statsX = 92;

    auto toMapX = [&](float lon) { return mapX + constrain((int)((lon + 180.0f) / 360.0f * mapW), 0, mapW - 1); };
    auto toMapY = [&](float lat) { return mapY + constrain((int)((90.0f - lat) / 180.0f * mapH), 0, mapH - 1); };

    // 1. Country/ocean header
    String header = location.length() > 0 ? location : "ESTACION ESPACIAL";
    const int headerMaxW = mapW + 2;
    bool headerTrimmed = trimToWidth(header, headerMaxW);
    display.getTextBounds(header.c_str(), 0, 0, &x1, &y1, &w, &h);
    int headerW = w + (headerTrimmed ? 8 : 0);
    int headerX = mapX - 1 + (headerMaxW - headerW) / 2;
    display.setCursor(headerX - x1, headerBaseline);
    display.print(header);
    if (headerTrimmed) display.drawBitmap(headerX + w, headerBaseline - 6, icon_dots, 8, 8, SH110X_WHITE);

    // 2. World map
    display.drawRect(mapX - 1, mapY - 1, mapW + 2, mapH + 2, SH110X_WHITE);
    display.drawBitmap(mapX, mapY, img_world_map, mapW, mapH, SH110X_WHITE);

    // 3. Ground track (current pass)
    float elapsedMin = min((millis() - data.fetched_at_ms) / 60000.0f, 15.0f);
    long nowTs = data.timestamp + (long)(elapsedMin * 60.0f);
    if (trackCount == 0 || nowTs < trackStartTs || nowTs >= trackEndTs) {
        trackCount = 0;
        float startMin, endMin;
        if (IssService::getPassWindow(data, elapsedMin, startMin, endMin)) {
            float trackLat, trackLon;
            for (float t = startMin; t <= endMin && trackCount < maxTrackPoints; t += IssService::PASS_STEP_MIN) {
                IssService::predictPosition(data, t, trackLat, trackLon);
                uint8_t px = toMapX(trackLon);
                uint8_t py = toMapY(trackLat);
                if (trackCount > 0 && trackX[trackCount - 1] == px && trackY[trackCount - 1] == py) continue;
                trackX[trackCount] = px;
                trackY[trackCount] = py;
                trackCount++;
            }
            trackStartTs = data.timestamp + (long)(startMin * 60.0f);
            trackEndTs = data.timestamp + (long)((endMin + IssService::PASS_STEP_MIN) * 60.0f);
        }
    }
    for (int i = 0; i < trackCount; i++) {
        display.drawPixel(trackX[i], trackY[i], SH110X_INVERSE);
    }

    // 4. ISS marker
    int ix = toMapX(issLon);
    int iy = toMapY(issLat);
    display.fillCircle(ix, iy, 4, SH110X_BLACK);
    display.drawCircle(ix, iy, 3, SH110X_WHITE);
    if (data.is_daylight) display.fillCircle(ix, iy, 1, SH110X_WHITE);

    // 5. Coordinates
    const int coordsGap = 6;
    display.getTextBounds(latStr.c_str(), 0, 0, &x1, &y1, &w, &h);
    int latW = w;
    display.getTextBounds(lonStr.c_str(), 0, 0, &x1, &y1, &w, &h);
    int lonW = w;
    int coordsX = mapX + (mapW - (latW + coordsGap + lonW)) / 2;
    display.setCursor(coordsX, coordsBaseline);
    display.print(latStr);
    display.setCursor(coordsX + latW + coordsGap, coordsBaseline);
    display.print(lonStr);

    // 6. Stats column: altitude, speed, distance
    for (int i = 0; i < 3; i++) {
        int rowY = 3 + i * 21;

        display.setFont(&Picopixel);
        display.setCursor(statsX, rowY + 5);
        display.print(labels[i]);

        display.setFont();
        display.setCursor(statsX, rowY + 8);
        display.print(values[i]);
    }
}

void DisplayService::drawPopulationScreen(const Config& config, const PopulationData& data) {
    if (data.world_pop_base == -1 && data.country_pop_base == -1) {
        drawInfoScreen(icon_error, "Sin datos");
        return;
    }

    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextWrap(false);
    display.setTextSize(1);
    display.setFont();
    
    int16_t x1, y1; uint16_t w, h;
    bool showW = config.pop_show_world && data.world_year > 0;
    bool showC = config.pop_show_country && data.country_year > 0;

    auto formatPop = [](long long num) -> String {
        String str = String(num);
        int p = str.length() - 3;
        while (p > 0) { str = str.substring(0, p) + "," + str.substring(p); p -= 3; }
        return str;
    };
    
    if (showW && showC) {
        long long liveW = PopulationService::getLivePopulation(data.world_pop_base, data.world_growth, data.world_year);
        long long liveC = PopulationService::getLivePopulation(data.country_pop_base, data.country_growth, data.country_year);
                
        // 1. World Icon & Label
        display.drawBitmap(8, 4, icon_world, 16, 16, 1);
        display.getTextBounds("Mundo", 0, 0, &x1, &y1, &w, &h);
        display.setCursor(16 - (w / 2), 21); 
        display.print("Mundo");

        // 2. Country Icon & Label
        String cCode = config.country_code; 
        cCode.toUpperCase();
        if(cCode.length() > 2) cCode = cCode.substring(0, 2);
        
        display.drawBitmap(9, 36, icon_location, 13, 16, 1);
        display.getTextBounds(cCode.c_str(), 0, 0, &x1, &y1, &w, &h);
        display.setCursor(16 - (w / 2), 53); 
        display.print(cCode);
        
        // 3. World Population
        String popWStr = formatPop(liveW);
        display.getTextBounds(popWStr.c_str(), 0, 0, &x1, &y1, &w, &h);
        
        int w_world_pop = w;
        int world_pop_x = 126 - w_world_pop;          
        int world_icon_x = world_pop_x - 12;         
        int center_ref = world_icon_x + (12 + w_world_pop) / 2; 

        display.drawBitmap(world_icon_x, 6, icon_people, 8, 8, 1);
        display.setCursor(world_pop_x, 7); 
        display.print(popWStr);

        // 4. World Growth 
        String grWStr = (data.world_growth > 0 ? "+" : "") + String(data.world_growth, 2) + "%";
        display.getTextBounds(grWStr.c_str(), 0, 0, &x1, &y1, &w, &h);
        int gr_w_icon_x = center_ref - (12 + w) / 2;
        display.drawBitmap(gr_w_icon_x, 18, (data.world_growth >= 0 ? icon_up_arrow : icon_down_arrow), 8, 8, 1);
        display.setCursor(gr_w_icon_x + 12, 19); 
        display.print(grWStr);

        // 5. Country Population
        String popCStr = formatPop(liveC);
        display.getTextBounds(popCStr.c_str(), 0, 0, &x1, &y1, &w, &h);
        int ctr_pop_icon_x = center_ref - (12 + w) / 2;
        display.drawBitmap(ctr_pop_icon_x, 38, icon_people, 8, 8, 1);
        display.setCursor(ctr_pop_icon_x + 12, 39); 
        display.print(popCStr);

        // 6. Country Growth
        String grCStr = (data.country_growth > 0 ? "+" : "") + String(data.country_growth, 2) + "%";
        display.getTextBounds(grCStr.c_str(), 0, 0, &x1, &y1, &w, &h);
        int gr_c_icon_x = center_ref - (12 + w) / 2;
        display.drawBitmap(gr_c_icon_x, 50, (data.country_growth >= 0 ? icon_up_arrow : icon_down_arrow), 8, 8, 1);
        display.setCursor(gr_c_icon_x + 12, 51); 
        display.print(grCStr);

    } else if (showW || showC) {
        long long base = showW ? data.world_pop_base : data.country_pop_base;
        double growth = showW ? data.world_growth : data.country_growth;
        int year = showW ? data.world_year : data.country_year;
        
        long long liveP = PopulationService::getLivePopulation(base, growth, year);
        String title = showW ? String("Mundo") : oledAscii(config.country); 

        // 1. Title Setup & Truncation
        display.getTextBounds(title.c_str(), 0, 0, &x1, &y1, &w, &h);
        bool titleTrimmed = false;
        if (w > 128) {
            for (int i = title.length(); i > 0; i--) {
                String trunc = title.substring(0, i);
                display.getTextBounds(trunc.c_str(), 0, 0, &x1, &y1, &w, &h);
                if ((int)w + 8 <= 128) {
                    title = trunc;
                    titleTrimmed = true;
                    break;
                }
            }
        }

        // 2. Main Icon & Centered Title
        const unsigned char* mainIcon = showW ? icon_world : icon_location;
        int iconW = showW ? 16 : 13;

        display.drawBitmap((128 - iconW) / 2, 6, mainIcon, iconW, 16, 1);

        display.getTextBounds(title.c_str(), 0, 0, &x1, &y1, &w, &h);
        int titleX = (128 - (w + (titleTrimmed ? 8 : 0))) / 2;
        display.setCursor(titleX, 23);
        display.print(title);
        if (titleTrimmed) {
            display.drawBitmap(titleX + w, 23, icon_dots, 8, 8, SH110X_WHITE);
        }

        // 3. Population Count
        String popStr = formatPop(liveP);
        display.getTextBounds(popStr.c_str(), 0, 0, &x1, &y1, &w, &h);
        int popBlockW = 12 + w; 
        int popIconX = (128 - popBlockW) / 2;
        
        display.drawBitmap(popIconX, 36, icon_people, 8, 8, 1);
        display.setCursor(popIconX + 12, 37); 
        display.print(popStr);

        // 4. Growth Percentage
        String grStr = (growth > 0 ? "+" : "") + String(growth, 2) + "%";
        display.getTextBounds(grStr.c_str(), 0, 0, &x1, &y1, &w, &h);
        int grBlockW = 12 + w;
        int grIconX = (128 - grBlockW) / 2;
        
        display.drawBitmap(grIconX, 50, (growth >= 0 ? icon_up_arrow : icon_down_arrow), 8, 8, 1);
        display.setCursor(grIconX + 12, 51); 
        display.print(grStr);
    }
}

void DisplayService::drawCryptoScreen(const Config& config, const CryptoData& data) {
    if (!data.updated) {
        drawInfoScreen(icon_error, "Sin datos cripto");
        return;
    }

    display.clearDisplay();

    display.setTextColor(SH110X_WHITE);
    display.setTextWrap(false);
    display.setTextSize(1);
    display.setFont();

    int16_t x1, y1; uint16_t w, h;

    // 1. Symbol
    display.setTextSize(3);
    display.setCursor(4, 6);
    display.print(data.symbol);

    // 2. Full Name
    if (config.crypto_fn) {
        display.setTextSize(1);
        String displayName = data.name;
        displayName.toUpperCase();
        display.getTextBounds(displayName.c_str(), 0, 0, &x1, &y1, &w, &h);
        bool nameTrimmed = false;
        if ((int)w > 120) {
            for (int i = displayName.length(); i > 0; i--) {
                String trunc = displayName.substring(0, i);
                display.getTextBounds(trunc.c_str(), 0, 0, &x1, &y1, &w, &h);
                if ((int)w + 8 <= 120) {
                    displayName = trunc;
                    nameTrimmed = true;
                    break;
                }
            }
        }
        display.setCursor(4, 32);
        display.print(displayName);
        if (nameTrimmed) {
            display.drawBitmap(4 + w, 32, icon_dots, 8, 8, SH110X_WHITE);
        }
    }

    // 3. Price
    display.setTextSize(2);
    display.setCursor(4, 44);
    display.print("$" + String(data.price_usd));

    // 4. Arrow & Percentage
    bool isPositive = (data.percent_change_24h >= 0);
    const unsigned char* arrowIcon = isPositive ? icon_arrow_up : icon_arrow_down;
    display.drawBitmap(102, 3, arrowIcon, 15, 15, 1);

    display.setTextSize(1);
    display.setCursor(95, 22);
    String trendPrefix = isPositive ? "+" : ""; 
    display.print(trendPrefix + String(data.percent_change_24h, 1) + "%");
}

void DisplayService::drawCurrencyScreen(const Config& config, const CurrencyData& data, int multiplier) {
    if (!data.updated) {
        drawInfoScreen(icon_error, "Sin cotizaciones");
        return;
    }

    display.clearDisplay();

    display.setTextColor(SH110X_WHITE);
    display.setTextWrap(false);
    display.setTextSize(1);
    display.setFont();

    int16_t x1, y1; uint16_t w, h;

    // 1. Base Currency Symbol
    display.setTextSize(3);
    display.setCursor(4, 6);
    display.print(data.base);

    // 2. Full Currency Name
    if (config.currency_fn) {
        String fullName = "Desconocido";
        for (auto c : allCurrencies) {
            if (data.base.equalsIgnoreCase(c.code)) {
                fullName = c.name;
                break;
            }
        }
        display.setTextSize(1);
        fullName.toUpperCase();
        display.getTextBounds(fullName.c_str(), 0, 0, &x1, &y1, &w, &h);
        bool nameTrimmed = false;
        if ((int)w > 120) {
            for (int i = fullName.length(); i > 0; i--) {
                String trunc = fullName.substring(0, i);
                display.getTextBounds(trunc.c_str(), 0, 0, &x1, &y1, &w, &h);
                if ((int)w + 8 <= 120) {
                    fullName = trunc;
                    nameTrimmed = true;
                    break;
                }
            }
        }
        display.setCursor(4, 32);
        display.print(fullName);
        if (nameTrimmed) {
            display.drawBitmap(4 + w, 32, icon_dots, 8, 8, SH110X_WHITE);
        }
    }

    // 3. Calculate Rate & Decimals using the passed Multiplier
    float displayRate = data.rate * multiplier;
    int decimals = (displayRate < 10.0) ? 3 : (displayRate < 100.0) ? 2 : (displayRate < 1000.0) ? 1 : 0;

    // 4. Rate and Target Currency
    display.setTextSize(2);
    display.setCursor(4, 44);
    display.print(String(displayRate, decimals) + " " + data.target);

    // 5. Context helper & Equals sign
    display.setTextSize(1);
    String topText = String(multiplier) + " " + data.base;
    uint16_t wTop, hTop;
    display.getTextBounds(topText, 0, 0, &x1, &y1, &wTop, &hTop);
    int topTextX = 128 - wTop - 4;
    display.setCursor(topTextX, 8); 
    display.print(topText);

    String eqText = "=";
    uint16_t wEq, hEq;
    display.getTextBounds(eqText, 0, 0, &x1, &y1, &wEq, &hEq);
    int centerOfTopText = topTextX + (wTop / 2);
    display.setCursor(centerOfTopText - (wEq / 2), 19); 
    display.print(eqText);
}

void DisplayService::drawStockScreen(const Config& config, const StockData& data) {
    if (!data.updated) {
        drawInfoScreen(icon_error, "Sin datos");
        return;
    }

    display.clearDisplay();

    display.setTextColor(SH110X_WHITE);
    display.setTextWrap(false);
    display.setTextSize(1);
    display.setFont();

    int16_t x1, y1; uint16_t w, h;

    // 1. Symbol
    display.setTextSize(3);
    display.setCursor(4, 6);
    display.print(data.symbol);

    // 2. Company Name
    if (config.stock_fn) {
        display.setTextSize(1);
        String displayName = data.name;
        displayName.toUpperCase();
        display.getTextBounds(displayName.c_str(), 0, 0, &x1, &y1, &w, &h);
        bool nameTrimmed = false;
        if ((int)w > 120) {
            for (int i = displayName.length(); i > 0; i--) {
                String trunc = displayName.substring(0, i);
                display.getTextBounds(trunc.c_str(), 0, 0, &x1, &y1, &w, &h);
                if ((int)w + 8 <= 120) {
                    displayName = trunc;
                    nameTrimmed = true;
                    break;
                }
            }
        }
        display.setCursor(4, 32);
        display.print(displayName);
        if (nameTrimmed) {
            display.drawBitmap(4 + w, 32, icon_dots, 8, 8, SH110X_WHITE);
        }
    }

    // 3. Price
    display.setTextSize(2);
    display.setCursor(4, 44);
    display.print("$" + String(data.price));

    // 4. Arrow & Percentage
    bool isPositive = (data.percent_change >= 0);
    const unsigned char* arrowIcon = isPositive ? icon_arrow_up : icon_arrow_down;
    display.drawBitmap(102, 3, arrowIcon, 15, 15, 1);

    display.setTextSize(1);
    display.setCursor(95, 22);
    String trendPrefix = isPositive ? "+" : ""; 
    display.print(trendPrefix + String(data.percent_change, 1) + "%");
}

void DisplayService::drawPcScreen(const PcStats& pcStats) {
    bool isInvalid = (isnan(pcStats.cpu_percent) || pcStats.cpu_percent == 0) && (isnan(pcStats.mem_percent) || pcStats.mem_percent == 0); 

    if (isInvalid) {
        drawInfoScreen(icon_monitor, "Sin PC"); 
        return; 
    }

    display.clearDisplay();

    display.setTextColor(SH110X_WHITE);
    display.setTextWrap(false);
    display.setTextSize(1);
    display.setFont(); 

    const int barX = 20;
    const int barW = 86;
    const int barH = 6;
    
    const int fillXOffset = 2;       
    const int fillYOffset = 2;       
    const int maxFillW = barW - 4;
    const int fillH = 2;
    const int textX = 110;

    auto drawInfilledBar = [&](int y, float percent) {
        display.drawRect(barX, y, barW, barH, 1);
        int fillW = (int)((constrain(percent, 0, 100) / 100.0) * maxFillW);
        if (fillW > 0) {
            display.fillRect(barX + fillXOffset, y + fillYOffset, fillW, fillH, 1);
        }
    };

    // 1. CPU
    display.drawBitmap(0, 0, icon_cpu_percent, 16, 16, 1);
    drawInfilledBar(5, pcStats.cpu_percent);
    display.setCursor(textX, 4);
    display.print(String((int)round(pcStats.cpu_percent)) + "%");

    // 2. RAM
    display.drawBitmap(0, 16, icon_ram_percent, 16, 16, 1);
    drawInfilledBar(21, pcStats.mem_percent);
    display.setCursor(textX, 20);
    display.print(String((int)round(pcStats.mem_percent)) + "%");

    // 3. Disk
    display.drawBitmap(0, 32, icon_disk_percent, 16, 16, 1);
    drawInfilledBar(37, pcStats.disk_percent);
    display.setCursor(textX, 36);
    display.print(String((int)round(pcStats.disk_percent)) + "%");

    // 4. Download
    display.drawBitmap(0, 48, icon_net_down, 16, 16, 1); 
    
    float netPercent = (pcStats.net_down_kb / 5120.0) * 100.0;
    drawInfilledBar(53, netPercent);
    
    display.setCursor(textX, 52);
    
    if (pcStats.net_down_kb >= 1024) {
        display.print(String((int)round(pcStats.net_down_kb / 1024.0)) + "M");
    } else if (pcStats.net_down_kb >= 100) {
        display.print("<1M");
    } else {
        display.print(String((int)pcStats.net_down_kb) + "K");
    }
}

void DisplayService::drawMediaScreen(const PcMedia& media) {
    bool isInvalid = (media.status.length() == 0 || media.name.length() == 0 || media.author.length() == 0 || media.name.equalsIgnoreCase("Unknown"));

    if (isInvalid) {
        drawInfoScreen(icon_note, "Sin musica"); 
        return; 
    }

    display.clearDisplay();

    display.setTextColor(SH110X_WHITE);
    display.setTextWrap(false);
    display.setTextSize(1);
    display.setFont(); 

    display.drawBitmap(2, 4, icon_note, 32, 32, 1);

    display.setFont(&Picopixel);
    String statusStr = media.status;
    statusStr.toUpperCase();
    if (statusStr == "") statusStr = "STOPPED";
    
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(statusStr.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor(18 - (w / 2), 46);
    display.print(statusStr);

    const unsigned char* iconBits = icon_stop;
    if (statusStr == "PLAYING") iconBits = icon_play;
    if (statusStr == "PAUSED")  iconBits = icon_pause;
    display.drawBitmap(14, 52, iconBits, 8, 8, 1);

    auto drawSmartText = [&](String text, int x, int &y, const GFXfont* font, bool isPicopixel, int maxLines) {
        if (text == "") return;
        display.setFont(font);
        
        String lines[8] = {"", "", "", "", "", "", "", ""}; 
        int lineCount = 0;
        int start = 0;
        int maxWidth = 82; 
        
        while (start < text.length()) {
            int spaceIdx = text.indexOf(' ', start);
            if (spaceIdx == -1) spaceIdx = text.length();
            String word = text.substring(start, spaceIdx);
            
            String testLine = lines[lineCount].length() == 0 ? word : lines[lineCount] + " " + word;
            display.getTextBounds(testLine.c_str(), 0, 0, &x1, &y1, &w, &h);
            
            if (w > maxWidth) {
                if (lines[lineCount].length() == 0) {
                    lines[lineCount++] = word; 
                } else {
                    lineCount++;
                    if (lineCount < maxLines) lines[lineCount] = word;
                }
                if (lineCount == maxLines) break; 
            } else {
                lines[lineCount] = testLine;
            }
            start = spaceIdx + 1;
        }
        if (lineCount < maxLines && lines[lineCount].length() > 0) lineCount++;
        
        bool lastLineTrimmed = false;
        if (start < text.length() && lineCount == maxLines) {
            lastLineTrimmed = true;
            String& lastLine = lines[maxLines - 1];
            while (lastLine.length() > 0) {
                display.getTextBounds(lastLine.c_str(), 0, 0, &x1, &y1, &w, &h);
                if ((int)w + 8 <= maxWidth) break;
                int lastSpace = lastLine.lastIndexOf(' ');
                if (lastSpace == -1) lastLine = lastLine.substring(0, lastLine.length() - 1);
                else lastLine = lastLine.substring(0, lastSpace);
            }
        }

        for (int i = 0; i < lineCount; i++) {
            bool isLastLine = (i == lineCount - 1);
            if (isPicopixel) {
                y += 5;
                display.setCursor(x, y);
                display.print(lines[i]);
                if (lastLineTrimmed && isLastLine) {
                    display.getTextBounds(lines[i].c_str(), 0, 0, &x1, &y1, &w, &h);
                    display.drawBitmap(x + w, y - 6, icon_dots, 8, 8, SH110X_WHITE);
                }
                y += 1;
            } else {
                display.setCursor(x, y);
                display.print(lines[i]);
                if (lastLineTrimmed && isLastLine) {
                    display.getTextBounds(lines[i].c_str(), 0, 0, &x1, &y1, &w, &h);
                    display.drawBitmap(x + w, y, icon_dots, 8, 8, SH110X_WHITE);
                }
                y += 8 + 1;
            }
        }
        y += 6;
    };

    int cursorY = 4;

    String trackName = media.name;
    trackName.toUpperCase();

    String albumName = media.album;
    albumName.toUpperCase();
    bool hasAlbum = (albumName.length() > 0 && albumName != "UNKNOWN");

    int reservedForAuthor = 15;
    int reservedForAlbum = hasAlbum ? 12 : 0;

    int trackAvailY = 64 - cursorY - reservedForAuthor - reservedForAlbum;
    int maxTrackLines = max(1, trackAvailY / 9);
    drawSmartText(trackName, 44, cursorY, nullptr, false, maxTrackLines);

    int authorAvailY = 64 - cursorY - reservedForAlbum;
    int maxAuthorLines = max(1, authorAvailY / 9);
    drawSmartText(media.author, 44, cursorY, nullptr, false, maxAuthorLines);

    if (hasAlbum) {
        int albumAvailY = 64 - cursorY;
        int maxAlbumLines = max(1, albumAvailY / 6);
        drawSmartText(albumName, 44, cursorY, &Picopixel, true, maxAlbumLines);
    }
}

void DisplayService::drawBambuScreen(const BambuData& data) {
    bool isInvalid = (data.status == "SYNCING" || data.status.length() == 0);

    if (isInvalid) {
        drawInfoScreen(icon_printer, "Sin impresora"); 
        return; 
    }

    display.clearDisplay();

    display.setTextColor(SH110X_WHITE);
    display.setTextWrap(false);
    display.setTextSize(1);
    display.setFont(); 

    String status = data.status;
    status.toUpperCase();
    bool isIdle = (status == "IDLE" || status == "FINISH" || status == "FINISHED" || status == "FAILED");

    if (isIdle) {
        // 1. Printer Icon & IDLE Text (Left side)
        display.drawBitmap(22, 9, icon_printer, 32, 32, 1);
        
        display.setTextSize(1);
        display.setCursor(26, 45);
        display.print("LIBRE");

        display.setTextSize(2);

        // 2. Nozzle Temp (Top Right)
        String n_val = String((int)round(data.nozzle_temp));
        int n_circle_x = 79 + (n_val.length() * 12) + 5; 
        
        display.drawBitmap(65, 17, icon_nozzle, 8, 8, 1);
        display.setCursor(79, 13);
        display.print(n_val);
        display.drawCircle(n_circle_x, 16, 3, SH110X_WHITE); 

        // 3. Bed Temp (Bottom Right)
        String b_val = String((int)round(data.bed_temp));
        int b_circle_x = 79 + (b_val.length() * 12) + 5;
        
        display.drawBitmap(65, 41, icon_bed, 8, 8, 1);
        display.setCursor(79, 37);
        display.print(b_val);
        display.drawCircle(b_circle_x, 40, 3, SH110X_WHITE);

        return;
    }
    
    display.setTextSize(1);
    display.setFont();

    int16_t x1, y1;
    uint16_t w, h;

    // 1. Icons
    display.drawBitmap(4, 4, icon_nozzle, 8, 8, 1);
    display.drawBitmap(4, 16, icon_bed, 8, 8, 1);
    display.drawBitmap(116, 4, icon_part_fan, 8, 8, 1);
    display.drawBitmap(116, 16, icon_aux_fan, 8, 8, 1);

    // 2. Temperatures (Smart Slash Alignment)
    String n_lhs = String((int)round(data.nozzle_temp));
    String n_rhs = "/" + String((int)round(data.nozzle_target)) + " C";
    String b_lhs = String((int)round(data.bed_temp));
    String b_rhs = "/" + String((int)round(data.bed_target)) + " C";

    uint16_t w_n_lhs, w_b_lhs;
    display.getTextBounds(n_lhs.c_str(), 0, 0, &x1, &y1, &w_n_lhs, &h);
    display.getTextBounds(b_lhs.c_str(), 0, 0, &x1, &y1, &w_b_lhs, &h);

    int startX = 16; 
    int x_slash = startX + max(w_n_lhs, w_b_lhs);

    display.setCursor(x_slash - w_n_lhs, 4);
    display.print(n_lhs + n_rhs);

    display.setCursor(x_slash - w_b_lhs, 16);
    display.print(b_lhs + b_rhs);

    // 3. Fans (Right Alignment)
    String p_fan = String(data.fan_part) + "%";
    String a_fan = String(data.fan_aux) + "%";

    display.getTextBounds(p_fan.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor(116 - 4 - w, 4);
    display.print(p_fan);

    display.getTextBounds(a_fan.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor(116 - 4 - w, 16);
    display.print(a_fan);

    // 4. File Name (Center Alignment & Truncation)
    String fileName = data.file_name;
    
    if (fileName.length() == 0 || fileName.equalsIgnoreCase("None")) {
        fileName = "Sin trabajo";
    }
        
    int maxWidth = 120; 
    display.getTextBounds(fileName.c_str(), 0, 0, &x1, &y1, &w, &h);
    
    if (w > maxWidth) {
        String trunc;
        for (int i = 1; i <= fileName.length() / 2; i++) {
            int leftLen = (fileName.length() / 2) - i;
            int rightLen = fileName.length() - (fileName.length() / 2) - i;
            trunc = fileName.substring(0, leftLen) + "..." + fileName.substring(fileName.length() - rightLen);
            
            display.getTextBounds(trunc.c_str(), 0, 0, &x1, &y1, &w, &h);
            if (w <= maxWidth) {
                fileName = trunc;
                break;
            }
        }
    }
    
    display.getTextBounds(fileName.c_str(), 0, 0, &x1, &y1, &w, &h);
    int cursorX = (128 - w) / 2;
    
    display.setCursor(cursorX, 31);
    display.print(fileName);

    // 5. Progress Bar
    display.drawRect(4, 43, 120, 6, 1);
    int fillW = (int)((constrain(data.progress, 0, 100) / 100.0) * 116);
    if (fillW > 0) display.fillRect(6, 45, fillW, 2, 1);

    // 6. Stats
    String progStr = String(data.progress) + "%";
    String layerStr = String(data.layer) + "/" + String(data.total_layers);
    String timeStr = String(data.time_left) + "m";

    display.setCursor(4, 53);
    display.print(progStr);

    display.getTextBounds(layerStr.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor(64 - (w / 2), 53);
    display.print(layerStr);

    display.getTextBounds(timeStr.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor(128 - 4 - w, 53);
    display.print(timeStr);
}


void DisplayService::drawInfoScreen(const unsigned char* image, String text) {
    display.clearDisplay();

    display.setTextColor(SH110X_WHITE);
    display.setTextWrap(false);
    display.setTextSize(1);
    display.setFont(); 
    
    display.drawRect(1, 1, 126, 62, 1);
    display.drawRect(3, 3, 122, 58, 1);
    
    int16_t x1, y1; 
    uint16_t w, h;

    if (image != nullptr) {
        display.setTextSize(1);
        
        display.getTextBounds(text.c_str(), 0, 0, &x1, &y1, &w, &h);
        int textX = (128 - w) / 2;
        
        display.drawBitmap(48, 10, image, 32, 32, 1);
        display.setCursor(textX, 46); 
        display.print(text);
    } else {
        display.setTextSize(2);
        
        display.getTextBounds(text.c_str(), 0, 0, &x1, &y1, &w, &h);
        int textX = (128 - w) / 2;
        int textY = (64 - h) / 2;
        
        display.setCursor(textX, textY);
        display.print(text);
    }
}

bool DisplayService::isScreenEnabled(const AppState& state, int screenIndex) {
    const Config& config = state.config;

    switch (screenIndex) {
        case SCREEN_TIME:           return config.show_time;
        case SCREEN_CALENDAR:       return config.show_calendar;
        case SCREEN_WEATHER:        return config.show_weather;
        case SCREEN_AIR_QUALITY:    return false;   // Mini C3 v1.4.1: removed
        case SCREEN_DAYLIGHT:       return config.show_daylight;
        case SCREEN_MOON:           return config.show_moon;
        case SCREEN_ISS:            return config.show_iss;
        case SCREEN_NEWS:
            return config.show_news;
        case SCREEN_MESSAGES:
            return config.show_messages;
        case SCREEN_SPOTIFY:
            return config.show_spotify;
        case SCREEN_GAMES:
            return config.show_games;
        case SCREEN_TRUCO:
            return config.show_truco;
        case SCREEN_FLIGHT:         return false;   // Mini C3 v1.11.0: removed
        case SCREEN_POPULATION:     return false;   // Mini C3 v1.4.1: removed
        case SCREEN_STOCK:          return config.show_stock;
        case SCREEN_CRYPTO:         return config.show_crypto;
        case SCREEN_CURRENCY:       return config.show_currency;
        case SCREEN_PC_MONITOR: {
            if (!config.show_pc) return false;
            if (config.hide_empty_pc) {
                bool isInvalid = (isnan(state.pc.cpu_percent) || state.pc.cpu_percent == 0) && (isnan(state.pc.mem_percent) || state.pc.mem_percent == 0); 
                if (isInvalid) return false;
            }
            return true;
        }
        case SCREEN_PC_MEDIA: {
            if (!config.show_media) return false;
            if (config.hide_empty_media) {
                bool isInvalid = (state.media.status.length() == 0 || state.media.name.length() == 0 || state.media.author.length() == 0 || state.media.name.equalsIgnoreCase("Unknown"));
                if (isInvalid) return false;
            }
            return true;
        }
        case SCREEN_BAMBU: {
            if (!config.show_bambu) return false;
            if (config.hide_empty_bambu) {
                bool isInvalid = (state.bambu.status == "SYNCING");
                if (isInvalid) return false;
            }
            return true;
        }
        default: return false;
    }
}

void DisplayService::drawScreen(int screenIndex, const AppState& state, int subIndex) {
  // Mini C3: subIndex = item + 100 * detail page (page 0 = the normal screen)
  int page = subIndex / 100;
  subIndex %= 100;
  if (page > 0) {
    drawDetails(screenIndex, state, subIndex, page);
    return;
  }
  switch(screenIndex) {
    case SCREEN_NEWS: drawNewsScreen(state.news, subIndex); break;
    case SCREEN_MESSAGES: messageInbox.draw(display, state.messages); break;
    case SCREEN_SPOTIFY: drawSpotifyScreen(state.spotify); break;
    case SCREEN_GAMES: gameService.drawScreen(display); break;
    case SCREEN_TRUCO: trucoService.draw(display); break;
    case SCREEN_TIME: drawTimeScreen(state.config, TimeService::getCurrentTimeShort(state.config.time_format), TimeService::getFullDate()); break;
    case SCREEN_CALENDAR: drawCalendarScreen(state.config, state.calendar); break;
    case SCREEN_WEATHER: drawWeatherScreen(state.config, state.weather, TimeService::getCurrentTimeShort(state.config.time_format)); break;
    case SCREEN_DAYLIGHT: drawDaylightScreen(state.config, state.daylight); break;
    case SCREEN_MOON: drawMoonScreen(state.config, state.moon); break;
    case SCREEN_ISS: drawIssScreen(state.config, state.iss); break;
    case SCREEN_STOCK: drawStockScreen(state.config, state.stocks[subIndex]); break;
    case SCREEN_CRYPTO: drawCryptoScreen(state.config, state.cryptos[subIndex]); break;
    case SCREEN_CURRENCY: drawCurrencyScreen(state.config, state.currencies[subIndex], state.config.currency_multipliers[subIndex]); break;
    case SCREEN_PC_MONITOR: drawPcScreen(state.pc); break;
    case SCREEN_PC_MEDIA: drawMediaScreen(state.media); break;
    case SCREEN_BAMBU: drawBambuScreen(state.bambu); break;
  }
}

void DisplayService::drawCurrentScreen(const AppState& state) {
    int pages = detailPageCount(currentScreen, state, currentSubScreen);
    if (detailPage > pages) detailPage = pages;   // e.g. the data changed
    drawScreen(currentScreen, state, currentSubScreen + detailPage * 100);
}

int DisplayService::getFirstEnabledScreen(const AppState& state) {
    for (int i = 0; i < NUM_SCREENS; i++) {
        int screenId = state.config.screen_order[i];
        if (isScreenEnabled(state, screenId)) {
            return screenId;
        }
    }
    return state.config.screen_order[0];
}

void DisplayService::enterScreen(int screenIndex) {
    currentScreen = screenIndex;
    currentSubScreen = 0;
    detailPage = 0;
    newsActive = false;        // news restart from the top
    msgShown = -1;             // messages too
    spDrawnTrack = "";         // Spotify: no slide when entering the screen
    spVolumeMode = false;
}

int DisplayService::subItemCount(const AppState& state, int screenIndex) {
    switch (screenIndex) {
        case SCREEN_STOCK:    return state.config.stock_count;
        case SCREEN_CRYPTO:   return state.config.crypto_count;
        case SCREEN_CURRENCY: return state.config.currency_count;
        case SCREEN_NEWS:     return state.news.count;
        case SCREEN_MESSAGES: return state.messages.count;
        default:              return 1;
    }
}

void DisplayService::jumpToFirstEnabledScreen(const AppState& state) {
    enterScreen(getFirstEnabledScreen(state));
}

bool DisplayService::isOnFirstEnabledScreen(const AppState& state) {
    return currentScreen == getFirstEnabledScreen(state);
}


bool DisplayService::cycleItem(const AppState& state) {

    if (currentScreen == SCREEN_NEWS && detailPage == 0 && state.news.count > 0) {
        newsNextItem(state);
        return true;
    }

    int n = subItemCount(state, currentScreen);
    if (n <= 1) return false;

    int oldSub = currentSubScreen;
    if (oldSub >= n) oldSub = n - 1;
    int newSub = (oldSub + 1) % n;   // wraps around

    // keep the same details page if the new item has it
    int newPage = detailPage;
    int pages = detailPageCount(currentScreen, state, newSub);
    if (newPage > pages) newPage = pages;

    if (state.config.anim_mask == 0) {
        display.clearDisplay();
        drawScreen(currentScreen, state, newSub + newPage * 100);
        display.display();
    } else {
        animateHorizontal(currentScreen, oldSub + detailPage * 100, currentScreen, newSub + newPage * 100, state, true);
    }

    currentSubScreen = newSub;
    detailPage = newPage;
    return true;
}

bool DisplayService::cycleDetail(const AppState& state) {

    int pages = detailPageCount(currentScreen, state, currentSubScreen);
    if (pages <= 0) return false;

    int oldPage = detailPage;
    int newPage = (oldPage >= pages) ? 0 : oldPage + 1;   // main -> 1 -> ... -> main

    if (state.config.anim_mask == 0) {
        display.clearDisplay();
        drawScreen(currentScreen, state, currentSubScreen + newPage * 100);
        display.display();
    } else {
        animateHorizontal(currentScreen, currentSubScreen + oldPage * 100, currentScreen, currentSubScreen + newPage * 100, state, false);
    }
    detailPage = newPage;
    return true;
}

bool DisplayService::closeDetails(const AppState& state) {
    if (detailPage == 0) return false;
    int oldPage = detailPage;
    detailPage = 0;
    if (state.config.anim_mask == 0) {
        display.clearDisplay();
        drawScreen(currentScreen, state, currentSubScreen);
        display.display();
    } else {
        animateHorizontal(currentScreen, currentSubScreen + oldPage * 100, currentScreen, currentSubScreen, state, true);
    }
    return true;
}

void DisplayService::switchToNextScreen(const AppState& state, bool skipSubScreens) {
    const Config& config = state.config;

    bool pagedScreen = (currentScreen == SCREEN_STOCK || currentScreen == SCREEN_CRYPTO || currentScreen == SCREEN_CURRENCY);
    if (!skipSubScreens && pagedScreen && currentSubScreen + 1 < subItemCount(state, currentScreen)) {
        currentSubScreen++;
        detailPage = 0;
        animateTransition(currentScreen, currentSubScreen - 1, currentScreen, currentSubScreen, state);
        return;
    }

    int oldScreen = currentScreen;
    int oldSubScreen = currentSubScreen;

    int currentIndex = 0;
    for (int i = 0; i < NUM_SCREENS; i++) {
        if (config.screen_order[i] == currentScreen) {
            currentIndex = i; break;
        }
    }

    int checkIndex = currentIndex;
    int nextScreenCandidate = currentScreen;
    do {
        checkIndex++;
        if (checkIndex >= NUM_SCREENS) checkIndex = 0;
        int candidateId = config.screen_order[checkIndex];
        if (isScreenEnabled(state, candidateId)) {
            nextScreenCandidate = candidateId;
            break;
        }
    } while (checkIndex != currentIndex);

    if (oldScreen == nextScreenCandidate && oldSubScreen == 0) return;

    animateTransition(oldScreen, oldSubScreen + detailPage * 100, nextScreenCandidate, 0, state);
    enterScreen(nextScreenCandidate);
}

void DisplayService::switchToPreviousScreen(const AppState& state, bool skipSubScreens) {
    const Config& config = state.config;

    bool pagedScreen = (currentScreen == SCREEN_STOCK || currentScreen == SCREEN_CRYPTO || currentScreen == SCREEN_CURRENCY);
    if (!skipSubScreens && pagedScreen && currentSubScreen > 0) {
        currentSubScreen--;
        detailPage = 0;
        animateTransition(currentScreen, currentSubScreen + 1, currentScreen, currentSubScreen, state);
        return;
    }

    int oldScreen = currentScreen;
    int oldSubScreen = currentSubScreen;

    int currentIndex = 0;
    for (int i = 0; i < NUM_SCREENS; i++) {
        if (config.screen_order[i] == currentScreen) {
            currentIndex = i; break;
        }
    }

    int checkIndex = currentIndex;
    int prevScreenCandidate = currentScreen;
    do {
        checkIndex--;
        if (checkIndex < 0) checkIndex = NUM_SCREENS - 1;
        int candidateId = config.screen_order[checkIndex];
        if (isScreenEnabled(state, candidateId)) {
            prevScreenCandidate = candidateId;
            break;
        }
    } while (checkIndex != currentIndex);

    if (oldScreen == prevScreenCandidate && oldSubScreen == 0) return;

    animateTransition(oldScreen, oldSubScreen + detailPage * 100, prevScreenCandidate, 0, state);
    enterScreen(prevScreenCandidate);
}

void DisplayService::setContrast(bool dim) {
    display.setContrast(dim ? CONTRAST_DIM : CONTRAST_MAX);
}

int DisplayService::getNextAnimationEffect(uint16_t mask) {
    int enabledAnims[10]; int count = 0;
    for (int i = 1; i <= 5; i++) { if (mask & (1 << i)) enabledAnims[count++] = i; }
    if (count == 0) return 0;
    return enabledAnims[random(0, count)];
}

void DisplayService::animateTransition(int prevScreen, int prevSub, int nextScreen, int nextSub, const AppState& state) {
    int selectedEffect = getNextAnimationEffect(state.config.anim_mask);
    switch(selectedEffect) {
        case ANIM_SLIDE_HORIZONTAL: animateHorizontal(prevScreen, prevSub, nextScreen, nextSub, state); break;
        case ANIM_SLIDE_VERTICAL: animateVertical(prevScreen, prevSub, nextScreen, nextSub, state); break;
        case ANIM_DISSOLVE: animateDissolve(prevScreen, prevSub, nextScreen, nextSub, state); break;
        case ANIM_CURTAIN: animateCurtain(prevScreen, prevSub, nextScreen, nextSub, state); break;
        case ANIM_BLINDS: animateBlinds(prevScreen, prevSub, nextScreen, nextSub, state); break;
        default:
            display.clearDisplay(); drawScreen(nextScreen, state, nextSub); display.display(); break;
    }
}

void DisplayService::animateHorizontal(int prev, int pSub, int next, int nSub, const AppState& state, bool reverse) {
  display.clearDisplay(); drawScreen(prev, state, pSub); memcpy(screenBufferOld, display.getBuffer(), 1024);
  display.clearDisplay(); drawScreen(next, state, nSub); memcpy(screenBufferNew, display.getBuffer(), 1024);
  int step = 16;
  for (int offset = 0; offset <= 128; offset += step) {
    uint8_t* displayBuf = display.getBuffer();
    for (int page = 0; page < 8; page++) {
      int start = page * 128;
      if (!reverse) {
        // old slides out to the left, new comes in from the right
        if (offset < 128) memcpy(&displayBuf[start], &screenBufferOld[start + offset], 128 - offset);
        if (offset > 0) memcpy(&displayBuf[start + (128 - offset)], &screenBufferNew[start], offset);
      } else {
        // old slides out to the right, new comes in from the left
        if (offset < 128) memcpy(&displayBuf[start + offset], &screenBufferOld[start], 128 - offset);
        if (offset > 0) memcpy(&displayBuf[start], &screenBufferNew[start + (128 - offset)], offset);
      }
    }
    display.display();
  }
}

void DisplayService::animateVertical(int prev, int pSub, int next, int nSub, const AppState& state) {
  display.clearDisplay(); drawScreen(prev, state, pSub); memcpy(screenBufferOld, display.getBuffer(), 1024);
  display.clearDisplay(); drawScreen(next, state, nSub); memcpy(screenBufferNew, display.getBuffer(), 1024);
  for (int step = 0; step <= 8; step++) {
    uint8_t* displayBuf = display.getBuffer();
    for (int page = 0; page < 8; page++) {
        int oldPageIdx = page + step; int newPageIdx = page - (8 - step); int destIndex = page * 128;
        if (oldPageIdx < 8) memcpy(&displayBuf[destIndex], &screenBufferOld[oldPageIdx * 128], 128);
        else if (newPageIdx >= 0) memcpy(&displayBuf[destIndex], &screenBufferNew[newPageIdx * 128], 128);
    }
    display.display(); delay(10); 
  }
}

void DisplayService::animateDissolve(int prev, int pSub, int next, int nSub, const AppState& state) {
  display.clearDisplay(); drawScreen(prev, state, pSub); memcpy(screenBufferOld, display.getBuffer(), 1024);
  display.clearDisplay(); drawScreen(next, state, nSub); memcpy(screenBufferNew, display.getBuffer(), 1024);
  uint8_t* displayBuf = display.getBuffer();
  for (int step = 0; step < 8; step++) {
    uint8_t mask = 0;
    switch(step) {
        case 0: mask = 0b10000000; break; case 1: mask = 0b11000000; break; case 2: mask = 0b11100000; break; case 3: mask = 0b11100100; break;
        case 4: mask = 0b11110100; break; case 5: mask = 0b11111100; break; case 6: mask = 0b11111110; break; case 7: mask = 0b11111111; break;
    }
    for (int i = 0; i < 1024; i++) displayBuf[i] = (screenBufferNew[i] & mask) | (screenBufferOld[i] & ~mask);
    display.display(); delay(10);
  }
}

void DisplayService::animateCurtain(int prev, int pSub, int next, int nSub, const AppState& state) {
  display.clearDisplay(); drawScreen(prev, state, pSub); memcpy(screenBufferOld, display.getBuffer(), 1024);
  display.clearDisplay(); drawScreen(next, state, nSub); memcpy(screenBufferNew, display.getBuffer(), 1024);
  int maxRadius = 80; int step = 4; uint8_t* displayBuf = display.getBuffer();
  for (int r = 0; r <= maxRadius; r += step) {
      int startX = 64 - r; if (startX < 0) startX = 0;
      int endX = 64 + r; if (endX > 128) endX = 128;
      for (int x = 0; x < 128; x++) {
          bool insideCurtain = (x >= startX && x < endX);
          for (int page = 0; page < 8; page++) {
              int idx = x + (page * 128); displayBuf[idx] = insideCurtain ? screenBufferNew[idx] : screenBufferOld[idx];
          }
      }
      display.display();
  }
}

void DisplayService::animateBlinds(int prev, int pSub, int next, int nSub, const AppState& state) {
  display.clearDisplay(); drawScreen(prev, state, pSub); memcpy(screenBufferOld, display.getBuffer(), 1024);
  display.clearDisplay(); drawScreen(next, state, nSub); memcpy(screenBufferNew, display.getBuffer(), 1024);
  uint8_t* displayBuf = display.getBuffer();
  memcpy(displayBuf, screenBufferOld, 1024); display.display();
  int blindWidth = 16; int numBlinds = 8; int stepSize = 2; 
  for (int progress = 0; progress < blindWidth; progress += stepSize) {
      for (int blind = 0; blind < numBlinds; blind++) {
          int blindStartX = blind * blindWidth;
          for (int i = 0; i < stepSize; i++) {
              int currentX = blindStartX + progress + i; if (currentX >= 128) continue;
              for (int page = 0; page < 8; page++) {
                  int idx = currentX + (page * 128); displayBuf[idx] = screenBufferNew[idx];
              }
          }
      }
      display.display(); delay(5);
  }
}

// ---------------------------------------------------------------------------
//  Mini C3 screens
// ---------------------------------------------------------------------------


void DisplayService::drawOtaProgress(int percent, const String& line1, const String& line2) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    display.clearDisplay();
    display.setFont();
    display.setTextSize(1);
    display.setTextWrap(false);
    display.setTextColor(SH110X_WHITE);

    int16_t x1, y1; uint16_t w, h;

    display.getTextBounds(line1.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor((128 - w) / 2, 8);
    display.print(line1);

    display.drawRect(4, 26, 120, 12, SH110X_WHITE);
    int fill = (116 * percent) / 100;
    if (fill > 0) display.fillRect(6, 28, fill, 8, SH110X_WHITE);

    String pct = String(percent) + "%";
    display.getTextBounds(pct.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor((128 - w) / 2, 44);
    display.print(pct);

    if (line2.length() > 0) {
        display.getTextBounds(line2.c_str(), 0, 0, &x1, &y1, &w, &h);
        display.setCursor((128 - w) / 2, 55);
        display.print(line2);
    }
    display.display();
}

// ---------------------------------------------------------------------------
//  Mini C3: news screen
// ---------------------------------------------------------------------------

// Word wrap for the 6x8 font. Returns the number of lines used; truncated is
// set when the text did not fit in maxLines.
static int wrapWords(const String& text, int maxChars, String* lines, int maxLines, bool& truncated) {
    truncated = false;
    int n = 0;
    String rest = text;
    rest.trim();
    while (rest.length() > 0) {
        if (n >= maxLines) { truncated = true; break; }
        if ((int)rest.length() <= maxChars) { lines[n++] = rest; break; }
        int cut = rest.lastIndexOf(' ', maxChars);
        if (cut <= 0) cut = maxChars;
        lines[n++] = rest.substring(0, cut);
        rest = rest.substring(cut);
        rest.trim();
    }
    return n;
}

// All the headlines as one continuous list of lines (rebuilt when the news change)
void DisplayService::buildNewsFeed(const NewsData& data) {
    String sig = data.update_time + "|" + String(data.count) + "|" + (data.count > 0 ? data.items[0].title.substring(0, 20) : String(""));
    if (sig == newsFeedSignature && newsFeedLineCount > 0) return;
    newsFeedSignature = sig;

    newsFeedLineCount = 0;
    for (int k = 0; k < data.count && k < MAX_NEWS; k++) {
        if (newsFeedLineCount >= NEWS_FEED_MAX_LINES - 2) break;
        newsItemStartLine[k] = newsFeedLineCount;

        String src = oledAscii(data.items[k].source);
        if (src.length() == 0) src = "Noticia";
        if (src.length() > 21) src = src.substring(0, 21);
        newsFeedLines[newsFeedLineCount] = src;
        newsFeedFlags[newsFeedLineCount] = 1;   // source line (underlined)
        newsFeedItem[newsFeedLineCount] = k;
        newsFeedLineCount++;

        String lines[12];
        bool truncated = false;
        int n = wrapWords(oledAscii(data.items[k].title), 21, lines, 12, truncated);
        for (int i = 0; i < n && newsFeedLineCount < NEWS_FEED_MAX_LINES - 1; i++) {
            newsFeedLines[newsFeedLineCount] = lines[i];
            newsFeedFlags[newsFeedLineCount] = 0;
            newsFeedItem[newsFeedLineCount] = k;
            newsFeedLineCount++;
        }

        newsFeedLines[newsFeedLineCount] = "";     // gap between headlines
        newsFeedFlags[newsFeedLineCount] = 0;
        newsFeedItem[newsFeedLineCount] = k;
        newsFeedLineCount++;
    }
    if (newsScrollY >= newsFeedLineCount * 10) newsScrollY = 0;
}

void DisplayService::drawNewsScreen(const NewsData& data, int index) {
    display.clearDisplay();
    display.setFont();
    display.setTextSize(1);
    display.setTextWrap(false);

    if (data.count <= 0) {
        if (DataSyncService::isStepSkipped(STEP_NEWS)) drawInfoScreen(icon_error, "Noticias salteadas");
        else if (data.fetch_status == 0) drawInfoScreen(icon_error, "Buscando noticias...");
        else drawInfoScreen(icon_error, oledAscii(data.fetch_error).substring(0, 20));
        return;
    }
    buildNewsFeed(data);
    if (newsFeedLineCount == 0) return;
    if (index < 0) index = 0;
    if (index >= data.count) index = data.count - 1;

    const int feedH = newsFeedLineCount * 10;
    int scroll = (currentScreen == SCREEN_NEWS && newsActive) ? newsScrollY : newsItemStartLine[index] * 10;
    scroll %= feedH;

    // Draw the lines that fall in the window; the list repeats, so after the
    // last headline the first one comes in again
    for (int copy = 0; copy < 2; copy++) {
        int base = NEWS_TOP - scroll + copy * feedH;
        int first = max(0, (-base + 3) / 10 - 1);
        for (int i = first; i < newsFeedLineCount; i++) {
            int y = base + i * 10;
            if (y > 63) break;
            if (y < 3) continue;
            if (newsFeedLines[i].length() == 0) continue;
            display.setCursor(0, y);
            display.print(newsFeedLines[i]);
            if (newsFeedFlags[i] == 1) display.drawFastHLine(0, y + 8, 6 * newsFeedLines[i].length(), SH110X_WHITE);
        }
    }

    // Which headline is at the top of the window
    int topLine = ((scroll + 5) / 10) % newsFeedLineCount;
    int current = newsFeedItem[topLine];

    // Title bar on top of the scrolled text
    display.fillRect(0, 0, 128, 11, SH110X_BLACK);
    display.fillRect(0, 0, 128, 10, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(2, 1);
    display.print(newsPaused ? "PAUSA" : "NOTICIAS");
    String pos = String(current + 1) + "/" + String(data.count);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(pos.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor(126 - w, 1);
    display.print(pos);
    display.setTextColor(SH110X_WHITE);

    // Position bar on the right
    int barH = max(4, (53 * 53) / max(53, feedH));
    int barY = 11 + ((53 - barH) * scroll) / max(1, feedH - 1);
    display.fillRect(126, barY, 2, barH, SH110X_WHITE);
}

bool DisplayService::tickNews(const AppState& state, bool paused) {
    if (currentScreen != SCREEN_NEWS || detailPage != 0 || state.news.count <= 0) return false;
    uint32_t now = millis();
    buildNewsFeed(state.news);
    if (newsFeedLineCount == 0) return false;
    const int feedH = newsFeedLineCount * 10;

    if (!newsActive) {
        newsActive = true;
        int idx = currentSubScreen < state.news.count ? currentSubScreen : 0;
        newsScrollY = newsItemStartLine[idx] * 10;
        newsPhase = NEWS_WAIT_START;
        newsPhaseAt = now;
        newsLoopedOnce = false;
        newsScrolled = 0;
    }

    bool redraw = false;
    if (paused != newsPaused) {
        newsPaused = paused;
        redraw = true;
    }

    if (newsPaused) {
        newsPhaseAt = now;   // the clock stops while A is held
    } else if (newsPhase == NEWS_WAIT_START) {
        if (now - newsPhaseAt >= NEWS_START_DELAY_MS) {
            newsPhase = NEWS_SCROLLING;
            newsPhaseAt = now;
        }
    } else {
        // catch up if the loop was busy, but never more than a few pixels at once
        int steps = (now - newsPhaseAt) / NEWS_STEP_MS;
        if (steps > 0) {
            if (steps > 4) steps = 4;
            newsPhaseAt += steps * NEWS_STEP_MS;
            newsScrollY += steps;
            newsScrolled += steps;
            if (newsScrollY >= feedH) newsScrollY -= feedH;
            if (newsScrolled >= feedH) newsLoopedOnce = true;
            redraw = true;
        }
    }

    // keep "the current item" in sync for → (details) and ←
    int topLine = ((newsScrollY + 5) / 10) % newsFeedLineCount;
    currentSubScreen = newsFeedItem[topLine];

    if (redraw) {
        drawNewsScreen(state.news, currentSubScreen);
        display.display();
    }
    return redraw;
}

// ←: jump to the start of the next headline and wait a moment there
void DisplayService::newsNextItem(const AppState& state) {
    buildNewsFeed(state.news);
    if (newsFeedLineCount == 0) return;
    int topLine = ((newsScrollY + 5) / 10) % newsFeedLineCount;
    int next = (newsFeedItem[topLine] + 1) % state.news.count;
    newsActive = true;
    newsScrollY = newsItemStartLine[next] * 10;
    newsPhase = NEWS_WAIT_START;
    newsPhaseAt = millis();
    currentSubScreen = next;
    drawNewsScreen(state.news, next);
    display.display();
}

bool DisplayService::isReadingNews(const AppState& state) const {
    return currentScreen == SCREEN_NEWS && state.news.count > 0 && (!newsLoopedOnce || newsPaused);
}

// ---------------------------------------------------------------------------
//  Mini C3: details pages (→)
// ---------------------------------------------------------------------------

static String fmtNum(float v, int decimals, const char* unit = "") {
    if (isnan(v)) return "--";
    return String(v, decimals) + unit;
}

static String fmtMins(int mins, const String& format) {
    if (mins < 0) return "--:--";
    return TimeService::formatMinsFromMidnight(mins, format);
}

static String moonPhaseEs(const String& phase) {
    if (phase.equalsIgnoreCase("New Moon")) return "Luna nueva";
    if (phase.equalsIgnoreCase("Waxing Crescent")) return "Creciente";
    if (phase.equalsIgnoreCase("First Quarter")) return "Cuarto creciente";
    if (phase.equalsIgnoreCase("Waxing Gibbous")) return "Gibosa creciente";
    if (phase.equalsIgnoreCase("Full Moon")) return "Luna llena";
    if (phase.equalsIgnoreCase("Waning Gibbous")) return "Gibosa menguante";
    if (phase.equalsIgnoreCase("Last Quarter") || phase.equalsIgnoreCase("Third Quarter")) return "Cuarto menguante";
    if (phase.equalsIgnoreCase("Waning Crescent")) return "Menguante";
    return phase;
}

static String fmtBig(long long v) {
    if (v < 0) return "--";
    // millions with one decimal is enough on a 21-char line
    double m = v / 1000000.0;
    if (m >= 1000) return String(m / 1000.0, 3) + " mil M";
    return String(m, 1) + " M";
}

int DisplayService::buildDetailRows(int screen, const AppState& state, int sub, DetailRow* rows, int maxRows, String& title) {
    const Config& c = state.config;
    int n = 0;
    auto add = [&](const String& label, const String& value) {
        if (n < maxRows) { rows[n].label = label; rows[n].value = value; n++; }
    };
    bool fahrenheit = (c.temp_unit == "F");
    auto temp = [&](float t) -> String {
        if (isnan(t)) return "--";
        return String(fahrenheit ? t * 9.0f / 5.0f + 32.0f : t, 1) + (fahrenheit ? " F" : " C");
    };

    switch (screen) {
        case SCREEN_TIME: {
            title = "HORA";
            struct tm t;
            if (getLocalTime(&t, 10)) {
                char buf[24];
                strftime(buf, sizeof(buf), "%d/%m/%Y", &t);
                add("Fecha", buf);
                add("Dia del ano", String(t.tm_yday + 1));
                strftime(buf, sizeof(buf), "%V", &t);
                add("Semana", buf);
            }
            add("Zona", c.timezone);
            if (state.daylight.sunrise_mins >= 0) {
                add("Amanecer", fmtMins(state.daylight.sunrise_mins, c.time_format));
                add("Atardecer", fmtMins(state.daylight.sunset_mins, c.time_format));
            }
            break;
        }
        case SCREEN_CALENDAR: {
            title = "FERIADOS";
            struct tm t;
            String today = "";
            if (getLocalTime(&t, 10)) {
                char buf[12];
                strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
                today = buf;
            }
            for (int i = 0; i < state.calendar.count; i++) {
                const Holiday& hd = state.calendar.items[i];
                if (today.length() && hd.date < today) continue;   // only upcoming
                String d = hd.date.length() >= 10 ? hd.date.substring(8, 10) + "/" + hd.date.substring(5, 7) : hd.date;
                add(d, oledAscii(hd.name));
            }
            if (n == 0) add("", "Sin feriados proximos");
            break;
        }
        case SCREEN_WEATHER: {
            title = "CLIMA";
            const WeatherData& w = state.weather;
            add("Temperatura", temp(w.temp));
            add("Sensacion", temp(w.apparent_temperature));
            add("Humedad", w.humidity > 0 ? String(w.humidity) + " %" : String("--"));
            add("Viento", fmtNum(w.wind_speed, 0, " km/h"));
            add("Lluvia", fmtNum(w.precipitation_probability, 0, " %"));
            add("Presion", fmtNum(w.pressure, 0, " hPa"));
            add("Visibilidad", isnan(w.visibility) ? String("--") : String(w.visibility / 1000.0f, 1) + " km");
            add("Actualizado", w.update_time);
            break;
        }
        case SCREEN_DAYLIGHT: {
            title = "SOL";
            const DaylightData& d = state.daylight;
            add("Amanecer", fmtMins(d.sunrise_mins, c.time_format));
            add("Mediodia", fmtMins(d.noon_mins, c.time_format));
            add("Atardecer", fmtMins(d.sunset_mins, c.time_format));
            add("Duracion", d.length_mins >= 0 ? String(d.length_mins / 60) + " h " + String(d.length_mins % 60) + " min" : String("--"));
            break;
        }
        case SCREEN_MOON: {
            title = "LUNA";
            const MoonData& m = state.moon;
            add("Fase", moonPhaseEs(m.curphase));
            add("Iluminada", m.fracillum >= 0 ? String(m.fracillum) + " %" : String("--"));
            add("Sale", fmtMins(m.rise_mins, c.time_format));
            add("Se pone", fmtMins(m.set_mins, c.time_format));
            break;
        }
        case SCREEN_ISS: {
            title = "ESTACION ESPACIAL";
            const IssData& i = state.iss;
            bool miles = (i.units == "miles");
            add("Sobre", oledAscii(IssService::getLocationName(i)));
            add("Latitud", fmtNum(i.latitude, 2));
            add("Longitud", fmtNum(i.longitude, 2));
            add("Altitud", fmtNum(i.altitude, 0, miles ? " mi" : " km"));
            add("Velocidad", fmtNum(i.velocity, 0, miles ? " mph" : " km/h"));
            add("De dia", i.is_daylight ? "si" : "no");
            break;
        }
        case SCREEN_STOCK: {
            const StockData& st = state.stocks[sub];
            title = "ACCION " + st.symbol;
            add("Nombre", oledAscii(st.name));
            add("Precio", st.updated ? "$" + String(st.price, 2) : String("--"));
            add("Cierre ant.", st.updated ? "$" + String(st.previous_close, 2) : String("--"));
            add("Variacion", st.updated ? String(st.percent_change, 2) + " %" : String("--"));
            add("Dif.", st.updated ? "$" + String(st.price - st.previous_close, 2) : String("--"));
            break;
        }
        case SCREEN_CRYPTO: {
            const CryptoData& cr = state.cryptos[sub];
            title = "CRIPTO";
            add("Nombre", oledAscii(cr.name));
            add("Simbolo", cr.symbol);
            add("Precio", cr.updated ? "US$ " + String(cr.price_usd, cr.price_usd < 10 ? 4 : 2) : String("--"));
            add("24 h", cr.updated ? String(cr.percent_change_24h, 2) + " %" : String("--"));
            break;
        }
        case SCREEN_CURRENCY: {
            const CurrencyData& cu = state.currencies[sub];
            title = "COTIZACION";
            String b = cu.base; b.toUpperCase();
            String t = cu.target; t.toUpperCase();
            int mult = c.currency_multipliers[sub] > 0 ? c.currency_multipliers[sub] : 1;
            add("Par", b + " > " + t);
            add("1 " + b, cu.updated ? String(cu.rate, 4) + " " + t : String("--"));
            if (mult != 1) add(String(mult) + " " + b, cu.updated ? String(cu.rate * mult, 2) + " " + t : String("--"));
            add("1 " + t, (cu.updated && cu.rate > 0) ? String(1.0f / cu.rate, 4) + " " + b : String("--"));
            add("Fecha", cu.date);
            break;
        }
        case SCREEN_MESSAGES: {
            const MessageData& md = state.messages;
            title = "MENSAJE";
            if (md.count <= 0 || sub >= md.count) {
                add("", "Sin mensajes");
                add("", String(DEVICE_HOSTNAME) + ".local/mensaje");
                break;
            }
            title = "MENSAJE " + String(sub + 1) + "/" + String(md.count);
            add("De", oledAscii(md.items[sub].from));
            add("Hora", md.items[sub].time);
            add("Estado", md.items[sub].read ? "leido" : "nuevo");
            add("Largo", String(md.items[sub].text.length()) + " letras");
            break;
        }
        case SCREEN_NEWS: {
            const NewsData& nd = state.news;
            if (nd.count <= 0 || sub >= nd.count) {
                title = "NOTICIAS";
                add("Estado", nd.fetch_status == 0 ? String("sin buscar") : oledAscii(nd.fetch_error));
                break;
            }
            title = "NOTICIA " + String(sub + 1) + "/" + String(nd.count);
            String lines[12];
            bool truncated = false;
            int k = wrapWords(oledAscii(nd.items[sub].title), 21, lines, 12, truncated);
            for (int i = 0; i < k; i++) add("", lines[i]);
            if (nd.items[sub].source.length()) add("Fuente", oledAscii(nd.items[sub].source));
            add("Hora", nd.update_time);
            break;
        }
        default:
            break;
    }
    return n;
}

int DisplayService::detailPageCount(int screen, const AppState& state, int sub) {
    if (screen == SCREEN_PC_MONITOR || screen == SCREEN_PC_MEDIA || screen == SCREEN_BAMBU || screen == SCREEN_SPOTIFY || screen == SCREEN_GAMES || screen == SCREEN_TRUCO || screen == SCREEN_MESSAGES) return 0;
    DetailRow rows[MAX_DETAIL_ROWS];
    String title;
    int n = buildDetailRows(screen, state, sub, rows, MAX_DETAIL_ROWS, title);
    return (n + DETAIL_ROWS_PER_PAGE - 1) / DETAIL_ROWS_PER_PAGE;
}

void DisplayService::drawDetails(int screen, const AppState& state, int sub, int page) {
    DetailRow rows[MAX_DETAIL_ROWS];
    String title;
    int n = buildDetailRows(screen, state, sub, rows, MAX_DETAIL_ROWS, title);
    int pages = (n + DETAIL_ROWS_PER_PAGE - 1) / DETAIL_ROWS_PER_PAGE;
    if (pages < 1) pages = 1;
    if (page > pages) page = pages;

    display.clearDisplay();
    display.setFont();
    display.setTextSize(1);
    display.setTextWrap(false);

    // Title bar: name + page
    display.fillRect(0, 0, 128, 10, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(2, 1);
    String t = title;
    if (t.length() > 15) t = t.substring(0, 15);
    display.print(t);
    String pg = String(page) + "/" + String(pages);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(pg.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor(126 - w, 1);
    display.print(pg);
    display.setTextColor(SH110X_WHITE);

    int first = (page - 1) * DETAIL_ROWS_PER_PAGE;
    for (int i = 0; i < DETAIL_ROWS_PER_PAGE && first + i < n; i++) {
        const DetailRow& r = rows[first + i];
        int y = 13 + i * 10;
        if (r.label.length() == 0) {
            display.setCursor(0, y);
            display.print(r.value.substring(0, 21));
            continue;
        }
        // label left, value right; the value gets what is left of the 21 columns
        String label = r.label;
        int maxValue = 21 - (int)label.length() - 1;
        String value = r.value;
        if (maxValue < 4) { label = label.substring(0, 21 - 5); maxValue = 4; }
        if ((int)value.length() > maxValue) value = value.substring(0, maxValue);
        display.setCursor(0, y);
        display.print(label);
        display.getTextBounds(value.c_str(), 0, 0, &x1, &y1, &w, &h);
        display.setCursor(128 - w, y);
        display.print(value);
    }
}

// ---------------------------------------------------------------------------
//  Mini C3: messages
// ---------------------------------------------------------------------------

int DisplayService::buildMessageLines(const Message& m, String* lines, int maxLines) {
    String head = "De " + oledAscii(m.from);
    String t = m.time.length() >= 5 ? m.time.substring(0, 5) : m.time;
    int room = 21 - (int)t.length() - 1;
    if ((int)head.length() > room) head = head.substring(0, room);
    while ((int)head.length() < room) head += " ";
    lines[0] = head + " " + t;
    bool truncated = false;
    return 1 + wrapWords(oledAscii(m.text), 21, lines + 1, maxLines - 1, truncated);
}

void DisplayService::drawMessagesScreen(const MessageData& data, int index) {
    display.clearDisplay();
    display.setFont();
    display.setTextSize(1);
    display.setTextWrap(false);
    display.setTextColor(SH110X_WHITE);
    int16_t x1, y1; uint16_t w, h;

    if (data.count <= 0) {
        display.fillRect(0, 0, 128, 10, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
        display.setCursor(2, 1);
        display.print("MENSAJES");
        display.setTextColor(SH110X_WHITE);
        display.drawBitmap(4, 18, icon_envelope_small, 16, 12, SH110X_WHITE);
        display.setCursor(26, 20);
        display.print("Sin mensajes");
        display.setCursor(0, 40);
        display.print("Manda uno desde:");
        String url = String(DEVICE_HOSTNAME) + ".local/mensaje";
        display.setCursor(0, 52);
        display.print(url.substring(0, 21));
        return;
    }
    if (index < 0) index = 0;
    if (index >= data.count) index = data.count - 1;
    const Message& m = data.items[index];

    int scroll = (currentScreen == SCREEN_MESSAGES && index == msgShown) ? msgScrollY : 0;

    String lines[MSG_MAX_LINES];
    int n = buildMessageLines(m, lines, MSG_MAX_LINES);
    for (int i = 0; i < n; i++) {
        int y = MSG_TOP + i * 10 - scroll;
        if (y < 3 || y > 63) continue;
        display.setCursor(0, y);
        display.print(lines[i]);
        if (i == 0) display.drawFastHLine(0, y + 8, 128, SH110X_WHITE);
    }

    display.fillRect(0, 0, 128, 11, SH110X_BLACK);
    display.fillRect(0, 0, 128, 10, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(2, 1);
    display.print(msgPaused ? "PAUSA" : (m.read ? "MENSAJE" : "NUEVO!"));
    String pos = String(index + 1) + "/" + String(data.count);
    display.getTextBounds(pos.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor(126 - w, 1);
    display.print(pos);
    display.setTextColor(SH110X_WHITE);

    int contentH = MSG_TOP + (n - 1) * 10 + 8;
    if (contentH > 64) {
        int maxScroll = contentH - 63;
        int barH = max(6, (53 * 53) / (contentH - 10));
        int barY = 11 + ((53 - barH) * scroll) / max(1, maxScroll);
        display.fillRect(126, barY, 2, barH, SH110X_WHITE);
    }
}

bool DisplayService::tickMessages(const AppState& state, bool paused) {
    const MessageData& md = state.messages;
    if (currentScreen != SCREEN_MESSAGES || detailPage != 0 || md.count <= 0) return false;
    uint32_t now = millis();
    int idx = currentSubScreen < md.count ? currentSubScreen : 0;

    if (idx != msgShown) {
        msgShown = idx;
        msgScrollY = 0;
        msgPhase = NEWS_WAIT_START;
        msgPhaseAt = now;
        msgReadOnce = false;
    }

    bool redraw = false;
    if (paused != msgPaused) { msgPaused = paused; redraw = true; }

    String lines[MSG_MAX_LINES];
    int n = buildMessageLines(md.items[idx], lines, MSG_MAX_LINES);
    int contentH = MSG_TOP + (n - 1) * 10 + 8;
    int maxScroll = contentH > 63 ? contentH - 63 : 0;

    if (maxScroll == 0) {
        if (!msgReadOnce && now - msgPhaseAt > 4000) msgReadOnce = true;
    } else if (msgPaused) {
        msgPhaseAt = now;
    } else if (msgPhase == NEWS_WAIT_START) {
        if (now - msgPhaseAt >= NEWS_START_DELAY_MS) { msgPhase = NEWS_SCROLLING; msgPhaseAt = now; }
    } else if (msgPhase == NEWS_SCROLLING) {
        int steps = (now - msgPhaseAt) / NEWS_STEP_MS;
        if (steps > 0) {
            if (steps > 4) steps = 4;
            msgPhaseAt += steps * NEWS_STEP_MS;
            msgScrollY += steps;
            redraw = true;
            if (msgScrollY >= maxScroll) {
                msgScrollY = maxScroll;
                msgPhase = MSG_WAIT_END;
                msgPhaseAt = now;
            }
        }
    } else {   // MSG_WAIT_END
        if (now - msgPhaseAt >= 2500) {
            msgReadOnce = true;
            msgScrollY = 0;
            msgPhase = NEWS_WAIT_START;
            msgPhaseAt = now;
            redraw = true;
        }
    }

    if (redraw) {
        drawMessagesScreen(md, idx);
        display.display();
    }
    return redraw;
}

bool DisplayService::isReadingMessage(const AppState& state) const {
    if (currentScreen != SCREEN_MESSAGES || state.messages.count <= 0) return false;
    for (int i = 0; i < state.messages.count; i++) if (!state.messages.items[i].read) return true;
    return !msgReadOnce || msgPaused;
}

void DisplayService::showMessage(const AppState& state, int index) {
    enterScreen(SCREEN_MESSAGES);
    currentSubScreen = (index >= 0 && index < state.messages.count) ? index : 0;
    msgShown = -1;
    drawCurrentScreen(state);
    display.display();
}

// "Te llego un mensaje!": the envelope drops in, opens and the letter comes out
void DisplayService::playMessageAlert() {
    const int ex = 44, ew = 40, eh = 26;   // envelope
    uint32_t start = millis();
    const uint32_t total = 2000;

    while (true) {
        uint32_t t = millis() - start;
        if (t > total) break;
        display.clearDisplay();
        display.setFont();
        display.setTextSize(1);
        display.setTextColor(SH110X_WHITE);

        // 0-450 ms: falls from the top with a small bounce
        int ey;
        if (t < 350) ey = -eh + (int)((6 + eh) * (t / 350.0f));
        else if (t < 450) ey = 6 - (int)(3 * sinf((t - 350) / 100.0f * 3.14159f));
        else ey = 6;

        // 450-850 ms: the flap opens; 850-1300 ms: the letter rises
        float open = t < 450 ? 0 : (t > 850 ? 1 : (t - 450) / 400.0f);
        float rise = t < 850 ? 0 : (t > 1300 ? 1 : (t - 850) / 450.0f);

        // letter (behind the front of the envelope)
        if (rise > 0) {
            int ly = ey + 4 - (int)(14 * rise);
            display.fillRect(ex + 5, ly, ew - 10, 20, SH110X_BLACK);
            display.drawRect(ex + 5, ly, ew - 10, 20, SH110X_WHITE);
            for (int k = 0; k < 3; k++) display.drawFastHLine(ex + 9, ly + 4 + k * 4, ew - 18 - (k == 2 ? 8 : 0), SH110X_WHITE);
        }
        // envelope body (front pocket)
        int py = ey + 8;
        display.fillRect(ex, py, ew, eh - 8, SH110X_BLACK);
        display.drawRect(ex, ey, ew, eh, SH110X_WHITE);
        display.drawLine(ex, ey + eh - 1, ex + ew / 2, py + 4, SH110X_WHITE);
        display.drawLine(ex + ew - 1, ey + eh - 1, ex + ew / 2, py + 4, SH110X_WHITE);
        // flap: tip moves from down (closed) to up (open)
        int tipY = ey + (int)(eh * 0.55f * (1 - open)) - (int)(10 * open);
        display.drawLine(ex, ey, ex + ew / 2, tipY, SH110X_WHITE);
        display.drawLine(ex + ew - 1, ey, ex + ew / 2, tipY, SH110X_WHITE);

        // text from 450 ms
        if (t > 450) {
            display.setCursor((128 - 11 * 6) / 2, 38);
            display.print("Te llego un");
            display.setCursor((128 - 8 * 6) / 2, 49);
            display.print("mensaje!");
        }
        // little sparkles while the letter rises
        if (t > 900 && ((t / 120) % 2 == 0)) {
            display.drawPixel(ex - 6, ey + 4, SH110X_WHITE);
            display.drawPixel(ex + ew + 5, ey + 2, SH110X_WHITE);
            display.drawPixel(ex - 3, ey + 14, SH110X_WHITE);
            display.drawPixel(ex + ew + 3, ey + 16, SH110X_WHITE);
        }
        display.display();
    }
    // two quick flashes
    for (int k = 0; k < 2; k++) {
        display.invertDisplay(true);
        delay(110);
        display.invertDisplay(false);
        delay(110);
    }
}

// ---------------------------------------------------------------------------
//  Mini C3: intro (4.5 s), like a product presentation:
//   0.0-0.5  a light sweeps in
//   0.3-1.5  the console is traced line by line, then its parts pop in
//   1.5-2.7  it spins once (turntable)
//   2.7-3.3  its screen lights up and a shine crosses it
//   3.2-3.7  it slides to the left
//   3.5-4.5  "MINI C3" / "FINELLI RUSTIC"
// ---------------------------------------------------------------------------

// Draws the console centered at cx, 44 px tall. width = 26 * f (f < 0 = back side)
static void drawConsole(MiniOled& d, int cx, float f, float parts, int screenBars, int shineX) {
    const int H = 44, W = 26;
    int w = (int)(W * fabsf(f));
    int y0 = 32 - H / 2;
    if (w < 2) {   // edge-on: just the side
        d.fillRect(cx - 1, y0 + 1, 3, H - 2, SH110X_WHITE);
        return;
    }
    int x0 = cx - w / 2;
    d.fillRoundRect(x0, y0, w, H, min(4, w / 3), SH110X_BLACK);
    d.drawRoundRect(x0, y0, w, H, min(4, w / 3), SH110X_WHITE);
    // thickness on the side that turns away
    if (fabsf(f) < 0.95f) {
        int t = (int)(4 * (1 - fabsf(f)));
        int sx = (f > 0) ? x0 + w : x0 - t;
        d.drawRect(sx, y0 + 2, t + 1, H - 4, SH110X_WHITE);
    }
    if (f < 0) {   // back: lid with the two ports and the snap slot
        d.drawRoundRect(x0 + 3, y0 + 3, w - 6, H - 6, 2, SH110X_WHITE);
        if (w > 12) {
            d.fillRect(cx - w / 4 - 1, y0 + H - 2, 3, 2, SH110X_WHITE);
            d.fillRect(cx + w / 4 - 1, y0 + H - 2, 3, 2, SH110X_WHITE);
        }
        return;
    }
    if (parts <= 0) return;
    float sc = (float)w / W;   // features squeeze with the spin
    // screen
    if (parts > 0.0f) {
        int sw = (int)((W - 6) * sc), sh = 15;
        d.drawRect(cx - sw / 2, y0 + 4, sw, sh, SH110X_WHITE);
        for (int k = 0; k < screenBars && k < 4; k++) {
            int bw = (int)((W - 12 - k * 3) * sc);
            if (bw > 0) d.drawFastHLine(cx - sw / 2 + 3, y0 + 7 + k * 3, bw, SH110X_WHITE);
        }
    }
    // d-pad
    if (parts > 0.35f) {
        int dcx = cx - (int)(4 * sc), dcy = y0 + 31;
        int a = max(1, (int)(4 * sc));
        d.fillRect(dcx - a, dcy - 1, 2 * a + 1, 3, SH110X_WHITE);
        d.fillRect(dcx - 1, dcy - 4, 3, 9, SH110X_WHITE);
    }
    // A button
    if (parts > 0.7f) d.fillCircle(cx + (int)(7 * sc), y0 + 26, 2, SH110X_WHITE);
    // shine sweeping across
    if (shineX > -100) {
        for (int k = 0; k < 3; k++) d.drawLine(shineX + k, y0 + 1, shineX + k - 10, y0 + H - 2, SH110X_INVERSE);
    }
}

void DisplayService::playBootAnimation() {
    const uint32_t total = 4500;
    uint32_t start = millis();

    while (true) {
        uint32_t t = millis() - start;
        if (t > total) break;
        if (t > 300 && digitalRead(PIN_TOUCH) == HIGH) introTouched = true;
        display.clearDisplay();
        display.setFont();
        display.setTextSize(1);
        display.setTextColor(SH110X_WHITE);

        // --- 1) light sweep: a bright bar crosses the screen
        if (t < 500) {
            int x = -20 + (int)(168 * (t / 500.0f));
            for (int k = 0; k < 6; k++) display.drawFastVLine(x + k * 2, 0, 64, SH110X_WHITE);
        }

        // console position (slides left from 3.2 s)
        float m = t < 3200 ? 0.0f : (t > 3700 ? 1.0f : (t - 3200) / 500.0f);
        m = m * m * (3 - 2 * m);
        int cx = 64 - (int)(43 * m);

        // --- 2) tracing the outline (0.3-1.0 s), then the parts (1.0-1.5 s)
        if (t >= 300 && t < 1000) {
            float p = (t - 300) / 700.0f;          // fraction of the perimeter drawn
            const int W = 26, H = 44;
            int x0 = cx - W / 2, y0 = 32 - H / 2;
            int per = 2 * (W + H);
            int len = (int)(per * p);
            // walk the rectangle clockwise from the top-left corner
            for (int s2 = 0; s2 < len; s2++) {
                int px, py;
                if (s2 < W) { px = x0 + s2; py = y0; }
                else if (s2 < W + H) { px = x0 + W - 1; py = y0 + (s2 - W); }
                else if (s2 < 2 * W + H) { px = x0 + W - 1 - (s2 - W - H); py = y0 + H - 1; }
                else { px = x0; py = y0 + H - 1 - (s2 - 2 * W - H); }
                display.drawPixel(px, py, SH110X_WHITE);
            }
            // the "pen"
            int s2 = len;
            int px, py;
            if (s2 < W) { px = x0 + s2; py = y0; }
            else if (s2 < W + H) { px = x0 + W - 1; py = y0 + (s2 - W); }
            else if (s2 < 2 * W + H) { px = x0 + W - 1 - (s2 - W - H); py = y0 + H - 1; }
            else { px = x0; py = y0 + H - 1 - (s2 - 2 * W - H); }
            display.fillCircle(px, py, 1, SH110X_WHITE);
        } else if (t >= 1000) {
            float parts = t < 1000 ? 0 : (t > 1500 ? 1.0f : (t - 1000) / 500.0f);

            // --- 3) one spin (1.5-2.7 s), eased
            float f = 1.0f;
            if (t >= 1500 && t < 2700) {
                float u = (t - 1500) / 1200.0f;
                u = u * u * (3 - 2 * u);
                f = cosf(u * 2.0f * 3.14159f);
            }

            // --- 4) screen lights up (2.7 s on) + shine (2.8-3.2 s)
            int bars = 0;
            if (t >= 2700) bars = min(4, (int)((t - 2700) / 90) + 1);
            int shineX = -1000;
            if (t >= 2800 && t < 3200) shineX = cx - 20 + (int)(50 * ((t - 2800) / 400.0f));

            drawConsole(display, cx, f, parts, bars, shineX);

            // little sparkles around while it spins
            if (t >= 1500 && t < 2700 && ((t / 100) % 2 == 0)) {
                display.drawPixel(cx - 22, 14, SH110X_WHITE);
                display.drawPixel(cx + 21, 20, SH110X_WHITE);
                display.drawPixel(cx - 19, 48, SH110X_WHITE);
                display.drawPixel(cx + 24, 44, SH110X_WHITE);
            }
            // floor shadow under the console
            int sw = (int)(30 * (0.6f + 0.4f * fabsf(f)));
            display.drawFastHLine(cx - sw / 2, 57, sw, SH110X_WHITE);
            display.drawFastHLine(cx - sw / 2 + 4, 59, sw - 8, SH110X_WHITE);
        }

        // --- 5) the name
        if (t >= 3500) {
            const char* l1 = "MINI C3";
            int letters = min(7, (int)((t - 3500) / 60) + 1);
            display.setTextSize(2);
            for (int k = 0; k < letters; k++) {
                if (l1[k] == ' ') continue;
                display.setCursor(44 + k * 12, 8);
                display.write((uint8_t)l1[k]);
            }
            display.setTextSize(1);
            // line that grows under the title
            if (t >= 3800) {
                int lw = min(84, (int)((t - 3800) / 200.0f * 84));
                display.drawFastHLine(44, 27, lw, SH110X_WHITE);
            }
            // FINELLI rises into place, centered under the title
            if (t >= 3900) {
                float u = min(1.0f, (t - 3900) / 300.0f);
                int y = 44 - (int)(12 * u);
                display.setCursor(44 + (84 - 7 * 6) / 2, y);
                display.print("FINELLI");
            }
            // final shine over the whole name
            if (t >= 4150 && t < 4450) {
                int sxp = 36 + (int)((t - 4150) / 300.0f * 100);
                for (int d = 0; d < 4; d++) display.drawLine(sxp + d, 6, sxp + d - 8, 42, SH110X_INVERSE);
            }
            if (t >= 4250) {
                display.setCursor(68, 54);
                display.print("v" FW_VERSION);
            }
        }
        display.display();
    }
}

// ---------------------------------------------------------------------------
//  Mini C3: Spotify screen
// ---------------------------------------------------------------------------

static String fmtTime(uint32_t ms) {
    uint32_t s = ms / 1000;
    char b[12];
    snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    return b;
}

// Spotify-like logo: filled circle with three dark arcs
static void drawSpotifyLogo(MiniOled& d, int cx, int cy, int r) {
    d.fillCircle(cx, cy, r, SH110X_WHITE);
    if (r >= 8) {
        for (int k = 0; k < 3; k++) {
            int yy = cy - r / 3 + k * (r / 3);
            int half = r * (7 - k * 2) / 10;
            for (int t = 0; t <= (r >= 12 ? 1 : 0); t++) {
                d.drawLine(cx - half, yy + t, cx, yy - r / 8 + t, SH110X_BLACK);
                d.drawLine(cx, yy - r / 8 + t, cx + half, yy + 1 + t, SH110X_BLACK);
            }
        }
    } else {
        d.drawFastHLine(cx - 2, cy - 1, 5, SH110X_BLACK);
        d.drawFastHLine(cx - 2, cy + 1, 4, SH110X_BLACK);
    }
}

// Text that scrolls sideways when it does not fit (x0..127)
static void drawMarquee(MiniOled& d, const String& text, int x0, int y, uint32_t now, uint32_t phase) {
    int avail = 128 - x0;
    int w = text.length() * 6;
    if (w <= avail) {
        d.setCursor(x0, y);
        d.print(text);
        return;
    }
    const int gap = 30;
    int cycle = w + gap;
    uint32_t t = (now - phase) % (1500 + cycle * 35);
    int off = t < 1500 ? 0 : (t - 1500) / 35;
    d.setCursor(x0 - off, y);
    d.print(text);
    d.setCursor(x0 - off + cycle, y);
    d.print(text);
}

void DisplayService::drawSpotifyScreen(const SpotifyState& sp) {
    display.clearDisplay();
    display.setFont();
    display.setTextSize(1);
    display.setTextWrap(false);
    display.setTextColor(SH110X_WHITE);
    uint32_t now = millis();

    // --- not ready: big logo + message
    if (sp.status != SP_ACTIVE) {
        drawSpotifyLogo(display, 18, 32, 14);
        const char* l1 = "";
        String l2 = "", l3 = "";
        switch (sp.status) {
            case SP_NOT_CONFIGURED: l1 = "Spotify"; l2 = "Vinculalo en"; l3 = String(DEVICE_HOSTNAME) + ".local/spotify"; break;
            case SP_CONNECTING: {
                l1 = "Conectando";
                int dots = (now / 300) % 4;
                l2 = String("....").substring(0, dots);
                break;
            }
            case SP_IDLE: l1 = "Nada sonando"; l2 = "Pone play en tu"; l3 = "celu o compu"; break;
            default: l1 = "Spotify"; l2 = oledAscii(sp.error).substring(0, 15); l3 = "Reintentando..."; break;
        }
        display.setCursor(38, 16);
        display.print(l1);
        display.setCursor(38, 30);
        display.print(l2);
        if (l3.length() > 15) {   // the setup address does not fit next to the logo
            display.setFont(&Picopixel);
            display.setCursor(38, 48);
            display.print(l3);
            display.setFont();
        } else {
            display.setCursor(38, 40);
            display.print(l3);
        }
        return;
    }

    // --- playing / paused
    uint32_t prog = sp.progressMs;
    if (sp.playing) prog += now - sp.fetchedAt;
    if (sp.durationMs && prog > sp.durationMs) prog = sp.durationMs;

    // title + artist (marquee) + album, to the right of the cover
    drawMarquee(display, oledAscii(sp.title), 36, 13, now, spTrackShownAt);
    drawMarquee(display, oledAscii(sp.artist), 36, 24, now, spTrackShownAt + 700);
    display.setFont(&Picopixel);
    String album = oledAscii(sp.album);
    if (album.length() > 23) album = album.substring(0, 22) + "..";
    display.setCursor(36, 40);
    display.print(album);
    display.setFont();

    // header (drawn over the marquee lines' overflow)
    display.fillRect(0, 0, 128, 11, SH110X_BLACK);
    drawSpotifyLogo(display, 4, 4, 4);
    String dev = oledAscii(sp.device);
    if (dev.length() > 15) dev = dev.substring(0, 15);
    display.setCursor(11, 1);
    display.print(dev);
    if (sp.playing) {
        // equalizer
        for (int k = 0; k < 4; k++) {
            float v = 0.5f + 0.5f * sinf(now / (110.0f + k * 37) + k * 1.7f);
            int h = 2 + (int)(v * 7);
            display.fillRect(112 + k * 4, 9 - h, 3, h, SH110X_WHITE);
        }
    } else {
        display.fillRect(116, 1, 3, 8, SH110X_WHITE);
        display.fillRect(121, 1, 3, 8, SH110X_WHITE);
    }

    // cover (or a vinyl that spins while playing)
    display.fillRect(0, 11, 35, 36, SH110X_BLACK);
    if (sp.artValid && sp.artTrack == sp.trackId) {
        display.drawBitmap(0, 12, sp.art, 32, 32, SH110X_WHITE);
    } else {
        display.drawCircle(16, 28, 15, SH110X_WHITE);
        display.drawCircle(16, 28, 10, SH110X_WHITE);
        display.fillCircle(16, 28, 3, SH110X_WHITE);
        float a = sp.playing ? now / 200.0f : 0;
        display.drawLine(16 + (int)(5 * cosf(a)), 28 + (int)(5 * sinf(a)), 16 + (int)(14 * cosf(a)), 28 + (int)(14 * sinf(a)), SH110X_WHITE);
    }

    // progress bar
    int px = sp.durationMs ? (int)((uint64_t)prog * 125 / sp.durationMs) : 0;
    display.drawFastHLine(0, 50, 128, SH110X_WHITE);
    display.fillRect(0, 49, px + 1, 3, SH110X_WHITE);
    display.fillCircle(min(px + 1, 125), 50, 2, SH110X_WHITE);

    // times + shuffle / repeat in the middle (or the last error for a while)
    display.setCursor(0, 56);
    display.print(fmtTime(prog));
    String total = fmtTime(sp.durationMs);
    display.setCursor(128 - total.length() * 6, 56);
    display.print(total);
    if (sp.error.length() && now - spErrorAt < 3500) {
        display.setFont(&Picopixel);
        String e = oledAscii(sp.error);
        display.setCursor(64 - e.length() * 2, 61);
        display.print(e);
        display.setFont();
    } else {
        if (sp.shuffle) {   // two crossing arrows
            display.drawLine(50, 57, 57, 62, SH110X_WHITE);
            display.drawLine(50, 62, 57, 57, SH110X_WHITE);
            display.drawPixel(56, 57, SH110X_WHITE); display.drawPixel(56, 62, SH110X_WHITE);
        }
        if (sp.repeat != "off") {   // loop
            display.drawRoundRect(64, 57, 12, 6, 2, SH110X_WHITE);
            display.drawPixel(66, 56, SH110X_WHITE);
            if (sp.repeat == "track") { display.setFont(&Picopixel); display.setCursor(78, 62); display.print("1"); display.setFont(); }
        }
    }

    // --- overlays
    // big icon that pops in (play / pause / next / previous)
    if (spPopIcon && now - spPopAt < 550) {
        uint32_t t = now - spPopAt;
        int r = t < 120 ? 8 + t / 10 : 20;
        if (t > 400) r = 20 - (t - 400) / 15;
        int cx = 64, cy = 30;
        display.fillCircle(cx, cy, r + 2, SH110X_BLACK);
        display.fillCircle(cx, cy, r, SH110X_WHITE);
        int s = r / 2;
        switch (spPopIcon) {
            case 1: display.fillTriangle(cx - s / 2, cy - s, cx - s / 2, cy + s, cx + s, cy, SH110X_BLACK); break;
            case 2: display.fillRect(cx - s / 2 - 2, cy - s, s / 2 + 1, 2 * s, SH110X_BLACK);
                    display.fillRect(cx + 2, cy - s, s / 2 + 1, 2 * s, SH110X_BLACK); break;
            case 3: display.fillTriangle(cx - s, cy - s, cx - s, cy + s, cx, cy, SH110X_BLACK);
                    display.fillTriangle(cx, cy - s, cx, cy + s, cx + s, cy, SH110X_BLACK);
                    display.fillRect(cx + s, cy - s, 2, 2 * s + 1, SH110X_BLACK); break;
            case 4: display.fillTriangle(cx + s, cy - s, cx + s, cy + s, cx, cy, SH110X_BLACK);
                    display.fillTriangle(cx, cy - s, cx, cy + s, cx - s, cy, SH110X_BLACK);
                    display.fillRect(cx - s - 1, cy - s, 2, 2 * s + 1, SH110X_BLACK); break;
        }
    }
    // volume
    if (spVolumeMode) {
        display.fillRoundRect(14, 14, 100, 36, 4, SH110X_BLACK);
        display.drawRoundRect(14, 14, 100, 36, 4, SH110X_WHITE);
        // speaker
        display.fillRect(20, 26, 4, 6, SH110X_WHITE);
        display.fillTriangle(23, 29, 29, 22, 29, 36, SH110X_WHITE);
        int v = max(0, spVolumeShown);
        for (int k = 0; k < 3; k++) if (v > k * 33) display.drawCircleHelper(29, 29, 4 + k * 3, 0x2 | 0x4, SH110X_WHITE);
        display.setCursor(44, 19);
        display.print("VOLUMEN");
        String vs = String(v) + "%";
        display.setCursor(108 - vs.length() * 6, 19);
        display.print(vs);
        display.drawRect(44, 32, 64, 8, SH110X_WHITE);
        display.fillRect(46, 34, (60 * v) / 100, 4, SH110X_WHITE);
        display.setFont(&Picopixel);
        display.setCursor(44, 47);
        display.print("SUBIR/BAJAR  A: LISTO");
        display.setFont();
    }
}

bool DisplayService::tickSpotify(const AppState& state) {
    if (currentScreen != SCREEN_SPOTIFY || detailPage != 0) return false;
    const SpotifyState& sp = state.spotify;
    uint32_t now = millis();

    if (sp.error != spLastError) {
        spLastError = sp.error;
        spErrorAt = now;
    }
    if (spVolumeMode && now - spVolumeAt > 3000) spVolumeMode = false;

    // new song: slide it in (from the right, or from the left after ←)
    if (sp.status == SP_ACTIVE && sp.trackId != spDrawnTrack) {
        bool animate = spDrawnTrack.length() > 0 && state.config.anim_mask != 0;
        spDrawnTrack = sp.trackId;
        spTrackShownAt = now;
        if (animate) {
            memcpy(screenBufferOld, display.getBuffer(), 1024);
            drawSpotifyScreen(sp);
            memcpy(screenBufferNew, display.getBuffer(), 1024);
            bool fromLeft = spSlideDir < 0;
            for (int offset = 16; offset <= 128; offset += 16) {
                uint8_t* buf = display.getBuffer();
                for (int page = 0; page < 8; page++) {
                    int start = page * 128;
                    if (!fromLeft) {
                        if (offset < 128) memcpy(&buf[start], &screenBufferOld[start + offset], 128 - offset);
                        memcpy(&buf[start + (128 - offset)], &screenBufferNew[start], offset);
                    } else {
                        if (offset < 128) memcpy(&buf[start + offset], &screenBufferOld[start], 128 - offset);
                        memcpy(&buf[start], &screenBufferNew[start + (128 - offset)], offset);
                    }
                }
                display.display();
            }
            spSlideDir = 1;
            spLastDraw = now;
            return true;
        }
    }

    // ~16 fps while something moves, slower when still
    bool moving = (sp.status == SP_ACTIVE && sp.playing) || sp.status == SP_CONNECTING || spVolumeMode || (spPopIcon && now - spPopAt < 600);
    uint32_t every = moving ? 60 : 400;
    if (now - spLastDraw < every) return false;
    spLastDraw = now;
    drawSpotifyScreen(sp);
    display.display();
    return true;
}

void DisplayService::spotifyPop(uint8_t icon, int slideDir) {
    spPopIcon = icon;
    spPopAt = millis();
    if (slideDir) spSlideDir = slideDir;
    spLastDraw = 0;
}

void DisplayService::spotifyVolume(bool on, int value) {
    spVolumeMode = on;
    spVolumeAt = millis();
    if (value >= 0) spVolumeShown = value;
    spLastDraw = 0;
}
