# Lightshot HDR Fix

Unofficial Windows addon that fixes washed-out, overexposed Lightshot screenshots when HDR is enabled. FP16 Desktop Duplication preserves highlight detail; GPU tone mapping produces an SDR image for the existing Lightshot editor. The capture backend does not create the yellow Windows Graphics Capture border.

[Download the addon](https://github.com/Joaovitorsw/lightshot-hdr-fix/releases/latest) · [Validation and limitations](docs/VALIDATION.md)

## Instalar / Install

The release contains **only the replacement capture DLL and the patcher**. You need your own installed Lightshot. No original Lightshot executable or patched proprietary DLL is distributed.

Requirements: Windows 10/11 with DXGI 1.5-capable graphics, the supported x86 Lightshot build below, and Python 3.10 or newer. Tested on Windows 11 with HDR + SDR monitors.

1. Download and extract the Windows x86 addon ZIP from Releases.
2. Exit Lightshot using its tray icon.
3. In PowerShell, opened in the extracted addon folder, run:

```powershell
py -3 .\scripts\package_copy.py --source "C:\Program Files (x86)\Skillbrains\lightshot\5.5.0.7" --destination "$env:LOCALAPPDATA\Lightshot-HDR" --new-dll ".\DXGIODScreenshot.dll"
```

4. Start the corrected copy:

```powershell
& "$env:LOCALAPPDATA\Lightshot-HDR\Lightshot.exe"
```

Use a fresh destination directory. The patcher refuses existing destinations, unexpected hashes, incompatible binaries and linked/reparse paths. It copies the installed application and resources, backs up the two original DLLs inside the copy, and patches only the copy. A JSON manifest records hashes and offsets.

If your installation is elsewhere, change `--source` to its internal version directory. There is no in-place update mode. To revert, exit the corrected copy and run your original installation.

**Português:** saia do Lightshot original, aplique o comando acima e abra o executável da cópia. O HDR permanece ligado. Se quiser iniciar com o Windows, aponte a inicialização para o executável da cópia e desative a entrada antiga. O pacote público não modifica automaticamente seus atalhos ou inicialização.

Application files are separated, but mutex, registry/preferences and hotkeys may be shared. Run one Lightshot instance at a time. The DLL is unsigned.

## Supported Lightshot build

Internal installation directory: `5.5.0.7`; executable version resources: `5.5.0.4`. All three SHA-256 values must match:

| Original file | SHA-256 |
| --- | --- |
| Lightshot.dll | `04a27f0d1e89341722461119e00a10e00ec2a52f5e305961161ec4378e610e93` |
| DXGIODScreenshot.dll | `6acdcd817cc5df637aa4cd101c25c9e0a69c778347a7a40ce7511eeea26fd6f0` |
| Lightshot.exe | `0b899508777d7ed5159e2a99a5eff60c54d0724493df3d630525b837fa43aa51` |

Other versions are intentionally rejected. Do not reuse these patch offsets on another build.

## How it works

- Preserves the x86 `bool __stdcall TakeScreenshotExp(HDC, RECT)` ABI, undecorated export at ordinal 1.
- Requests FP16/BGRA using `IDXGIOutput5::DuplicateOutput1` and queries each monitor’s HDR state and Windows SDR white level.
- Converts linear scRGB to SDR sRGB using a shared-luminance highlight shoulder. The HDR reference white maps to about 240, leaving space for brighter highlights. This preserves detail at the cost of a different tone curve from other screenshot tools.
- Reuses D3D devices, duplicators and conversion textures. Shaders are compiled at build time. There is no background screenshot polling or image-file recording.
- Rejects an initial pointer-only notification with no valid desktop image. Reuses an existing valid image when the desktop has not changed; color/topology changes and failures invalidate the cache.
- Stages every monitor before writing the destination bitmap. On capture failure the caller keeps its GDI fallback, which may still be overexposed with HDR.
- Applies three guarded binary patches: try the replacement after GDI, retain retry capability, and disable the obsolete ASLR relocation for a removed instruction.

Measured on one 3440×1440 HDR + 1920×1080 SDR setup: subsequent complete core captures took **62–78 ms**, with about **312 ms** on the first call. These are capture timings, not universal key-to-overlay latency guarantees. See [validation](docs/VALIDATION.md).

## Build

The supplied `Build.cmd` targets x86 using Clang/LLD, MSVC headers/static CRT, and Windows SDK libraries. Tested toolchain: VS 2022 VC 14.44, Windows SDK 10.0.22621.0, MSYS2 clang64.

Default paths are in `Build.cmd`. Override `LIGHTSHOT_VSROOT`, `LIGHTSHOT_CLANG` and `LIGHTSHOT_LINK` if needed; the `llvm-rc.exe` path is also set in that script. The specified VC/SDK versions must be installed.

```cmd
Build.cmd
```

Build regenerates `src/gpu_color_bytecode.h` from `src/gpu_color.hlsl`, builds the DLL and diagnostic harnesses, then runs CPU/GPU color tests. The shader builder needs D3DCompiler_47; the shipped capture DLL has no runtime shader compiler dependency. Tests require a Windows graphics environment.

```powershell
py -3 -m unittest discover -s tests -p test_package_copy.py -v
```

CI runs patcher tests on Windows/Linux and the portable CPU color reference on Linux. Real-monitor HDR calibration, D3D hardware/WARP tests and capture integration were performed locally; hosted CI does not validate an HDR display.

## Scope and limitations

Exclusive-fullscreen games, protected content, rotated displays and the complete editor/save/clipboard flow require further testing. Software-drawn cursors may already be present in the duplicated image while Lightshot also draws a cursor. Desktop/driver mode changes can temporarily use the uncorrected GDI fallback.

The module and capture resources remain loaded until process exit; hot unloading/reloading is not supported. Output screenshots are SDR, not HDR image files.

## Project and third-party status

This is an independent addon, not an official Lightshot release and not a recompilation of Lightshot’s closed-source core. Lightshot is not included. No general open-source license has been selected for the newly authored project code. Third-party components retain their licenses; see [DirectXMath MIT license](vendor/DirectXMath/LICENSE) and its [pinned upstream version](vendor/DirectXMath/UPSTREAM.txt).

## References

- [Microsoft: DuplicateOutput1](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_5/nf-dxgi1_5-idxgioutput5-duplicateoutput1)
- [Microsoft: AcquireNextFrame, cursor updates and access loss](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutputduplication-acquirenextframe)
- [OBS HDR duplication implementation](https://github.com/obsproject/obs-studio/blob/master/libobs-d3d11/d3d11-duplicator.cpp), consulted to verify format/color-space behavior; no GPL implementation code was copied.
