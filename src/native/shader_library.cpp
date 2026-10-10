#include "native/shader_library.h"

#include <algorithm>
#include <atomic>
#include <span>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#define XXH_INLINE_ALL
#include <xxhash.h>
#include <smolv.h>

#include <rex/logging.h>
#include <rex/ui/vulkan/device.h>
#include <rex/ui/vulkan/util.h>

#include "native/module_table.h"
#include "native/pipeline_parts.h"
#include "native/shader_pack.h"

namespace wwe::native::shader_library {

namespace {

uint32_t LoadBE32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

struct State {
  const rex::ui::vulkan::VulkanDevice* device = nullptr;
  ShaderPack pack;
  std::unique_ptr<NativeShader[]> shaders;  // parallel to pack.entries
  // Modules, parallel to pack.entries: the pipeline workers build them all at
  // startup; a module built on any other thread is counted.
  std::unique_ptr<ModuleTable<VkShaderModule>> modules;
  // Stages, parallel to pack.entries, set when the module is built.
  std::unique_ptr<std::atomic<uint32_t>[]> stages;
  std::mutex mutex;
  std::unordered_map<uint32_t, uint64_t> hash_by_object;
  uint32_t unknown_hashes = 0;
  // Shaders converted in the session (AddLive), under mutex; never removed,
  // so pointers to them stay valid.
  struct Live {
    NativeShader shader;
    VkShaderStageFlagBits stage = VkShaderStageFlagBits(0);
    bool published = false;
  };
  std::unordered_map<uint64_t, std::unique_ptr<Live>> live;
} g;

const State::Live* FindLive(uint64_t hash) {
  std::lock_guard lock(g.mutex);
  const auto it = g.live.find(hash);
  return it != g.live.end() ? it->second.get() : nullptr;
}

const ShaderPackEntry* FindEntry(uint64_t hash) {
  const ShaderPackEntry* begin = g.pack.entries.data();
  const ShaderPackEntry* end = begin + g.pack.entries.size();
  const ShaderPackEntry* it = std::lower_bound(
      begin, end, hash, [](const ShaderPackEntry& e, uint64_t h) { return e.hash < h; });
  return it != end && it->hash == hash ? it : nullptr;
}

// The slots a container's shader reads, by the FNV-1a hash its dumped file
// (and so the fetch slot table) is named with.
uint32_t FetchSlots(const uint8_t* container, size_t size,
                    const std::vector<ShaderPackFetchSlots>& table = g.pack.fetch_slots) {
  uint64_t fnv = 14695981039346656037ull;
  for (size_t i = 0; i < size; ++i) {
    fnv = (fnv ^ container[i]) * 1099511628211ull;
  }
  const ShaderPackFetchSlots* begin = table.data();
  const ShaderPackFetchSlots* end = begin + table.size();
  const ShaderPackFetchSlots* it = std::lower_bound(
      begin, end, fnv, [](const ShaderPackFetchSlots& e, uint64_t h) { return e.fnv < h; });
  return it != end && it->fnv == fnv ? it->mask : UINT32_MAX;
}

thread_local bool t_worker = false;

// The module for a pack entry's smol-v stream in spirv, and its stage.
VkShaderModule CreateModule(const ShaderPackEntry& entry, const std::vector<uint8_t>& spirv_data,
                            VkShaderStageFlagBits& stage) {
  const uint8_t* encoded = spirv_data.data() + entry.spirv_offset;
  const size_t decoded_size = smolv::GetDecodedBufferSize(encoded, entry.spirv_size);
  std::vector<uint32_t> spirv((decoded_size + 3) / 4);
  VkShaderModule module = VK_NULL_HANDLE;
  if (decoded_size && smolv::Decode(encoded, entry.spirv_size, spirv.data(), decoded_size)) {
    module = rex::ui::vulkan::util::CreateShaderModule(g.device, spirv.data(), decoded_size);
    stage = parts::ShaderStageOf(std::span<const uint32_t>(spirv.data(), decoded_size / 4));
  }
  if (!module) {
    REXLOG_ERROR("native renderer: could not build shader {:016X}", entry.hash);
  }
  return module;
}

VkShaderModule BuildModule(size_t index) {
  VkShaderStageFlagBits stage = VkShaderStageFlagBits(0);
  const VkShaderModule module = CreateModule(g.pack.entries[index], g.pack.spirv, stage);
  g.stages[index].store(uint32_t(stage), std::memory_order_release);
  return module;
}

VkShaderModule ModuleAt(const ShaderPackEntry* entry) {
  return g.modules->Get(size_t(entry - g.pack.entries.data()), BuildModule, !t_worker);
}

}  // namespace

void Initialize(const rex::ui::vulkan::VulkanDevice* device, ShaderPack pack) {
  g.pack = std::move(pack);
  g.shaders = std::make_unique<NativeShader[]>(g.pack.entries.size());
  for (size_t i = 0; i < g.pack.entries.size(); ++i) {
    g.shaders[i].hash = g.pack.entries[i].hash;
    g.shaders[i].spec_constants_mask = g.pack.entries[i].spec_constants_mask;
  }
  g.modules = std::make_unique<ModuleTable<VkShaderModule>>(g.pack.entries.size());
  g.stages = std::make_unique<std::atomic<uint32_t>[]>(g.pack.entries.size());
  g.device = device;
  REXLOG_INFO("native renderer: {} converted shaders", g.pack.entries.size());
}

bool OnShaderCreated(const uint8_t* container, uint32_t guest_object, bool is_pixel_shader) {
  if (!g.device || !container || !guest_object) {
    return false;
  }
  const size_t size = size_t(LoadBE32(container + 4)) + LoadBE32(container + 8);
  const uint64_t hash = XXH3_64bits(container, size);
  const ShaderPackEntry* entry = FindEntry(hash);
  std::lock_guard lock(g.mutex);
  g.hash_by_object[guest_object] = hash;
  if (entry) {
    NativeShader& shader = g.shaders[entry - g.pack.entries.data()];
    shader.is_pixel_shader = is_pixel_shader;
    shader.fetch_slots = FetchSlots(container, size);
    if (!is_pixel_shader && shader.elements.empty() &&
        !guest::DecodeVertexElements(container, size, shader.elements, shader.code_offset,
                                     shader.code_size)) {
      REXLOG_WARN("native renderer: vertex shader {:016X} inputs did not decode", hash);
    }
  } else if (g.live.count(hash)) {
    return true;
  } else if (g.unknown_hashes++ < 8) {
    REXLOG_WARN("native renderer: {} shader {:016X} is not in the shader pack",
                is_pixel_shader ? "pixel" : "vertex", hash);
  }
  return entry != nullptr;
}

const NativeShader* Find(uint32_t guest_object) {
  const ShaderPackEntry* entry;
  {
    std::lock_guard lock(g.mutex);
    const auto it = g.hash_by_object.find(guest_object);
    if (it == g.hash_by_object.end()) {
      return nullptr;
    }
    entry = FindEntry(it->second);
    if (!entry) {
      const auto live = g.live.find(it->second);
      return live != g.live.end() && live->second->published ? &live->second->shader : nullptr;
    }
  }
  NativeShader& shader = g.shaders[entry - g.pack.entries.data()];
  // Only the render thread asks, so only it writes shader.module.
  shader.module = ModuleAt(entry);
  return shader.module ? &shader : nullptr;
}

VkShaderModule Module(uint64_t hash) {
  if (const ShaderPackEntry* entry = FindEntry(hash)) {
    return ModuleAt(entry);
  }
  const State::Live* live = FindLive(hash);
  return live ? live->shader.module : VK_NULL_HANDLE;
}

void MarkWorkerThread() { t_worker = true; }

bool BuildNextModule() { return g.modules && g.modules->BuildNext(BuildModule); }

size_t ModuleCount() { return g.modules ? g.modules->size() : 0; }

size_t ModulesBuilt() { return g.modules ? g.modules->Built() : 0; }

void TakeRenderThreadModules(uint64_t& count, uint64_t& ns) {
  count = ns = 0;
  if (g.modules) {
    g.modules->TakeCounted(count, ns);
  }
}

uint64_t RenderThreadModules() { return g.modules ? g.modules->CountedTotal() : 0; }

VkShaderStageFlagBits Stage(uint64_t hash) {
  const ShaderPackEntry* entry = FindEntry(hash);
  if (!entry) {
    const State::Live* live = FindLive(hash);
    return live ? live->stage : VkShaderStageFlagBits(0);
  }
  if (!ModuleAt(entry)) {
    return VkShaderStageFlagBits(0);
  }
  return VkShaderStageFlagBits(
      g.stages[entry - g.pack.entries.data()].load(std::memory_order_acquire));
}

uint32_t SpecConstantsMask(uint64_t hash) {
  if (const ShaderPackEntry* entry = FindEntry(hash)) {
    return entry->spec_constants_mask;
  }
  const State::Live* live = FindLive(hash);
  return live ? live->shader.spec_constants_mask : 0;
}

bool Has(uint64_t hash) { return FindEntry(hash) != nullptr || FindLive(hash) != nullptr; }

std::span<const ShaderPackEntry> Entries() { return g.pack.entries; }

uint64_t AddLive(const ShaderPack& pack, const uint8_t* container, size_t size,
                 bool is_pixel_shader) {
  if (!g.device || pack.entries.size() != 1) {
    return 0;
  }
  const ShaderPackEntry& entry = pack.entries[0];
  if (entry.hash != XXH3_64bits(container, size) || FindEntry(entry.hash) ||
      FindLive(entry.hash)) {
    return 0;
  }
  auto live = std::make_unique<State::Live>();
  NativeShader& shader = live->shader;
  shader.hash = entry.hash;
  shader.spec_constants_mask = entry.spec_constants_mask;
  shader.is_pixel_shader = is_pixel_shader;
  shader.fetch_slots = FetchSlots(container, size, pack.fetch_slots);
  if (!is_pixel_shader && !guest::DecodeVertexElements(container, size, shader.elements,
                                                       shader.code_offset, shader.code_size)) {
    REXLOG_WARN("native renderer: vertex shader {:016X} inputs did not decode", entry.hash);
  }
  shader.module = CreateModule(entry, pack.spirv, live->stage);
  if (!shader.module) {
    return 0;
  }
  std::lock_guard lock(g.mutex);
  g.live.emplace(entry.hash, std::move(live));
  return entry.hash;
}

void Publish(uint64_t hash) {
  std::lock_guard lock(g.mutex);
  if (const auto it = g.live.find(hash); it != g.live.end()) {
    it->second->published = true;
  }
}

uint64_t Hash(uint32_t guest_object) {
  std::lock_guard lock(g.mutex);
  const auto it = g.hash_by_object.find(guest_object);
  return it != g.hash_by_object.end() ? it->second : 0;
}

}  // namespace wwe::native::shader_library
