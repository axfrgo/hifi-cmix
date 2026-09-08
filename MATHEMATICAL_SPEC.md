# Governing mathematical specification

All future changes are judged by this specification.  Cross-entropy is a
diagnostic; the actual range-coded archive plus executable and side data is
the competition score.

## Reversible representation

For the exact input `x` of 1,000,000,000 bytes:

```text
x -> ordering -> PHDA9 -> WRT -> payload transform -> y
```

Formally:

\[
T=T_{payload}\circ T_{WRT}\circ T_{PHDA9}\circ T_{ordering}
\]

and

\[
T^{-1}=T_{ordering}^{-1}\circ T_{PHDA9}^{-1}\circ
T_{WRT}^{-1}\circ T_{payload}^{-1}.
\]

The required invariant is `T^-1(T(x)) = x`.  The measured transformed length
is `|y| = 586,459,321` bytes.

## Causal prediction

Let the complete design be

\[
\mathcal A=(T,\mathcal M,\theta,\pi),
\]

where `M` is the model portfolio, `theta` contains parameters and update rules,
and `pi` controls gating, allocation, update cadence, and scheduling.

For transformed bit `b_t`,

\[
s_t=F_{\mathcal A}(s_{t-1},b_{t-1},z_t),
\]

where `z_t` is deterministic structural state derivable from already decoded
data.  Each expert supplies `p_i(t) = P_i(b_t=1 | s_t)`.  The mixer is

\[
\ell_t=\sum_{i\in\mathcal M}w_i(s_t)\operatorname{logit}p_i(t)-c(s_t),
\qquad p_t=\sigma(\ell_t),
\]

followed by calibration `p_hat_t = A(p_t,s_t)`.

## Charged score

The ideal diagnostic loss is

\[
L_{ideal}=\sum_t-\log_2 P_{\hat p_t}(b_t).
\]

Actual coded bytes are

\[
C(y;\mathcal A)=\left\lceil\frac{L_{ideal}+\varepsilon_{coder}}{8}\right\rceil.
\]

Therefore the charged score is

\[
\boxed{S(\mathcal A)=E(\mathcal A)+H(\mathcal A)+C(y;\mathcal A)}
\]

where `E` is executable size and `H` is charged headers, dictionaries, and
side data.  These costs add; none is subtracted.

The design objective is

\[
\boxed{\mathcal A^*=\operatorname*{arg\,min}_{T,\mathcal M,\theta,\pi}S(\mathcal A)}
\]

subject to exact restoration and SHA-256 equality, decoder causality,
`RSS < 10 GB`, contest time limits, and zero external information.

Current measured baseline and target:

\[
S_0=109,650,047,\qquad S^*=106,685,197,
\]

\[
G_{required}=2,964,850\text{ bytes}
\]

which equals `0.040444067` bits per transformed byte.

## Conditional candidate value

For a new prediction `q_t`, define

\[
r_t=\operatorname{logit}(q_t)-\operatorname{logit}(p_t^{ref}).
\]

A consistent residual blend is

\[
\ell_t^{(q)}=\ell_t^{(0)}+\alpha_t r_t,
\qquad p_t^{(q)}=\sigma(\ell_t^{(q)}).
\]

The conditional information and charged value are

\[
I(q|\mathcal A_0)=L(\mathcal A_0)-L(\mathcal A_0+q),
\]

\[
\boxed{G(q)=I(q|\mathcal A_0)/8-\Delta E_q-\Delta H_q}.
\]

Standalone model loss is not a promotion criterion because redundancy is the
dominant failure mode.

## Regions, resources, and promotion

For region `R_r`,

\[
I_r(q)=\sum_{t\in R_r}[-\log_2P_0(b_t)+\log_2P_q(b_t)],
\qquad d_r(q)=I_r(q)/|R_r|.
\]

Model selection is a resource-constrained portfolio problem:

\[
G_{A+B}=G_A+G_B-R_{AB},
\]

with total memory and computation below their legal limits.  A candidate must
prove causal shadow gain, actual complete-page archive savings, exact SHA,
charged executable/side-data cost, CPU time, and RSS.  The research gate is
at least 3.5 MB projected saving, not merely the 2.965 MB minimum.

## Residual field and curvature screening

The baseline surprise field is

\[
e_t=-\log_2P_0(b_t).
\]

Its autocorrelation is

\[
C(k)=\mathbb E[(e_t-\bar e)(e_{t+k}-\bar e)],
\]

which identifies whether missing information is local, recurrent, structural,
or long-range.  This is diagnostic only.

For a candidate residual \(r_t\), let

\[
g_t=p_t-b_t,\qquad h_t=p_t(1-p_t).
\]

The second-order loss approximation is

\[
L(\alpha)\approx L(0)+\alpha\sum_tg_tr_t+
\frac{\alpha^2}{2}\sum_th_tr_t^2.
\]

Therefore

\[
\alpha^*=-\frac{\sum_tg_tr_t}{\sum_th_tr_t^2},
\qquad
\Delta L^*\approx
\frac{(\sum_tg_tr_t)^2}{2\sum_th_tr_t^2}.
\]

This is an optimistic screening ceiling, not a legal archive result.  For
multiple candidates, with residual matrix \(R\),

\[
A=R^THR,\qquad b=R^Tg,\qquad \alpha^*=-A^{-1}b,
\]

and the quadratic gain ceiling is \(\tfrac12b^TA^{-1}b\).  Off-diagonal
entries expose candidate redundancy.

## Resource-aware selection

If candidate \(i\) saves \(\Delta B_i\) bits and costs CPU cycles \(C_i\),
memory traffic \(D_i\), memory \(M_i\), executable bytes \(\Delta E_i\),
and side-data bytes \(\Delta H_i\), define

\[
U_i=\Delta B_i-\lambda_CC_i-\lambda_DD_i-
\lambda_MM_i-8\Delta E_i-8\Delta H_i.
\]

An optional model is executed only when its estimated conditional value exceeds
its resource price:

\[
u_i(s)=\mathbf1[V_i(s)>K_i(s)].
\]

The estimates must themselves be synchronized from causal decoder state.

For speed, distinguish computation from memory latency and page faults:

\[
\tau\gtrsim\max\left(
\frac{N_{inst}}{IPC\,f},\frac{B_{RAM}}{BW_{RAM}},
N_{miss}L_{mem},N_{fault}L_{disk}\right).
\]

This is why PPMD locality, node layout, and paging must be measured before
micro-optimizing arithmetic.

## Transform, ordering, and evidence ladder

The transform objective is downstream coded cost, not intermediate size:

\[
T^*=\operatorname*{arg\,min}_T
\left[L_{\mathcal A}(T(x))+8H_T+8E_T\right].
\]

For article order \(\pi\), a compressor-native approximation uses transition
costs \(c_{ij}=L(A_j\mid\text{state after }A_i)-L(A_j\mid\text{neutral})\),
followed by exact verification.

Every candidate must pass this evidence ladder:

```text
current baseline
→ scalar hindsight blend
→ regional hindsight blend
→ causal adaptive blend
→ actual range-coded candidate
→ charged score
```

Only the final two levels support promotion claims.

## Headroom certificate

For a fixed input of (n) coded bits, let (p_t^0) be the frozen baseline
probability and (q_t) a proposed decoder-causal probability.  Define their
ideal losses and charged costs as

\[
L_0=\sum_{t=1}^{n}-\log_2 p_t^0(b_t),\qquad
L_q=\sum_{t=1}^{n}-\log_2 q_t(b_t\mid s_t^q),
\]

\[
S_0=E_0+H_0+\left\lceil\frac{L_0+\epsilon_0}{8}\right\rceil,
\qquad
S_q=E_q+H_q+\left\lceil\frac{L_q+\epsilon_q}{8}\right\rceil.
\]

The only valid improvement certificate is

\[
\boxed{\mathsf{CertifiedHeadroom}(q)=S_0-S_q>0.}
\]

Equivalently, before integer rounding, the candidate must satisfy

\[
L_0-L_q > 8[(E_q-E_0)+(H_q-H_0)] +(\epsilon_q-\epsilon_0).
\]

This inequality is a theorem only when (q_t) is generated from state already
known to the decoder, the model description/side data are charged, and the
range-coded candidate is actually reconstructed and hash-verified.  A
non-causal empirical distribution (q_t^\mathrm{oracle}) may report only

\[
\mathsf{PotentialHeadroom}=L_0-L_{q^\mathrm{oracle}},
\]

which is explicitly a bound and has zero certified charged saving.  The
`CMIX_MODEL_TRACE=1` output records this distinction in its
`headroom_certificate` section.

## Modification classes and novel-information certificate

Changes are classified as recombination, representation, or new information:

\[
\text{recombination}:\{p_i\}\to\text{different mixer},\qquad
\text{representation}:s_t\to s'_t,\qquad
\text{new information}:I(B_t;Q_t\mid S_t)>0.
\]

For a shadow candidate (q_t), with reference prediction (p_t), define

\[
r_t=\operatorname{logit}(q_t)-\operatorname{logit}(p_t),\quad
A=\sum_t(p_t-b_t)r_t,\quad
B=\sum_t p_t(1-p_t)r_t^2.
\]

The local quadratic (Fisher-curvature) screening estimate is

\[
\boxed{I^{(2)}_{\max}(q)=\frac{A^2}{2B\ln 2}}.
\]

This is a rejection bound only: it is not a coded saving and cannot certify a
candidate by itself.  A candidate proceeds only if its causal shadow and then
its charged range-coded score satisfy the headroom certificate above.

For residuals (r_i) from multiple existing families, the trace also records

\[
K_{ij}=\sum_t p_t(1-p_t)r_{i,t}r_{j,t},\qquad
\rho_{ij}=\frac{K_{ij}}{\sqrt{K_{ii}K_{jj}}}.
\]

This is a redundancy diagnostic for speed pruning, not a compression proof.
