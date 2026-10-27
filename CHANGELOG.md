# Changes

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