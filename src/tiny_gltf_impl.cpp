// tiny_gltf_impl.cpp
//
// This file provides the actual implementation of TinyGLTF + stb_image
// Make sure "tiny_gltf.h" is in your include path.

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

// If you are using Windows, also define this to avoid warnings about sprintf
#define _CRT_SECURE_NO_WARNINGS

#include "tiny_gltf.h"
