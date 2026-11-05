#pragma once

#include "command/Document.h"
#include "style/classify.hpp"   // render2d: FieldSummary (§51)

#include <optional>
#include <string>

namespace e2d {

// The probe of layer.add (notions §38), with GDAL: opens the source once in
// the main thread, reads its name, type, fields, extent, and how many other
// layers its file holds; then closes it. "file.gpkg::layer" picks a layer.
std::optional<ProbedLayer> probeSource(const std::string& source, std::string& error);

// §50: what a field of the source holds, read once over the whole layer in
// the main thread (as the probe). Nothing, with the reason, if it cannot open.
std::optional<r2d::FieldSummary> summarizeSource(const std::string& source, const std::string& field,
                                                 std::string& error);

} // namespace e2d
