#include "krc/frontend/source_manager.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace krc::frontend {

SourceFile::SourceFile(std::uint32_t id, std::string name, std::string text)
    : id_(id), name_(std::move(name)), text_(std::move(text)) {
    line_starts_.push_back(0);
    for (std::size_t i = 0; i < text_.size(); ++i) {
        if (text_[i] == '\n') {
            line_starts_.push_back(static_cast<std::uint32_t>(i + 1));
        }
    }
}

LineColumn SourceFile::line_column(std::uint32_t offset) const {
    const auto clamped = std::min(offset, size());
    const auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), clamped);
    const auto index = static_cast<std::size_t>(std::distance(line_starts_.begin(), it) - 1);
    const auto physical_line = static_cast<std::uint32_t>(index + 1);
    std::uint32_t virtual_line = physical_line;
    for (const auto& directive : line_directives_) {
        if (directive.offset <= clamped && physical_line >= directive.next_physical_line) {
            virtual_line = directive.virtual_line + (physical_line - directive.next_physical_line);
        }
    }
    return LineColumn{
        virtual_line,
        static_cast<std::uint32_t>(clamped - line_starts_[index] + 1),
    };
}

void SourceFile::add_line_directive(std::uint32_t offset, std::uint32_t next_physical_line,
                                    std::uint32_t virtual_line, std::string file_name) {
    line_directives_.push_back(
        LineDirective{offset, next_physical_line, virtual_line, std::move(file_name)});
}

std::string_view SourceFile::display_name(std::uint32_t offset) const {
    const auto clamped = std::min(offset, size());
    std::string_view name = name_;
    for (const auto& directive : line_directives_) {
        if (directive.offset <= clamped && !directive.file_name.empty()) {
            name = directive.file_name;
        }
    }
    return name;
}

std::uint32_t SourceFile::physical_line(std::uint32_t offset) const {
    const auto clamped = std::min(offset, size());
    const auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), clamped);
    return static_cast<std::uint32_t>(std::distance(line_starts_.begin(), it));
}

std::string_view SourceFile::line_text(std::uint32_t offset) const {
    const auto clamped = std::min(offset, size());
    const auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), clamped);
    const auto index = static_cast<std::size_t>(std::distance(line_starts_.begin(), it) - 1);
    const auto start = line_starts_[index];
    const auto end = index + 1 < line_starts_.size()
                         ? line_starts_[index + 1]
                         : static_cast<std::uint32_t>(text_.size());
    auto line = std::string_view(text_).substr(start, end - start);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.remove_suffix(1);
    }
    return line;
}

std::uint32_t SourceManager::add_buffer(std::string name, std::string text) {
    if (name.empty()) {
        name = "<buffer>";
    }
    const auto id = static_cast<std::uint32_t>(files_.size());
    files_.emplace_back(id, std::move(name), std::move(text));
    return id;
}

std::optional<std::uint32_t> SourceManager::load_file(const std::filesystem::path& path,
                                                      std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "unable to open '" + path.string() + "'";
        return std::nullopt;
    }

    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad()) {
        error = "unable to read '" + path.string() + "'";
        return std::nullopt;
    }
    return add_buffer(path.string(), std::move(text));
}

const SourceFile& SourceManager::file(std::uint32_t id) const {
    if (id >= files_.size()) {
        throw std::out_of_range("source file id is out of range");
    }
    return files_[id];
}

SourceLocation SourceManager::location(std::uint32_t file_id, std::uint32_t offset) const {
    if (file_id >= files_.size()) {
        return SourceLocation{};
    }
    return SourceLocation{file_id, std::min(offset, files_[file_id].size())};
}

LineColumn SourceManager::line_column(SourceLocation location) const {
    if (!location.valid()) {
        return LineColumn{};
    }
    return file(location.file_id).line_column(location.offset);
}

std::uint32_t SourceManager::physical_line(SourceLocation location) const {
    if (!location.valid()) {
        return 0;
    }
    return file(location.file_id).physical_line(location.offset);
}

void SourceManager::add_line_directive(std::uint32_t file_id, std::uint32_t offset,
                                       std::uint32_t next_physical_line,
                                       std::uint32_t virtual_line, std::string file_name) {
    if (file_id < files_.size()) {
        files_[file_id].add_line_directive(offset, next_physical_line, virtual_line,
                                           std::move(file_name));
    }
}

std::string_view SourceManager::spelling(SourceLocation location, std::uint32_t length) const {
    if (!location.valid()) {
        return {};
    }
    const auto& source = file(location.file_id);
    const auto offset = std::min(location.offset, source.size());
    const auto available = source.size() - offset;
    const auto count = std::min(length, available);
    return std::string_view(source.text()).substr(offset, count);
}

std::string_view SourceManager::line_text(SourceLocation location) const {
    if (!location.valid()) {
        return {};
    }
    return file(location.file_id).line_text(location.offset);
}

std::string_view SourceManager::file_name(std::uint32_t file_id) const {
    if (file_id >= files_.size()) {
        return {};
    }
    return files_[file_id].name();
}

std::string_view SourceManager::file_name(SourceLocation location) const {
    if (!location.valid()) {
        return {};
    }
    return file(location.file_id).display_name(location.offset);
}

void SourceManager::push_include(std::uint32_t file_id) {
    static_cast<void>(file(file_id));
    include_stack_.push_back(file_id);
}

void SourceManager::pop_include() noexcept {
    if (!include_stack_.empty()) {
        include_stack_.pop_back();
    }
}

}  // namespace krc::frontend
