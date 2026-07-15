#pragma once

#include "mastering/project/ProjectDocument.h"

#include <string>
#include <string_view>

namespace mastering::research {

struct ExportOptions {
    std::string selectedVariant {"balanced"};
    bool userApproved {true};
};

// Produces a metadata-only training example. Track names, file paths, audio,
// and raw project identifiers are deliberately excluded.
[[nodiscard]] std::string serializeExample(
    const project::ProjectDocument& project,
    const ExportOptions& options = {});

[[nodiscard]] bool isResearchExample(std::string_view source);

} // namespace mastering::research
