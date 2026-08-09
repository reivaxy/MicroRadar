#pragma once

#include <ArduinoJson.h>
#include <vector>

namespace JsonParser {
    template<typename T>
    T Parse(const JsonVariant& doc);

    template<typename T>
    std::vector<T> ParseArray(const JsonArray& array) {
        std::vector<T> results;
        // Don't reserve() upfront - it tries to allocate one huge contiguous block
        // which fails on fragmented heap. Let vector grow incrementally instead.
        for (JsonVariant item : array) {
            results.push_back(Parse<T>(item));
        }

        return results;
    }

}