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
using namespace std;

unsigned int TextureFromFile(const char *path, const string &directory, bool gamma = false);

class Model 
{
public:
    // model data 
    vector<Texture> textures_loaded;	// stores all the textures loaded so far, optimization to make sure textures aren't loaded more than once.
    vector<Mesh>    meshes;
    string directory;
    bool gammaCorrection;

    // constructor, expects a filepath to a 3D model.
    Model(string const &path, bool gamma = false) : gammaCorrection(gamma)
    {
        loadModel(path);
    }

    // draws the model: opaque first, then transparent (simple two-pass for correct blending)
    // Accepts the current model matrix (world transform) and the camera position for sorting transparent meshes.
    void Draw(Shader &shader, const glm::mat4 &modelMatrix, const glm::vec3 &cameraPos)
    {
        // first draw opaque meshes
        for (unsigned int i = 0; i < meshes.size(); ++i) {
            if (!meshes[i].transparent)
                meshes[i].Draw(shader);
        }
        // collect transparent meshes and sort back-to-front based on camera distance
        struct TransparentEntry { size_t idx; float dist; };
        std::vector<TransparentEntry> transparentList;
        for (size_t i = 0; i < meshes.size(); ++i) {
            if (meshes[i].transparent) {
                // world-space centroid
                glm::vec4 wc = modelMatrix * glm::vec4(meshes[i].centroid, 1.0f);
                float d = glm::length(glm::vec3(wc) - cameraPos);
                transparentList.push_back({i, d});
            }
        }
        // sort descending (furthest first)
        std::sort(transparentList.begin(), transparentList.end(), [](const TransparentEntry &a, const TransparentEntry &b){ return a.dist > b.dist; });
        // then draw transparent meshes (disable depth writes so blending works)
        glDepthMask(GL_FALSE);
        for (auto &e : transparentList) {
            meshes[e.idx].Draw(shader);
        }
        glDepthMask(GL_TRUE);
    }
    
private:
    struct UVTransform {
        glm::vec2 offset = glm::vec2(0.0f);
        glm::vec2 scale = glm::vec2(1.0f);
        float rotation = 0.0f;
    };

    // per-image transforms parsed from glTF (indexed by image index)
    std::vector<UVTransform> imageTransforms;
    // images URIs from the glTF (indexed by image index)
    std::vector<std::string> imageUris;
    // per-material references to image indices
    struct MatRefs { int baseColor = -1; int normal = -1; int metallicRoughness = -1; };
    std::vector<MatRefs> materialImageRefs;
    // per-material baseColorFactor (r,g,b,a) from glTF
    std::vector<glm::vec4> materialBaseColorFactors;
    // per-material metallic/roughness factors from glTF
    std::vector<float> materialMetallicFactors;
    std::vector<float> materialRoughnessFactors;
    // loads a model with supported ASSIMP extensions from file and stores the resulting meshes in the meshes vector.
    void loadModel(string const &path)
    {
        // read file via ASSIMP
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);
        // check for errors
        if(!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) // if is Not Zero
        {
            cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << endl;
            return;
        }
        // retrieve the directory path of the filepath
        directory = path.substr(0, path.find_last_of('/'));

        // Try to parse the glTF JSON to extract image URIs and KHR_texture_transform info
        try {
            std::ifstream in(path);
            if (in.good()) {
                nlohmann::json j;
                in >> j;
                // images
                if (j.contains("images") && j["images"].is_array()) {
                    for (auto &img : j["images"]) {
                        if (img.contains("uri")) {
                            imageUris.push_back(img["uri"].get<std::string>());
                        } else {
                            imageUris.push_back(std::string());
                        }
                        imageTransforms.push_back(UVTransform());
                    }
                }
                // materials: map pbr textures to image indices and record KHR transforms
                if (j.contains("materials") && j["materials"].is_array()) {
                    materialImageRefs.resize(j["materials"].size());
                    materialBaseColorFactors.resize(j["materials"].size(), glm::vec4(1.0f));
                    materialMetallicFactors.resize(j["materials"].size(), 1.0f);
                    materialRoughnessFactors.resize(j["materials"].size(), 1.0f);
                    for (size_t mi = 0; mi < j["materials"].size(); ++mi) {
                        auto &mat = j["materials"][mi];
                        // baseColorFactor
                        if (mat.contains("pbrMetallicRoughness") && mat["pbrMetallicRoughness"].contains("baseColorFactor")) {
                            auto &f = mat["pbrMetallicRoughness"]["baseColorFactor"];
                            if (f.is_array() && f.size() >= 4) {
                                glm::vec4 bc;
                                bc.r = f[0].get<float>(); bc.g = f[1].get<float>(); bc.b = f[2].get<float>(); bc.a = f[3].get<float>();
                                materialBaseColorFactors[mi] = bc;
                            }
                        }
                        // metallic / roughness factors
                        if (mat.contains("pbrMetallicRoughness")) {
                            auto &pbr = mat["pbrMetallicRoughness"];
                            if (pbr.contains("metallicFactor")) materialMetallicFactors[mi] = pbr["metallicFactor"].get<float>();
                            if (pbr.contains("roughnessFactor")) materialRoughnessFactors[mi] = pbr["roughnessFactor"].get<float>();
                        }
                        // baseColorTexture
                        if (mat.contains("pbrMetallicRoughness") && mat["pbrMetallicRoughness"].contains("baseColorTexture")) {
                            auto &bct = mat["pbrMetallicRoughness"]["baseColorTexture"];
                            if (bct.contains("index")) {
                                materialImageRefs[mi].baseColor = bct["index"].get<int>();
                                // khr transform
                                if (bct.contains("extensions") && bct["extensions"].contains("KHR_texture_transform")) {
                                    auto &t = bct["extensions"]["KHR_texture_transform"];
                                    UVTransform ut;
                                    if (t.contains("offset") && t["offset"].is_array()) {
                                        ut.offset.x = t["offset"][0].get<float>();
                                        ut.offset.y = t["offset"][1].get<float>();
                                    }
                                    if (t.contains("scale") && t["scale"].is_array()) {
                                        ut.scale.x = t["scale"][0].get<float>();
                                        ut.scale.y = t["scale"][1].get<float>();
                                    }
                                    if (t.contains("rotation")) ut.rotation = t["rotation"].get<float>();
                                    if (materialImageRefs[mi].baseColor >= 0 && materialImageRefs[mi].baseColor < (int)imageTransforms.size())
                                        imageTransforms[materialImageRefs[mi].baseColor] = ut;
                                }
                            }
                        }
                        // metallicRoughnessTexture
                        if (mat.contains("pbrMetallicRoughness") && mat["pbrMetallicRoughness"].contains("metallicRoughnessTexture")) {
                            auto &mrt = mat["pbrMetallicRoughness"]["metallicRoughnessTexture"];
                            if (mrt.contains("index")) materialImageRefs[mi].metallicRoughness = mrt["index"].get<int>();
                        }
                        // normalTexture
                        if (mat.contains("normalTexture")) {
                            auto &nt = mat["normalTexture"];
                            if (nt.contains("index")) {
                                materialImageRefs[mi].normal = nt["index"].get<int>();
                                // check for transform on normalTexture also
                                if (nt.contains("extensions") && nt["extensions"].contains("KHR_texture_transform")) {
                                    auto &t = nt["extensions"]["KHR_texture_transform"];
                                    UVTransform ut;
                                    if (t.contains("offset") && t["offset"].is_array()) {
                                        ut.offset.x = t["offset"][0].get<float>();
                                        ut.offset.y = t["offset"][1].get<float>();
                                    }
                                    if (t.contains("scale") && t["scale"].is_array()) {
                                        ut.scale.x = t["scale"][0].get<float>();
                                        ut.scale.y = t["scale"][1].get<float>();
                                    }
                                    if (t.contains("rotation")) ut.rotation = t["rotation"].get<float>();
                                    if (materialImageRefs[mi].normal >= 0 && materialImageRefs[mi].normal < (int)imageTransforms.size())
                                        imageTransforms[materialImageRefs[mi].normal] = ut;
                                }
                            }
                        }
                    }
                }
            }
        } catch (...) {
            // Ignore JSON parsing errors; loader will still proceed using Assimp's data
        }

        // process ASSIMP's root node recursively
        processNode(scene->mRootNode, scene, glm::mat4(1.0f));
    }

    // processes a node in a recursive fashion. Processes each individual mesh located at the node and repeats this process on its children nodes (if any).
    // convert Assimp matrix to glm::mat4
    glm::mat4 aiMatToGlm(const aiMatrix4x4 &m)
    {
        glm::mat4 out;
        out[0][0] = m.a1; out[1][0] = m.a2; out[2][0] = m.a3; out[3][0] = m.a4;
        out[0][1] = m.b1; out[1][1] = m.b2; out[2][1] = m.b3; out[3][1] = m.b4;
        out[0][2] = m.c1; out[1][2] = m.c2; out[2][2] = m.c3; out[3][2] = m.c4;
        out[0][3] = m.d1; out[1][3] = m.d2; out[2][3] = m.d3; out[3][3] = m.d4;
        return out;
    }

    // processes a node in a recursive fashion. Applies the node transform to child meshes.
    void processNode(aiNode *node, const aiScene *scene, const glm::mat4 &parentTransform)
    {
        // compute this node's transform (Assimp stores transforms as column-major 4x4)
        glm::mat4 nodeTransform = parentTransform * aiMatToGlm(node->mTransformation);

        // process each mesh located at the current node
        for(unsigned int i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back(processMesh(mesh, scene, nodeTransform));
        }
        // after we've processed all of the meshes (if any) we then recursively process each of the children nodes
        for(unsigned int i = 0; i < node->mNumChildren; i++)
        {
            processNode(node->mChildren[i], scene, nodeTransform);
        }
    }

    Mesh processMesh(aiMesh *mesh, const aiScene *scene, const glm::mat4 &nodeTransform)
    {
        // data to fill
        vector<Vertex> vertices;
        vector<unsigned int> indices;
        vector<Texture> textures;

        // walk through each of the mesh's vertices
        for(unsigned int i = 0; i < mesh->mNumVertices; i++)
        {
            Vertex vertex;
            glm::vec3 vector; // we declare a placeholder vector since assimp uses its own vector class that doesn't directly convert to glm's vec3 class so we transfer the data to this placeholder glm::vec3 first.
            // positions (apply node transform)
            vector.x = mesh->mVertices[i].x;
            vector.y = mesh->mVertices[i].y;
            vector.z = mesh->mVertices[i].z;
            glm::vec4 transformedPos = nodeTransform * glm::vec4(vector, 1.0f);
            vertex.Position = glm::vec3(transformedPos);
            // normals
            // prepare a normal matrix for transforming normals/tangents/bitangents
            glm::mat3 normalMat = glm::mat3(1.0f);
            if (mesh->HasNormals())
            {
                vector.x = mesh->mNormals[i].x;
                vector.y = mesh->mNormals[i].y;
                vector.z = mesh->mNormals[i].z;
                // normal should be transformed by inverse-transpose of the model matrix (3x3)
                normalMat = glm::transpose(glm::inverse(glm::mat3(nodeTransform)));
                vertex.Normal = glm::normalize(normalMat * vector);
            }
            // texture coordinates
            if(mesh->mTextureCoords[0]) // does the mesh contain texture coordinates?
            {
                glm::vec2 vec;
                // a vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't 
                // use models where a vertex can have multiple texture coordinates so we always take the first set (0).
                vec.x = mesh->mTextureCoords[0][i].x; 
                vec.y = mesh->mTextureCoords[0][i].y;
                vertex.TexCoords = vec;
                // tangent
                vector.x = mesh->mTangents[i].x;
                vector.y = mesh->mTangents[i].y;
                vector.z = mesh->mTangents[i].z;
                vertex.Tangent = glm::normalize(normalMat * vector);
                // bitangent
                vector.x = mesh->mBitangents[i].x;
                vector.y = mesh->mBitangents[i].y;
                vector.z = mesh->mBitangents[i].z;
                vertex.Bitangent = glm::normalize(normalMat * vector);
            }
            else
                vertex.TexCoords = glm::vec2(0.0f, 0.0f);

            vertices.push_back(vertex);
        }
        // now wak through each of the mesh's faces (a face is a mesh its triangle) and retrieve the corresponding vertex indices.
        for(unsigned int i = 0; i < mesh->mNumFaces; i++)
        {
            aiFace face = mesh->mFaces[i];
            // retrieve all indices of the face and store them in the indices vector
            for(unsigned int j = 0; j < face.mNumIndices; j++)
                indices.push_back(face.mIndices[j]);        
        }
    // process materials
    aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];    
    // Debug: print mesh and material info to help trace texture bindings
    std::cout << "[Model] Processing mesh '" << mesh->mName.C_Str() << "' (materialIndex=" << mesh->mMaterialIndex << ")" << std::endl;
        // we assume a convention for sampler names in the shaders. Each diffuse texture should be named
        // as 'texture_diffuseN' where N is a sequential number ranging from 1 to MAX_SAMPLER_NUMBER. 
        // Same applies to other texture as the following list summarizes:
        // diffuse: texture_diffuseN
        // specular: texture_specularN
        // normal: texture_normalN

    // 1. diffuse maps
    vector<Texture> diffuseMaps = loadMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse");
    textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());
        // 2. specular maps
        vector<Texture> specularMaps = loadMaterialTextures(material, aiTextureType_SPECULAR, "texture_specular");
        textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());
        // 3. normal maps
        std::vector<Texture> normalMaps = loadMaterialTextures(material, aiTextureType_HEIGHT, "texture_normal");
        textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());
        // 4. height maps
        std::vector<Texture> heightMaps = loadMaterialTextures(material, aiTextureType_AMBIENT, "texture_height");
        textures.insert(textures.end(), heightMaps.begin(), heightMaps.end());
        
        // Also, ensure textures specified directly in glTF JSON (baseColor/normal/metallicRoughness) are loaded
        glm::vec4 bcFactor = glm::vec4(1.0f);
        if (mesh->mMaterialIndex >= 0 && mesh->mMaterialIndex < (int)materialBaseColorFactors.size()) {
            bcFactor = materialBaseColorFactors[mesh->mMaterialIndex];
        }

        if (mesh->mMaterialIndex >= 0 && mesh->mMaterialIndex < (int)materialImageRefs.size()) {
            MatRefs refs = materialImageRefs[mesh->mMaterialIndex];
            // baseColor
            if (refs.baseColor >= 0 && refs.baseColor < (int)imageUris.size()) {
                std::string uri = imageUris[refs.baseColor];
                bool found = false;
                for (auto &t : textures) if (t.path == uri) { found = true; break; }
                if (!found && !uri.empty()) {
                    Texture tex;
                    // baseColor should be gamma-correct
                    tex.id = TextureFromFile(uri.c_str(), this->directory, true);
                    tex.type = "texture_diffuse";
                    tex.path = uri;
                    // apply image transform if any
                    if (refs.baseColor >= 0 && refs.baseColor < (int)imageTransforms.size()) {
                        tex.uvOffset = imageTransforms[refs.baseColor].offset;
                        tex.uvScale = imageTransforms[refs.baseColor].scale;
                        tex.uvRotation = imageTransforms[refs.baseColor].rotation;
                    }
                    textures.push_back(tex);
                }
            }
            // normal
            if (refs.normal >= 0 && refs.normal < (int)imageUris.size()) {
                std::string uri = imageUris[refs.normal];
                bool found = false;
                for (auto &t : textures) if (t.path == uri) { found = true; break; }
                if (!found && !uri.empty()) {
                    Texture tex;
                    // normal maps are linear
                    tex.id = TextureFromFile(uri.c_str(), this->directory, false);
                    tex.type = "texture_normal";
                    tex.path = uri;
                    if (refs.normal >= 0 && refs.normal < (int)imageTransforms.size()) {
                        tex.uvOffset = imageTransforms[refs.normal].offset;
                        tex.uvScale = imageTransforms[refs.normal].scale;
                        tex.uvRotation = imageTransforms[refs.normal].rotation;
                    }
                    textures.push_back(tex);
                }
            }
            // metallicRoughness
            if (refs.metallicRoughness >= 0 && refs.metallicRoughness < (int)imageUris.size()) {
                std::string uri = imageUris[refs.metallicRoughness];
                bool found = false;
                for (auto &t : textures) if (t.path == uri) { found = true; break; }
                if (!found && !uri.empty()) {
                    Texture tex;
                    // metallicRoughness texture is linear (channels are numeric)
                    tex.id = TextureFromFile(uri.c_str(), this->directory, false);
                    tex.type = "texture_metallicRoughness";
                    tex.path = uri;
                    if (refs.metallicRoughness >= 0 && refs.metallicRoughness < (int)imageTransforms.size()) {
                        tex.uvOffset = imageTransforms[refs.metallicRoughness].offset;
                        tex.uvScale = imageTransforms[refs.metallicRoughness].scale;
                        tex.uvRotation = imageTransforms[refs.metallicRoughness].rotation;
                    }
                    textures.push_back(tex);
                }
            }
        }

            // Heuristic: consider mesh transparent if baseColorFactor alpha < 1 or any texture path suggests glass or alpha
            bool isTransparent = false;
            if (mesh->mMaterialIndex >= 0 && mesh->mMaterialIndex < (int)materialBaseColorFactors.size()) {
                if (materialBaseColorFactors[mesh->mMaterialIndex].a < 0.999f) isTransparent = true;
            }
            // check texture filenames for keywords like 'glass' or 'alpha'
            for (auto &t : textures) {
                std::string p = t.path;
                // lower-case check
                for (auto &c : p) c = (char)std::tolower(c);
                if (p.find("glass") != std::string::npos || p.find("alpha") != std::string::npos || p.find("transp") != std::string::npos) {
                    isTransparent = true;
                    break;
                }
            }

            // compute centroid
            glm::vec3 centroid(0.0f);
            for (const auto &v : vertices) centroid += v.Position;
            if (!vertices.empty()) centroid /= (float)vertices.size();
            float matMetal = 1.0f;
            float matRough = 1.0f;
            if (mesh->mMaterialIndex >= 0 && mesh->mMaterialIndex < (int)materialMetallicFactors.size()) matMetal = materialMetallicFactors[mesh->mMaterialIndex];
            if (mesh->mMaterialIndex >= 0 && mesh->mMaterialIndex < (int)materialRoughnessFactors.size()) matRough = materialRoughnessFactors[mesh->mMaterialIndex];
            Mesh m = Mesh(vertices, indices, textures, bcFactor, isTransparent, matMetal, matRough);
            m.centroid = centroid;
            return m;
    }

    // checks all material textures of a given type and loads the textures if they're not loaded yet.
    // the required info is returned as a Texture struct.
    vector<Texture> loadMaterialTextures(aiMaterial *mat, aiTextureType type, string typeName)
    {
        vector<Texture> textures;
        for(unsigned int i = 0; i < mat->GetTextureCount(type); i++)
        {
            aiString str;
            mat->GetTexture(type, i, &str);
            // Debug: report that Assimp returned a texture entry for this material/type
            std::cout << "[Model]  Mat texture: type=" << typeName << " uri=" << str.C_Str() << std::endl;
            // check if texture was loaded before and if so, continue to next iteration: skip loading a new texture
            bool skip = false;
            for(unsigned int j = 0; j < textures_loaded.size(); j++)
            {
                if(std::strcmp(textures_loaded[j].path.data(), str.C_Str()) == 0)
                {
                    textures.push_back(textures_loaded[j]);
                    std::cout << "[Model]   -> Reusing previously loaded texture: " << str.C_Str() << std::endl;
                    skip = true; // a texture with the same filepath has already been loaded, continue to next one. (optimization)
                    break;
                }
            }
            if(!skip)
            {   // if texture hasn't been loaded already, load it
                Texture texture;
                // treat diffuse / baseColor as gamma (sRGB) textures
                bool isGamma = (typeName == "texture_diffuse");
                texture.id = TextureFromFile(str.C_Str(), this->directory, isGamma);
                texture.type = typeName;
                texture.path = str.C_Str();
                textures.push_back(texture);
                textures_loaded.push_back(texture);  // store it as texture loaded for entire model, to ensure we won't unnecessary load duplicate textures.
            }
        }
        return textures;
    }
};


unsigned int TextureFromFile(const char *path, const string &directory, bool gamma)
{
    string filename = string(path);
    filename = directory + '/' + filename;

    unsigned int textureID;
    glGenTextures(1, &textureID);

    // Quick GL sanity check immediately after generation
    {
        GLenum e = glGetError();
        if (e != GL_NO_ERROR) {
            std::cerr << "[TextureFromFile][GL ERROR] after glGenTextures for '" << path << "' : 0x" << std::hex << e << std::dec << std::endl;
        }
    }

    int width, height, nrComponents;
    unsigned char *data = stbi_load(filename.c_str(), &width, &height, &nrComponents, 0);
    if (data)
    {
        std::cout << "[TextureFromFile] loading '" << filename << "' -> " << width << "x" << height << " comps=" << nrComponents << " -> id=" << textureID << std::endl;
        GLenum format = GL_RGB;
        GLenum internalFormat = GL_RGB;
        if (nrComponents == 1) {
            format = GL_RED;
            internalFormat = GL_RED;
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
        {
            GLenum e = glGetError();
            if (e != GL_NO_ERROR) std::cerr << "[TextureFromFile][GL ERROR] after glBindTexture for '" << path << "' : 0x" << std::hex << e << std::dec << std::endl;
        }
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        {
            GLenum e = glGetError();
            if (e != GL_NO_ERROR) std::cerr << "[TextureFromFile][GL ERROR] after glTexImage2D for '" << path << "' : 0x" << std::hex << e << std::dec << std::endl;
        }
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    }
    else
    {
        std::cout << "Texture failed to load at path: " << path << std::endl;
        stbi_image_free(data);
    }

    return textureID;
}
#endif
