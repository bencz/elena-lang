# ELENA code generation

This directory contains the target-independent runtime model, EIR, e-code decoding,
and target code generators. E-codes remain the ELENA input IR. EIR is the optimizing
IR. `machine.h` defines the target-independent value, effect, and logical relocation
vocabulary used at the machine boundary. Runtime data references are part of the
shared runtime contract. Each target owns its physical machine IR after instruction
selection.

The x86-family backend uses the following boundary:

- `x86/machine.*` owns physical x86 and AMD64 registers, selected opcodes, operands,
  instruction sequences, and target-specific verification;
- `x86/lowering.*` selects those physical operations from verified EIR;
- `x86/encoder.*` encodes selected operations and target relocations;
- `x86/runtimecore*` emits the x86-family implementations of runtime operations.

A physical MIR is not shared between unrelated instruction sets. ARM64 and PPC64LE
reuse EIR, machine effects, runtime contracts, layouts, and protocols, then provide
their own physical instruction selection and encoding. Generic code must not include
a target directory; each backend may include the generic root.

## Source organization

The code generator has no 80-column limit. A declaration or expression stays on one
line while that is the clearest representation; a line is split only to expose
semantic hierarchy. Artificially narrow formatting is not accepted.

Validation, state preparation, instruction emission, relocation, and final
verification are separate phases. A lowering routine represents one e-code or one
named phase of an e-code. Large opcode dispatchers select those routines; they do
not contain the implementation of several unrelated operations.

Blank lines separate logical phases and synchronization boundaries. Positional
initializers are reserved for types whose fields are self-evident at the call site.
Runtime layouts, ABI descriptions, instructions, relocations, blocks, and edges use
named fields or named construction helpers. Numeric field offsets are forbidden
when a typed runtime-layout field exists.

Comments document e-code identities, machine encodings, ABI obligations, and
non-obvious concurrency invariants. Comments do not restate ordinary C++ control
flow.

`common/runtimelayout.h` is the single structural layout vocabulary shared by the
engine, GC/MTA protocols, and every code generator. Fixed fields are addressed by
typed names such as `GCDataField::PermanentCurrent` and
`ThreadContentField::StackFrame`; backends must not reproduce their word indices.
The system environment deliberately supports its hybrid native-word and serialized
32-bit layout. Physical spill slots remain private to the backend that owns the
stack convention.

`ECodeOperandResolver` owns logical frame, stack, and VMT index normalization.
`ECodeRuntimeProvider` owns the runtime operation required by an e-code.
`ECodeEIRProvider` owns e-code interpretation, runtime-mode choices, and EIR
construction. `FrameEIRProvider` owns frame alignment and the semantic frame
sequence for every target. These classes are architecture-independent and are
tested with i386, AMD64, ARM64, and PPC64LE target descriptions.

`EIRLocation` names the managed value, object, cached arguments, wide value,
allocation size, frame, and stack without naming a physical register. Location
operands are the explicit-state input form of EIR. SSA construction replaces their
definitions and uses with EIR values and phi nodes; instruction selection maps any
locations that remain at the machine boundary through the target managed ABI.
Neither an e-code provider nor an optimization pass may assign an x86, ARM64, or
PPC64LE register to an `EIRLocation`.

The x86-family lowering contains no `ByteCode` reference. Its `ByteCommand` entry
point is an integration adapter that immediately invokes the target-independent EIR
provider. Scalar, object, control-transfer, allocation, frame, managed-slot,
runtime, method, dispatch, and exception semantics are selected only from verified
EIR and generic metadata. The backend may inspect EIR effects, operands, allocation
properties, and runtime contracts, but may not reconstruct the source e-code or
choose a runtime policy from an inline number. `rg ByteCode codegen/x86` returning
no matches is a mandatory migration gate.

Backend directories may own physical registers, instruction forms, addressing-mode
selection, spill placement, machine labels, relocations, TLS instruction spelling,
and concrete ABI save/restore sequences. They may not own e-code decoding, logical
operand scaling, frame composition, GC or MTA protocol ordering, collection kind,
runtime-call selection, object-size policy, or source-level control flow.

Dispatch control flow is target-independent. `DispatchControlFlow` owns the phases,
conditional edges, terminal edges, receiver-list traversal, overload advancement,
argument matching, ancestor walking, success restoration, and fallthrough.
`DispatchEIRProvider` materializes and verifies that graph as EIR before physical
selection. A backend owns only the realization of those phases in its registers,
addressing modes, spills, and branch instructions.

`RuntimeCoreProtocol` defines architecture-independent runtime actions, ordering,
effects, and threading requirements. Its action mask is identical for x86, AMD64,
ARM64, and PPC64LE. Target directories contain only ABI lowering, register choices,
instruction selection, encoding, and target relocation forms. Supporting an action
on one target does not authorize removing the corresponding legacy block for another
target.

## Migration rules

Every operand must be classified before lowering as one of:

- literal;
- logical index;
- physical displacement;
- module reference;
- module message;
- runtime symbol;
- implicit JIT state.

Lowering must preserve the semantic class. Module references, messages, runtime
symbols, and external symbols must remain relocations until the linker resolves
them. Raw byte equality is meaningful only after this resolution.

Runtime operations must declare their heap, global, TLS, synchronization,
safepoint, root-relocation, call, allocation, and exception effects. External calls
must be emitted through the selected platform ABI, including argument locations,
stack alignment, shadow space, preserved managed roots, and import relocation form.

Raw instruction bytes are documented at the emission site. A phase identifies the
runtime action being lowered, and every byte sequence identifies its mnemonic and
operands. ABI-specific argument moves and stack adjustments identify the ABI they
implement. Relocation comments name the symbolic operand instead of presenting its
placeholder bytes as a literal address.

The legacy assembly is a behavioral reference, not a specification. Its algorithm
must be checked against the runtime contract before migration.

Pointer-size multiplication is reserved for genuinely dynamic sizes and indices.
Fixed object, VMT, message-table, GC, thread, TLS, and system-environment positions
must use the shared typed layout. A new architecture extends instruction selection
and ABI lowering against this contract instead of copying numeric displacements.

A legacy block can be removed for a target variant only after:

1. the generated operation is complete for that architecture, threading mode, and ABI;
2. relocation and ABI tests pass;
3. the corresponding core assembly builds without the block;
4. a clean compiler build links and passes the system tests with that core binary.

When a required suite already has a reproducible baseline failure, the generated
path must reach the same or a later point without a legacy fallback, and the baseline
failure must be recorded. A known baseline failure is never reported as a passing
gate.

Incremental builds are insufficient after changing a shared C++ interface because
the legacy makefiles do not track all header dependencies.

E-codes with multiple inline variants are migrated with a bit mask. Only generated
variants are excluded from the legacy core image; all other variants remain loaded
until their own lowering and validation gates are complete. If variant zero is
generated, an absent non-generated variant is kept null instead of inheriting an
unrelated byte sequence; an emitted unsupported variant fails with its full
`variant << 8 | opcode` identity.

## Runtime core status

`GC_ALLOC` is generated for x86 and AMD64 in STA and MTA modes.
`GC_ALLOCPERM` is generated for x86 and AMD64 in STA and MTA modes. Its x86-family
assembly blocks have been removed. The MTA fast path and external collector adapter
are structurally tested. The remaining MTA system-suite failures are recorded in the
validation section and are never hidden by restoring a removed assembly body.

`PREPARE` is generated for x86 and AMD64 with System V and Windows ABI handling.
`THREAD_WAIT` is generated for x86 and AMD64 MTA cores. The generated routine
revalidates the collection state under the GC lock, publishes and restores the frame
chain, marks parked threads safe, and waits on a collection-generation barrier. The
barrier is implemented by the Linux, macOS, and Windows runtimes. The MTA assembly
procedures have been removed; STA emits the corresponding no-op routine, so no x86
core retains a `THREAD_WAIT` assembly body.

`GC_COLLECT` is generated for x86 and AMD64 in STA and MTA modes. The generic runtime
protocol selects frame publication, mutator parking, thread enumeration, wait-list
coordination, root-lock acquisition, static, permanent, TLS, and frame roots, the
collector call, mutator resumption, and lock release. The x86 backend lowers this
protocol for System V and Windows ABIs, including stack alignment, shadow space,
managed-root preservation, and external relocation forms. All four x86-family
assembly bodies have been removed. MTA Mach-O remains rejected until its current-
thread TLS contract is implemented.

The MTA parking protocol enters the safe region before publishing the stop signal,
waits on the collection-generation barrier, and restores the previous thread state
only after the barrier returns. A later collection can therefore enumerate the
parked frame but cannot add that mutator to its wait list. Waiting on the collector's
per-thread event is forbidden because a later table scan can reset or reuse it.

`GCDataField::QueueSemaphore` preserves the runtime table ABI introduced by the
upstream GCX semaphore fix. The generated x86-family core does not duplicate that
semaphore protocol: its collection-generation barrier and safe-region publication
provide the same race guarantee without restoring `GC_COLLECT` or `THREAD_WAIT`
assembly bodies. All target core tables reserve the field so x86, AMD64, AArch64,
and PPC64LE retain one shared `GCTable` layout.

`XHookDPR` (`0xE6`) lowers to the generic `ExceptionHook` EIR operation. The handler
target remains typed as either a procedure label or a module reference until the
linker resolves it. The x86-family selector materializes the exception structure,
loads the previous handler from STA runtime data or MTA TLS, publishes the catch
frame, catch level, catch address, and previous link, then makes the new record
current. Its four i386 and AMD64 STA/MTA inline bodies have been removed.
`ModuleReferenceValue` carries the actual symbolic reference produced by EIR;
`ModuleReference` remains the transitional source-argument relocation used by older
selectors. The exception selector never converts its reference back into an e-code
operand ordinal.

`VEH_HANDLER` (core reference `0x10003`) is a generated runtime-core entry rather
than an e-code. The OS signal or exception bridge enters it with the ELENA error in
the accumulator and the faulting instruction in the managed value register. The
entry preserves that instruction in cached argument zero, moves the error to the
managed value register, resolves the current thread through STA data or the target
TLS model, and transfers to `eh_critical`. The generated AMD64 MTA ELF path replaces
an empty Linux branch in the legacy `corex60.asm`; it is not an as-is translation of
that defect. All four x86-family procedure bodies have been removed.

`System 3` thread startup is lowered to MIR for x86 and AMD64. TLS access is selected
from the target, thread slots use the runtime layout and an explicit scaled store,
and stack root/frame offsets come from `ThreadContentLayoutSpec`. Variant 3 has been
removed from both MTA assembly cores.

`System 4` process startup is lowered to target-aware MIR for x86 and AMD64. FPU
initialization, native stack capture, STA stack-root publication, the generated
`PREPARE` call, and the System V synthetic entry frame are explicit. FreeBSD keeps
its distinct entry-stack source. Variant 4 has been removed from the STA and MTA
assembly cores.

`System 6` and `System 7` acquire and release the MTA GC lock through generic
`GCLockAcquire` and `GCLockRelease` EIR operations. The x86-family selector uses a
typed `GCDataLock` relocation and atomic 32-bit compare-exchange/exchange-add
sequences for i386 and AMD64. The variants are valid only in MTA mode and no longer
depend on a legacy system-inline body.

`System 8` and `System 9` safe-region transitions are lowered to MIR for x86 and
AMD64. Local labels and branches, atomic compare-exchange/exchange-add, GC data
relocations, TLS access, and the `WaitForGC` safepoint call are explicit operations
with verified effects. In STA mode both transitions lower to an explicit no-op.
Both variants have been removed from the MTA assembly cores.

`System 0`, `System 1`, and `System 2` are generated for x86 and AMD64. Variant zero
is the neutral system operation. Variants one and two select minor or full
collection through `RuntimeOperation::Collect`; MTA acquires the GC lock atomically,
and both architectures preserve the managed object register across the call. Their
four STA/MTA assembly implementations have been removed.

`XCreateR` is lowered for x86 and AMD64 through `AllocatePermanent`. The allocation
size is overflow-checked, pointer-size scaled, target-aligned, and the VMT remains a
linker relocation. `LLoadSI`, `LoadSI`, and `XLoadArgFI` preserve the distinct long,
signed integer, cached-argument, stack-slot, and frame-slot semantics on both
architectures. Their x86 and AMD64 assembly blocks have been removed.

`NewIR`, `NewNR`, `XNewNR`, `CreateR`, `CreateNR`, and `XCreateR` share the generic
`AllocationSpec`. It records fixed versus dynamic sizing, reference versus binary
payloads, permanent allocation, alignment-checked sizes, payload masks, element
width, and the concrete symbolic VMT reference. The x86-family selector owns only
register assignment, address forms, runtime-call encoding, and relocation spelling.
Because EIR already resolved the symbolic operand, generated allocation code uses
`ModuleReferenceValue`; it never converts the VMT back to the legacy argument-one
or argument-two relocation marker.

`XDispatchMR` is described by a target-independent dispatch contract. The contract
separates direct and virtual targets, fixed and variadic signatures, direct overload
tables and receiver-provided table chains, and the alternative VMT table. Argument
origin and fixed arity are derived from the message bit fields, not from legacy
inline numbers. `VMTLayoutSpec` owns the VMT header, parent link, method-table entry,
and overload metadata entry sizes used by every backend.

The fixed, variadic, direct-list, and receiver-list forms are generated for i386 and
AMD64. `DispatchControlFlow` describes list traversal, overload traversal, argument
matching, parent traversal, success, failure, and indirect transfer. The variadic
sentinel loop is represented by explicit EIR blocks instead of being hidden inside a
target emitter. `DispatchFrameLayout` assigns logical object, list-index, signature-
cursor, and argument-count slots without encoding a pointer size.

The x86-family selector consumes the EIR blocks and terminators and owns only the
physical registers, scaled addressing, stack displacements, relocations, branches,
and instruction selection. The generated paths preserve cached managed arguments,
resolve MData and `VOIDPTR` through logical relocations, match nil and inherited
argument types, restore the message on fallthrough, and branch to the selected
direct target. `0FA`, `5FA`, `9FA`, and `AFA` are registered as migrated and have
been removed from both x86-family STA cores. The rebuilt core binaries and the AMD64
and i386 system suites validate the absence of the four assembly bodies. ARM64 and
PPC64LE keep their architecture-specific definitions until those JIT backends gain
physical EIR selectors; their explicitly unfinished dispatch bodies are not a
semantic reference.

`DispatchMR` uses the same overload and argument-matching graph, but its EIR keeps
the selected method slot distinct from a direct code pointer. The success path loads
the receiver VMT, optionally selects the hidden method table from the typed VMT size
and entry layout, resolves the final code pointer, and only then performs the
indirect branch. The x86-family selector realizes this with an indexed load followed
by the existing register branch, so no architecture opcode was added solely to
imitate the legacy memory-jump spelling. Normal `0FB/5FB` and alternative-table
`6FB/BFB` are registered as migrated and removed from the i386 and AMD64 STA cores.
Both cores rebuild without those variants, AMD64 passes the system suite, and i386
reaches its documented post-suite shutdown failure.

`VCallMR` (`0xFC`) and `VJumpMR` (`0xEC`) share the target-independent virtual
method contract in `method.h`. The EIR represents receiver-VMT loading, a symbolic
VMT/HMT method-offset relocation, optional hidden-table selection, final code-
pointer resolution, and call or tail transfer as distinct operations. Method
offsets remain linker symbols carrying the class and message identity; no backend
converts them into an untyped integer before relocation.

The x86-family selector lowers that contract with the ELENA managed ABI, independently
of the host external ABI. Indirect calls conservatively declare heap reads and
writes, safepoint and exception effects; the object register remains the managed
receiver. Both call and jump load the `address` field selected by
`VMTTableField::FirstMethod`. This intentionally fixes the legacy `VJumpMR` bodies,
which used `findMethodOffset` but transferred through the entry's `message` field.
Normal variant `0` and alternative-HMT variant `6` are registered as migrated and
removed from the i386 and AMD64 cores. ARM64 and PPC64LE retain their bodies until
their physical selectors consume the same EIR contract.

All compiler and VM build descriptions, including ARM64, PPC64LE, macOS, BSD, and
Visual Studio projects, compile the generic method contract even though only i386
and AMD64 currently select machine instructions for it. Both x86-family cores
rebuild without the eight legacy blocks. The AMD64 STA suite passes; i386 reaches
its documented post-suite shutdown failure. AMD64 MTA passes and exits normally;
i386 MTA reaches the passing marker but has a nondeterministic shutdown failure.

`CallR` (`0xB0`), `CallVI` (`0xB1`), `JumpVI` (`0xB5`), `JumpMR` (`0xED`), and
`CallMR` (`0xFD`) share the generic managed-method transfer contract. Symbol calls
carry a typed code reference; indexed transfers resolve the address field from the
receiver VMT using `VMTTableField::FirstMethod` and `EntryEnd`; statically selected
methods carry the class, message, transfer kind, and VMT/HMT selection through EIR.
The x86-family selector emits relative managed calls or tail transfers and declares
the same heap, safepoint, and exception effects as indirect managed calls.

HMT direct calls use `HMTMethodAddress`, a distinct linker relocation resolved from
the hidden entry's address field. They no longer reinterpret `HMTMethodOffset` as a
code address, as the legacy `loadMROp` path did. All five e-codes are registered as
migrated and their ten i386/AMD64 STA blocks are removed. Both cores rebuild without
the blocks; the AMD64 system suite passes and i386 passes every test body before its
documented shutdown failure. ARM64 and PPC64LE retain their bodies until their
physical selectors implement this contract.

`MovEnv` (`0x05`), `LoadV` (`0x0C`), `XCmp` (`0x0D`), `PeekR` (`0x84`), and
`StoreR` (`0x85`) form the runtime-data and module-reference batch. `MovEnv` uses a
typed `SystemEnvironment` runtime-data relocation, with the linker selecting the
platform-width rdata relocation. `LoadV` composes the argument bits from the current
message with the action bits stored in the receiver, while `XCmp` compares that
stored message as a 32-bit value on both managed ABIs. The physical MIR includes a
real register `Or` operation; no arithmetic substitute is used.

Module static loads and stores first materialize the typed module-reference address
and then perform a managed-word load or store. This gives i386 and AMD64 one semantic
lowering while retaining their different address widths and encodings. All five
e-codes are registered as migrated, their ten legacy blocks are removed, and tests
verify the MIR sequence, exact machine bytes, relocation kind, and relocation slot.
Both AMD64 suites pass and exit normally. Both i386 suites reach the passing marker
before their distinct shutdown faults. No legacy fallback is restored to conceal
either failure.

Parameterized scalar operations `Shl`, `Shr`, `MovN`, `AddN`, `SubN`, `AndN`,
`OrN`, `MulN`, `CmpN`, `MovM`, `SetR`, and `XSaveN` are decoded into EIR operations
over logical managed locations. `SetR`, `PeekR`, and `StoreR` carry the actual
module reference through `ModuleReferenceValue`; they no longer encode a source
argument ordinal for the linker to reinterpret. Shift-count masking is part of the
generic e-code contract, while register width and instruction spelling remain in
the physical selector.

Managed slot operations `SaveDP`, `StoreFI`, `SaveSI`, `StoreSI`, `XFlushSI`,
`XRefreshSI`, `PeekFI`, `PeekSI`, `GetI`, `XStoreI`, `LSaveDP`, `LLoadDP`,
`LLoadSI`, `LoadSI`, `XLoadArgFI`, `SetDP`, `SetFP`, `SetSP`, `LoadDP`, and
`XCmpDP` are normalized and lowered to EIR before x86-family selection. The generic
target contract selects cached arguments versus stack slots and records signed,
native-word, reference, and 64-bit accesses explicitly. The i386 selector alone
decomposes 64-bit values into its managed low/high register pair; AMD64 selects a
single 64-bit location. None of these e-codes remains in the four x86-family core
assembly files.

`ExtOpenIN` and `ExtCloseN` external frames are generated from one parameterized
EIR operation for x86 and AMD64. The selector saves the platform nonvolatile state,
preserves Windows home arguments or System V register arguments, links and restores
the GC-visible frame chain through STA data or MTA TLS, initializes local slots, and
restores the native ABI frame. All twelve open variants and both close variants have
been removed from the four x86 assembly cores. MTA Mach-O remains rejected until a
native current-thread TLS access contract is available. `FrameEIRProvider` aligns
managed slots and raw storage from `TargetSpec::managedABI`; the same normalized
layout is used for scope accounting and physical selection, so open and close remain
symmetric for odd argument counts and non-aligned local sizes.

External-frame closing follows the managed links emitted by opening before selecting
the saved native frame. It does not advance linearly from `RBP`/`EBP`: the raw local
area is variable-sized, and treating it as a fixed scaffold restores the thread frame
and callee-saved registers from unrelated slots after a worker returns.

`ExternalFrameLayoutProvider` is the single x86-family description of the saved
register order, caller-stack versus saved-register argument storage, and managed
frame scaffold. `XLoadArgFI` derives its frame base from this target ABI description;
it never uses host preprocessor macros or a duplicated displacement formula. This is
required for cross-compiling Win64 from a System V host and for zero-sized external
frames, where the former i386 formula selected the slot eight bytes too early.

ELF load segments preserve distinct file and memory sizes. TLS program headers map
the actual TLS bytes inside the data load segment instead of the end of that segment.

## Validation

From `iteration47`:

```sh
make clean_codegen_tests codegen_tests
make clean_elc_i386
CCACHE_DISABLE=1 make elc_i386
make clean_elc_amd64
CCACHE_DISABLE=1 make elc_amd64
```

Rebuild the matching core binary after removing an assembly block, then compile and
run `tests60/system_tests`. MTA migrations additionally rebuild `corex60_lnx.bin`
and run `tests60/mta_tests`. The asynchronous test suite is not part of this gate.

The AMD64 and i386 STA system suites pass and exit with status zero after these
migrations. The dedicated `tests60/mta_tests` concurrency suite passes on both
architectures with five rounds and one hundred critical-section increments per worker,
including thread-table churn, concurrent full/minor collection, and TLS. Its original
twenty-round profile completes the synchronization-event tests but exceeds the
available thirty-second validation window in the 160,000-operation critical-section
stress test.

`TryLock`, `FreeLock`, `PeekTLS`, and `StoreTLS` are absent from both x86-family base
assembly cores as well as the MTA overlays during this validation. `pthread_join` is
called with its required result-pointer argument, so it no longer writes through an
undefined second System V argument when a test or application joins a worker.
