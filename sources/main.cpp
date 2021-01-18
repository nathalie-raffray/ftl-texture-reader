#include "glfw/glfw3.h"

#include "vfs.hpp"

#include "nlohmann/json.hpp"

#include "ftl/foundation/serialization/uuid.hpp"

#include "ftl/core/assets/texture_description.hpp"

#include <iostream>
#include <fstream>

int main(int argc, char *argv[])
{
    if (argc != 1)
    {
        std::cout << "Wrong number of arguments." << std::endl;
        return 1;
    }
    
    auto textureUUID = ftl::uuid(argv[0]);

    // Get description. 
    auto descFilePath = "texture.description." + textureUUID.toString();
    auto spFile = vfs::open_read_only(descFilePath, vfs::file_creation_options::open_if_existing);

    if (!spFile || !spFile->isValid())
    {
        std::cout << "Could not open description." << std::endl;
        return 1;
    }

    auto textureDescription = ftl::texture2d_description();

    try
    {
        auto jStr = std::string();
        spFile->read(jStr);
        auto j = nlohmann::json::parse(jStr);
        textureDescription << j;
    }
    catch (nlohmann::json::exception e)
    {
        std::cout << "Error in parsing description: " << e.what() << std::endl;
        return 1;
    }
    
    for (auto i = 0; i < textureDescription.mips.size(); ++i)
    {
        // Get mip payload. 
        auto payloadFilePath = "texture.payload.mip" + i + std::string(".") + textureUUID.toString();
        auto spFile = vfs::open_read_only(payloadFilePath, vfs::file_creation_options::open_if_existing);

        if (!spFile || !spFile->isValid())
        {
            std::cout << "Could not open payload." << std::endl;
            return 1;
        }
    }

    // Read all mip payloads w/its description.

    // Decompress them.

    // Create a window thats sized right. 

    // Print them. 
    
    return 0;
} 