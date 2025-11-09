#ifndef MESH_H
#define MESH_H

#include <glad/glad.h> // holds all OpenGL type declarations

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <shader.h>

#include <string>
#include <vector>
using namespace std;

#define MAX_BONE_INFLUENCE 4

struct Vertex {
    // position
    glm::vec3 Position;
    // normal
    glm::vec3 Normal;
    // texCoords
    glm::vec2 TexCoords;
    // tangent
    glm::vec3 Tangent;
    // bitangent
    glm::vec3 Bitangent;
	//bone indexes which will influence this vertex
	int m_BoneIDs[MAX_BONE_INFLUENCE];
	//weights from each bone
	float m_Weights[MAX_BONE_INFLUENCE];
};

struct Texture {
    unsigned int id;
    string type;
    string path;
    // UV transform from glTF KHR_texture_transform (offset, scale, rotation)
    glm::vec2 uvOffset = glm::vec2(0.0f, 0.0f);
    glm::vec2 uvScale = glm::vec2(1.0f, 1.0f);
    float uvRotation = 0.0f;
};

class Mesh {
public:
    // mesh Data
    vector<Vertex>       vertices;
    vector<unsigned int> indices;
    vector<Texture>      textures;
    unsigned int VAO;
    // whether this mesh should be treated as transparent (draw in second pass)
    bool transparent = false;

    // baseColorFactor (r,g,b,a) applied to sampled baseColor
    glm::vec4 baseColorFactor = glm::vec4(1.0f);
    // metallic / roughness factors (per-mesh defaults; may be overridden by textures)
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    // centroid of mesh in model space (computed at load time)
    glm::vec3 centroid = glm::vec3(0.0f);

    // constructor
    Mesh(vector<Vertex> vertices, vector<unsigned int> indices, vector<Texture> textures, glm::vec4 baseColorFactor = glm::vec4(1.0f), bool transparent = false, float metallicFactor = 1.0f, float roughnessFactor = 1.0f)
    {
        this->vertices = vertices;
        this->indices = indices;
        this->textures = textures;
        this->baseColorFactor = baseColorFactor;
        this->transparent = transparent;
        this->metallicFactor = metallicFactor;
        this->roughnessFactor = roughnessFactor;

        // now that we have all the required data, set the vertex buffers and its attribute pointers.
        setupMesh();
    }

    // render the mesh
    void Draw(Shader &shader) 
    {
        // Ensure shader program is active before setting uniforms
        shader.use();
        // local helper to check GL errors immediately and print context
        auto localGlCheck = [&](const char *where) {
            GLenum e = glGetError();
            if (e != GL_NO_ERROR) {
                GLint curProg = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &curProg);
                GLint vao = 0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
                GLint ebo = 0; glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &ebo);
                GLint activeTex = 0; glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex);
                GLint maxTex = 0; glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxTex);
                std::cerr << "[Mesh Debug][GL ERROR] 0x" << std::hex << e << std::dec << " at " << where << "\n";
                std::cerr << "  shader.ID=" << shader.ID << " GL_CURRENT_PROGRAM=" << curProg << "\n";
                std::cerr << "  VAO=" << vao << " EBO=" << ebo << " ACTIVE_TEXTURE=0x" << std::hex << activeTex << std::dec << " MAX_TEX=" << maxTex << "\n";
                int inspect = std::min(maxTex, 8);
                for (int u = 0; u < inspect; ++u) {
                    GLenum unit = GL_TEXTURE0 + u;
                    glActiveTexture(unit);
                    GLint bound2D = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound2D);
                    std::cerr << "    Unit " << u << " bound2D=" << bound2D << "\n";
                }
                // restore active texture
                glActiveTexture(activeTex);
            }
        };
        // check immediately after using program
        localGlCheck("after shader.use()");
        // debug: print current program id
        static bool printedMeshDebug = false;
        // throttle GL error prints to avoid log floods that can hang the host
        static int mesh_gl_error_prints = 0;
        const int MESH_GL_ERROR_PRINT_LIMIT = 50;
        GLint curProg = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &curProg);
        // Print pairing of shader.ID and current program once for diagnosis
        if (!printedMeshDebug) {
            std::cout << "[Mesh Debug] shader.ID=" << shader.ID << " GL_CURRENT_PROGRAM=" << curProg << std::endl;
            GLint maxTexUnits = 0;
            glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxTexUnits);
            std::cout << "[Mesh Debug] GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS=" << maxTexUnits << std::endl;
        }
        // bind appropriate textures
    unsigned int diffuseNr  = 1;
        unsigned int specularNr = 1;
        unsigned int normalNr   = 1;
        unsigned int heightNr   = 1;
        bool hasDiffuse = false;
        bool hasNormalMap = false;
        bool hasMetallicRoughness = false;
        // We'll bind the first diffuse -> unit 0, normal -> unit 1, metallicRoughness -> unit 2, height -> unit 3
        const int UNIT_DIFFUSE = 0;
        const int UNIT_NORMAL = 1;
        const int UNIT_MR = 2;
        const int UNIT_HEIGHT = 3;
    GLint maxTexUnits = 0;
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxTexUnits);
        for(unsigned int i = 0; i < textures.size(); i++)
        {
            const Texture &T = textures[i];
            if (!printedMeshDebug)
            {
                std::cout << "[Mesh Debug] Consider texture idx=" << i << " type=" << T.type << " path=" << T.path << " id=" << T.id << std::endl;
            }
            if (T.type == "texture_diffuse" && !hasDiffuse)
            {
                hasDiffuse = true;
                glActiveTexture(GL_TEXTURE0 + UNIT_DIFFUSE);
                glBindTexture(GL_TEXTURE_2D, T.id);
                glUniform1i(glGetUniformLocation(shader.ID, "texture_diffuse1"), UNIT_DIFFUSE);
                localGlCheck("after set texture_diffuse1 uniform");
                GLint loc_uv = glGetUniformLocation(shader.ID, "texture_diffuse1_uv");
                if (loc_uv != -1) glUniform4f(loc_uv, T.uvOffset.x, T.uvOffset.y, T.uvScale.x, T.uvScale.y);
                GLint loc_rot = glGetUniformLocation(shader.ID, "texture_diffuse1_rot");
                if (loc_rot != -1) glUniform1f(loc_rot, T.uvRotation);
            }
            else if (T.type == "texture_normal" && !hasNormalMap)
            {
                hasNormalMap = true;
                glActiveTexture(GL_TEXTURE0 + UNIT_NORMAL);
                glBindTexture(GL_TEXTURE_2D, T.id);
                glUniform1i(glGetUniformLocation(shader.ID, "texture_normal1"), UNIT_NORMAL);
                localGlCheck("after set texture_normal1 uniform");
                GLint loc_uv = glGetUniformLocation(shader.ID, "texture_normal1_uv");
                if (loc_uv != -1) glUniform4f(loc_uv, T.uvOffset.x, T.uvOffset.y, T.uvScale.x, T.uvScale.y);
                GLint loc_rot = glGetUniformLocation(shader.ID, "texture_normal1_rot");
                if (loc_rot != -1) glUniform1f(loc_rot, T.uvRotation);
            }
            else if (T.type == "texture_metallicRoughness" && !hasMetallicRoughness)
            {
                hasMetallicRoughness = true;
                glActiveTexture(GL_TEXTURE0 + UNIT_MR);
                glBindTexture(GL_TEXTURE_2D, T.id);
                glUniform1i(glGetUniformLocation(shader.ID, "texture_metallicRoughness1"), UNIT_MR);
                localGlCheck("after set texture_metallicRoughness1 uniform");
                GLint loc_uv = glGetUniformLocation(shader.ID, "texture_metallicRoughness1_uv");
                if (loc_uv != -1) glUniform4f(loc_uv, T.uvOffset.x, T.uvOffset.y, T.uvScale.x, T.uvScale.y);
                GLint loc_rot = glGetUniformLocation(shader.ID, "texture_metallicRoughness1_rot");
                if (loc_rot != -1) glUniform1f(loc_rot, T.uvRotation);
            }
            else
            {
                // ignore other textures for now (specular, height, additional diffs)
            }
            localGlCheck((std::string("after handling texture ") + T.path).c_str());
        }
        // If no diffuse texture was found, bind the first available texture as a fallback
        if(!hasDiffuse && textures.size() > 0)
        {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, textures[0].id);
            glUniform1i(glGetUniformLocation(shader.ID, "texture_diffuse1"), 0);
            // set uv for fallback
            GLint loc_uv = glGetUniformLocation(shader.ID, "texture_diffuse1_uv");
            if (loc_uv != -1) glUniform4f(loc_uv, textures[0].uvOffset.x, textures[0].uvOffset.y, textures[0].uvScale.x, textures[0].uvScale.y);
            GLint loc_rot = glGetUniformLocation(shader.ID, "texture_diffuse1_rot");
            if (loc_rot != -1) glUniform1f(loc_rot, textures[0].uvRotation);
            hasDiffuse = true;
        }

        // set presence flags for shader
        GLint loc_hasBase = glGetUniformLocation(shader.ID, "hasBaseColor");
        if (loc_hasBase != -1) glUniform1i(loc_hasBase, hasDiffuse ? 1 : 0);
        GLint loc_hasNormal = glGetUniformLocation(shader.ID, "hasNormalMap");
        if (loc_hasNormal != -1) glUniform1i(loc_hasNormal, hasNormalMap ? 1 : 0);
        GLint loc_hasMR = glGetUniformLocation(shader.ID, "hasMetallicRoughness");
        if (loc_hasMR != -1) glUniform1i(loc_hasMR, hasMetallicRoughness ? 1 : 0);

    // set metallic/roughness factors
    GLint loc_metal = glGetUniformLocation(shader.ID, "metallicFactor");
    if (loc_metal != -1) glUniform1f(loc_metal, metallicFactor);
    GLint loc_rough = glGetUniformLocation(shader.ID, "roughnessFactor");
    if (loc_rough != -1) glUniform1f(loc_rough, roughnessFactor);

        // set baseColorFactor uniform
        GLint loc_baseColorFactor = glGetUniformLocation(shader.ID, "baseColorFactor");
        if (loc_baseColorFactor != -1) {
            glUniform4f(loc_baseColorFactor, baseColorFactor.r, baseColorFactor.g, baseColorFactor.b, baseColorFactor.a);
        }

        // draw mesh
        if (!printedMeshDebug)
        {
            std::cout << "[Mesh Debug] About to draw VAO=" << VAO << " indicesCount=" << indices.size() << " EBO bound=" << (EBO != 0) << std::endl;
            GLboolean isVAO = glIsVertexArray(VAO);
            std::cout << "[Mesh Debug] glIsVertexArray(VAO)=" << (isVAO ? "true" : "false") << std::endl;
            GLint errBefore = glGetError();
            std::cout << "[Mesh Debug] glGetError before draw: 0x" << std::hex << errBefore << std::dec << std::endl;
        }
        glBindVertexArray(VAO);
        localGlCheck("after glBindVertexArray(VAO)");
        // Safety: explicitly bind the EBO before draw in case VAO state wasn't restored properly.
        if (EBO != 0) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
            localGlCheck("after explicit glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO)");
        } else {
            std::cerr << "[Mesh Debug] Warning: mesh EBO is 0 for VAO=" << VAO << "\n";
        }
        glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(indices.size()), GL_UNSIGNED_INT, 0);
        localGlCheck("after glDrawElements");
        if (!printedMeshDebug)
        {
            GLint errAfter = glGetError();
            std::cout << "[Mesh Debug] glGetError after draw: 0x" << std::hex << errAfter << std::dec << std::endl;
            printedMeshDebug = true;
        }
        glBindVertexArray(0);

        // always good practice to set everything back to defaults once configured.
        glActiveTexture(GL_TEXTURE0);
    }

private:
    // render data 
    unsigned int VBO, EBO;

    // initializes all the buffer objects/arrays
    void setupMesh()
    {
        // create buffers/arrays
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);
        // load data into vertex buffers
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        // A great thing about structs is that their memory layout is sequential for all its items.
        // The effect is that we can simply pass a pointer to the struct and it translates perfectly to a glm::vec3/2 array which
        // again translates to 3/2 floats which translates to a byte array.
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);  

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

        // set the vertex attribute pointers
        // vertex Positions
        glEnableVertexAttribArray(0);	
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        // vertex normals
        glEnableVertexAttribArray(1);	
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
        // vertex texture coords
        glEnableVertexAttribArray(2);	
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
        // vertex tangent
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));
        // vertex bitangent
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));
		// ids
		glEnableVertexAttribArray(5);
		glVertexAttribIPointer(5, 4, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, m_BoneIDs));

		// weights
		glEnableVertexAttribArray(6);
		glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, m_Weights));
        glBindVertexArray(0);
    }
};
#endif
