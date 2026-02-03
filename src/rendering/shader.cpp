#include "rendering/shader.hpp"
#include "core/logger.hpp"
#include <fstream>
#include <sstream>

namespace wowee {
namespace rendering {

Shader::~Shader() {
    if (program) glDeleteProgram(program);
    if (vertexShader) glDeleteShader(vertexShader);
    if (fragmentShader) glDeleteShader(fragmentShader);
}

bool Shader::loadFromFile(const std::string& vertexPath, const std::string& fragmentPath) {
    // Load vertex shader
    std::ifstream vFile(vertexPath);
    if (!vFile.is_open()) {
        LOG_ERROR("Failed to open vertex shader: ", vertexPath);
        return false;
    }
    std::stringstream vStream;
    vStream << vFile.rdbuf();
    std::string vertexSource = vStream.str();

    // Load fragment shader
    std::ifstream fFile(fragmentPath);
    if (!fFile.is_open()) {
        LOG_ERROR("Failed to open fragment shader: ", fragmentPath);
        return false;
    }
    std::stringstream fStream;
    fStream << fFile.rdbuf();
    std::string fragmentSource = fStream.str();

    return compile(vertexSource, fragmentSource);
}

bool Shader::loadFromSource(const std::string& vertexSource, const std::string& fragmentSource) {
    return compile(vertexSource, fragmentSource);
}

bool Shader::compile(const std::string& vertexSource, const std::string& fragmentSource) {
    GLint success;
    GLchar infoLog[512];

    // Compile vertex shader
    const char* vCode = vertexSource.c_str();
    vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vCode, nullptr);
    glCompileShader(vertexShader);
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
        LOG_ERROR("Vertex shader compilation failed: ", infoLog);
        return false;
    }

    // Compile fragment shader
    const char* fCode = fragmentSource.c_str();
    fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fCode, nullptr);
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
        LOG_ERROR("Fragment shader compilation failed: ", infoLog);
        return false;
    }

    // Link program
    program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        LOG_ERROR("Shader program linking failed: ", infoLog);
        return false;
    }

    return true;
}

void Shader::use() const {
    glUseProgram(program);
}

void Shader::unuse() const {
    glUseProgram(0);
}

GLint Shader::getUniformLocation(const std::string& name) const {
    // Check cache first
    auto it = uniformLocationCache.find(name);
    if (it != uniformLocationCache.end()) {
        return it->second;
    }

    // Look up and cache
    GLint location = glGetUniformLocation(program, name.c_str());
    uniformLocationCache[name] = location;
    return location;
}

void Shader::setUniform(const std::string& name, int value) {
    glUniform1i(getUniformLocation(name), value);
}

void Shader::setUniform(const std::string& name, float value) {
    glUniform1f(getUniformLocation(name), value);
}

void Shader::setUniform(const std::string& name, const glm::vec2& value) {
    glUniform2fv(getUniformLocation(name), 1, &value[0]);
}

void Shader::setUniform(const std::string& name, const glm::vec3& value) {
    glUniform3fv(getUniformLocation(name), 1, &value[0]);
}

void Shader::setUniform(const std::string& name, const glm::vec4& value) {
    glUniform4fv(getUniformLocation(name), 1, &value[0]);
}

void Shader::setUniform(const std::string& name, const glm::mat3& value) {
    glUniformMatrix3fv(getUniformLocation(name), 1, GL_FALSE, &value[0][0]);
}

void Shader::setUniform(const std::string& name, const glm::mat4& value) {
    glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, &value[0][0]);
}

void Shader::setUniformMatrixArray(const std::string& name, const glm::mat4* matrices, int count) {
    glUniformMatrix4fv(getUniformLocation(name), count, GL_FALSE, &matrices[0][0][0]);
}

} // namespace rendering
} // namespace wowee
