#pragma once

#include "panels/LayerSymbol.h"

#include <map>
#include <string>

// ============================================================================
// SymbolTextures — the Layers panel's image symbols on the graphics card
// (notions §78). render2d draws the symbol in a small image (symbolPixels,
// the same drawing as the map); it is sent once as a texture and kept, by
// image, size and colours. ImGui shows it with AddImage.
// ============================================================================

namespace e2d {

class SymbolTextures {
public:
    SymbolTextures() = default;
    ~SymbolTextures();

    // Owns GPU objects: no copies.
    SymbolTextures(const SymbolTextures&) = delete;
    SymbolTextures& operator=(const SymbolTextures&) = delete;

    // The texture of `symbol` (with an image) in a `side` pixels square; 0 if
    // it could not be made. Drawn and sent the first time, then taken from
    // the cache.
    unsigned int get(const LayerSymbol& symbol, int side, float ratio);

private:
    std::map<std::string, unsigned int> m_textures;   // key: image, side, size, colours
};

} // namespace e2d
