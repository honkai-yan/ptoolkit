#include "hardware_info.h"
#include "app.h"

#include <cstdlib>
#include <iostream>

template <class T>
T Require(std::expected<T, std::string> value)
{
  if (!value)
  {
    std::cerr << value.error() << '\n';
    std::exit(EXIT_FAILURE);
  }
  return *value;
}
int main()
{
  const auto cpu = Require(hardware_info::GetCpuBrandName());
  const auto cores = Require(hardware_info::GetCpuCoreCount());
  const auto memory = Require(hardware_info::GetTotalMemoryBytes());
  const auto disk = Require(hardware_info::GetTotalDiskBytes());
  const auto gpus = Require(hardware_info::GetGpus());
  if (cpu.empty() || !cores || !memory || !disk) return EXIT_FAILURE;
  std::cout << cpu << ", " << cores << " cores\nRAM bytes: " << memory
            << "\nDisk bytes: " << disk << '\n';
  for (const auto& gpu : gpus)
  {
    if (gpu.brand_name.empty()) return EXIT_FAILURE;
    std::cout << gpu.brand_name << ", dedicated bytes: " << gpu.dedicated_memory_bytes << '\n';
  }
  auto& app = ptoolkit::App::GetInstance();
  const auto initialized = app.Init();
  if (!initialized)
  {
    std::cerr << initialized.error() << '\n';
    return EXIT_FAILURE;
  }
  const auto& info = app.GetHardwareInfo();
  if (info.cpu.empty() || info.gpu.empty() || info.memory.empty() || info.disk.empty())
    return EXIT_FAILURE;
  if (app.GetSystemEdition().empty()) return EXIT_FAILURE;
}
