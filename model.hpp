#pragma once
#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <functional>

// CLADES - Compartmental Lightweight Agent-based Disease Epidemiology Simulator
// Model: extended SEIVRD with waning immunity, vaccination sub-compartments,
// age-stratified optional layer, and stochastic SDE noise on beta.

static constexpr int N_RUNS = 5; // monte-carlo ensemble; average washes out noise

// All rates are per-day unless stated otherwise
struct Params {
	    // --- Population ---
    double N = 1e6;          // total population
    double I0 = 100.0;       // initial infectious
    double E0 = 0.0;

    // --- Transmission ---
    double beta      = 0.3;  // baseline contact * transmission prob
    double sigma     = 0.2;  // 1/incubation_period  (E->I)
    double gamma     = 0.1;  // 1/infectious_period  (I->R)
    double mu        = 0.0;  // background birth/death rate (keeps N constant)

    // --- Disease outcomes ---
    double ifr       = 0.01; // infection fatality ratio  (fraction of I -> D)
    double hosp_rate = 0.05; // fraction of I requiring hospitalisation
    double delta     = 0.0;  // additional mortality for hospitalised (per day)

    // --- Immunity ---
    double omega_r   = 0.003;// waning rate recovered (R->S), ~1yr default
    double omega_v   = 0.002;// waning rate vaccinated (V->S)

    // --- Vaccination ---
    double vax_rate  = 0.0;  // daily vaccination rate (fraction of S vaccinated)
    double vax_eff   = 0.85; // vaccine efficacy against infection
    double vax_cov   = 0.7;  // max coverage fraction (stops when V+R >= cov*N)

    // --- Seasonality ---
    double season_amp  = 0.0;// amplitude [0,1] of seasonal forcing on beta
    double season_phi  = 0.0;// phase offset in days (0 = peak at day 0)

    // --- Stochastic noise (Ornstein-Uhlenbeck on beta) ---
    double noise_sigma = 0.05;// noise amplitude on beta
    double noise_theta = 0.1; // OU mean-reversion speed

    // --- Simulation ---
    int    T_days      = 365;
    double dt          = 0.5; // integration step (days)

    std::string disease_name = "Novel Pathogen";
};

struct DayState {
	    double S, E, I, R, V, D, H; // H = current hospitalisations
    double Rt;                   // effective reproduction number
    double beta_eff;
};

struct RunResult {
	    std::vector<DayState> ts; // time series, one entry per recorded day
    double peak_I, peak_day, total_D, total_attack_rate;
};

struct SimResult {
	    RunResult mean;
    RunResult lo, hi;   // 10th/90th percentile across ensemble
    int n_runs;
};

// progress callback: (run_idx, total_runs, day, total_days)
using ProgressCb = std::function<void(int,int,int,int)>;

SimResult run_ensemble(const Params& p, ProgressCb cb = nullptr);
