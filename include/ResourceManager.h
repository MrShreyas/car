#ifndef RESOURCE_MANAGER_H
#define RESOURCE_MANAGER_H

#include <map>
#include <string>
#include <shader.h>
#include <model.h>

// A static singleton-like class to host all loaded resources
class ResourceManager {
public:
    // Resource storage
    static std::map<std::string, Shader*> Shaders;
    static std::map<std::string, Model*> Models;

    // Loads (and generates) a shader program from file loading vertex, fragment (and geometry) shader's source code. If gShaderFile is not nullptr, it also loads a geometry shader
    static Shader* LoadShader(const char* vShaderFile, const char* fShaderFile, std::string name);
    // Retrieves a stored sader
    static Shader* GetShader(std::string name);
    
    // Loads (and generates) a model from file
    static Model* LoadModel(const char* file, std::string name, bool flipUV = true);
    // Retrieves a stored model
    static Model* GetModel(std::string name);

    // Properly de-allocates all loaded resources
    static void Clear();

private:
    // Private constructor, that is we do not want any actual resource manager objects. Its members and functions should be publicly available (static).
    ResourceManager() { }
};

#endif
