#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "krc/frontend/diagnostics.h"
#include "krc/frontend/lexer.h"
#include "krc/frontend/source_manager.h"
#include "krc/target/target.h"

namespace krc::frontend {

class Preprocessor {
public:
    struct Options {
        std::vector<std::filesystem::path> include_paths;
    };

    Preprocessor(SourceManager& sources, DiagnosticEngine& diagnostics,
                 const target::TargetProfile& profile, Options options = {});

    std::optional<std::vector<Token>> preprocess_file(const std::filesystem::path& path);
    std::vector<Token> preprocess(std::uint32_t file_id);

    [[nodiscard]] bool is_defined(std::string_view name) const;

private:
    struct Macro {
        std::string name;
        bool function_like = false;
        bool variadic = false;
        std::vector<std::string> parameters;
        std::vector<Token> replacement;
        SourceLocation location;
    };

    struct Conditional {
        bool parent_active = true;
        bool active = true;
        bool taken = false;
        bool else_seen = false;
    };

    void seed_predefined();
    void define_object(std::string name, std::string replacement);

    [[nodiscard]] bool is_active() const;
    [[nodiscard]] int line_of(const Token& token) const;
    [[nodiscard]] Token make_token(TokenKind kind, std::string text,
                                   SourceLocation location) const;

    std::vector<Token> expand(const std::vector<Token>& input,
                              std::unordered_set<std::string> disabled, int depth);
    std::vector<Token> substitute(const Macro& macro,
                                  const std::vector<std::vector<Token>>& arguments,
                                  const Token& invocation,
                                  const std::unordered_set<std::string>& disabled);
    bool parse_macro_arguments(const std::vector<Token>& input, std::size_t open_index,
                               std::vector<std::vector<Token>>& arguments,
                               std::size_t& close_index) const;

    void handle_directive(const std::vector<Token>& directive, std::uint32_t file_id,
                          std::vector<Token>& output);
    void handle_define(const std::vector<Token>& directive);
    void handle_undef(const std::vector<Token>& directive);
    void handle_include(const std::vector<Token>& directive, std::uint32_t file_id,
                        std::vector<Token>& output);
    void handle_if(const std::vector<Token>& directive, bool negate);
    void handle_if_expression(const std::vector<Token>& directive);
    void handle_elif(const std::vector<Token>& directive);
    void handle_else();
    void handle_endif();
    void handle_line_directive(const std::vector<Token>& directive, std::uint32_t file_id);
    void handle_error(const std::vector<Token>& directive, Severity severity);

    [[nodiscard]] std::optional<std::filesystem::path> resolve_include(
        const std::string& name, bool quoted, std::uint32_t including_file) const;
    [[nodiscard]] std::string decode_string_literal(const Token& token) const;
    [[nodiscard]] long long evaluate_if(const std::vector<Token>& tokens,
                                        const Token& directive_token);
    [[nodiscard]] std::string stringize(const std::vector<Token>& tokens) const;

    SourceManager& sources_;
    DiagnosticEngine& diagnostics_;
    const target::TargetProfile& profile_;
    Options options_;
    std::unordered_map<std::string, Macro> macros_;
    std::unordered_set<std::string> once_files_;
    std::vector<Conditional> conditionals_;
    std::uint32_t counter_ = 0;
};

}  // namespace krc::frontend
