cmake_minimum_required(VERSION 3.18.1)

project("glvulkantest_native")

set (SOURCE
glvulkantest.cpp
)

set (INCLUDE_DIR
.
../java/com.example.glvulkantest.Native
)

set(CMAKE_CXX_STANDARD 17)

add_compile_options(-fPIC)

include_directories(${INCLUDE_DIR})

add_library(glvulkantest_native SHARED ${SOURCE})
target_link_libraries(glvulkantest_native android log EGL GLESv3 vulkan)

