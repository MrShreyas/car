#ifndef MESH_H
#define MESH_H

#include <glad/glad.h> // holds all OpenGL type declarations

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

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
    shader.use();

    bool hasDiffuse = false;
    bool hasNormal = false;
    bool hasMR = false;

    // Bind textures and set corresponding uniforms
    for(unsigned int i = 0; i < textures.size(); i++)
    {
        glActiveTexture(GL_TEXTURE0 + i);
        string name = textures[i].type;

        if (name == "texture_diffuse" && !hasDiffuse) {
            glUniform1i(glGetUniformLocation(shader.ID, "texture_diffuse1"), i);
            glUniform4f(glGetUniformLocation(shader.ID, "texture_diffuse1_uv"), 0, 0, 1, 1);
            glUniform1f(glGetUniformLocation(shader.ID, "texture_diffuse1_rot"), 0);
            glBindTexture(GL_TEXTURE_2D, textures[i].id);
            hasDiffuse = true;
        } 
        else if (name == "texture_normal" && !hasNormal) {
            glUniform1i(glGetUniformLocation(shader.ID, "texture_normal1"), i);
            glUniform4f(glGetUniformLocation(shader.ID, "texture_normal1_uv"), 0, 0, 1, 1);
            glUniform1f(glGetUniformLocation(shader.ID, "texture_normal1_rot"), 0);
            glBindTexture(GL_TEXTURE_2D, textures[i].id);
            hasNormal = true;
        }
        else if (name == "texture_metallicRoughness" && !hasMR) {
            glUniform1i(glGetUniformLocation(shader.ID, "texture_metallicRoughness1"), i);
            glUniform4f(glGetUniformLocation(shader.ID, "texture_metallicRoughness1_uv"), 0, 0, 1, 1);
            glUniform1f(glGetUniformLocation(shader.ID, "texture_metallicRoughness1_rot"), 0);
            glBindTexture(GL_TEXTURE_2D, textures[i].id);
            hasMR = true;
        }
    }

    // Set Shader Presence Flags
    glUniform1i(glGetUniformLocation(shader.ID, "hasBaseColor"), hasDiffuse ? 1 : 0);
    glUniform1i(glGetUniformLocation(shader.ID, "hasNormalMap"), hasNormal ? 1 : 0);
    glUniform1i(glGetUniformLocation(shader.ID, "hasMetallicRoughness"), hasMR ? 1 : 0);
    
    // Set material factors (ensure these aren't 0)
    glUniform4f(glGetUniformLocation(shader.ID, "baseColorFactor"), baseColorFactor.r, baseColorFactor.g, baseColorFactor.b, baseColorFactor.a);
    glUniform1f(glGetUniformLocation(shader.ID, "metallicFactor"), metallicFactor);
    glUniform1f(glGetUniformLocation(shader.ID, "roughnessFactor"), roughnessFactor);

    // Draw mesh
    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(indices.size()), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
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
