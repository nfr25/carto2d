#pragma once

// ============================================================================
// MapTexture — the map image on the graphics card (notions §5, §25).
//
// Cairo draws on the processor; OpenGL only SHOWS the result. upload() sends
// the CairoImage's bytes: B, G, R, A in memory on a PC (§4). GL_RGBA8, not
// sRGB: the bytes reach the screen unchanged, same colours as render2d's PNG.
// No mipmaps; GL_LINEAR for the stretched image of step 1b. The first row
// sent is Cairo's top row: no flip, ImGui's uv (0,0)-(1,1).
//
// Two textures, used in turn ("ping-pong", §25): a new image goes into the
// one NOT shown, then becomes the shown one.
//
// Four ways to hand the bytes over (§25, measured 22 October: the call itself
// took 35 ms for 7.1 MB, the graphics card 5.7 ms). Chosen at start by the
// environment variable E2D_UPLOAD, to find the fastest on a given card:
//   0  GL_BGRA, GL_UNSIGNED_BYTE, GL_UNPACK_ROW_LENGTH always set (until 0.3.0)
//   1  the same, GL_UNPACK_ROW_LENGTH only when the rows are not contiguous
//   2  GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV: the same bytes, the "native" name
//   3  GL_RGBA as they are, and the card swaps red and blue when it reads
//      (texture swizzle): no conversion at all
// ============================================================================

namespace e2d {

class MapTexture {
public:
    MapTexture();
    ~MapTexture();

    // Owns GPU objects: no copies (as editor3d's Texture).
    MapTexture(const MapTexture&) = delete;
    MapTexture& operator=(const MapTexture&) = delete;

    // Sends a width x height image whose rows are `stride` bytes apart, into
    // the hidden texture, then shows it. A texture is recreated only when its
    // size differs from the image's.
    bool upload(const unsigned char* pixels, int width, int height, int stride);

    // The texture shown.
    bool         valid() const { return shown().id != 0; }
    unsigned int id() const { return shown().id; }
    int          width() const { return shown().width; }
    int          height() const { return shown().height; }

    int method() const { return m_method; }   // 0 to 3, see above

private:
    struct Slot {
        unsigned int id = 0;
        int width = 0, height = 0;
    };

    const Slot& shown() const { return m_slots[m_shown]; }
    static void destroy(Slot& slot);

    Slot m_slots[2];
    int  m_shown = 0;
    int  m_method = 0;
};

} // namespace e2d
