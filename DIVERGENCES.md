# Divergences from editor3d

`src/window/Window.*`, `src/window/ImGuiLayer.*` and `src/command/History.*`
(with `Command.h`) are copied from editor3d (`app/Window.*`, `ui/ImGuiLayer.*`,
`scene/History.*`). Every change is listed here, so that it
can be carried back to editor3d, or so we know why we differ.

| File | Change | Why | Back to editor3d? |
|---|---|---|---|
| both | namespace `e2d` instead of `app` / `ui`; folder `window/` | render2d also has an `app/` folder in its include path (notions §1) | no |
| Window | `GLFW_SCALE_TO_MONITOR` hint | 1600 x 900 becomes 2400 x 1350 on a 4K screen at 150 %, like other applications | yes |
| Window | `contentScale()` | the pixel ratio for render2d and the scale of ImGui (notions §7) | yes, with the next item |
| Window | `cancelClose()` removed | nothing to save yet | no |
| Window | `takeDrops()`: the files dropped on the window (`glfwSetDropCallback`) | adding layers by drag and drop (notions §39) | yes, to open models by dropping them |
| Window | `MouseState::middlePressed`: the middle button's press as an edge, like the left one | the middle button pans with every tool (notions §27) | yes, if editor3d wants a quick middle click |
| ImGuiLayer | `init(window, scale)`: `ScaleAllSizes(scale)` and `FontScaleDpi = scale` | readable panels at 150 % (notions §7) | yes, if editor3d's panels look small |
| History, Command | `apply`/`revert` take a `CommandContext` (document + event bus) instead of the `Scene` | commands publish their events (notions §17) | no |
| Command | `toJson()`: every command can be written as JSON | the journal and the API (notions §16) | yes, when editor3d gets its server |
| History | no `record()`, no "dirty" serial | nothing changes live over frames, nothing is saved yet | no |

To do later (step 1b): react when the window moves to a screen with another
scale (`glfwSetWindowContentScaleCallback`).
