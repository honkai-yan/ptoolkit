#include "system_info.h"

#include <cstdlib>
#include <iostream>

int main()
{
  const auto zh = system_info::GetNameZh();
  const auto en = system_info::GetNameEn();
  const auto display = system_info::GetDisplayVersion();
  const auto build = system_info::GetBuildVersion();
  for (const auto* result : {&zh, &en, &display, &build})
  {
    // Windows permits an absent release label to return an empty string.
    if (!*result || (result != &display && result->value().empty()))
    {
      std::cerr << (*result ? "Empty system information" : result->error()) << '\n';
      return EXIT_FAILURE;
    }
    std::cout << **result << '\n';
  }
#ifdef __APPLE__
  if (*zh != "macOS" || *en != "macOS" || display->empty()) return EXIT_FAILURE;
#endif
}
