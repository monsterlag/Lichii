# Private shutdown diagnostics

This branch extends integration `01442dca1e8539da8b59061c5583bfcfba5d2c7a`
with observational GPU wait counters and kernel suspension phases. It does not
change wait predicates, completion fences, queue order, callbacks or shutdown
order. The two modified production translation units compiled with the existing
MSVC toolchain; complete builds and runtime results are separate evidence.

The original controls construct the actual System and GPU without initializing
guest memory, a renderer or GPU workers. They observe completed fences, a pending
wait crossing shutdown, a wait beginning after shutdown and enqueue after stop.
A real `TickWork` call rescues pending no-op requests before joining each waiter.
Exit zero means the existing blocked-wait behavior was observed and the rescue
controls passed. It does not mean the shutdown defect is fixed.

Those original host entry handshakes do not establish the waiter's exact CV
position. The added started-worker case has stronger synchronization:

1. A real owned System GPU starts with RendererNull, a headless EmuWindow and the
   concrete no-op GraphicsContext. A real WaitForIdle tick completes a pending
   no-op fence with no manual TickWork, proving the production worker executes.
2. A RunInOrder command waits on an owned promise/future gate. Another owned
   thread calls WaitForIdle, queuing a no-op sync request and a real GPUTick behind
   the blocked command. Read-only metadata access observes its actual idle waiter
   count under the same mutex and both command/request fences.
3. An owned thread calls real NotifyShutdown. A stop_token copied before shutdown
   observes the worker's actual stop request. Only then is the command released.
   NotifyShutdown must join; the uncompleted command fence proves the queued tick
   was abandoned, while the sync waiter must remain blocked for 250 ms.
4. Real TickWork executes the pending no-op and rescues the waiter. A new
   WaitForIdle after stop must likewise block and be rescued. A previously
   completed fence and a null-renderer preemptive OnCPURead must return after stop.
   Every host thread joins before GPU/Host1x/device memory teardown.

The started fixture does not call System::Initialize, Kernel::Initialize or Load.
A compile-gated helper inside core.cpp populates the existing System-owned
optional DeviceMemory, Host1x and GPU. Existing HostMemoryTestAccess(false) limits
backing to 64 KiB, forces the normal successful fallback allocator and specifies
4 MiB virtual size without allocating a fastmem arena. Host1x additionally uses
about 264 MiB sparse address metadata and 16 MiB initialized cache counters, plus
roughly 640 KiB GMMU vectors and small objects. These are source-derived sizes,
not a measured process RSS. No guest addresses are mapped or accessed.

Production worker registration constructs a TLS dummy KThread with owner=nullptr.
Its constructor registers a raw pointer in the constructed kernel object's set;
the dummy initialization needs no initialized scheduler/slab/guest memory.
Worker exit destroys this TLS object with the default KThread destructor. This
source does not unregister that pointer there. The fixture never traverses that
set or calls Kernel::Shutdown; the default kernel destructor destroys the set
without dereferencing its pointer elements. The joined worker and test threads
die before GPU, then Host1x, then DeviceMemory; System/Kernel and window outlive
that sequence. This limited initialization is a fixture boundary, not proof of
guest CPU or complete kernel shutdown behavior.

The no-op idle case does not reproduce reactive CPU downloads, GPU driver work,
split workers, a full bounded command queue, guest-core suspension or the GUI
hang. RendererNull::OnCPURead is always preemptive and is only a nonblocking
control. The test never treats a pending fence as completed to escape a wait.

`private-target.cmake` is included at the end of `src/CMakeLists.txt` to inherit
the normal source-directory settings. The target and fixture implementations
require explicit `-DLICHII_ISSUE222_WORKER_REPRO=ON` during cloud configuration;
the default is OFF. The workflow's Linux configure command must receive this
flag before its existing explicit target build/run can work. The target remains
excluded from ordinary builds. No additional warning suppression is introduced.
Windows/default configurations do not compile the fixture implementations.

`run.py` sets EDEN_NX2_SPLIT=0 in its child only, requires a fresh portable profile
beside the test executable and caps
only that child at 15 seconds. The executable verifies its NAND path before
constructing System, whose ProfileManager may create a synthetic profile.
Existing profiles are never reused or removed. CI archives only synthetic
command/result JSON and stdout/stderr, excluding profiles and executables.
Results record both the 014 baseline and the actual diagnostic build revision.
All started-worker compile/runtime results remain unverified until the coherent
cloud build runs. No local compilation or execution is authorized for this draft.

This is a private diagnostic contribution branch, not a proposed production
repair or upstream test registration. No game content, keys, saves, captures or
private game logs belong in it.
