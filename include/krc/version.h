#pragma once

#include <string_view>

#include "krc/version_stamp.h"

namespace krc {

inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 2;
inline constexpr int kVersionPatch = 0;
inline constexpr std::string_view kVersionString = "0.2.0";

// Build provenance. Two installed packages have sat side by side on this machine
// whose only difference was the configuration they were built with, and nothing
// in either could say which one it was. These come from the build tree, not the
// source, and answer "which compiler built that image".
inline constexpr std::string_view kBuildType = KRC_BUILD_TYPE;
inline constexpr std::string_view kBuildRevision = KRC_BUILD_REVISION;
inline constexpr std::string_view kBuildCompiler = KRC_BUILD_COMPILER;

}  // namespace krc
