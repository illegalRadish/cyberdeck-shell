#pragma once

#include <string>

struct SDL_Surface;

namespace cyberdeck {

class Texture {
public:
    Texture() = default;
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    bool createFromRGBA(int width, int height, const void* pixels);
    bool createFromSurface(SDL_Surface* surface);
    bool loadFromFile(const std::string& path);
    void destroy();

    void bind(unsigned int unit = 0) const;

    bool valid() const { return id_ != 0; }
    unsigned int id() const { return id_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    unsigned int id_ = 0;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace cyberdeck
