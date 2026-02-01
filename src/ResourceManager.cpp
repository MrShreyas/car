#include "ResourceManager.h"
#include <iostream> 

// Instantiate static variables
std::map<std::string, Shader*> ResourceManager::Shaders;
std::map<std::string, Model*> ResourceManager::Models;

Shader* ResourceManager::LoadShader(const char* vShaderFile, const char* fShaderFile, std::string name)
{
    if (Shaders.find(name) != Shaders.end())
        return Shaders[name];

    Shader* shader = new Shader(vShaderFile, fShaderFile);
    Shaders[name] = shader;
    return shader;
}

Shader* ResourceManager::GetShader(std::string name)
{
    return Shaders[name];
}

Model* ResourceManager::LoadModel(const char* file, std::string name, bool flipUV)
{
    if (Models.find(name) != Models.end())
        return Models[name];
    std::cout << "Loading model " << name << " from " << file << endl;
    Model* model = new Model(file, false, flipUV);
    Models[name] = model;
    return model;
}

Model* ResourceManager::GetModel(std::string name)
{
    return Models[name];
}

void ResourceManager::Clear()
{
    // (Properly delete all shaders)
    for (auto iter : Shaders)
        delete iter.second;
    Shaders.clear();

    // (Properly delete all models)
    for (auto iter : Models)
        delete iter.second;
    Models.clear();
}
