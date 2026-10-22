#include "qmg/map_writer.hpp"

#include "qmg/map_builder.hpp"
#include "qmg/validator.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace qmg {
namespace {

[[nodiscard]] MapDialect default_dialect(const Game game) {
    switch (game) {
    case Game::quake1:
        return MapDialect::quake1_valve220;
    case Game::quake2:
        return MapDialect::quake2_valve220;
    case Game::quake3:
        return MapDialect::quake3_legacy;
    }
    throw std::invalid_argument("unsupported game");
}

[[nodiscard]] MapBuildResult build_document(const Layout &layout, const MapDialect dialect) {
    const ValidationResult validation = LayoutValidator{}.validate(layout);
    if (!validation.ok()) {
        const auto first_error = std::find_if(validation.issues.begin(), validation.issues.end(),
                                              [](const Issue &issue) { return issue.severity == Severity::error; });
        throw std::invalid_argument("cannot serialize invalid level: " + first_error->code + ": " +
                                    first_error->message);
    }

    MapBuildOptions options;
    options.dialect = dialect;
    try {
        return MapBuilder{}.build(layout, options);
    } catch (const MapBuildError &error) {
        throw std::invalid_argument("cannot serialize level: " + std::string(error.what()));
    }
}

void require_safe_output_leaf(const std::filesystem::path &path) {
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);
    if (error && error != std::errc::no_such_file_or_directory) {
        throw std::runtime_error("cannot inspect map output: " + path.string());
    }
    if (!error && std::filesystem::is_symlink(status)) {
        throw std::runtime_error("refusing to replace a symbolic-link map output: " + path.string());
    }
    if (!error && std::filesystem::exists(status) && !std::filesystem::is_regular_file(status)) {
        throw std::runtime_error("map output exists and is not a regular file: " + path.string());
    }
}

[[nodiscard]] std::filesystem::path temporary_sibling(const std::filesystem::path &path) {
    static std::atomic<std::uint64_t> sequence{};
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory = path.has_parent_path() ? path.parent_path() : std::filesystem::path{"."};

    for (std::uint64_t attempt = 0; attempt < 128; ++attempt) {
        std::filesystem::path candidate = directory / path.filename();
        candidate += ".qmg-tmp-" + std::to_string(tick) + "-" +
                     std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) + "-" + std::to_string(attempt);
        std::error_code error;
        if (!std::filesystem::exists(candidate, error) && !error) {
            return candidate;
        }
    }
    throw std::runtime_error("cannot allocate a temporary map output next to: " + path.string());
}

void replace_file(const std::filesystem::path &source, const std::filesystem::path &destination) {
#ifdef _WIN32
    if (::MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        throw std::runtime_error("cannot replace map output: " + destination.string());
    }
#else
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    if (error) {
        throw std::runtime_error("cannot replace map output: " + destination.string());
    }
#endif
}

void write_atomically(const std::filesystem::path &path, const std::string &contents) {
    if (path.empty() || !path.has_filename()) {
        throw std::invalid_argument("map output path must name a file");
    }
    require_safe_output_leaf(path);
    if (path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            throw std::runtime_error("cannot create map output directory: " + path.parent_path().string());
        }
    }

    const std::filesystem::path temporary = temporary_sibling(path);
    try {
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) {
                throw std::runtime_error("cannot open temporary map output: " + temporary.string());
            }
            output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            output.flush();
            if (!output) {
                throw std::runtime_error("failed while writing temporary map output: " + temporary.string());
            }
        }
        require_safe_output_leaf(path);
        replace_file(temporary, path);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

} // namespace

std::string MapWriter::render(const Layout &layout) const { return render(layout, default_dialect(layout.spec.game)); }

std::string MapWriter::render(const Layout &layout, const MapDialect dialect) const {
    return render(build_document(layout, dialect).document);
}

std::string MapWriter::render(const MapDocument &document, const MapWriteOptions &options) const {
    return MapDocumentWriter{}.write(document, options);
}

void MapWriter::write(const Layout &layout, const std::filesystem::path &path) const {
    write(layout, path, default_dialect(layout.spec.game));
}

void MapWriter::write(const Layout &layout, const std::filesystem::path &path, const MapDialect dialect) const {
    write_atomically(path, render(layout, dialect));
}

void MapWriter::write(const MapDocument &document, const std::filesystem::path &path,
                      const MapWriteOptions &options) const {
    write_atomically(path, render(document, options));
}

} // namespace qmg
