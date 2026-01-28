#include <model.h>
#include <fstream>
#include <glad/glad.h>
#include <stb_image.h>
#include <iostream>
#include <cstring>

using namespace std;

unsigned int TextureFromFile(const char *path, const string &directory, bool gamma)
{
    if (!path || std::strlen(path) == 0) {
        std::cout << "[TextureFromFile] Empty texture path, skipping.\n";
        return 0;
    }

    string relPath = string(path);
    string filename = directory + '/' + relPath;

    {
        std::ifstream test(filename, std::ios::binary);
        if (!test.good()) {
            std::cout << "[TextureFromFile] File not found: '" << filename << "', skip.\n";
            return 0;
        }
    }

    unsigned int textureID = 0;
    glGenTextures(1, &textureID);

    int width = 0, height = 0, nrComponents = 0;
    unsigned char *data = stbi_load(filename.c_str(), &width, &height, &nrComponents, 0);
    if (data)
    {
        GLenum format = GL_RGB;
        GLenum internalFormat = GL_RGB;
        if (nrComponents == 1) {
            format = GL_RED;
            internalFormat = GL_RED;
        }
        else if (nrComponents == 2) {
            format = GL_RG;
            internalFormat = GL_RG;
        }
        else if (nrComponents == 3) {
            format = GL_RGB;
            internalFormat = gamma ? GL_SRGB : GL_RGB;
        }
        else if (nrComponents == 4) {
            format = GL_RGBA;
            internalFormat = gamma ? GL_SRGB_ALPHA : GL_RGBA;
        }

        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
                     format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        // Handle Swizzling for 2-component (Grey/Alpha) textures
        if (nrComponents == 2) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_GREEN);
        }

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    }
    else
    {
        std::cout << "[TextureFromFile] Failed to load at path: " << filename << std::endl;
        stbi_image_free(data);
        if (textureID != 0) {
            glDeleteTextures(1, &textureID);
        }
        textureID = 0;
    }

    return textureID;
}
