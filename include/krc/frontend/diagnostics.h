#pragma once

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "krc/frontend/source_manager.h"

namespace krc::frontend {

enum class Severity {
    Note,
    Warning,
    Error,
    Fatal,
};

[[nodiscard]] std::string_view severity_name(Severity severity) noexcept;

struct Diagnostic {
    Severity severity = Severity::Error;
    SourceLocation location;
    std::string message;
    std::vector<std::string> notes;
};

class DiagnosticEngine {
public:
    explicit DiagnosticEngine(const SourceManager& sources);

    void report(Severity severity, SourceLocation location, std::string message);
    void report(Severity severity, std::string message);
    void note(SourceLocation location, std::string message);

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] std::size_t error_count() const noexcept { return error_count_; }
    [[nodiscard]] std::size_t warning_count() const noexcept { return warning_count_; }
    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept {
        return diagnostics_;
    }

    void set_error_limit(std::size_t limit) noexcept { error_limit_ = limit; }
    void sort();
    void clear() noexcept;
    void print(std::ostream& out) const;

    [[nodiscard]] const SourceManager& source_manager() const noexcept { return sources_; }

private:
    void append(Diagnostic diagnostic);

    const SourceManager& sources_;
    std::vector<Diagnostic> diagnostics_;
    std::size_t error_count_ = 0;
    std::size_t warning_count_ = 0;
    std::size_t error_limit_ = 20;
    bool limit_reported_ = false;
};

}  // namespace krc::frontend
