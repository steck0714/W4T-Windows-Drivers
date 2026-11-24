# Switchroot tray artwork

`nvpmodel-switch.svg` is the original blue/red Joy-Con-shaped artwork from
Switchroot Noble's `switch-bsp` package, reproduced without changing its paths
or colors at the user's request. `source.json` pins the package URL/version and
SHA256, original path and both asset hashes. The SVG carries no separate license
or author notice; its original content is retained and no new license is asserted
for the artwork. Nintendo Switch/Joy-Con marks remain their owners' property.

The ICO is a format conversion for Windows, containing16/20/24/32/40/48/64/256px
sizes with alpha. To regenerate it, use CairoSVG2.8.2 to render the SVG at256px,
then Pillow12.0.0 RGBA ICO output at those sizes. These are build-time tools only.
Resource101 is embedded in both ARM64 and x64 executables. Tool mode and charge
state remain in the menu and tooltip; the original artwork is not recolored.
