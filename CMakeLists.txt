cmake_minimum_required(VERSION 3.10)
project(CarViewer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Assimp paths ---
# Ideally these should be found via find_package or set via environment variables.
# For now, we will assume they are in a known relative location or let the user configure (best practice).
# But to simply fix the hardcoding to "katal", we will try to look locally or assume standard install.
# However, given the user has a specific structure `development/assimp`, we can try to make it relative to the project root if they are side-by-side.
# Assuming standard structure:
# car/
# assimp/
set(ASSIMP_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../assimp/installed/include")
set(ASSIMP_LIB_DIR "${CMAKE_SOURCE_DIR}/../assimp/installed/lib")

include_directories(${ASSIMP_INCLUDE_DIR})
link_directories(${ASSIMP_LIB_DIR})

add_executable(main main.cpp src/glad.c src/tiny_gltf_impl.cpp src/Shader.cpp src/Camera.cpp src/Application.cpp src/CarDemo.cpp src/model.cpp src/ResourceManager.cpp src/Vehicle.cpp src/ParticleSystem.cpp)
target_include_directories(main PRIVATE include ${CMAKE_SOURCE_DIR}/src)

# Link libraries
# If tinyexr sources are present in src/, add them and define HAS_TINYEXR
if(EXISTS "${CMAKE_SOURCE_DIR}/src/tinyexr.c" OR EXISTS "${CMAKE_SOURCE_DIR}/src/tinyexr.cpp" OR EXISTS "${CMAKE_SOURCE_DIR}/src/tinyexr.h")
	message(STATUS "Found tinyexr sources in src/ - adding to build and defining HAS_TINYEXR")
	# Prefer compiling tinyexr.cpp if present (tinyexr uses C++ headers). Fall back to tinyexr.c
	if(EXISTS "${CMAKE_SOURCE_DIR}/src/tinyexr.cpp")
		target_sources(main PRIVATE src/tinyexr.cpp)
	elseif(EXISTS "${CMAKE_SOURCE_DIR}/src/tinyexr.c")
		target_sources(main PRIVATE src/tinyexr.c)
	endif()

	# If miniz implementation is present, add it so linking succeeds for tinyexr
	if(EXISTS "${CMAKE_SOURCE_DIR}/src/miniz.c")
		target_sources(main PRIVATE src/miniz.c)
	endif()
	# Also add other miniz translation units if present (tdef/tinfl/zip)
	if(EXISTS "${CMAKE_SOURCE_DIR}/src/miniz_tdef.c")
		target_sources(main PRIVATE src/miniz_tdef.c)
	endif()
	if(EXISTS "${CMAKE_SOURCE_DIR}/src/miniz_tinfl.c")
		target_sources(main PRIVATE src/miniz_tinfl.c)
	endif()
	if(EXISTS "${CMAKE_SOURCE_DIR}/src/miniz_zip.c")
		target_sources(main PRIVATE src/miniz_zip.c)
	endif()
	target_compile_definitions(main PRIVATE HAS_TINYEXR=1)
endif()

target_link_libraries(main PRIVATE assimp glfw3 opengl32 gdi32 dwmapi)
