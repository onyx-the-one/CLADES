#pragma once
#include "model.hpp"
#include <array>
#include <string>
#include <vector>

// Multi-strain extension for CLADES.
// Up to MAX_STRAINS strains compete in a shared susceptible pool.
//
// Model structure per strain k (shared S pool):
//   S  -> E_k  : beta_k * S * I_k / N  (cross-immunity from prior strains reduces this)
//   E_k -> I_k : sigma_k * E_k
//   I_k -> R_k : gamma_k * (1 - ifr_k) * I_k
//   I_k -> D   : gamma_k * ifr_k * I_k
//   R_k -> S   : omega_k * R_k   (waning, strain-specific)
//
// Cross-immunity matrix X[k][j] in [0,1]:
//   = 1 means prior infection with strain j gives full protection against k
//   = 0 means no cross-protection at all
//   Effective susceptibility of R_j to strain k = (1 - X[k][j])
//
// Vaccine cross-protection: vax_cross[k] in [0,1] — how well the vaccine
// (tuned to reference strain) protects against strain k.
//
// Superinfection: someone in I_j can be co-infected by strain k at rate
//   superinf_factor * beta_k * I_j_infected * I_k / N
//   (modelled as direct S->I_k flow from I_j compartment, small correction term)
//   Disabled when superinf_factor = 0.
//
// Reference: Johnston, Pell, Rubel 2023 (MBE); PMC9049317 generic multi-strain model.

static constexpr int MAX_STRAINS = 6;

struct StrainParams {
	    std::string name   = "Strain";
    double beta        = 0.3;   // transmission rate
    double sigma       = 0.2;   // 1/incubation
    double gamma       = 0.1;   // 1/infectious period
    double ifr         = 0.01;  // infection fatality ratio
    double hosp_rate   = 0.05;
    double omega       = 0.003; // waning rate R->S
    double vax_cross   = 0.85;  // vaccine cross-protection [0,1]
    double intro_day   = 0.0;   // day strain is seeded (0 = at t=0)
    double intro_size  = 100.0; // number introduced at intro_day
    bool   enabled     = false;
};

struct MultiStrainParams {
	    int    n_strains   = 2;
    std::array<StrainParams, MAX_STRAINS> strains;

    // cross_immunity[k][j]: protection strain j confers against strain k
    double cross_immunity[MAX_STRAINS][MAX_STRAINS] = {};

    double superinf_factor = 0.0; // 0 = disabled

    MultiStrainParams() {
	        // strain 0: baseline (copies global Params.beta etc at runtime)
        strains[0].name    = "Wild-type";
        strains[0].enabled = true;
        strains[0].intro_day = 0;

        // strain 1: more transmissible variant, partial cross-immunity default
        strains[1].name    = "Variant-A";
        strains[1].beta    = 0.5;
        strains[1].ifr     = 0.008;
        strains[1].enabled = true;
        strains[1].intro_day  = 60;
        strains[1].intro_size = 50;

        // default cross-immunity: symmetrical 50% between strain 0 and 1
        cross_immunity[0][1] = 0.5;
        cross_immunity[1][0] = 0.5;
    }
};

// Per-strain compartments (E, I, R share the same S pool globally)
struct StrainCompartments {
	    std::array<double, MAX_STRAINS> E{}, I{}, R{}, D{}, H{};
};

struct MSDay {
	    double S, V;          // shared susceptible + vaccinated
    StrainCompartments c;
    std::array<double, MAX_STRAINS> Rt{};
    double beta_eff;
};

struct MSRunResult {
	    std::vector<MSDay> ts;
    std::array<double, MAX_STRAINS> peak_I{}, peak_day{}, total_D{};
    double total_attack_rate = 0;
};

struct MSSimResult {
	    MSRunResult mean, lo, hi;
    int n_runs = 0;
};

MSSimResult run_ms_ensemble(const Params& p, const MultiStrainParams& mp, ProgressCb cb = nullptr);
