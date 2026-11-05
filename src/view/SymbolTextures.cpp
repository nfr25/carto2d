#include "view/SymbolTextures.h"

#include <glad/gl.h>

#include <cstdio>

namespace e2d {

namespace {

// Everything that changes the picture: the image, the square, the size, and
// the fallback mark (shown when the image cannot be read).
std::string keyOf(const LayerSymbol& s, int side, float ratio)
{
    char buffer[256];
    auto colour = [](const std::optional<r2d::Color>& c) {
        return c ? (c->r + 2.0 * c->g + 4.0 * c->b + 8.0 * c->a) : -1.0;
    };
    std::snprintf(buffer, sizeof buffer, "|%d|%.3f|%.3f|%d|%.6f|%.6f|%.3f", side, double(ratio), s.size,
                  static_cast<int>(s.shape), colour(s.fill), colour(s.stroke), s.strokeWidth);
    return s.image + buffer;
}

} // namespace

SymbolTextures::~SymbolTextures()
{
    for (auto& [key, id] : m_textures)
        if (id != 0)
            glDeleteTextures(1, &id);
}

unsigned int SymbolTextures::get(const LayerSymbol& symbol, int side, float ratio)
{
    const std::string key = keyOf(symbol, side, ratio);
    if (const auto it = m_textures.find(key); it != m_textures.end())
        return it->second;

    unsigned int id = 0;
    const std::vector<unsigned char> pixels = symbolPixels(symbol, side, ratio);
    if (!pixels.empty()) {
        glCreateTextures(GL_TEXTURE_2D, 1, &id);
        glTextureStorage2D(id, 1, GL_RGBA8, side, side);
        glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        // B, G, R, A in memory, as the map (§4); rows contiguous.
        glTextureSubImage2D(id, 0, 0, 0, side, side, GL_BGRA, GL_UNSIGNED_BYTE, pixels.data());
    }
    m_textures.emplace(key, id);   // 0 too: not tried again every frame
    return id;
}

} // namespace e2d
