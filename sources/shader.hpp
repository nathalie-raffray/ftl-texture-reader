#pragma once

#include <string>

class shader
{
public:
    shader(const std::string &filePath);
    ~shader();

    void bind() const;
    void unbind() const;

    // Set uniforms
    void setUniformScalar2d(const std::string &name, uint32_t texture);

private:
    uint32_t getUniformLocation(const std::string &name);

    bool compileShader();

private:
    uint32_t rendererId_;
    std::string filePath_;
};