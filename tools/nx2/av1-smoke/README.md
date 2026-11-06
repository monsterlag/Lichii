# Packaged AV1 smoke test

Configure Lichii with `-DLICHII_AV1_SMOKE_TEST=ON`, then build the
`av1_decode_smoke` target. This does not require `YUZU_TESTS` or Catch2.
It links the same selected FFmpeg libraries as `video_core`, looks up AV1 by
codec ID, and decodes an original synthetic frame on the CPU. A missing decoder,
decode error, missing frame, or incorrect visible pixels returns a nonzero exit code.

Run `av1_decode_smoke.exe` beside the packaged Windows binaries after their DLLs
have been collected. Use a fresh process with `PATH` restricted to Windows system
directories so the build environment cannot supply missing DLLs. Run from the package
directory, outside the dependency installation. A missing imported DLL fails at process
startup, before the test can print its diagnostic. The probe can be removed before
publishing the package.

This checks the libraries selected for that build and their packaged runtime dependencies.
It does not inspect FFmpeg already statically linked into an existing emulator executable,
test GPU decoding, or fix an AV1-less package. The Windows release workflow enables this
target for both MSVC and MinGW PGO and runs it after collecting DLLs, before upload.
Other builds remain opt-in. The MinGW PGO build uses MSYS2 system FFmpeg with AV1 support, following [PR #195](https://github.com/zortos293/Lichii/pull/195).

## Manual Windows MinGW PGO package

The manual-only `.github/workflows/build88-windows-mingw-pgo.yml` builds the full GUI and CLI with the release workflow's UCRT64 clang/LLD, `eglantine.profdata`, and Release settings. It collects Qt plugins and recursive runtime DLL dependencies, then runs the AV1 probe from the package in a fresh process with `PATH` restricted to Windows system directories. On success it removes only the probe executable and its PDB; runtime DLLs remain in the package. One seven-day artifact contains the complete GUI/CLI package and `AV1-DECODE-SMOKE.txt`; it uploads only if the CLI and AV1 checks both exit zero. The workflow has no release or tag operation.

On this integration branch, the catalogued `.github/workflows/minimal-windows-av1-smoke.yml` is a manual-only wrapper that calls the full package workflow. Dispatch that wrapper with this branch selected to run the same full build.

The CLI `--help` check exits before `System::Initialize`; the GUI does not handle `--help` before its Vulkan/startup checks, so the workflow omits a GUI loader launch to avoid starting the UI. `AV1-DECODE-SMOKE.txt` records both checked exit codes.

The probe fixture is synthetic and contains no game assets. The cloud run validates the build's selected FFmpeg decoder and packaged DLL closure; it does not validate GPU video decoding, game playback, or performance on the owner's PC.
## Fixture provenance

The embedded 22-byte OBU stream contains one lossless 16x16 black YUV420P frame
(Y=16, U=V=128). It contains no game data. It was generated using FFmpeg's
`color` source and libaom 3.15.0 with this command:

```sh
ffmpeg -f lavfi -i color=black:s=16x16:r=1 -frames:v 1 -c:v libaom-av1 \
  -cpu-used 8 -threads 1 -crf 0 -b:v 0 -still-picture 1 -lag-in-frames 0 \
  -f obu black16.obu
```

Encoder versions may produce different valid bytes. The test checks the decoded
pixels, dimensions, format, and frame count, rather than the encoder's byte stream.
