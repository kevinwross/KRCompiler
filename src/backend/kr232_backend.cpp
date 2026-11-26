#include "kr232_backend.h"

#include "kr232_cfg.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace krc::backend {
namespace {

// The ABI's frame is a multiple of the 4-byte stack alignment.
std::uint32_t align_frame(std::uint32_t bytes) { return (bytes + 3u) & ~3u; }

// The alignment a type requires. A packed record aligns to one byte, which is
// what makes a three-char record three bytes rather than twelve -- the ABI's
// size-exact copy depends on it.
std::uint32_t type_bytes(const ir::Type* type);

std::uint32_t type_align(const ir::Type* type) {
    if (type == nullptr) {
        return 1;
    }
    switch (type->kind) {
        case ir::TypeKind::Void:
            return 1;
        case ir::TypeKind::Pointer:
            return 4;
        case ir::TypeKind::Integer:
        case ir::TypeKind::Float: {
            const std::uint32_t bytes = (type->bits + 7u) / 8u;
            return bytes < 4u ? (bytes == 0 ? 1u : bytes) : 4u;
        }
        case ir::TypeKind::Array:
            return type_align(type->element);
        case ir::TypeKind::Struct:
        case ir::TypeKind::Union: {
            if (type->packed) {
                return 1;
            }
            std::uint32_t alignment = 1;
            for (const ir::Type* field : type->fields) {
                const std::uint32_t field_align = type_align(field);
                if (field_align > alignment) {
                    alignment = field_align;
                }
            }
            return alignment;
        }
        case ir::TypeKind::Function:
            return 1;
    }
    return 1;
}

// kr232 sizes: a pointer is 32 bits, an integer or float is `bits` wide, an array
// is its element times its count, a record is its fields laid out at their natural
// offsets. `type_size` is private to each backend, so this backend owns its own.
std::uint32_t type_bytes(const ir::Type* type) {
    if (type == nullptr) {
        return 0;
    }
    switch (type->kind) {
        case ir::TypeKind::Void:
            return 0;
        case ir::TypeKind::Pointer:
            return 4;
        case ir::TypeKind::Integer:
        case ir::TypeKind::Float:
            return (type->bits + 7u) / 8u;
        case ir::TypeKind::Array:
            return type_bytes(type->element) * static_cast<std::uint32_t>(type->count);
        case ir::TypeKind::Struct:
        case ir::TypeKind::Union: {
            // Fields take their natural offsets; a packed record pads nothing. A
            // three-char record is three bytes, which the ABI's size-exact copy
            // depends on.
            std::uint32_t bytes = 0;
            for (const ir::Type* field : type->fields) {
                const std::uint32_t field_align = type->packed ? 1u : type_align(field);
                bytes = (bytes + field_align - 1u) & ~(field_align - 1u);
                bytes += type_bytes(field);
            }
            const std::uint32_t record_align = type->packed ? 1u : type_align(type);
            return (bytes + record_align - 1u) & ~(record_align - 1u);
        }
        case ir::TypeKind::Function:
            return 0;
    }
    return 0;
}

// The byte offset of a record's field, which is what a member index means.
std::uint32_t field_offset(const ir::Type* record, std::size_t index) {
    std::uint32_t bytes = 0;
    for (std::size_t i = 0; i < record->fields.size(); ++i) {
        const ir::Type* field = record->fields[i];
        const std::uint32_t field_align = record->packed ? 1u : type_align(field);
        bytes = (bytes + field_align - 1u) & ~(field_align - 1u);
        if (i == index) {
            return bytes;
        }
        bytes += type_bytes(field);
    }
    return bytes;
}

// R6 is the value scratch and R8 the address scratch. They are separate on
// purpose: materialising an address is `ld`, which overwrites its register, and a
// shared pool let that overwrite a value four times.
constexpr unsigned kValueReg = 6;
constexpr unsigned kSecondReg = 7;
constexpr unsigned kAddressReg = 8;

bool takes_two_slots(const ir::Type* type) {
    return type != nullptr && type->kind == ir::TypeKind::Float &&
           (type->bits + 7u) / 8u >= 8u;
}

struct SlotEntity {
    const ir::Instruction* instruction = nullptr;
    std::uint32_t slots = 1;
    bool align_even = false;
};

// Frame-resident entities in descending order of how often the function reads
// them. The emitter assigns the first slots to the hottest entities, which puts
// them in the signed-byte FP range and lets them use direct addressing instead of
// materialising an address through R8.
std::vector<SlotEntity> slots_by_frequency(const ir::Function& function) {
    std::unordered_map<const ir::Value*, std::size_t> uses;
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block->instructions) {
            for (const auto* operand : instruction->operands) {
                if (operand != nullptr) {
                    ++uses[operand];
                }
            }
            for (const auto& incoming : instruction->incoming) {
                if (incoming.first != nullptr) {
                    ++uses[incoming.first];
                }
            }
        }
    }

    std::vector<SlotEntity> entities;
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block->instructions) {
            if (instruction->opcode == ir::Opcode::Alloca) {
                entities.push_back(
                    SlotEntity{instruction.get(),
                               std::max<std::uint32_t>(
                                   1u, align_frame(type_bytes(instruction->allocated_type)) /
                                           4u),
                               false});
                continue;
            }
            if (instruction->type == nullptr ||
                instruction->type->kind == ir::TypeKind::Void) {
                continue;
            }
            const bool two_slots = takes_two_slots(instruction->type);
            entities.push_back(
                SlotEntity{instruction.get(), two_slots ? 2u : 1u, two_slots});
        }
    }
    std::stable_sort(entities.begin(), entities.end(),
                     [&](const SlotEntity& left, const SlotEntity& right) {
                         const auto left_uses = uses.find(left.instruction);
                         const auto right_uses = uses.find(right.instruction);
                         const std::size_t left_count =
                             left_uses == uses.end() ? 0 : left_uses->second;
                         const std::size_t right_count =
                             right_uses == uses.end() ? 0 : right_uses->second;
                         return left_count > right_count;
                     });
    return entities;
}

struct ValueSlotPlan {
    std::unordered_map<const ir::Instruction*, std::uint32_t> slots;
    std::uint32_t next_slot = 0;
};

// First-fit slot coloring for one-word values. A value's interval starts at the
// earliest position that reads it and ends at the latest; two intervals that do
// not overlap may share a frame slot. Allocas are excluded because their
// addresses can escape, and two-slot floating-point values are placed separately.
ValueSlotPlan plan_one_word_value_slots(const ir::Function& function,
                                        std::uint32_t base) {
    std::unordered_map<const ir::Value*, std::size_t> first;
    std::unordered_map<const ir::Value*, std::size_t> last;
    std::size_t position = 0;
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block->instructions) {
            if (instruction->opcode != ir::Opcode::Alloca &&
                instruction->type != nullptr &&
                instruction->type->kind != ir::TypeKind::Void) {
                first[instruction.get()] = position;
                last[instruction.get()] = position;
            }
            for (const auto* operand : instruction->operands) {
                if (operand == nullptr) {
                    continue;
                }
                const auto inserted = first.emplace(operand, position);
                if (inserted.second) {
                    last[operand] = position;
                } else {
                    last[operand] = std::max(last[operand], position);
                }
            }
            for (const auto& incoming : instruction->incoming) {
                if (incoming.first == nullptr) {
                    continue;
                }
                const auto inserted = first.emplace(incoming.first, position);
                if (inserted.second) {
                    last[incoming.first] = position;
                } else {
                    last[incoming.first] =
                        std::max(last[incoming.first], position);
                }
            }
            ++position;
        }
    }

    struct Interval {
        const ir::Instruction* instruction;
        std::size_t block;
        std::size_t first;
        std::size_t last;
    };
    std::vector<Interval> intervals;
    for (std::size_t block_index = 0; block_index < function.blocks.size();
         ++block_index) {
        for (const auto& instruction : function.blocks[block_index]->instructions) {
            if (instruction->opcode == ir::Opcode::Alloca ||
                instruction->type == nullptr ||
                instruction->type->kind == ir::TypeKind::Void ||
                takes_two_slots(instruction->type)) {
                continue;
            }
            intervals.push_back(
                Interval{instruction.get(), block_index, first[instruction.get()],
                         last[instruction.get()]});
        }
    }
    std::stable_sort(intervals.begin(), intervals.end(),
                     [](const Interval& left, const Interval& right) {
                         return left.first < right.first;
                     });

    ValueSlotPlan plan;
    plan.next_slot = base;
    std::vector<std::size_t> expiry;
    std::vector<std::size_t> slot_block;
    for (const auto& interval : intervals) {
        std::uint32_t assigned = plan.next_slot;
        for (std::size_t slot = 0; slot < expiry.size(); ++slot) {
            if (slot_block[slot] == interval.block &&
                expiry[slot] < interval.first) {
                assigned = base + static_cast<std::uint32_t>(slot);
                expiry[slot] = interval.last;
                break;
            }
        }
        if (assigned == plan.next_slot) {
            expiry.push_back(interval.last);
            slot_block.push_back(interval.block);
            plan.next_slot = base + static_cast<std::uint32_t>(expiry.size());
        }
        plan.slots[interval.instruction] = assigned;
    }
    return plan;
}

}  // namespace

Kr232Backend::Kr232Backend(const ir::Module& module, const target::TargetProfile& profile,
                           OptimizationLevel level)
    : module_(module), level_(level) {
    // The factory passes the target profile to every backend; this one takes its
    // widths from constants, so the parameter is part of the contract and nothing
    // more. Keeping a field for it was dead weight.
    (void)profile;
}

std::uint32_t Kr232Backend::frame_bytes(const ir::Function& function) const {
    // The size must be the slot count the emitter will use, computed the same way:
    // an alloca reserves its whole size rounded to words, a value reserves one word
    // and an eight-byte value two, aligned to an even slot. Counting bytes here
    // instead -- no rounding, no alignment, no two-slot values -- made ENTER
    // allocate less than the body wrote to, so a function's spills landed in its
    // caller's frame.
    bool holds_eight_bytes = false;
    for (const auto& parameter : function.parameters) {
        holds_eight_bytes = holds_eight_bytes || type_bytes(parameter->type) > 4u;
    }
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block->instructions) {
            if (instruction->opcode == ir::Opcode::Alloca &&
                type_bytes(instruction->allocated_type) > 4u) {
                holds_eight_bytes = true;
            }
        }
    }
    std::uint32_t next_slot = holds_eight_bytes ? 1u : 0u;
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block->instructions) {
            if (instruction->opcode == ir::Opcode::Alloca) {
                next_slot +=
                    std::max<std::uint32_t>(
                        1u, align_frame(type_bytes(instruction->allocated_type)) / 4u);
            }
        }
    }
    const auto plan = plan_one_word_value_slots(function, next_slot);
    next_slot = plan.next_slot;
    for (const auto& entity : slots_by_frequency(function)) {
        if (entity.instruction->opcode == ir::Opcode::Alloca ||
            !takes_two_slots(entity.instruction->type)) {
            continue;
        }
        if ((next_slot % 2u) != 0u) {
            ++next_slot;
        }
        next_slot += 2u;
    }
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block->instructions) {
            if (instruction->opcode == ir::Opcode::Call && instruction->type != nullptr &&
                (instruction->type->kind == ir::TypeKind::Struct ||
                 instruction->type->kind == ir::TypeKind::Union ||
                 instruction->type->kind == ir::TypeKind::Array)) {
                next_slot += align_frame(type_bytes(instruction->type)) / 4u;
            }
        }
    }
    return 4u + 4u * next_slot;
}

namespace {

// A floating-point constant's label is its IEEE-754 bit pattern in hex. It used to
// be std::hash<double>, which is implementation-defined: the same source produced
// different labels under MSVC and libstdc++, so an image was not reproducible
// across hosts.
std::string fp_label(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    std::ostringstream label;
    label << ".Lfp" << std::hex << bits;
    return label.str();
}

}  // namespace

namespace {

std::string_view trim_text(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

bool parse_register(std::string_view text, unsigned& reg) {
    text = trim_text(text);
    if (text.size() < 2 || text.front() != 'r') {
        return false;
    }
    unsigned value = 0;
    for (std::size_t index = 1; index < text.size(); ++index) {
        if (text[index] < '0' || text[index] > '9') {
            return false;
        }
        value = value * 10 + static_cast<unsigned>(text[index] - '0');
    }
    if (value >= 16) {
        return false;
    }
    reg = value;
    return true;
}

bool parse_frame_slot(std::string_view text, int& slot) {
    text = trim_text(text);
    if (text.size() < 6 || text.front() != '[' || text.back() != ']' ||
        text.substr(1, 4) != "r13-") {
        return false;
    }
    int value = 0;
    const auto body = text.substr(5, text.size() - 6);
    if (body.empty()) {
        return false;
    }
    for (const char character : body) {
        if (character < '0' || character > '9') {
            return false;
        }
        value = value * 10 + (character - '0');
    }
    slot = -value;
    return true;
}

bool is_branch(std::string_view mnemonic) {
    return mnemonic == "bra" || mnemonic == "jmp" || mnemonic == "beq" ||
           mnemonic == "bne" || mnemonic == "blt" || mnemonic == "bgt" ||
           mnemonic == "ble" || mnemonic == "bge" || mnemonic == "bcs" ||
           mnemonic == "bcc";
}

bool writes_register(std::string_view mnemonic) {
    return mnemonic == "mov" || mnemonic == "ld" || mnemonic == "ldbu" ||
           mnemonic == "ldbs" || mnemonic == "ldhu" || mnemonic == "ldhs" ||
           mnemonic == "add" || mnemonic == "sub" || mnemonic == "mul" ||
           mnemonic == "div" || mnemonic == "mod" || mnemonic == "and" ||
           mnemonic == "or" || mnemonic == "xor" || mnemonic == "shl" ||
           mnemonic == "shr" || mnemonic == "sar" || mnemonic == "neg" ||
           mnemonic == "not" || mnemonic == "pop";
}

// Replaces a frame load with the register that last stored the slot. The pass
// is conservative: labels, branches, calls and any store through an unknown
// pointer forget every fact. A register is valid only until something writes it.
std::string forward_frame_loads(std::string_view assembly) {
    std::istringstream input{std::string(assembly)};
    std::string line;
    std::string result;
    std::unordered_map<int, int> slot_register;
    bool register_valid[16] = {};
    int value_ids[16] = {};
    int next_value = 1;
    const auto forget_all = [&]() {
        slot_register.clear();
        for (std::size_t index = 0; index < 16; ++index) {
            register_valid[index] = false;
            value_ids[index] = next_value++;
        }
    };
    const auto forget_register = [&](unsigned reg) {
        register_valid[reg] = false;
        value_ids[reg] = next_value++;
        for (auto entry = slot_register.begin(); entry != slot_register.end();) {
            if (entry->second == static_cast<int>(reg)) {
                entry = slot_register.erase(entry);
            } else {
                ++entry;
            }
        }
    };
    forget_all();
    while (std::getline(input, line)) {
        const auto text = trim_text(line);
        if (text.empty() || text.front() == ';') {
            result += line;
            result += '\n';
            continue;
        }
        if (text.back() == ':') {
            // `.Ldbg` only names a source line; a real label can be a target.
            if (text.rfind(".Ldbg.", 0) != 0) {
                forget_all();
            }
            result += line;
            result += '\n';
            continue;
        }
        const auto space = text.find_first_of(" \t");
        const auto mnemonic =
            space == std::string_view::npos ? text : text.substr(0, space);
        if (mnemonic == "st" || mnemonic == "stb" || mnemonic == "stw") {
            const auto open = text.find('[');
            const auto close = text.find(']');
            const auto comma = text.find(',', close == std::string_view::npos ? 0 : close);
            if (open != std::string_view::npos && close != std::string_view::npos &&
                comma != std::string_view::npos) {
                int slot = 0;
                unsigned source = 0;
                if (parse_frame_slot(text.substr(open, close - open + 1), slot) &&
                    parse_register(text.substr(comma + 1), source)) {
                    slot_register[-slot] = static_cast<int>(source);
                    register_valid[source] = true;
                } else {
                    forget_all();
                }
            } else {
                forget_all();
            }
            result += line;
            result += '\n';
            continue;
        }
        if (mnemonic == "ld" || mnemonic == "ldbu" || mnemonic == "ldbs" ||
            mnemonic == "ldhu" || mnemonic == "ldhs") {
            const auto comma = text.find(',');
            unsigned destination = 0;
            int slot = 0;
            if (comma != std::string_view::npos &&
                parse_register(text.substr(mnemonic.size(), comma - mnemonic.size()),
                               destination) &&
                parse_frame_slot(text.substr(comma + 1), slot)) {
                const auto found_slot = slot_register.find(-slot);
                const int source =
                    found_slot == slot_register.end() ? -1 : found_slot->second;
                if (source >= 0 && register_valid[static_cast<unsigned>(source)] &&
                    destination != 13 && destination != 15) {
                    if (static_cast<unsigned>(source) == destination) {
                        continue;
                    }
                    forget_register(destination);
                    register_valid[destination] = true;
                    value_ids[destination] = value_ids[source];
                    slot_register[-slot] = static_cast<int>(destination);
                    result += "  mov r";
                    result += std::to_string(destination);
                    result += ", r";
                    result += std::to_string(source);
                    result += '\n';
                    continue;
                }
            }
            if (comma != std::string_view::npos &&
                parse_register(text.substr(mnemonic.size(), comma - mnemonic.size()),
                               destination)) {
                forget_register(destination);
                int loaded_slot = 0;
                if (parse_frame_slot(text.substr(comma + 1), loaded_slot) &&
                    destination != 13 && destination != 15) {
                    slot_register[-loaded_slot] = static_cast<int>(destination);
                    register_valid[destination] = true;
                }
            } else {
                forget_all();
            }
            result += line;
            result += '\n';
            continue;
        }
        if (mnemonic == "mov") {
            const auto comma = text.find(',');
            unsigned destination = 0;
            unsigned source = 0;
            if (comma != std::string_view::npos &&
                parse_register(text.substr(mnemonic.size(), comma - mnemonic.size()),
                               destination) &&
                parse_register(text.substr(comma + 1), source)) {
                if (value_ids[destination] == value_ids[source]) {
                    continue;
                }
                forget_register(destination);
                register_valid[destination] = register_valid[source];
                value_ids[destination] = value_ids[source];
            } else {
                forget_all();
            }
            result += line;
            result += '\n';
            continue;
        }
        if (mnemonic == "call" || mnemonic == "enter" || mnemonic == "leave" ||
            mnemonic == "ret" || is_branch(mnemonic)) {
            forget_all();
            result += line;
            result += '\n';
            continue;
        }
        if (mnemonic == "cmp" || mnemonic == "push") {
            result += line;
            result += '\n';
            continue;
        }
        if (mnemonic.size() > 1 && mnemonic.front() == 'f') {
            // Normally an F-register operation, but `f2i r6, f0` writes an
            // integer destination.
            const auto comma = text.find(',');
            unsigned destination = 0;
            if (comma != std::string_view::npos &&
                parse_register(text.substr(mnemonic.size(), comma - mnemonic.size()),
                               destination)) {
                forget_register(destination);
            }
            result += line;
            result += '\n';
            continue;
        }
        if (writes_register(mnemonic)) {
            const auto comma = text.find(',');
            unsigned destination = 0;
            if (comma != std::string_view::npos &&
                parse_register(text.substr(mnemonic.size(), comma - mnemonic.size()),
                               destination)) {
                forget_register(destination);
            } else {
                forget_all();
            }
            result += line;
            result += '\n';
            continue;
        }
        forget_all();
        result += line;
        result += '\n';
    }
    return result;
}

std::string coalesce_argument_moves(std::string_view assembly) {
    std::istringstream input{std::string(assembly)};
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    std::vector<bool> dead(lines.size(), false);
    std::vector<std::string> replacements(lines.size());
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto move = trim_text(lines[index]);
        if (move.rfind("mov r", 0) != 0) {
            continue;
        }
        const auto move_comma = move.find(',');
        if (move_comma == std::string_view::npos) {
            continue;
        }
        const auto target = std::string(trim_text(
            move.substr(4, move_comma - 4)));
        if (trim_text(move.substr(move_comma + 1)) != "r6" ||
            (target != "r2" && target != "r3" && target != "r4" &&
             target != "r5")) {
            continue;
        }
        std::vector<std::size_t> stores;
        std::size_t at = index;
        while (at > 0) {
            --at;
            const auto candidate = trim_text(lines[at]);
            if (candidate.empty() || candidate.front() == ';' ||
                (candidate.back() == ':' &&
                 candidate.rfind(".Ldbg.", 0) == 0)) {
                continue;
            }
            const auto space = candidate.find_first_of(" \t");
            const auto mnemonic =
                space == std::string_view::npos ? candidate
                                                : candidate.substr(0, space);
            if (mnemonic != "st" && mnemonic != "stb" && mnemonic != "stw") {
                break;
            }
            const auto comma = candidate.find(',');
            if (comma == std::string_view::npos ||
                trim_text(candidate.substr(comma + 1)) != "r6") {
                stores.clear();
                break;
            }
            const auto address = trim_text(candidate.substr(
                mnemonic.size(), comma - mnemonic.size()));
            if (address.find(target) != std::string_view::npos) {
                stores.clear();
                break;
            }
            stores.push_back(at);
        }
        if (stores.empty() || at == 0) {
            continue;
        }
        const auto producer = trim_text(lines[at]);
        const auto producer_space = producer.find_first_of(" \t");
        if (producer_space == std::string_view::npos) {
            continue;
        }
        const auto producer_mnemonic = producer.substr(0, producer_space);
        const auto producer_comma = producer.find(',');
        if (producer_comma == std::string_view::npos ||
            trim_text(producer.substr(producer_space,
                                      producer_comma - producer_space)) != "r6") {
            continue;
        }
        const auto producer_rest =
            std::string(trim_text(producer.substr(producer_comma + 1)));
        const bool is_load = producer_mnemonic == "ld" ||
                             producer_mnemonic == "ldbu" ||
                             producer_mnemonic == "ldbs" ||
                             producer_mnemonic == "ldhu" ||
                             producer_mnemonic == "ldhs";
        const bool is_two_address =
            producer_mnemonic == "add" || producer_mnemonic == "sub" ||
            producer_mnemonic == "and" || producer_mnemonic == "or" ||
            producer_mnemonic == "xor" || producer_mnemonic == "shl" ||
            producer_mnemonic == "shr" || producer_mnemonic == "sar" ||
            producer_mnemonic == "neg" || producer_mnemonic == "not";
        std::size_t base_index = at;
        std::string base_mnemonic;
        std::string base_rest;
        if (is_two_address) {
            if (producer_rest.find("r6") != std::string::npos) {
                continue;
            }
            std::size_t base = at;
            while (base > 0) {
                --base;
                const auto candidate = trim_text(lines[base]);
                if (candidate.empty() || candidate.front() == ';' ||
                    (candidate.back() == ':' &&
                     candidate.rfind(".Ldbg.", 0) == 0)) {
                    continue;
                }
                break;
            }
            if (base == at || base >= lines.size()) {
                continue;
            }
            const auto base_text = trim_text(lines[base]);
            const auto base_space = base_text.find_first_of(" \t");
            if (base_space == std::string_view::npos) {
                continue;
            }
            base_mnemonic = std::string(base_text.substr(0, base_space));
            const auto base_load = base_mnemonic == "ld" ||
                                   base_mnemonic == "ldbu" ||
                                   base_mnemonic == "ldbs" ||
                                   base_mnemonic == "ldhu" ||
                                   base_mnemonic == "ldhs";
            if (!base_load && base_mnemonic != "mov") {
                continue;
            }
            const auto base_comma = base_text.find(',');
            if (base_comma == std::string_view::npos ||
                trim_text(base_text.substr(
                    base_space, base_comma - base_space)) != "r6") {
                continue;
            }
            base_rest = std::string(trim_text(base_text.substr(base_comma + 1)));
            base_index = base;
        } else if (!is_load && producer_mnemonic != "mov") {
            continue;
        }
        if (base_index != at) {
            replacements[base_index] = "  " + base_mnemonic + " " + target +
                                       ", " + base_rest;
            replacements[at] = "  " + std::string(producer_mnemonic) + " " +
                               target + ", " + producer_rest;
        } else {
            replacements[at] = "  " + std::string(producer_mnemonic) + " " +
                               target + ", " + producer_rest;
        }
        for (const auto store : stores) {
            const auto store_text = trim_text(lines[store]);
            const auto store_space = store_text.find_first_of(" \t");
            const auto store_mnemonic = store_text.substr(0, store_space);
            const auto store_comma = store_text.find(',');
            const auto address = trim_text(store_text.substr(
                store_mnemonic.size(), store_comma - store_mnemonic.size()));
            replacements[store] = "  " + std::string(store_mnemonic) + " " +
                                  std::string(address) + ", " + target;
        }
        dead[index] = true;
    }
    std::string result;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (dead[index]) {
            continue;
        }
        if (!replacements[index].empty()) {
            result += replacements[index];
            result += '\n';
            continue;
        }
        result += lines[index];
        result += '\n';
    }
    return result;
}

std::string reuse_frame_addresses(std::string_view assembly) {
    std::istringstream input{std::string(assembly)};
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    const auto next_instruction = [&](std::size_t start) {
        std::size_t at = start;
        while (at < lines.size()) {
            const auto candidate = trim_text(lines[at]);
            if (candidate.empty() || candidate.front() == ';' ||
                (candidate.back() == ':' &&
                 candidate.rfind(".Ldbg.", 0) == 0)) {
                ++at;
                continue;
            }
            break;
        }
        return at;
    };
    const auto writes_r8 = [](std::string_view candidate) {
        const auto space = candidate.find_first_of(" \t");
        if (space == std::string_view::npos) {
            return false;
        }
        const auto mnemonic = candidate.substr(0, space);
        if (mnemonic != "mov" && mnemonic != "ld" && mnemonic != "ldbu" &&
            mnemonic != "ldbs" && mnemonic != "ldhu" && mnemonic != "ldhs" &&
            mnemonic != "add" && mnemonic != "sub" && mnemonic != "and" &&
            mnemonic != "or" && mnemonic != "xor" && mnemonic != "shl" &&
            mnemonic != "shr" && mnemonic != "sar" && mnemonic != "neg" &&
            mnemonic != "not" && mnemonic != "pop") {
            return false;
        }
        const auto comma = candidate.find(',');
        const auto first = trim_text(candidate.substr(
            mnemonic.size(), comma == std::string_view::npos
                                  ? std::string_view::npos
                                  : comma - mnemonic.size()));
        return first == "r8";
    };
    std::vector<bool> dead(lines.size(), false);
    std::size_t expected_arithmetic = lines.size();
    bool valid = false;
    long current_offset = 0;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto text = trim_text(lines[index]);
        if (text.empty() || text.front() == ';' ||
            (text.back() == ':' && text.rfind(".Ldbg.", 0) == 0)) {
            continue;
        }
        if (text.back() == ':') {
            valid = false;
            continue;
        }
        const auto space = text.find_first_of(" \t");
        const auto mnemonic =
            space == std::string_view::npos ? text : text.substr(0, space);
        if (mnemonic == "call" || is_branch(mnemonic)) {
            valid = false;
            continue;
        }
        if (index == expected_arithmetic) {
            expected_arithmetic = lines.size();
            continue;
        }
        if (text == "mov r8, r13") {
            const auto arithmetic = next_instruction(index + 1);
            if (arithmetic >= lines.size()) {
                valid = false;
                continue;
            }
            const auto arithmetic_text = trim_text(lines[arithmetic]);
            const auto arithmetic_space =
                arithmetic_text.find_first_of(" \t");
            if (arithmetic_space == std::string_view::npos) {
                valid = false;
                continue;
            }
            const auto arithmetic_mnemonic =
                arithmetic_text.substr(0, arithmetic_space);
            const auto comma = arithmetic_text.find(',');
            if ((arithmetic_mnemonic != "sub" && arithmetic_mnemonic != "add") ||
                comma == std::string_view::npos) {
                valid = false;
                continue;
            }
            const auto immediate =
                trim_text(arithmetic_text.substr(comma + 1));
            if (immediate.empty() || immediate.front() != '#') {
                valid = false;
                continue;
            }
            char* stop = nullptr;
            const std::string immediate_value(immediate.substr(1));
            const long value = std::strtol(immediate_value.c_str(), &stop, 0);
            if (stop == nullptr || *stop != '\0') {
                valid = false;
                continue;
            }
            const long offset =
                arithmetic_mnemonic == "sub" ? -value : value;
            if (valid && offset == current_offset) {
                dead[index] = true;
                dead[arithmetic] = true;
            } else {
                current_offset = offset;
                valid = true;
                expected_arithmetic = arithmetic;
            }
            continue;
        }
        if (writes_r8(text)) {
            valid = false;
        }
    }
    std::string result;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (dead[index]) {
            continue;
        }
        result += lines[index];
        result += '\n';
    }
    return result;
}

std::string fold_frame_addresses(std::string_view assembly) {
    std::istringstream input{std::string(assembly)};
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    const auto register_text = [](unsigned reg) {
        return "r" + std::to_string(reg);
    };
    const auto next_instruction = [&](std::size_t start) {
        std::size_t at = start;
        while (at < lines.size()) {
            const auto candidate = trim_text(lines[at]);
            if (candidate.empty() || candidate.front() == ';' ||
                (candidate.back() == ':' &&
                 candidate.rfind(".Ldbg.", 0) == 0)) {
                ++at;
                continue;
            }
            break;
        }
        return at;
    };
    const auto mentions = [](std::string_view text, std::string_view reg) {
        std::size_t count = 0;
        for (auto at = text.find(reg); at != std::string_view::npos;
             at = text.find(reg, at + reg.size())) {
            const bool before_ok =
                at == 0 || (!(text[at - 1] >= '0' && text[at - 1] <= '9') &&
                            text[at - 1] != 'r');
            const auto after = at + reg.size();
            const bool after_ok =
                after >= text.size() || !(text[after] >= '0' && text[after] <= '9');
            if (before_ok && after_ok) {
                ++count;
            }
        }
        return count;
    };
    const auto dead_after = [&](std::size_t start, std::string_view reg) {
        for (std::size_t at = next_instruction(start + 1); at < lines.size();
             at = next_instruction(at + 1)) {
            const auto candidate = trim_text(lines[at]);
            if (candidate.back() == ':') {
                return false;
            }
            const auto space = candidate.find_first_of(" \t");
            const auto mnemonic =
                space == std::string_view::npos ? candidate
                                                : candidate.substr(0, space);
            if (mnemonic == "ret" || mnemonic == "halt") {
                return true;
            }
            if (mnemonic == "call" || is_branch(mnemonic)) {
                return false;
            }
            const auto count = mentions(candidate, reg);
            if (count == 0) {
                continue;
            }
            if (count != 1) {
                return false;
            }
            const bool writes =
                mnemonic == "mov" || mnemonic == "ld" || mnemonic == "ldbu" ||
                mnemonic == "ldbs" || mnemonic == "ldhu" ||
                mnemonic == "ldhs" || mnemonic == "add" || mnemonic == "sub" ||
                mnemonic == "and" || mnemonic == "or" || mnemonic == "xor" ||
                mnemonic == "shl" || mnemonic == "shr" || mnemonic == "sar" ||
                mnemonic == "neg" || mnemonic == "not" || mnemonic == "pop";
            if (!writes) {
                return false;
            }
            const auto comma = candidate.find(',');
            const auto first = trim_text(candidate.substr(
                mnemonic.size(), comma == std::string_view::npos
                                      ? std::string_view::npos
                                      : comma - mnemonic.size()));
            unsigned written = 0;
            return parse_register(first, written) && written != 0 &&
                   mnemonic.size() != 0 &&
                   ("r" + std::to_string(written)) == reg;
        }
        return true;
    };
    std::vector<bool> dead(lines.size(), false);
    std::vector<std::string> replacements(lines.size());
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto move = trim_text(lines[index]);
        if (move.rfind("mov ", 0) != 0) {
            continue;
        }
        const auto move_comma = move.find(',');
        if (move_comma == std::string_view::npos) {
            continue;
        }
        unsigned destination = 0;
        unsigned source = 0;
        if (!parse_register(move.substr(4, move_comma - 4), destination) ||
            !parse_register(move.substr(move_comma + 1), source) ||
            source != 13 || destination == 13) {
            continue;
        }
        const auto destination_name = register_text(destination);
        const auto arithmetic = next_instruction(index + 1);
        if (arithmetic >= lines.size()) {
            continue;
        }
        const auto arithmetic_text = trim_text(lines[arithmetic]);
        const auto arithmetic_space = arithmetic_text.find_first_of(" \t");
        if (arithmetic_space == std::string_view::npos) {
            continue;
        }
        const auto arithmetic_mnemonic =
            arithmetic_text.substr(0, arithmetic_space);
        if (arithmetic_mnemonic != "add" && arithmetic_mnemonic != "sub") {
            continue;
        }
        const auto arithmetic_comma = arithmetic_text.find(',');
        if (arithmetic_comma == std::string_view::npos) {
            continue;
        }
        unsigned arithmetic_destination = 0;
        if (!parse_register(arithmetic_text.substr(
                                arithmetic_space,
                                arithmetic_comma - arithmetic_space),
                            arithmetic_destination) ||
            arithmetic_destination != destination) {
            continue;
        }
        const auto immediate_text = trim_text(
            arithmetic_text.substr(arithmetic_comma + 1));
        if (immediate_text.empty() || immediate_text.front() != '#') {
            continue;
        }
        char* stop = nullptr;
        const std::string immediate_value(immediate_text.substr(1));
        const long value = std::strtol(immediate_value.c_str(), &stop, 0);
        if (stop == nullptr || *stop != '\0') {
            continue;
        }
        const long delta = arithmetic_mnemonic == "sub" ? -value : value;
        const auto access = next_instruction(arithmetic + 1);
        if (access >= lines.size()) {
            continue;
        }
        const auto access_text = trim_text(lines[access]);
        const auto access_space = access_text.find_first_of(" \t");
        if (access_space == std::string_view::npos) {
            continue;
        }
        const auto access_mnemonic = access_text.substr(0, access_space);
        const bool is_load = access_mnemonic == "ld" ||
                             access_mnemonic == "ldbu" ||
                             access_mnemonic == "ldbs" ||
                             access_mnemonic == "ldhu" ||
                             access_mnemonic == "ldhs";
        const bool is_store = access_mnemonic == "st" ||
                              access_mnemonic == "stb" ||
                              access_mnemonic == "stw";
        if (!is_load && !is_store) {
            continue;
        }
        const auto access_comma = access_text.find(',');
        if (access_comma == std::string_view::npos) {
            continue;
        }
        std::string load_destination;
        std::string store_value;
        auto address = trim_text(access_text.substr(access_comma + 1));
        if (is_load) {
            load_destination = std::string(trim_text(access_text.substr(
                access_mnemonic.size(),
                access_comma - access_mnemonic.size())));
        } else {
            address = trim_text(access_text.substr(
                access_mnemonic.size(),
                access_comma - access_mnemonic.size()));
            store_value = std::string(trim_text(access_text.substr(access_comma + 1)));
        }
        if (address.size() < 3 || address.front() != '[') {
            continue;
        }
        const auto body = address.substr(1, address.size() - 2);
        if (body.rfind(destination_name, 0) != 0) {
            continue;
        }
        const auto rest = body.substr(destination_name.size());
        long displacement = 0;
        if (!rest.empty()) {
            if (rest.front() != '+' && rest.front() != '-') {
                continue;
            }
            char* rest_stop = nullptr;
            const std::string rest_value(rest);
            displacement = std::strtol(rest_value.c_str(), &rest_stop, 0);
            if (rest_stop == nullptr || *rest_stop != '\0') {
                continue;
            }
        }
        const long total = delta + displacement;
        if (total < -128 || total > 127) {
            continue;
        }
        std::string frame_address = "[r13";
        if (total >= 0) {
            frame_address += "+";
        }
        frame_address += std::to_string(total);
        frame_address += "]";
        replacements[access] =
            is_load
                ? "  " + std::string(access_mnemonic) + " " + load_destination +
                      ", " + frame_address
                : "  " + std::string(access_mnemonic) + " " + frame_address +
                      ", " + store_value;
        if (dead_after(access, destination_name)) {
            dead[index] = true;
            dead[arithmetic] = true;
        }
    }
    std::string result;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (dead[index]) {
            continue;
        }
        if (!replacements[index].empty()) {
            result += replacements[index];
            result += '\n';
            continue;
        }
        result += lines[index];
        result += '\n';
    }
    return result;
}

std::string coalesce_address_moves(std::string_view assembly) {
    std::istringstream input{std::string(assembly)};
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    const auto register_text = [](unsigned reg) {
        return "r" + std::to_string(reg);
    };
    const auto count_register = [](std::string_view text, std::string_view reg) {
        std::size_t count = 0;
        for (auto at = text.find(reg); at != std::string_view::npos;
             at = text.find(reg, at + reg.size())) {
            const bool before_ok =
                at == 0 || (!(text[at - 1] >= '0' && text[at - 1] <= '9') &&
                            text[at - 1] != 'r');
            const auto after = at + reg.size();
            const bool after_ok =
                after >= text.size() || !(text[after] >= '0' && text[after] <= '9');
            if (before_ok && after_ok) {
                ++count;
            }
        }
        return count;
    };
    std::vector<bool> dead(lines.size(), false);
    std::vector<std::string> replacements(lines.size());
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto move = trim_text(lines[index]);
        if (move.rfind("mov ", 0) != 0) {
            continue;
        }
        const auto move_comma = move.find(',');
        if (move_comma == std::string_view::npos) {
            continue;
        }
        unsigned destination = 0;
        unsigned source = 0;
        if (!parse_register(move.substr(4, move_comma - 4), destination) ||
            !parse_register(move.substr(move_comma + 1), source) ||
            destination == source || destination == 0 || destination == 1 ||
            destination == 13 || destination == 14 || destination == 15) {
            continue;
        }
        const auto destination_name = register_text(destination);
        const auto source_name = register_text(source);
        std::size_t next = index + 1;
        while (next < lines.size()) {
            const auto candidate = trim_text(lines[next]);
            if (candidate.empty() || candidate.front() == ';' ||
                (candidate.back() == ':' &&
                 candidate.rfind(".Ldbg.", 0) == 0)) {
                ++next;
                continue;
            }
            break;
        }
        if (next >= lines.size()) {
            continue;
        }
        const auto operation = trim_text(lines[next]);
        const auto space = operation.find_first_of(" \t");
        if (space == std::string_view::npos) {
            continue;
        }
        const auto mnemonic = operation.substr(0, space);
        const bool is_load = mnemonic == "ld" || mnemonic == "ldbu" ||
                             mnemonic == "ldbs" || mnemonic == "ldhu" ||
                             mnemonic == "ldhs";
        const bool is_store = mnemonic == "st" || mnemonic == "stb" ||
                              mnemonic == "stw";
        if (!is_load && !is_store) {
            continue;
        }
        const auto comma = operation.find(',');
        if (comma == std::string_view::npos) {
            continue;
        }
        std::string text_operand;
        std::string value_operand;
        auto address = trim_text(operation.substr(comma + 1));
        if (is_load) {
            text_operand = std::string(trim_text(operation.substr(
                mnemonic.size(), comma - mnemonic.size())));
        } else {
            address = trim_text(operation.substr(
                mnemonic.size(), comma - mnemonic.size()));
            value_operand = std::string(trim_text(operation.substr(comma + 1)));
        }
        if (address.size() < 3 || address.front() != '[') {
            continue;
        }
        std::string rewritten(address);
        bool changed = false;
        for (auto at = rewritten.find(destination_name);
             at != std::string::npos;
             at = rewritten.find(destination_name, at + source_name.size())) {
            const bool before_ok = at == 0 || rewritten[at - 1] != 'r';
            const auto after = at + destination_name.size();
            const bool after_ok =
                after >= rewritten.size() ||
                !(rewritten[after] >= '0' && rewritten[after] <= '9');
            if (before_ok && after_ok) {
                rewritten.replace(at, destination_name.size(), source_name);
                changed = true;
            }
        }
        if (!changed) {
            continue;
        }
        if (is_store && value_operand == destination_name) {
            value_operand = source_name;
        }
        bool safe = is_load && text_operand == destination_name;
        if (!safe) {
            for (std::size_t after = next + 1; after < lines.size(); ++after) {
                const auto candidate = trim_text(lines[after]);
                if (candidate.empty() || candidate.front() == ';' ||
                    (candidate.back() == ':' &&
                     candidate.rfind(".Ldbg.", 0) == 0)) {
                    continue;
                }
                if (candidate.back() == ':') {
                    break;
                }
                const auto candidate_space = candidate.find_first_of(" \t");
                const auto candidate_mnemonic =
                    candidate_space == std::string_view::npos
                        ? candidate
                        : candidate.substr(0, candidate_space);
                if (candidate_mnemonic == "ret" || candidate_mnemonic == "halt") {
                    safe = destination_name != "r2";
                    break;
                }
                if (candidate_mnemonic == "call" ||
                    is_branch(candidate_mnemonic)) {
                    break;
                }
                const auto count = count_register(candidate, destination_name);
                if (count == 0) {
                    continue;
                }
                if (count != 1) {
                    break;
                }
                const bool writes =
                    candidate_mnemonic == "mov" ||
                    candidate_mnemonic == "ld" ||
                    candidate_mnemonic == "ldbu" ||
                    candidate_mnemonic == "ldbs" ||
                    candidate_mnemonic == "ldhu" ||
                    candidate_mnemonic == "ldhs" ||
                    candidate_mnemonic == "add" ||
                    candidate_mnemonic == "sub" ||
                    candidate_mnemonic == "mul" ||
                    candidate_mnemonic == "div" ||
                    candidate_mnemonic == "mod" ||
                    candidate_mnemonic == "and" ||
                    candidate_mnemonic == "or" ||
                    candidate_mnemonic == "xor" ||
                    candidate_mnemonic == "shl" ||
                    candidate_mnemonic == "shr" ||
                    candidate_mnemonic == "sar" ||
                    candidate_mnemonic == "neg" ||
                    candidate_mnemonic == "not" ||
                    candidate_mnemonic == "pop";
                if (!writes) {
                    break;
                }
                const auto candidate_comma = candidate.find(',');
                const auto first_operand = trim_text(candidate.substr(
                    candidate_mnemonic.size(),
                    candidate_comma == std::string_view::npos
                        ? std::string_view::npos
                        : candidate_comma - candidate_mnemonic.size()));
                unsigned written = 0;
                safe = parse_register(first_operand, written) &&
                       written == destination;
                break;
            }
        }
        if (safe) {
            dead[index] = true;
        }
        replacements[next] =
            is_load
                ? "  " + std::string(mnemonic) + " " + text_operand + ", " +
                      rewritten
                : "  " + std::string(mnemonic) + " " + rewritten + ", " +
                      value_operand;
    }
    std::string result;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (dead[index]) {
            continue;
        }
        if (!replacements[index].empty()) {
            result += replacements[index];
            result += '\n';
            continue;
        }
        result += lines[index];
        result += '\n';
    }
    return result;
}

}  // namespace

std::optional<std::string> Kr232Backend::emit(std::string& error) const {
    debug_info_.clear();
    std::ostringstream out;
    out << "; kr232 backend, optimization level " << static_cast<int>(level_) << "\n";
    // The floating-point constants this module uses, each one an eight-byte entry
    // in `.data`; a constant is loaded with `fld` from its address.
    std::vector<double> fp_constants;
    for (const auto& function : module_.functions()) {
        for (const auto& block : function->blocks) {
            for (const auto& instruction : block->instructions) {
                for (const ir::Value* operand : instruction->operands) {
                    const auto* constant = dynamic_cast<const ir::ConstantFloat*>(operand);
                    if (constant == nullptr) {
                        continue;
                    }
                    if (std::find(fp_constants.begin(), fp_constants.end(), constant->value) ==
                        fp_constants.end()) {
                        fp_constants.push_back(constant->value);
                    }
                }
            }
        }
    }
    if (!fp_constants.empty()) {
        out << ".data\n";
        for (const double value : fp_constants) {
            std::ostringstream label;
            label << fp_label(value);
            out << label.str() << ":\n";
            std::uint64_t bits = 0;
            std::memcpy(&bits, &value, sizeof bits);
            out << "  .word " << (bits & 0xFFFFFFFFu) << "\n";
            out << "  .word " << ((bits >> 32) & 0xFFFFFFFFu) << "\n";
        }
    }
    if (!module_.globals().empty()) {
        out << ".data\n";
        for (const auto& global : module_.globals()) {
            if (global->external) {
                continue;
            }
            if (!global->name.empty() && global->name.front() != '.') {
                if (global->weak) {
                    out << ".weak " << global->name << "\n";
                } else if (!global->internal) {
                    out << ".globl " << global->name << "\n";
                }
            }
            out << global->name << ":\n";
            // A table of addresses is what a vtable is: one relocated word per
            // element, naming the function or object it points at.
            if (const auto* addresses =
                    dynamic_cast<const ir::ConstantAddressArray*>(global->initializer)) {
                for (const ir::Value* element : addresses->elements) {
                    // kr232 reserves the word below the vptr for type information
                    // and requires it to be zero, unlike the Itanium ABI's pointer
                    // to a typeinfo object. The IR names that slot with a `_ZTI`
                    // symbol, so its word is emitted as zero.
                    if (const auto* named = dynamic_cast<const ir::GlobalVariable*>(element)) {
                        if (named->name.rfind("_ZTI", 0) == 0) {
                            out << "  .word 0\n";
                            continue;
                        }
                    }
                    if (const auto* function = dynamic_cast<const ir::Function*>(element)) {
                        out << "  .word " << function->name << "\n";
                    } else if (const auto* target =
                                   dynamic_cast<const ir::GlobalVariable*>(element)) {
                        out << "  .word " << target->name << "\n";
                    } else {
                        out << "  .word 0\n";
                    }
                }
                continue;
            }
            const auto* bytes = dynamic_cast<const ir::ConstantDataArray*>(global->initializer);
            if (bytes != nullptr && !bytes->bytes.empty()) {
                for (const std::uint8_t byte : bytes->bytes) {
                    out << "  .byte " << static_cast<unsigned>(byte) << "\n";
                }
            } else if (const auto* integer =
                           dynamic_cast<const ir::ConstantInt*>(global->initializer)) {
                const auto bits = static_cast<std::uint64_t>(integer->value);
                out << "  .word " << (bits & 0xFFFFFFFFu) << "\n";
                if (global->element_type && global->element_type->kind == ir::TypeKind::Integer &&
                    global->element_type->bits > 32) {
                    out << "  .word " << ((bits >> 32) & 0xFFFFFFFFu) << "\n";
                }
            } else {
                // A global with no initialiser still occupies its whole type. One
                // word was emitted for every such object, so the linker packed the
                // next global four bytes in: `block[6]` and two counters overlapped,
                // and a write to block[2] silently changed a counter.
                const ir::Type* allocated = global->element_type != nullptr ? global->element_type : global->type;
                std::uint32_t remaining = type_bytes(allocated);
                if (remaining == 0) {
                    remaining = 4;
                }
                for (; remaining >= 4; remaining -= 4) {
                    out << "  .word 0\n";
                }
                if (remaining != 0) {
                    out << "  .byte 0\n";
                }
            }
        }
    }

    std::size_t next_function_label = 0;
    for (const auto& function : module_.functions()) {
        if (function->declaration) {
            continue;
        }
        const std::string name = function->name;
        // Block, compare and indirect-call labels belong to one function. Two
        // functions both emitting `.Lbb0` left the assembler binding every branch
        // to whichever it saw first, so a branch could leave its own function --
        // which is how cons_puts lost its loop while its code was byte-identical
        // to a working one.
        const std::string label_tag = ".Lf" + std::to_string(next_function_label++) + "_";
        if (function->linkage == ir::Linkage::Weak) {
            out << ".weak " << name << "\n";
        } else if (function->linkage != ir::Linkage::Internal) {
            out << ".globl " << name << "\n";
        }
        out << ".text\n";
        out << name << ":\n";
        // The entry label precedes the prologue so a disassembly has one stable
        // marker at the definition. This assembler builds its bridge from the
        // assembly text, so it has no definition-line record yet (ADR 0061).
        out << ".L" << name << ".begin:\n";

        // Slots: allocas first (their address is FP-relative), then one slot per
        // value, then one per parameter. An f64 takes two slots and starts on an
        // eight-byte boundary, so adjacent doubles cannot overlap.
        const auto slots_for = [](const ir::Type* type) -> std::uint32_t {
            return type != nullptr && type->kind == ir::TypeKind::Float &&
                           (type->bits + 7u) / 8u >= 8u
                       ? 2u
                       : 1u;
        };
        std::unordered_map<const ir::Value*, std::uint32_t> slot_of;
        // Slot 0 is left unused when the function holds an eight-byte value: a
        // double stored at FP-4 would cover FP+3 and overwrite the saved frame
        // pointer that ENTER writes at [FP]. Starting at one puts the first slot
        // at FP-8, so such a value spans FP-8..FP-1 and never reaches it. A
        // function with only four-byte values keeps frame 0 for an empty body.
        bool holds_eight_bytes = false;
        for (const auto& parameter : function->parameters) {
            holds_eight_bytes = holds_eight_bytes || type_bytes(parameter->type) > 4u;
        }
        for (const auto& block : function->blocks) {
            for (const auto& instruction : block->instructions) {
                if (instruction->opcode == ir::Opcode::Alloca &&
                    type_bytes(instruction->allocated_type) > 4u) {
                    holds_eight_bytes = true;
                }
            }
        }
        std::uint32_t next_slot = holds_eight_bytes ? 1u : 0u;
        for (const auto& block : function->blocks) {
            for (const auto& instruction : block->instructions) {
                if (instruction->opcode == ir::Opcode::Alloca) {
                    slot_of.emplace(instruction.get(), next_slot);
                    next_slot += std::max<std::uint32_t>(
                        1u,
                        align_frame(type_bytes(instruction->allocated_type)) / 4u);
                }
            }
        }
        // A function that returns a record receives its destination in R6.
        const bool returns_record = function->type != nullptr &&
                                    function->type->return_type != nullptr &&
                                    function->type->return_type->kind != ir::TypeKind::Void &&
                                    (function->type->return_type->kind == ir::TypeKind::Struct ||
                                     function->type->return_type->kind == ir::TypeKind::Union ||
                                     function->type->return_type->kind == ir::TypeKind::Array);
        std::uint32_t return_pointer_slot = 0;
        if (returns_record) {
            return_pointer_slot = next_slot++;
        }
        // Parameters live below ordinary values so a large function does not
        // need an address pair for every argument access.
        for (const auto& parameter : function->parameters) {
            const std::uint32_t slots = slots_for(parameter->type);
            if (slots == 2u && (next_slot % 2u) != 0u) {
                ++next_slot;
            }
            slot_of.emplace(parameter.get(), next_slot);
            next_slot += slots;
        }
        const auto plan = plan_one_word_value_slots(*function, next_slot);
        for (const auto& [instruction, slot] : plan.slots) {
            slot_of.emplace(instruction, slot);
        }
        next_slot = plan.next_slot;
        for (const auto& entity : slots_by_frequency(*function)) {
            if (entity.instruction->opcode == ir::Opcode::Alloca ||
                !takes_two_slots(entity.instruction->type)) {
                continue;
            }
            if ((next_slot % 2u) != 0u) {
                ++next_slot;
            }
            slot_of.emplace(entity.instruction, next_slot);
            next_slot += 2u;
        }
        // A call that returns a record needs somewhere to put it: the callee copies
        // into a destination the caller names. Those destinations get their own
        // frame space, recorded per call.
        std::unordered_map<const ir::Value*, std::uint32_t> aggregate_destination;
        for (const auto& block : function->blocks) {
            for (const auto& instruction : block->instructions) {
                if (instruction->opcode == ir::Opcode::Call && instruction->type != nullptr &&
                    (instruction->type->kind == ir::TypeKind::Struct ||
                     instruction->type->kind == ir::TypeKind::Union ||
                     instruction->type->kind == ir::TypeKind::Array)) {
                    aggregate_destination.emplace(instruction.get(), next_slot);
                    next_slot += align_frame(type_bytes(instruction->type)) / 4u;
                }
            }
        }
        // An object of more than one word occupies a run of slots, and a slot's
        // address falls as its index rises, so the object's lowest byte sits at the
        // LAST slot of its run. Using the first slot put a twelve-byte local array
        // above FP, where it overwrote the saved frame pointer and every return
        // through that frame jumped into nothing.
        const auto alloca_offset = [&](const ir::Instruction& alloca) -> std::uint32_t {
            const auto slot = slot_of.find(&alloca);
            if (slot == slot_of.end()) {
                return 4u;
            }
            const std::uint32_t words = std::max<std::uint32_t>(
                1u, align_frame(type_bytes(alloca.allocated_type)) / 4u);
            return 4u + 4u * (slot->second + words - 1u);
        };

        // A frame access that names its operand after any prologue it needs, so a
        // deep slot is reached through R8 instead of an unencodable displacement.
        const auto frame_load = [&](const char* opcode, std::uint32_t slot,
                                    const std::string& target) {
            const std::uint32_t offset = 4u + 4u * slot;
            if (offset > 128u) {
                out << "  mov r8, r13\n";
                out << "  sub r8, #" << offset << "\n";
                out << "  " << opcode << " " << target << ", [r8]\n";
                return;
            }
            out << "  " << opcode << " " << target << ", [r13-" << offset << "]\n";
        };
        // `fst` names its register first, so it needs its own helper: passing "fst"
        // through frame_store emitted `fst [r13-52], f0`, which the assembler rejects.
        const auto frame_store_float = [&](const std::string& source, std::uint32_t slot) {
            const std::uint32_t offset = 4u + 4u * slot;
            if (offset > 128u) {
                out << "  mov r8, r13\n";
                out << "  sub r8, #" << offset << "\n";
                out << "  fst " << source << ", [r8]\n";
                return;
            }
            out << "  fst " << source << ", [r13-" << offset << "]\n";
        };
        const auto frame_store = [&](const char* opcode, std::uint32_t slot,
                                     const std::string& source) {
            const std::uint32_t offset = 4u + 4u * slot;
            if (offset > 128u) {
                out << "  mov r8, r13\n";
                out << "  sub r8, #" << offset << "\n";
                out << "  " << opcode << " [r8], " << source << "\n";
                return;
            }
            out << "  " << opcode << " [r13-" << offset << "], " << source << "\n";
        };
        const std::uint32_t frame = align_frame(next_slot * 4u);

        out << "  enter #" << frame << ", #0\n";
        // Local variables for the debug bridge: a frame offset the debugger adds
        // to the frame base it unwound. The type index is 0, so only the size is
        // recorded until the bridge carries type records for this target.
        for (const auto& block : function->blocks) {
            for (const auto& instruction : block->instructions) {
                if (instruction->opcode != ir::Opcode::Alloca ||
                    instruction->name.empty()) {
                    continue;
                }
                const auto slot = slot_of.find(instruction.get());
                if (slot == slot_of.end()) {
                    continue;
                }
                std::string variable = instruction->name;
                const std::string suffix = ".addr";
                if (variable.size() > suffix.size() &&
                    variable.compare(variable.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    variable.resize(variable.size() - suffix.size());
                }
                out << ".debug_var " << name << " " << variable << " "
                    << static_cast<long>(0) - static_cast<long>(alloca_offset(*instruction))
                    << " 0 " << type_bytes(instruction->allocated_type) << "\n";
            }
        }
        if (returns_record) {
            frame_store("st", return_pointer_slot, "r6");
        }
        // Arguments arrive in R2-R5, except floating-point ones, which arrive in
        // F0-F3 and are spilled to their slots with `fst`. A variadic function is
        // the exception: every argument arrives in an eight-byte stack slot, the
        // first at [FP+8], because the caller pushed them instead of filling
        // registers (ADR 0057, variadic convention).
        const bool variadic_function = function->type != nullptr && function->type->variadic;
        const bool stack_arguments =
            variadic_function || function->parameters.size() > 4;
        std::size_t integer_argument = 0;
        std::size_t float_argument = 0;
        for (std::size_t i = 0; i < function->parameters.size(); ++i) {
            const auto slot = slot_of.at(function->parameters[i].get());
            const ir::Type* type = function->parameters[i]->type;
            const bool is_float = type != nullptr && type->kind == ir::TypeKind::Float;
            if (stack_arguments) {
                const std::uint32_t incoming = 8u + 8u * static_cast<std::uint32_t>(i);
                if (is_float) {
                    out << "  fld f0, [r13+" << incoming << "]\n";
                    frame_store_float("f0", slot);
                } else {
                    out << "  ld r5, [r13+" << incoming << "]\n";
                    frame_store("st", slot, "r5");
                }
                continue;
            }
            if (is_float) {
                frame_store_float("f" + std::to_string(float_argument++), slot);
            } else {
                frame_store("st", slot, "r" + std::to_string(2u + integer_argument++));
            }
        }

        std::uint32_t next_compare = 0;
        std::uint32_t next_indirect = 0;
        std::uint32_t next_copy = 0;
        std::unordered_map<const ir::BasicBlock*, std::size_t> block_index;
        for (std::size_t i = 0; i < function->blocks.size(); ++i) {
            block_index.emplace(function->blocks[i].get(), i);
        }
        const auto materialize = [&](const ir::Value* value, unsigned reg) {
            if (value == nullptr) {
                out << "  ld r" << reg << ", #0\n";
                return;
            }
            if (const auto* constant = dynamic_cast<const ir::ConstantInt*>(value)) {
                out << "  ld r" << reg << ", #" << constant->value << "\n";
                return;
            }
            if (const auto* global = dynamic_cast<const ir::GlobalVariable*>(value)) {
                out << "  ld r" << reg << ", #" << global->name << "\n";
                return;
            }
            // An alloca's value is its address, which is FP-relative. A cast or a
            // member GEP built on an alloca reaches here, and reading a slot would
            // hand the caller the alloca's contents instead of its address.
            if (const auto* instruction = dynamic_cast<const ir::Instruction*>(value)) {
                if (instruction->opcode == ir::Opcode::Alloca) {
                    const auto alloca_slot = slot_of.find(value);
                    if (alloca_slot != slot_of.end()) {
                        out << "  mov r" << reg << ", r13\n";
                        out << "  sub r" << reg << ", #" << alloca_offset(*instruction)
                            << "\n";
                        return;
                    }
                }
            }
            const auto slot = slot_of.find(value);
            if (slot == slot_of.end()) {
                out << "  ld r" << reg << ", #0\n";
                return;
            }
            frame_load("ld", slot->second, "r" + std::to_string(reg));
        };
        // A load or store address: FP-relative for an alloca, R8 for a computed
        // pointer. Naming R8 keeps the address out of the value register.
        const auto address_of = [&](const ir::Value* value) -> std::string {
            if (const auto* instruction = dynamic_cast<const ir::Instruction*>(value)) {
                if (instruction->opcode == ir::Opcode::Alloca) {
                    const auto slot = slot_of.find(value);
                    if (slot != slot_of.end()) {
                        const std::uint32_t offset = alloca_offset(*instruction);
                        if (offset > 128u) {
                            // A d8 displacement cannot reach a slot this deep: form the
                            // address in R8, the register the backend keeps as its
                            // address scratch.
                            out << "  mov r8, r13\n";
                            out << "  sub r8, #" << offset << "\n";
                            return "[r8]";
                        }
                        return "[r13-" + std::to_string(offset) + "]";
                    }
                }
            }
            materialize(value, kAddressReg);
            return "[r8]";
        };
        // A floating-point value is two words in a slot; a constant is loaded from
        // its own .data entry, emitted in `fp_constants`.
        const auto materialize_float = [&](const ir::Value* value, unsigned fregister) {
            if (const auto* constant = dynamic_cast<const ir::ConstantFloat*>(value)) {
                std::ostringstream label;
                label << fp_label(constant->value);
                out << "  ld r" << kAddressReg << ", #" << label.str() << "\n";
                out << "  fld f" << fregister << ", [r" << kAddressReg << "]\n";
                return;
            }
            const auto slot = slot_of.find(value);
            if (slot == slot_of.end()) {
                out << "  ld r" << kAddressReg << ", #0\n";
                out << "  fld f" << fregister << ", [r" << kAddressReg << "]\n";
                return;
            }
            out << "  mov r" << kAddressReg << ", r13\n";
            out << "  sub r" << kAddressReg << ", #" << (4u + 4u * slot->second) << "\n";
            out << "  fld f" << fregister << ", [r" << kAddressReg << "]\n";
        };        const auto store_result = [&](const ir::Instruction* instruction, unsigned reg) {
            if (instruction->type != nullptr && instruction->type->kind != ir::TypeKind::Void) {
                const auto slot = slot_of.find(instruction);
                if (slot != slot_of.end()) {
                    frame_store("st", slot->second, "r" + std::to_string(reg));
                }
            }
        };

        for (const auto& block : function->blocks) {
            out << label_tag << "bb" << block_index[block.get()] << ":\n";
            for (const auto& instruction : block->instructions) {
                const auto operand = [&](std::size_t index, unsigned reg) {
                    materialize(index < instruction->operands.size()
                                    ? instruction->operands[index]
                                    : nullptr,
                                reg);
                };
                switch (instruction->opcode) {
                    case ir::Opcode::Alloca:
                        break;
                    case ir::Opcode::Trunc:
                    case ir::Opcode::ZExt:
                    case ir::Opcode::SExt:
                    case ir::Opcode::BitCast:
                    case ir::Opcode::PtrToInt:
                    case ir::Opcode::IntToPtr: {
                        if (!instruction->operands.empty()) {
                            operand(0, kValueReg);
                            store_result(instruction.get(), kValueReg);
                        }
                        break;
                    }
                    case ir::Opcode::GetElementPtr: {
                        if (instruction->operands.empty()) {
                            error = "kr232 backend: getelementptr without a base";
                            return std::nullopt;
                        }
                        const auto* base_type = instruction->operands[0]->type != nullptr
                                                    ? instruction->operands[0]->type->element
                                                    : nullptr;
                        const std::uint32_t element = type_bytes(base_type);
                        const auto* base_instruction =
                            dynamic_cast<const ir::Instruction*>(instruction->operands[0]);
                        if (base_instruction != nullptr &&
                            base_instruction->opcode == ir::Opcode::Alloca) {
                            // The element offset below is added to the object's lowest
                            // byte, which is its last slot, not its first.
                            out << "  mov r" << kValueReg << ", r13\n";
                            out << "  sub r" << kValueReg << ", #"
                                << alloca_offset(*base_instruction) << "\n";
                        } else {
                            operand(0, kValueReg);
                        }
                        // A record member index selects a field: its byte offset is
                        // the sum of the preceding fields' sizes, not the index
                        // times the whole record.
                        if (base_type != nullptr && base_type->kind == ir::TypeKind::Struct &&
                            instruction->operands.size() == 3) {
                            const auto* member =
                                dynamic_cast<const ir::ConstantInt*>(instruction->operands[2]);
                            if (member == nullptr) {
                                error = "kr232 backend: a variable member index is not implemented";
                                return std::nullopt;
                            }
                            const std::uint32_t offset_bytes =
                                field_offset(base_type, static_cast<std::size_t>(member->value));
                            if (offset_bytes != 0) {
                                out << "  add r" << kValueReg << ", #" << offset_bytes << "\n";
                            }
                            store_result(instruction.get(), kValueReg);
                            break;
                        }
                        // `gep [N x T], base, 0, i` is the shape the front end emits
                        // for an array element: the leading zero steps over the array
                        // itself and the second index selects elements. Scaling every
                        // index by the pointee size made the stride the whole array,
                        // so element 1 landed at base + N * sizeof(T) and every
                        // initialiser past the first wrote outside the object.
                        std::uint32_t stride = element;
                        std::size_t first_index = 1;
                        if (base_type != nullptr &&
                            base_type->kind == ir::TypeKind::Array &&
                            instruction->operands.size() == 3) {
                            stride = type_bytes(base_type->element);
                            first_index = 2;
                        }
                        for (std::size_t i = first_index; i < instruction->operands.size(); ++i) {
                            const auto* index =
                                dynamic_cast<const ir::ConstantInt*>(instruction->operands[i]);
                            if (index != nullptr) {
                                const std::int64_t constant =
                                    index->value * static_cast<std::int64_t>(stride);

                                if (constant != 0) {
                                    out << "  add r" << kValueReg << ", #" << constant << "\n";
                                }
                                continue;
                            }
                            operand(i, kSecondReg);
                            if (stride > 1) {
                                // Any power-of-two stride is a shift, not only
                                // 2, 4, and 8: a three-dimensional array steps by
                                // the whole inner array, which is 64 here.
                                std::uint32_t shift = 0;
                                std::uint32_t remaining = stride;
                                while ((remaining & 1u) == 0 && remaining > 1) {
                                    remaining >>= 1;
                                    ++shift;
                                }
                                if (remaining != 1) {
                                    error = "kr232 backend: element size " +
                                            std::to_string(stride) +
                                            " is not a power of two in " + name;
                                    return std::nullopt;
                                }
                                out << "  shl r" << kSecondReg << ", #" << shift << "\n";
                            }
                            out << "  add r" << kValueReg << ", r" << kSecondReg << "\n";
                        }
                        store_result(instruction.get(), kValueReg);
                        break;
                    }
                    case ir::Opcode::Load: {
                        if (instruction->operands.empty()) {
                            error = "kr232 backend: load without an address";
                            return std::nullopt;
                        }
                        // A floating-point value is two words: frame traffic uses
                        // `fld`/`fst` through a computed address, because the FP
                        // forms take a base register rather than a frame offset.
                        if (instruction->type != nullptr &&
                            instruction->type->kind == ir::TypeKind::Float) {
                            materialize(instruction->operands[0], kAddressReg);
                            out << "  fld f0, [r8]\n";
                            const auto slot = slot_of.find(instruction.get());
                            if (slot != slot_of.end()) {
                                out << "  mov r8, r13\n";
                                out << "  sub r8, #" << (4u + 4u * slot->second) << "\n";
                                out << "  fst f0, [r8]\n";
                            }
                            break;
                        }
                        // A record value is its address: loading one names where it
                        // lives rather than copying its bytes.
                        if (instruction->type != nullptr &&
                            (instruction->type->kind == ir::TypeKind::Struct ||
                             instruction->type->kind == ir::TypeKind::Union ||
                             instruction->type->kind == ir::TypeKind::Array)) {
                            materialize(instruction->operands[0], kValueReg);
                            store_result(instruction.get(), kValueReg);
                            break;
                        }
                        const auto bytes = type_bytes(instruction->type);
                        const bool is_signed =
                            instruction->type != nullptr && instruction->type->is_signed;
                        const std::string address = address_of(instruction->operands[0]);
                        if (bytes == 1) {
                            out << (is_signed ? "  ldbs r" : "  ldbu r") << kValueReg << ", "
                                << address << "\n";
                        } else if (bytes == 2) {
                            out << (is_signed ? "  ldhs r" : "  ldhu r") << kValueReg << ", "
                                << address << "\n";
                        } else {
                            out << "  ld r" << kValueReg << ", " << address << "\n";
                        }
                        store_result(instruction.get(), kValueReg);
                        break;
                    }
                    case ir::Opcode::Store: {
                        if (instruction->operands.size() < 2) {
                            error = "kr232 backend: store without an address";
                            return std::nullopt;
                        }
                        // A floating-point store moves the value through f0 and
                        // writes eight bytes at the computed address.
                        if (instruction->operands[0]->type != nullptr &&
                            instruction->operands[0]->type->kind == ir::TypeKind::Float) {
                            materialize_float(instruction->operands[0], 0);
                            materialize(instruction->operands[1], kAddressReg);
                            out << "  fst f0, [r8]\n";
                            break;
                        }
                        // A record store copies its bytes: the value is an address
                        // and so is the destination.
                        if (instruction->operands[0]->type != nullptr &&
                            (instruction->operands[0]->type->kind == ir::TypeKind::Struct ||
                             instruction->operands[0]->type->kind == ir::TypeKind::Union ||
                             instruction->operands[0]->type->kind == ir::TypeKind::Array)) {
                            const std::uint32_t bytes =
                                type_bytes(instruction->operands[0]->type);
                            materialize(instruction->operands[1], kSecondReg);  // destination
                            materialize(instruction->operands[0], kValueReg);   // source
                            // Copy in a loop rather than unrolling: a 9 KiB
                            // aggregate otherwise emits tens of thousands of
                            // instructions and overruns the branch range.
                            if (bytes > 0) {
                                const std::string copy_loop =
                                    label_tag + "copy" + std::to_string(next_copy++);
                                out << "  ld r5, #" << bytes << "\n";
                                out << copy_loop << ":\n";
                                out << "  ldbu r" << kAddressReg << ", [r"
                                    << kValueReg << "]\n";
                                out << "  stb [r" << kSecondReg << "], r"
                                    << kAddressReg << "\n";
                                out << "  add r" << kValueReg << ", #1\n";
                                out << "  add r" << kSecondReg << ", #1\n";
                                out << "  sub r5, #1\n";
                                out << "  bne " << copy_loop << "\n";
                            }
                            break;
                        }
                        const auto bytes = type_bytes(instruction->operands[0]->type);
                        // The value first, then the address into R8: a global's
                        // address is an `ld`, and loading it first would overwrite
                        // the value.
                        materialize(instruction->operands[0], kValueReg);
                        const std::string address = address_of(instruction->operands[1]);
                        if (bytes == 1) {
                            out << "  stb " << address << ", r" << kValueReg << "\n";
                        } else if (bytes == 2) {
                            out << "  sth " << address << ", r" << kValueReg << "\n";
                        } else {
                            out << "  st " << address << ", r" << kValueReg << "\n";
                        }
                        break;
                    }
                    case ir::Opcode::FPTrunc:
                    case ir::Opcode::FPExt: {
                        // The profile has one floating-point format, so `float` and
                        // `double` are both 64 bits and a conversion between them
                        // moves the same value: a copy, not an instruction. The ISA
                        // has no FCVT to emit.
                        if (instruction->operands.empty()) {
                            error = "kr232 backend: conversion without an operand";
                            return std::nullopt;
                        }
                        materialize_float(instruction->operands[0], 0);
                        const std::uint32_t offset = 4u + 4u * slot_of.at(instruction.get());
                        if (offset > 128u) {
                            out << "  mov r8, r13\n";
                            out << "  sub r8, #" << offset << "\n";
                            out << "  fst f0, [r8]\n";
                        } else {
                            out << "  fst f0, [r13-" << offset << "]\n";
                        }
                        break;
                    }                    case ir::Opcode::FAdd:
                    case ir::Opcode::FSub:
                    case ir::Opcode::FMul:
                    case ir::Opcode::FDiv: {
                        if (instruction->operands.size() < 2) {
                            error = "kr232 backend: a floating-point operation needs two operands";
                            return std::nullopt;
                        }
                        const char* op_name = "fadd";
                        switch (instruction->opcode) {
                            case ir::Opcode::FPTrunc:
                    case ir::Opcode::FPExt: {
                        // The profile has one floating-point format, so `float` and
                        // `double` are both 64 bits and a conversion between them
                        // moves the same value: a copy, not an instruction. The ISA
                        // has no FCVT to emit.
                        if (instruction->operands.empty()) {
                            error = "kr232 backend: conversion without an operand";
                            return std::nullopt;
                        }
                        materialize_float(instruction->operands[0], 0);
                        const std::uint32_t offset = 4u + 4u * slot_of.at(instruction.get());
                        if (offset > 128u) {
                            out << "  mov r8, r13\n";
                            out << "  sub r8, #" << offset << "\n";
                            out << "  fst f0, [r8]\n";
                        } else {
                            out << "  fst f0, [r13-" << offset << "]\n";
                        }
                        break;
                    }                    case ir::Opcode::FAdd: op_name = "fadd"; break;
                            case ir::Opcode::FSub: op_name = "fsub"; break;
                            case ir::Opcode::FMul: op_name = "fmul"; break;
                            default: op_name = "fdiv"; break;
                        }
                        // Floating-point values live in frame slots like everything
                        // else: two words each, moved through f6 and f7.
                        materialize_float(instruction->operands[0], 0);
                        materialize_float(instruction->operands[1], 1);
                        out << "  " << op_name << " f0, f1\n";
                        // The FP store names its register first: `fst fD, [addr]`.
                        {
                            const std::uint32_t offset = 4u + 4u * slot_of.at(instruction.get());
                            if (offset > 128u) {
                                out << "  mov r8, r13\n";
                                out << "  sub r8, #" << offset << "\n";
                                out << "  fst f0, [r8]\n";
                            } else {
                                out << "  fst f0, [r13-" << offset << "]\n";
                            }
                        }
                        break;
                    }
                    case ir::Opcode::FPToSI:
                    case ir::Opcode::FPToUI: {
                        if (instruction->operands.empty()) {
                            error = "kr232 backend: an FP conversion needs an operand";
                            return std::nullopt;
                        }
                        materialize_float(instruction->operands[0], 0);
                        out << "  " << (instruction->opcode == ir::Opcode::FPToSI ? "f2i" : "f2u")
                            << " r6, f0\n";
                        store_result(instruction.get(), kValueReg);
                        break;
                    }
                    case ir::Opcode::SIToFP:
                    case ir::Opcode::UIToFP: {
                        if (instruction->operands.empty()) {
                            error = "kr232 backend: an FP conversion needs an operand";
                            return std::nullopt;
                        }
                        materialize(instruction->operands[0], kValueReg);
                        out << "  " << (instruction->opcode == ir::Opcode::SIToFP ? "i2f" : "u2f")
                            << " f0, r6\n";
                        // The FP store forms take a base register, not a frame offset.
                        out << "  mov r8, r13\n";
                        out << "  sub r8, #" << (4u + 4u * slot_of.at(instruction.get()))
                            << "\n";
                        out << "  fst f0, [r8]\n";
                        break;
                    }
                    case ir::Opcode::Add:
                    case ir::Opcode::Sub:
                    case ir::Opcode::Mul:
                    case ir::Opcode::And:
                    case ir::Opcode::Or:
                    case ir::Opcode::Xor:
                    case ir::Opcode::Shl:
                    case ir::Opcode::LShr:
                    case ir::Opcode::AShr:
                    case ir::Opcode::UDiv:
                    case ir::Opcode::SDiv: {
                        if (instruction->operands.size() < 2) {
                            error = "kr232 backend: binary operation without two operands";
                            return std::nullopt;
                        }
                        operand(0, kValueReg);
                        operand(1, kSecondReg);
                        const char* op_name = "add";
                        switch (instruction->opcode) {
                            case ir::Opcode::Add: op_name = "add"; break;
                            case ir::Opcode::Sub: op_name = "sub"; break;
                            case ir::Opcode::Mul: op_name = "mul"; break;
                            case ir::Opcode::And: op_name = "and"; break;
                            case ir::Opcode::Or: op_name = "or"; break;
                            case ir::Opcode::Xor: op_name = "xor"; break;
                            case ir::Opcode::Shl: op_name = "shl"; break;
                            case ir::Opcode::LShr: op_name = "shr"; break;
                            case ir::Opcode::AShr: op_name = "sar"; break;
                            case ir::Opcode::UDiv: op_name = "div"; break;
                            default: op_name = "divs"; break;
                        }
                        out << "  " << op_name << " r" << kValueReg << ", r" << kSecondReg << "\n";
                        store_result(instruction.get(), kValueReg);
                        break;
                    }
                    case ir::Opcode::URem:
                    case ir::Opcode::SRem: {
                        // DIV writes the quotient only (ISA §12), so the remainder
                        // is synthesised as a - (a / b) * b. R8 is the address
                        // register and carries no live value inside this block.
                        if (instruction->operands.size() < 2) {
                            error = "kr232 backend: binary operation without two operands";
                            return std::nullopt;
                        }
                        operand(0, kValueReg);
                        operand(1, kSecondReg);
                        out << "  mov r" << kAddressReg << ", r" << kValueReg << "\n";
                        out << "  " << (instruction->opcode == ir::Opcode::URem ? "div" : "divs")
                            << " r" << kAddressReg << ", r" << kSecondReg << "\n";
                        out << "  mul r" << kAddressReg << ", r" << kSecondReg << "\n";
                        out << "  sub r" << kValueReg << ", r" << kAddressReg << "\n";
                        store_result(instruction.get(), kValueReg);
                        break;
                    }
                    case ir::Opcode::FCmp: {
                        // FCMP writes N = (Fd < Fs), Z = (Fd == Fs) and
                        // V = unordered (ISA §10), so the ordered predicates are the
                        // integer branch conditions and the unordered ones test Z.
                        if (instruction->operands.size() < 2) {
                            error = "kr232 backend: comparison without two operands";
                            return std::nullopt;
                        }
                        materialize_float(instruction->operands[0], 0);
                        materialize_float(instruction->operands[1], 1);
                        const std::string tail =
                            label_tag + "fcmp" + std::to_string(next_compare++);
                        out << "  fcmp f0, f1\n";
                        const auto predicate = instruction->fcmp_predicate;
                        if (predicate == ir::FCmpPredicate::False ||
                            predicate == ir::FCmpPredicate::True) {
                            out << "  ld r" << kValueReg << ", #"
                                << (predicate == ir::FCmpPredicate::True ? 1 : 0) << "\n";
                            store_result(instruction.get(), kValueReg);
                            break;
                        }
                        const char* branch = "beq";
                        switch (predicate) {
                            case ir::FCmpPredicate::Oeq: branch = "beq"; break;
                            case ir::FCmpPredicate::One: branch = "bne"; break;
                            case ir::FCmpPredicate::Olt: branch = "blt"; break;
                            case ir::FCmpPredicate::Ole: branch = "ble"; break;
                            case ir::FCmpPredicate::Ogt: branch = "bgt"; break;
                            case ir::FCmpPredicate::Oge: branch = "bge"; break;
                            case ir::FCmpPredicate::Ord: branch = "bvc"; break;
                            case ir::FCmpPredicate::Uno: branch = "bvs"; break;
                            default: break;
                        }
                        out << "  ld r" << kValueReg << ", #0\n";
                        out << "  " << branch << " " << tail << "\n";
                        out << "  bra " << tail << "end\n";
                        out << tail << ":\n";
                        out << "  ld r" << kValueReg << ", #1\n";
                        out << tail << "end:\n";
                        store_result(instruction.get(), kValueReg);
                        break;
                    }                    case ir::Opcode::ICmp: {
                        if (instruction->operands.size() < 2) {
                            error = "kr232 backend: comparison without two operands";
                            return std::nullopt;
                        }
                        operand(0, kValueReg);
                        operand(1, kSecondReg);
                        const std::string tail = label_tag + "cmp" + std::to_string(next_compare++);
                        const char* branch = "beq";
                        switch (instruction->icmp_predicate) {
                            case ir::ICmpPredicate::Eq: branch = "beq"; break;
                            case ir::ICmpPredicate::Ne: branch = "bne"; break;
                            case ir::ICmpPredicate::Ugt: branch = "bhi"; break;
                            case ir::ICmpPredicate::Uge: branch = "bcs"; break;
                            case ir::ICmpPredicate::Ult: branch = "bcc"; break;
                            case ir::ICmpPredicate::Ule: branch = "bls"; break;
                            case ir::ICmpPredicate::Sgt: branch = "bgt"; break;
                            case ir::ICmpPredicate::Sge: branch = "bge"; break;
                            case ir::ICmpPredicate::Slt: branch = "blt"; break;
                            case ir::ICmpPredicate::Sle: branch = "ble"; break;
                        }
                        out << "  cmp r" << kValueReg << ", r" << kSecondReg << "\n";
                        out << "  ld r" << kValueReg << ", #0\n";
                        out << "  " << branch << " " << tail << "\n";
                        out << "  bra " << tail << "end\n";
                        out << tail << ":\n";
                        out << "  ld r" << kValueReg << ", #1\n";
                        out << tail << "end:\n";
                        store_result(instruction.get(), kValueReg);
                        break;
                    }
                    case ir::Opcode::Br: {
                        if (instruction->successors.empty()) {
                            error = "kr232 backend: branch without a target";
                            return std::nullopt;
                        }
                        out << "  bra " << label_tag << "bb" << block_index[instruction->successors[0]] << "\n";
                        break;
                    }
                    case ir::Opcode::CondBr: {
                        if (instruction->operands.empty() || instruction->successors.size() < 2) {
                            error = "kr232 backend: conditional branch without a condition";
                            return std::nullopt;
                        }
                        operand(0, kValueReg);
                        out << "  ld r" << kSecondReg << ", #0\n";
                        out << "  cmp r" << kValueReg << ", r" << kSecondReg << "\n";
                        out << "  bne " << label_tag << "bb" << block_index[instruction->successors[0]] << "\n";
                        out << "  bra " << label_tag << "bb" << block_index[instruction->successors[1]] << "\n";
                        break;
                    }
                    case ir::Opcode::VaStart: {
                        // The variable arguments begin after the named ones: the
                        // first stack slot past the return address, eight bytes
                        // each (ADR 0057, variadic convention).
                        const auto fixed =
                            static_cast<std::uint32_t>(function->parameters.size());
                        out << "  mov r" << kAddressReg << ", r13\n";
                        out << "  add r" << kAddressReg << ", #" << (8u + 8u * fixed) << "\n";
                        store_result(instruction.get(), kAddressReg);
                        break;
                    }
                    case ir::Opcode::Call: {
                        // Arguments are typed: integers fill r2-r5, doubles fill
                        // f0-f3, each in its own order, which is how the callee's
                        // prologue stores them.
                        unsigned integer_arguments = 0;
                        unsigned float_arguments = 0;
                        if (instruction->operands.empty()) {
                            error = "kr232 backend: call without a callee";
                            return std::nullopt;
                        }
                        const auto* callee =
                            dynamic_cast<const ir::Function*>(instruction->operands[0]);
                        if (callee == nullptr) {
                            // An indirect call: a virtual dispatch reaches a function
                            // pointer loaded from the vtable. The ISA has no call
                            // through a register, so the return address is pushed and
                            // the callee is reached with `jmp rD` -- `ret` pops what
                            // was pushed, exactly as `call` would have left it.
                            if (instruction->operands.size() > 5) {
                                error =
                                    "kr232 backend: more than four arguments needs the stack form";
                                return std::nullopt;
                            }
                            for (std::size_t i = 1; i < instruction->operands.size(); ++i) {
                                const ir::Value* argument = instruction->operands[i];
                                if (argument->type != nullptr &&
                                    argument->type->kind == ir::TypeKind::Float) {
                                    materialize_float(argument, float_arguments++);
                                } else {
                                    materialize(argument, 2u + integer_arguments++);
                                }
                            }
                            const std::string back = label_tag + "call" + std::to_string(next_indirect++);
                            materialize(instruction->operands[0], kAddressReg);
                            out << "  ld r" << kSecondReg << ", #" << back << "\n";
                            out << "  push r" << kSecondReg << "\n";
                            out << "  jmp r" << kAddressReg << "\n";
                            out << back << ":\n";
                            if (instruction->type != nullptr &&
                                instruction->type->kind != ir::TypeKind::Void) {
                                out << "  mov r" << kValueReg << ", r2\n";
                                store_result(instruction.get(), kValueReg);
                            }
                            break;
                        }
                        // A variadic callee has no register limit: every argument
                        // is pushed in an eight-byte slot below. A fixed function
                        // with more than four parameters uses the same stack form,
                        // with the callee reading every parameter from the stack.
                        const bool callee_variadic =
                            callee->type != nullptr && callee->type->variadic;
                        const bool stack_call =
                            callee_variadic ||
                            (callee->type != nullptr &&
                             callee->type->parameters.size() > 4);
                        if (!stack_call) {
                            for (std::size_t i = 1; i < instruction->operands.size(); ++i) {
                                const ir::Value* argument = instruction->operands[i];
                                if (argument->type != nullptr &&
                                    argument->type->kind == ir::TypeKind::Float) {
                                    materialize_float(argument, float_arguments++);
                                } else {
                                    materialize(argument, 2u + integer_arguments++);
                                }
                            }
                        }
                        // Every stack argument is an eight-byte slot, pushed last
                        // first so the first sits at [FP+8] in the callee.
                        if (stack_call) {
                            for (std::size_t i = instruction->operands.size(); i-- > 1;) {
                                const ir::Value* argument = instruction->operands[i];
                                const bool argument_float =
                                    argument->type != nullptr &&
                                    argument->type->kind == ir::TypeKind::Float;
                                if (argument_float) {
                                    materialize_float(argument, 0);
                                } else {
                                    materialize(argument, kValueReg);
                                }
                                out << "  sub r15, #8\n";
                                if (argument_float) {
                                    out << "  fst f0, [r15]\n";
                                } else {
                                    out << "  st [r15], r" << kValueReg << "\n";
                                    out << "  ld r" << kSecondReg << ", #0\n";
                                    out << "  st [r15+4], r" << kSecondReg << "\n";
                                }
                            }
                        }
                        // A record return needs a destination the callee copies
                        // into; the caller names it in R6.
                        const auto destination = aggregate_destination.find(instruction.get());
                        if (destination != aggregate_destination.end()) {
                            out << "  mov r6, r13\n";
                            out << "  sub r6, #" << (4u + 4u * destination->second) << "\n";
                        }
                        out << "  call " << callee->name << "\n";
                        if (stack_call) {
                            out << "  add r15, #"
                                << (8u * static_cast<unsigned>(
                                              instruction->operands.size() - 1))
                                << "\n";
                        }
                        if (instruction->type != nullptr &&
                            instruction->type->kind == ir::TypeKind::Float) {
                            // A floating-point result comes back in F0.
                            out << "  mov r8, r13\n";
                            out << "  sub r8, #" << (4u + 4u * slot_of.at(instruction.get()))
                                << "\n";
                            out << "  fst f0, [r8]\n";
                        } else if (instruction->type != nullptr &&
                                   instruction->type->kind != ir::TypeKind::Void) {
                            out << "  mov r" << kValueReg << ", r2\n";
                            store_result(instruction.get(), kValueReg);
                        }
                        break;
                    }
                    case ir::Opcode::Ret: {
                        if (instruction->operands.empty()) {
                            out << "  ld r2, #0\n";
                        } else if (function->type != nullptr &&
                                   function->type->return_type != nullptr &&
                                   function->type->return_type->kind == ir::TypeKind::Float) {
                            // A floating-point result is returned in F0.
                            materialize_float(instruction->operands[0], 0);
                        } else if (returns_record) {
                            // Copy the record out to the destination the caller named
                            // in R6, and hand that pointer back in R2.
                            const std::uint32_t bytes =
                                type_bytes(function->type->return_type);
                            materialize(instruction->operands[0], kValueReg);
                            frame_load("ld", return_pointer_slot,
                                       "r" + std::to_string(kSecondReg));
                            // Copy in a loop rather than unrolling: a 9 KiB
                            // aggregate otherwise emits tens of thousands of
                            // instructions and overruns the branch range.
                            if (bytes > 0) {
                                const std::string copy_loop =
                                    label_tag + "copy" + std::to_string(next_copy++);
                                // R7 stays the destination base because it is
                                // returned in R2; R4 is the write cursor.
                                out << "  mov r4, r" << kSecondReg << "\n";
                                out << "  ld r5, #" << bytes << "\n";
                                out << copy_loop << ":\n";
                                out << "  ldbu r" << kAddressReg << ", [r"
                                    << kValueReg << "]\n";
                                out << "  stb [r4], r" << kAddressReg << "\n";
                                out << "  add r" << kValueReg << ", #1\n";
                                out << "  add r4, #1\n";
                                out << "  sub r5, #1\n";
                                out << "  bne " << copy_loop << "\n";
                            }
                            out << "  mov r2, r" << kSecondReg << "\n";
                        } else {
                            materialize(instruction->operands[0], 2);
                        }
                        out << "  leave #0\n";
                        out << "  ret\n";
                        break;
                    }
                    default:
                        error = "kr232 backend: opcode " +
                                std::to_string(static_cast<int>(instruction->opcode)) +
                                " is not implemented yet";
                        return std::nullopt;
                }
            }
        }
    }
    std::string text = out.str();
    if (optimizes(level_)) {
        const int passes = is_aggressive(level_) ? 2 : 1;
        for (int pass = 0; pass < passes; ++pass) {
            const auto before = text;
            text = forward_frame_loads(text);
            text = fold_frame_addresses(text);
            text = reuse_frame_addresses(text);
            text = reuse_frame_addresses_across_blocks(text);
            text = fold_frame_addresses_to_lea(text);
            text = coalesce_argument_moves(text);
            text = coalesce_address_moves(text);
            text = forward_frame_loads_across_blocks(text);
            text = eliminate_local_dead_frame_stores(text);
            text = eliminate_dead_frame_stores(text);
            text = eliminate_dead_register_definitions(text);
            if (pass > 0 && text.size() >= before.size()) {
                text = before;
            }
        }
    }
    return text;
}

}  // namespace krc::backend
