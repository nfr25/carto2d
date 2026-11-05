#pragma once

#include "command/Registry.h"

namespace e2d {

// The commands on layers. Step 1c: layer.setVisible (notions §18).
// Step 3a: layer.add, layer.remove, layer.move (§37, §38). layer.add opens
// the source once with `probe` to refuse what cannot be read; `probe` must
// outlive the registry (it is the Commander's). Step 3b-1: layer.setStyle,
// the whole style as YSLD text (§41), checked by `styleCheck`.
void registerLayerCommands(Registry& registry, const LayerProbe& probe, const StyleCheck& styleCheck);

} // namespace e2d
