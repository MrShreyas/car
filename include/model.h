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
#include <cfloat>   // for FLT_MAX
#include <cstring>  // for std::strcmp

using namespace std;

unsigned int TextureFromFile(const char *path, const string &directory, bool gamma = false);

class Model 
{
public:
    // model data 
    vector<Texture> textures_loaded;	// stores all the textures loaded so far
    vector<Mesh>    meshes;
    string directory;
    bool gammaCorrection;

    // precomputed local-space AABB (over all meshes, after Assimp processing)
    glm::vec3 localMin = glm::vec3(0.0f);
    glm::vec3 localMax = glm::vec3(0.0f);
    bool      hasLocalAABB = false;
    float     normalizationScale = 1.0f;   // scale to make height = 1.0

    // constructor, expects a filepath to a 3D model.
    Model(string const &path, bool gamma = false) : gammaCorrection(gamma)
    {
        loadModel(path);
        computeLocalAABB();   // compute once after loading
    }

    // draws the model: opaque first, then transparent
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
        transparentList.reserve(meshes.size());
        for (size_t i = 0; i < meshes.size(); ++i) {
            if (meshes[i].transparent) {
                // world-space centroid
                glm::vec4 wc = modelMatrix * glm::vec4(meshes[i].centroid, 1.0f);
                float d = glm::length(glm::vec3(wc) - cameraPos);
                transparentList.push_back({i, d});
            }
        }
        std::sort(transparentList.begin(), transparentList.end(),
                  [](const TransparentEntry &a, const TransparentEntry &b){ return a.dist > b.dist; });

        // then draw transparent meshes (disable depth writes so blending works)
        glDepthMask(GL_FALSE);
        for (auto &e : transparentList) {
            meshes[e.idx].Draw(shader);
        }
        glDepthMask(GL_TRUE);
    }

    // Computes world-space axis-aligned bounding box for the model under a transform
    // Uses per-vertex positions, but you should call this only occasionally (e.g. at placement)
    void CalculateAABB(const glm::mat4 &modelMatrix, glm::vec3 &minOut, glm::vec3 &maxOut) const
    {
        minOut = glm::vec3(FLT_MAX);
        maxOut = glm::vec3(-FLT_MAX);

        for (const auto &mesh : meshes)
        {
            for (const auto &v : mesh.vertices)
            {
                glm::vec3 worldPos = glm::vec3(modelMatrix * glm::vec4(v.Position, 1.0f));

                minOut.x = std::min(minOut.x, worldPos.x);
                minOut.y = std::min(minOut.y, worldPos.y);
                minOut.z = std::min(minOut.z, worldPos.z);

                maxOut.x = std::max(maxOut.x, worldPos.x);
                maxOut.y = std::max(maxOut.y, worldPos.y);
                maxOut.z = std::max(maxOut.z, worldPos.z);
            }
        }
    }

    // Returns the model's height in its own space (after Assimp processing)
    float GetLocalHeight() const
    {
        if (!hasLocalAABB) return 1.0f;
        return localMax.y - localMin.y;
    }

    // Returns the precomputed scale factor that makes height = 1.0
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
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_FlipUVs |
            aiProcess_CalcTangentSpace);
        if(!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            std::cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
            return;
        }

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
                        if (img.contains("uri") && img["uri"].is_string()) {
                            imageUris.push_back(img["uri"].get<std::string>());
                        } else {
                            imageUris.push_back(std::string());
                        }
                        imageTransforms.push_back(UVTransform());
                    }
                }
                // materials
                if (j.contains("materials") && j["materials"].is_array()) {
                    size_t mCount = j["materials"].size();
                    materialImageRefs.resize(mCount);
                    materialBaseColorFactors.assign(mCount, glm::vec4(1.0f));
                    materialMetallicFactors.assign(mCount, 1.0f);
                    materialRoughnessFactors.assign(mCount, 1.0f);

                    for (size_t mi = 0; mi < mCount; ++mi) {
                        auto &mat = j["materials"][mi];
                        // baseColorFactor
                        if (mat.contains("pbrMetallicRoughness") &&
                            mat["pbrMetallicRoughness"].contains("baseColorFactor")) {
                            auto &f = mat["pbrMetallicRoughness"]["baseColorFactor"];
                            if (f.is_array() && f.size() >= 4) {
                                glm::vec4 bc;
                                bc.r = f[0].get<float>();
                                bc.g = f[1].get<float>();
                                bc.b = f[2].get<float>();
                                bc.a = f[3].get<float>();
                                materialBaseColorFactors[mi] = bc;
                            }
                        }
                        // metallic / roughness factors
                        if (mat.contains("pbrMetallicRoughness")) {
                            auto &pbr = mat["pbrMetallicRoughness"];
                            if (pbr.contains("metallicFactor"))
                                materialMetallicFactors[mi] = pbr["metallicFactor"].get<float>();
                            if (pbr.contains("roughnessFactor"))
                                materialRoughnessFactors[mi] = pbr["roughnessFactor"].get<float>();
                        }

                        // baseColorTexture
                        if (mat.contains("pbrMetallicRoughness") &&
                            mat["pbrMetallicRoughness"].contains("baseColorTexture")) {
                            auto &bct = mat["pbrMetallicRoughness"]["baseColorTexture"];
                            if (bct.contains("index")) {
                                materialImageRefs[mi].baseColor = bct["index"].get<int>();
                                if (bct.contains("extensions") &&
                                    bct["extensions"].contains("KHR_texture_transform")) {
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
                                    if (t.contains("rotation"))
                                        ut.rotation = t["rotation"].get<float>();
                                    int idx = materialImageRefs[mi].baseColor;
                                    if (idx >= 0 && idx < (int)imageTransforms.size())
                                        imageTransforms[idx] = ut;
                                }
                            }
                        }
                        // metallicRoughnessTexture
                        if (mat.contains("pbrMetallicRoughness") &&
                            mat["pbrMetallicRoughness"].contains("metallicRoughnessTexture")) {
                            auto &mrt = mat["pbrMetallicRoughness"]["metallicRoughnessTexture"];
                            if (mrt.contains("index"))
                                materialImageRefs[mi].metallicRoughness = mrt["index"].get<int>();
                        }
                        // normalTexture
                        if (mat.contains("normalTexture")) {
                            auto &nt = mat["normalTexture"];
                            if (nt.contains("index")) {
                                materialImageRefs[mi].normal = nt["index"].get<int>();
                                if (nt.contains("extensions") &&
                                    nt["extensions"].contains("KHR_texture_transform")) {
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
                                    if (t.contains("rotation"))
                                        ut.rotation = t["rotation"].get<float>();
                                    int idx = materialImageRefs[mi].normal;
                                    if (idx >= 0 && idx < (int)imageTransforms.size())
                                        imageTransforms[idx] = ut;
                                }
                            }
                        }
                    }
                }
            }
        } catch (...) {
            // Ignore JSON parsing errors
        }

        // process ASSIMP's root node recursively
        processNode(scene->mRootNode, scene, glm::mat4(1.0f));
    }

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
        glm::mat4 nodeTransform = parentTransform * aiMatToGlm(node->mTransformation);

        // process each mesh located at the current node
        for(unsigned int i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back(processMesh(mesh, scene, nodeTransform));
        }
        // recursively process children
        for(unsigned int i = 0; i < node->mNumChildren; i++)
        {
            processNode(node->mChildren[i], scene, nodeTransform);
        }
    }

    Mesh processMesh(aiMesh *mesh, const aiScene *scene, const glm::mat4 &nodeTransform)
    {
        vector<Vertex> vertices;
        vector<unsigned int> indices;
        vector<Texture> textures;

        // precompute normal matrix once
        glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(nodeTransform)));

        // vertices
        for(unsigned int i = 0; i < mesh->mNumVertices; i++)
        {
            Vertex vertex{};
            glm::vec3 vector;

            // positions (apply node transform)
            vector.x = mesh->mVertices[i].x;
            vector.y = mesh->mVertices[i].y;
            vector.z = mesh->mVertices[i].z;
            glm::vec4 transformedPos = nodeTransform * glm::vec4(vector, 1.0f);
            vertex.Position = glm::vec3(transformedPos);

            // normals
            if (mesh->HasNormals())
            {
                vector.x = mesh->mNormals[i].x;
                vector.y = mesh->mNormals[i].y;
                vector.z = mesh->mNormals[i].z;
                vertex.Normal = glm::normalize(normalMat * vector);
            }
            else {
                vertex.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }

            // texture coordinates + tangents/bitangents
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
                vertex.Tangent = glm::normalize(normalMat * vector);

                // bitangent
                vector.x = mesh->mBitangents[i].x;
                vector.y = mesh->mBitangents[i].y;
                vector.z = mesh->mBitangents[i].z;
                vertex.Bitangent = glm::normalize(normalMat * vector);
            }
            else {
                vertex.TexCoords  = glm::vec2(0.0f);
                vertex.Tangent    = glm::vec3(1.0f, 0.0f, 0.0f);
                vertex.Bitangent  = glm::vec3(0.0f, 0.0f, 1.0f);
            }

            // bone IDs / weights left at default (no animation wired yet)
            for (int k = 0; k < MAX_BONE_INFLUENCE; ++k) {
                vertex.m_BoneIDs[k] = 0;
                vertex.m_Weights[k] = 0.0f;
            }

            vertices.push_back(vertex);
        }

        // indices
        for(unsigned int i = 0; i < mesh->mNumFaces; i++)
        {
            aiFace face = mesh->mFaces[i];
            for(unsigned int j = 0; j < face.mNumIndices; j++)
                indices.push_back(face.mIndices[j]);        
        }

        // materials
        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

        // 1. diffuse (legacy / non-PBR)
        vector<Texture> diffuseMaps = loadMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse");
        textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());
        // 2. specular (legacy, not used directly in your PBR shader now)
        vector<Texture> specularMaps = loadMaterialTextures(material, aiTextureType_SPECULAR, "texture_specular");
        textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());
        // 3. normal maps (legacy)
        std::vector<Texture> normalMaps = loadMaterialTextures(material, aiTextureType_HEIGHT, "texture_normal");
        textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());
        // 4. height maps (legacy)
        std::vector<Texture> heightMaps = loadMaterialTextures(material, aiTextureType_AMBIENT, "texture_height");
        textures.insert(textures.end(), heightMaps.begin(), heightMaps.end());
        
        // glTF PBR info from JSON
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
                    tex.id = TextureFromFile(uri.c_str(), this->directory, true);
                    tex.type = "texture_diffuse";
                    tex.path = uri;
                    if (refs.baseColor >= 0 && refs.baseColor < (int)imageTransforms.size()) {
                        tex.uvOffset   = imageTransforms[refs.baseColor].offset;
                        tex.uvScale    = imageTransforms[refs.baseColor].scale;
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
                    tex.id = TextureFromFile(uri.c_str(), this->directory, false);
                    tex.type = "texture_normal";
                    tex.path = uri;
                    if (refs.normal >= 0 && refs.normal < (int)imageTransforms.size()) {
                        tex.uvOffset   = imageTransforms[refs.normal].offset;
                        tex.uvScale    = imageTransforms[refs.normal].scale;
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
                    tex.id = TextureFromFile(uri.c_str(), this->directory, false);
                    tex.type = "texture_metallicRoughness";
                    tex.path = uri;
                    if (refs.metallicRoughness >= 0 && refs.metallicRoughness < (int)imageTransforms.size()) {
                        tex.uvOffset   = imageTransforms[refs.metallicRoughness].offset;
                        tex.uvScale    = imageTransforms[refs.metallicRoughness].scale;
                        tex.uvRotation = imageTransforms[refs.metallicRoughness].rotation;
                    }
                    textures.push_back(tex);
                }
            }
        }

        // Heuristic: transparent if alpha < 1 or texture name hints glass/alpha/etc.
        bool isTransparent = false;
        if (mesh->mMaterialIndex >= 0 &&
            mesh->mMaterialIndex < (int)materialBaseColorFactors.size()) {
            if (materialBaseColorFactors[mesh->mMaterialIndex].a < 0.999f)
                isTransparent = true;
        }
        for (auto &t : textures) {
            std::string p = t.path;
            for (auto &c : p) c = (char)std::tolower(c);
            if (p.find("glass") != std::string::npos ||
                p.find("alpha") != std::string::npos ||
                p.find("transp") != std::string::npos) {
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
        if (mesh->mMaterialIndex >= 0 && mesh->mMaterialIndex < (int)materialMetallicFactors.size())
            matMetal = materialMetallicFactors[mesh->mMaterialIndex];
        if (mesh->mMaterialIndex >= 0 && mesh->mMaterialIndex < (int)materialRoughnessFactors.size())
            matRough = materialRoughnessFactors[mesh->mMaterialIndex];

        Mesh m = Mesh(vertices, indices, textures, bcFactor, isTransparent, matMetal, matRough);
        m.centroid = centroid;
        return m;
    }

    // checks all material textures of a given type and loads the textures if they're not loaded yet.
    vector<Texture> loadMaterialTextures(aiMaterial *mat, aiTextureType type, string typeName)
    {
        vector<Texture> textures;
        unsigned int count = mat->GetTextureCount(type);
        for(unsigned int i = 0; i < count; i++)
        {
            aiString str;
            mat->GetTexture(type, i, &str);

            if (str.length == 0 || std::strlen(str.C_Str()) == 0) {
                continue; // skip empty entries
            }

            bool skip = false;
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
                bool isGamma = (typeName == "texture_diffuse");
                texture.id = TextureFromFile(str.C_Str(), this->directory, isGamma);
                if (texture.id == 0) {
                    // failed to load, skip binding this texture
                    continue;
                }
                texture.type = typeName;
                texture.path = str.C_Str();
                textures.push_back(texture);
                textures_loaded.push_back(texture);
            }
        }
        return textures;
    }

    // compute AABB in local/model space (after node transforms baked into vertex positions)
    void computeLocalAABB()
    {
        if (meshes.empty()) {
            hasLocalAABB = false;
            normalizationScale = 1.0f;
            return;
        }

        glm::vec3 minV(FLT_MAX);
        glm::vec3 maxV(-FLT_MAX);

        for (const auto &mesh : meshes)
        {
            for (const auto &v : mesh.vertices)
            {
                minV.x = std::min(minV.x, v.Position.x);
                minV.y = std::min(minV.y, v.Position.y);
                minV.z = std::min(minV.z, v.Position.z);

                maxV.x = std::max(maxV.x, v.Position.x);
                maxV.y = std::max(maxV.y, v.Position.y);
                maxV.z = std::max(maxV.z, v.Position.z);
            }
        }

        localMin = minV;
        localMax = maxV;
        hasLocalAABB = true;

        float h = maxV.y - minV.y;
        if (h > 1e-4f) {
            normalizationScale = 1.0f / h;   // make height = 1.0
        } else {
            normalizationScale = 1.0f;
        }
    }
};


// -----------------------------------------------------------------------------
// Texture loading implementation moved to src/model.cpp
// -----------------------------------------------------------------------------

#endif
