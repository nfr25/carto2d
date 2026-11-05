#include "worker/LayerProbe.h"

#include "backend/gdal/gdal_layer.hpp"
#include "backend/gdal/gdal_raster.hpp"

namespace e2d {

namespace {

// "file.gpkg::layer" -> the file and the layer ("" = the first).
std::unique_ptr<r2d::GdalLayer> openSource(const std::string& source, std::string& error)
{
    const auto [file, layerName] = r2d::splitSource(source);   // "PG:...::::t" repaired too
    return r2d::GdalLayer::open(file, layerName, error);
}

} // namespace

std::optional<r2d::FieldSummary> summarizeSource(const std::string& source, const std::string& field,
                                                 std::string& error)
{
    const auto layer = openSource(source, error);
    if (!layer)
        return std::nullopt;
    r2d::FieldSummary s = r2d::summarizeField(*layer, field);
    if (!s.found) {
        error = "no field '" + field + "' in " + source;
        return std::nullopt;
    }
    return s;
}

// §66: a raster has a name, an extent, and nothing else to describe.
std::optional<ProbedLayer> probeRaster(const std::string& source, std::string& error)
{
    std::string rasterError;
    const auto raster = r2d::GdalRaster::open(source, rasterError);
    if (!raster) {
        // Neither vector nor raster: the message that says the most, as
        // render2d's Map (a TIFF without position is a raster with a problem).
        if (rasterError.rfind("cannot open", 0) != 0)
            error = rasterError;
        return std::nullopt;
    }
    const r2d::LayerInfo& info = raster->info();
    ProbedLayer p;
    p.name     = info.name;
    p.geometry = "raster";
    p.web      = info.web;   // §76: render2d's §73
    if (info.crs.known)
        p.crs = info.crs.wkt;   // §69
    if (info.extent)
        p.extent = Extent{info.extent->xmin, info.extent->ymin, info.extent->xmax, info.extent->ymax};
    return p;
}

std::optional<ProbedLayer> probeSource(const std::string& source, std::string& error)
{
    const auto layer = openSource(source, error);
    if (!layer)   // not a vector layer: perhaps a raster ("file::layer" is a vector's)
        return source.rfind("::") == std::string::npos ? probeRaster(source, error) : std::nullopt;

    const r2d::LayerInfo& info = layer->info();
    ProbedLayer p;
    p.name     = info.name;
    p.geometry = !info.type ? "" : *info.type == r2d::GeomType::Point ? "point"
               : *info.type == r2d::GeomType::Line ? "line" : "polygon";
    for (const r2d::FieldDef& f : layer->fields()) {
        p.fields.push_back(f.name);
        p.numeric.push_back(f.type != r2d::FieldType::Text);
    }
    if (info.extent)
        p.extent = Extent{info.extent->xmin, info.extent->ymin, info.extent->xmax, info.extent->ymax};
    if (info.crs.known)
        p.crs = info.crs.wkt;   // §69
    const bool named = source.rfind("::") != std::string::npos;   // "file.gpkg::layer": a chosen one
    p.otherLayers    = named ? 0 : info.layersInSource - 1;
    return p;
}

} // namespace e2d
