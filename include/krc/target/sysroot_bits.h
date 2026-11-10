#pragma once

#include <string>

#include "krc/target/target.h"

namespace krc::target {

[[nodiscard]] std::string generate_bits_types(const TargetProfile& profile);
[[nodiscard]] std::string generate_bits_stdint(const TargetProfile& profile);

}  // namespace krc::target
