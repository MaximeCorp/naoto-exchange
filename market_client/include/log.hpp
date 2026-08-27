#pragma once
//
// log.hpp
//
// Minimal tagged stderr logging, unconditionally on. Added specifically
// so the trading-connection handshake can be debugged in isolation --
// previously CDP polling noise (see client_details_poller.hpp /
// main.cpp) was drowning it out. Comment out the DASHBOARD_LOG body (or
// wrap it in an #ifdef) if it gets too noisy once things are working.
//

#include <cstdint>
#include <cstdio>
#include <string>

#define DASHBOARD_LOG(tag, fmt, ...)                                           \
    std::fprintf(stderr, "[%s] " fmt "\n", tag, ##__VA_ARGS__)

inline std::string hex_dump(const uint8_t *data, size_t len)
{
    static const char *hex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 3);
    for (size_t i = 0; i < len; ++i)
    {
        out += hex[data[i] >> 4];
        out += hex[data[i] & 0xF];
        out += ' ';
    }
    return out;
}