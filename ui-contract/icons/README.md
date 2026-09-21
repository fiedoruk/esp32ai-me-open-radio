# Core2 icon subset

`tabler-core2.svg` contains the reviewed icons used by the pixel-perfect GUI
lab. They are copied from Tabler Icons `v3.44.0`, tag commit
`6d128ed935d4546607b1e4d5d08c8b27bdbe7758`, and remain under the MIT license
in `TABLER-LICENSE.txt`.

The subset keeps the original 24x24 viewBox, 2 px stroke, round caps and round
joins. The browser renders the SVG symbols directly. Firmware must convert the
same pinned paths into compile-time monochrome masks; it must not add a runtime
SVG parser or remote icon dependency.

Weather uses only the pinned `sun`, `cloud`, `cloud-rain` and `umbrella`
symbols. No weather icon package or remote SVG is loaded at runtime.
