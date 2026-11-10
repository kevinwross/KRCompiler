#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace krc::frontend {

struct SourceLocation {
    static constexpr std::uint32_t kInvalidFile = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t file_id = kInvalidFile;
    std::uint32_t offset = 0;

    [[nodiscard]] bool valid() const noexcept { return file_id != kInvalidFile; }
    friend bool operator==(const SourceLocation&, const SourceLocation&) = default;
};

struct LineColumn {
    std::uint32_t line = 1;
    std::uint32_t column = 1;

    friend bool operator==(const LineColumn&, const LineColumn&) = default;
};

struct LineDirective {
    std::uint32_t offset = 0;
    std::uint32_t next_physical_line = 1;
    std::uint32_t virtual_line = 1;
    std::string file_name;
};

class SourceFile {
public:
    SourceFile(std::uint32_t id, std::string name, std::string text);

    [[nodiscard]] std::uint32_t id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] std::uint32_t size() const noexcept {
        return static_cast<std::uint32_t>(text_.size());
    }

    [[nodiscard]] LineColumn line_column(std::uint32_t offset) const;
    [[nodiscard]] std::uint32_t physical_line(std::uint32_t offset) const;
    [[nodiscard]] std::string_view line_text(std::uint32_t offset) const;
    [[nodiscard]] std::string_view display_name(std::uint32_t offset) const;
    void add_line_directive(std::uint32_t offset, std::uint32_t next_physical_line,
                            std::uint32_t virtual_line, std::string file_name = {});

private:
    std::uint32_t id_;
    std::string name_;
    std::string text_;
    std::vector<std::uint32_t> line_starts_;
    std::vector<LineDirective> line_directives_;
};

class SourceManager {
public:
    std::uint32_t add_buffer(std::string name, std::string text);
    std::optional<std::uint32_t> load_file(const std::filesystem::path& path, std::string& error);

    [[nodiscard]] const SourceFile& file(std::uint32_t id) const;
    [[nodiscard]] std::size_t file_count() const noexcept { return files_.size(); }

    [[nodiscard]] SourceLocation location(std::uint32_t file_id, std::uint32_t offset) const;
    [[nodiscard]] LineColumn line_column(SourceLocation location) const;
    [[nodiscard]] std::uint32_t physical_line(SourceLocation location) const;
    [[nodiscard]] std::string_view spelling(SourceLocation location, std::uint32_t length) const;
    [[nodiscard]] std::string_view line_text(SourceLocation location) const;
    [[nodiscard]] std::string_view file_name(std::uint32_t file_id) const;
    [[nodiscard]] std::string_view file_name(SourceLocation location) const;

    void add_line_directive(std::uint32_t file_id, std::uint32_t offset,
                            std::uint32_t next_physical_line, std::uint32_t virtual_line,
                            std::string file_name = {});

    void push_include(std::uint32_t file_id);
    void pop_include() noexcept;
    [[nodiscard]] std::span<const std::uint32_t> include_stack() const noexcept {
        return include_stack_;
    }
    [[nodiscard]] std::uint32_t current_file() const noexcept {
        return include_stack_.empty() ? SourceLocation::kInvalidFile : include_stack_.back();
    }

private:
    std::vector<SourceFile> files_;
    std::vector<std::uint32_t> include_stack_;
};

}  // namespace krc::frontend
