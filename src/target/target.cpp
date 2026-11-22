#include "krc/target/target.h"

#include <array>
#include <cassert>
#include <string_view>

namespace krc::target {

namespace {

constexpr TargetProfile kKr32{
    TargetId::Kr32,
    "kr32",
    "__kr32__",
    TargetFamily::Register,
    32,
    Endianness::Little,
    DataModel::Ilp32,
    8,
    false,
    16,
    32,
    32,
    64,
    32,
    64,
    64,
    32,
    32,
    64,
    32,
    true,
    true,
    true,
    true,
    /*has_eh=*/true,
    false,
    true,
    true,
    true,
    AggregateRules::RegistersOrMemory,
};

constexpr TargetProfile kKr16{
    TargetId::Kr16,
    "kr16",
    "__kr16__",
    TargetFamily::Register,
    16,
    Endianness::Little,
    DataModel::Ip16,
    8,
    true,
    16,
    16,
    16,
    0,
    0,
    0,
    0,
    16,
    16,
    16,
    16,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    true,
    AggregateRules::MemoryOnly,
};

constexpr TargetProfile kKr64{
    TargetId::Kr64,
    "kr64",
    "__kr64__",
    TargetFamily::Register,
    64,
    Endianness::Little,
    DataModel::Lp64,
    8,
    false,
    16,
    32,
    64,
    64,
    32,
    64,
    64,
    64,
    64,
    64,
    32,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    AggregateRules::RegistersOrMemory,
};

constexpr TargetProfile kKr216{
    TargetId::Kr216,
    "kr216",
    "__kr216__",
    TargetFamily::Register,
    16,
    Endianness::Little,
    DataModel::Ip16Long32,
    8,
    false,
    16,
    16,
    32,
    64,
    32,
    64,
    64,
    16,
    16,
    64,
    16,
    true,
    false,
    false,
    false,
    false,
    true,
    false,
    true,
    true,
    AggregateRules::RegistersOrMemory,
};

constexpr std::array<TargetProfile, 4> kProfiles{kKr32, kKr16, kKr64, kKr216};

}  // namespace

std::string_view to_string(TargetId id) noexcept {
    switch (id) {
    case TargetId::Kr32:
        return "kr32";
    case TargetId::Kr16:
        return "kr16";
    case TargetId::Kr64:
        return "kr64";
    case TargetId::Kr216:
        return "kr216";
    }
    return "unknown";
}

std::string_view to_string(TargetFamily family) noexcept {
    switch (family) {
    case TargetFamily::Register:
        return "register";
    case TargetFamily::Accumulator:
        return "accumulator";
    case TargetFamily::Stack:
        return "stack";
    }
    return "unknown";
}

std::string_view to_string(DataModel model) noexcept {
    switch (model) {
    case DataModel::Ilp32:
        return "ilp32";
    case DataModel::Ip16:
        return "ip16";
    case DataModel::Lp64:
        return "lp64";
    case DataModel::Ip16Long32:
        return "ip16-long32";
    }
    return "unknown";
}

std::string_view to_string(Endianness endianness) noexcept {
    switch (endianness) {
    case Endianness::Little:
        return "little";
    case Endianness::Big:
        return "big";
    }
    return "unknown";
}

std::optional<TargetId> parse_target(std::string_view name) noexcept {
    if (name == "kr32") {
        return TargetId::Kr32;
    }
    if (name == "kr16") {
        return TargetId::Kr16;
    }
    if (name == "kr64") {
        return TargetId::Kr64;
    }
    if (name == "kr216") {
        return TargetId::Kr216;
    }
    return std::nullopt;
}

const TargetProfile& get_target(TargetId id) {
    const auto index = static_cast<std::size_t>(id);
    assert(index < kProfiles.size());
    return kProfiles[index];
}

std::span<const TargetProfile> target_profiles() noexcept { return kProfiles; }

}  // namespace krc::target
