#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/peephole.h"
#include "ir/ir.h"
#include "krc/target/target.h"

namespace krc::backend {

// Emits the kr216 C subset fixed by docs/abi-kr216.md: 16- and 32-bit integer
// scalars, pointers, arrays and memory-class aggregates, with ENTER/LEAVE
// frames, R2-R5 arguments and the R6 aggregate destination. Floating point,
// 64-bit integers and exceptions are diagnosed rather than lowered.
class Kr216Backend {
public:
    Kr216Backend(const ir::Module& module, const target::TargetProfile& profile,
                 OptimizationLevel level = OptimizationLevel::O0);

    [[nodiscard]] std::optional<std::string> emit(std::string& error) const;
    [[nodiscard]] const std::string& debug_info() const { return debug_info_; }
    [[nodiscard]] std::uint32_t frame_bytes(const ir::Function& function) const;

private:
    struct FrameLayout {
        std::unordered_map<const ir::Value*, std::uint32_t> slots;
        std::unordered_map<const ir::Instruction*, std::uint32_t> allocas;
        std::uint32_t return_pointer = 0;
        bool has_return_pointer = false;
        std::uint32_t eh_scratch = 0;
        bool has_eh_scratch = false;
        std::uint32_t frame_size = 0;
    };

    [[nodiscard]] FrameLayout layout_function(const ir::Function& function,
                                              std::string& error) const;
    void emit_function(std::string& out, const ir::Function& function,
                       const FrameLayout& layout, std::string& error) const;
    void add_debug_type(ir::TypePtr type) const;

    const ir::Module& module_;
    const target::TargetProfile& profile_;
    OptimizationLevel level_ = OptimizationLevel::O0;
    mutable std::string debug_info_;
    mutable std::unordered_map<std::string, std::uint32_t> debug_sources_;
    mutable std::unordered_set<std::string> debug_types_;
    mutable std::uint32_t next_debug_source_ = 0;
    struct EhRegion {
        std::string start;
        std::string end;
        std::string pad;
    };
    mutable std::vector<EhRegion> eh_regions_;
};

}  // namespace krc::backend
