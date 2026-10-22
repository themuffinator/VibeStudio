#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace qmg {

[[nodiscard]] std::string sha256(std::string_view input);
[[nodiscard]] std::string sha256_file(const std::filesystem::path &path);

} // namespace qmg
