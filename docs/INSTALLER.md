# Automatic Windows installer

The installer runs per user with Inno Setup 6. No Python, administrator rights,
downloads, HDR toggling or startup changes are required at installation time.
An already installed, hash-matching Lightshot is required.

Setup detects standard install locations and relevant uninstall metadata. For
custom locations it offers a folder picker. It validates the originals and the
bundled capture DLL before creating a separate versioned copy under the install
directory. Start menu and optional desktop shortcuts use the activation helper.
The helper asks before closing a supported Lightshot owned by the same user and
session. If another instance appears without consent, it refuses to close it.
Silent installation skips launching the application.

The uninstaller removes only manifest-owned, unchanged application files.
Unknown or modified files are preserved. It does not uninstall the original
Lightshot or change its settings. A preserved file can leave a nonempty folder.
Reinstalling the same version is allowed only when its managed files and payload
match; a changed/unmanaged destination is refused.

## Build

First compile the capture DLL with `Build.cmd`. Install Inno Setup 6 on the build
machine, then run:

```powershell
.\scripts\Build-Installer.ps1 -DllPath .\build\DXGIODScreenshot.dll
```

Optional parameters: `-OutputDirectory`, `-CompilerPath`, `-Python`. Python is used
only by the build process to export the canonical hashes and patch offsets from
`scripts/package_copy.py`. The bundled runtime engine uses Windows PowerShell 5.1
and .NET already present on Windows. It explicitly imports its own runtime's
modules to avoid inherited PowerShell 7 module-search-path conflicts.

Output: `dist/Lightshot-HDR-Fix-Setup.exe`. The installer contains only the new
capture DLL, a generated hash/patch specification and the installer scripts.
Original Lightshot binaries are copied locally at install time, never bundled.
The installer is unsigned; Releases includes SHA-256 checksums.

## Validation

```powershell
powershell.exe -NoProfile -File tests\test_installer.ps1
powershell.exe -NoProfile -File tests\test_launcher.ps1
python -m unittest discover -s tests -p 'test_*.py' -v
```

Fixtures cover source/payload guards, opcode verification, reparse paths,
traversal, idempotence, preservation, user/session ownership and the activation
consent race. Local end-to-end validation also exercised automatic detection,
silent setup, reinstall, real patched-core capture and uninstall with a user
file preserved. Hosted CI runs fixture tests, not an interactive HDR desktop.

For controlled deployment/testing, Inno supports `/VERYSILENT`, `/NORESTART`,
`/DIR="absolute destination"`, `/LOG="absolute log path"` and this project's
`/LIGHTSHOTSOURCE="absolute internal version directory"` override. Only pass
paths you control. Interactive setup handles source selection automatically.
