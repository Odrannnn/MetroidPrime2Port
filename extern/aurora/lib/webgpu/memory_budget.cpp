#if _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#endif
#include <aurora/gfx.h>

#if _WIN32
#include "gpu.hpp"

#include <windows.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <mutex>

// Dawn loads dxgi.dll itself, so nothing else links it (clang-cl/MSVC).
#pragma comment(lib, "dxgi.lib")

namespace {
// The DXGI adapter Dawn picked, matched by vendor and device id (the same whichever backend runs:
// DXGI reports this process's use of the adapter's memory for D3D11, D3D12 and Vulkan alike).
Microsoft::WRL::ComPtr<IDXGIAdapter3> find_adapter() {
  const auto& info = aurora::webgpu::g_adapterInfo;
  if (info.vendorID == 0 && info.deviceID == 0) {
    return {};
  }
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    return {};
  }
  for (UINT i = 0;; ++i) {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
      return {};
    }
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc)) || desc.VendorId != info.vendorID || desc.DeviceId != info.deviceID) {
      continue;
    }
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
    if (SUCCEEDED(adapter.As(&adapter3))) {
      return adapter3;
    }
  }
}
} // namespace

bool aurora_get_gpu_memory_budget(uint64_t* budget, uint64_t* usage) {
  static std::mutex mutex;
  static Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
  static bool searched = false;
  std::lock_guard lock{mutex};
  if (!searched) {
    adapter = find_adapter();
    // Before the adapter is known, look again next time.
    searched = adapter != nullptr || aurora::webgpu::g_adapterInfo.vendorID != 0;
  }
  DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
  if (adapter == nullptr || FAILED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory))) {
    return false;
  }
  *budget = memory.Budget;
  *usage = memory.CurrentUsage;
  return true;
}
#else
bool aurora_get_gpu_memory_budget(uint64_t*, uint64_t*) { return false; }
#endif
