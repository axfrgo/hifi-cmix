# cmix-lex Hutter Prize handoff

Date: 2026-08-31

This document is a continuation point for another engineer or agent. It records
the architecture, measured results, experiments, constraints, and unresolved
research question. Measured results are separated from estimates and
non-implementable bounds. The final section intentionally asks the central
improvement question without answering it.

## 1. Objective

The project aims to produce a legal, lossless Hutter Prize `enwik9` submission:

```text
input:       exactly 1,000,000,000 bytes of enwik9
required:    exact byte-for-byte reconstruction
score:       self-extracting decompressor + compressed archive
constraint:  one CPU, under 10 GB RAM, contest time limit
stretch:     <= 106,685,197 charged total bytes
```

The current public cmix-lex result is already much stronger than the earlier
HFT prototypes:

| Quantity | Bytes |
|---|---:|
| Public cmix-lex archive9 | 109,190,109 |
| Packaged compressor | 459,938 |
| Charged total | **109,650,047** |
| Stretch target | **106,685,197** |
| Remaining gap | **2,964,850** |
| Required reduction from current cmix-lex total | about **2.704%** |

The target is not a claim that has been achieved. It is an engineering target.
The public result is the baseline to beat; the earlier 247--250 MB HFT result
must not be confused with it.

## 2. Authoritative measured baseline

The public cmix-lex README reports:

```text
archive9:              109,190,109 bytes
cmix executable:           459,938 bytes
total:                  109,650,047 bytes
compressed cmix payload:108,929,925 bytes
```

The full decompressor restored exactly 1,000,000,000 bytes. The source and
reconstructed output both had SHA-256:

```text
159b85351e5f76e60cbe32e04c677847a9ecba3adc79addab6f4c6c7aa3744bc
```

Reported public resource measurements:

```text
compression wall time: 51:30:03
decompression wall time: 54:31:14
compression peak RSS:   9,696,008 KiB
decompression peak RSS: 9,846,960 KiB
CPU time compression:   43:33:23
CPU time decompression: 43:04:55
swap:                   0
```

At the published Geekbench normalization (`T=1200`), the reported CPU time
was under the Hutter limit (`70000/T = 58.33 h`). This is a public result, not a
fresh run on the current laptop.

The independent published baseline is available in the
[cmix-lex repository](https://github.com/blahem/cmix-lex). The upstream
architecture and result should be treated as facts; proposed improvements are
only hypotheses until they pass a charged, lossless A/B gate.

## 3. Repository and workspace

Windows workspace:

```text
C:\Users\alexj\Documents\high fidelity compression
```

Main project:

```text
C:\Users\alexj\Documents\high fidelity compression\cmix-lex
```

WSL-native audit copy used for bounded gates:

```text
/home/alexj/cmix-lex-audit
```

Related local repositories:

```text
fx2-cmix
fx3-cmix
hifi
zenith-substrate-institutional
hutter-lab
```

The broader workspace is dirty and contains user work. There is no trustworthy
clean Git baseline. Do not use `git reset --hard`, broad checkout, recursive
deletion, or bulk cleanup. Preserve unrelated changes.

Important local files:

```text
README.md                    public result and build notes
changes.md                   transform and architecture explanation
EXPERIMENTS.md               detailed measured experiment record
src/runner.cpp               preprocessing, coding, packaging, dispatch
src/predictor.cpp            model portfolio and outer mixer integration
src/models/fxcmv1.cpp        integrated fxcm_v26 model
src/models/ppmd.cpp/.h       PPMD byte model and memory controls
src/models/match.cpp/.h      causal match models
src/r1_reorder_transform.cpp payload_lex/R1ORD3 reversible transform
tools/run_profile_gate.sh    bounded exact encode/decode A/B runner
makefile                     clang build and profile targets
```

`EXPERIMENTS.md` is the detailed lab notebook. This handoff summarizes it; do
not erase the notebook when changing direction.

## 4. Complete pipeline

The high-level data path is:

```text
enwik9
  -> split intro/main/coda
  -> benchmark-specific article reorder
  -> PHDA9 reversible Wikipedia preprocessing
  -> cmix dictionary word-replacement transform (WRT)
  -> payload_lex reorder of one structured PHDA9 tail regime
  -> append R1ORD3 side data
  -> cmix/PAQ bitwise predictor and range coder
  -> package dictionary, compressed payload, header, and self-extractor
  -> archive9
```

The reverse path is:

```text
archive9 self-extracts
  -> cmix reconstructs the transformed stream
  -> extract R1ORD3 side blob
  -> restore payload_lex tail order
  -> inverse dictionary/WRT
  -> inverse PHDA9
  -> restore article-body order by embedded page IDs
  -> merge intro/main/coda
  -> enwik9_uncompressed
```

Every transform is intended to be reversible. “Semantic similarity” is not
enough; the output must match every original byte.

## 5. Reversible preprocessing and payload_lex

After PHDA9 and WRT, the recorded ready stream was:

```text
post-WRT stream before payload_lex: 586,459,321 bytes
encoded tail start:                 541,126,651
encoded tail length:                 45,332,670
```

The tail is line-oriented PHDA9 metadata, not ordinary prose. It has three
regimes:

```text
regime 0: start of tail
regime 1: offset 13,599,801 within tail
regime 2: offset 30,372,888 within tail
```

Regime 1 contains 243,425 article-related metadata blocks, with repeated `D99`
and `D86a` fields plus payload-like text. `payload_lex` changes only regime 1:
it lexically sorts `D99` blocks by visible payload bytes after the `D99` and
first `D86a` lines. Similar payloads become adjacent to improve cmix contexts.

Because the original metadata order is needed for exact PHDA9 restoration, the
transform appends a correction blob called `R1ORD3`. Its current order
description uses the visible `D86a` field as a predictor and Lehmer ranks for
the residual permutation.

Recorded sizes:

```text
transformed stream with side blob: 587,138,826 bytes
R1ORD3 side blob:                       679,489 bytes
restored ready stream:              586,459,321 bytes
```

The side data is deliberately at EOF so cmix sees the reordered tail before it
encounters the correction data. The full round-trip audit reported:

```text
ready stream after cmix:       587,138,826 bytes
dictionary-decoded stream:    934,220,701 bytes
reconstructed enwik9:       1,000,000,000 bytes
byte compare:                         OK
```

The transform is benchmark-specific but self-contained: no network or hidden
database is required at decompression.

## 6. Predictor architecture

The coded symbol is one bit at a time. The encoder and decoder run identical
causal predictor state and feed a binary arithmetic/range coder.

The current `Predictor` portfolio contains:

```text
Bracket model
FXCM v26 model (up to 560 outputs by default)
Direct state maps
Hashed direct models (optional)
Word sparse-context nonstationary models
Match models over causal history contexts
Run-map indirect models
Four double-indirect models (Profile 0)
PPMD byte model
ByteMixer (recurrent in the published configuration)
Two layers of adaptive mixers
SSE/APM-style final calibration
```

The outer flow is approximately:

```text
each model predicts next bit
  -> model outputs are stretched/quantized
  -> first-layer context mixers combine them
  -> second-layer mixer combines first-layer results and auxiliaries
  -> SSE/APM calibration
  -> binary range coder
```

Important defaults in the current source:

```text
CMIX_PROFILE=0
FXCM_OUTPUT_LIMIT=560
PPMD_ORDER=25
PPMD_MEMORY_MB=14000
CMIX_PPMD_MMAP_TO_DISK=1
CMIX_PPMD_HUGEPAGE=0
CMIX_E1_RECENT_CACHE=0
CMIX_E1_SIMD_SCAN=1 (promoted output-neutral speed path)
```

PPMD is disk-backed in the published-style build because its logical heap is
larger than the RAM target. The mapping remains stable while
`MADV_DONTNEED` drops resident pages; `MADV_RANDOM`, `O_NOATIME`, and exact
`ftruncate()` handling reduce unnecessary I/O effects. These changes do not
alter probability state.

`fxcmv1.cpp` is an integrated port of Kaido Orav's `fxcm_v26`, not the old
minimal fx2 model. It supplies wiki-aware parsing, sentence and paragraph
state, decoded-word and dictionary-codeword contexts, word types, template/
link/table/list contexts, direct/indirect state maps, and gates for low-value
sections such as categories, references, bibliography, and external links.

The `ByteMixer` is a separate byte-level predictor whose output enters the
outer mixer. The default source still contains an optional recurrent LSTM path;
the fast byte-model/byte-mixer replacement was measured but not accepted as a
submission configuration.

## 7. Archive and executable accounting

The score is not just compressed payload bytes. Every candidate must account for:

```text
compressed archive bytes
+ packaged/self-extracting decompressor bytes
+ dictionary/header/side data included in the submission
```

For a development gate, record at minimum:

```text
input length and SHA-256
archive byte length and SHA-256
executable byte length
decoded length and SHA-256
encode wall time and CPU/RSS
decode wall time and CPU/RSS
compiler flags and macro values
```

A raw archive reduction that costs more executable bytes is not automatically a
win. An entropy estimate, hindsight selector, or offline oracle is not a
submission result unless it can be reproduced by decoder-known state.

## 8. Experimental history and decisions

The complete measurements are in `EXPERIMENTS.md`. The important decisions are:

### Accepted or provisionally retained

* `payload_lex` plus `R1ORD3`: the only recent full-corpus transform with a
  meaningful measured win, producing the public 109,650,047-byte total.
* `fxcm_v26` integration and benchmark-specific article ordering: part of the
  public cmix-lex result.
* E1 checksum SIMD scan (`CMIX_E1_SIMD_SCAN=1`): output- and SHA-identical;
  on a repeated 941,724-byte complete-page gate it reduced combined time by
  19.1% (8.2% encode, 27.9% decode). It is a speed optimization, not a ratio
  improvement.
* Profile 4 is retained as a development fast/balanced ablation only. It
  removes model families and is not the published full-memory baseline.

### Rejected strength or speed candidates

* FXCM output limits 544 and 480: neutral or worse; 480 lost 1,621 bytes on
  the raw gate.
* Structural line-prefix and line-class mixer contexts: 8--9 bytes worse on
  the raw gate.
* Previous-byte direct expert: saved 7 archive bytes but cost 2,073 charged
  bytes after executable growth.
* Two-byte direct expert: saved 1 archive byte but cost 559 charged bytes.
* Line-class byte expert: saved 7 archive bytes but cost 569 charged bytes.
* Hashed three-byte continuation expert: 9 archive bytes worse and larger
  executable.
* Long-match continuation expert: no archive gain, +1,456 executable bytes,
  and slower on the raw gate.
* Bounded 16-bit match-history context: neutral archive and executable.
* Line-class-conditioned match history: neutral archive and executable.
* Causal family selector, individual selector, and context-conditioned selector:
  all worse than the production mixer in measured cross-entropy.
* PGO/profile-use: slower and changed archive bytes; rejected.
* PPMD huge-page advice: slower; rejected.
* Larger PPMD update cadence and update thinning: slower/worse; one thinning
  attempt crashed and was removed.
* RAM-only anonymous PPMD heap: lossless but not faster enough; rejected as the
  default.
* Fast order-1 byte model plus bypassed ByteMixer: faster on a tiny gate but
  lost archive bytes and remained too slow on a realistic page gate.
* PPMD order/memory reductions: small speed changes and negligible small-gate
  ratio changes; no evidence of a four-hour full-corpus route.

### Latest source-backed candidate: sparse match model

The [fx2-cmix README](https://github.com/kaitz/fx2-cmix) documents a sparse
match model with 1--2 byte gaps and minimum lengths 3--6, mainly for escaped
UTF-8. The local `fxcm_v26` integration did not contain this symbol, so an
opt-in `CMIX_EXTRA_SPARSE_MATCH=1` port was implemented as a four-hash,
decoder-causal probability expert. It emits no source pointer or side stream.

Exact 51,052-byte gate:

```text
input SHA-256: 3e698082f5ded92f8fad58b94cac0c681da8ecd1e67627320d9a4d0641d46e9b
control:   archive 6,128 B, executable 321,024 B
candidate: archive 6,129 B, executable 322,912 B
candidate encode/decode: 2:16.67 / 2:27.24
candidate peak RSS: 4,659,092 / 4,658,716 KiB
decoded SHA: exact match
```

The first port accidentally serialized a zero table; that was corrected to
BSS/runtime initialization and rerun. The corrected candidate remained one
archive byte worse and 1,888 executable bytes larger. It is rejected and
remains opt-in only. No larger or full-corpus sparse-match run is justified.

## 9. Small-gate methodology

The bounded runner is:

```text
cmix-lex/tools/run_profile_gate.sh
```

It refuses inputs over 20 MB and performs real encode/decode plus SHA checks.
The principal complete-page development input is:

```text
prof_input/input2
size: 941,724 bytes
SHA-256: 7ca547e67ca14341250c969caf5fa9d3c21d5966579b03b111d6b80540f99048
```

The raw small gate is:

```text
prof_input/input
size: 51,052 bytes
SHA-256: 3e698082f5ded92f8fad58b94cac0c681da8ecd1e67627320d9a4d0641d46e9b
```

Do not test PHDA9 on an arbitrary truncated byte slice: it may end inside
XML/wiki structure and the inverse transform can fail. Use complete-page
inputs or the provided gate scripts.

Run WSL-native, not from `/mnt/c`, because WSL filesystem access was unstable
and much slower for these pointer-heavy workloads.

Typical command:

```bash
cd /home/alexj/cmix-lex-audit
PROFILE_LIST="0 4" tools/run_profile_gate.sh prof_input/input2 profile-gates
```

For a macro candidate, pass one environment flag, run the same input and same
control, then compare charged totals. Never promote from timing alone.

## 10. Build and verification commands

From an Administrator-launched Ubuntu WSL shell:

```bash
cd /home/alexj/cmix-lex-audit
make fast
```

The Windows source can be synchronized without touching unrelated files:

```bash
cp -f '/mnt/c/Users/alexj/Documents/high fidelity compression/cmix-lex/src/models/fxcmv1.cpp' src/models/fxcmv1.cpp
cp -f '/mnt/c/Users/alexj/Documents/high fidelity compression/cmix-lex/tools/run_profile_gate.sh' tools/run_profile_gate.sh
chmod +x tools/run_profile_gate.sh
```

Build the packaged public-style compressor from the project instructions:

```bash
bash ./build_and_construct_comp.sh
```

The final self-extracting compressor is normally `run/cmix`; the compressed
archive is `archive9`. For a real decompression audit, use an empty working
directory, run the self-extractor, and compare output length plus SHA-256.

Do not rebuild with audit/profile/debug features when measuring executable size.
Rebuild the default stripped binary first. Build options such as `PPMD_ORDER`,
`PPMD_MEMORY_MB`, `CMIX_PROFILE`, and the `CMIX_EXTRA_*` flags are development
controls, not automatically legal submission settings.

## 11. Operational and licensing cautions

* A full public-style run consumes roughly 20 GB of temporary disk and about
  10 GB RSS. It previously took 50--55 hours. Do not launch one on the laptop
  merely to test a weak hypothesis.
* A four-hour objective is not currently demonstrated. Small-gate times do not
  extrapolate through PPMD page faults, preprocessing, and packaging.
* Use SHA-256 and byte length as final truth; process exit status alone is not
  sufficient.
* `fx2-cmix`, cmix, FXCM, and PAQ-family code are GPL-derived. Any distributed
  submission incorporating them must preserve GPL notices, source obligations,
  and attribution.
* No credentials, network service, external model, Qwen model, or hidden corpus
  may be required by the decoder.
* Public neural/SSM results are useful as modeling clues but may use GPUs or
  different rules. They are not automatically Hutter-legal baselines.

## 12. Research evidence and what it means

The relevant public technical sources are:

* [cmix-lex](https://github.com/blahem/cmix-lex): measured 109,650,047-byte
  total, `payload_lex`, FXCM v26, article ordering, and disk-backed PPMD.
* [fx2-cmix](https://github.com/kaitz/fx2-cmix): documented sparse match model,
  predictor pruning, mixer-update reductions, and Wikipedia-specific modeling.
* [PAQ8px technical documentation](https://github.com/hxim/paq8px/blob/master/DOC):
  bitwise arithmetic coding, stretched prediction mixing, and match models as
  probability features rather than mandatory transmitted pointers.
* [Official Hutter Prize rules](https://prize.hutter1.net/): CPU, memory, and
  timing requirements.
* [StateSMix paper](https://arxiv.org/abs/2605.02904): a research lead about
  selective state-space mixing, not a validated Hutter result.

The rigorous implication is not that any named model must work. It is that an
upgrade must be decoder-causal, lossless, benchmark-accounted, and measured on
an identical input. A paper, web claim, Shannon bound, or hindsight oracle is
evidence for what to test, not evidence that the local compressor improved.

## 13. Recommended handoff procedure

1. Read `README.md`, `changes.md`, and the entire `EXPERIMENTS.md` before
   changing code.
2. Establish a matched control on `prof_input/input2` with the intended build
   flags.
3. State the proposed causal information source, memory allocation, executable
   cost, and kill threshold before implementing it.
4. Run the smallest exact A/B gate. Require archive bytes, executable bytes,
   encode/decode time, RSS, and both SHA-256 checks.
5. Repeat a promising candidate on the complete-page 941,724-byte input.
6. Promote only a measured charged-total win that does not violate the runtime
   or memory objective.
7. Update `EXPERIMENTS.md` with commands and raw measurements. Do not report an
   optimistic estimate as a result.
8. Run the full 1 GB pipeline only after a candidate shows an improvement on
   the required scale and has enough headroom for executable and transform
   costs.

## 14. New externally supported recurrent-model lead

After this handoff was drafted, a public LiGRU experiment supplied a more
credible scale hypothesis than the rejected direct-table and match variants.
On a 61,607,920-byte `enwik8.fxd` transformed stream, the author reported:

```text
fx2-cmix LSTM-only: 20,148,724 bytes
LiGRU-384:         19,085,215 bytes
LiGRU-416:         18,941,638 bytes
LiGRU-448:         18,891,029 bytes
PPMD-conditioned LiGRU-256: 16,989,819 bytes
```

These are external forum results, not local cmix-lex results. They are useful
because the required cmix-lex improvement is only about 0.04044 bits per
post-WRT byte, but they cannot be linearly extrapolated to enwik9. The
PPMD-conditioned result is more relevant than the standalone LiGRU numbers:
the existing ByteMixer receives byte-model information, so a fair replacement
must preserve that causal auxiliary information.

The proposal is to investigate an opt-in recurrent replacement for the current
ByteMixer/LSTM, not to add a large independent predictor blindly. A candidate
could consume PPMD byte probabilities plus compact FXCM/structural features,
produce one 256-way next-byte distribution per completed byte, and derive its
bit probabilities from the already decoded prefix. A residual or gated output
should be compared with the existing PPMD/FXCM mixture to measure redundancy.

Important caveat: the complete LiGRU implementation and optimizer are not
available in the cited posts. A from-scratch implementation is a new
hypothesis, not a reproduction. The public discussion also notes scalar/SSE/
AVX and fused-multiply-add differences. Deterministic initialization, update
rounding, and cross-machine archive identity must be tested explicitly. Do not
claim that LiGRU weights are “free” until the implementation is proven
decoder-synchronized; online-trained weights are causal state only if encoder
and decoder perform identical updates.

Relevant sources:

* [LiGRU benchmark](https://encode.su/threads/4116-fxcm/page3)
* [PPMD-conditioned LiGRU discussion](https://encode.su/threads/4370-The-new-kid-on-the-block-ligru-compress-an-experimental-file-compressor/page4)
* [PAQ8px model-mixing documentation](https://github.com/hxim/paq8px/blob/master/DOC)

## 15. The unresolved research question

Do not assume the answer. Determine it from source analysis and measured gates:

> **What specific, decoder-causal change to the existing cmix-lex pipeline can
> remove at least 2,964,850 charged bytes (about 2.704%) from the measured
> 109,650,047-byte total, while preserving exact 1,000,000,000-byte SHA-256
> reconstruction, staying under the Hutter CPU/RAM rules, and moving toward a
> practical four-hour-or-less runtime?**

The answer must identify:

```text
where the saved bytes currently occur
why the candidate information is not already represented
how the decoder reconstructs the same state
how executable and side-data cost are charged
why the speed/RSS cost is acceptable
what exact falsification gate can reject it quickly
```

The previous experiments establish that ad-hoc direct tables, small source-rank
variants, generic selectors, and isolated sparse-match additions are not enough
on their own. The next agent should answer the question from the measured
residuals and verified upstream architecture, not from an uncharged oracle or a
placeholder implementation.
### WRT-phase partition gate (rejected)

On 2026-08-31 we ran a bounded, lossless A/B test for the proposed WRT-phase
outer-mixer intervention.  The candidate added an opt-in mixer keyed by the
existing binary `wrt_state_` signal; it did not alter the default codec.
Using the 51,052-byte complete-page gate with PPMD order 12 / 32 MiB:

- Control: 6,129-byte archive, 321,040-byte executable.
- Candidate: 6,129-byte archive, 321,040-byte executable.
- Both outputs had SHA-256
  `3e698082f5ded92f8fad58b94cac0c681da8ecd1e67627320d9a4d0641d46e9b`.

There is therefore no measured compression gain.  Timing varied between runs
but cannot be credited without a byte reduction.  Do not promote this feature
or run it on enwik9.  A future phase experiment would first need an exact
WRT/preprocessor grammar parser and a shadow conditional-loss gain; the simple
three-state claim is invalid because codes are variable length and escaped.

### Existing-family residual oracle (2026-09-01)

The trace build now performs a 33-point fixed logit-blend sweep between the
full final prediction and each existing model family. On the 51,052-byte gate,
the final loss was `0.119292925` bits/bit. Every family had its optimum at
alpha `0` except PPMD, whose optimistic alpha `0.125` blend reached
`0.119244541`, only `0.000048384` bits/bit better (roughly 2.5 bytes on the
gate). This confirms that ordinary reweighting of current families cannot
close the target gap. A serious candidate must provide a new causal
prediction stream, not another blend of existing outputs.

### Residual-physics ledger (2026-09-02)

The trace build now also records loss by the causal `(line_class, wrt_state)`
key and surprise autocorrelation for lags 1--64.  On the 51,052-byte gate the
archive remained 6,129 bytes and the restored SHA-256 matched exactly.  Final
loss was `0.119292901` bits/bit.  The highest observed regional loss was
`0.623648433` bits/bit (region key 15, only 888 bits); the largest well-sampled
region was key 12 at `0.247254110` bits/bit over 30,624 bits.  Covariance stayed
positive through lag 64, but this bit-level statistic is confounded by
within-byte and bit-position effects.  It does not yet establish a semantic
long-range dependency.  Use it to choose measurements, not to promote a
predictor.

### Byte-normalized residual check (2026-09-02)

The trace was rerun after aggregating the eight coded bits of each byte and
adding a first-difference autocorrelation diagnostic.  On the same exact
complete-page gate, the archive remained `6,129` bytes and the restored
SHA-256 remained
`3e698082f5ded92f8fad58b94cac0c681da8ecd1e67627320d9a4d0641d46e9b`.
There were `51,058` complete bytes; mean surprise was `0.119292901` bits/bit
and variance was `0.0661756645`.

Raw byte-surprise covariance was `0.036194927` at lag 1 and `0.0175256833`
at lag 64.  After differencing adjacent byte surprises, lag 1 was
`-0.0276585315`, lag 8 was `-0.000206109776`, and lag 64 was
`-0.000021806`.  The bounded sample therefore does not support another fixed
delayed/geometric history as the route to the target; the raw positive
covariance is mostly nonstationarity and local regime persistence.  A future
strength candidate must supply genuinely new conditional information (for
example a shadow-tested recurrent residual or a separately measured
region-specific expert), not simply a wider raw context table.

Artifact: `profile-0-ledger-diff.tsv`.

### Conditional delta-byte expert (2026-09-02, rejected)

The first genuinely new conditional predictor was implemented as an opt-in
`ConditionalDeltaByteExpert`.  It uses only the two preceding completed bytes,
decoder-known line class, current bit position, and an adaptive agreement
counter for repeat/small-delta/linear-continuation predictions.  It emits no
side information and is therefore decoder-causal.

Exact gate result (`prof_input/input`, profile 0, PPMD order 12 / 32 MiB):
archive `6,126` B versus the same-source default control's `6,129` B; exact
SHA-256 was preserved.  The executable increased from `321,056` to `321,520`
B, making the charged result `461` B worse.  Candidate encode/decode were
`1:28.50/1:10.39` versus control `1:15.22/1:14.52`; peak RSS was
`4,653,872/4,653,336` versus `4,652,776/4,652,744` KiB.  The feature is
retained for further research but must remain disabled by default and must not
be promoted to enwik9.

### Headroom certificate (2026-09-02)

The trace infrastructure now emits the formal headroom certificate from
`MATHEMATICAL_SPEC.md`.  On the exact gate, baseline ideal loss was
`48,726.8554697` bits; the best hindsight blend of an existing family was
`48,707.1022091` bits, only `19.7532606` bits (`2.469` potential bytes).
The certificate is explicitly marked `NON_CAUSAL_ORACLE` and reports charged
saving `0` bytes.  Any future change must provide a decoder-causal (q_t),
charge all executable/side data, and satisfy `S_0 - S_q > 0` after actual
range coding and SHA verification.

Artifact: `profile-0-headroom-certificate.tsv`.

The certificate is explicitly scoped (`profile-0 bounded gate`), names its
hypothesis class (hindsight static reweighting of existing family logits), and
reports coverage (`51,052` bytes).  It also emits the Fisher-curvature bound
for each family.  The largest local bound was 44.699 bits for PPMD, but the
exact hindsight oracle recovered only 19.753 bits and the charged saving was
zero.  The field is labeled `quadratic_local_headroom_bits` to make clear that
it is a local Taylor estimate, not a global upper bound.  The trace also emits
the normalized residual-correlation matrix for redundancy and speed-pruning
analysis.  Neither diagnostic constitutes a codec improvement.
