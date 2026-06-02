#include "multi_strain.hpp"
#include <cmath>
#include <random>
#include <algorithm>
#include <stdexcept>

// Multi-strain SEIVRD integrator.
//
// Shared S pool formulation (Castillo-Chavez style):
//   dS/dt = mu*N + sum_k(omega_k*R_k) + omega_v*V
//           - S * sum_k(lambda_k) - vax_flow - mu*S
//
// Each recovered class R_k has partial susceptibility to other strains.
// Effective new infections of strain k from R_j class:
//   (1 - X[k][j]) * lambda_k * R_j
// where X[k][j] is cross_immunity[k][j].
//
// Vaccination: shared V pool. Vaccine protects against strain k proportionally
// to vax_cross[k], i.e. breakthrough rate = (1 - vax_cross[k]).

static double ou_step(double b, double b0, double theta, double sigma, double dt, double dW)
{
	    return b + theta * (b0 - b) * dt + sigma * b0 * dW;
}

static double season_b(const Params& p, double day, double b)
{
	    return b * (1.0 + p.season_amp * std::cos(2.0 * M_PI * (day - p.season_phi) / 365.0));
}

static MSRunResult single_ms_run(const Params& p, const MultiStrainParams& mp, uint64_t seed)
{
	    const int K    = std::min(mp.n_strains, MAX_STRAINS);
    const double dt= p.dt;
    const int steps= static_cast<int>(std::ceil(p.T_days / dt));
    const double N = p.N;

    std::mt19937_64 rng(seed);
    std::normal_distribution<double> norm(0.0, 1.0);

    // OU beta per strain (starts at strain-specific beta)
    std::array<double, MAX_STRAINS> beta_ou;
    for (int k = 0; k < K; ++k)
        beta_ou[k] = mp.strains[k].beta;

    // Initialise compartments
    // strain 0 seeded at t=0, others at their intro_day
    StrainCompartments c{};
    double S = N;
    double V = 0;

    for (int k = 0; k < K; ++k) {
	        if (!mp.strains[k].enabled) continue;
        if (mp.strains[k].intro_day <= 0) {
	            c.I[k] = mp.strains[k].intro_size;
            S -= c.I[k];
        }
    }
    S -= p.E0; // a bit of initial E in strain 0
    c.E[0] += p.E0;

    MSRunResult res;
    res.ts.reserve(steps + 1);
    res.total_attack_rate = 0;
    for (int k = 0; k < K; ++k) {
	        res.peak_I[k]  = c.I[k];
        res.peak_day[k]= 0;
        res.total_D[k] = 0;
    }

    auto rec = [&](double /*day*/) {
	        MSDay d;
        d.S = S; d.V = V; d.c = c;
        for (int k = 0; k < K; ++k) {
	            double sb = beta_ou[k];
            // Rt_k = beta_k * (S_eff) / (N * gamma_k)
            // S_eff accounts for partial susceptibility in R classes
            double S_eff = S;
            for (int j = 0; j < K; ++j)
                S_eff += (1.0 - mp.cross_immunity[k][j]) * c.R[j];
            S_eff += V * (1.0 - mp.strains[k].vax_cross);
            double g = mp.strains[k].gamma;
            d.Rt[k] = (g > 0) ? sb * S_eff / (N * g) : 0.0;
        }
        d.beta_eff = beta_ou[0];
        res.ts.push_back(d);
    };

    rec(0.0);

    double max_vax = p.vax_cov * N;

    for (int step = 0; step < steps; ++step) {
	        double day = step * dt;

        // OU update for each strain's beta
        for (int k = 0; k < K; ++k) {
	            double dW = std::sqrt(dt) * norm(rng);
            beta_ou[k] = ou_step(beta_ou[k], mp.strains[k].beta,
                                  p.noise_theta, p.noise_sigma, dt, dW);
            beta_ou[k] = std::max(0.0, beta_ou[k]);
        }

        // Seed introductions: check if any strain crosses its intro_day this step
        for (int k = 1; k < K; ++k) {
	            if (!mp.strains[k].enabled) continue;
            double intro = mp.strains[k].intro_day;
            if (day < intro && day + dt >= intro) {
	                double sz = std::min(mp.strains[k].intro_size, S);
                c.I[k] += sz;
                S -= sz;
            }
        }

        // Force of infection per strain on S, V, and each R_j
        // lambda_k = b_k * I_k / N  (frequency-dependent transmission)
        std::array<double, MAX_STRAINS> lam{};
        for (int k = 0; k < K; ++k) {
	            if (!mp.strains[k].enabled) continue;
            double b = season_b(p, day, beta_ou[k]);
            lam[k] = b * c.I[k] / N;
        }

        // Flows from S
        double sum_lam = 0;
        for (int k = 0; k < K; ++k) sum_lam += lam[k];

        double vax_avail = std::max(0.0, max_vax - V);
        double vax_flow  = std::min(p.vax_rate * S, vax_avail) * dt;

        double dS = (p.mu * N - S * sum_lam - vax_flow / dt - p.mu * S) * dt;

        // waning from all R classes back to S
        for (int k = 0; k < K; ++k)
            dS += mp.strains[k].omega * c.R[k] * dt;
        dS += p.omega_v * V * dt;

        // per-strain compartment updates
        std::array<double, MAX_STRAINS> dE{}, dI{}, dR{}, dD{}, dH{};
        for (int k = 0; k < K; ++k) {
	            if (!mp.strains[k].enabled) continue;
            auto& sk = mp.strains[k];

            // new infections of strain k from S
            double inf_from_S = lam[k] * S;

            // reinfections from R_j (partial cross-immunity)
            double reinf = 0;
            for (int j = 0; j < K; ++j) {
	                if (j == k) continue;
                reinf += (1.0 - mp.cross_immunity[k][j]) * lam[k] * c.R[j];
            }

            // breakthrough from V
            double breakthrough = (1.0 - sk.vax_cross) * lam[k] * V;

            // superinfection from I_j (optional, small term)
            double superinf = 0;
            if (mp.superinf_factor > 0) {
	                for (int j = 0; j < K; ++j) {
	                    if (j == k) continue;
                    superinf += mp.superinf_factor * lam[k] * c.I[j];
                }
            }

            double new_E = (inf_from_S + reinf + breakthrough + superinf) * dt;
            double EI    = sk.sigma * c.E[k] * dt;
            double IR    = sk.gamma * (1.0 - sk.ifr) * c.I[k] * dt;
            double ID    = sk.gamma * sk.ifr * c.I[k] * dt;
            double RS    = sk.omega * c.R[k] * dt; // already in dS

            double hosp_in  = sk.hosp_rate * EI;
            double hosp_out = c.H[k] / 14.0 * dt;
            double hosp_d   = p.delta * c.H[k] * dt;

            dE[k] = new_E - EI - p.mu * c.E[k] * dt;
            dI[k] = EI    - IR - ID - p.mu * c.I[k] * dt;
            dR[k] = IR    - RS - p.mu * c.R[k] * dt;
            dD[k] = ID + hosp_d + p.mu * (c.E[k] + c.I[k] + c.R[k]) * dt;
            dH[k] = hosp_in - hosp_out;

            // subtract reinfections from respective R class (they move to E_k)
            // handled implicitly: R_k decreases only via omega (waning) above,
            // reinfections are drawn from R_j of OTHER strains — no double-count issue
        }

        double dV = (vax_flow - p.omega_v * V * dt - p.mu * V * dt);
        // subtract breakthroughs from V into each strain's E
        for (int k = 0; k < K; ++k)
            dV -= (1.0 - mp.strains[k].vax_cross) * lam[k] * V * dt;

        S += dS; S = std::max(S, 0.0);
        V += dV; V = std::max(V, 0.0);
        for (int k = 0; k < K; ++k) {
	            c.E[k] += dE[k]; c.E[k] = std::max(c.E[k], 0.0);
            c.I[k] += dI[k]; c.I[k] = std::max(c.I[k], 0.0);
            c.R[k] += dR[k]; c.R[k] = std::max(c.R[k], 0.0);
            c.D[k] += dD[k];
            c.H[k] += dH[k]; c.H[k] = std::max(c.H[k], 0.0);

            if (c.I[k] > res.peak_I[k]) {
	                res.peak_I[k]  = c.I[k];
                res.peak_day[k]= day + dt;
            }
        }

        double next_day = (step + 1) * dt;
        if (std::floor(next_day) > std::floor(day) || step == steps - 1)
            rec(next_day);
    }

    double total_r = 0;
    for (int k = 0; k < K; ++k) {
	        res.total_D[k] = c.D[k];
        total_r += c.R[k] + c.D[k];
    }
    res.total_attack_rate = total_r / N;
    return res;
}

static MSRunResult avg_ms(const std::vector<MSRunResult>& runs)
{
	    int n = (int)runs.size();
    size_t len = runs[0].ts.size();
    for (auto& r : runs) len = std::min(len, r.ts.size());

    MSRunResult avg;
    avg.ts.resize(len);
    for (size_t t = 0; t < len; ++t) {
	        avg.ts[t] = {};
        for (auto& r : runs) {
	            avg.ts[t].S        += r.ts[t].S / n;
            avg.ts[t].V        += r.ts[t].V / n;
            avg.ts[t].beta_eff += r.ts[t].beta_eff / n;
            for (int k = 0; k < MAX_STRAINS; ++k) {
	                avg.ts[t].c.E[k] += r.ts[t].c.E[k] / n;
                avg.ts[t].c.I[k] += r.ts[t].c.I[k] / n;
                avg.ts[t].c.R[k] += r.ts[t].c.R[k] / n;
                avg.ts[t].c.D[k] += r.ts[t].c.D[k] / n;
                avg.ts[t].c.H[k] += r.ts[t].c.H[k] / n;
                avg.ts[t].Rt[k]  += r.ts[t].Rt[k]  / n;
            }
        }
    }
    for (auto& r : runs) {
	        avg.total_attack_rate += r.total_attack_rate / n;
        for (int k = 0; k < MAX_STRAINS; ++k) {
	            avg.peak_I[k]   += r.peak_I[k]   / n;
            avg.peak_day[k] += r.peak_day[k] / n;
            avg.total_D[k]  += r.total_D[k]  / n;
        }
    }
    return avg;
}

static MSRunResult pct_ms(const std::vector<MSRunResult>& runs, double pct)
{
	    int n = (int)runs.size();
    int idx = (int)(pct * (n - 1));
    size_t len = runs[0].ts.size();
    for (auto& r : runs) len = std::min(len, r.ts.size());

    MSRunResult pr;
    pr.ts.resize(len);
    std::vector<double> buf(n);

    for (size_t t = 0; t < len; ++t) {
	        pr.ts[t] = runs[n/2].ts[t]; // carry median for base fields
        for (int k = 0; k < MAX_STRAINS; ++k) {
	            for (int i = 0; i < n; ++i) buf[i] = runs[i].ts[t].c.I[k];
            std::sort(buf.begin(), buf.end());
            pr.ts[t].c.I[k] = buf[idx];
            for (int i = 0; i < n; ++i) buf[i] = runs[i].ts[t].c.D[k];
            std::sort(buf.begin(), buf.end());
            pr.ts[t].c.D[k] = buf[idx];
        }
    }
    return pr;
}

MSSimResult run_ms_ensemble(const Params& p, const MultiStrainParams& mp, ProgressCb cb)
{
	    std::vector<MSRunResult> runs;
    runs.reserve(N_RUNS);
    std::mt19937_64 sg(0xCAFEBABE);
    for (int i = 0; i < N_RUNS; ++i) {
	        if (cb) cb(i, N_RUNS, 0, p.T_days);
        runs.push_back(single_ms_run(p, mp, sg()));
        if (cb) cb(i, N_RUNS, p.T_days, p.T_days);
    }
    MSSimResult sr;
    sr.n_runs = N_RUNS;
    sr.mean   = avg_ms(runs);
    sr.lo     = pct_ms(runs, 0.1);
    sr.hi     = pct_ms(runs, 0.9);
    return sr;
}
