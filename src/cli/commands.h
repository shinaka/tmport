#pragma once
// tmport command implementations (milestone 1 tools).

#include <filesystem>

#include "cli/options.h"

namespace tmport::cli {

// Entry point used by main(): parses argv, runs the command, returns the process exit code.
int run(int argc, char** argv);

int runInfo(const Options& o, const std::filesystem::path& dataPath);
int runDumpPmap(const Options& o, const std::filesystem::path& dataPath);
int runDumpSheet(const Options& o, const std::filesystem::path& dataPath);
int runHeadlessFrame(const Options& o, const std::filesystem::path& dataPath);
int runView(const Options& o, const std::filesystem::path& dataPath);

}  // namespace tmport::cli
