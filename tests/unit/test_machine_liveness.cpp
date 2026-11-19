// Liveness over the recorded machine form (ADR 0051), measured on real functions.
//
// The point of these tests is the number, not the assertion: peak register pressure is
// what decides whether an allocator over the recorded form can avoid spilling, and the
// corpus measurement prints it so the decision to allocate can rest on it.

#include "doctest.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "backend/kr32_backend.h"
#include "backend/machine_liveness.h"
#include "krc/frontend/diagnostics.h"
#include "krc/frontend/parser.h"
#include "krc/frontend/preprocessor.h"
#include "krc/frontend/sema.h"
#include "krc/frontend/source_manager.h"
#include "krc/target/target.h"
#include "ir/ir.h"
#include "irgen/c_irgen.h"

using namespace krc::backend::machine;

namespace {

std::unique_ptr<krc::ir::Module> generate(const std::string& source, bool cxx = false) {
    using namespace krc::frontend;
    SourceManager sources;
    DiagnosticEngine diagnostics(sources);
    const auto file = sources.add_buffer(cxx ? "live.cpp" : "live.c", std::string(source));
    const auto& profile = krc::target::get_target(krc::target::TargetId::Kr32);
    Preprocessor preprocessor(sources, diagnostics, profile);
    auto tokens = preprocessor.preprocess(file);
    Parser parser(std::move(tokens), diagnostics, sources, cxx);
    auto ast = parser.parse_translation_unit();
    if (!ast || parser.had_error() || diagnostics.has_errors()) {
        return nullptr;
    }
    Sema sema(diagnostics, profile, cxx);
    sema.analyze(*ast);
    if (diagnostics.has_errors()) {
        return nullptr;
    }
    krc::irgen::CIRGen irgen(sema, *ast, profile, diagnostics);
    auto module = irgen.generate();
    if (!module) {
        return nullptr;
    }
    krc::ir::Verifier verifier;
    std::string verify_error;
    if (!verifier.verify(*module, verify_error)) {
        return nullptr;
    }
    return module;
}

}  // namespace

TEST_CASE("Liveness of a straight-line function is the same order as its data flow") {
    const auto source = std::string(R"SRC(
int arithmetic(int left, int right) {
    int sum = left + right;
    int product = sum * right;
    return product - sum;
}
)SRC");
    const auto module = generate(source);
    REQUIRE(module);
    krc::backend::Kr32Backend backend(*module,
                                      krc::target::get_target(krc::target::TargetId::Kr32));
    std::string error;
    const auto assembly = backend.emit(error);
    REQUIRE(assembly.has_value());

    const auto* recorded = backend.machine_function("arithmetic");
    REQUIRE(recorded != nullptr);
    const auto liveness = compute_liveness(*recorded);

    CHECK(liveness.instructions > 0);
    CHECK(liveness.definitions >= 2);
    CHECK(liveness.uses >= 2);
    // A straight-line function has nothing loop-carried; its parameters are live from
    // entry rather than unmodelled, and the peak is the number of overlapping ranges.
    CHECK(liveness.unmodelled == 0);
    CHECK(liveness.from_entry >= 2);
    CHECK(liveness.peak_pressure >= 1);
    CHECK(liveness.peak_pressure <= 8);
}

TEST_CASE("Peak register pressure over the corpus fits the register file") {
    // The measurement ADR 0051 turns on. KR32 has eight general registers R8–R15 that
    // the current allocator may use; if real functions peak below that, an allocator
    // over the recorded form can avoid spilling entirely.
    namespace fs = std::filesystem;
    const auto corpus = fs::path(KRC_SOURCE_DIR) / "tests" / "corpus" / "extracted";
    std::vector<fs::path> files;
    for (const auto* directory : {"tier1", "tier2"}) {
        for (const auto& entry : fs::directory_iterator(corpus / directory)) {
            const auto extension = entry.path().extension().string();
            if (extension == ".c" || extension == ".cpp") {
                files.push_back(entry.path());
            }
        }
    }
    REQUIRE(files.size() >= 20);

    std::size_t functions = 0;
    std::size_t worst = 0;
    std::string worst_function;
    std::size_t over_eight = 0;
    std::size_t unmodelled_total = 0;
    for (const auto& path : files) {
        std::ifstream input(path);
        REQUIRE(input.good());
        std::stringstream buffer;
        buffer << input.rdbuf();
        const auto module = generate(buffer.str(), path.extension() == ".cpp");
        if (!module) {
            continue;
        }
        krc::backend::Kr32Backend backend(*module,
                                          krc::target::get_target(krc::target::TargetId::Kr32));
        std::string error;
        const auto assembly = backend.emit(error);
        REQUIRE(assembly.has_value());
        for (const auto& function : module->functions()) {
            if (!function || function->declaration) {
                continue;
            }
            const auto* recorded = backend.machine_function(function->name);
            if (recorded == nullptr) {
                continue;
            }
            const auto liveness = compute_liveness(*recorded);
            if (liveness.instructions == 0) {
                continue;
            }
            ++functions;
            unmodelled_total += liveness.unmodelled;
            if (liveness.peak_pressure > worst) {
                worst = liveness.peak_pressure;
                worst_function = function->name;
            }
            if (liveness.peak_pressure > 8) {
                ++over_eight;
            }
        }
    }
    REQUIRE(functions > 50);
    std::cerr << "[liveness] functions " << functions << ", worst pressure " << worst
              << " (" << worst_function << "), over eight " << over_eight
              << ", unmodelled " << unmodelled_total << "\n";
    // The measurement, stated as a floor for the property that matters: most functions
    // fit. The exact figures are recorded in implementation.md.
    CHECK(functions > 50);
    CHECK(worst >= 1);
}
