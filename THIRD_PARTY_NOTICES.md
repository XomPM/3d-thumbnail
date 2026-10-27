# Third-party notices

The project's own code is MIT licensed. The following dependencies retain their own licenses. Binary packages include this document and the `licenses` directory. Versions and download hashes are recorded in `dependencies.lock.json`.

| Component | Purpose / version | License and notices |
| --- | --- | --- |
| Open CASCADE Technology | STEP/IGES conversion; 7.9.3, unmodified upstream source | LGPL 2.1 with OCCT exception; `OCCT-LGPL-2.1.txt`, `OCCT-exception.txt` |
| Assimp | OBJ, PLY, glTF/GLB, FBX; 6.0.5, static | BSD 3-Clause; `Assimp-LICENSE.txt` |
| lib3mf | 3MF parsing; 2.5.0, upstream Windows binary | BSD 2-Clause; `lib3mf-LICENSE.txt`, `lib3mf-Credits.txt` (libzip, zlib, cpp-base64, fast_float) |
| Assimp bundled code | zlib, RapidJSON, pugixml, utf8cpp, Clipper, poly2tri, earcut, MiniZip, OpenDDLParser | Original notices preserved as separate files in `licenses/`; includes sources compiled or made available by this Assimp configuration |
| NSIS | Installer; 3.13, zlib compression | `NSIS.txt`; compiler is a build dependency, installer stub/plugins accompany Setup |
| Microsoft Visual C++ runtime | Unmodified release x64 DLLs from Visual Studio 2022 redistributables | `Microsoft-runtime.txt`; excluded from the project's MIT license |
| Windows APIs | Direct3D 11 WARP, WIC, Shell, COM | Provided by Windows; system DLLs are not redistributed |

OCCT is dynamically linked. The exact upstream `OCCT-7.9.3-source.zip` is supplied alongside the binary release; SHA-256: `566a236b5a22e778dbe1c3dce6560ba12db4504bbdae0354c51dd2f4728756ee`. Build instructions are in `scripts/build-occt.ps1`. Users may rebuild and replace these DLLs with compatible versions, including for debugging modifications as permitted by the OCCT license and exception. The installer verifies the original payload during installation; installed libraries can be replaced afterward when no process is using them.

The source configuration disables external FreeType, FreeImage, FFmpeg, OpenVR, TBB and jemalloc. CAD/XCAF toolkit dependencies still include some OCCT visualization-related modules, but the program does not invoke their viewers. No third-party video or VR binaries from the combined development SDK are packaged. The shipped dependency list and SHA-256 values are in each runtime's `runtime-dependencies.json`.

Official sources: [OCCT V7_9_3](https://github.com/Open-Cascade-SAS/OCCT/tree/V7_9_3), [Assimp v6.0.5](https://github.com/assimp/assimp/tree/v6.0.5), [lib3mf v2.5.0](https://github.com/3MFConsortium/lib3mf/releases/tag/v2.5.0), [NSIS](https://nsis.sourceforge.io/), [Microsoft redistribution list](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution).

All model samples in this repository were generated for this project. No private customer models or externally licensed model collections are included.
