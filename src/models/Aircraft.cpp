#include "models/Aircraft.h"

namespace JsonParser {
    template<>
    Aircraft Parse<Aircraft>(const JsonVariant& state) {
        Aircraft a;

        // Use as<const char*>() to get direct reference to JSON buffer,
        // then assign to String member for single allocation (not temporary + move).
        // This dramatically reduces heap fragmentation on parsing.
        a.icao24 = state[0].isNull() ? "" : state[0].as<const char*>();
        a.callsign = state[1].isNull() ? "" : state[1].as<const char*>();
        a.originCountry = state[2].isNull() ? "" : state[2].as<const char*>();
        a.timePosition = state[3].isNull() ? 0 : state[3].as<long>();
        a.lastContact = state[4].isNull() ? 0 : state[4].as<long>();
        a.longitude = state[5].isNull() ? 0.0f : state[5].as<float>();
        a.latitude = state[6].isNull() ? 0.0f : state[6].as<float>();
        a.baroAltitude = state[7].isNull() ? 0.0f : state[7].as<float>();
        a.onGround = state[8].as<bool>();
        a.velocity = state[9].isNull() ? 0.0f : state[9].as<float>();
        a.trueTrack = state[10].isNull() ? 0.0f : state[10].as<float>();

        return a;
    }

}