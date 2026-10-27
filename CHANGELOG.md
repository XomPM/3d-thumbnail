# Changes

## 1.0.3

- Settings window in Korean or English, switchable from the top-right list; the choice is saved. Defaults to the Windows display language.
- Setup asks for Korean or English, and creates Start menu shortcuts and the user guide link in that language.
- Added an English README (`README.en.md`).

## 1.0.2

- 3MF: show the creator's stored package thumbnail first; render the model only when none is usable.
- Redesigned settings window: aligned fields and buttons, color swatches beside HEX values, palette-matched slider, Explorer-style preview tile.

## 1.0.1

- Render Bambu Studio and other slicer 3MF files that carry vendor metadata/attributes (lib3mf non-strict reading).
- Fall back to the package's stored preview when direct 3MF rendering fails; fix swapped red/blue in RGB previews.
- Show the failing file name and error line directly in the settings status after a refresh failure.

## 1.0.0

First public release, replacing the earlier preview downloads.

- Support STL, 3MF, STEP/STP, OBJ, GLB/glTF, PLY, IGES/IGS and FBX thumbnails in Windows Explorer.
- Configure background/model colors and background transparency from 0 to 100 percent, with live checkerboard preview.
- Preserve opaque model shading and correctly blend transparent edges in PNG and Windows Shell images.
- Retain previous color settings on upgrade and refresh selected files or folders through the Windows thumbnail cache.
- Provide the DMIND app icon, discoverable Start menu name, user installer, updates and original-handler recovery.
- Publish MIT-licensed project sources and dependency notices/source archives. The installer is unsigned; additional PC compatibility testing remains.