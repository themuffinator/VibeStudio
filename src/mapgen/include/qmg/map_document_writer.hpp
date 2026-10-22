#pragma once

#include "qmg/map_document.hpp"

#include <string>

namespace qmg {

enum class MapWriteMode {
    canonical,
    preserve_pristine_source,
};

struct MapWriteOptions {
    MapWriteMode mode{MapWriteMode::canonical};
    bool validate_document{false};
    MapValidationOptions validation_options{};
};

class MapDocumentWriter {
  public:
    [[nodiscard]] std::string write(const MapDocument &document, const MapWriteOptions &options = {}) const;
};

} // namespace qmg
