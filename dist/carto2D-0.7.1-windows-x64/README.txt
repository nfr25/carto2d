carto2D 0.7.1 for Windows (64-bit)
=====================================

See, query, measure and print maps. https://github.com/nfr25/carto2d
With render2d, the map renderer it is built on: https://github.com/nfr25/render2d

carto2d.exe     the program; it opens carto2d.yaml, the demonstration
                (Brussels on OpenStreetMap: an Internet connection is needed)
render2d.exe    the same maps from a command prompt: render2d --help
symbols\        the symbol library; your own symbols go in symbols\user\
styles\         example styles; basemaps\  ready web basemaps (drop one on the map)
docs\           the design notes (Help > carto2D notes)

Everything both programs need is in this folder: the DLLs, and GDAL's and
PROJ's reference files in share\. Nothing else to install.

Licence: GNU AGPL v3 or later (LICENSE.txt). The libraries included keep
their own licences (licenses\, PACKAGES.txt lists them with their versions).
Map data in the demonstration: (c) OpenStreetMap contributors (ODbL).
