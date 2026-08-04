#pragma once

#include <string>

namespace cyberdeck {

class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    bool loadFromFiles(const std::string& vertPath, const std::string& fragPath);
    void destroy();

    void use() const;
    void setFloat(const char* name, float value) const;
    void setVec2(const char* name, float x, float y) const;
    void setVec4(const char* name, float x, float y, float z, float w) const;
    void setInt(const char* name, int value) const;

    unsigned int id() const { return program_; }
    bool valid() const { return program_ != 0; }

private:
    static unsigned int compile(unsigned int type, const std::string& source);
    static bool readFile(const std::string& path, std::string& out);

    unsigned int program_ = 0;
};

}  // namespace cyberdeck
