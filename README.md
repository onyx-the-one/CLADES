# CLADES
**Compartmental Lightweight Agent-based Disease Epidemiology Simulator**

A stochastic, multi-mode epidemiological simulation tool with a retro-style desktop GUI.  
Supports homogeneous SEIVRD, age-stratified (POLYMOD contact matrix), and multi-strain competition models.  
Ensemble averaging (5 independent runs) removes single-run stochastic luck.

---

## Features

- **Homogeneous SEIVRD** — S → E → I → V/R/D with vaccination, waning immunity, seasonal forcing, hospitalisation tracking
- **Age-stratified** — 8 age groups (0–9 … 70+), POLYMOD contact matrix (editable), per-group IFR / hosp rate / vax uptake
- **Multi-strain** — up to 6 concurrent strains, asymmetric cross-immunity matrix, scheduled variant introductions, optional superinfection
- **Stochastic ensemble** — Ornstein-Uhlenbeck process on β, 5 runs averaged with 10th–90th percentile bands
- **Import / Export** — all parameters saved/loaded as plain `.ini` files; results exported to CSV
- **Retro GUI** — Dear ImGui + ImPlot, Win95-style theme

---

## Building from source

### Prerequisites

| Dependency | Notes |
|---|---|
| CMake ≥ 3.18 | `sudo apt install cmake` |
| C++17 compiler | GCC 10+ or Clang 12+ |
| SDL2 dev headers | `sudo apt install libsdl2-dev` (optional — auto-downloaded if missing) |
| OpenGL | `sudo apt install libgl-dev` |

SDL2, Dear ImGui and ImPlot are fetched automatically via CMake `FetchContent` if not found locally.

### Linux (primary)

```bash
git clone https://github.com/YOUR_USER/clades.git
cd clades
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/clades
```

### Windows (MSVC or MinGW)

```bat
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
build\Release\clades.exe
```

### macOS

```bash
brew install cmake sdl2
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu)
./build/clades
```

---

## Quick start

1. Launch `clades`
2. In the **Parameters** tab (left pane), set your population size, R0-equivalent beta, simulation duration, etc.
3. Press **Run Simulation** — progress bar shows ensemble progress
4. Switch between **Plots**, **Age Plots**, **Strain Plots** tabs (right pane) to view results
5. Use **File → Save Params** to export your configuration; **File → Load Params** to restore it

### Enabling age-stratified mode

1. Go to the **Age Groups** tab (left pane)
2. Check **Enable age-stratified mode**
3. Optionally check **Also run homogeneous (both results)** to get both sets of plots simultaneously
4. Click **Edit Contact Matrix...** to modify the POLYMOD mixing matrix
5. Run simulation — results appear under **Age Plots** and **Age Summary**

### Enabling multi-strain mode

1. Go to the **Strains** tab (left pane)
2. Check **Enable multi-strain mode**
3. Set number of strains (1–6); configure each strain's β, σ, γ, IFR, introduction day/size
4. Click **Edit Cross-Immunity Matrix...** to set pairwise cross-protection
5. Run simulation — results appear under **Strain Plots** and **Strain Summary**

---

## Parameter reference

### Global (Parameters tab)

| Parameter | Symbol | Typical range | Description |
|---|---|---|---|
| Population | N | 10 000 – 100 000 000 | Total population size |
| beta | β | 0.1 – 1.0 | Transmission rate per contact per day |
| sigma | σ | 0.1 – 0.5 | 1 / mean incubation period (days⁻¹) |
| gamma | γ | 0.05 – 0.3 | 1 / mean infectious period (days⁻¹) |
| IFR | δ_IFR | 0 – 0.1 | Infection fatality ratio |
| Hosp rate | h | 0 – 0.3 | Fraction of infections requiring hospitalisation |
| omega_r | ω_r | 0 – 0.01 | Natural immunity waning rate (day⁻¹) |
| omega_v | ω_v | 0 – 0.01 | Vaccine immunity waning rate (day⁻¹) |
| Vax coverage | V_cov | 0 – 1 | Target fraction of population vaccinated |
| Vax rate | V_rate | 0 – 0.01 | Daily fraction of susceptibles vaccinated |
| Vax efficacy | V_eff | 0 – 1 | Probability vaccination prevents infection |
| mu | μ | ~0.00003 | Background birth/death rate (day⁻¹) |
| delta_H | δ_H | 0 – 0.05 | Additional mortality rate for hospitalised |
| T_days | T | 30 – 3650 | Simulation duration (days) |
| dt | dt | 0.1 – 1.0 | Integration step size (days); smaller = more accurate |
| Season amp | A | 0 – 0.5 | Seasonal forcing amplitude on β |
| Season phi | φ | 0 – 365 | Day of peak transmission |
| Noise theta | θ | 0 – 1 | OU mean-reversion rate for stochastic β |
| Noise sigma | σ_n | 0 – 0.5 | OU noise intensity |
| E0 | E₀ | 1 – 1000 | Initial exposed seed size |

### Per-strain (Strains tab)

| Parameter | Description |
|---|---|
| beta | Strain-specific transmission rate |
| sigma / gamma | Strain-specific latent / infectious rates |
| IFR | Strain-specific infection fatality ratio |
| Vax cross-protection | How well the baseline vaccine protects against this strain (0–1) |
| Intro day | Day the strain is seeded into the population |
| Intro size | Number of index cases introduced |

### Cross-immunity matrix X[k][j]

`X[k][j]` = fraction of protection that prior infection with strain **j** gives against strain **k**.  
- `1.0` = full cross-immunity (strain k cannot reinfect recovered-from-j individuals)  
- `0.0` = no cross-immunity  
- Diagonal is ignored (same-strain reinfection is handled by the waning omega parameter)

---

## File formats

All parameter files are plain text INI-style (`key = value`), human-editable.

| File | Contents |
|---|---|
| `clades_params.ini` | Global simulation parameters |
| `clades_age.ini` | Age group fractions, per-group IFR/hosp/vax, contact matrix |
| `clades_strains.ini` | Per-strain parameters, cross-immunity matrix |
| `clades_results.csv` | Exported time-series (day, S, E, I, R, D, V, H, Rt) |

---

## Pre-built binaries

Pre-built binaries for Linux x86_64 are attached to each [GitHub Release](../../releases).  
Download `clades-linux-x86_64`, make executable, and run:

```bash
chmod +x clades-linux-x86_64
./clades-linux-x86_64
```

Windows and macOS builds are CI-generated on tagged releases.

---

## Theoretical background

- **Base model**: SEIVRD with Ornstein-Uhlenbeck stochastic forcing on β (Euler-Maruyama integration)
- **Age stratification**: WAIFW force-of-infection \( \lambda_i = \beta \sum_j C_{ij} I_j / N_j \) with POLYMOD European contact matrix (Mossong 2008)
- **Multi-strain**: Shared susceptible pool (Castillo-Chavez formulation), asymmetric cross-immunity (Johnston, Pell & Rubel 2023), proportional outflow capping for numerical stability
- **Ensemble**: 5 independent MT19937-seeded runs; mean ± 10th/90th percentile band reported

---

## License

MIT
