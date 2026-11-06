# Private shutdown diagnostics

This branch extends integration `01442dca1e8539da8b59061c5583bfcfba5d2c7a`
with observational GPU wait counters and kernel suspension phases. It does not
change wait predicates, completion fences, queue order, callbacks or shutdown
order. The two modified production translation units compiled with the existing
MSVC toolchain; complete builds and runtime results are separate evidence.

The CPU reproducer constructs the actual System and GPU without initializing
guest memory, a renderer or GPU workers. It observes completed fences, a pending
wait crossing shutdown, a wait beginning after shutdown and enqueue after stop.
A real `TickWork` call rescues pending no-op requests before joining each waiter.
Exit zero means the existing blocked-wait behavior was observed and the rescue
controls passed. It does not mean the shutdown defect is fixed.

The host entry handshake does not establish the waiter's exact CV position.
Unstarted workers do not reproduce worker termination, guest-core suspension,
real graphics completion, a full bounded command queue or the observed GUI hang.
Diagnostic snapshots may miss racing waiters; absence is inconclusive.

`private-target.cmake` is included at the end of `src/CMakeLists.txt` to inherit
the normal source-directory settings. The target is excluded from ordinary
builds. Linux CI explicitly builds and runs it after production and full tests.
The Windows job builds its ordinary game targets without this executable.

`run.py` requires a fresh portable profile beside the test executable and caps
only that child at 15 seconds. The executable verifies its NAND path before
constructing System, whose ProfileManager may create a synthetic profile.
Existing profiles are never reused or removed. CI archives only synthetic
command/result JSON and stdout/stderr, excluding profiles and executables.
Results record both the 014 baseline and the actual diagnostic build revision.

This is a private diagnostic contribution branch, not a proposed production
repair or upstream test registration. No game content, keys, saves, captures or
private game logs belong in it.
