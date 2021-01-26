#include <algorithm>

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
#include <sstream>

#include "bc7decomp.cpp"
#define RGBCX_IMPLEMENTATION
#include "rgbcx.h"

#define ASSERT(x) if(!(x)) __debugbreak();
#define GLCall(x) GLClearError();\
    x;\
    ASSERT(GLLogCall(#x, __FILE__, __LINE__));

#pragma warning(disable:4996)

//--------------------------------------------------------------------------------------------------
struct color_quad_u8
{
    uint8_t m_c[4];

    inline color_quad_u8(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        set(r, g, b, a);
    }

    inline color_quad_u8(uint8_t y = 0, uint8_t a = 255)
    {
        set(y, a);
    }

    inline color_quad_u8 &set(uint8_t y, uint8_t a = 255)
    {
        m_c[0] = y;
        m_c[1] = y;
        m_c[2] = y;
        m_c[3] = a;
        return *this;
    }

    inline color_quad_u8 &set(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        m_c[0] = r;
        m_c[1] = g;
        m_c[2] = b;
        m_c[3] = a;
        return *this;
    }

    inline uint8_t &operator[] (uint32_t i) { assert(i < 4);  return m_c[i]; }
    inline uint8_t operator[] (uint32_t i) const { assert(i < 4); return m_c[i]; }

    inline int get_luma() const { return (13938U * m_c[0] + 46869U * m_c[1] + 4729U * m_c[2] + 32768U) >> 16U; } // REC709 weightings
};
typedef std::vector<color_quad_u8> color_quad_u8_vec;

//--------------------------------------------------------------------------------------------------
uint64_t getDecompressedSize(const ftl::texture_format format, const uint64_t numPixels)
{
    switch (format)
    {
    case ftl::texture_format::bc1:
        return numPixels * 2 * 4; // 4 bytes per pixel im guessing ?
    case ftl::texture_format::bc3:
        return numPixels * 4; // 4 bytes per pixel im guessing ?
    case ftl::texture_format::bc4:
        return numPixels * 2 * 2; // 2 bytes per pixel ??
    case ftl::texture_format::bc5:
        return numPixels * 4;
    default:
        return numPixels;
    }
}

//--------------------------------------------------------------------------------------------------
GLenum getGLFormat(const ftl::texture2d_description &textureDescription)
{
    // The issue here is that bc7 can be either rgb or rgba. 
    // Not sure about grayscale formats, bc4 and bc5. 
    // Check https://stackoverflow.com/questions/680125/can-i-use-a-grayscale-image-with-the-opengl-glteximage2d-function
    // Will probably need to adapt fragment shader, depending on which format we can expect the texture to be in. 
    switch (textureDescription.format)
    {
    case ftl::texture_format::bc4:
        return GL_RED;
    case ftl::texture_format::bc5:
        return GL_RG;
    case ftl::texture_format::bc6:
        return GL_RGB;
    default:
        return GL_RGBA;
    }
}

//--------------------------------------------------------------------------------------------------
GLenum getGLFormatCompressed(const ftl::texture2d_description &textureDescription)
{
    // The issue here is that bc7 can be either rgb or rgba. 
    // Not sure about grayscale formats, bc4 and bc5. 
    // Check https://stackoverflow.com/questions/680125/can-i-use-a-grayscale-image-with-the-opengl-glteximage2d-function
    // Will probably need to adapt fragment shader, depending on which format we can expect the texture to be in. 
    switch (textureDescription.format)
    {
    case ftl::texture_format::bc1:
        // There is also GL_COMPRESSED_RGB_S3TC_DXT1_EXT if there is no alpha channel. 
        return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
    case ftl::texture_format::bc2:
        // Get fucked.
        __debugbreak;
    case ftl::texture_format::bc3:
        return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    case ftl::texture_format::bc4:
        std::cout << "Doesnt support bc4 yet." << std::endl;
        __debugbreak;
    case ftl::texture_format::bc5:
        std::cout << "Doesnt support bc5 yet." << std::endl;
        __debugbreak;
    case ftl::texture_format::bc6:
        return GL_RG;
        std::cout << "Doesnt support bc6 yet." << std::endl;
        __debugbreak;
    case ftl::texture_format::bc7:
        std::cout << "Doesnt support bc7 yet." << std::endl;
        __debugbreak;
    }

    return GL_INVALID_ENUM;
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
void glfwErrorCallback(int error, const char *description)
{
    fprintf(stderr, "Error: %s\n", description);
}

//---------------------------------------------------------------------------------------------
static void GLClearError()
{
    while (glGetError() != GL_NO_ERROR)
    {
    }
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
    //    -1.0f,  1.0f, 0.0f,   0.0f, 1.0f    // top left 
    //    -1.0f, -1.0f, 0.0f,   0.0f, 0.0f,   // bottom left
    //     1.0f,  1.0f, 0.0f,   1.0f, 1.0f,   // top right
    //     1.0f, -1.0f, 0.0f,   1.0f, 0.0f,   // bottom right
    //};

    auto windowDimensions = getWindowDimensions(textureDescription);

    auto vertices = std::vector<float>();

    auto topLeftX = -1.0f;
    auto topLeftY = 1.0f;

    auto numMips = std::min(1, (int)textureDescription.mips.size());

    for (auto i = 0; i < numMips; ++i)
    {
        auto currentMip = textureDescription.mips[i];
        auto mipViewportWidth = (currentMip.dimension.x / (float)windowDimensions.first) * 2;
        auto mipViewportHeight = (currentMip.dimension.y /(float) windowDimensions.second) * 2;

        // top left:
        //viewport positions
        vertices.push_back(topLeftX);
        vertices.push_back(topLeftY);
        vertices.push_back(0);

        //texture coords
        vertices.push_back(0);
        vertices.push_back(1);

        // bottom left:
        //viewport positions
        vertices.push_back(topLeftX);
        vertices.push_back(topLeftY - mipViewportHeight);
        vertices.push_back(0);

        //texture coords
        vertices.push_back(0);
        vertices.push_back(0);

        // top right:
        //viewport positions
        vertices.push_back(topLeftX + mipViewportWidth);
        vertices.push_back(topLeftY);
        vertices.push_back(0);

        //texture coords
        vertices.push_back(1);
        vertices.push_back(1);

        // bottom right:
        //viewport positions
        vertices.push_back(topLeftX + mipViewportWidth);
        vertices.push_back(topLeftY - mipViewportHeight);
        vertices.push_back(0);

        //texture coords
        vertices.push_back(1);
        vertices.push_back(0);

        //update topLeftX and topLeftY
        topLeftX += mipViewportWidth;
        //topLeftY /= 2;
        topLeftY = -1.0f + mipViewportHeight / 2;
    }

    // Print vertices.
    return vertices;
}

//---------------------------------------------------------------------------------------------
std::vector<uint32_t> getIndices(const ftl::texture2d_description &textureDescription)
{
    auto indices = std::vector<uint32_t>();

    // Something like this:
    // unsigned int indices[] = { 
    //    0, 1, 2,   // first triangle
    //    1, 3, 2    // second triangle
    // };

    auto numMips = std::min(1, (int)textureDescription.mips.size());

    for (auto i = 0; i < numMips; ++i)
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
    if (argc != 2)
    {
        std::cout << "Wrong number of arguments." << std::endl;
        std::cout << "Number of arguments found: " << argc << std::endl;
        return -1;
    }
    
    auto textureUUID = ftl::uuid(argv[1]);

    // Get description. 
    auto descFilePath = "C:/Dev/3dverse-experiments/ftl-texture-reader/res/desc.texture." + textureUUID.toString();
    auto spFile = vfs::open_read_only(descFilePath, vfs::file_creation_options::open_if_existing);

    if (!spFile || !spFile->isValid())
    {
        std::cout << "Could not open description." << std::endl;
        return -1;
    }

    auto textureDescription = ftl::texture2d_description();

    auto jStr = std::string();
    auto descFileSize = spFile->size();
    jStr.resize(descFileSize);
    spFile->read(jStr);
    
    try
    {
        auto j = nlohmann::json::parse(jStr);
        textureDescription = j;
    }
    catch (nlohmann::json::exception e)
    {
        std::cout << "Error in parsing description: " << e.what() << std::endl;
        std::cout << "What was found in the file: " << std::endl;
        std::cout << jStr << std::endl;
        return -1;
    }

    if (!glfwInit())
    {
        // Initialization failed
        std::cout << "glfw Initialization failed." << std::endl;
        return -1;
    }

    glfwSetErrorCallback(glfwErrorCallback);

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

    // Initialize glad.
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // Clear screen. 
    GLCall(glClear(GL_COLOR_BUFFER_BIT));

    // Make texture.
    unsigned int texture;
    GLCall(glGenTextures(1, &texture));
    // Attach 'texture' to the texture unit number 0. 
    GLCall(glActiveTexture(GL_TEXTURE0));
    GLCall(glBindTexture(GL_TEXTURE_2D, texture));

    // set the texture wrapping/filtering options (on the currently bound texture object)
    GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER));
    GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER));
    GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));

    /*int i;
    FILE *f = fopen("C:/Users/natha/Downloads/ily.bmp", "rb");
    unsigned char info[54];

    // read the 54-byte header
    fread(info, sizeof(unsigned char), 54, f);

    // extract image height and width from header
    int width = *(int *)&info[18];
    int height = *(int *)&info[22];

    // allocate 3 bytes per pixel
    int size = 3 * width * height;
    unsigned char *data = new unsigned char[size];

    // read the rest of the data at once
    fread(data, sizeof(unsigned char), size, f);
    fclose(f);

    for (i = 0; i < size; i += 3)
    {
        // flip the order of every 3 bytes
        unsigned char tmp = data[i];
        data[i] = data[i + 2];
        data[i + 2] = tmp;
    }

    GLCall(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data));
    GLCall(glGenerateMipmap(GL_TEXTURE_2D));*/

    // Get mip payload. 
    int i = 0;
    std::stringstream num;
    num << i;
    auto payloadFilePath = "C:/Dev/3dverse-experiments/ftl-texture-reader/res/payload.texture.mip" + num.str() + std::string(".") + textureUUID.toString();
    auto spFileView = vfs::open_read_only_view(payloadFilePath, vfs::file_creation_options::open_if_existing);

    if (!spFileView || !spFileView->isValid())
    {
        std::cout << "Could not open payload." << std::endl;
        std::cout << "Payload file path: " << payloadFilePath << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    std::vector<uint8_t> pPixels;
    auto width = textureDescription.mips[i].dimension[0];
    auto height = textureDescription.mips[i].dimension[1];

    pPixels.resize(getDecompressedSize(textureDescription.format, height * width));

    rgbcx::bc1_approx_mode bc1_mode = rgbcx::bc1_approx_mode::cBC1Ideal;

    auto glInternalFormat = getGLFormatCompressed(textureDescription);
    GLCall(glCompressedTexImage2D(GL_TEXTURE_2D, i, glInternalFormat, width, height, 0, textureDescription.mips[i].payloadSize, spFileView->cursor<void>()));
    GLCall(glGenerateMipmap(GL_TEXTURE_2D));


    // Need to specify mode for bc7.
   /* switch (textureDescription.format)
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
    }*/
    
    //auto outputFilePath = "C:/Dev/3dverse-experiments/ftl-texture-reader/res/output/payload.texture.mip" + num.str() + std::string(".") + textureUUID.toString();
    //auto spOutputFileView = vfs::open_read_only_view(outputFilePath, vfs::file_creation_options::create_if_nonexisting);

    //spOutputFileView->write(pPixels.data(), pPixels.size());

    // https://learnopengl.com/Getting-started/Textures
    // Set each mip map level manually. 
    // i corresponds to mip level.
    /*auto glFormat = getGLFormat(textureDescription);
    GLCall(glTexImage2D(GL_TEXTURE_2D, i, glFormat, width, height, 0, glFormat, GL_UNSIGNED_BYTE, pPixels.data()));
    GLCall(glGenerateMipmap(GL_TEXTURE_2D));*/
    //GLCall(glCompressedTexImage2D(GL_TEXTURE_2D, i, glFormat, width, height, 0, textureDescription.payloadTotalSize, spFileView->cursor<void>()));

    /*for (auto i = 0; i < 8; ++i)
    {
        // Get mip payload. 
        std::stringstream num;
        num << i;
        auto payloadFilePath = "C:/Dev/3dverse-experiments/ftl-texture-reader/res/payload.texture.mip" + num.str() + std::string(".") + textureUUID.toString();
        auto spFileView = vfs::open_read_only_view(payloadFilePath, vfs::file_creation_options::open_if_existing);

        if (!spFileView || !spFileView->isValid())
        {
            std::cout << "Could not open payload." << std::endl;
            std::cout << "Payload file path: " << payloadFilePath << std::endl;
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

        //auto outputFilePath = "C:/Dev/3dverse-experiments/ftl-texture-reader/res/output/payload.texture.mip" + num.str() + std::string(".") + textureUUID.toString();
        //auto spOutputFileView = vfs::open_read_only_view(outputFilePath, vfs::file_creation_options::create_if_nonexisting);

        //spOutputFileView->write(pPixels.data(), pPixels.size());

        // https://learnopengl.com/Getting-started/Textures
        // Set each mip map level manually. 
        // i corresponds to mip level.
        auto glFormat = getGLFormat(textureDescription);
        GLCall(glTexImage2D(GL_TEXTURE_2D, i, glFormat, width, height, 0, glFormat, GL_UNSIGNED_BYTE, pPixels.data()));
        //GLCall(glCompressedTexImage2D(GL_TEXTURE_2D, i, glFormat, width, height, 0, textureDescription.payloadTotalSize, spFileView->cursor<void>()));
    }*/

    std::string vertexShader =
        "#version 330 core\n"
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
        "#version 330 core\n"
        "out vec4 FragColor;\n"
        "in vec2 texCoords;\n"
        "\n"
        "uniform sampler2D ourTexture;\n"
        "\n"
        "void main()\n"
        "{\n"
        "FragColor = texture(ourTexture, texCoords);\n"
        "//FragColor = vec4(1, 0, 0, 1);\n"
        "}";

    auto program = createShader(vertexShader, fragmentShader);
    GLCall(glUseProgram(program));

    GLCall(glActiveTexture(GL_TEXTURE0));
    // Bind ourTexture to texture unit 0
    GLCall(glUniform1i(glGetUniformLocation(program, "ourTexture"), 0));

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

    std::cout << "vertices : " << std::endl;
    for (auto i = 0; i < vertices.size(); ++i)
    {
        if (i % 5 == 0)std::cout << std::endl;
        std::cout << vertices[i] << ", ";
    }
    std::cout << std::endl << "indices : " << std::endl;
    for (auto i = 0; i < indices.size(); ++i)
    {
        if (i % 3 == 0) std::cout << std::endl;
        std::cout << indices[i] << ", ";
    }
    std::cout << std::endl;

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

    //GLCall(glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0));
    GLCall(glActiveTexture(GL_TEXTURE0));

    while (!glfwWindowShouldClose(window))
    {
        GLCall(glClear(GL_COLOR_BUFFER_BIT));
        GLCall(glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0));
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Exit.
    GLCall(glDeleteProgram(program));
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
} 