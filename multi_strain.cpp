#include "multi_strain.hpp"
#include <cmath>
#include <random>
#include <algorithm>

// Multi-strain SEIVRD integrator.
//
// Core correctness rules:
//  1. All outflows from a compartment are computed as rates first.
//  2. cap_outflows() scales them down if they'd drain more than the compartment holds.
//  3. Transfers are accounted in BOTH source and destination.
//  4. No compartment goes negative (floor as last resort only for fp dust).
//  5. Per-strain deaths tracked individually.

static double season_b(const Params& p, double day, double b)
{
	    return b * (1.0 + p.season_amp * std::cos(2.0 * M_PI * (day - p.season_phi) / 365.0));
}

// Scale outflow rates[] so sum*dt <= available. Modifies in-place.
static void cap_outflows(double* rates, int n, double available, double dt)
{
	    double demand = 0;
    for (int i = 0; i < n; ++i) demand += rates[i];
    demand *= dt;
    if (demand > available && demand > 0) {
	        double scale = available / demand;
        for (int i = 0; i < n; ++i) rates[i] *= scale;
    }
}

static MSRunResult single_ms_run(const Params& p, const MultiStrainParams& mp, uint64_t seed)
{
	    const int K    = std::min(mp.n_strains, MAX_STRAINS);
    const double dt= p.dt;
    const int steps= static_cast<int>(std::ceil(p.T_days / dt));
    const double N = p.N;

    std::mt19937_64 rng(seed);
    std::normal_distribution<double> norm01(0.0, 1.0);

    std::array<double, MAX_STRAINS> beta_ou{};
    for (int k = 0; k < K; ++k) beta_ou[k] = mp.strains[k].beta;

    double S = N, V = 0;
    StrainCompartments c{};

    // seed infections at t=0
    for (int k = 0; k < K; ++k) {
	        if (!mp.strains[k].enabled || mp.strains[k].intro_day > 0) continue;
        double sz = std::min(mp.strains[k].intro_size, S);
        c.I[k] += sz; S -= sz;
    }
    double e0 = std::min(p.E0, S);
    c.E[0] += e0; S -= e0;

    MSRunResult res;
    res.ts.reserve(steps + 1);
    for (int k = 0; k < K; ++k) { res.peak_I[k] = c.I[k]; res.peak_day[k] = 0; res.total_D[k] = 0; }

    auto rec = [&](double /*day*/) {
	        MSDay d; d.S = S; d.V = V; d.c = c; d.beta_eff = beta_ou[0];
        for (int k = 0; k < K; ++k) {
	            double S_eff = S + V * (1.0 - mp.strains[k].vax_cross);
            for (int j = 0; j < K; ++j)
                S_eff += (1.0 - mp.cross_immunity[k][j]) * c.R[j];
            double g = mp.strains[k].gamma;
            d.Rt[k] = (g > 0 && N > 0) ? beta_ou[k] * S_eff / (N * g) : 0.0;
        }
        res.ts.push_back(d);
    };

    rec(0.0);

    const double max_vax = p.vax_cov * N;

    for (int step = 0; step < steps; ++step) {
	        double day = step * dt;

        // OU update on each strain's beta
        for (int k = 0; k < K; ++k) {
	            double dW = std::sqrt(dt) * norm01(rng);
            beta_ou[k] += p.noise_theta * (mp.strains[k].beta - beta_ou[k]) * dt
                        + p.noise_sigma * mp.strains[k].beta * dW;
            beta_ou[k] = std::max(0.0, beta_ou[k]);
        }

        // Scheduled introductions
        for (int k = 0; k < K; ++k) {
	            if (!mp.strains[k].enabled) continue;
            double intro = mp.strains[k].intro_day;
            if (intro > 0 && day < intro && day + dt >= intro) {
	                double sz = std::min(mp.strains[k].intro_size, S);
                c.I[k] += sz; S -= sz;
            }
        }

        // Force of infection (rates, not yet * dt)
        std::array<double, MAX_STRAINS> lam{};
        for (int k = 0; k < K; ++k) {
	            if (!mp.strains[k].enabled || N <= 0) continue;
            lam[k] = season_b(p, day, beta_ou[k]) * c.I[k] / N;
        }

        // ---- S outflows ----
        // To E[k] via infection, to V via vaccination, background death
        std::array<double, MAX_STRAINS + 2> S_out{};
        for (int k = 0; k < K; ++k) S_out[k] = lam[k] * S;
        double vax_avail_rate = std::max(0.0, max_vax - V) / dt; // max rate not to overshoot cap
        S_out[K]   = std::min(p.vax_rate * S, vax_avail_rate);
        S_out[K+1] = p.mu * S;
        cap_outflows(S_out.data(), K+2, S, dt);

        double vax_flow_rate = S_out[K];
        double mu_S_rate     = S_out[K+1];
        // S_out[0..K-1] = scaled infection rates from S

        // ---- V outflows ----
        std::array<double, MAX_STRAINS + 2> V_out{};
        for (int k = 0; k < K; ++k) V_out[k] = (1.0 - mp.strains[k].vax_cross) * lam[k] * V;
        V_out[K]   = p.omega_v * V;   // waning -> S
        V_out[K+1] = p.mu * V;
        cap_outflows(V_out.data(), K+2, V, dt);

        double omega_v_rate = V_out[K];
        double mu_V_rate    = V_out[K+1];

        // ---- Per-strain E, I, R outflows ----
        // We also compute reinfection outflows from R[j] -> E[k] here, jointly capped.

        // First pass: per-strain E and I outflows (no inter-strain coupling yet)
        std::array<double, MAX_STRAINS> EI_rate{}, mu_E_rate{};
        std::array<double, MAX_STRAINS> IR_rate{}, ID_rate{}, mu_I_rate{};
        std::array<double, MAX_STRAINS> RS_rate{}, mu_R_rate{};
        std::array<double, MAX_STRAINS> hosp_in_rate{}, hosp_out_rate{}, hosp_d_rate{};

        for (int k = 0; k < K; ++k) {
	            if (!mp.strains[k].enabled) continue;
            auto& sk = mp.strains[k];

            double e_out[2] = { sk.sigma * c.E[k], p.mu * c.E[k] };
            cap_outflows(e_out, 2, c.E[k], dt);
            EI_rate[k] = e_out[0]; mu_E_rate[k] = e_out[1];

            double i_out[3] = { sk.gamma*(1.0-sk.ifr)*c.I[k], sk.gamma*sk.ifr*c.I[k], p.mu*c.I[k] };
            cap_outflows(i_out, 3, c.I[k], dt);
            IR_rate[k] = i_out[0]; ID_rate[k] = i_out[1]; mu_I_rate[k] = i_out[2];

            // R: waning + background death. Reinfection outflows added below.
            double r_out[2] = { sk.omega * c.R[k], p.mu * c.R[k] };
            cap_outflows(r_out, 2, c.R[k], dt);
            RS_rate[k] = r_out[0]; mu_R_rate[k] = r_out[1];

            hosp_in_rate[k]  = sk.hosp_rate * EI_rate[k];
            double h_out[2]  = { c.H[k]/14.0, p.delta*c.H[k] };
            cap_outflows(h_out, 2, c.H[k], dt);
            hosp_out_rate[k] = h_out[0]; hosp_d_rate[k] = h_out[1];
        }

        // Reinfection: R[j] -> E[k] for k != j.
        // For each R[j], compute how much leaves for reinfection across all k,
        // then cap jointly with RS and mu_R already reserved.
        std::array<std::array<double, MAX_STRAINS>, MAX_STRAINS> reinf_rate{}; // reinf_rate[k][j]
        for (int j = 0; j < K; ++j) {
	            if (!mp.strains[j].enabled || c.R[j] <= 0) continue;
            // remaining capacity in R[j] after waning+death already reserved
            double reserved = (RS_rate[j] + mu_R_rate[j]) * dt;
            double R_free = std::max(0.0, c.R[j] - reserved);

            std::array<double, MAX_STRAINS> rr{};
            double rr_total = 0;
            for (int k = 0; k < K; ++k) {
	                if (k == j || !mp.strains[k].enabled) continue;
                rr[k] = (1.0 - mp.cross_immunity[k][j]) * lam[k] * c.R[j];
                rr_total += rr[k];
            }
            // cap so reinfections don't exceed free capacity
            double demand = rr_total * dt;
            if (demand > R_free && demand > 0) {
	                double scale = R_free / demand;
                for (int k = 0; k < K; ++k) rr[k] *= scale;
            }
            for (int k = 0; k < K; ++k) reinf_rate[k][j] = rr[k];
        }

        // ---- Apply all changes ----
        // dS
        double dS = 0;
        dS += p.mu * N;                   // births
        dS -= mu_S_rate * dt;             // background deaths from S
        dS -= vax_flow_rate * dt;         // vaccination
        dS += omega_v_rate * dt;          // waning from V
        for (int k = 0; k < K; ++k) {
	            dS -= S_out[k] * dt;          // infections from S
            dS += RS_rate[k] * dt;        // waning from R[k]
        }
        S += dS; S = std::max(S, 0.0);

        // dV
        double dV = 0;
        dV += vax_flow_rate * dt;
        dV -= omega_v_rate * dt;
        dV -= mu_V_rate * dt;
        for (int k = 0; k < K; ++k) dV -= V_out[k] * dt;
        V += dV; V = std::max(V, 0.0);

        for (int k = 0; k < K; ++k) {
	            if (!mp.strains[k].enabled) continue;

            // total reinfection inflow to E[k] from all R[j]
            double reinf_in = 0;
            for (int j = 0; j < K; ++j) reinf_in += reinf_rate[k][j];

            // superinfection (optional small term)
            double superinf = 0;
            if (mp.superinf_factor > 0) {
	                for (int j = 0; j < K; ++j) {
	                    if (j == k || !mp.strains[j].enabled) continue;
                    superinf += mp.superinf_factor * lam[k] * c.I[j];
                }
            }

            double E_in = S_out[k] + V_out[k] + reinf_in + superinf;
            c.E[k] += (E_in - EI_rate[k] - mu_E_rate[k]) * dt;
            c.E[k]  = std::max(c.E[k], 0.0);

            c.I[k] += (EI_rate[k] - IR_rate[k] - ID_rate[k] - mu_I_rate[k]) * dt;
            c.I[k]  = std::max(c.I[k], 0.0);

            // R[k]: gains from I[k] recovery, loses to waning, background death,
            // and reinfection to other strains' E
            double R_reinf_out = 0;
            for (int kk = 0; kk < K; ++kk) R_reinf_out += reinf_rate[kk][k];
            c.R[k] += (IR_rate[k] - RS_rate[k] - mu_R_rate[k] - R_reinf_out) * dt;
            c.R[k]  = std::max(c.R[k], 0.0);

            // Deaths: disease (ID), hosp extra (hosp_d), background from E+I+R
            double dDk = (ID_rate[k] + hosp_d_rate[k]
                         + mu_E_rate[k] + mu_I_rate[k] + mu_R_rate[k]) * dt;
            c.D[k] += dDk;

            // Hospitalisation tracker
            c.H[k] += (hosp_in_rate[k] - hosp_out_rate[k] - hosp_d_rate[k]) * dt;
            c.H[k]  = std::max(c.H[k], 0.0);

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
    MSRunResult avg; avg.ts.resize(len);
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
    MSRunResult pr; pr.ts.resize(len);
    std::vector<double> buf(n);
    for (size_t t = 0; t < len; ++t) {
	        pr.ts[t] = runs[n/2].ts[t];
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
