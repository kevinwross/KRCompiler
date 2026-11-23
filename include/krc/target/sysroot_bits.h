#pragma once

#include <string>

#include "krc/target/target.h"

namespace krc::target {

[[nodiscard]] std::string generate_bits_types(const TargetProfile& profile);
[[nodiscard]] std::string generate_bits_stdint(const TargetProfile& profile);
// The floating-point constants of the target's own format: a profile whose float
// is 64 bits gets the double limits, and one without floating point gets none.
[[nodiscard]] std::string generate_bits_float(const TargetProfile& profile);

}  // namespace krc::target
