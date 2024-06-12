#pragma once
// std
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <array>
#include <functional>
#include <deque>
#include <stack>
#include <variant>
#include <ctime>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <fstream>
#include <filesystem>
// vulkan
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
#include <vulkan/vk_enum_string_helper.h>
// volk
#include <volk/volk.h>
// vma
#include <vma/vk_mem_alloc.h>
// fmt
#define FMT_HEADER_ONLY
#include <fmt/core.h>
#include <fmt/format.h>
// glm
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_DEPTH_ZERO_TO_ONE 
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>
// sdl
//#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
// vkb
#include <vkbootstrap/VkBootstrap.h>
// imgui
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_sdl2.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/misc/cpp/imgui_stdlib.h>



#define VK_CHECK(x)                                                     \
    do {                                                                \
        VkResult err = x;                                               \
        if (err) {                                                      \
            fmt::print("Detected Vulkan error: {}\n", string_VkResult(err)); \
            abort();                                                    \
        }                                                               \
    } while (0)
