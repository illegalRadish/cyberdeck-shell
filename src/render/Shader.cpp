#include "render/Shader.hpp"
#include "render/GL.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace cyberdeck {

Shader::~Shader() {
    destroy();
}

void Shader::destroy() {
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
}

bool Shader::readFile(const std::string& path, std::string& out) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    out = ss.str();
    return true;
}

unsigned int Shader::compile(unsigned int type, const std::string& source) {
    const unsigned int shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "Shader compile error: " << log << '\n';
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool Shader::loadFromFiles(const std::string& vertPath, const std::string& fragPath) {
    destroy();

    std::string vertSrc;
    std::string fragSrc;
    if (!readFile(vertPath, vertSrc)) {
        std::cerr << "Failed to read vertex shader: " << vertPath << '\n';
        return false;
    }
    if (!readFile(fragPath, fragSrc)) {
        std::cerr << "Failed to read fragment shader: " << fragPath << '\n';
        return false;
    }

    const unsigned int vs = compile(GL_VERTEX_SHADER, vertSrc);
    const unsigned int fs = compile(GL_FRAGMENT_SHADER, fragSrc);
    if (vs == 0 || fs == 0) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);

    glDeleteShader(vs);
    glDeleteShader(fs);

    int success = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        std::cerr << "Shader link error: " << log << '\n';
        destroy();
        return false;
    }
    return true;
}

void Shader::use() const {
    glUseProgram(program_);
}

void Shader::setFloat(const char* name, float value) const {
    const int loc = glGetUniformLocation(program_, name);
    if (loc >= 0) {
        glUniform1f(loc, value);
    }
}

void Shader::setVec2(const char* name, float x, float y) const {
    const int loc = glGetUniformLocation(program_, name);
    if (loc >= 0) {
        glUniform2f(loc, x, y);
    }
}

void Shader::setVec4(const char* name, float x, float y, float z, float w) const {
    const int loc = glGetUniformLocation(program_, name);
    if (loc >= 0) {
        glUniform4f(loc, x, y, z, w);
    }
}

void Shader::setInt(const char* name, int value) const {
    const int loc = glGetUniformLocation(program_, name);
    if (loc >= 0) {
        glUniform1i(loc, value);
    }
}

}  // namespace cyberdeck
