#include "backend/kr16_backend.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace krc::backend {

namespace {

constexpr std::uint32_t kSlotSize = 2;

std::string label_for(const ir::BasicBlock& block) {
    return ".L" + block.parent->name + "." + block.name;
}

std::string inverse_condition_for(ir::ICmpPredicate predicate) {
    switch (predicate) {
    case ir::ICmpPredicate::Eq:
        return "BNE";
    case ir::ICmpPredicate::Ne:
        return "BEQ";
    case ir::ICmpPredicate::Ult:
        return "BCS";
    case ir::ICmpPredicate::Ule:
        return "BHI";
    case ir::ICmpPredicate::Ugt:
        return "BLS";
    case ir::ICmpPredicate::Uge:
        return "BCC";
    case ir::ICmpPredicate::Slt:
        return "BGE";
    case ir::ICmpPredicate::Sle:
        return "BGT";
    case ir::ICmpPredicate::Sgt:
        return "BLE";
    case ir::ICmpPredicate::Sge:
        return "BLT";
    }
    return "BNE";
}

// A sixteen-bit machine spends a whole instruction moving a literal into a
// register, so an ALU or shift instruction takes one directly when it fits the
// assembler's literal range, which ADR 0034 fixes at -32768..65535. Outside that
// range the value keeps the register form the emitter already used.
bool fits_inline_literal(std::int64_t value) {
    return value >= -32768 && value <= 65535;
}

// Returns the literal an instruction can carry inline, or null when the operand
// has to reach it through a register.
const ir::ConstantInt* inline_literal(const ir::Value* value) {
    if (!value || value->kind != ir::ValueKind::ConstantInt) {
        return nullptr;
    }
    const auto* constant = static_cast<const ir::ConstantInt*>(value);
    return fits_inline_literal(constant->value) ? constant : nullptr;
}

// The alignment a field is laid out with. A field narrower than a word is aligned
// by its own width, so a three-`char` record is three bytes and `sizeof` agrees
// with the object the back end actually lays out. Rounding every field up to the
// two-byte word made that record six bytes while the front end reported three, and
// `new T[n]` sized the allocation from `sizeof`.
std::uint32_t type_alignment(const ir::TypePtr& type) {
    if (!type) {
        return 1;
    }
    switch (type->kind) {
    case ir::TypeKind::Integer:
        return static_cast<std::uint32_t>(
            std::max<std::uint64_t>(1, (type->bits + 7) / 8));
    case ir::TypeKind::Pointer:
        return 2;
    case ir::TypeKind::Array:
        return type_alignment(type->element);
    case ir::TypeKind::Struct:
    case ir::TypeKind::Union: {
        std::uint32_t alignment = 1;
        for (const auto& field : type->fields) {
            alignment = std::max(alignment, type_alignment(field));
        }
        return alignment;
    }
    default:
        return 2;
    }
}

std::uint32_t round_up(std::uint32_t value, std::uint32_t alignment) {
    return alignment == 0 ? value : (value + alignment - 1) / alignment * alignment;
}

std::uint32_t type_size(const ir::TypePtr& type) {
    if (!type) {
        return 0;
    }
    switch (type->kind) {
    case ir::TypeKind::Void:
        return 0;
    case ir::TypeKind::Integer:
        return static_cast<std::uint32_t>(
            std::max<std::uint64_t>(1, type->bits / 8));
    case ir::TypeKind::Pointer:
        return 2;
    case ir::TypeKind::Array:
        return type_size(type->element) *
               static_cast<std::uint32_t>(type->count);
    case ir::TypeKind::Struct: {
        std::uint32_t size = 0;
        std::uint32_t alignment = 1;
        for (const auto& field : type->fields) {
            const auto field_alignment = type_alignment(field);
            alignment = std::max(alignment, field_alignment);
            size = round_up(size, field_alignment);
            size += type_size(field);
        }
        return round_up(size, alignment);
    }
    case ir::TypeKind::Union: {
        std::uint32_t size = 0;
        for (const auto& field : type->fields) {
            size = std::max(size, type_size(field));
        }
        return size;
    }
    default:
        return 2;
    }
}

// The spelling `.debug_info` uses for a type: no IR sigils and no spaces, so a
// record field can hold it. The same transformation Kr32Backend applies.
std::string debug_type_name(const ir::TypePtr& type) {
    auto name = ir::type_to_string(type);
    name.erase(std::remove(name.begin(), name.end(), '%'), name.end());
    std::replace(name.begin(), name.end(), ' ', '_');
    return name;
}

// An aggregate return passes its destination as a hidden stack argument (ADR 0048).
// A variadic callee reads its arguments by position, so its destination would be
// ambiguous; those keep the rejection.
bool returns_aggregate(const ir::Function& function) {
    if (!function.type || function.type->variadic) {
        return false;
    }
    const auto& type = function.type->return_type;
    return type && (type->kind == ir::TypeKind::Struct ||
                    type->kind == ir::TypeKind::Union);
}

bool gep_offset(const ir::Instruction& instruction, std::uint32_t& offset,
                std::string& error) {
    offset = 0;
    ir::TypePtr current = instruction.source_type;
    for (std::size_t index = 1; index < instruction.operands.size(); ++index) {
        const auto* constant =
            dynamic_cast<const ir::ConstantInt*>(instruction.operands[index]);
        if (!constant) {
            error = "KR16 dynamic GEP indices are not implemented yet";
            return false;
        }
        const auto element = constant->value;
        if (index == 1) {
            offset += static_cast<std::uint32_t>(
                element *
                static_cast<std::int64_t>(type_size(current)));
            continue;
        }
        if (current && current->kind == ir::TypeKind::Array) {
            offset += static_cast<std::uint32_t>(
                element *
                static_cast<std::int64_t>(type_size(current->element)));
            current = current->element;
            continue;
        }
        if (current && current->kind == ir::TypeKind::Struct) {
            if (element < 0 ||
                static_cast<std::size_t>(element) >= current->fields.size()) {
                error = "KR16 GEP struct index is out of range";
                return false;
            }
            for (std::int64_t field = 0; field < element; ++field) {
                offset += type_size(
                    current->fields[static_cast<std::size_t>(field)]);
            }
            current = current->fields[static_cast<std::size_t>(element)];
            continue;
        }
        error = "KR16 GEP index does not select an aggregate element";
        return false;
    }
    return true;
}

// The scale for a GEP whose last index is a runtime value. The indices before it
// must be constants: they walk to the aggregate the runtime index selects from,
// contributing their own byte offset, and the scale is the size of one element of
// that aggregate. `getelementptr %T, ptr %base, 0, %i` is the shape a loop over
// an array field produces.
bool dynamic_gep_scale(const ir::Instruction& instruction, std::uint32_t& offset,
                       std::uint32_t& scale, std::string& error) {
    offset = 0;
    scale = 0;
    ir::TypePtr current = instruction.source_type;
    const std::size_t last = instruction.operands.size() - 1;
    for (std::size_t index = 1; index < last; ++index) {
        const auto* constant =
            dynamic_cast<const ir::ConstantInt*>(instruction.operands[index]);
        if (!constant) {
            error = "KR16 dynamic GEP has more than one runtime index";
            return false;
        }
        const auto element = constant->value;
        if (current && current->kind == ir::TypeKind::Array) {
            offset += static_cast<std::uint32_t>(
                element * static_cast<std::int64_t>(type_size(current->element)));
            current = current->element;
            continue;
        }
        if (current && current->kind == ir::TypeKind::Struct) {
            if (element < 0 ||
                static_cast<std::size_t>(element) >= current->fields.size()) {
                error = "KR16 GEP struct index is out of range";
                return false;
            }
            for (std::int64_t field = 0; field < element; ++field) {
                offset += type_size(current->fields[static_cast<std::size_t>(field)]);
            }
            current = current->fields[static_cast<std::size_t>(element)];
            continue;
        }
        // The first index steps over whole source values, which is how a pointer
        // to one element becomes a pointer to the next.
        offset += static_cast<std::uint32_t>(
            element * static_cast<std::int64_t>(type_size(current)));
    }
    if (current && current->kind == ir::TypeKind::Array) {
        scale = static_cast<std::uint32_t>(type_size(current->element));
        return true;
    }
    if (current && current->kind == ir::TypeKind::Pointer) {
        scale = static_cast<std::uint32_t>(type_size(current->element));
        return true;
    }
    scale = static_cast<std::uint32_t>(type_size(current));
    return true;
}

}  // namespace

Kr16Backend::Kr16Backend(const ir::Module& module,
                         const target::TargetProfile& profile,
                         OptimizationLevel level)
    : module_(module), profile_(profile), level_(level) {}

Kr16Backend::FrameLayout Kr16Backend::layout_function(
    const ir::Function& function, std::string& error) const {
    FrameLayout layout;
    std::uint32_t offset = 0;
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block->instructions) {
            if (instruction->opcode == ir::Opcode::Alloca) {
                offset = (offset + 1) & ~1u;
                layout.allocas[instruction.get()] = offset;
                layout.slots[instruction.get()] = offset;
                offset += std::max<std::uint32_t>(
                    kSlotSize, type_size(instruction->allocated_type));
                continue;
            }
            if (instruction->type && !ir::is_void(instruction->type)) {
                offset = (offset + 1) & ~1u;
                layout.slots[instruction.get()] = offset;
                // A whole-record value needs its whole size, not one slot: a two-byte
                // slot made a four-byte struct share its tail with the next slot.
                offset += std::max<std::uint32_t>(kSlotSize, type_size(instruction->type));
            }
        }
    }
    // A slot for the destination of an aggregate return, which arrives as a hidden
    // stack argument (ADR 0048).
    if (returns_aggregate(function)) {
        offset = (offset + 1) & ~1u;
        layout.return_pointer = offset;
        layout.has_return_pointer = true;
        offset += kSlotSize;
    }
    for (const auto& parameter : function.parameters) {
        offset = (offset + 1) & ~1u;
        layout.slots[parameter.get()] = offset;
        offset += kSlotSize;
    }
    layout.frame_size = (offset + 1) & ~1u;
    if (layout.frame_size > 0x7FFF) {
        error = "KR16 frame exceeds the signed 16-bit displacement range";
    }
    return layout;
}

void Kr16Backend::add_debug_type(ir::TypePtr type) const {
    if (!type || ir::is_void(type)) {
        return;
    }
    const auto name = debug_type_name(type);
    if (!debug_types_.insert(name).second) {
        return;
    }
    const auto size = type_size(type);
    debug_info_ += "T " + name + " " + std::to_string(size) + std::string(1, '\0');
    if (type->kind == ir::TypeKind::Struct) {
        // The C record's base field is written now, `-` for none (v1.1), and
        // the base's own record is emitted first so a reader can follow it.
        if (type->base_type) {
            add_debug_type(type->base_type);
        }
        debug_info_ += "C " + name + " " + std::to_string(size) + " " +
                       (type->base.empty() ? "-" : type->base) +
                       std::string(1, '\0');
        std::uint32_t offset = 0;
        for (std::size_t index = 0; index < type->fields.size(); ++index) {
            const auto& field = type->fields[index];
            offset = round_up(offset, type_alignment(field));
            const auto field_size = type_size(field);
            const auto field_name = index < type->field_names.size()
                                        ? type->field_names[index]
                                        : std::to_string(index);
            debug_info_ += "M " + name + " " + field_name + " " +
                           std::to_string(offset) + " " + debug_type_name(field) +
                           " " + std::to_string(field_size) + std::string(1, '\0');
            add_debug_type(field);
            offset += field_size;
        }
    } else if (type->kind == ir::TypeKind::Array) {
        add_debug_type(type->element);
        debug_info_ += "A " + name + " " + debug_type_name(type->element) + " " +
                       std::to_string(type->count) + std::string(1, '\0');
    } else if (type->kind == ir::TypeKind::Pointer) {
        add_debug_type(type->element);
        debug_info_ += "R " + name + " " + debug_type_name(type->element) +
                       std::string(1, '\0');
    }
}

void Kr16Backend::emit_function(std::string& out,
                                const ir::Function& function,
                                const FrameLayout& layout,
                                std::string& error) const {
    const auto slot = [&](const ir::Value* value) -> std::uint32_t {
        const auto found = layout.slots.find(value);
        if (found == layout.slots.end()) {
            error = "KR16 value '" + value->name + "' has no frame slot";
            return 0;
        }
        return found->second;
    };
    // `adjust` compensates for bytes pushed below SP: the staged slots sit
    // above SP, so their SP-relative offsets grow by everything pushed.
    const auto load = [&](const ir::Value* value, std::string_view reg,
                          std::uint32_t adjust = 0) {
        if (!value) {
            out += "    MOV " + std::string(reg) + ", #0\n";
            return;
        }
        if (value->kind == ir::ValueKind::ConstantInt) {
            const auto constant =
                static_cast<const ir::ConstantInt*>(value)->value;
            out += "    MOV " + std::string(reg) + ", #" +
                   std::to_string(constant) + "\n";
            return;
        }
        if (value->kind == ir::ValueKind::ConstantNull ||
            value->kind == ir::ValueKind::ConstantUndef) {
            out += "    MOV " + std::string(reg) + ", #0\n";
            return;
        }
        if (value->kind == ir::ValueKind::Global) {
            out += "    LEA " + std::string(reg) + ", [" + value->name + "]\n";
            return;
        }
        if (value->kind == ir::ValueKind::Instruction) {
            const auto* instruction =
                static_cast<const ir::Instruction*>(value);
            if (instruction->opcode == ir::Opcode::Alloca) {
                out += "    LEA " + std::string(reg) + ", [SP+" +
                       std::to_string(slot(value) + adjust) + "]\n";
                return;
            }
        }
        out += "    LD " + std::string(reg) + ", [SP+" +
               std::to_string(slot(value) + adjust) + "]\n";
    };
    const auto store = [&](const ir::Instruction& instruction,
                           std::string_view reg) {
        out += "    ST [SP+" + std::to_string(slot(&instruction)) + "], " +
               std::string(reg) + "\n";
    };
    // The frame slot a pointer names directly, when the value is an alloca in
    // this function. Used to fold a load or store into an addressing mode
    // instead of a LEA followed by an indirect access.
    //
    // `byte_access` restricts the fold to the small M displacement (-32..31):
    // the assembler's wide byte forms map onto the word indexed ops
    // (`LoadByteAbsolute`/`StoreByteAbsolute` become `LoadIndexed`/`StoreIndexed`),
    // which is unverified and produced wrong values the first time it was
    // reachable, so a byte access outside that range keeps the register path.
    const auto frame_object = [&](const ir::Value* value,
                                  bool byte_access) -> std::optional<std::uint32_t> {
        const auto* pointer = dynamic_cast<const ir::Instruction*>(value);
        if (!pointer || pointer->opcode != ir::Opcode::Alloca) {
            return std::nullopt;
        }
        const auto found = layout.slots.find(pointer);
        if (found == layout.slots.end()) {
            return std::nullopt;
        }
        const bool wide = found->second > 31;
        if (byte_access && wide) {
            return std::nullopt;
        }
        return found->second;
    };

    if (function.linkage == ir::Linkage::External) {
        out += ".global " + function.name + "\n";
    } else if (function.linkage == ir::Linkage::Weak) {
        // A template or inline definition: visible, overridable, and duplicated
        // across objects without a duplicate-symbol error.
        out += ".weak " + function.name + "\n";
    }
    out += function.name + ":\n";
    out += "    PUSH R4\n";
    out += "    MOV R4, SP\n";
    out += "    LEA SP, [R4-" + std::to_string(layout.frame_size) + "]\n";
    if (layout.has_return_pointer) {
        // The destination is the last hidden argument, one slot above the declared
        // ones; nothing survives a call in a register on this target, so it goes
        // straight into the frame (ADR 0048).
        // Only parameters past the third reach the stack (ADR 0028), so those are
        // what the destination sits above.
        const auto stack_parameters =
            function.parameters.size() > 3 ? function.parameters.size() - 3 : std::size_t{0};
        out += "    LD R0, [R4+" +
               std::to_string(4 + 2 * stack_parameters) + "]\n";
        out += "    ST [SP+" + std::to_string(layout.return_pointer) + "], R0\n";
    }
    static const char* arguments[] = {"R1", "R2", "R3"};
    // A variadic callee receives every argument on the stack, so one rule reads
    // both the named parameters and the variable ones (ADR 0029).
    const bool variadic = function.type && function.type->variadic;
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        if (variadic) {
            out += "    LD R0, [R4+" + std::to_string(4 + 2 * index) + "]\n";
            out += "    ST [SP+" +
                   std::to_string(slot(function.parameters[index].get())) +
                   "], R0\n";
            continue;
        }
        if (index < 3) {
            out += "    ST [SP+" +
                   std::to_string(slot(function.parameters[index].get())) + "], " +
                   arguments[index] + "\n";
            continue;
        }
        // The caller pushed the extras above the return address, last argument
        // first, so the fourth sits just past the saved frame pointer
        // (docs/abi-kr16.md, ADR 0028).
        out += "    LD R0, [R4+" + std::to_string(4 + 2 * (index - 3)) + "]\n";
        out += "    ST [SP+" +
               std::to_string(slot(function.parameters[index].get())) + "], R0\n";
    }

    std::size_t synthetic_label = 0;
    // A comparison whose only use is the conditional branch that tests it does
    // not need a boolean in a frame slot: the branch can consume the comparison
    // directly. That holds only while the comparison has exactly one use, so the
    // uses are counted for the whole function first.
    folded_conditions_.clear();
    {
        std::unordered_map<const ir::Value*, std::size_t> uses;
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block->instructions) {
                for (const auto* operand : instruction->operands) {
                    if (operand) {
                        ++uses[operand];
                    }
                }
            }
        }
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block->instructions) {
                if (instruction->opcode != ir::Opcode::CondBr ||
                    instruction->operands.empty()) {
                    continue;
                }
                const auto* condition =
                    dynamic_cast<const ir::Instruction*>(instruction->operands[0]);
                if (!condition || condition->opcode != ir::Opcode::ICmp ||
                    condition->operands.empty()) {
                    continue;
                }
                if (uses[condition] != 1) {
                    continue;
                }
                folded_conditions_.insert(condition);
            }
        }
    }
    std::size_t debug_label_counter = 0;
    for (const auto& block : function.blocks) {
        out += label_for(*block) + ":\n";
        for (const auto& instruction : block->instructions) {
            if (folded_conditions_.count(instruction.get()) != 0) {
                continue;
            }
            // A phi has no code of its own: its value is written on the incoming
            // edge, so the copy goes at the end of each predecessor.
            if (instruction->opcode == ir::Opcode::Phi) {
                continue;
            }
            if (ir::is_terminator(instruction->opcode)) {
                std::size_t successor_count = 0;
                for (const auto* successor : instruction->successors) {
                    if (successor) {
                        ++successor_count;
                    }
                }
                for (const auto* successor : instruction->successors) {
                    if (!successor) {
                        continue;
                    }
                    std::size_t predecessor_count = 0;
                    for (const auto& candidate : function.blocks) {
                        if (!candidate) {
                            continue;
                        }
                        for (const auto& other : candidate->instructions) {
                            if (!other) {
                                continue;
                            }
                            for (const auto* target : other->successors) {
                                if (target == successor) {
                                    ++predecessor_count;
                                }
                            }
                        }
                    }
                    // The copy belongs to this edge alone; an edge that is
                    // critical at both ends has nowhere to put it.
                    const bool critical = successor_count > 1 && predecessor_count > 1;
                    for (const auto& phi : successor->instructions) {
                        if (!phi || phi->opcode != ir::Opcode::Phi) {
                            continue;
                        }
                        for (const auto& incoming : phi->incoming) {
                            if (incoming.second != block.get() || !incoming.first) {
                                continue;
                            }
                            if (critical) {
                                out += "    HALT ; phi on a critical edge: split the edge first\n";
                                continue;
                            }
                            load(incoming.first, "R0");
                            store(*phi, "R0");
                        }
                    }
                }
            }
            // The bridge records a source line per emitted instruction, the same
            // way Kr32Backend does: an `.Ldbg` label the linker resolves to an
            // address, and an L record naming its file, line and column. A line
            // whose instructions a pass removed has no label and no record, and
            // neither does a declaration: an Alloca emits nothing, so its label
            // would share the next statement's address and a debugger stopping
            // there would report the declaration's line for the statement. That
            // is why `int local;` on line 2 used to shadow `local = 7;` on line 3.
            if (instruction->opcode != ir::Opcode::Alloca && instruction->debug.valid()) {
                auto source = debug_sources_.find(instruction->debug.file);
                if (source == debug_sources_.end()) {
                    const auto id = next_debug_source_++;
                    source = debug_sources_.emplace(instruction->debug.file, id).first;
                    debug_info_ += "S " + std::to_string(id) + " " +
                                   instruction->debug.file + std::string(1, '\0');
                }
                const auto label = ".Ldbg." + function.name + "." +
                                   std::to_string(debug_label_counter++);
                out += label + ":\n";
                debug_info_ += "L " + label + " " +
                               std::to_string(source->second) + " " +
                               std::to_string(instruction->debug.line) + " " +
                               std::to_string(instruction->debug.column) +
                               std::string(1, '\0');
            }
            switch (instruction->opcode) {
            case ir::Opcode::Alloca:
                break;
            case ir::Opcode::VaStart: {
                // The first variable argument follows the named parameters in
                // the caller's pushed area (ADR 0029).
                const auto fixed = function.parameters.size();
                out += "    LEA R0, [R4+" + std::to_string(4 + 2 * fixed) + "]\n";
                store(*instruction, "R0");
                break;
            }
            case ir::Opcode::Load:
                // A whole-record load copies the record, as KR32 does. One word carried
                // only the first field, which truncated an aggregate return.
                if (instruction->type &&
                    (instruction->type->kind == ir::TypeKind::Struct ||
                     instruction->type->kind == ir::TypeKind::Union)) {
                    load(instruction->operands[0], "R0");
                    out += "    MOV R1, R0\n";
                    const auto size = type_size(instruction->type);
                    const auto destination = slot(instruction.get());
                    for (std::uint32_t offset = 0; offset < size; offset += kSlotSize) {
                        out += "    LD R2, [R1+" + std::to_string(offset) + "]\n";
                        out += "    ST [SP+" +
                               std::to_string(destination + offset) + "], R2\n";
                    }
                    break;
                }
                if (const auto object = frame_object(
                        instruction->operands[0],
                        instruction->type && instruction->type->bits == 8);
                    object) {
                    const auto load_mnemonic =
                        instruction->type && instruction->type->bits == 8 ? "LDB" : "LD";
                    out += "    " + std::string(load_mnemonic) + " R1, [SP+" +
                           std::to_string(*object) + "]\n";
                    store(*instruction, "R1");
                    break;
                }
                load(instruction->operands[0], "R0");
                out += instruction->type && instruction->type->bits == 8
                           ? "    LDB R1, [R0]\n"
                           : "    LD R1, [R0]\n";
                store(*instruction, "R1");
                break;
            case ir::Opcode::Store:
                // A whole-record store copies the record, like the load above.
                if (instruction->operands[0] &&
                    instruction->operands[0]->type &&
                    (instruction->operands[0]->type->kind == ir::TypeKind::Struct ||
                     instruction->operands[0]->type->kind == ir::TypeKind::Union)) {
                    load(instruction->operands[1], "R1");
                    const auto size = type_size(instruction->operands[0]->type);
                    const auto source = slot(instruction->operands[0]);
                    for (std::uint32_t offset = 0; offset < size; offset += kSlotSize) {
                        out += "    LD R2, [SP+" + std::to_string(source + offset) + "]\n";
                        out += "    ST [R1+" + std::to_string(offset) + "], R2\n";
                    }
                    break;
                }
                if (const auto object = frame_object(
                        instruction->operands[1],
                        instruction->operands[0] && instruction->operands[0]->type &&
                            instruction->operands[0]->type->bits == 8);
                    object) {
                    load(instruction->operands[0], "R0");
                    const auto store_mnemonic =
                        instruction->operands[0] && instruction->operands[0]->type &&
                                instruction->operands[0]->type->bits == 8
                            ? "STB"
                            : "ST";
                    out += "    " + std::string(store_mnemonic) + " [SP+" +
                           std::to_string(*object) + "], R0\n";
                    break;
                }
                load(instruction->operands[0], "R0");
                load(instruction->operands[1], "R1");
                out += instruction->operands[0] &&
                               instruction->operands[0]->type &&
                               instruction->operands[0]->type->bits == 8
                           ? "    STB [R1], R0\n"
                           : "    ST [R1], R0\n";
                break;
            case ir::Opcode::Mul:
            case ir::Opcode::SDiv:
            case ir::Opcode::UDiv:
            case ir::Opcode::SRem:
            case ir::Opcode::URem:
                load(instruction->operands[0], "R0");
                load(instruction->operands[1], "R1");
                out += instruction->opcode == ir::Opcode::Mul
                           ? "    MUL R0, R1\n"
                           : (instruction->opcode == ir::Opcode::SDiv
                                  ? "    DIV R0, R1\n"
                                  : (instruction->opcode == ir::Opcode::UDiv
                                         ? "    DIVU R0, R1\n"
                                         : (instruction->opcode ==
                                                    ir::Opcode::SRem
                                                ? "    MOD R0, R1\n"
                                                : "    MODU R0, R1\n")));
                store(*instruction, "R0");
                break;
            case ir::Opcode::Shl:
            case ir::Opcode::LShr:
            case ir::Opcode::AShr: {
                const auto mnemonic = instruction->opcode == ir::Opcode::Shl
                                          ? "SHL"
                                          : (instruction->opcode == ir::Opcode::LShr
                                                 ? "SHR"
                                                 : "SAR");
                load(instruction->operands[0], "R0");
                if (const auto* literal = inline_literal(instruction->operands[1])) {
                    out += "    " + std::string(mnemonic) + " R0, #" +
                           std::to_string(literal->value) + "\n";
                } else {
                    load(instruction->operands[1], "R1");
                    out += "    " + std::string(mnemonic) + " R0, R1\n";
                }
                store(*instruction, "R0");
                break;
            }
            case ir::Opcode::Add:
            case ir::Opcode::Sub:
            case ir::Opcode::And:
            case ir::Opcode::Or:
            case ir::Opcode::Xor: {
                const auto mnemonic =
                    instruction->opcode == ir::Opcode::Add
                        ? "ADD"
                        : (instruction->opcode == ir::Opcode::Sub
                               ? "SUB"
                               : (instruction->opcode == ir::Opcode::And
                                      ? "AND"
                                      : (instruction->opcode == ir::Opcode::Or
                                             ? "OR"
                                             : "XOR")));
                // A literal operand travels in the instruction. Reversing the
                // operands of a commutative op puts the literal in that position;
                // the value the instruction computes is unchanged.
                const bool commutative = instruction->opcode != ir::Opcode::Sub;
                const auto* right_literal = inline_literal(instruction->operands[1]);
                const auto* left_literal =
                    commutative ? inline_literal(instruction->operands[0]) : nullptr;
                const ir::Value* register_operand = right_literal
                                                        ? instruction->operands[0]
                                                    : left_literal
                                                        ? instruction->operands[1]
                                                        : instruction->operands[0];
                const auto* literal = right_literal ? right_literal : left_literal;
                load(register_operand, "R0");
                if (literal) {
                    out += "    " + std::string(mnemonic) + " R0, #" +
                           std::to_string(literal->value) + "\n";
                } else {
                    load(instruction->operands[1], "R1");
                    out += "    " + std::string(mnemonic) + " R0, R1\n";
                }
                store(*instruction, "R0");
                break;
            }
            case ir::Opcode::ICmp: {
                load(instruction->operands[0], "R0");
                if (const auto* literal = inline_literal(instruction->operands[1])) {
                    out += "    CMP R0, #" + std::to_string(literal->value) + "\n";
                } else {
                    load(instruction->operands[1], "R1");
                    out += "    CMP R0, R1\n";
                }
                const auto false_label =
                    ".Lcmp.false." + function.name + "." +
                    std::to_string(synthetic_label);
                const auto true_label =
                    ".Lcmp.true." + function.name + "." +
                    std::to_string(synthetic_label);
                const auto end_label =
                    ".Lcmp.end." + function.name + "." +
                    std::to_string(synthetic_label);
                ++synthetic_label;
                out += "    " +
                       inverse_condition_for(instruction->icmp_predicate) + " " +
                       false_label + "\n";
                out += "    BRA " + true_label + "\n";
                out += false_label + ":\n";
                out += "    MOV R0, #0\n";
                out += "    BRA " + end_label + "\n";
                out += true_label + ":\n";
                out += "    MOV R0, #1\n";
                out += end_label + ":\n";
                store(*instruction, "R0");
                break;
            }
            case ir::Opcode::BitCast:
                if (!instruction->operands.empty() &&
                    instruction->operands[0] &&
                    instruction->operands[0]->kind == ir::ValueKind::Function) {
                    // A function's address is an address for the same reason.
                    out += "    LEA R0, [" + instruction->operands[0]->name +
                           "]\n";
                    store(*instruction, "R0");
                    break;
                }
                load(instruction->operands[0], "R0");
                store(*instruction, "R0");
                break;
            case ir::Opcode::PtrToInt:
            case ir::Opcode::IntToPtr:
            case ir::Opcode::ZExt:
            case ir::Opcode::SExt:
            case ir::Opcode::Trunc:
                load(instruction->operands[0], "R0");
                store(*instruction, "R0");
                break;
            case ir::Opcode::Br:
                if (instruction->successors.empty()) {
                    error = "KR16 branch has no destination";
                    return;
                }
                out += "    BRA " + label_for(*instruction->successors[0]) +
                       "\n";
                break;
            case ir::Opcode::CondBr: {
                if (instruction->successors.size() < 2) {
                    error = "KR16 conditional branch needs two destinations";
                    return;
                }
                if (const auto* condition =
                        dynamic_cast<const ir::Instruction*>(instruction->operands[0]);
                    condition && folded_conditions_.count(condition) != 0) {
                    load(condition->operands[0], "R0");
                    if (const auto* literal = inline_literal(condition->operands[1])) {
                        out += "    CMP R0, #" + std::to_string(literal->value) + "\n";
                    } else {
                        load(condition->operands[1], "R1");
                        out += "    CMP R0, R1\n";
                    }
                    const auto false_label =
                        ".Lcond.false." + function.name + "." +
                        std::to_string(synthetic_label);
                    ++synthetic_label;
                    out += "    " +
                           inverse_condition_for(condition->icmp_predicate) + " " +
                           false_label + "\n";
                    out += "    BRA " + label_for(*instruction->successors[0]) +
                           "\n";
                    out += false_label + ":\n";
                    out += "    BRA " + label_for(*instruction->successors[1]) +
                           "\n";
                    break;
                }
                load(instruction->operands[0], "R0");
                out += "    CMP R0, #0\n";
                const auto false_label =
                    ".Lcond.false." + function.name + "." +
                    std::to_string(synthetic_label);
                ++synthetic_label;
                out += "    BEQ " + false_label + "\n";
                out += "    BRA " + label_for(*instruction->successors[0]) +
                       "\n";
                out += false_label + ":\n";
                out += "    BRA " + label_for(*instruction->successors[1]) +
                       "\n";
                break;
            }
            case ir::Opcode::Call: {
                const auto argument_count = instruction->operands.size() - 1;
                // KR16's shipped runtime wrappers call `__host_call(n, block)`,
                // which kr16's own compiler lowers to `SYS #n` with the block in
                // R0 and then loads the status from the block
                // (kr16 src/cc/compiler.cpp:8094-8100). The service number is a
                // compile-time immediate; a non-constant or out-of-range service
                // falls through to an ordinary call, which the linker then
                // reports as an undefined symbol rather than mis-lowering it.
                const auto* direct_callee = instruction->operands[0];
                if (direct_callee && direct_callee->kind == ir::ValueKind::Function &&
                    direct_callee->name == "__host_call" && argument_count == 2) {
                    const auto* service = dynamic_cast<const ir::ConstantInt*>(
                        instruction->operands[1]);
                    if (service && service->value >= 0 && service->value <= 255) {
                        load(instruction->operands[2], "R0");
                        out += "    SYS #" + std::to_string(service->value) + "\n";
                        out += "    LD R0, [R0]\n";
                        if (instruction->type && !ir::is_void(instruction->type)) {
                            store(*instruction, "R0");
                        }
                        break;
                    }
                }
                const auto* callee_type = [&]() -> const ir::Type* {
                    const auto* callee = instruction->operands[0];
                    if (!callee || !callee->type) {
                        return nullptr;
                    }
                    if (callee->type->kind == ir::TypeKind::Function) {
                        return callee->type;
                    }
                    if (callee->type->kind == ir::TypeKind::Pointer &&
                        callee->type->element &&
                        callee->type->element->kind == ir::TypeKind::Function) {
                        return callee->type->element;
                    }
                    return nullptr;
                }();
                const bool variadic_call = callee_type && callee_type->variadic;
                const bool aggregate_return =
                    instruction->type &&
                    (instruction->type->kind == ir::TypeKind::Struct ||
                     instruction->type->kind == ir::TypeKind::Union);
                // Register arguments first, and before the destination is pushed: the
                // staged values are addressed relative to SP, so pushing anything first
                // shifts every one of them. That was the defect behind
                // `struct { int a; char c; }` losing `c` when returned by value -- the
                // second argument was loaded as `[SP+4]` after the destination PUSH had
                // moved SP, so it read the destination pointer instead of the value. It
                // only showed when an argument came from a stack slot; a call whose
                // arguments were all immediates was unaffected, which is why `{int, int}`
                // returned correctly and this did not.
                if (!variadic_call) {
                    for (std::size_t index = 0;
                         index < argument_count && index < 3; ++index) {
                        load(instruction->operands[index + 1], arguments[index]);
                    }
                }
                // The aggregate destination is the last hidden stack argument, so it
                // is pushed after the register arguments and before the extras: pushes
                // run highest index first, which leaves the first push at the highest
                // address (ADR 0048).
                if (aggregate_return) {
                    out += "    LEA R0, [R4-" +
                           std::to_string(layout.frame_size - slot(instruction.get())) +
                           "]\n";
                    out += "    PUSH R0\n";
                }
                // Extras go out last first, so the first argument ends at the
                // lowest address and they run upward in declaration order. A
                // variadic call passes every argument this way.
                const std::size_t first_pushed = variadic_call ? 1 : 4;
                for (std::size_t index = argument_count, pushed = 0;
                     index >= first_pushed; --index, ++pushed) {
                    load(instruction->operands[index], "R0",
                         static_cast<std::uint32_t>(2 * pushed));
                    out += "    PUSH R0\n";
                    if (index == first_pushed) {
                        break;
                    }
                }
                const auto pushed_count =
                    argument_count >= first_pushed
                        ? argument_count - first_pushed + 1
                        : std::size_t{0};
                const auto* callee = instruction->operands[0];
                if (callee->kind == ir::ValueKind::Function) {
                    // A symbol is an address, not a sixteen-bit immediate: the
                    // assembler cannot resolve one at assembly time and rejects it.
                    out += "    LEA R0, [" + callee->name + "]\n";
                } else {
                    // The callee value is read after the pushes, so its slot
                    // offset carries the same adjustment as the arguments.
                    load(callee, "R0",
                         static_cast<std::uint32_t>(2 * pushed_count));
                }
                out += "    CALL R0\n";
                const auto pushed_total =
                    pushed_count + (aggregate_return ? std::size_t{1} : std::size_t{0});
                if (pushed_total > 0) {
                    out += "    LEA SP, [SP+" + std::to_string(2 * pushed_total) +
                           "]\n";
                }
                if (aggregate_return) {
                    // The callee wrote the aggregate to the destination already.
                } else if (instruction->type && !ir::is_void(instruction->type)) {
                    store(*instruction, "R0");
                }
                break;
            }
            case ir::Opcode::GetElementPtr: {
                // Two forms reach here: [%i] selects from the source type and
                // keeps its own lowering, and [0, %i] selects an element of an
                // aggregate field, which is the shape a loop over an array
                // member produces. Only the second is new here.
                const bool aggregate_index =
                    instruction->operands.size() >= 3 &&
                    dynamic_cast<const ir::ConstantInt*>(
                        instruction->operands.back()) == nullptr;
                const bool dynamic_index =
                    aggregate_index ||
                    (instruction->operands.size() == 2 &&
                     dynamic_cast<const ir::ConstantInt*>(
                         instruction->operands[1]) == nullptr);
                std::uint32_t offset = 0;
                if (!dynamic_index &&
                    !gep_offset(*instruction, offset, error)) {
                    return;
                }
                if (!instruction->operands.empty() &&
                    instruction->operands[0] &&
                    instruction->operands[0]->kind ==
                        ir::ValueKind::Global) {
                    out += "    LEA R0, [" + instruction->operands[0]->name +
                           "]\n";
                } else {
                    load(instruction->operands[0], "R0");
                }
                if (aggregate_index) {
                    std::uint32_t scale = 0;
                    if (!dynamic_gep_scale(*instruction, offset, scale, error)) {
                        return;
                    }
                    load(instruction->operands.back(), "R1");
                    if (scale == 2) {
                        out += "    ADD R1, R1\n";
                    } else if (scale == 4) {
                        out += "    SHL R1, #2\n";
                    } else if (scale == 8) {
                        out += "    SHL R1, #3\n";
                    } else if (scale != 1) {
                        // Any other element size multiplies: a record of three
                        // ints is six bytes, and a loop over an array of them is
                        // as ordinary as one over a pair.
                        out += "    MOV R2, #" + std::to_string(scale) + "\n";
                        out += "    MUL R1, R2\n";
                    }
                    out += "    ADD R0, R1\n";
                }
                if (!aggregate_index && dynamic_index) {
                    const auto size = type_size(instruction->source_type);
                    load(instruction->operands[1], "R1");
                    if (size == 2) {
                        out += "    ADD R1, R1\n";
                    } else if (size == 4) {
                        out += "    SHL R1, #2\n";
                    } else if (size == 8) {
                        out += "    SHL R1, #3\n";
                    } else if (size != 1) {
                        // The same fallback as the aggregate form: an element
                        // that is neither one, two, four, nor eight bytes
                        // multiplies.
                        out += "    MOV R2, #" + std::to_string(size) + "\n";
                        out += "    MUL R1, R2\n";
                    }
                    out += "    ADD R0, R1\n";
                }
                if (!aggregate_index && offset != 0) {
                    // A negative displacement is stored as a 32-bit two's
                    // complement, and printing that unsigned puts 4294967292 in
                    // a sixteen-bit immediate: the assembler cannot accept it.
                    out += "    MOV R1, #" +
                           std::to_string(static_cast<std::int32_t>(offset)) +
                           "\n";
                    out += "    ADD R0, R1\n";
                }
                store(*instruction, "R0");
                break;
            }
            case ir::Opcode::Ret:
                if (!instruction->operands.empty() &&
                    instruction->operands[0]) {
                    // An aggregate is copied to the caller's destination in two-byte
                    // words; the destination arrived as a hidden stack argument
                    // (ADR 0048). R0 carries it and R1 each word.
                    if (instruction->operands[0]->type &&
                        (instruction->operands[0]->type->kind ==
                             ir::TypeKind::Struct ||
                         instruction->operands[0]->type->kind ==
                             ir::TypeKind::Union)) {
                        if (!layout.has_return_pointer) {
                            error = "KR16 aggregate returns are not implemented";
                            return;
                        }
                        const auto size = type_size(instruction->operands[0]->type);
                        out += "    LD R0, [SP+" +
                               std::to_string(layout.return_pointer) + "]\n";
                        for (std::uint32_t word = 0; word < size; word += kSlotSize) {
                            load(instruction->operands[0], "R1", word);
                            out += "    ST [R0+" + std::to_string(word) + "], R1\n";
                        }
                    } else {
                        load(instruction->operands[0], "R0");
                    }
                }
                out += "    BRA .L" + function.name + ".ret\n";
                break;
            case ir::Opcode::Unreachable:
                // Nothing branches here. Jumping to the function's return keeps
                // the instruction stream well formed, and a path that reaches it
                // is a program that had no defined behaviour anyway. A 	hrow`r
                // in a header such as <stdexcept> ends its function this way.
                out += "    BRA .L" + function.name + ".ret\n";
                break;
            default:
                error = "KR16 backend does not support opcode " +
                        std::string(ir::opcode_name(instruction->opcode));
                return;
            }
        }
    }
    // The display name is what a debugger shows; the symbol stays mangled
    // (docs/decisions/0030-debug-display-names.md).
    const auto& display_name =
        function.debug_name.empty() ? function.name : function.debug_name;
    debug_info_ += "F " + display_name + " " + function.name + " .L" +
                   function.name + ".ret " + std::to_string(layout.frame_size) +
                   " " +
                   debug_type_name(function.type ? function.type->return_type
                                                 : nullptr) +
                   std::string(1, '\0');
    // The function scope; `-` marks the function level, which has no parent.
    // KR16 has no separate `.end` label: `.ret` is its function end.
    debug_info_ += "K " + function.name + " " + function.name + " - " +
                   function.name + " .L" + function.name + ".ret" +
                   std::string(1, '\0');
    // A frame offset in the bridge is positive and measured from the frame base,
    // as KR32's is from R7. This frame base is R4 and the slots are addressed
    // from SP (= R4 - frame_size), so a slot at [SP+off] is at R4 - (frame_size -
    // off): the offset a debugger that reads `fp - offset` expects, which is the
    // convention kr16's own `.debug_vars` uses.
    const auto frame_offset = [&](std::uint32_t slot_offset) {
        return layout.frame_size >= slot_offset ? layout.frame_size - slot_offset
                                                : 0u;
    };
    std::unordered_set<std::string> debug_variable_names;
    for (const auto& parameter : function.parameters) {
        const auto parameter_slot = layout.slots.find(parameter.get());
        if (parameter_slot == layout.slots.end() || !parameter->type) {
            continue;
        }
        if (!debug_variable_names.insert(function.name + "." + parameter->name)
                 .second) {
            continue;
        }
        add_debug_type(parameter->type);
        debug_info_ += "V " + function.name + " " + parameter->name + " " +
                       std::to_string(frame_offset(parameter_slot->second)) + " " +
                       debug_type_name(parameter->type) + " " +
                       std::to_string(type_size(parameter->type)) +
                       std::string(1, '\0');
        debug_info_ += "W " + function.name + " " + parameter->name + " param " +
                       function.name + " 0x0" + std::string(1, '\0');
    }
    std::unordered_set<std::string> parameter_names;
    for (const auto& parameter : function.parameters) {
        parameter_names.insert(parameter->name);
    }
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block->instructions) {
            if (instruction->opcode != ir::Opcode::Alloca ||
                !instruction->allocated_type) {
                continue;
            }
            const auto object = layout.allocas.find(instruction.get());
            if (object == layout.allocas.end()) {
                continue;
            }
            // A source variable is the alloca IRGen named `<variable>.addr`; a
            // compiler temporary is not one a debugger should list (docs/symbols.md).
            auto name = instruction->name;
            if (name.size() <= 5 || name.compare(name.size() - 5, 5, ".addr") != 0) {
                continue;
            }
            name.resize(name.size() - 5);
            if (parameter_names.contains(name)) {
                continue;
            }
            if (!debug_variable_names.insert(function.name + "." + name).second) {
                continue;
            }
            add_debug_type(instruction->allocated_type);
            debug_info_ += "V " + function.name + " " + name + " " +
                           std::to_string(frame_offset(object->second)) + " " +
                           debug_type_name(instruction->allocated_type) + " " +
                           std::to_string(type_size(instruction->allocated_type)) +
                           std::string(1, '\0');
            debug_info_ += "W " + function.name + " " + name + " local " +
                           function.name + " 0x0" + std::string(1, '\0');
        }
    }

    out += ".L" + function.name + ".ret:\n";
    out += "    MOV SP, R4\n";
    out += "    POP R4\n";
    out += "    RET\n";
}

std::uint32_t Kr16Backend::frame_bytes(const ir::Function& function) const {
    std::string error;
    return layout_function(function, error).frame_size;
}

std::optional<std::string> Kr16Backend::emit(std::string& error) const {
    static_cast<void>(profile_);
    std::string out = "; KR16 assembly generated by krc\n";
    // Per-emission state: one build's bridge cannot leak into the next.
    debug_info_.clear();
    debug_sources_.clear();
    debug_types_.clear();
    next_debug_source_ = 0;
    out += ".text\n";
    for (const auto& function : module_.functions()) {
        if (function->declaration) {
            out += ".extern " + function->name + "\n";
            continue;
        }
        for (const auto& parameter : function->parameters) {
            if (parameter->type &&
                (parameter->type->kind == ir::TypeKind::Struct ||
                 ir::is_float(parameter->type))) {
                error = "KR16 aggregate and floating-point parameters are not "
                        "implemented";
                return std::nullopt;
            }
        }
        const auto layout = layout_function(*function, error);
        if (!error.empty()) {
            return std::nullopt;
        }
        emit_function(out, *function, layout, error);
        if (!error.empty()) {
            return std::nullopt;
        }
    }
    for (const auto& global : module_.globals()) {
        if (global->external) {
            out += "    .extern " + global->name + "\n";
            continue;
        }
        const auto* data =
            dynamic_cast<const ir::ConstantDataArray*>(global->initializer);
        if (global->name.empty() || global->name.front() != '.') {
            if (!global->internal) {
                out += (global->weak ? "    .weak " : "    .global ") +
                       global->name + "\n";
            }
        }
        out += global->name + ":\n";
        // A table of addresses -- a vtable, a typeinfo descriptor -- needs the
        // symbol in the data, not a zero: the assembler records a relocation at
        // that offset and the linker fills it.
        if (const auto* addresses =
                dynamic_cast<const ir::ConstantAddressArray*>(
                    global->initializer)) {
            for (const auto* element : addresses->elements) {
                if (element && (element->kind == ir::ValueKind::Function ||
                                element->kind == ir::ValueKind::Global)) {
                    out += "    .short " + element->name + "\n";
                } else {
                    out += "    .short 0\n";
                }
            }
            continue;
        }
        if (data) {
            out += "    .byte ";
            for (std::size_t index = 0; index < data->bytes.size(); ++index) {
                if (index != 0) {
                    out += ", ";
                }
                out += std::to_string(data->bytes[index]);
            }
            out += "\n";
            if (data->bytes.size() % 2 != 0) {
                out += "    .byte 0\n";
            }
            continue;
        }
        if (const auto* constant =
                dynamic_cast<const ir::ConstantInt*>(global->initializer)) {
            const auto size = type_size(global->element_type);
            out += "    .byte ";
            for (std::uint32_t index = 0; index < size; ++index) {
                if (index != 0) {
                    out += ", ";
                }
                out += std::to_string(
                    static_cast<std::uint8_t>(
                        (constant->value >> (index * 8)) & 0xFF));
            }
            out += "\n";
            continue;
        }
        const auto size = type_size(global->element_type);
        for (std::uint32_t offset = 0; offset < size; offset += 16) {
            out += "    .byte ";
            const auto count = std::min<std::uint32_t>(16, size - offset);
            for (std::uint32_t index = 0; index < count; ++index) {
                if (index != 0) {
                    out += ", ";
                }
                out += "0";
            }
            out += "\n";
        }
    }
    for (const auto& global : module_.globals()) {
        if (global->external) {
            continue;
        }
        add_debug_type(global->element_type);
        debug_info_ += "G " + global->name + " " + global->name + " " +
                       debug_type_name(global->element_type) + " " +
                       std::to_string(type_size(global->element_type)) +
                       std::string(1, '\0');
    }
    // `-O1` runs the text pass; `-O0` returns the emitter's text untouched, which
    // is what keeps the default level byte-identical to a build without the flag.
    if (level_ == OptimizationLevel::O1) {
        return remove_redundant_jumps(out);
    }
    return out;
}

}  // namespace krc::backend
