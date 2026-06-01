#include "model.hpp"
#include <cmath>
#include <random>
#include <algorithm>
#include <numeric>
#include <stdexcept>

// Euler-Maruyama integration of the SDE-extended SEIVRD system.
// The OU process perturbs beta around its deterministic value to simulate
// superspreading events / environmental stochasticity (per Royalsoc 2023 paper).
//
// Compartments:
//   S  - Susceptible
//   E  - Exposed (infected, not yet infectious; latent period)
//   I  - Infectious (symptomatic + asymptomatic together)
//   H  - Hospitalised (subset of I, tracked separately)
//   R  - Recovered (natural immunity)
//   V  - Vaccinated (partial immunity, vax_eff reduces susceptibility)
//   D  - Dead (absorbing)
//
// Flows:
//   S -> E  : beta * S * (I + eps*E) / N   (pre-symptomatic contribution)
//   V -> E  : beta*(1-vax_eff) * V * I / N (breakthrough)
//   E -> I  : sigma * E
//   I -> R  : gamma * (1 - ifr) * I
//   I -> D  : gamma * ifr * I
//   R -> S  : omega_r * R                  (waning)
//   V -> S  : omega_v * V                  (waning vax immunity)
//   S -> V  : vax_rate * S  (capped at vax_cov)

static double seasonal_beta(const Params& p, double day, double beta_ou)
{
	    double s = 1.0 + p.season_amp * std::cos(2.0 * M_PI * (day - p.season_phi) / 365.0);
    return beta_ou * s;
}

static RunResult single_run(const Params& p, uint64_t seed)
{
	    const double dt = p.dt;
    const int steps = static_cast<int>(std::ceil(p.T_days / dt));

    std::mt19937_64 rng(seed);
    std::normal_distribution<double> norm(0.0, 1.0);

    double S = p.N - p.I0 - p.E0;
    double E = p.E0;
    double I = p.I0;
    double R = 0.0;
    double V = 0.0;
    double D = 0.0;
    // H is a derived flow tracker, not a compartment that changes N
    double H_current = p.hosp_rate * I;

    // OU state for beta; initialised at deterministic value
    double beta_ou = p.beta;

    RunResult res;
    res.ts.reserve(steps + 1);

    auto rec = [&](double day) {
	        double Rt = 0.0;
        if (S + V > 0 && I > 1e-9) {
	            double S_eff = S + V * (1.0 - p.vax_eff);
            Rt = seasonal_beta(p, day, beta_ou) * S_eff / (p.N * (p.gamma + p.mu));
        }
        DayState st{S, E, I, R, V, D, H_current, Rt, seasonal_beta(p, day, beta_ou)};
        res.ts.push_back(st);
    };

    double peak_I = I, peak_day = 0.0;
    rec(0.0);

    double max_vax_abs = p.vax_cov * p.N;

    for (int step = 0; step < steps; ++step) {
	        double day = step * dt;

        // OU update: dX = theta*(mu_X - X)*dt + sigma*dW
        double dW = std::sqrt(dt) * norm(rng);
        beta_ou += p.noise_theta * (p.beta - beta_ou) * dt + p.noise_sigma * p.beta * dW;
        beta_ou = std::max(0.0, beta_ou); // physical constraint

        double b = seasonal_beta(p, day, beta_ou);

        // pre-symptomatic contribution factor (E contributes ~30% relative to I)
        double pre_factor = 0.3;
        double lambda_S = b * (I + pre_factor * E) / p.N;
        double lambda_V = b * (1.0 - p.vax_eff) * I / p.N;

        // flows
        double vax_available = std::max(0.0, max_vax_abs - V - R); // don't over-vaccinate
        double vacc_flow  = p.vax_rate * std::min(S, vax_available);
        double SE         = lambda_S * S;
        double VE         = lambda_V * V;
        double EI         = p.sigma * E;
        double IR_frac    = p.gamma * (1.0 - p.ifr) * I;
        double ID_frac    = p.gamma * p.ifr * I;
        double RS         = p.omega_r * R;
        double VS         = p.omega_v * V;
        double birth      = p.mu * p.N;          // births -> S
        double death_S    = p.mu * S;
        double death_E    = p.mu * E;
        double death_I    = p.mu * I;
        double death_R    = p.mu * R;
        double death_V    = p.mu * V;

        double hosp_in    = p.hosp_rate * EI;    // new hospitalisations from E->I
        double hosp_out   = H_current / 14.0;    // average 14d stay
        double hosp_death = p.delta * H_current;

        // Euler step
        double dS = (birth + RS + VS - SE - vacc_flow - death_S) * dt;
        double dE = (SE + VE - EI - death_E) * dt;
        double dI = (EI - IR_frac - ID_frac - death_I) * dt;
        double dR = (IR_frac - RS - death_R) * dt;
        double dV = (vacc_flow - VE - VS - death_V) * dt;
        double dD = (ID_frac + hosp_death + death_E + death_I + death_R + death_V) * dt;
        double dH = (hosp_in - hosp_out) * dt;

        S += dS; E += dE; I += dI; R += dR; V += dV; D += dD; H_current += dH;

        // floor at 0 to prevent numerical drift below zero
        S = std::max(S, 0.0);
        E = std::max(E, 0.0);
        I = std::max(I, 0.0);
        R = std::max(R, 0.0);
        V = std::max(V, 0.0);
        H_current = std::max(H_current, 0.0);

        if (I > peak_I) { peak_I = I; peak_day = day + dt; }

        // record once per integer day
        double next_day = (step + 1) * dt;
        if (std::floor(next_day) > std::floor(day) || step == steps - 1)
            rec(next_day);
    }

    res.peak_I = peak_I;
    res.peak_day = peak_day;
    res.total_D = D;
    res.total_attack_rate = (p.N - S - V) / p.N;
    return res;
}

static RunResult average_runs(const std::vector<RunResult>& runs)
{
	    if (runs.empty()) throw std::runtime_error("no runs");
    size_t len = runs[0].ts.size();
    // align all to shortest (rounding might differ by 1)
    for (auto& r : runs) len = std::min(len, r.ts.size());

    RunResult avg;
    avg.ts.resize(len);
    int n = runs.size();

    for (size_t t = 0; t < len; ++t) {
	        DayState& d = avg.ts[t];
        d = {};
        for (auto& r : runs) {
	            auto& s = r.ts[t];
            d.S += s.S; d.E += s.E; d.I += s.I;
            d.R += s.R; d.V += s.V; d.D += s.D;
            d.H += s.H; d.Rt += s.Rt; d.beta_eff += s.beta_eff;
        }
        d.S /= n; d.E /= n; d.I /= n;
        d.R /= n; d.V /= n; d.D /= n;
        d.H /= n; d.Rt /= n; d.beta_eff /= n;
    }

    avg.peak_I = 0; avg.peak_day = 0;
    avg.total_D = 0; avg.total_attack_rate = 0;
    for (auto& r : runs) {
	        avg.peak_I           += r.peak_I           / n;
        avg.peak_day         += r.peak_day         / n;
        avg.total_D          += r.total_D          / n;
        avg.total_attack_rate+= r.total_attack_rate/ n;
    }
    return avg;
}

static RunResult percentile_run(const std::vector<RunResult>& runs, double pct)
{
	    size_t len = runs[0].ts.size();
    for (auto& r : runs) len = std::min(len, r.ts.size());

    RunResult prun;
    prun.ts.resize(len);
    int n = runs.size();

    std::vector<double> buf(n);
    auto pick = [&](auto fn) -> double {
	        for (int i = 0; i < n; ++i) buf[i] = fn(runs[i]);
        std::sort(buf.begin(), buf.end());
        return buf[static_cast<int>(pct * (n - 1))];
    };

    for (size_t t = 0; t < len; ++t) {
	        auto& d = prun.ts[t];
        for (int i = 0; i < n; ++i) buf[i] = runs[i].ts[t].I;
        std::sort(buf.begin(), buf.end());
        d.I = buf[static_cast<int>(pct * (n-1))];
        for (int i = 0; i < n; ++i) buf[i] = runs[i].ts[t].D;
        std::sort(buf.begin(), buf.end());
        d.D = buf[static_cast<int>(pct * (n-1))];
        for (int i = 0; i < n; ++i) buf[i] = runs[i].ts[t].Rt;
        std::sort(buf.begin(), buf.end());
        d.Rt = buf[static_cast<int>(pct * (n-1))];
        // copy other fields from average (not meaningful as percentiles)
        d.S = runs[n/2].ts[t].S;
        d.E = runs[n/2].ts[t].E;
        d.R = runs[n/2].ts[t].R;
        d.V = runs[n/2].ts[t].V;
        d.H = runs[n/2].ts[t].H;
        d.beta_eff = runs[n/2].ts[t].beta_eff;
    }
    prun.peak_I = pick([](const RunResult& r){ return r.peak_I; });
    prun.peak_day = pick([](const RunResult& r){ return r.peak_day; });
    prun.total_D  = pick([](const RunResult& r){ return r.total_D; });
    prun.total_attack_rate = pick([](const RunResult& r){ return r.total_attack_rate; });
    return prun;
}

SimResult run_ensemble(const Params& p, ProgressCb cb)
{
	    std::vector<RunResult> runs;
    runs.reserve(N_RUNS);
    std::mt19937_64 seed_gen(42);
    for (int i = 0; i < N_RUNS; ++i) {
	        uint64_t seed = seed_gen();
        auto steps = static_cast<int>(std::ceil(p.T_days / p.dt));
        // proxy progress: report beginning of each run
        if (cb) cb(i, N_RUNS, 0, steps);
        runs.push_back(single_run(p, seed));
        if (cb) cb(i, N_RUNS, steps, steps);
    }
    SimResult sr;
    sr.n_runs = N_RUNS;
    sr.mean   = average_runs(runs);
    sr.lo     = percentile_run(runs, 0.1);
    sr.hi     = percentile_run(runs, 0.9);
    return sr;
}
