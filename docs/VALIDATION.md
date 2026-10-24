# Local validation — 2026-10-24

Test setup: RTX 5080; 3440×1440 HDR primary and 1920×1080 SDR secondary; virtual desktop 5360×1440. Windows SDR white scale on the HDR display was 3.6.

| Check | Result |
| --- | --- |
| Four HDR gray patches, old GDI | 210, 255, 255, 255 |
| Same HDR patches, corrected | 118, 240, 250, 253 |
| SDR old/new | Both 118, 255, 255, 255 |
| Cached static frame | Preserved |
| New presented content | Observed by next capture |
| GPU vs CPU reference | 197,376 pixels per hardware/WARP device; max error 1/0 channel bytes |
| Whole-desktop capture | 12 repeated calls passed; no GDI handle growth |
| Actual patched core | 12 repeated captures passed, plus 2 after local deployment |
| Invalid arguments | Rejected without destination mutation |
| Failed capture then valid call | Destination preserved; next call recovered |
| Original installation | Required-file SHA-256 values unchanged |
| Patcher | 13 tests; one symbolic-link test skipped locally for missing privilege; Windows junction tests passed |

Warm DLL calls took about 15–32 ms; warm complete core captures took 62–78 ms. First-call initialization was about 235 ms for the DLL and 312 ms through the core. The prior WGC DLL took 672–796 ms per mixed-desktop call. These are measurements from one machine and different pipeline boundaries are identified explicitly.

The user reported successful HDR screenshots and successful testing of the lower-latency version. Exclusive fullscreen, protected content, rotation and full UI save/clipboard workflows were not exhaustively validated. No private screenshots, machine registry backups or local diagnostic paths are included here.

## Installer 0.3.0

The per-user installer was built with Inno Setup 6.7.3 and tested without
administrator rights or a Python runtime at install time. Automatic detection,
installation and exact-version reinstall completed successfully. Three captures
through the installed patched core passed. Uninstallation removed managed files
and its shortcut while preserving a deliberately added user file. The existing
active HDR copy and original installation remained unchanged.

Seventeen Windows PowerShell fixture tests cover the copy/uninstall backend,
including incompatible inherited module paths. Launcher tests verify executable
hash, current owner SID/session and refusal of a newly appearing process without
closure consent. The interactive message-box path was reviewed in source; the
end-to-end install/uninstall tests used silent mode and did not close the active
Lightshot. See [installer details](INSTALLER.md).
