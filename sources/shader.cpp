#include "shader.hpp"

shader::shader(const std::string &filePath)
    : filePath_(filePath), rendererId_(0)
{
    compileShader();
}

shader::~shader()
{
}

void shader::bind() const
{
}

void shader::unbind() const
{
}

void shader::setUniformScalar2d(const std::string &name, uint32_t texture)
{
}

uint32_t shader::getUniformLocation(const std::string &name)
{
    return uint32_t();
}

bool shader::compileShader()
{
    return false;
}
