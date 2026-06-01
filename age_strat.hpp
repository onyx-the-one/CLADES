#pragma once
#include <array>
#include <string>
#include <vector>
#include <cstddef>

// Age-stratified extension for CLADES.
// 8 groups matching the standard 10-year bands used in COVID-era modelling
// (0-9, 10-19, 20-29, 30-39, 40-49, 50-59, 60-69, 70+).
// See: Mossong et al. POLYMOD 2008; Prem et al. 2017 projections.

static constexpr int N_AGE = 8;

static constexpr const char* AGE_LABELS[N_AGE] = {
	    "0-9", "10-19", "20-29", "30-39",
    "40-49", "50-59", "60-69", "70+"
};

// POLYMOD-derived all-contacts matrix (European average, symmetric-corrected).
// Rows = participant age group, columns = contact age group.
// Values = mean contacts per day. Source: Mossong 2008 + Prem 2017 projections.
// This is a reasonable generic starting point; users can override per-cell.
static constexpr double POLYMOD_DEFAULT[N_AGE][N_AGE] = {
	    // 0-9   10-19  20-29  30-39  40-49  50-59  60-69  70+
    { 7.00,  1.20,  0.90,  2.40,  1.10,  0.60,  0.30,  0.20 }, // 0-9
    { 1.20, 12.50,  1.80,  1.10,  0.80,  0.60,  0.25,  0.15 }, // 10-19
    { 0.90,  1.80,  8.20,  2.80,  1.20,  0.80,  0.30,  0.15 }, // 20-29
    { 2.40,  1.10,  2.80,  7.80,  2.10,  1.10,  0.50,  0.20 }, // 30-39
    { 1.10,  0.80,  1.20,  2.10,  6.50,  2.00,  0.70,  0.30 }, // 40-49
    { 0.60,  0.60,  0.80,  1.10,  2.00,  5.80,  1.40,  0.55 }, // 50-59
    { 0.30,  0.25,  0.30,  0.50,  0.70,  1.40,  4.50,  1.20 }, // 60-69
    { 0.20,  0.15,  0.15,  0.20,  0.30,  0.55,  1.20,  3.20 }, // 70+
};

// Age-specific IFR baseline (rough COVID-like gradient; user can override).
// Represents the probability of death given infection per age group.
static constexpr double IFR_DEFAULT[N_AGE] = {
	    0.00002, 0.00006, 0.0003, 0.0008,
    0.0025,  0.010,   0.040,  0.130
};

// Age-specific hospitalisation rate baseline
static constexpr double HOSP_DEFAULT[N_AGE] = {
	    0.001, 0.003, 0.010, 0.020,
    0.040, 0.080, 0.140, 0.200
};

struct AgeParams {
	    // Relative population fractions (must sum to 1; user editable).
    // Default: rough European/WHO distribution.
    std::array<double, N_AGE> pop_frac = {
	        0.11, 0.11, 0.13, 0.14,
        0.14, 0.13, 0.12, 0.12
    };

    // Contact matrix C[i][j] — editable per cell in the UI
    double C[N_AGE][N_AGE];

    // Per-age-group parameters (relative to global baseline; multiplied in)
    std::array<double, N_AGE> ifr;        // infection fatality ratio
    std::array<double, N_AGE> hosp_rate;  // hospitalisation fraction
    std::array<double, N_AGE> vax_uptake; // fraction of group willing to vaccinate

    bool enabled = false; // if false, model falls back to homogeneous

    AgeParams() {
	        for (int i = 0; i < N_AGE; ++i) {
	            for (int j = 0; j < N_AGE; ++j)
                C[i][j] = POLYMOD_DEFAULT[i][j];
            ifr[i]       = IFR_DEFAULT[i];
            hosp_rate[i] = HOSP_DEFAULT[i];
            vax_uptake[i]= 0.8;
        }
        // last age group lower uptake (vaccine hesitancy / access)
        vax_uptake[0] = 0.5; // children — parental decision
    }
};

// Per-age-group state vector
struct AgeState {
	    std::array<double, N_AGE> S, E, I, R, V, D, H;
    std::array<double, N_AGE> Rt; // group-level Rt approximation
};

struct AgeDayState {
	    AgeState s;
    double beta_eff; // shared OU-perturbed beta at this step
};

struct AgeRunResult {
	    std::vector<AgeDayState> ts;
    std::array<double, N_AGE> peak_I;
    std::array<double, N_AGE> peak_day;
    std::array<double, N_AGE> total_D;
    double total_attack_rate;
};

struct AgeSimResult {
	    AgeRunResult mean;
    AgeRunResult lo, hi;
    int n_runs;
};
