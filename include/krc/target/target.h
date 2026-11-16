#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace krc::target {

enum class TargetId : std::uint8_t {
    Kr32,
    Kr16,
    Kr64,
};

enum class TargetFamily : std::uint8_t {
    Register,
    Accumulator,
    Stack,
};

enum class Endianness : std::uint8_t {
    Little,
    Big,
};

enum class DataModel : std::uint8_t {
    Ilp32,
    Ip16,
    Lp64,
};

// docs/targets/schema.md's ABI `aggregate_rules`: how a by-value aggregate is
// passed. `RegistersOrMemory` classifies by size (docs/abi.md: an aggregate of
// eight bytes or less stays in integer registers, an FP-only one of nine to
// sixteen in two FPRs, everything else in memory). `MemoryOnly` passes every
// aggregate as a hidden pointer, which is what a target without a register path
// needs -- and it is a property of the target, not something code generation may
// infer from a size threshold that only happens to correlate with it.
enum class AggregateRules : std::uint8_t {
    MemoryOnly,
    RegistersOrMemory,
};

struct TargetProfile {
    TargetId id;
    std::string_view name;
    std::string_view macro;
    TargetFamily family;
    std::uint32_t pointer_bits;
    Endianness endianness;
    DataModel data_model;
    std::uint32_t char_bits;
    bool char_unsigned;
    std::uint32_t short_bits;
    std::uint32_t int_bits;
    std::uint32_t long_bits;
    std::uint32_t long_long_bits;
    std::uint32_t float_bits;
    std::uint32_t double_bits;
    std::uint32_t long_double_bits;
    std::uint32_t size_t_bits;
    std::uint32_t ptrdiff_bits;
    std::uint32_t intmax_bits;
    std::uint32_t wchar_bits;
    bool has_fpu;
    bool has_matrix;
    bool has_atomics;
    bool has_threads;
    bool has_eh;
    bool has_rtti;
    bool has_tls;
    bool has_cpp;
    bool backend_available;
    AggregateRules aggregate_rules;
};

[[nodiscard]] std::string_view to_string(TargetId id) noexcept;
[[nodiscard]] std::string_view to_string(TargetFamily family) noexcept;
[[nodiscard]] std::string_view to_string(DataModel model) noexcept;
[[nodiscard]] std::string_view to_string(Endianness endianness) noexcept;

[[nodiscard]] std::optional<TargetId> parse_target(std::string_view name) noexcept;
[[nodiscard]] const TargetProfile& get_target(TargetId id);
[[nodiscard]] std::span<const TargetProfile> target_profiles() noexcept;

}  // namespace krc::target
