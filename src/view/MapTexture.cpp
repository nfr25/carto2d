#include "view/MapTexture.h"

#include <glad/gl.h>

#include <cstdlib>

namespace e2d {

MapTexture::MapTexture()
{
    // §25: the way to hand the bytes over, for the measurement.
    if (const char* v = std::getenv("E2D_UPLOAD"); v && v[0] >= '0' && v[0] <= '3' && v[1] == '\0')
        m_method = v[0] - '0';
}

MapTexture::~MapTexture()
{
    destroy(m_slots[0]);
    destroy(m_slots[1]);
}

void MapTexture::destroy(Slot& slot)
{
    if (slot.id != 0)
        glDeleteTextures(1, &slot.id);
    slot = {};
}

bool MapTexture::upload(const unsigned char* pixels, int width, int height, int stride)
{
    if (!pixels || width <= 0 || height <= 0 || stride < width * 4)
        return false;

    // §25: write into the texture that is NOT shown.
    const int hidden = 1 - m_shown;
    Slot& slot = m_slots[hidden];

    // Not the image's size (or never made): a new texture
    // (glTextureStorage2D fixes the size for good).
    if (slot.id == 0 || width != slot.width || height != slot.height) {
        destroy(slot);
        glCreateTextures(GL_TEXTURE_2D, 1, &slot.id);
        glTextureStorage2D(slot.id, 1, GL_RGBA8, width, height);   // 1 level: no mipmaps
        glTextureParameteri(slot.id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(slot.id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(slot.id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(slot.id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (m_method == 3) {
            // The bytes go in as they are (B in "red", R in "blue"): the card
            // swaps them back each time it reads the texture.
            glTextureParameteri(slot.id, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
            glTextureParameteri(slot.id, GL_TEXTURE_SWIZZLE_B, GL_RED);
        }
        slot.width  = width;
        slot.height = height;
    }

    // §5: the stride in pixels; rows are 4-byte aligned in ARGB32. Methods 1
    // to 3 set it only when the rows are not contiguous (§25).
    const bool rowLength = m_method == 0 || stride != width * 4;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (rowLength)
        glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);

    switch (m_method) {
    case 2:
        glTextureSubImage2D(slot.id, 0, 0, 0, width, height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
        break;
    case 3:
        glTextureSubImage2D(slot.id, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        break;
    default:   // 0, 1
        glTextureSubImage2D(slot.id, 0, 0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
        break;
    }

    if (rowLength)
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);   // back to the default for others

    m_shown = hidden;   // the next frames draw the new image
    return true;
}

} // namespace e2d
