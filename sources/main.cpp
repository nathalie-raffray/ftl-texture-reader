#include "glad/glad.h"

#include "glfw/glfw3.h"

#include "vfs.hpp"

#include "glm/glm.hpp"

#include "nlohmann/json.hpp"

#include "ftl/foundation/serialization/uuid.hpp"
#include "ftl/foundation/serialization/glm_json.hpp"

#include "ftl/core/assets/texture_description.hpp"

#include <iostream>
#include <fstream>

#include "bc7decomp.cpp"
#define RGBCX_IMPLEMENTATION
#include "rgbcx.h"

#define ASSERT(x) if(!(x)) __debugbreak();
#define GLCall(x) GLClearError();\
    x;\
    ASSERT(GLLogCall(#x, __FILE__, __LINE__));

//--------------------------------------------------------------------------------------------------
uint64_t getDecompressedSize(const ftl::texture_format format, const uint64_t numPixels)
{
    switch (format)
    {
    case ftl::texture_format::bc1:
    case ftl::texture_format::bc4:
        return numPixels * 2;
    default:
        return numPixels;
    }
}

//--------------------------------------------------------------------------------------------------
GLenum getInternalFormat(const ftl::texture2d_description &textureDescription)
{
    // The issue here is that bc7 can be either rgb or rgba. 
    switch (textureDescription.format)
    {
    case ftl::texture_format::bc6:
        return GL_RGB;
    default:
        return GL_RGBA;
    }
}

//--------------------------------------------------------------------------------------------------
std::pair<uint16_t, uint16_t> getWindowDimensions(const ftl::texture2d_description &textureDescription)
{
    auto dimensions = std::pair<uint16_t, uint16_t>();

    auto width = textureDescription.mips[0].dimension[0];
    auto height = textureDescription.mips[0].dimension[1];
    
    // let n = width.
    // sum of 2^i (from i = 0 to i = logn) = 2^(logn + 1) - 1
    //                                     = (2^logn * 2) - 1
    //                                      = (n * 2) -1
    dimensions.first = width * 2 - 1;
    dimensions.second = height;

    return dimensions;
}

//---------------------------------------------------------------------------------------------
void error_callback(int error, const char *description)
{
    fprintf(stderr, "Error: %s\n", description);
}


//---------------------------------------------------------------------------------------------
void draw_image()
{

}

//---------------------------------------------------------------------------------------------
static void GLClearError()
{
    while (glGetError() != GL_NO_ERROR);
}

//---------------------------------------------------------------------------------------------
static bool GLLogCall(const char *function, const char *file, int line)
{
    while (GLenum error = glGetError())
    {
        std::cout << "[OpenGL Error] (" << error << "): " << function << " " << file << ": " << line << std::endl;
        return false;
    }
    return true;
}

//---------------------------------------------------------------------------------------------
unsigned int compileShader(const std::string &source, unsigned int type)
{
    GLCall(unsigned int id = glCreateShader(type));
    const char* src = source.c_str();
    GLCall(glShaderSource(id, 1, &src, nullptr));
    GLCall(glCompileShader(id));

    int result;
    GLCall(glGetShaderiv(id, GL_COMPILE_STATUS, &result));
    if (result == GL_FALSE)
    {
        // Shader did not compile successfully. 
        int length;
        GLCall(glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length));
        char *message = (char *)alloca(length * sizeof(char));
        GLCall(glGetShaderInfoLog(id, length, &length, message));
        std::cout << "Failed to compile " << (type == GL_VERTEX_SHADER ? "vertex" : "fragment") << " shader." << std::endl;
        std::cout << message << std::endl;
        GLCall(glDeleteShader(id));
        return 0;
    }

    return id;
}

//---------------------------------------------------------------------------------------------
unsigned int createShader(const std::string &vertexShader, const std::string &fragmentShader)
{
    GLCall(unsigned int program = glCreateProgram());
    unsigned int vs = compileShader(vertexShader, GL_VERTEX_SHADER);
    // if vs == 0, then exit...
    unsigned int fs = compileShader(fragmentShader, GL_FRAGMENT_SHADER);

    // Bind ourTexture to texture unit 0
    GLCall(glUniform1i(glGetUniformLocation(fs, "ourTexture"), 0));

    GLCall(glAttachShader(program, vs));
    GLCall(glAttachShader(program, fs));
    GLCall(glLinkProgram(program));
    GLCall(glValidateProgram(program));

    // delete obj files.
    GLCall(glDeleteShader(vs));
    GLCall(glDeleteShader(fs));

    return program;
}

//---------------------------------------------------------------------------------------------
std::vector<float> getVertices(const ftl::texture2d_description &textureDescription)
{
    // Something like this.
    //float vertices[] = {
    //    // positions         // texture coords
    //     0.0f,  1.0f, 0.0f,   1.0f, 1.0f,   // top right
    //     0.0f, -1.0f, 0.0f,   1.0f, 0.0f,   // bottom right
    //    -1.0f, -1.0f, 0.0f,   0.0f, 0.0f,   // bottom left
    //    -1.0f,  1.0f, 0.0f,   0.0f, 1.0f    // top left 
    //};

    auto windowDimensions = getWindowDimensions(textureDescription);

    auto vertices = std::vector<float>();
    auto numVertices = textureDescription.mips.size() * 4;

    auto topLeftX = -1.0f;
    auto topLeftY = 1.0f;

    for (auto i = 0; i < textureDescription.mips.size(); ++i)
    {
        auto currentMip = textureDescription.mips[i];
        auto mipViewportWidth = (currentMip.dimension.x / windowDimensions.first) * 2;
        auto mipViewportHeight = (currentMip.dimension.y / windowDimensions.second) * 2;

        // top left
        //viewport positions
        vertices.push_back(topLeftX);
        vertices.push_back(topLeftY);
        vertices.push_back(0);

        //texture coords
        vertices.push_back(0);
        vertices.push_back(1);

        // bottom left
        //viewport positions
        vertices.push_back(topLeftX);
        vertices.push_back(topLeftY - mipViewportHeight);
        vertices.push_back(0);

        //texture coords
        vertices.push_back(0);
        vertices.push_back(1);

        // top right
        //viewport positions
        vertices.push_back(topLeftX + mipViewportWidth);
        vertices.push_back(topLeftY);
        vertices.push_back(0);

        //texture coords
        vertices.push_back(1);
        vertices.push_back(1);

        // bottom right
        //viewport positions
        vertices.push_back(topLeftX + mipViewportWidth);
        vertices.push_back(topLeftY - mipViewportHeight);
        vertices.push_back(0);

        //texture coords
        vertices.push_back(1);
        vertices.push_back(0);

        //update topLeftX and topLeftY
        topLeftX += mipViewportWidth;
        topLeftY /= 2;
    }

    // Print vertices.
    return vertices;
}

//---------------------------------------------------------------------------------------------
std::vector<uint32_t> getIndices(const ftl::texture2d_description &textureDescription)
{
    auto indices = std::vector<uint32_t>();

    // Something like this:
    //unsigned int indices[] = { 
    //    0, 1, 2,   // first triangle
    //    1, 3, 2    // second triangle
    //};

    for (auto i = 0; i < textureDescription.mips.size(); ++i)
    {
        // For each mip, push 6 indices (for two triangles)
        auto firstIndex = i * 4;
        indices.emplace_back(firstIndex);
        indices.emplace_back(firstIndex+1);
        indices.emplace_back(firstIndex+2);
        indices.emplace_back(firstIndex+1);
        indices.emplace_back(firstIndex+3);
        indices.emplace_back(firstIndex+2);
    }

    // Print indices.
    return indices;
}

//---------------------------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    if (argc != 1)
    {
        std::cout << "Wrong number of arguments." << std::endl;
        return -1;
    }
    
    auto textureUUID = ftl::uuid(argv[0]);

    // Read all mip payloads w/its description.B

    // Get description. 
    auto descFilePath = "texture.description." + textureUUID.toString();
    auto spFile = vfs::open_read_only(descFilePath, vfs::file_creation_options::open_if_existing);

    if (!spFile || !spFile->isValid())
    {
        std::cout << "Could not open description." << std::endl;
        return -1;
    }

    auto textureDescription = ftl::texture2d_description();

    try
    {
        auto jStr = std::string();
        spFile->read(jStr);
        auto j = nlohmann::json::parse(jStr);
        textureDescription = j;
    }
    catch (nlohmann::json::exception e)
    {
        std::cout << "Error in parsing description: " << e.what() << std::endl;
        return -1;
    }

    if (!glfwInit())
    {
        // Initialization failed
        std::cout << "glfw Initialization failed." << std::endl;
        return -1;
    }

    glfwSetErrorCallback(error_callback);

    // Make window
    auto dimensions = getWindowDimensions(textureDescription);
    GLFWwindow *window = glfwCreateWindow(dimensions.first, dimensions.second, "My mom", NULL, NULL);

    if (!window)
    {
        std::cout << "Window or OpenGL context creation failed." << std::endl;
        glfwTerminate();
        return -1;
    }

    // Make context.
    glfwMakeContextCurrent(window);

    // Clear screen. 
    GLCall(glClear(GL_COLOR_BUFFER_BIT));

    // Make texture.
    unsigned int texture;
    GLCall(glGenTextures(1, &texture));
    // Attach 'texture' to the texture unit number 0. 
    GLCall(glActiveTexture(GL_TEXTURE0));
    GLCall(glBindTexture(GL_TEXTURE_2D, texture));

    // set the texture wrapping/filtering options (on the currently bound texture object)
    GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    
    for (auto i = 0; i < textureDescription.mips.size(); ++i)
    {
        // Get mip payload. 
        auto payloadFilePath = "texture.payload.mip" + i + std::string(".") + textureUUID.toString();
        auto spFileView = vfs::open_read_only_view(payloadFilePath, vfs::file_creation_options::open_if_existing);

        if (!spFileView || !spFileView->isValid())
        {
            std::cout << "Could not open payload." << std::endl;
            glfwDestroyWindow(window);
            glfwTerminate();
            return -1;
        }

        std::vector<uint8_t> pPixels;
        auto width = textureDescription.mips[i].dimension[0];
        auto height = textureDescription.mips[i].dimension[1];

        pPixels.resize(getDecompressedSize(textureDescription.format, height * width));

        // Need to specify mode for bc7.
        switch (textureDescription.format)
        {
        case ftl::texture_format::bc1:
            rgbcx::unpack_bc1(spFileView->cursor<void>(), (void *)pPixels.data());
            break;
        case ftl::texture_format::bc3:
            rgbcx::unpack_bc3(spFileView->cursor<void>(), (void *)pPixels.data());
            break;
        case ftl::texture_format::bc4:
            rgbcx::unpack_bc4(spFileView->cursor<void>(), pPixels.data());
            break;
        case ftl::texture_format::bc5:
            rgbcx::unpack_bc5(spFileView->cursor<void>(), (void *)pPixels.data());
            break;
        case ftl::texture_format::bc7:
            std::cout << "Can't decode bc7 yet." << std::endl;
            glfwDestroyWindow(window);
            glfwTerminate();
            return -1;
            break;
        }

        // rgb ? rgba ?
        // https://learnopengl.com/Getting-started/Textures
        // Set each mip map level manually. 
        // i corresponds to mip level.
        GLCall(glTexImage2D(GL_TEXTURE_2D, i, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, pPixels.data()));
    }

    std::string vertexShader =
        "#version 330"
        "layout(location = 0) in vec3 aPos;\n"
        "layout(location = 1) in vec2 aTexCoords;\n"
        "\n"
        "out vec2 texCoords;\n"
        "\n"
        "void main()\n"
        "{\n"
        "gl_Position = vec4(aPos, 1.0);\n"
        "texCoords = aTexCoords;\n"
        "}";

    std::string fragmentShader =
        "#version 330"
        "out vec4 FragColor;\n"
        "in vec2 texCoords;\n"
        "\n"
        "uniform sampler2D ourTexture;\n"
        "\n"
        "void main()\n"
        "{\n"
        "FragColor = texture(ourTexture, texCoords);\n"
        "}";

    auto program = createShader(vertexShader, fragmentShader);
    GLCall(glUseProgram(program));

    //float vertices[] = {
    //    // positions         // texture coords
    //     0.0f,  1.0f, 0.0f,   1.0f, 1.0f,   // top right
    //     0.0f, -1.0f, 0.0f,   1.0f, 0.0f,   // bottom right
    //    -1.0f, -1.0f, 0.0f,   0.0f, 0.0f,   // bottom left
    //    -1.0f,  1.0f, 0.0f,   0.0f, 1.0f    // top left 
    //};

    //unsigned int indices[] = {  // note that we start from 0!
    //    0, 1, 3,   // first triangle
    //    1, 2, 3    // second triangle
    //};

    auto vertices = getVertices(textureDescription);
    auto indices = getIndices(textureDescription);

    //Core OpenGL requires that we use a VAO so it knows what to do with our vertex inputs. If we fail to bind a VAO, OpenGL will most likely refuse to draw anything. 
    unsigned int VAO;
    GLCall(glGenVertexArrays(1, &VAO));
    GLCall(glBindVertexArray(VAO));

    // EBO is a buffer that stores indices that OpenGL uses to decide what vertices to draw.
    unsigned int EBO;
    GLCall(glGenBuffers(1, &EBO));
    GLCall(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO));
    GLCall(glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW));

    unsigned int VBO;
    GLCall(glGenBuffers(1, &VBO));
    GLCall(glBindBuffer(GL_ARRAY_BUFFER, VBO));
    GLCall(glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW));

    GLCall(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0));
    GLCall(glEnableVertexAttribArray(0));
    GLCall(glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(3 * sizeof(float))));
    GLCall(glEnableVertexAttribArray(1));

    //Aren't these three already binded...?
    //GLCall(glBindTexture(GL_TEXTURE_2D, texture));
    //GLCall(glBindVertexArray(VAO));
    //GLCall(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO));

   // change this 6 at a point.
    GLCall(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0));

    while (!glfwWindowShouldClose(window));

    // Exit.
    GLCall(glDeleteProgram(program));
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
} 