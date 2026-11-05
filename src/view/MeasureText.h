#pragma once

#include <cmath>
#include <cstdio>
#include <string>

namespace e2d {

// How a measure is written (notions §72). Pure: tested without a window.

// 1234567.8 -> "1,234,567.8": the thousands grouped, `decimals` kept.
inline std::string groupedNumber(double v, int decimals)
{
    char raw[64];
    std::snprintf(raw, sizeof raw, "%.*f", decimals, std::abs(v));
    std::string s(raw);
    const auto dot = s.find('.');
    std::string whole = s.substr(0, dot), rest = dot == std::string::npos ? "" : s.substr(dot);
    std::string out;
    for (std::size_t i = 0; i < whole.size(); ++i) {
        if (i > 0 && (whole.size() - i) % 3 == 0)
            out += ',';
        out += whole[i];
    }
    return (v < 0 ? "-" : "") + out + rest;
}

// "812.47 m" below a kilometre, "3.251 km" above.
inline std::string lengthText(double metres)
{
    if (metres < 1000.0)
        return groupedNumber(metres, 2) + " m";
    return groupedNumber(metres / 1000.0, 3) + " km";
}

// "652.4 m²" below a hectare, "4.27 ha" to a square kilometre, "12.6 km²" above.
inline std::string areaText(double squareMetres)
{
    if (squareMetres < 10000.0)
        return groupedNumber(squareMetres, 1) + " m²";
    if (squareMetres < 1000000.0)
        return groupedNumber(squareMetres / 10000.0, 2) + " ha";
    return groupedNumber(squareMetres / 1000000.0, 1) + " km²";
}

} // namespace e2d
