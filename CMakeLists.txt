cmake_minimum_required(VERSION 3.18)

project("foray-denoiser-nrd")

MESSAGE("--- << CMAKE of ${PROJECT_NAME} >> --- ")
MESSAGE(STATUS "CURRENT SOURCE DIR \"${CMAKE_CURRENT_SOURCE_DIR}\"")#

add_subdirectory("nvidia-ray-tracing-denoiser")

# Enable strict mode for own code
SET(CMAKE_CXX_FLAGS ${STRICT_FLAGS})

# collect sources
file(GLOB_RECURSE src "src/*.cpp")
add_library(${PROJECT_NAME} ${src})

# find vulkan
find_package(Vulkan REQUIRED)

target_link_libraries(
	${PROJECT_NAME}
	PUBLIC foray
    PUBLIC NRD
)

if (WIN32)
    target_link_libraries(
		${PROJECT_NAME}
		PUBLIC ${SDL2_LIBRARIES}
	)

endif()

target_include_directories(
	${PROJECT_NAME}
	PUBLIC "${CMAKE_SOURCE_DIR}/foray/src"
	PUBLIC "${CMAKE_SOURCE_DIR}/foray/third_party"
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}/nvidia-ray-tracing-denoiser/Include"
	PUBLIC ${Vulkan_INCLUDE_DIRS}
)

target_compile_options(${PROJECT_NAME} PUBLIC "-DNRD_SHADER_DIR=\"${CMAKE_CURRENT_LIST_DIR}/src/shaders\"")
