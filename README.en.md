# 3D Thumbnail

**English** · [한국어](README.md)

A Windows x64 program that shows the shape of 3D models as thumbnails in Windows Explorer. **Current version: 1.0.3.** It supports background and model colors, background transparency, a Korean/English settings window, and per-user install and uninstall.

Made by **DMIND** / **dmind114@gmail.com** · GitHub: [XomPM](https://github.com/XomPM)

## Supported formats

| Format | Extensions | How it is handled |
| --- | --- | --- |
| STL | `.stl` | ASCII / binary mesh |
| 3MF | `.3mf` | Reads models, components and build placement. If the file contains its creator's thumbnail, that image is shown first; otherwise the model is rendered (slicer extensions such as Bambu Studio's are accepted) |
| STEP | `.step`, `.stp` | CAD surfaces converted to triangles |
| OBJ | `.obj` | Mesh |
| glTF | `.glb`, `.gltf` | Embedded meshes, and external glTF buffers in the same folder or below |
| PLY | `.ply` | ASCII / binary meshes with faces |
| IGES | `.iges`, `.igs` | Surfaces / solids |
| FBX | `.fbx` | ASCII / binary mesh |

Rendering uses Direct3D WARP, so no modeling software or dedicated GPU is needed. Your original files and default apps for opening files are not changed. Instead of textures and original materials, models are drawn in the color you choose with 3D shading.

## Install and use

1. Download `3DThumbnail-…-x64-Setup.exe` from [Releases](https://github.com/XomPM/3d-thumbnail/releases) and run it. The first screen lets you choose the setup language (한국어/English).
2. In Explorer, choose the Large icons or Extra large icons view. If Explorer's "Always show icons, never thumbnails" option is on, turn it off.
3. Search Windows for **3D Thumbnail** and open **3D Thumbnail Settings** (DMIND logo), or use Start → **3D Thumbnail → 3D Thumbnail Settings**. Pick colors with the swatches or type `#RRGGBB`, set **Transparency** with the slider or a number, and select **Save**. Use the list at the top right to switch the window between 한국어 and English; your choice is remembered.
4. Under **Refresh existing thumbnails**, select **Choose file…** or **Choose folder…** to apply new colors to thumbnails already shown. A folder refresh handles up to 1,000 supported files directly in that folder and skips subfolders. **Stop** takes effect after the current file.

**0% is opaque, 50% is semi-transparent, 100% is fully transparent.** The model always stays opaque. The checkerboard in the preview marks transparent areas and is not part of the saved image. Transparent backgrounds blend with Explorer's light or dark theme.

Defaults are a dark gray background `#2B2B2B`, a light gray model `#D6D6D6`, and **0%** transparency. Settings are stored for the current Windows user and also apply to PNG output.

The program installs for the current user only, in `%LOCALAPPDATA%\Programs\3DThumbnail`. Running a newer Setup adds that version's files and updates the registration. Uninstall from Windows Settings → Apps → Installed apps, or with **Uninstall** in the Start menu. The original thumbnail-handler registration is backed up in `%LOCALAPPDATA%\3DThumbnail\registration.json`; values changed by other apps are not overwritten on uninstall. Settings and recovery records are kept after uninstall. A DLL still in use by Explorer may be left behind during file cleanup.

## PNG output and diagnostics

Run from the build folder or the installed `versions/<build-id>` folder. An existing output PNG is never overwritten.

```powershell
.\thumbnail-cli.exe model.step preview.png --size 512
.\thumbnail-cli.exe model.stl colored.png --background '#14283C' --model '#FF8000'
.\thumbnail-cli.exe model.step transparent.png --background-transparency 100
.\shell-probe.exe --refresh 'C:\models\model.step'
.\shell-probe.exe --diagnose .step
```

CLI color options apply to that run only and do not change the saved settings. `shell-probe --direct` calls the handler directly; `--shell` checks the real Windows thumbnail extraction.

## Limits and verification

- Developed and verified on Windows 11 x64. Windows 10, ARM64, NAS, OneDrive and other PCs have not been verified yet.
- Limits: 64 MiB input, 250,000 triangles, 15 seconds per worker, 1 GiB committed memory. Large or complex models may fail to produce a thumbnail.
- Point clouds and line-only geometry, Draco/meshopt compression, external GLB buffers, and animation/skinning results are not supported or not verified.
- glTF external buffers are read only from their declared relative paths. Changing only the `.bin` does not refresh the Windows cache automatically; refresh it yourself.
- A small bridge DLL runs inside Explorer; parsing and rendering happen in a separate worker process. The Job Object limits time and memory; it is not a security sandbox for file or network access.
- The installer is not code-signed. Installation on other clean Windows PCs still needs testing.

CTest covers color settings, rendering, formats, the worker, and the COM handler. Registration tests check recovery, preservation of external changes, and interrupted updates in a separate GUID registry. Real Windows install and display checks are done separately. See the [Explorer integration notes](docs/shell-integration.md) (Korean) for the design.

## Building from source

Requires Visual Studio 2022 with **Desktop development with C++**, the Windows SDK, the CMake tools, and PowerShell. Run from the repository root:

```powershell
.\scripts\setup-dependencies.ps1
.\scripts\build-occt.ps1
.\scripts\build.ps1
.\tests\registration_tests.ps1
.\scripts\package.ps1
```

The first OCCT build takes a while. It builds the selected CAD toolkits and their dependencies with external video, VR, font and memory-allocator libraries turned off. Packaging includes only the DLLs listed in `runtime-dependencies.json` and checks their hashes.

`dependencies.lock.json` pins download versions, sources and SHA-256 hashes. `samples` contains test models made for this project only. A manual GitHub Actions build example is in `docs/windows-build-workflow.yml.example`; copy it to `.github/workflows/build.yml` to enable it.

## License

Code written for this project is [MIT](LICENSE). External components such as OCCT, Assimp, lib3mf and the Microsoft runtime have their own terms; see the [third-party notices](THIRD_PARTY_NOTICES.md) and `licenses/`. The OCCT source archive is provided with each binary release.
