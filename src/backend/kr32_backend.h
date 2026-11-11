#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "ir/ir.h"
#include "krc/target/target.h"

namespace krc::backend {

class Kr32Backend {
public:
    Kr32Backend(const ir::Module& module, const target::TargetProfile& profile);

    [[nodiscard]] std::optional<std::string> emit(std::string& error) const;
    [[nodiscard]] const std::string& debug_info() const { return debug_info_; }

private:
    struct FrameLayout {
        std::unordered_map<const ir::Value*, std::uint32_t> slots;
        std::unordered_map<const ir::Instruction*, std::uint32_t> alloca_offsets;
        std::uint32_t outgoing_size = 0;
        std::uint32_t float_temp = 0;
        std::uint32_t frame_size = 0;
    };

    [[nodiscard]] FrameLayout layout_function(const ir::Function& function) const;
    [[nodiscard]] std::optional<std::uint32_t> value_slot(
        const ir::Function& function, const FrameLayout& layout, const ir::Value* value,
        std::string& error) const;
    [[nodiscard]] std::uint64_t size_of(ir::TypePtr type, std::string& error) const;
    [[nodiscard]] std::uint64_t align_of(ir::TypePtr type) const;
    void emit_function(std::string& out, const ir::Function& function,
                       const FrameLayout& layout) const;
    void emit_instruction(std::string& out, const ir::Function& function,
                          const FrameLayout& layout, const ir::Instruction& instruction) const;
    void load_value(std::string& out, const ir::Function& function, const FrameLayout& layout,
                    const ir::Value* value, std::string_view reg) const;
    void load_fp(std::string& out, const ir::Function& function, const FrameLayout& layout,
                 const ir::Value* value, std::string_view reg) const;
    void store_fp(std::string& out, const ir::Function& function, const FrameLayout& layout,
                  const ir::Instruction& instruction, std::string_view reg) const;
    void add_debug_type(ir::TypePtr type) const;
    void store_value(std::string& out, const ir::Function& function,
                     const FrameLayout& layout, const ir::Instruction& instruction,
                     std::string_view reg) const;
    [[nodiscard]] std::string block_label(const ir::Function& function,
                                          const ir::BasicBlock& block) const;
    [[nodiscard]] std::string escape_string(const std::vector<std::uint8_t>& bytes) const;
    mutable std::size_t compare_label_counter_ = 0;

    const ir::Module& module_;
    const target::TargetProfile& profile_;
    mutable std::uint32_t next_debug_source_ = 0;
    mutable std::unordered_map<std::string, std::uint32_t> debug_sources_;
    mutable std::unordered_set<std::string> debug_types_;
    mutable std::string debug_info_;
};

}  // namespace krc::backend
