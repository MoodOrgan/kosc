# kosc

Dual-ladder harmonic resonator for Bela Gem Multi — edit on Mac in Cursor, deploy with `bela-tools`.

## Spectra (ladders)

Two **node ladders** (11 rungs each), ratios: ⅛, ⅙, ¼, ⅓, ½, 1, ³⁄₂, 2, ⁵⁄₂, 3, ⁷⁄₂ of each ladder’s F0:

- **Ladder A** (audio out L): detuned **down** from center `F0`
- **Ladder B** (audio out R): detuned **up** from center `F0`
- Ladders always run with independent phases (detune floored at `FLT_MIN` when the slider is at zero)

## I/O (current wiring)

| Jack | Path |
|------|------|
| **Audio in 0 / 1** | **All in** A / B — uniform drive on every node |
| **Audio out 0 / 1** | **Scan out** A / B — crossfade between adjacent rungs (`Output A/B Scan` CV) |
| **SYNC** (digital in 0) | Rising edge resets **both** ladders (shared F0) |

Scan in on dedicated jacks is planned; **Input A/B Scan** CV is reserved for that.

## Signal flow (per frame)

1. **All in** on audio per ladder
2. Per node: uniform drive → bandpass → envelope → sine (slow family-correlated drift)
3. **Node coupling** — envelope diffusion along each ladder (ratio-weighted)
4. **Ladder coupling** — same diffusion law across matching rungs on the other ladder
5. Energy budget + coupling memory on both paths
6. **Scan out** on main audio outputs

## Controls (GUI — dev; map to CV + digital SYNC)

| Control | Role |
|--------|------|
| F0 (Hz) | Center frequency (both ladders) |
| Detune | A = F0×(1−d), B = F0×(1+d) |
| Osc Phase | Global offset [0, 2π] |
| Node Coupling | Intra-ladder envelope diffusion [0, 2] |
| Ladder Coupling | Cross-ladder envelope diffusion (symmetric) [0, 2] |
| Input A/B Scan | Reserved for future scan-in jacks |
| Output A/B Scan | Scan-out position on main audio outputs |
| Node Env Attack / Decay | Envelope shape |
| In/Out A/B Peak (1=clip) | Peak hold meters (aux task → GUI) |

**Planned:** coupling sliders as **macros** (envelope decay and other controls interact — needs careful tuning).

Internal: energy reserve, drift depth, coupling memory (`render.cpp`).

## Deploy

```bash
bela deploy ~/scripts/kosc
```

Or Cursor: **Run Task → Bela: Deploy**
