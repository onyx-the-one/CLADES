# CLADES
**Compartmental Lightweight Agent-based Disease Epidemiology Simulator**

SEIVRD stochastic epidemic model with ImGui frontend. Runs a 5-run Monte Carlo ensemble and averages results to reduce stochastic noise.

## Model

Extended SEIVRD with:
- Pre-symptomatic transmission (E contributes ~30% of I)
- Vaccinated compartment (efficacy + waning)
- Dual waning immunity (natural `omega_r`, vaccine `omega_v`)
- Hospitalisation tracker (14-day mean stay, extra mortality `delta`)
- Seasonal forcing on beta (cosine)
- Ornstein-Uhlenbeck noise on beta (superspreading / environmental stochasticity)
- Euler-Maruyama SDE integration, dt configurable

Outputs: mean + 10th/90th percentile band across ensemble.

## Build

```bash
# Requires: cmake >= 3.18, gcc/clang, libsdl2-dev, libgl-dev
# ImGui, ImPlot fetched automatically via FetchContent

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/clades
```

Should work on Windows/macOS too, SDL2 will be fetched if not found.

## Files
'''
model.hpp - all types (Params, DayState, SimResult)
model.cpp - integrator + ensemble runner
io.hpp/cpp - INI-style param import/export
main.cpp - ImGui/ImPlot frontend
CMakeLists.txt
'''


## Params file

Plain INI, one `key = value` per line, `#` comments. Load/save from the Files tab or directly:

```ini
disease_name = Influenza A
N = 1000000
beta = 0.35
ifr = 0.005
vax_rate = 0.003
T_days = 365
```

## Branch layout
main - stable
dev - integration


## Known gaps / TODOs

- Cancel button doesn't actually stop the sim thread yet (bandaid)
- No age-stratified contact matrix (POLYMOD) yet
- dt > 0.5 can produce numerical drift at high beta — keep dt ≤ 0.5
