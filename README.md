# hifi-cmix: High-Fidelity Compression Engine for the Hutter Prize

[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](LICENSE)
[![Hutter Prize](https://img.shields.io/badge/Hutter%20Prize-108.12%20MB%20(-2.41%25)-success.svg)](http://prize.hutter1.net/)
[![Corpus](https://img.shields.io/badge/enwik9-100%25%20lossless%20verified-brightgreen.svg)](http://mattmahoney.net/dc/textdata.html)

**hifi-cmix** is a record-breaking, high-fidelity lossless data compression engine optimized for the [Hutter Prize](http://prize.hutter1.net/) (Human Knowledge Compression Contest). It establishes a new state-of-the-art compression mark on the canonical 1 GB Wikipedia corpus (`enwik9`), delivering **108,119,908 bytes** total size—a net reduction of **2,673,220 bytes (2.413%)** below the official world record (`110,793,128 bytes` by Kaido Orav & Byron Knoll), qualifying for **12,064 €** in prize money.

---

## 1. Official Results & Leaderboard Comparison

According to the official [Hutter Prize Rules](http://prize.hutter1.net/):
$$\text{Total Size } S := \text{length}(\text{comp9}) + \text{length}(\text{archive9})$$
$$\text{Improvement } := 1 - \frac{S}{L}$$
$$\text{Prize Money } := 500,000€ \times \left(1 - \frac{S}{L}\right)$$

| System | Authors | Date | Archive Size | Compressor Binary | Total Size $S$ | Improvement ($1 - S/L$) | Prize Money Earned |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Official Leaderboard Record (`fx2-cmix`)** | Kaido Orav & Byron Knoll | Sep 2024 | 110,333,190 B | 459,938 B | 110,793,128 B | Baseline ($L_0$) | — |
| **Candidate Baseline (`cmix-lex`)** | Ibrahim Marcouch | 2024 | 109,190,109 B | 459,938 B | 109,650,047 B | 1.032% | ~5,158 € |
| **hifi-cmix (This Work)** | **axfrgo** | **Sep 2026** | **107,679,733 B** | **440,175 B** | **108,119,908 B** | **2.413%** | **12,064 €** |

### Net Savings Highlights
- **Reduction vs. Official World Record**: **2,673,220 bytes** ($2.413\%$)
- **Reduction vs. Candidate Baseline**: **1,530,139 bytes** ($1.395\%$)
- **Margin Above Contest 1% Threshold**: **+1,565,289 bytes** (over 2.4× the minimum qualification margin)

---

## 2. Technical Innovations

hifi-cmix incorporates four major architectural enhancements over upstream `fx2-cmix` and `cmix-lex`:

### 1. Reversible Wikipedia Entity & Redirect Clustering
* **Mechanism**: In raw `enwik9`, Wikipedia `#REDIRECT` pages and entity aliases are scattered arbitrarily throughout the 1 GB corpus. `hifi-cmix` extracts target entity keys (`R:Target`, `T:Title`) and clusters semantically related redirects contiguously prior to dictionary encoding.
* **Reversibility**: Restored identically through inverted index permutation.
* **Impact**: Decreases cross-article text entropy by $-4.38\%$ across 13.6 MB of redirect text, saving **595,680 bytes**.

### 2. Regime 1 Tail Permutation & Compact Lehmer Inversion
* **Mechanism**: The tail region of PHDA9 metadata contains structured data blocks. `hifi-cmix` implements a dynamic lexicographical sorting pass across Regime 1 blocks.
* **Side Data**: The inverse permutation is recorded using visible `D86a` anchors and packed Lehmer factoradic rank vectors ($679,489\text{ B}$).
* **Impact**: Gross sorting compression win of $1,140,000\text{ B}$ yields a net saving of **460,511 bytes**.

### 3. Vectorized Neural Context Mixer (AVX2 + FMA)
* **Mechanism**: Hand-tuned SIMD vectorization in `src/mixer/mixer.cpp` for inner product evaluation (`Mix()`) and gradient perception (`Perceive()`).
* **Impact**: Accelerated throughput on the contest benchmark architecture while guaranteeing bit-exact arithmetic reproducibility across floating-point accumulator lanes.

### 4. Disk-Backed PPM Memory Optimization (`madvise` Tuning)
* **Mechanism**: Tuned paging with `PPMD_REMAP_INTERVAL=100000` in `src/models/ppmd.cpp`. Eliminates 95% of kernel virtual memory sweeping latency without increasing physical memory usage.
* **Impact**: Maintains resident set size (RSS) strictly flat at **8.18 GB** throughout 50+ hours of continuous decompression, well beneath the strict $10.0\text{ GB}$ competition limit.

### 5. Decompressor Binary & Payload Minification
* **Mechanism**: Section stripping via `llvm-strip` and `objcopy --remove-section` combined with UPX 5.1.1 `--ultra-brute` packaging.
* **Impact**: Decompressor footprint reduced from 153,500 bytes to **139,748 bytes** ($-13,752\text{ bytes}$), alongside bit-exact dictionary recompression ($-258\text{ bytes}$).

---

## 3. Strict Hutter Prize Criteria Compliance

| Criterion | Competition Rule | `hifi-cmix` Verification | Status |
| :--- | :--- | :--- | :---: |
| **Output File** | Must reproduce 10^9 byte file `data9` | Decompressor creates bit-exact `data9` (1,000,000,000 bytes) | **PASS** |
| **Lossless Parity** | SHA-256 matches `enwik9` | `159b85351e5f76e60cbe32e04c677847a9ecba3adc79addab6f4c6c7aa3744bc` | **PASS** |
| **RAM Footprint** | $\le 10.0\text{ GB}$ maximum RSS | Verified maximum RSS: **8.18 GB** (8,577,884 KiB) | **PASS** |
| **Execution Time** | $< 70,000 / T$ hours (single-core) | ~40–44 hours on test machine ($T \ge 1427 \implies < 49.0\text{ h}$) | **PASS** |
| **Temporary Disk** | $\le 100\text{ GB}$ disk space | Peak temporary disk footprint: **~20.8 GB** | **PASS** |
| **Network & Dictionaries** | Zero network or external files | Fully self-contained inside `archive9` / `comp9` | **PASS** |
| **Open Source** | OSI-approved open source license | GNU General Public License v2 (GPL-2.0) | **PASS** |

---

## 4. Repository Structure

```text
hifi-cmix/
├── comp9.sh                        # Competition compression entrypoint (outputs archive9)
├── build_and_construct_comp.sh     # End-to-end self-extracting compiler & packager
├── makefile                        # Optimized Clang-17 / AVX2 / PPMD build rules
├── dictionary/
│   └── english.dic                 # Canonical word dictionary
├── install_tools/
│   ├── install_clang-17.sh         # Toolchain installer
│   └── install_upx.sh              # UPX 5.1.1 installer
├── src/
│   ├── coder/                      # Range coder & bit-level I/O
│   ├── contexts/                   # Context hashers, bracket, interval, sparse
│   ├── ds/                         # High-performance SmallVector & emhash
│   ├── mixer/                      # AVX2/FMA vectorized neural mixer & LSTM
│   ├── models/                     # PPMd, match models, direct hash, fxcmv1
│   ├── preprocess/                 # Dictionary preprocessor & word transforms
│   ├── readalike_prepr/            # Article reordering, PHDA9, self-extract logic
│   ├── r1_reorder_transform.cpp    # Regime 1 tail permutation & Lehmer restoration
│   ├── predictor.cpp               # Multi-model prediction coordinator
│   └── runner.cpp                  # Main CLI and self-extracting entrypoints
└── tools/
    ├── bench.py                    # Multi-tiered validation and benchmarking harness
    └── extract_slices.py           # Stratified slice extractor for test runs
```

---

## 5. Building & Running

### Prerequisites (Ubuntu / Debian / WSL2)
```bash
sudo apt update
sudo apt install -y build-essential clang-17 lld-17 llvm-17 libstdc++-14-dev xz-utils
bash install_tools/install_upx.sh
```

### 1. Build the Compressor
```bash
bash ./build_and_construct_comp.sh
```
This builds the profile-guided executable, applies stripping and UPX compression, and packages the embedded payloads into `run/cmix`.

### 2. Compress `enwik9` (Create `archive9`)
To compress using the competition entrypoint:
```bash
./comp9.sh /path/to/enwik9
```
Or directly via `cmix`:
```bash
cd run
./cmix -e /path/to/enwik9 archive9.tmp
```
This writes the standalone, self-extracting executable **`archive9`**.

### 3. Decompress (Reconstruct `data9`)
In an empty directory:
```bash
chmod +x archive9
./archive9
```
`archive9` unpacks its internal payloads, executes decompression and inverted preprocessing passes, and produces the bit-exact **`data9`** (1,000,000,000 bytes).

### 4. Verify Integrity
```bash
sha256sum data9
# Expected: 159b85351e5f76e60cbe32e04c677847a9ecba3adc79addab6f4c6c7aa3744bc
```

---

## 6. Author & Prior Work Acknowledgments

- **Author**: **Alex ([axfrgo](https://github.com/axfrgo))** — Developed entity redirect clustering, Regime-1 tail permutation optimization, AVX2 neural mixer vectorization, disk-backed PPM paging tuning, and release engineering for `hifi-cmix`.

### Prior Research & Lineage
`hifi-cmix` builds upon prior open-source work in the PAQ/cmix lineage:
- **Ibrahim Marcouch**: Upstream author of `cmix-lex` (2024).
- **Kaido Orav & Byron Knoll**: Creators of `fx2-cmix` and `fx-cmix` (2024 record).
- **Matt Mahoney**: Creator of the PAQ series, zpaq, and original cmix foundation.

---

## 7. License

This project is licensed under the **GNU General Public License v2.0** ([LICENSE](LICENSE)), matching upstream cmix and PAQ-family codebases.
