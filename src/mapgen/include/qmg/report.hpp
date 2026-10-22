#pragma once

#include "qmg/pipeline.hpp"

#include <filesystem>
#include <string>

namespace qmg {

[[nodiscard]] std::string render_report(const PipelineResult &result);
void write_report(const PipelineResult &result, const std::filesystem::path &path);

} // namespace qmg
