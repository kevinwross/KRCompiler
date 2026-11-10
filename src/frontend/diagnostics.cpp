#include "krc/frontend/diagnostics.h"

#include <algorithm>
#include <utility>

namespace krc::frontend {

namespace {

int severity_rank(Severity severity) {
    switch (severity) {
    case Severity::Fatal:
        return 0;
    case Severity::Error:
        return 1;
    case Severity::Warning:
        return 2;
    case Severity::Note:
        return 3;
    }
    return 4;
}

}  // namespace

std::string_view severity_name(Severity severity) noexcept {
    switch (severity) {
    case Severity::Note:
        return "note";
    case Severity::Warning:
        return "warning";
    case Severity::Error:
        return "error";
    case Severity::Fatal:
        return "fatal error";
    }
    return "diagnostic";
}

DiagnosticEngine::DiagnosticEngine(const SourceManager& sources) : sources_(sources) {}

void DiagnosticEngine::report(Severity severity, SourceLocation location, std::string message) {
    append(Diagnostic{severity, location, std::move(message), {}});
}

void DiagnosticEngine::report(Severity severity, std::string message) {
    append(Diagnostic{severity, SourceLocation{}, std::move(message), {}});
}

void DiagnosticEngine::note(SourceLocation location, std::string message) {
    if (diagnostics_.empty()) {
        report(Severity::Note, location, std::move(message));
        return;
    }
    diagnostics_.back().notes.push_back(std::move(message));
}

void DiagnosticEngine::append(Diagnostic diagnostic) {
    const bool is_error = diagnostic.severity == Severity::Error ||
                          diagnostic.severity == Severity::Fatal;

    if (diagnostic.severity == Severity::Fatal) {
        ++error_count_;
        diagnostics_.push_back(std::move(diagnostic));
        return;
    }

    if (is_error && error_limit_ != 0 && error_count_ >= error_limit_) {
        if (!limit_reported_) {
            limit_reported_ = true;
            ++error_count_;
            diagnostics_.push_back(
                Diagnostic{Severity::Fatal, SourceLocation{},
                           "too many errors emitted; stopping now", {}});
        }
        return;
    }

    if (is_error) {
        ++error_count_;
    } else if (diagnostic.severity == Severity::Warning) {
        ++warning_count_;
    }
    diagnostics_.push_back(std::move(diagnostic));
}

bool DiagnosticEngine::has_errors() const noexcept {
    return std::any_of(diagnostics_.begin(), diagnostics_.end(), [](const Diagnostic& diagnostic) {
        return diagnostic.severity == Severity::Error || diagnostic.severity == Severity::Fatal;
    });
}

void DiagnosticEngine::sort() {
    std::stable_sort(diagnostics_.begin(), diagnostics_.end(),
                     [](const Diagnostic& left, const Diagnostic& right) {
                         const auto left_file = left.location.valid()
                                                    ? left.location.file_id
                                                    : SourceLocation::kInvalidFile;
                         const auto right_file = right.location.valid()
                                                     ? right.location.file_id
                                                     : SourceLocation::kInvalidFile;
                         if (left_file != right_file) {
                             return left_file < right_file;
                         }
                         if (left.location.offset != right.location.offset) {
                             return left.location.offset < right.location.offset;
                         }
                         return severity_rank(left.severity) < severity_rank(right.severity);
                     });
}

void DiagnosticEngine::clear() noexcept {
    diagnostics_.clear();
    error_count_ = 0;
    warning_count_ = 0;
    limit_reported_ = false;
}

void DiagnosticEngine::print(std::ostream& out) const {
    for (const auto& diagnostic : diagnostics_) {
        if (diagnostic.location.valid()) {
            const auto line_column = sources_.line_column(diagnostic.location);
            out << sources_.file_name(diagnostic.location) << ':' << line_column.line << ':'
                << line_column.column << ": ";
        }
        out << severity_name(diagnostic.severity) << ": " << diagnostic.message << '\n';

        if (diagnostic.location.valid()) {
            const auto line = sources_.line_text(diagnostic.location);
            const auto position = sources_.line_column(diagnostic.location);
            if (!line.empty() && position.column > 0) {
                out << "  " << line << '\n' << "  ";
                const auto prefix_length = std::min<std::size_t>(
                    position.column - 1, line.size());
                for (std::size_t i = 0; i < prefix_length; ++i) {
                    out << (line[i] == '\t' ? '\t' : ' ');
                }
                out << "^\n";
            }
        }

        for (const auto& note : diagnostic.notes) {
            out << "  note: " << note << '\n';
        }
    }
}

}  // namespace krc::frontend
