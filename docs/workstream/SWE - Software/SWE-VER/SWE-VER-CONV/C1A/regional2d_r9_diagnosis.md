# R9 Regional2D Reconstruction Verification

Second-order classification: `SECOND_ORDER_VERIFIED`

The mathematical NLSWE model, source physics, boundary roles, and coupling quantities are unchanged. R9 changes only the spatial face-state reconstruction option.

The production implementation preserves `first_order` as the default and adds an opt-in `limited_linear` reconstruction of `(eta,u,v)` with the existing hydrostatic bed step and Rusanov flux.

## Finest Limited-Linear MMS Orders

- h/mass L1/L2/Linf: `2.35534`, `2.32261`, `2.0907`
- qx L1/L2/Linf: `2.23289`, `2.21635`, `1.54603`
- qy L1/L2/Linf: `2.22464`, `2.16137`, `1.13482`

## Smooth-Wave Proxy

Amplitude error reduction: `5.34752`.

Phase-proxy error reduction: `17.4255`.

## Event Gate

Production Tohoku convergence may resume only if the build/test matrix and robustness gates remain green; h300 and temporal convergence remain gated.
