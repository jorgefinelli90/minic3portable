#ifndef STOCK_SERVICE_H
#define STOCK_SERVICE_H

#include "structs.h"

class StockService {
public:
    bool fetchStock(const String& symbol, StockData &data);

private:
    static constexpr const char* STOCK_API_URLS[2] = {
        "https://query1.finance.yahoo.com/v8/finance/chart/",
        "https://query2.finance.yahoo.com/v8/finance/chart/"
    };
};

#endif