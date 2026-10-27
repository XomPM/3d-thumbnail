# Changes

## 0.5.1 preview

- Name the Start menu app "3D Thumbnail 설정" so Windows search can identify the product.
- Apply the supplied DMIND logo to the executable, window, settings header, installer and shortcuts.
- Include friendly product/version metadata and migrate the old "색상 설정" shortcut during updates.

## 0.5.0 preview

- Add per-user background/model color settings, HEX input, color picker, preview and default restoration.
- Refine the settings layout with a live model preview, clear color controls and DMIND creator details.
- Apply saved colors to the worker and allow per-invocation CLI color overrides.
- Refresh selected files or a single folder through the Windows thumbnail cache, with cancellation between files.
- Add a per-user Windows installer and transactional runtime updates that retain the original handler backup.
- Rebuild OCCT without external video/VR/font/allocator libraries; include required release VC++ runtimes.
- Publish MIT-licensed project sources and preserve dependency notices.

## 0.4.0

- Add GLB/glTF, PLY, IGES/IGS and FBX to STL, 3MF, STEP/STP and OBJ.
- Support bounded local external glTF buffers and correct scene placement/orientation.

## 0.3.0

- Add the Explorer bridge, worker limits, registration recovery and file-specific cache refresh.
- Use a dark gray background and light gray model with surface shading.
