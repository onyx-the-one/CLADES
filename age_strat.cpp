#include "age_strat.hpp"
#include "model.hpp"
#include <cmath>
#include <random>
#include <algorithm>
#include <stdexcept>

// Age-stratified SEIVRD integrator.
//
// Force of infection on group i:
//   lambda_i = beta_ou * season(t) * sum_j[ C[i][j] * I[j] / N[j] ]
//
// This is the standard next-generation / WAIFW formulation.
// C[i][j] is the mean contacts per day that age group i makes with j.
// Dividing I[j] by N[j] gives per-capita prevalence in group j.
//
// Vaccination is distributed across groups proportionally to their uptake
// fraction, capped per group at vax_uptake[i] * N[i].

static double seasonal_b(const Params& p, double day, double b_ou)
{
	    return b_ou * (1.0 + p.season_amp * std::cos(2.0 * M_PI * (day - p.season_phi) / 365.0));
}

static AgeRunResult single_age_run(const Params& p, const AgeParams& ap, uint64_t seed)
{
	    const double dt   = p.dt;
    const int steps   = static_cast<int>(std::ceil(p.T_days / dt));
    const double Ntot = p.N;

    std::mt19937_64 rng(seed);
    std::normal_distribution<double> norm(0.0, 1.0);

    // Absolute population per group
    std::array<double, N_AGE> N_g;
    for (int i = 0; i < N_AGE; ++i)
        N_g[i] = ap.pop_frac[i] * Ntot;

    // Seed infections in 20-29 group (highest contact, typical index case age)
    AgeState st{};
    for (int i = 0; i < N_AGE; ++i) {
	        double seed_frac = (i == 2) ? p.I0 : 0.0;
        st.I[i] = seed_frac;
        st.E[i] = (i == 2) ? p.E0 : 0.0;
        st.S[i] = N_g[i] - st.I[i] - st.E[i];
        st.R[i] = st.V[i] = st.D[i] = st.H[i] = 0.0;
    }

    double beta_ou = p.beta;
    AgeRunResult res;
    res.ts.reserve(steps + 1);
    res.total_attack_rate = 0;
    for (int i = 0; i < N_AGE; ++i) {
	        res.peak_I[i] = st.I[i];
        res.peak_day[i] = 0;
        res.total_D[i] = 0;
    }

    auto rec = [&](double /*day*/) {
	        AgeDayState ads;
        ads.s = st;
        ads.beta_eff = beta_ou;
        for (int i = 0; i < N_AGE; ++i) {
	            // Rt_i = beta * sum_j C[i][j] * S[i]/N[i] / gamma
            double lam = 0;
            for (int j = 0; j < N_AGE; ++j)
                lam += ap.C[i][j] * st.I[j] / std::max(N_g[j], 1.0);
            double Si_eff = st.S[i] + st.V[i] * (1.0 - p.vax_eff);
            ads.s.Rt[i] = (p.gamma > 0) ? beta_ou * lam / p.gamma * Si_eff / std::max(N_g[i], 1.0) : 0.0;
        }
        res.ts.push_back(ads);
    };

    rec(0.0);

    for (int step = 0; step < steps; ++step) {
	        double day = step * dt;

        // OU update on beta
        double dW = std::sqrt(dt) * norm(rng);
        beta_ou += p.noise_theta * (p.beta - beta_ou) * dt + p.noise_sigma * p.beta * dW;
        beta_ou = std::max(0.0, beta_ou);

        double b = seasonal_b(p, day, beta_ou);

        // Total daily vaccination budget (absolute individuals)
        double vax_budget = p.vax_rate * Ntot * dt;

        // Distribute vax budget proportionally to each group's remaining
        // eligible susceptibles (S[i] not yet at uptake cap)
        double eligible_total = 0;
        std::array<double, N_AGE> eligible;
        for (int i = 0; i < N_AGE; ++i) {
	            double cap = ap.vax_uptake[i] * N_g[i] * p.vax_cov;
            eligible[i] = std::max(0.0, std::min(st.S[i], cap - st.V[i] - st.R[i]));
            eligible_total += eligible[i];
        }

        for (int i = 0; i < N_AGE; ++i) {
	            double vax_i = (eligible_total > 0)
                ? vax_budget * eligible[i] / eligible_total
                : 0.0;
            vax_i = std::min(vax_i, eligible[i]);

            // Force of infection on group i
            double lam_S = 0, lam_V = 0;
            for (int j = 0; j < N_AGE; ++j) {
	                double prev_j = st.I[j] / std::max(N_g[j], 1.0);
                lam_S += b * ap.C[i][j] * prev_j;
                lam_V += b * (1.0 - p.vax_eff) * ap.C[i][j] * prev_j;
            }

            double SE   = lam_S * st.S[i];
            double VE   = lam_V * st.V[i];
            double EI   = p.sigma * st.E[i];
            double ifr_i= ap.ifr[i];
            double IR   = p.gamma * (1.0 - ifr_i) * st.I[i];
            double ID   = p.gamma * ifr_i * st.I[i];
            double RS   = p.omega_r * st.R[i];
            double VS   = p.omega_v * st.V[i];
            double mu   = p.mu;

            double hosp_in  = ap.hosp_rate[i] * EI;
            double hosp_out = st.H[i] / 14.0;
            double hosp_d   = p.delta * st.H[i];

            double birth_i = mu * N_g[i];

            st.S[i] += (birth_i + RS + VS - SE - vax_i - mu * st.S[i]) * dt;
            st.E[i] += (SE + VE - EI - mu * st.E[i]) * dt;
            st.I[i] += (EI - IR - ID - mu * st.I[i]) * dt;
            st.R[i] += (IR - RS - mu * st.R[i]) * dt;
            st.V[i] += (vax_i - VE - VS - mu * st.V[i]) * dt;
            st.D[i] += (ID + hosp_d + mu * (st.E[i] + st.I[i] + st.R[i] + st.V[i])) * dt;
            st.H[i] += (hosp_in - hosp_out) * dt;

            st.S[i] = std::max(st.S[i], 0.0);
            st.E[i] = std::max(st.E[i], 0.0);
            st.I[i] = std::max(st.I[i], 0.0);
            st.R[i] = std::max(st.R[i], 0.0);
            st.V[i] = std::max(st.V[i], 0.0);
            st.H[i] = std::max(st.H[i], 0.0);

            if (st.I[i] > res.peak_I[i]) {
	                res.peak_I[i]  = st.I[i];
                res.peak_day[i]= day + dt;
            }
        }

        double next_day = (step + 1) * dt;
        if (std::floor(next_day) > std::floor(day) || step == steps - 1)
            rec(next_day);
    }

    // total attack rate across all groups
    double total_infected = 0;
    for (int i = 0; i < N_AGE; ++i) {
	        res.total_D[i] = st.D[i];
        total_infected += (N_g[i] - st.S[i] - st.V[i]);
    }
    res.total_attack_rate = total_infected / Ntot;
    return res;
}

static AgeRunResult average_age_runs(const std::vector<AgeRunResult>& runs)
{
	    int n = runs.size();
    size_t len = runs[0].ts.size();
    for (auto& r : runs) len = std::min(len, r.ts.size());

    AgeRunResult avg;
    avg.ts.resize(len);
    avg.total_attack_rate = 0;
    avg.peak_I   = {};
    avg.peak_day = {};
    avg.total_D  = {};

    for (size_t t = 0; t < len; ++t) {
	        avg.ts[t].s = {};
        avg.ts[t].beta_eff = 0;
        for (auto& r : runs) {
	            avg.ts[t].beta_eff += r.ts[t].beta_eff / n;
            for (int i = 0; i < N_AGE; ++i) {
	                avg.ts[t].s.S[i] += r.ts[t].s.S[i] / n;
                avg.ts[t].s.E[i] += r.ts[t].s.E[i] / n;
                avg.ts[t].s.I[i] += r.ts[t].s.I[i] / n;
                avg.ts[t].s.R[i] += r.ts[t].s.R[i] / n;
                avg.ts[t].s.V[i] += r.ts[t].s.V[i] / n;
                avg.ts[t].s.D[i] += r.ts[t].s.D[i] / n;
                avg.ts[t].s.H[i] += r.ts[t].s.H[i] / n;
                avg.ts[t].s.Rt[i]+= r.ts[t].s.Rt[i]/ n;
            }
        }
    }

    for (auto& r : runs) {
	        avg.total_attack_rate += r.total_attack_rate / n;
        for (int i = 0; i < N_AGE; ++i) {
	            avg.peak_I[i]   += r.peak_I[i]   / n;
            avg.peak_day[i] += r.peak_day[i] / n;
            avg.total_D[i]  += r.total_D[i]  / n;
        }
    }
    return avg;
}

static AgeRunResult percentile_age_run(const std::vector<AgeRunResult>& runs, double pct)
{
	    int n = runs.size();
    size_t len = runs[0].ts.size();
    for (auto& r : runs) len = std::min(len, r.ts.size());
    int idx = static_cast<int>(pct * (n - 1));

    AgeRunResult pr;
    pr.ts.resize(len);
    pr.total_attack_rate = 0;
    pr.peak_I = {}; pr.peak_day = {}; pr.total_D = {};

    std::vector<double> buf(n);
    for (size_t t = 0; t < len; ++t) {
	        pr.ts[t].beta_eff = runs[n/2].ts[t].beta_eff;
        for (int i = 0; i < N_AGE; ++i) {
	            for (int k = 0; k < n; ++k) buf[k] = runs[k].ts[t].s.I[i];
            std::sort(buf.begin(), buf.end());
            pr.ts[t].s.I[i] = buf[idx];
            for (int k = 0; k < n; ++k) buf[k] = runs[k].ts[t].s.D[i];
            std::sort(buf.begin(), buf.end());
            pr.ts[t].s.D[i] = buf[idx];
            // carry median for other compartments
            pr.ts[t].s.S[i]  = runs[n/2].ts[t].s.S[i];
            pr.ts[t].s.R[i]  = runs[n/2].ts[t].s.R[i];
            pr.ts[t].s.V[i]  = runs[n/2].ts[t].s.V[i];
            pr.ts[t].s.H[i]  = runs[n/2].ts[t].s.H[i];
            pr.ts[t].s.Rt[i] = runs[n/2].ts[t].s.Rt[i];
        }
    }
    return pr;
}

AgeSimResult run_age_ensemble(const Params& p, const AgeParams& ap, ProgressCb cb)
{
	    std::vector<AgeRunResult> runs;
    runs.reserve(N_RUNS);
    std::mt19937_64 sg(0xDEADBEEF);
    for (int i = 0; i < N_RUNS; ++i) {
	        if (cb) cb(i, N_RUNS, 0, p.T_days);
        runs.push_back(single_age_run(p, ap, sg()));
        if (cb) cb(i, N_RUNS, p.T_days, p.T_days);
    }
    AgeSimResult sr;
    sr.n_runs = N_RUNS;
    sr.mean   = average_age_runs(runs);
    sr.lo     = percentile_age_run(runs, 0.1);
    sr.hi     = percentile_age_run(runs, 0.9);
    return sr;
}
