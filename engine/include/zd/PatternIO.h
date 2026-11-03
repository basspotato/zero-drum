#pragma once

#include "zd/Sequencer.h"

#include <string>
#include <string_view>

namespace zd {

/// Plain-text, versioned pattern format. Human readable and diffable; only active steps are
/// written. Used for plugin state and, later, inside .zdloop bundles. Allocates.
///
///   zdpattern 1
///   grid 0.25 swing 0.54
///   lane 0 32
///   step 0 1 1 0 -1 1      (index velocity probability microOffset note ratchet)
std::string serializePattern(const Pattern& pattern);

/// Parses text written by serializePattern. On failure returns false and leaves `out` as it was.
bool deserializePattern(std::string_view text, Pattern& out);

} // namespace zd
