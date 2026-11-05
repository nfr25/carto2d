#pragma once

#include "core/attributes.hpp"   // render2d: Value

#include <charconv>
#include <string>
#include <variant>

namespace e2d {

// A value as the panels show it: NULL, 120, 412.6, a text (§24, §60).
inline std::string valueText(const r2d::Value& v)
{
    if (std::holds_alternative<std::monostate>(v))
        return "NULL";
    if (const auto* i = std::get_if<long long>(&v))
        return std::to_string(*i);
    if (const auto* d = std::get_if<double>(&v)) {
        // §64: the shortest text that gives back exactly this number:
        // 5000070 (not %g's 5.00007e+06, which lost digits), 412.6, 0.1.
        char text[64];
        const auto r = std::to_chars(text, text + sizeof text, *d);
        return std::string(text, r.ptr);
    }
    return std::get<std::string>(v);
}

} // namespace e2d
