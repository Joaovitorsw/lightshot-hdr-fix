# Windows installer backend

`Install-Core.ps1` runs with Windows PowerShell 5.1 and .NET Framework already
included with Windows. It does not use Python, require elevation, launch
Lightshot, change processes, write registry keys, or alter startup settings.
The Inno wrapper owns shortcuts, its own uninstaller registration and any
explicit finish-page launch.

## Calling contract

```powershell
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "Install-Core.ps1" `
  -Action Detect -Spec "C:\payload\installer-spec.json" -Report "C:\temp\detect.json"

powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "Install-Core.ps1" `
  -Action Validate -Source "C:\Program Files (x86)\Skillbrains\lightshot\5.5.0.7" `
  -Destination "C:\Users\Example\AppData\Local\Programs\Lightshot HDR\Lightshot-v0.3.0" `
  -NewDll "C:\payload\DXGIODScreenshot.dll" -Spec "C:\payload\installer-spec.json" `
  -Report "C:\temp\validate.json"

# Same arguments as Validate, using -Action Install, create the application copy.
# Its destination parent must exist; destination itself must be fresh or match
# the complete prior owned manifest and the same payload/spec.

powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "Install-Core.ps1" `
  -Action Uninstall -Destination "C:\Users\Example\AppData\Local\Programs\Lightshot HDR\Lightshot-v0.3.0" `
  -Report "C:\temp\uninstall.json"
```

All input/output paths must be absolute local-drive paths. `Source` is optional
for Detect, Validate and Install: detection checks the standard Skillbrains
installation directories and read-only matching uninstall `InstallLocation`
entries, then at most 32 immediate child folders per candidate root. It matches
all three original binaries before scanning the complete resource tree.
For a custom installation, supply the internal version folder explicitly.
Detect needs Spec only; Validate and Install also require NewDll. Uninstall
requires only Destination, plus optional Report.

Spec comes from `scripts/export_installer_spec.py` at build time. The backend
does not duplicate version hashes or patch opcodes. This JSON is trusted bundled
build metadata, not a downloaded or user-authored configuration. It includes
schema_version=1, original_hashes, patches with integer offset and hex
original/replacement, and new_dll_sha256. The payload is checked against that
hash and must have x86 PE DLL characteristics. ABI/runtime validation belongs
to the build harness.

Exit code is zero on success and one on operational failure. Standard output
contains a JSON object with `success`, `action`, `source`, `destination`, `error`,
and action-specific fields. Report, when supplied, receives the same UTF-8 JSON
without BOM. Beside it, `Report + '.source.txt'` contains the validated source
on success; `Report + '.error.txt'` contains the diagnostic on failure. Both
sidecars are always written, with the unused one empty, in UTF-8 without BOM.
Use `LoadStringFromFile` and `UTF8Decode` in Inno. Report's parent must exist;
all three outputs must be outside the source/destination and cannot overwrite
Spec or NewDll. If a report path is itself unsafe or unwritable, inspect stdout
and the nonzero exit code instead.

## Ownership and preservation

Install copies the internal version folder, patches only its copied DLL,
replaces only its copied capture DLL, and preserves both originals under
`backup\`. `HDR-install-manifest.json` records origin, destination, original
hashes, spec hash, patches, all installed file hashes and managed directories.
Repeated installation is successful only when every managed file still matches
and the payload/spec match; it does not overwrite changed or unrelated files.

Uninstall validates the complete destination tree and manifest paths before
deleting anything. It deletes only managed files whose current hashes match,
then removes only empty managed directories. Unknown and changed files remain
and are reported in `preserved`; the ownership manifest remains when files are
preserved. The manifest is removed after a complete managed uninstall. No
recursive deletion occurs in the backend. The Inno wrapper must **not** add a
recursive `[UninstallDelete]` entry for the application copy.

Junctions/reparse paths, source/destination overlap, path traversal, alternate
data streams and reserved path names are refused. Do not modify inputs or the
destination concurrently with the backend. I/O failures may leave a partial
copy without a completed manifest; the backend refuses to overwrite that tree.
Choose a fresh versioned destination after inspecting a partial failure.

This is file isolation. Original Lightshot registry/preferences/mutex behavior
may still be shared; the backend does not promise a separate user profile.

## Tests

```powershell
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tests\test_installer.ps1
```

Tests use synthetic binaries and temporary directories, including real Windows
junctions. They do not inspect installed Lightshot, modify registry/processes,
or require Pester/Python. The PS 5.1 JSON representation follows
[Microsoft's ConvertFrom-Json documentation](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.utility/convertfrom-json?view=powershell-5.1);
the backend uses object properties rather than PowerShell 6+ `-AsHashtable`.
