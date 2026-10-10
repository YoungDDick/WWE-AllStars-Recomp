// The game's shaders, converted ahead of time by XenosRecomp, matched to the
// guest shader objects D3D creates.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <rex/ui/vulkan/api.h>

#include "native/guest_decode.h"
#include "native/shader_pack.h"

namespace rex::ui::vulkan {
class VulkanDevice;
}

namespace wwe::native {

struct NativeShader {
  uint64_t hash = 0;  // XXH3 of the container, as in the shader pack
  VkShaderModule module = VK_NULL_HANDLE;
  uint32_t spec_constants_mask = 0;
  // Texture fetch slots the shader reads (all when unknown).
  uint32_t fetch_slots = UINT32_MAX;
  bool is_pixel_shader = false;
  // Vertex shaders: the elements and the code offset in the microcode, to
  // decode the live (declaration-patched) vfetches at draw time.
  std::vector<guest::VertexElementRef> elements;
  uint32_t code_size = 0;
  uint32_t code_offset = 0;
};

namespace shader_library {

// Takes the shader pack the session loaded (LoadShaderPackFile).
void Initialize(const rex::ui::vulkan::VulkanDevice* device, ShaderPack pack);

// Guest render thread, from the Create{Vertex,Pixel}Shader hooks: container is
// the host pointer to the shader container, guest_object the created shader.
// True when the loaded shader pack holds the shader (false without a pack).
bool OnShaderCreated(const uint8_t* container, uint32_t guest_object, bool is_pixel_shader);

// The converted shader for a guest shader object; nullptr if the object is
// unknown, its hash is not in the cache or its module failed. Render thread.
const NativeShader* Find(uint32_t guest_object);

// The container hash of a guest shader object (0 if unknown), for debugging.
uint64_t Hash(uint32_t guest_object);

// The module of a converted shader by container hash; VK_NULL_HANDLE if the
// cache has no such shader. Any thread.
VkShaderModule Module(uint64_t hash);

// Modules are built at startup by the pipeline workers, each of which calls
// MarkWorkerThread once and then BuildNextModule until it returns false. A
// module first needed on any other thread is built there and counted.
void MarkWorkerThread();
bool BuildNextModule();
size_t ModuleCount();
// Modules built so far, on any thread (progress of the prepare mode).
size_t ModulesBuilt();

// Modules built off the workers since the last call, and the time spent.
void TakeRenderThreadModules(uint64_t& count, uint64_t& ns);
// Modules built off the workers this session (scripts/analyze_hitches.py
// reads them as render-thread module creations).
uint64_t RenderThreadModules();

// The stage of a pack shader, read from its SPIR-V when its module is built
// (built here if no worker has yet); 0 if unknown. Any thread.
VkShaderStageFlagBits Stage(uint64_t hash);

// The spec-constant bits a pack shader reads; 0 if it is not in the pack.
uint32_t SpecConstantsMask(uint64_t hash);

// Whether the shader pack holds a shader with this container hash.
bool Has(uint64_t hash);

// The loaded pack's shaders, sorted by hash (for the prebuild plan).
std::span<const ShaderPackEntry> Entries();

// Live top-ups (live_topup.h): a shader the loaded pack lacks, converted in
// the session. AddLive takes its one-shader pack and container, builds its
// module and makes it known to Module, Stage, SpecConstantsMask and Has;
// Find returns it only once Publish is called (after its pipeline libraries
// are built), so a draw never meets a half-ready shader. Converter thread.
// Returns the shader's container hash, 0 if it could not be added.
uint64_t AddLive(const ShaderPack& pack, const uint8_t* container, size_t size,
                 bool is_pixel_shader);
void Publish(uint64_t hash);

}  // namespace shader_library

}  // namespace wwe::native
