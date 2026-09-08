# Local experiment record

This file records measured runs only. A smaller raw archive alone is **not** a
Hutter Prize score: the prize score includes the self-extracting decompressor
and must be built and packaged under the competition rules.

## Evidence-based upgrade policy (2026-08-31)

The next upgrades are constrained by verified public implementations and
decoder-causal measurements. The public `cmix-lex` result combines a
reversible `payload_lex` transform, `fxcm_v26`, benchmark-specific article
ordering, and disk-backed PPMD; it reports **109,650,047 total bytes** with an
exact 1,000,000,000-byte SHA-256 restoration. Source:
https://github.com/blahem/cmix-lex

The public `fx2-cmix` record describes speed gains from removing weak model
families, skipping low-error mixer updates, and simplifying the executable,
while spending the saved budget on the stronger FXCM/text model. Source:
https://github.com/kaitz/fx2-cmix

PAQ's technical documentation establishes the relevant coding mechanism:
models predict the next bit, their stretched probabilities are mixed, and a
match model is a probability feature rather than an explicit source pointer.
Source: https://github.com/hxim/paq8px/blob/master/DOC

Therefore this project will no longer promote a new predictor merely because
it is causal or theoretically plausible. A candidate must first improve
measured cross-entropy or archive bytes on an identical complete-page gate,
then pass exact SHA-256 restoration, and finally justify its executable and
runtime cost. Match-history variants that fail this gate stay opt-in only.
Recent neural/SSM papers are research leads, not Hutter evidence: for
example, StateSMix reports enwik8 results and explicitly studies a different
scale/runtime regime (https://arxiv.org/abs/2605.02904).

## Raw predictor A/B: `prof_input/input2`

Input SHA-256:

```
7ca547e67ca14341250c969caf5fa9d3c21d5966579b03b111d6b80540f99048
```

| Predictor | Archive | Decode result | Notes |
|---|---:|---|---|
| local `fx2-cmix` baseline | 180,656 B | hash matched | reference run |
| `cmix-lex` at `370e698` | **180,476 B** | hash matched | 180 B smaller |

The `cmix-lex` encode/decode times were 302.10 s / 284.82 s. Its raw test
binary was 325,408 B, versus 283,824 B for the local fx2 binary. Those binary
sizes are not comparable prize packaging sizes because this lab build used the
system linker after `lld` was unavailable, with no PGO or UPX packaging.

## FXCM output-budget ablation: 544 of 560 inputs

The outer mixer was compiled with `FXCM_OUTPUT_LIMIT=544`, omitting the tail
16 FXCM inputs while leaving the codec state transitions symmetric.

| Variant | Mixer inputs | Archive | Raw executable | Encode / decode |
|---|---:|---:|---:|---:|
| cmix-lex baseline | 590 | 180,476 B | 325,408 B | 302.10 s / 284.82 s* |
| FXCM-544 | 574 | 180,476 B | 325,408 B | 451.27 s / 440.64 s** |

The FXCM-544 restored hash matched the input SHA-256 exactly. It is a neutral
archive/executable result, not a promoted candidate. The time comparison is
not valid because this candidate used the disk-backed PPM configuration while
the earlier raw baseline used the memory-backed configuration. It establishes
that the omitted tail inputs had no effect on this corpus; the next bisection
must find a lower useful limit under matched build settings.

## FXCM output-budget ablation: 480 of 560 inputs

| Variant | Mixer inputs | Archive | Encode / decode |
|---|---:|---:|---:|
| cmix-lex baseline | 590 | 180,476 B | 302.10 s / 284.82 s* |
| FXCM-480 | 510 | 182,097 B | 445.36 s / 465.99 s** |

The restored output SHA-256 exactly matched the input, so the test is valid.
However it costs 1,621 B, therefore FXCM-480 is rejected. Together with the
FXCM-544 neutral result, this identifies a useful output boundary but does not
yet reduce packaged executable size: actual payload reduction must remove a
whole redundant FXCM module, rather than merely omit its outer-mixer inputs.

## Structural line-shape mixer: prefix-hash variant

This candidate added a mixer keyed by an existing line-class detector and an
eight-byte line-prefix hash. The codec stayed lossless (the decoded SHA-256
matched), but the archive was 180,485 B: 9 B worse than baseline. It is
rejected. The high-cardinality prefix hash fragmented online mixer learning;
the follow-up test retains only the eight line classes and bit position.

## Structural line-shape mixer: bounded line-class variant

The information screen estimated a 13,222 B *gross* byte-entropy ceiling for
line class. The actual cmix-lex residual already captures that information:
the bounded 2,048-state mixer produced 180,484 B, 8 B worse than baseline.
It was rejected at the archive gate without a decode run, because an
archive-size loser cannot advance to total-score or full-corpus testing.

## Full pipeline status

The 1,000,000,000-byte `data/enwik9` corpus is present and its `-e` path is
being prepared using cmix-lex's self-extracting payload mechanism. The payload
packaging process is currently computing the compressed dictionary and article
order. A full archive result will be logged here only after the process exits
and an independent decompression SHA-256 check succeeds.

## Decoder-causal entropy trace (diagnostic, 2026-08-26)

`src/coder/encoder.cpp` now has an opt-in `CMIX_ENTROPY_TRACE` build flag. It
records the actual quantized range-coder cross-entropy in one-million-byte
blocks without changing predictor state or archive decisions. The hot path uses
a static lookup table rather than calling `log2` per bit. The default build is
unchanged (`CMIX_ENTROPY_TRACE=0`).

A traced 100,000-byte smoke run produced one complete trace row:

```text
offset=0  coded_bits=800048  cross_entropy=1.952151806 bits/byte
archive=24441 B  (trace payload before decode validation=24404 B)
```

The encoder completed and the trace file was written. The WSL host became
abnormally slow during the subsequent decode (several minutes for this small
sample), so that run was stopped before its exact SHA check and is **not** an
accepted compression result. The complete-page trace was likewise stopped
early for the same operational reason. The next use should run the traced
binary on a stable WSL-native host or a deliberately bounded sample, then
record archive, decode SHA-256, wall time, and RSS together.

## Output-neutral mixer lookup reduction (2026-08-26)

The generic `Mixer` now retains the `ContextData*` selected by `Mix()` and
reuses it in the immediately following `Perceive()` call. The previous code
performed a second lookup of the same context for every non-skipped mixer
update. No map insertion, floating-point operation, weight-update order, or
probability is changed; the old lookup remains as a fallback for an unusual
caller that perceives without mixing first.

Both the normal build and the `CMIX_ENTROPY_TRACE=1` diagnostic build compiled
successfully in the WSL-native audit copy. This is a source-level optimization,
not yet a measured speed claim; the next short exact gate must compare archive,
restored SHA-256, and wall time against the frozen reference.

The first exact candidate gate used the modified mixer on the first 10,000 bytes
of `prof_input/input2` (input SHA-256
`b5506c1f5dc08fbb5b7378d93a945660f3c99ee41ff65298ffe47b6ca9e145ff`). Encode
and decode both completed within the bounded 180-second per-command limit; the
restored SHA-256 matched exactly and the archive was **2,879 B**. This confirms
lossless state synchronization, but no speed or ratio improvement is claimed
until a same-build control is run on the same gate.

## PPMD disabled-byte path precomputation (2026-08-26)

PPMD's vocabulary filter previously recomputed eight tree-node indices and bit
masks for every disabled byte on every `ByteUpdate()`. Those values depend only
on the static vocabulary, so the constructor now precomputes compact paths and
the update loop reuses them. The PPMD masses, tree updates, branch conditions,
and iteration order are unchanged. The full cmix-lex target compiled
successfully after this change; a ratio/speed measurement is still pending and
must use a same-build short control.

The combined mixer-cache plus PPMD-path build also passed the bounded 10,000-
byte encode/decode gate. It produced **2,879 B** and restored the exact input
SHA-256 `b5506c1f5dc08fbb5b7378d93a945660f3c99ee41ff65298ffe47b6ca9e145ff`.
This confirms losslessness only; the next measurement is a same-build control
timing comparison.

That same-build A/B timing comparison is now complete:

| Build | Encode | Decode | Archive | Restored SHA |
|---|---:|---:|---:|---|
| Control (`CMIX_MIXER_CONTEXT_CACHE=0`, `CMIX_PPMD_PRECOMPUTE_PATHS=0`) | 35.60 s | 35.70 s | 2,879 B | matched |
| Candidate (both optimizations enabled) | **26.57 s** | **32.41 s** | **2,879 B** | matched |

On this small gate the candidate is 25.4% faster to encode and 9.2% faster to
decode, with identical output. These percentages are preliminary because model
startup dominates a 10 KB run; a larger stable gate is required before making
a full-corpus runtime estimate.

## Isolated speed gates (2026-08-26)

To separate the two output-neutral changes, each was built alone and tested on
the same 10,000-byte input. Both variants produced **2,879 B** and restored
SHA-256 `b5506c1f5dc08fbb5b7378d93a945660f3c99ee41ff65298ffe47b6ca9e145ff`.

| Variant | Encode | Decode | Archive |
|---|---:|---:|---:|
| Mixer cache only | 28.61 s | 34.47 s | 2,879 B |
| PPMD path precompute only | 38.08 s | 28.84 s | 2,879 B |

The 10 KB control used earlier was 35.60 s / 35.70 s. Because startup and WSL
page-fault variance dominate this sample, these isolated numbers are signals,
not final speed claims. They do show that the mixer cache primarily helps the
encode path while the PPMD path change primarily helped this decode run; a
larger stable gate is required before selecting a submission configuration.

The larger 100,000-byte same-build A/B also completed losslessly. Both archives
were **24,441 B** and both restored SHA-256
`53f03c5afe744eb6cb191721868f9212e7e226b40be76bf78ad49a30ea3cb3f1`.

| Build | Encode | Decode | Archive |
|---|---:|---:|---:|
| Control (both gates disabled) | 427.46 s | 405.63 s | 24,441 B |
| Candidate (both gates enabled) | **376.92 s** | 418.67 s | 24,441 B |

The candidate is 11.8% faster to encode but 3.2% slower to decode on this
100 KB gate. This is a real speed signal, but not a submission configuration:
the decode regression and small-sample variance require a stable larger gate
before promotion.

## 50 KB confirmation and decode-order check (2026-08-26)

The combined candidate and control were rerun on the first 50,000 bytes. Both
archives were **13,413 B** and both restored SHA-256
`fac203a8ab2f3c8b4f8b6a50f4bd649638c51b8901567f70e438618e7aa0aacf`.

| Build | Encode | Decode (candidate first) |
|---|---:|---:|
| Control | 183.23 s | 183.52 s |
| Candidate | **170.67 s** | 220.10 s |

Because disk-backed PPMD is sensitive to page-cache order, the decoders were
then run in reverse order using the already-created archives. That produced
190.96 s for control-first and 203.29 s for candidate-second. The order check
reduces, but does not eliminate, the apparent candidate decode penalty. The
candidate remains experimental: encode is consistently faster, while decode
needs a stable larger-host measurement before promotion.

## Parameterized PPMD speed/ratio tiers (2026-08-26)

`Predictor::AddPPMD()` now takes `PPMD_ORDER` and `PPMD_MEMORY_MB` compile-time
parameters. Defaults remain the recorded cmix-lex values (`25` and `14000`),
so the reference configuration is unchanged. A development build can now
measure smaller states such as `-DPPMD_ORDER=16 -DPPMD_MEMORY_MB=4096` without
editing source or confusing the result with the baseline. Both the default and
4 GB/order-16 variants compiled successfully. Smaller PPMD tiers may improve
runtime and memory but can lose compression; they require an exact short A/B
before any promotion.

The first reduced-state gate (`PPMD_ORDER=16`, `PPMD_MEMORY_MB=4096`, with both
output-neutral optimizations enabled) completed exactly on the 10,000-byte
input. It produced **2,879 B** and restored SHA-256
`b5506c1f5dc08fbb5b7378d93a945660f3c99ee41ff65298ffe47b6ca9e145ff`.
Timing was **29.75 s encode / 24.94 s decode**. This is slower to encode than
the 25/14,000 combined candidate on that gate but substantially faster to
decode; a larger gate with peak RSS is needed before judging the tier.

The larger 50,000-byte reduced-state gate also completed with exact
round-trip verification. Input SHA-256 was
`fac203a8ab2f3c8b4f8b6a50f4bd649638c51b8901567f70e438618e7aa0aacf`; the
decoded output had the same hash and the archive was **13,413 B**, identical
to the default-state candidate and control on this gate. The order-16/4 GB
build took **187.21 s to encode** and **207.67 s to decode**, with maximum
resident set size reported as **5,725,320 KiB (~5.46 GiB)** for encode and
**5,724,792 KiB (~5.46 GiB)** for decode. Thus the smaller configured PPMD
heap did not produce a small process RSS in this environment; it is not yet a
credible four-hour/full-corpus speed configuration. Treat this as a
losslessness and accounting result, not a projected enwik9 score.

## Profile-guided layout gate (2026-08-26)

The existing PGO script was run only on its bundled 51-KB training corpus; no
full enwik9 pass was started. The resulting profile-use/LTO binary was then
compared against a freshly built non-PGO binary with the same default
parameters on the identical 50,000-byte gate. Both produced **13,413 B** and
both decoded to SHA-256
`fac203a8ab2f3c8b4f8b6a50f4bd649638c51b8901567f70e438618e7aa0aacf`.

| Build | Encode | Decode | Peak RSS | Archive |
|---|---:|---:|---:|---:|
| Non-PGO control | **2:18.16** | **2:58.48** | 5,725,680 KiB | 13,413 B |
| PGO/profile-use | 3:00.42 | 2:58.47 | 5,725,864 KiB | 13,413 B |

On this exact gate PGO was **30.6% slower to encode** and neutral to decode,
with no memory reduction. It is rejected as a speed improvement. The profile
training itself also spent roughly 27 minutes in dictionary pretraining, so
it is not an efficient iteration loop for this laptop. Keep the PGO script
available for a stable high-end host, but do not use its binary as the local
development configuration.

## PPMD residency-cadence gate (2026-08-26)

The disk-backed PPMD heap currently issues `MADV_DONTNEED` every 5,000 input
bytes. A compile-time **1 MiB** cadence (`CMIX_PPMD_REMAP_INTERVAL=1048576`)
was tested on the same 50,000-byte input with order-16/4-GB PPMD. It produced
the identical **13,413 B** archive and restored SHA-256
`fac203a8ab2f3c8b4f8b6a50f4bd649638c51b8901567f70e438618e7aa0aacf`.

The encode took **3:07.02** with **5,725,908 KiB** peak RSS; decode took
**3:36.27** with **5,725,496 KiB** peak RSS. This is effectively no encode
change and a decode regression versus the 5,000-byte cadence. Reject the
larger cadence for now; the full-corpus speed bottleneck is elsewhere (likely
PPMD pointer-chasing/page faults and model updates), and any future paging
change must be tested on a longer, stable gate with an explicit RSS limit.

## Anonymous PPMD heap gate (2026-08-26)

`CMIX_PPMD_MMAP_TO_DISK` is now a compile-time switch. The default remains
the recorded disk-backed mapping (`1`). Setting it to `0` uses a zero-filled
anonymous mapping, so a bounded smaller PPMD heap can be tested without file
page faults; the model state and archive format are otherwise unchanged.

The first attempted RAM-only measurement was invalid: the WSL audit copy did
not yet contain this switch, so its binary still used disk-backed PPMD. Those
timings are superseded and are not reported as RAM-only results. The corrected
order-16/4-GB build produced **13,413 B** and restored SHA-256
`fac203a8ab2f3c8b4f8b6a50f4bd649638c51b8901567f70e438618e7aa0aacf`.

| Build | Encode | Decode | Peak RSS | Archive |
|---|---:|---:|---:|---:|
| Disk-backed, 5-KB cadence | **3:07.21** | **3:27.67** | ~5,725,320 KiB | 13,413 B |
| Anonymous RAM, 4-GB heap | 3:18.52 | 3:26.05 | ~5,725,996 KiB | 13,413 B |

RAM-only decode was essentially neutral/slightly faster, but encode was 6.0%
slower and total time was about 2.5% worse. It is rejected as the default
speed configuration; retain the switch only for future larger-host tests.

## PPMD update-thinning attempt (2026-08-26)

A causal `CMIX_PPMD_UPDATE_INTERVAL` fast branch was attempted to reduce the
number of expensive byte-model refreshes. Interval 4 crashed during the
first 10-KB gate, even after retaining ByteMixer state updates on every byte;
the implementation has therefore been removed. No archive, timing, or speed
claim is attached to this path. The reference byte-update schedule is restored
in source.

## Fast causal byte-model profile (2026-08-26)

This is a real opt-in build intended to buy speed, not a placeholder or a
claimed Hutter submission. `CMIX_FAST_BYTE_MODEL=1` replaces the expensive
PPMD byte predictor with a decoder-synchronised order-1 transition table;
`CMIX_FAST_BYTE_MIXER=1` bypasses the recurrent ByteMixer and normalises the
already supplied byte-model mixture directly. The normal build remains
unchanged when both switches are `0`.

Build flags used for the bounded audit copy were:

```
-DSEED=923 -DUPDATE_LIMIT=3000 -DPPMD_ORDER=16 -DPPMD_MEMORY_MB=512
-DCMIX_FAST_BYTE_MODEL=1 -DCMIX_FAST_BYTE_MIXER=1
```

On the 50,000-byte gate (input SHA-256
`fac203a8ab2f3c8b4f8b6a50f4bd649638c51b8901567f70e438618e7aa0aacf`), the
fast profile produced **13,533 B** and decoded to the identical SHA-256.
Encode was **2:55.41** and decode **2:25.23**, with peak RSS about
**5,711,864 KiB**. The reduced-state reference on the same gate was 13,413 B;
thus the profile traded 120 archive bytes for roughly 6.3% faster encoding and
30% faster decoding. These are measured gate results, not a full-corpus
projection.

A complete-page 941,724-byte gate (`prof_input/input2`, SHA-256
`7ca547e67ca14341250c969caf5fa9d3c21d5966579b03b111d6b80540f99048`) was
started to check that the profile remains causal on a realistic page boundary.
It reached **25.17%** after about **24 minutes**, at roughly **7.4 GiB RSS**,
without producing an archive. It was deliberately terminated because that
throughput already projects to well over the four-hour full-enwik9 objective;
no score or compression claim is attached to the partial run. Do not start a
billion-byte run with this profile on the laptop.

The result is therefore useful as an implementation/architecture finding:
the simple causal byte path is materially faster on the tiny exact gate, but
the remaining predictor state and memory footprint still dominate on a larger
page. Further work should profile and reduce those structures before any
attempt to trade more compression ratio for speed.

## PPMD transparent-huge-page hint gate (2026-08-26)

`CMIX_PPMD_HUGEPAGE=1` was added as an opt-in, semantics-preserving
`madvise(MADV_HUGEPAGE)` hint for the pointer-heavy PPMD heap. On the exact
50,000-byte gate it produced the same **13,413 B** archive and restored the
input SHA-256
`fac203a8ab2f3c8b4f8b6a50f4bd649638c51b8901567f70e438618e7aa0aacf`.

| Build | Encode | Decode | Peak RSS | Archive |
|---|---:|---:|---:|---:|
| Existing control | **2:18.16** | **2:58.48** | ~5,725,680 KiB | 13,413 B |
| Huge-page hint | 2:46.22 | 2:49.79 | ~5,731,024 KiB | 13,413 B |

The hint made encoding about 20% slower and decode only about 5% faster on
this host, so it is rejected as a default speed improvement. The compile-time
switch remains available for testing on a different kernel or hardware.

## ContextMap3 inlining gate (2026-08-26)

The gprof profile identified `ContextMap3::mix()` as the third-largest named
hotspot at **5.27%** of sampled CPU time. An opt-in
`CMIX_INLINE_CONTEXT_MIX=1` build allows this function to inline; the default
keeps the original `noinline` attribute. On the matched 50,000-byte,
order-16/64-MiB anonymous-heap gate, the inlined encoder produced **13,413 B**
with archive SHA-256
`b6bbd5b9e0f445e147d3650d7c27ed1234cae6274819536b3474f01d44fd2e0a`.
Encode time was **50.77 s** versus **52.62 s** for the control (a **3.5%**
improvement) at **5,726,428 KiB** peak RSS. The inline decoder restored
50,000 bytes in **51.39 s** at **5,726,252 KiB** peak RSS, with SHA-256
`fac203a8ab2f3c8b4f8b6a50f4bd649638c51b8901567f70e438618e7aa0aacf`, exactly
matching the input. A matched control-decoder timing is still needed before
claiming a total encode/decode speed improvement.

The matched control decoder subsequently took **50.14 s** at **5,726,240 KiB**
and restored the same SHA-256. Thus the inline path is 2.5% slower to decode,
but its 1.85-second encode saving leaves a provisional **0.60-second (0.6%)**
combined gain on this tiny gate (102.16 s versus 102.76 s). This is too close
to gate noise to promote without a larger complete-page timing comparison;
keep `CMIX_INLINE_CONTEXT_MIX=1` opt-in for now.

The next candidate is isolated as `CMIX_INLINE_DIRECT_STATE=1`, forcing
`DirectStateMap::set()` to inline. This targets the second-largest profile
hotspot (**10.57%**) and should be evaluated together with the already-tested
context-mix inline path, then compared against the 50.77-second inline control.

That matched test produced **13,413 B** with the same archive hash, but encode
took **53.77 s** versus **50.77 s** for the context-inline control (a **5.9%**
regression). The compiler's normal inline decisions are therefore better on
this host; `CMIX_INLINE_DIRECT_STATE=1` is rejected.

## E1 checksum SIMD scan (correctness passed; speed conditional, 2026-08-26)

The profile's dominant named hotspot is `E1<14,128>::get` at 41.99% of
sampled CPU time. An opt-in `CMIX_E1_SIMD_SCAN=1` path now compares the 14
contiguous 16-bit checksums with two SSE2 loads, then falls back to the
existing priority scan on a miss. It preserves lowest-index match order,
recency updates, replacement selection, and zero-initialization. The default
remains `0` because the exact gate confirmed correctness but the speed result
was input-size dependent; a repeated larger-corpus timing gate is required
before changing the default.

## Macro predictor portfolio profiles (pending bounded A/B gate, 2026-08-26)

`src/predictor.cpp` now accepts `CMIX_PROFILE` while preserving the default
profile at `0`:

| Profile | Construction |
|---:|---|
| 0 | Published cmix-lex model portfolio |
| 1 | Profile 0 without the four double-indirect models |
| 2 | Profile 1 without the five standalone match models |
| 3 | Profile 2 without the word-context portfolio |
| 4 | Profile 3 without the bracket auxiliary direct/indirect pair |

These are construction-time gates, not per-bit branches: the existing model
loops naturally contain fewer objects. The transform, range coder, and all
remaining models are unchanged. Each profile must be tested against profile 0
on the identical complete-page corpus, recording archive bytes, executable
size, encode/decode wall time, peak RSS, and restored SHA-256. No profile is
promoted from a speed result alone; compression loss must be charged.

The bounded runner is `tools/run_profile_gate.sh`. From Administrator Ubuntu:

```bash
cd /home/alexj/cmix-lex-audit
chmod +x tools/run_profile_gate.sh
tools/run_profile_gate.sh prof_input/input2 profile-gates
```

It refuses inputs over 20 MB and runs no full-corpus benchmark.

Set `PROFILE_LIST` to control order or rerun a subset, for example
`PROFILE_LIST="1 0" tools/run_profile_gate.sh ...` for a matched reverse-order
check.

The runner also accepts `PPMD_ORDER` and `PPMD_MEMORY_MB` environment
overrides. The next macro tier should compare profile 0 and profile 2 under a
smaller matched PPMD state, for example:

```bash
PPMD_ORDER=12 PPMD_MEMORY_MB=32 PROFILE_LIST="0 2" \
  tools/run_profile_gate.sh prof_input/input2 profile-gates-ppmd12
```

This is deliberately an A/B pair: a smaller PPMD state is useful only if its
runtime/RSS reduction outweighs the measured archive loss.

The runner also accepts `PPMD_ENABLED=0` for a full PPMD-off ablation. In that
mode the `PPMD` object retains a deterministic uniform permitted-byte prior,
but allocates no adaptive PPMD tree. This is the decisive macro test for
whether PPMD's CPU/RAM cost is justified; it is not a proposed final codec
until its archive and exact SHA results are measured.

`PPMD_UPDATE_PERIOD` is another opt-in macro gate. Period `1` is the current
behavior; period `2` or `4` updates the adaptive tree only on every second or
fourth byte while resetting the byte-boundary bit state on skipped updates.
The next bounded test should compare Profile 2 at periods 1, 2, and 4. This
tests compute reduction without removing PPMD's probability stream entirely.

The runner's `E1_SIMD_SCAN=1` option enables the two-load SSE2 checksum scan in
`E1::get()`, the largest gprof hotspot. It is disabled by default and must be
tested against Profile 4 with the same archive/SHA gate.

The first PPMD-off gate compiled successfully (only ablation-only unused-state
warnings before cleanup) and restored the exact input SHA. Its archives were
**181,937 B** (profile 0) and **182,034 B** (profile 2), approximately 1.33 KB
larger than the corresponding PPMD12/32 archives. Timing and RSS are still
required before accepting or rejecting this macro path.

The completed PPMD-off timings were:

| Profile | Encode | Decode | Combined | Executable | Peak RSS (encode) |
|---:|---:|---:|---:|---:|---:|
| 0 | 10:40.20 | 10:34.24 | 21:14.44 | 295,848 B | 8,227,112 KiB |
| 2 | 9:54.76 | 10:05.65 | 20:00.41 | 291,672 B | 8,200,252 KiB |

Against the matched PPMD12/32 pair, profile 2 with PPMD disabled is about 3.4%
faster overall, uses about 17 MiB less RSS, and has a 24,776-byte smaller
executable, but its archive is 1,330 B larger. It is retained as a `FAST`
ablation; profile 2 with PPMD enabled remains the `BALANCED` baseline because
the speed gain is modest and the full-corpus ratio impact is unknown.

The PPMD update-period-2 gate (Profile 2, order-16/64-MiB PPMD) also restored
the exact input SHA, but produced **182,018 B** and took **10:21.27 encode +
10:39.61 decode = 21:00.88**. The matched period-1 Profile 2 control took
20:41.93 with a 180,706-byte archive. Period 2 is therefore both slower and
about 1.3 KB larger; update scheduling is rejected and period 4 will not be
run.

Profile 4 completed its bounded exact gate with archive **180,900 B** and the
same input SHA-256. Its timing/RSS remains to be collected; an ablation-only
warning about `update_counter_` was removed from the Windows source by
guarding that field to update periods greater than one.

The Profile 4 timing gate then completed at **10:15.33 encode + 10:16.18
decode = 20:31.51**, with **8,093,816 KiB** peak encode RSS and a **301,864 B**
executable. Relative to the matched Profile 2 control (20:41.93, 8,220,980
KiB, 316,448 B), Profile 4 is about 0.8% faster, uses about 124 MiB less RSS,
and reduces total archive-plus-executable size by 14,390 B while adding only
194 archive bytes. Profile 4 is now the provisional macro fast/balanced
candidate, pending a second complete-page confirmation.

The independent 51,052-byte raw-gate confirmation also restored the exact
input SHA. Profile 2 took **0:42.73 encode + 0:45.88 decode = 1:28.61**;
Profile 4 took **0:41.57 encode + 0:43.73 decode = 1:25.30**. Profile 4 was
therefore **3.7% faster**, used **123,940 KiB less peak RSS**, produced an
archive only **6 B larger** (6,143 B versus 6,137 B), and retained a 14,600-byte
smaller executable. Profile 4 is promoted as the development fast/balanced
baseline for the next macro experiment; it is not yet a full enwik9 score.

The PPMD12/32 profile pair completed with exact decoder checks. Profile 0 took
10:35.61 encode + 10:31.13 decode, with 8,244,284 KiB peak encode RSS;
profile 2 took 10:21.95 encode + 10:22.02 decode, with 8,217,356 KiB peak
encode RSS. Thus profile 2 was only about 1.8% faster overall and used about
26 MiB less RSS. Archive byte counts from this pair must still be collected
before judging the smaller PPMD state.

The pair's archives were **180,611 B** (profile 0) and **180,704 B** (profile
2), with exact round-trip checks enforced by the runner. Relative to the
order-16/64-MiB control, PPMD12/32 changed profile-0 archive size by only 66 B
and delivered only a 1.8% combined runtime reduction. PPMD state-size tuning
is therefore demoted; future speed work should target whole predictor stages.

The first complete-page profile matrix ran on `prof_input/input2` (941,724
bytes) with order-16/64-MiB anonymous PPMD and all micro-optimizations disabled.
Every profile restored the exact input SHA-256
`7ca547e67ca14341250c969caf5fa9d3c21d5966579b03b111d6b80540f99048`.

| Profile | Archive | Executable | Encode | Decode | Peak RSS (encode) |
|---:|---:|---:|---:|---:|---:|
| 0 | 180,545 B | 320,624 B | 11:40.69 | 12:36.64 | 8,247,720 KiB |
| 1 | 180,672 B | 319,488 B | 57:02.48 | 10:48.49 | 8,245,308 KiB |
| 2 | 180,706 B | 316,448 B | 10:18.20 | 10:26.07 | 8,220,768 KiB |
| 3 | 180,954 B | 307,840 B | 10:04.70 | 10:15.71 | 8,175,408 KiB |

Profile 1's 57-minute encode is an outlier relative to profiles 0, 2, and 3
and must be rerun before interpretation. Profiles 2 and 3 show a preliminary
14.6% and 16.3% combined-time improvement, respectively, with total
archive-plus-executable reductions of 4,015 B and 12,375 B on this gate.
These are gate signals only; no profile is promoted until the profile-1 timing
is explained and at least one additional complete-page run confirms the
ordering.

The profile-1 reverse-order rerun completed without the earlier outlier. It
also reconfirmed the same archive sizes and exact SHA-256 values:

| Profile | Encode | Decode | Combined | Peak RSS (encode) |
|---:|---:|---:|---:|---:|
| 0 | 10:40.49 | 10:42.89 | 21:23.38 | 8,247,840 KiB |
| 1 | 10:20.04 | 10:26.02 | 20:46.06 | 8,245,780 KiB |
| 2 | 10:21.65 | 10:20.28 | 20:41.93 | 8,220,980 KiB |
| 3 | 10:08.06 | 10:08.88 | 20:16.94 | 8,175,500 KiB |

Relative to profile 0, the stable combined-time improvements are 2.9% (profile
1), 3.2% (profile 2), and 5.2% (profile 3). Profile 2 is the provisional
strength/speed baseline; profile 3 is the fast ablation. These timings use the
development order-16/64-MiB PPMD configuration and must not be extrapolated to
the published full-memory Hutter configuration without a matched gate.

## Hot-path profile and E1 cache check (2026-08-26)

A symbolized gprof build of the default order-16/512-MiB audit configuration
was run on `cache-input50k`. The flat profile identified the dominant CPU
costs as:

| Function | Sampled CPU |
|---|---:|
| `fxcmv1::E1<14,128>::get` | **41.99%** |
| `fxcmv1::DirectStateMap::set` | **10.57%** |
| `fxcmv1::ContextMap3::mix` | **5.27%** |
| `Mixer::Mix` | **4.12%** |

The profile binary encoded the gate in **3:15.20** with **5,725,028 KiB**
peak RSS and a 13,413-byte archive. Based on that evidence, an opt-in
`CMIX_E1_RECENT_CACHE=1` path now checks `E1::get()`'s stored
second-most-recent slot before scanning all 14 entries. The change is causal
and preserves the same recency update as the original scan; the default value
is `0` until the exact archive/SHA gate is rerun.

The post-change exact run was compiled successfully, but WSL began returning
`Wsl/Service/E_ACCESSDENIED` when launching the process after the build. No
post-change archive, timing, or speed claim is made until that bounded SHA-256
gate can be rerun. Do not treat the cache check as promoted based on the
profile alone.

The gate was subsequently rerun from an Administrator-launched Ubuntu shell
using `PPMD_ORDER=16`, `PPMD_MEMORY_MB=64`, anonymous PPMD mapping, and
`CMIX_E1_RECENT_CACHE=1`. It produced **13,413 B**, encoded in **59.63 s** and
decoded in **50.62 s**, with peak RSS of **5,726,452 KiB** (encode) and
**5,726,248 KiB** (decode). Both decoded output and input had SHA-256
`fac203a8ab2f3c8b4f8b6a50f4bd649638c51b8901567f70e438618e7aa0aacf`.
This confirms lossless encoder/decoder synchronization for the opt-in path;
because it uses a reduced anonymous heap and has no matched control timing,
it is not yet a promoted speed result.

A matched control was then built with the same order-16/64-MiB anonymous
configuration and `CMIX_E1_RECENT_CACHE=0`. It produced the same **13,413 B**
archive and archive SHA-256
`b6bbd5b9e0f445e147d3650d7c27ed1234cae6274819536b3474f01d44fd2e0a`.
Encode timing was **52.62 s** for the control versus **59.63 s** for the E1
cache variant, making the opt-in change **13.3% slower**. It is rejected;
`CMIX_E1_RECENT_CACHE` remains disabled by default and should not be promoted
without evidence from a different architecture or host.

## E1 SIMD checksum-scan gate (completed; speed conditional)

The next single-variable experiment is `tools/run_e1_simd_gate.sh`. It compares
`CMIX_E1_SIMD_SCAN=0` with `=1` using Profile 4, identical PPMD settings, the
same complete-page input, and the same compiler. The SIMD path checks the same
14 16-bit checksums in two unaligned SSE2 loads, then uses the original scalar
replacement scan on misses. It preserves the first matching index, recency
update, checksum replacement, and seven-byte bit-history reset. Consequently
the candidate is accepted only if both archive length *and* archive SHA-256
match, and the decoded output SHA-256 matches the input. Timing is a separate
measurement and cannot override a byte mismatch.

Run from an Administrator Ubuntu shell (WSL-native checkout):

```bash
cd /home/alexj/cmix-lex-audit
bash tools/run_e1_simd_gate.sh prof_input/input2 e1-simd-gate
```

This is a bounded gate (input <=20 MB), not a full enwik9 result. The initial
matched run completed on 2026-08-27 from an Administrator Ubuntu shell. Both
variants restored the exact 941,724-byte input SHA-256
`7ca547e67ca14341250c969caf5fa9d3c21d5966579b03b111d6b80540f99048` and both
produced a 180,900-byte archive with identical archive SHA-256
`442f4e2e9d12d493645857b6ce8ed034b1a858228f1cfa8f9f4805438ca12a3e`.

Measured wall times and peak RSS were:

| Variant | Encode | Decode | Combined | Encode RSS | Decode RSS |
|---|---:|---:|---:|---:|---:|
| Scalar | 13:52.54 | 17:56.34 | 31:48.88 | 8,093,668 KiB | 8,092,972 KiB |
| SIMD | 13:37.49 | 13:45.19 | 27:22.68 | 8,093,556 KiB | 8,092,596 KiB |

The SIMD path was 1.8% faster for encode, 23.3% faster for decode, and 13.9%
faster combined on this larger gate, with no archive or SHA change. An
independent 51,052-byte repeat also passed archive/output SHA identity, but
measured 0:53.38 + 0:54.76 = 1:48.14 for scalar versus
0:54.53 + 0:54.01 = 1:48.54 for SIMD (0.37% slower). Therefore correctness is
accepted, but speed was not promoted as a general default until the repeated
larger-corpus timing gate below. That follow-up is now complete.

### Repeated Profile-0 SIMD/scalar gate (completed; SIMD promoted for this path)

To resolve the conditional result above, a matched Profile-0 gate was run on
the same complete-page input (`prof_input/input2`, 941,724 bytes,
SHA-256 `7ca547e67ca14341250c969caf5fa9d3c21d5966579b03b111d6b80540f99048`).
The scalar control used `CMIX_E1_SIMD_SCAN=0`; the candidate used `=1`.
Both archives were **180,545 bytes**, and both decoded outputs matched the
input SHA-256 exactly.

| Variant | Encode | Decode | Combined | Encode RSS | Decode RSS |
|---|---:|---:|---:|---:|---:|
| Scalar | 17:51.13 | 22:23.22 | 40:14.35 | 8,247,300 KiB | 8,246,796 KiB |
| SIMD | 16:23.17 | 16:08.65 | 32:31.82 | 8,247,060 KiB | 8,246,276 KiB |

On this repeated larger gate, SIMD reduced encode time by **8.2%**, decode
time by **27.9%**, and combined time by **19.1%**, with no archive-size or
integrity change. This is a measured speed win for the E1 checksum scan, not
a compression-strength win. The SSE2 path is now the default; passing
`CMIX_E1_SIMD_SCAN=0` remains available for a matched scalar control.

### Existing PGO profile gate (rejected)

The repository's existing `pgo_data/default.profdata` was applied with
`make prof_use`, using the same model defines and `CMIX_E1_SIMD_SCAN=1`. On
the matched `prof_input/input2` gate it produced **180,547 B**, versus
**180,545 B** for the non-PGO SIMD control, and its archive SHA-256 differed.
Encode time was **19:16.55** with **8,246,640 KiB RSS**, slower than the SIMD
control's **16:23.17**. Because PGO changed the archive bytes and lost speed,
it is rejected; the existing profile is stale or not representative of this
build and is not used for promotion.

### Clean promoted-default smoke gate (completed)

After changing the source and gate-script defaults to `CMIX_E1_SIMD_SCAN=1`,
the binary was rebuilt from a clean tree and run on `prof_input/input`
(51,052 bytes). It produced **6,128 B**; encode/decode took **1:07.38 / 1:07.72**
with peak RSS **4,653,192 / 4,652,492 KiB**. The decoded output length and
SHA-256 matched the input exactly (`3e698082f5ded92f8fad58b94cac0c681da8ecd1e67627320d9a4d0641d46e9b`).

## Causal previous-byte direct expert (pending exact A/B gate)

`CMIX_EXTRA_PREVBYTE_DIRECT=1` adds one bounded `Direct` model keyed by
`recent_bytes_[0]` (the previous completed byte) and `bit_context_` (the
current byte prefix). Both values are available to the decoder before the next
bit is coded, so the expert is causal. Its 256x256 adaptive probability table
is created at runtime and contributes no static archive bytes. Promotion
requires a smaller measured archive, exact encoder/decoder SHA equality, and
a reproducible runtime tradeoff. The gate is
`tools/run_prevbyte_direct_gate.sh` and is bounded to inputs of at most 20 MB.

The independent 51,052-byte gate completed with exact SHA-256 restoration. The
baseline produced a 6,143-byte archive and a 301,848-byte executable; the
candidate produced a 6,136-byte archive and a 303,928-byte executable. Encode
time was 1:03.64 (baseline) versus 1:02.78 (candidate), and decode time was
1:03.02 versus 1:01.17. Although the candidate saved 7 archive bytes and was
slightly faster, its charged archive-plus-executable total was 2,073 bytes
larger. It is rejected and remains opt-in (`CMIX_EXTRA_PREVBYTE_DIRECT=1`)
only for future diagnostic work.

## Shadow cross-entropy diagnostic (rejected)

The diagnostic-only `CMIX_SHADOW_TRACE=1` path evaluates a Laplace-smoothed
previous-byte/current-prefix predictor with the same 16-bit quantization used
by the range coder. On the 51,052-byte complete input it reported
`7.653411824` bits/byte for the production mixed prediction versus
`28.465867977` bits/byte for the shadow expert. The bounded encode/decode gate
also restored the exact input SHA and retained the 6,143-byte archive. Since
the candidate is over 20 bits/byte worse than the existing mixer, it is
rejected as a predictor and will not be integrated.

## Model-family entropy microscope (diagnostic; no archive change)

`CMIX_MODEL_TRACE=1` records the cross-entropy of each existing predictor
family before its state is updated. The arithmetic path is unchanged. For
each family, `mean_loss_bits` is the loss of the simple mean of that family's
outputs (a causal, implementable baseline). `best_member_loss` is a hindsight
lower bound that chooses whichever member predicted the observed bit best; it
is explicitly *not* implementable and measures only available diversity. The
report is written to `CMIX_MODEL_TRACE_PATH` and is not part of the archive.

The exact 51,052-byte gate (`prof_input/input`, SHA-256
`3e698082f5ded92f8fad58b94cac0c681da8ecd1e67627320d9a4d0641d46e9b`) passed
encoder/decoder SHA equality. Profile 0 produced a 6,128-byte archive; the
diagnostic does not alter that archive. Values below are bits per bit (multiply
by eight for bits per input byte on this small warm-up corpus):

| Family | Causal mean | Hindsight best member |
|---|---:|---:|
| bracket | 0.958140 | 0.958140 |
| FXCM | 0.677180 | 0.038852 |
| direct | 1.119726 | 1.119726 |
| match | 0.372439 | 0.128801 |
| indirect | 0.392413 | 0.058390 |
| PPMD | 0.141377 | 0.141377 |
| byte mixer | 0.180028 | 0.180028 |
| final production mix | 0.119287 | 0.119287 |

This rejects replacing the predictor with any single family mean: each family
is worse than the trained final mix. It also identifies the next measurable
opportunity: match and indirect families have substantial diversity gaps, and
FXCM's gap is largest. The next experiment should be a causal selector or
small gate over those existing outputs, measured first as a shadow
cross-entropy model and integrated only if it beats the current final mix after
executable cost. The hindsight column must not be reported as a compression
result.

The follow-up causal-family selector used deterministic exponential weights
over the seven family means (squared-error update, no coder integration). On
the same gate it measured **0.141562 bits/bit** (1.13249 bits/byte), versus
**0.119287 bits/bit** (0.95430 bits/byte) for the existing final mix. The
archive remained 6,128 bytes and the restored SHA matched exactly. Because
this causal selector is worse than the production mixer, it is rejected as a
replacement. Future selector work must operate on richer per-model/context
features or target a new predictive expert; another family-average gate is
not justified.

A tracing-disabled matched control (`CMIX_MODEL_TRACE=0`) also produced
exactly 6,128 archive bytes and the same input/output SHA-256. This confirms
that the diagnostic changes neither the probability path nor the archive; the
model reports are measurements rather than a codec variant.

An online selector over all individual model outputs (2,048-slot bounded
portfolio, causal squared-error updates) measured **0.124109 bits/bit**
(0.99287 bits/byte). This is closer than family selection but still worse than
the production mix at **0.119287 bits/bit**. The exact archive remained 6,128
bytes with matching SHA-256, so the selector is rejected as a replacement.
The large hindsight gap therefore cannot be harvested by a small generic
selector; the next candidate must add genuinely new predictive information.

The richer context-conditioned selector (8 causal line classes x 128 byte
prefix states) was also rejected. Its measured loss was **0.175954 bits/bit**
(1.40763 bits/byte), worse than both the global family selector and the
existing final mix. It passed the same exact 6,128-byte archive and SHA gate.
This rules out a small family gate as the next route; a new expert must supply
information not already represented by the current mixer.

## Two-byte causal direct expert (rejected)

`CMIX_EXTRA_BIGRAM_DIRECT=1` adds a runtime `Direct` table keyed by the two
previous completed bytes and the current bit prefix (65,536 causal contexts).
It adds no archive metadata and passed exact round-trip SHA validation on the
51,052-byte gate. The candidate produced **6,127 archive bytes** versus
**6,128** for the matched control, but its executable grew from **320,640** to
**321,200 bytes**. The charged total therefore increased by **559 bytes**.
Encode/decode time was 1:13.79/1:11.80 for the candidate; memory peaked at
4,735,632/4,735,116 KiB. The one-byte archive gain is not sufficient to pay
the executable cost, so this expert remains opt-in and is rejected for
promotion.

## Deterministic seed check (rejected candidate)

The profile gate now accepts `SEED` as an environment parameter so indirect
map initialization can be tested without editing source. A first alternate
(`SEED=1`, Profile 4, same 51,052-byte input and exact SHA gate) produced
**6,145 B** versus **6,143 B** for the verified `SEED=923` control. It is
therefore rejected; the production seed remains 923. A broader sweep is not
justified until a larger gate suggests a materially different basin.

## PPMD huge-page advice (rejected for speed)

`CMIX_PPMD_HUGEPAGE=1` was tested as an output-neutral runtime optimization
on the 51,052-byte exact gate with Profile 4. Archive size stayed **6,143 B**
and the input/output SHA-256 matched. The candidate executable was **302,104
B**. Encode/decode took **1:14.38/1:16.09**, with peak RSS
4,510,044/4,509,712 KiB; this was slower than the matched Profile-4 gate
(0:41.57/0:43.73). It is not promoted.

## Hashed three-byte continuation expert (rejected)

`CMIX_EXTRA_HASHED_NGRAM=1` adds a 20-bit hashed table combining the previous
three completed bytes with the current bit prefix. It is causal and uses about
5 MB of runtime state, with no archive side data. On the 51,052-byte exact
gate it produced **6,137 B** versus **6,128 B** for the control and passed
SHA-256 restoration. The executable grew from **320,640 B** to **322,432 B**;
encode/decode times were 1:21.69/1:20.83 with peak RSS 4,659,436/4,659,288 KiB.
The archive and charged total both worsened, so this expert is rejected.

## Larger model-family gate (completed)

The same diagnostic was run on the 941,724-byte complete-page gate
(`prof_input/input2`, SHA-256
`7ca547e67ca14341250c969caf5fa9d3c21d5966579b03b111d6b80540f99048`). The
archive was **180,545 B**, and encoder/decoder SHA-256 matched exactly. Peak
RSS was 8,247,484/8,246,448 KiB; encode/decode wall time was 19:56.30/17:06.32.

Measured losses (bits per bit) were: final production mix **0.191675**, PPMD
**0.235858**, byte mixer **0.224563**, indirect **0.523102**, match **0.662757**,
FXCM **0.765766**, and the causal context-family selector **0.255924**. The
larger gate therefore confirms that simple selector/table replacements are
not the route to a gain; a new expert must add information absent from the
current portfolio.

## Line-class-conditioned byte expert (rejected)

`CMIX_EXTRA_LINEBYTE_DIRECT=1` adds a causal `Direct` table keyed by the
decoder-known line class and previous completed byte (2,048 contexts). On the
51,052-byte exact gate it reduced the archive from **6,128 B to 6,121 B** and
passed SHA restoration, but increased the executable from **320,640 B to
321,216 B**. Charged total therefore worsened by **569 B**. Encode/decode
times were 1:01.10/1:06.80 with peak RSS 4,656,312/4,655,648 KiB. It is
rejected and remains opt-in.

## Causal long-match continuation expert (rejected)

`CMIX_EXTRA_LONG_MATCH=1` adds one PAQ-style match-continuation probability
expert. It is decoder-causal: the model is neutral until an exact match has
continued for at least 32 bytes, and it serializes no source pointer or side
metadata. The matched control used the same build with
`CMIX_EXTRA_LONG_MATCH=0`.

On the 51,052-byte exact gate (`prof_input/input`, SHA-256
`3e698082f5ded92f8fad58b94cac0c681da8ecd1e67627320d9a4d0641d46e9b`), both
control and candidate produced **6,128 archive bytes** and restored the input
SHA exactly. The control executable was **321,024 B**; the candidate was
**322,480 B** (+1,456 B). Control encode/decode wall times were
**2:43.78/2:50.31**, with peak RSS **4,653,016/4,652,608 KiB**. Candidate
encode/decode were **3:24.71/3:24.12**, with peak RSS
**4,654,192/4,653,764 KiB**. Since there is no archive gain and both charged
size and runtime worsen, this expert is rejected and remains opt-in.

## Bounded 16-bit match-history context (neutral, rejected)

`CMIX_EXTRA_MATCH_CONTEXT2=1` adds one match expert over a decoder-causal
16-bit history (`ContextHash(order=2, hash_size=8)`, 65,536 states). It adds no
archive metadata and was compared with the same Profile-0 build with the
switch disabled.

On the 51,052-byte exact gate (`prof_input/input`, SHA-256
`3e698082f5ded92f8fad58b94cac0c681da8ecd1e67627320d9a4d0641d46e9b`), the
candidate and control both produced **6,128 archive bytes**, both restored the
input SHA exactly, and both charged **321,024 B** for the executable. Candidate
encode/decode times were **1:32.45/2:19.44**, with peak RSS
**4,653,216/4,652,576 KiB**. The prior control gate was **2:43.78/2:50.31**
with **4,653,016/4,652,608 KiB** RSS; this timing difference is within the
observed WSL variance and has no archive benefit. The context is therefore
rejected and remains opt-in.

## Line-class-conditioned match history (neutral, rejected)

`CMIX_EXTRA_LINE_MATCH=1` adds a match expert whose eight-slot source history
is keyed by the decoder-known line class. This is a structural partition of
the existing causal match state; it emits no line-class tag or other archive
metadata. On the same 51,052-byte exact gate, it produced **6,128 archive B**
and **321,024 executable B**, exactly matching control, with an identical
restored SHA-256. Candidate encode/decode were **2:27.14/2:15.78** and peak
RSS **4,653,148/4,652,648 KiB**. With no archive or charged-size gain, this
candidate is rejected and remains opt-in.
### Rejected: opt-in sparse match expert (2026-08-31)

The fx2-cmix README documents a sparse match model using gaps of 1--2 bytes and minimum lengths of 3--6 bytes.  We ported a bounded, decoder-causal four-hash version as `CMIX_EXTRA_SPARSE_MATCH=1`; it contributes two mixer inputs and no serialized side stream.  The exact gate used `prof_input/input` (51,052 bytes, SHA-256 `3e698082f5ded92f8fad58b94cac0c681da8ecd1e67627320d9a4d0641d46e9b`) with all other experimental predictors disabled.

Control (`long-match-control`, SIMD scan on): archive 6,128 bytes, executable 321,024 bytes, encode 2:43.78, decode 2:50.31, RSS 4,653,016/4,652,608 KiB.  The first candidate build accidentally serialized its zero table; that was corrected to BSS initialization and rerun.  Corrected sparse candidate: archive 6,129 bytes (+1), executable 322,912 bytes (+1,888), encode 2:16.67, decode 2:27.24, RSS 4,659,092/4,658,716 KiB.  Both directions restored the exact SHA-256.  The byte result is neutral-to-worse even after fair executable accounting, so this candidate is rejected and must not be promoted to a larger or full-corpus run.  The default macro remains 0.

## New research lead: PPMD-conditioned LiGRU (not yet implemented)

An external `ligru-compress` benchmark reports standalone LiGRU results on a
61,607,920-byte `enwik8.fxd` stream: hidden sizes 384/416/448 produced
19,085,215 / 18,941,638 / 18,891,029 bytes, versus 20,148,724 bytes for an
fx2-cmix LSTM-only comparison.  A later post reports a **PPMD-conditioned
LiGRU-256** result of 16,989,819 bytes on the same transformed input.  These
are external measurements, not cmix-lex measurements, and must not be
linearly extrapolated to enwik9.

Sources: [fxcm forum benchmark](https://encode.su/threads/4116-fxcm/page3),
[LiGRU + PPMD discussion](https://encode.su/threads/4370-The-new-kid-on-the-block-ligru-compress-an-experimental-file-compressor/page4),
and [PAQ8px technical documentation](https://github.com/hxim/paq8px/blob/master/DOC).

The evidence justifies a shadow-model investigation, not immediate replacement
of the production ByteMixer. The public posts do not provide the complete
LiGRU source, optimizer, quantization, initialization, or exact auxiliary-input
interface. They also discuss scalar/SSE/AVX and fused-multiply-add differences,
so deterministic cross-machine behavior must be demonstrated rather than
assumed. Any implementation must therefore be opt-in, decoder-causal, and
tested first on complete-page transformed gates.

The first gate should compare the current ByteMixer/LSTM against a minimal
PPMD-conditioned recurrent candidate, measuring actual coded bytes, executable
growth, exact SHA-256, and encode/decode RSS/time. A promising external result
does not waive those local acceptance tests.
## WRT-phase mixer gate (2026-08-31)

The proposed “token-smearing” intervention was tested in the smallest exact
gate before any full-corpus run.  The candidate added one extra outer-mixer
context keyed by the existing decoder-causal `ContextManager::wrt_state_`
signal.  This signal is deliberately weaker than a true WRT parser: it only
records whether the previous transformed byte was in the high-byte range.

Input: `prof_input/input` (51,052 bytes, complete-page gate), with identical
`PPMD_ORDER=12`, `PPMD_MEMORY_MB=32`, profile 0 and SIMD settings.

| build | archive | executable | encode | decode | peak RSS |
|---|---:|---:|---:|---:|---:|
| control | 6,129 B | 321,040 B | 1:18.52 | 1:28.31 | 4,652,872 KiB |
| WRT-phase mixer | 6,129 B | 321,040 B | 1:14.22 | 1:13.30 | 4,652,840 KiB |

Both decoded outputs matched the input SHA-256
`3e698082f5ded92f8fad58b94cac0c681da8ecd1e67627320d9a4d0641d46e9b`.
The archive delta is **0 bytes**, so the timing difference is not evidence of
a compression improvement.  The experiment is rejected as a route to the
2.7% target and the opt-in macro remains disabled by default.

The original three-state proposal is not equivalent to this gate.  The WRT
encoding has escaped literals, capitalization markers, and variable-length
1/2/3-byte dictionary codes; a valid phase model would need to reproduce that
grammar and the preprocessor segment boundaries exactly.  It should only be
attempted after a shadow parser demonstrates a measurable conditional-loss
reduction.

## Existing-family residual oracle (2026-09-01)

To enforce the residual-information rule, `CMIX_MODEL_TRACE=1` was extended
with a 33-point fixed logit-blend sweep.  For each existing family it evaluates
`sigmoid(logit(final) + alpha*(logit(family)-logit(final)))` for
`alpha=-2..2`; this is diagnostic only and never controls the range coder.

On the same 51,052-byte exact gate (PPMD order 12 / 32 MiB), the final loss was
`0.119292925` bits/bit.  The best alpha for bracket, FXCM, direct, match,
indirect, and byte-mixer was exactly `0`, meaning no residual gain.  PPMD was
the sole exception: alpha `0.125` reached `0.119244541`, an optimistic gain of
`0.000048384` bits/bit (about 2.5 bytes on this input).  This is many orders
of magnitude below the `0.04044` bits/transformed-byte required for the target.

The raw trace is preserved as `profile-0-residual-oracle.tsv`.  A future
candidate (for example LiGRU) must supply a genuinely new prediction stream to
this same oracle; reblending an existing family is not a viable upgrade.

## Residual-physics ledger (2026-09-02)

The trace build was extended with a causal ledger containing loss by the
existing `(line_class, wrt_state)` key and surprise autocorrelation for lags
1--64.  The exact 51,052-byte gate remained lossless: archive `6,129` bytes,
and restored SHA-256
`3e698082f5ded92f8fad58b94cac0c681da8ecd1e67627320d9a4d0641d46e9b`.

The final loss was `0.119292901` bits/bit.  Regional mean losses were:

| region key | bits | mean loss (bits/bit) |
|---:|---:|---:|
| 0 | 28,040 | 0.037326309 |
| 10 | 34,240 | 0.059672490 |
| 12 | 30,624 | 0.247254110 |
| 14 | 314,672 | 0.119207715 |
| 15 | 888 | 0.623648433 |

Other keys had zero observations on this gate.  Bit-level surprise covariance
was positive through lag 64 (lag 1: `0.0510621`; lag 8: `0.0499767`; lag 64:
`0.0349079`).  This is evidence of structured residual error, but it is not
yet evidence of 64-byte-or-longer semantic dependence: byte-position and
within-byte effects must be removed before interpreting the spectrum.  The
ledger is a measurement foundation, not a model-promotion result.

## Byte-normalized residual check (2026-09-02)

The trace was rerun after aggregating the eight coded bits of each byte and
adding a first-difference autocorrelation diagnostic.  The exact complete-page
gate remained lossless: archive `6,129` bytes and restored SHA-256
`3e698082f5ded92f8fad58b94cac0c681da8ecd1e67627320d9a4d0641d46e9b`.
The trace covered `51,058` complete bytes; mean final surprise was
`0.119292901` bits/bit and variance was `0.0661756645`.

Raw byte-surprise covariance declined from `0.036194927` at lag 1 to
`0.0175256833` at lag 64, which can be caused by slowly changing regimes.
After differencing adjacent byte surprises, lag 1 was `-0.0276585315`, lag 8
was `-0.000206109776`, and lag 64 was `-0.000021806`.  Thus this bounded
sample does not show a stable long-range byte residual that another fixed
delayed/geometric history is likely to exploit.  The positive raw covariance
is primarily nonstationarity/local regime persistence, not evidence that a
wider raw context table can supply the missing `0.04044` bits per transformed
byte.  The next strength search must provide a genuinely new conditional
information source (or a measured region-specific model).

Artifact: `profile-0-ledger-diff.tsv`.

## Conditional delta-byte expert (2026-09-02, rejected)

An opt-in `ConditionalDeltaByteExpert` was added.  It is fully
decoder-synchronized: it derives repeat/small-delta/linear-continuation modes
from the two preceding completed bytes, conditions on line class and bit
position, and adapts an agreement probability from decoded truth bits.  It
emits no pointer, model table, or side-data bytes.  The feature is enabled by
`EXTRA_CONDITIONAL_EXPERT=1` and remains disabled by default.

On the exact 51,052-byte complete-page gate (`PPMD_ORDER=12`, 32 MiB, profile
0), the candidate produced archive `6,126` bytes versus the same-source
default control's `6,129` bytes and restored the exact SHA-256
`3e698082f5ded92f8fad58b94cac0c681da8ecd1e67627320d9a4d0641d46e9b`.
However, the executable grew from `321,056` to `321,520` bytes, so the
charged result worsened by `461` bytes.  Candidate encode/decode were
`1:28.50/1:10.39` versus control `1:15.22/1:14.52`, with peak RSS
`4,653,872/4,653,336` versus `4,652,776/4,652,744` KiB.  The 3-byte archive
gain is far below the charged executable cost and is rejected; no full-corpus
run is justified.

## Headroom certificate (2026-09-02)

The trace now emits the formal certificate defined in `MATHEMATICAL_SPEC.md`.
On the exact gate, baseline ideal loss was `48,726.8554697` bits.  The best
33-point hindsight blend of an existing family was `48,707.1022091` bits,
leaving only `19.7532606` bits (`2.469` potential bytes).  The certificate
status is `NON_CAUSAL_ORACLE` and its charged saving is explicitly `0` bytes.
This is a reproducible proof that reweighting the existing predictor family
cannot provide meaningful headroom on this gate; a real improvement must
introduce new causal information and pass the charged-score inequality.

Artifact: `profile-0-headroom-certificate.tsv`.

The certificate now records `oracle_scope`, `oracle_hypothesis_class`, and
`coverage_bytes`, preventing this bounded result from being overstated as a
corpus-wide theorem.  It also reports the Fisher-curvature screening bound for
each existing family.  On this gate the largest bound was 44.699 bits for
PPMD, while the exact hindsight oracle recovered only 19.753 bits and the
charged saving remained zero.  The output now labels these values
`quadratic_local_headroom_bits`; they are local screening estimates, not global
upper bounds or codec improvements.  It also emits the normalized residual
correlation matrix for model-redundancy and speed-pruning analysis.

## PPMD residency-interval speed gate (2026-09-02)

The speed infrastructure now exposes `PPMD_REMAP_INTERVAL` in both the
profile script and the Makefile.  This changes only the cadence of
`MADV_DONTNEED` on the disk-backed PPMD heap; it does not change model bytes,
pointer values, update order, or coded probabilities.

On the exact 51,052-byte complete-page gate with profile 2, order 12, 32 MiB
PPMD, and mmap-backed state:

| remap interval | archive | encode | decode | SHA |
|---:|---:|---:|---:|:---:|
| 5,000 bytes | 6,137 B | 60.59 s | 54.31 s | exact |
| 500,000 bytes | 6,137 B | 52.97 s | 54.20 s | exact |

The candidate is therefore output-neutral on this gate and reduced encode
time by 12.6% (6.7% over encode+decode).  This is a speed signal, not proof
of a 55-to-35-hour full-corpus reduction; full-run RSS and page-fault counts
must still be measured before selecting a production interval.
