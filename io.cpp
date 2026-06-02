#include "io.hpp"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <charconv>
#include <cstring>

// Minimal INI-style serialisation: key = value, one per line, # comments.
// Chosen over JSON to avoid bringing in a parser dependency.

static void wln(std::ostream& o, const char* k, double v)  { o << k << " = " << v << "\n"; }
static void wln(std::ostream& o, const char* k, int v)     { o << k << " = " << v << "\n"; }
static void wln(std::ostream& o, const char* k, const std::string& v) { o << k << " = " << v << "\n"; }

std::string save_params(const Params& p, const std::string& path)
{
	    std::ofstream f(path);
    if (!f) return "cannot open " + path + " for writing";
    f << "# CLADES parameter file\n";
    wln(f, "disease_name",   p.disease_name);
    wln(f, "N",              p.N);
    wln(f, "I0",             p.I0);
    wln(f, "E0",             p.E0);
    wln(f, "beta",           p.beta);
    wln(f, "sigma",          p.sigma);
    wln(f, "gamma",          p.gamma);
    wln(f, "mu",             p.mu);
    wln(f, "ifr",            p.ifr);
    wln(f, "hosp_rate",      p.hosp_rate);
    wln(f, "delta",          p.delta);
    wln(f, "omega_r",        p.omega_r);
    wln(f, "omega_v",        p.omega_v);
    wln(f, "vax_rate",       p.vax_rate);
    wln(f, "vax_eff",        p.vax_eff);
    wln(f, "vax_cov",        p.vax_cov);
    wln(f, "season_amp",     p.season_amp);
    wln(f, "season_phi",     p.season_phi);
    wln(f, "noise_sigma",    p.noise_sigma);
    wln(f, "noise_theta",    p.noise_theta);
    wln(f, "T_days",         p.T_days);
    wln(f, "dt",             p.dt);
    return "";
}

std::string load_params(Params& p, const std::string& path)
{
	    std::ifstream f(path);
    if (!f) return "cannot open " + path;

    std::unordered_map<std::string, std::string> kv;
    std::string line;
    while (std::getline(f, line)) {
	        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        // trim
        auto trim = [](std::string& s){
	            size_t a = s.find_first_not_of(" \t\r");
            size_t b = s.find_last_not_of(" \t\r");
            s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
        };
        trim(k); trim(v);
        kv[k] = v;
    }

    auto getd = [&](const char* k, double& dst) {
	        auto it = kv.find(k);
        if (it == kv.end()) return;
        try { dst = std::stod(it->second); } catch(...) {}
    };
    auto geti = [&](const char* k, int& dst) {
	        auto it = kv.find(k);
        if (it == kv.end()) return;
        try { dst = std::stoi(it->second); } catch(...) {}
    };
    auto gets = [&](const char* k, std::string& dst) {
	        auto it = kv.find(k);
        if (it != kv.end()) dst = it->second;
    };

    gets("disease_name",  p.disease_name);
    getd("N",             p.N);
    getd("I0",            p.I0);
    getd("E0",            p.E0);
    getd("beta",          p.beta);
    getd("sigma",         p.sigma);
    getd("gamma",         p.gamma);
    getd("mu",            p.mu);
    getd("ifr",           p.ifr);
    getd("hosp_rate",     p.hosp_rate);
    getd("delta",         p.delta);
    getd("omega_r",       p.omega_r);
    getd("omega_v",       p.omega_v);
    getd("vax_rate",      p.vax_rate);
    getd("vax_eff",       p.vax_eff);
    getd("vax_cov",       p.vax_cov);
    getd("season_amp",    p.season_amp);
    getd("season_phi",    p.season_phi);
    getd("noise_sigma",   p.noise_sigma);
    getd("noise_theta",   p.noise_theta);
    geti("T_days",        p.T_days);
    getd("dt",            p.dt);
    return "";
}

// ---- age params I/O --------------------------------------------------------
#include "age_strat.hpp"

std::string save_age_params(const AgeParams& ap, const std::string& path)
{
	    std::ofstream f(path);
    if (!f) return "cannot open " + path;
    f << "# CLADES age-stratified parameter file\n";
    for (int i = 0; i < N_AGE; ++i)
        f << "pop_frac_" << i << " = " << ap.pop_frac[i] << "\n";
    for (int i = 0; i < N_AGE; ++i)
        f << "ifr_" << i << " = " << ap.ifr[i] << "\n";
    for (int i = 0; i < N_AGE; ++i)
        f << "hosp_rate_" << i << " = " << ap.hosp_rate[i] << "\n";
    for (int i = 0; i < N_AGE; ++i)
        f << "vax_uptake_" << i << " = " << ap.vax_uptake[i] << "\n";
    for (int i = 0; i < N_AGE; ++i)
        for (int j = 0; j < N_AGE; ++j)
            f << "C_" << i << "_" << j << " = " << ap.C[i][j] << "\n";
    return "";
}

std::string load_age_params(AgeParams& ap, const std::string& path)
{
	    std::ifstream f(path);
    if (!f) return "cannot open " + path;
    std::unordered_map<std::string, std::string> kv;
    std::string line;
    while (std::getline(f, line)) {
	        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        auto trim = [](std::string& s){
	            size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
            s = (a == std::string::npos) ? "" : s.substr(a, b-a+1);
        };
        trim(k); trim(v); kv[k] = v;
    }
    auto getd = [&](const std::string& k, double& dst) {
	        auto it = kv.find(k); if (it == kv.end()) return;
        try { dst = std::stod(it->second); } catch(...) {}
    };
    for (int i = 0; i < N_AGE; ++i) {
	        getd("pop_frac_"  + std::to_string(i), ap.pop_frac[i]);
        getd("ifr_"       + std::to_string(i), ap.ifr[i]);
        getd("hosp_rate_" + std::to_string(i), ap.hosp_rate[i]);
        getd("vax_uptake_"+ std::to_string(i), ap.vax_uptake[i]);
        for (int j = 0; j < N_AGE; ++j)
            getd("C_" + std::to_string(i) + "_" + std::to_string(j), ap.C[i][j]);
    }
    return "";
}
