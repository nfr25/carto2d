#pragma once

#include "command/Document.h"
#include "style/classify.hpp"   // render2d: FieldSummary (§51)

#include <atomic>
#include <optional>
#include <string>

namespace e2d {

// The probe of layer.add (notions §38), with GDAL: opens the source once in
// the main thread, reads its name, type, fields, extent, and how many other
// layers its file holds; then closes it. "file.gpkg::layer" picks a layer.
std::optional<ProbedLayer> probeSource(const std::string& source, std::string& error);

// §50: what a field of the source holds, read once over the whole layer.
// Nothing, with the reason, if it cannot open. §82: called from a thread of
// its own (the Classify tab); `total` gets the layer's feature count as GDAL
// knows it (-1: unknown), `done` the features read so far, for a progress bar.
std::optional<r2d::FieldSummary> summarizeSource(const std::string& source, const std::string& field,
                                                 std::string& error, std::atomic<long long>* done = nullptr,
                                                 std::atomic<long long>* total = nullptr);

} // namespace e2d
