#ifndef MODEL_H
#define MODEL_H

#include <glad/glad.h> 

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <json.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <mesh.h>
#include <shader.h>

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <vector>
#include <set>
#include <cfloat>   
#include <cstring>  
#include <functional>
#include <algorithm>

using namespace std;

unsigned int TextureFromFile(const char *path, const string &directory, bool gamma = false);

struct Node {
    std::string name;
    glm::mat4 localTransform;
    std::vector<unsigned int> meshIndices;
    std::vector<Node> children;
};

class Model 
{
public:
    // model data 
    vector<Texture> textures_loaded;	
    vector<Mesh>    meshes;
    string directory;
    bool gammaCorrection;
    
    Node rootNode;

    // precomputed local-space AABB (approximate)
    glm::vec3 localMin = glm::vec3(0.0f);
    glm::vec3 localMax = glm::vec3(0.0f);
    bool      hasLocalAABB = false;
    float     normalizationScale = 1.0f;   

    // constructor
    Model(string const &path, bool gamma = false, bool flipUV = true) : gammaCorrection(gamma)
    {
        loadModel(path, flipUV);
        computeLocalAABB(); 
    }

    // Draws the model using the node hierarchy
    // overrides: map of node name -> extra transform matrix (multiplied to local)
    void Draw(Shader &shader, const glm::mat4 &modelMatrix, const glm::vec3 &cameraPos, const std::map<std::string, glm::mat4>* overrides = nullptr)
    {
        glm::mat4 base = modelMatrix;
        drawNode(rootNode, base, shader, overrides);
    }
    
    // Traverse the model hierarchy and call callback for each mesh instance with its global transform
    void Traverse(std::function<void(const Mesh&, const glm::mat4&)> callback)
    {
        traverseNode(rootNode, glm::mat4(1.0f), callback);
    }

    void CalculateAABB(const glm::mat4 &modelMatrix, glm::vec3 &minOut, glm::vec3 &maxOut) const
    {
        minOut = glm::vec3(FLT_MAX);
        maxOut = glm::vec3(-FLT_MAX);
        
        // Helper to traverse const
        calculateAABBNode(rootNode, modelMatrix, minOut, maxOut);
    }

    float GetLocalHeight() const
    {
        if (!hasLocalAABB) return 1.0f;
        return localMax.y - localMin.y;
    }

    float GetNormalizationScale() const
    {
        return normalizationScale;
    }
    
private:
    struct UVTransform {
        glm::vec2 offset = glm::vec2(0.0f);
        glm::vec2 scale = glm::vec2(1.0f);
        float rotation = 0.0f;
    };

    std::vector<UVTransform> imageTransforms;
    std::vector<std::string> imageUris;
    struct MatRefs { int baseColor = -1; int normal = -1; int metallicRoughness = -1; };
    std::vector<MatRefs> materialImageRefs;
    std::vector<glm::vec4> materialBaseColorFactors;
    std::vector<float> materialMetallicFactors;
    std::vector<float> materialRoughnessFactors;

    void loadModel(string const &path, bool flipUV)
{
    // Configure flags for both GLTF and GLB files
    unsigned int flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace;
    
    // For GLB files, also enable embedded texture loading
    std::string ext = path.substr(path.find_last_of(".") + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext == "glb") {
        // GLB-specific processing flags for proper embedded texture handling
        flags |= aiProcess_EmbedTextures;
    }
    
    if (flipUV) flags |= aiProcess_FlipUVs;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, flags);
        
    if(!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        std::cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
        std::cout << "Failed to load model: " << path << std::endl;
        return;
    }

    directory = path.substr(0, path.find_last_of('/'));

    // ONLY parse JSON if it's a .gltf file. .glb is binary and will cause crashes/errors here.
    if (ext == "gltf") {
        try {
            std::ifstream in(path);
            if (in.is_open()) {
                nlohmann::json j;
                in >> j;
                // ... [Keep your existing JSON logic for gltf extensions here] ...
            }
        } catch (...) { std::cout << "Note: PBR JSON extensions not found or failed to parse." << std::endl; }
    }

    // 1. Process Meshes
    for(unsigned int i = 0; i < scene->mNumMeshes; i++)
    {
        meshes.push_back(processMesh(scene->mMeshes[i], scene));
    }

    // 2. Process Nodes
    rootNode = processNode(scene->mRootNode, scene);
}

vector<Texture> loadMaterialTextures(aiMaterial *mat, aiTextureType type, string typeName, const aiScene* scene)
{
    vector<Texture> textures;
    for(unsigned int i = 0; i < mat->GetTextureCount(type); i++)
    {
        aiString str;
        mat->GetTexture(type, i, &str);
        
        // Skip empty paths
        if (!str.data || strlen(str.data) == 0) {
            continue;
        }
        
        bool skip = false;
        // Check if we already loaded this exact texture
        for(unsigned int j = 0; j < textures_loaded.size(); j++)
        {
            if(std::strcmp(textures_loaded[j].path.data(), str.C_Str()) == 0)
            {
                textures.push_back(textures_loaded[j]);
                skip = true; 
                break;
            }
        }
        if(!skip)
        {   
            Texture texture;
            const char* pathStr = str.C_Str();
            bool isGamma = (typeName == "texture_diffuse");

            // KEY FIX: Check if the texture is embedded in the GLB
            const aiTexture* embeddedTexture = scene->GetEmbeddedTexture(pathStr);
            if (embeddedTexture) {
                // It's a GLB with internal textures
                texture.id = TextureFromEmbedded(embeddedTexture, isGamma);
            } else {
                // It's a GLTF or FBX with external textures
                texture.id = TextureFromFile(pathStr, directory, isGamma);
            }
            if (texture.id != 0) {
                std::cout << "[Texture Debug] Loaded " << typeName << " | ID: " << texture.id << " | Path: " << str.C_Str() << std::endl;
            } else {
                std::cout << "[Texture Debug] FAILED to load texture: " << str.C_Str() << std::endl;
            }
            if (texture.id != 0) {
                texture.type = typeName;
                texture.path = str.C_Str();
                textures.push_back(texture);
                textures_loaded.push_back(texture);
            }
        }
    }
    return textures;
}

// Helper to load all textures from a material for all available texture types
vector<Texture> loadAllMaterialTextures(aiMaterial *mat, const aiScene* scene)
{
    vector<Texture> allTextures;
    set<string> loadedPaths; // Track paths we've already loaded to avoid duplicates
    
    // Define priority order for texture types - first match wins
    vector<pair<aiTextureType, string>> typeConfig = {
        // Diffuse/Base Color (try in priority order)
        {aiTextureType_BASE_COLOR, "texture_diffuse"},
        {aiTextureType_DIFFUSE, "texture_diffuse"},
        {aiTextureType_UNKNOWN, "texture_diffuse"},
        // Normal maps
        {aiTextureType_NORMALS, "texture_normal"},
        // Metallic/Roughness
        {aiTextureType_METALNESS, "texture_metallicRoughness"},
        // Emissive
        {aiTextureType_EMISSIVE, "texture_diffuse"},
    };
    
    // Track which texture type names we've already loaded
    set<string> usedTypes;
    
    for (const auto& typeInfo : typeConfig) {
        aiTextureType type = typeInfo.first;
        string textureName = typeInfo.second;
        
        // Skip if we've already loaded this texture name type
        if (usedTypes.count(textureName)) {
            continue;
        }
        
        int count = mat->GetTextureCount(type);
        if (count > 0) {
            for (unsigned int i = 0; i < mat->GetTextureCount(type); i++) {
                aiString str;
                mat->GetTexture(type, i, &str);
                
                const char* pathStr = str.C_Str();
                
                // Skip if we've already loaded this exact path
                if (loadedPaths.count(string(pathStr))) {
                    continue;
                }
                loadedPaths.insert(string(pathStr));
                
                // Check cache first
                bool found = false;
                for (const auto& cached : textures_loaded) {
                    if (cached.path == pathStr) {
                        // Use cached texture but add with the type name
                        Texture tex = cached;
                        tex.type = textureName;
                        allTextures.push_back(tex);
                        found = true;
                        usedTypes.insert(textureName);
                        break;
                    }
                }
                
                if (!found) {
                    Texture texture;
                    bool isGamma = (textureName == "texture_diffuse");

                    // Check if embedded
                    const aiTexture* embeddedTexture = scene->GetEmbeddedTexture(pathStr);
                    if (embeddedTexture) {
                        texture.id = TextureFromEmbedded(embeddedTexture, isGamma);
                    } else {
                        texture.id = TextureFromFile(pathStr, directory, isGamma);
                    }
                    
                    if (texture.id != 0) {
                        texture.type = textureName;
                        texture.path = pathStr;
                        allTextures.push_back(texture);
                        textures_loaded.push_back(texture);
                        usedTypes.insert(textureName);
                    }
                }
                
                // Stop after first successful load for this type
                if (usedTypes.count(textureName)) {
                    break;
                }
            }
        }
    }
    
    return allTextures;
}

    glm::mat4 aiMatToGlm(const aiMatrix4x4 &m)
    {
        glm::mat4 out;
        out[0][0] = m.a1; out[1][0] = m.a2; out[2][0] = m.a3; out[3][0] = m.a4;
        out[0][1] = m.b1; out[1][1] = m.b2; out[2][1] = m.b3; out[3][1] = m.b4;
        out[0][2] = m.c1; out[1][2] = m.c2; out[2][2] = m.c3; out[3][2] = m.c4;
        out[0][3] = m.d1; out[1][3] = m.d2; out[2][3] = m.d3; out[3][3] = m.d4;
        return out;
    }

    Node processNode(aiNode *ai_node, const aiScene *scene)
    {
        Node node;
        node.name = ai_node->mName.C_Str();
        node.localTransform = aiMatToGlm(ai_node->mTransformation);

        // Store mesh indices
        for(unsigned int i = 0; i < ai_node->mNumMeshes; i++)
        {
            node.meshIndices.push_back(ai_node->mMeshes[i]);
        }
        
        // Process children
        for(unsigned int i = 0; i < ai_node->mNumChildren; i++)
        {
            node.children.push_back(processNode(ai_node->mChildren[i], scene));
        }
        return node;
    }

    Mesh processMesh(aiMesh *mesh, const aiScene *scene)
    {
        vector<Vertex> vertices;
        vector<unsigned int> indices;
        vector<Texture> textures;

        for(unsigned int i = 0; i < mesh->mNumVertices; i++)
        {
            Vertex vertex{};
            glm::vec3 vector;

            // positions (LOCAL space now)
            vector.x = mesh->mVertices[i].x;
            vector.y = mesh->mVertices[i].y;
            vector.z = mesh->mVertices[i].z;
            vertex.Position = vector;

            // normals
            if (mesh->HasNormals())
            {
                vector.x = mesh->mNormals[i].x;
                vector.y = mesh->mNormals[i].y;
                vector.z = mesh->mNormals[i].z;
                vertex.Normal = vector;
            }
            else {
                vertex.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }

            // texCoords...
            if(mesh->mTextureCoords[0])
            {
                glm::vec2 vec;
                vec.x = mesh->mTextureCoords[0][i].x; 
                vec.y = mesh->mTextureCoords[0][i].y;
                vertex.TexCoords = vec;
                
                // tangent
                vector.x = mesh->mTangents[i].x;
                vector.y = mesh->mTangents[i].y;
                vector.z = mesh->mTangents[i].z;
                vertex.Tangent = vector;
                
                // bitangent
                vector.x = mesh->mBitangents[i].x;
                vector.y = mesh->mBitangents[i].y;
                vector.z = mesh->mBitangents[i].z;
                vertex.Bitangent = vector;
            }
            else {
                vertex.TexCoords = glm::vec2(0.0f);
            }
            
            vertices.push_back(vertex);
        }

        for(unsigned int i = 0; i < mesh->mNumFaces; i++)
        {
            aiFace face = mesh->mFaces[i];
            for(unsigned int j = 0; j < face.mNumIndices; j++)
                indices.push_back(face.mIndices[j]);        
        }

        // Inside processMesh - safely handle material loading
aiMaterial* material = nullptr;
if (mesh->mMaterialIndex >= 0 && mesh->mMaterialIndex < (int)scene->mNumMaterials) {
    material = scene->mMaterials[mesh->mMaterialIndex];
}

// Load ALL texture types from material using the new helper function
vector<Texture> allMatTextures;
if (material) {
    allMatTextures = loadAllMaterialTextures(material, scene);
}
textures.insert(textures.end(), allMatTextures.begin(), allMatTextures.end());
        // PBR Heuristics (Same as before)
        glm::vec4 bcFactor = glm::vec4(1.0f);
        if (mesh->mMaterialIndex >= 0 && mesh->mMaterialIndex < (int)materialBaseColorFactors.size())
             bcFactor = materialBaseColorFactors[mesh->mMaterialIndex];

        float matMetal = 1.0f, matRough = 1.0f;
         if (mesh->mMaterialIndex >= 0 && mesh->mMaterialIndex < (int)materialMetallicFactors.size())
            matMetal = materialMetallicFactors[mesh->mMaterialIndex];
         if (mesh->mMaterialIndex >= 0 && mesh->mMaterialIndex < (int)materialRoughnessFactors.size())
            matRough = materialRoughnessFactors[mesh->mMaterialIndex];

        return Mesh(vertices, indices, textures, bcFactor, false, matMetal, matRough);
    }

    unsigned int TextureFromEmbedded(const aiTexture* aiTex, bool gamma)
{
    if (!aiTex) return 0;

    unsigned int textureID;
    glGenTextures(1, &textureID);

    int width, height, nrComponents;
    unsigned char* data = nullptr;

    if (aiTex->mHeight == 0) // Compressed data (PNG, JPG)
    {
        // Use mWidth as the buffer size
        data = stbi_load_from_memory(reinterpret_cast<unsigned char*>(aiTex->pcData), 
                                     aiTex->mWidth, &width, &height, &nrComponents, 0);
        
        if (!data) {
            std::cout << "STB_IMAGE ERROR: " << stbi_failure_reason() << " for embedded texture." << std::endl;
            return 0;
        }
    }
    else // Uncompressed raw data
    {
        width = aiTex->mWidth;
        height = aiTex->mHeight;
        nrComponents = 4;
        data = reinterpret_cast<unsigned char*>(aiTex->pcData); 
        // Note: Don't free/delete aiTex->pcData; it's owned by Assimp
    }

    if (data)
    {
        GLenum format = GL_RGBA;
        if (nrComponents == 1) format = GL_RED;
        else if (nrComponents == 3) format = GL_RGB;
        else if (nrComponents == 4) format = GL_RGBA;

        GLenum internalFormat = format;
        if (gamma && format == GL_RGB) internalFormat = GL_SRGB;
        if (gamma && format == GL_RGBA) internalFormat = GL_SRGB_ALPHA;

        glBindTexture(GL_TEXTURE_2D, textureID);
        // Ensure alignment is set for textures that might not be power-of-two
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1); 
        
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        // Parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        if (aiTex->mHeight == 0) stbi_image_free(data);
    }

    return textureID;
}



    void drawNode(const Node& node, glm::mat4 parentTransform, Shader& shader, const std::map<std::string, glm::mat4>* overrides)
    {
        glm::mat4 globalTransform = parentTransform * node.localTransform;
        
        // Apply override if exists (e.g. rotation for wheels)
        if (overrides) {
            auto it = overrides->find(node.name);
            if (it != overrides->end()) {
                // Determine how to apply override. 
                // Using parent * (local * override) allows rotating the part in its local space
                globalTransform = parentTransform * node.localTransform * it->second;
            }
        }
        
        // Draw meshes for this node
        for (unsigned int i : node.meshIndices) {
            shader.setMat4("model", globalTransform);
            meshes[i].Draw(shader);
        }
        
        for (const auto& child : node.children) {
            drawNode(child, globalTransform, shader, overrides);
        }
    }

    void traverseNode(const Node& node, glm::mat4 parentTransform, std::function<void(const Mesh&, const glm::mat4&)> callback)
    {
        glm::mat4 globalTransform = parentTransform * node.localTransform;
        
        for (unsigned int i : node.meshIndices) {
            callback(meshes[i], globalTransform);
        }
        
        for (const auto& child : node.children) {
            traverseNode(child, globalTransform, callback);
        }
    }
    
    void calculateAABBNode(const Node& node, glm::mat4 parentTransform, glm::vec3& minOut, glm::vec3& maxOut) const
    {
        glm::mat4 globalTransform = parentTransform * node.localTransform;
         for (unsigned int i : node.meshIndices) {
            const auto& mesh = meshes[i];
            for(const auto& v : mesh.vertices) {
                glm::vec4 worldPos = globalTransform * glm::vec4(v.Position, 1.0f);
                minOut = glm::min(minOut, glm::vec3(worldPos));
                maxOut = glm::max(maxOut, glm::vec3(worldPos));
            }
         }
         for (const auto& child : node.children) {
             calculateAABBNode(child, globalTransform, minOut, maxOut);
         }
    }

    void computeLocalAABB()
    {
        // Re-calculate using our new traverse logic with Identity
        glm::vec3 minV(FLT_MAX);
        glm::vec3 maxV(-FLT_MAX);
        calculateAABBNode(rootNode, glm::mat4(1.0f), minV, maxV);
        
        if (minV.x > maxV.x) { // Invalid
             hasLocalAABB = false;
             normalizationScale = 1.0f;
             return;
        }

        localMin = minV;
        localMax = maxV;
        hasLocalAABB = true;
        float h = maxV.y - minV.y;
        if (h > 1e-4f) normalizationScale = 1.0f / h;
        else normalizationScale = 1.0f;
    }
};

#endif
