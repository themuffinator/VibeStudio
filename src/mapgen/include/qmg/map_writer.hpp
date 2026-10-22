#pragma once

#include "qmg/layout.hpp"
#include "qmg/map_document_writer.hpp"

#include <filesystem>
#include <string>

namespace qmg {

class MapWriter {
  public:
    [[nodiscard]] std::string render(const Layout &layout) const;
    [[nodiscard]] std::string render(const Layout &layout, MapDialect dialect) const;
    [[nodiscard]] std::string render(const MapDocument &document, const MapWriteOptions &options = {}) const;

    void write(const Layout &layout, const std::filesystem::path &path) const;
    void write(const Layout &layout, const std::filesystem::path &path, MapDialect dialect) const;
    void write(const MapDocument &document, const std::filesystem::path &path,
               const MapWriteOptions &options = {}) const;
};

} // namespace qmg
