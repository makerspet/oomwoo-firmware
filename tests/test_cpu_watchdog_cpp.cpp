#include "oomwoo_cpu_watchdog.h"

#include <cstdint>
#include <type_traits>

static void hard_stop(void *context, oomwoo_cpu_stop_reason_t reason) {
  auto *count = static_cast<std::uint32_t *>(context);
  if (reason != OOMWOO_CPU_STOP_NONE) {
    ++(*count);
  }
}

int main() {
  static_assert(std::is_standard_layout<oomwoo_cpu_watchdog_t>::value,
                "watchdog state must remain usable in static C++ storage");

  oomwoo_cpu_watchdog_t watchdog{};
  std::uint32_t stop_count = 0;
  const oomwoo_cpu_watchdog_config_t config{
      150U,
      hard_stop,
      &stop_count,
  };

  if (!oomwoo_cpu_watchdog_init(&watchdog, &config) || stop_count != 1U) {
    return 1;
  }
  return oomwoo_cpu_watchdog_motion_permitted(&watchdog) ? 1 : 0;
}
