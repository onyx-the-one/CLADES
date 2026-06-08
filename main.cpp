// CLADES — Compartmental Lightweight Agent-based Disease Epidemiology Simulator
// Frontend: Dear ImGui + SDL2 + OpenGL3
// Retro "grey box" styling deliberately imitates early-2000s scientific GUIs.

#include "model.hpp"
#include "io.hpp"
#include "age_strat.hpp"
#include "multi_strain.hpp"

AgeSimResult run_age_ensemble(const Params&, const AgeParams&, ProgressCb);
MSSimResult  run_ms_ensemble (const Params&, const MultiStrainParams&, ProgressCb);

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

#include <SDL.h>
#include <SDL_opengl.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <vector>
#include <algorithm>
#include <chrono>

// ---- retro Win98-ish colour palette ----------------------------------------

// ImGui only has SliderScalar for double; wrap it once here
static bool SliderDouble(const char* label, double* v, double vmin, double vmax, const char* fmt = "%.4f")
{
    return ImGui::SliderScalar(label, ImGuiDataType_Double, v, &vmin, &vmax, fmt);
}

static void apply_retro_style()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 0;
    s.FrameRounding     = 0;
    s.ScrollbarRounding = 0;
    s.GrabRounding      = 0;
    s.TabRounding       = 0;
    s.ChildRounding     = 0;
    s.PopupRounding     = 0;
    s.WindowBorderSize  = 1;
    s.FrameBorderSize   = 1;
    s.ItemSpacing       = ImVec2(6, 4);
    s.FramePadding      = ImVec2(4, 3);

    auto* c = s.Colors;
    // window background — classic grey
    c[ImGuiCol_WindowBg]          = ImVec4(0.753f, 0.753f, 0.753f, 1.0f);
    c[ImGuiCol_ChildBg]           = ImVec4(0.753f, 0.753f, 0.753f, 1.0f);
    c[ImGuiCol_PopupBg]           = ImVec4(0.753f, 0.753f, 0.753f, 1.0f);
    // title bars — classic Win95 deep blue
    c[ImGuiCol_TitleBg]           = ImVec4(0.0f, 0.0f, 0.502f, 1.0f);
    c[ImGuiCol_TitleBgActive]     = ImVec4(0.0f, 0.0f, 0.502f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed]  = ImVec4(0.0f, 0.0f, 0.502f, 0.8f);
    c[ImGuiCol_MenuBarBg]         = ImVec4(0.753f, 0.753f, 0.753f, 1.0f);
    // borders / separators
    c[ImGuiCol_Border]            = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    c[ImGuiCol_Separator]         = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    c[ImGuiCol_SeparatorHovered]  = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    c[ImGuiCol_SeparatorActive]   = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    // frames / inputs — slightly sunken (darker edge on right/bottom)
    c[ImGuiCol_FrameBg]           = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    c[ImGuiCol_FrameBgHovered]    = ImVec4(0.96f, 0.96f, 0.96f, 1.0f);
    c[ImGuiCol_FrameBgActive]     = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
    // buttons — raised 3D bevel effect approximation
    c[ImGuiCol_Button]            = ImVec4(0.827f, 0.827f, 0.827f, 1.0f);
    c[ImGuiCol_ButtonHovered]     = ImVec4(0.878f, 0.878f, 0.878f, 1.0f);
    c[ImGuiCol_ButtonActive]      = ImVec4(0.67f,  0.67f,  0.67f,  1.0f);
    // sliders / check marks
    c[ImGuiCol_SliderGrab]        = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    c[ImGuiCol_SliderGrabActive]  = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    c[ImGuiCol_CheckMark]         = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    // scrollbar
    c[ImGuiCol_ScrollbarBg]       = ImVec4(0.753f, 0.753f, 0.753f, 1.0f);
    c[ImGuiCol_ScrollbarGrab]     = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    // header (combo/selectable)
    c[ImGuiCol_Header]            = ImVec4(0.0f, 0.0f, 0.502f, 1.0f);
    c[ImGuiCol_HeaderHovered]     = ImVec4(0.0f, 0.0f, 0.6f, 1.0f);
    c[ImGuiCol_HeaderActive]      = ImVec4(0.0f, 0.0f, 0.4f, 1.0f);
    // text
    c[ImGuiCol_Text]              = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    c[ImGuiCol_TextDisabled]      = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    // tab bar
    c[ImGuiCol_Tab]               = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    c[ImGuiCol_TabHovered]        = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
    c[ImGuiCol_TabActive]         = ImVec4(0.753f, 0.753f, 0.753f, 1.0f);
    c[ImGuiCol_TabUnfocused]      = ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
    c[ImGuiCol_TabUnfocusedActive]= ImVec4(0.753f, 0.753f, 0.753f, 1.0f);
    // plot
    c[ImGuiCol_PlotLines]         = ImVec4(0.0f, 0.502f, 0.0f, 1.0f);
    c[ImGuiCol_PlotHistogram]     = ImVec4(0.0f, 0.0f, 0.8f, 1.0f);
}

// ---- shared sim state -------------------------------------------------------

struct AppState {
    Params params;
    SimResult result;
    bool has_result = false;

    std::atomic<bool> running{false};
    std::atomic<int>  progress_run{0};
    std::atomic<int>  progress_day{0};
    std::atomic<int>  total_days{0};
    std::mutex        result_mtx;

    char   load_path[512] = "clades_params.ini";
    char   save_path[512] = "clades_params.ini";
    char   export_csv_path[512] = "clades_result.csv";
    std::string status_msg;
    bool   show_about = false;

    // age-stratified extension
    AgeParams  age_params;
    AgeSimResult age_result;
    bool has_age_result = false;
    bool age_mode = false;
    bool also_homogeneous = true; // run homogeneous alongside age/ms mode
    bool show_contact_matrix = false;

    // multi-strain extension
    MultiStrainParams ms_params;
    MSSimResult       ms_result;
    bool has_ms_result = false;
    bool ms_mode       = false;
    bool show_cross_mtx = false;
};

static AppState g;

// ---- simulation thread ------------------------------------------------------

static void sim_thread_fn()
{
    Params    p  = g.params;
    AgeParams ap = g.age_params;
    bool age_mode = g.age_mode;
    bool ms_mode  = g.ms_mode; (void)ms_mode;

    g.progress_run = 0;
    g.total_days = p.T_days;

    auto cb = [](int ri, int rn, int day, int tdays) {
        g.progress_run = ri;
        g.progress_day = day;
        (void)rn; (void)tdays;
    };

    // homogeneous always runs if: no special mode, or also_homogeneous is set
    bool run_homo = (!age_mode && !g.ms_mode) || g.also_homogeneous;
    if (run_homo) {
        SimResult sr = run_ensemble(p, cb);
        std::lock_guard<std::mutex> lk(g.result_mtx);
        g.result     = std::move(sr);
        g.has_result = true;
    }
    if (age_mode) {
        ap.enabled = true;
        AgeSimResult asr = run_age_ensemble(p, ap, cb);
        std::lock_guard<std::mutex> lk(g.result_mtx);
        g.age_result     = std::move(asr);
        g.has_age_result = true;
    }
    if (g.ms_mode) {
        MultiStrainParams mp = g.ms_params;
        MSSimResult msr = run_ms_ensemble(p, mp, cb);
        std::lock_guard<std::mutex> lk(g.result_mtx);
        g.ms_result     = std::move(msr);
        g.has_ms_result = true;
    }
    g.running = false;
    g.status_msg = "Simulation complete.";
}

// ---- CSV export -------------------------------------------------------------

static std::string export_csv(const std::string& path)
{
    std::lock_guard<std::mutex> lk(g.result_mtx);
    if (!g.has_result) return "No results to export.";
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return "Cannot open " + path;
    fprintf(f, "day,S,E,I,H,R,V,D,Rt,beta_eff,I_lo,I_hi,D_lo,D_hi\n");
    auto& m = g.result.mean;
    auto& lo= g.result.lo;
    auto& hi= g.result.hi;
    size_t n = m.ts.size();
    for (size_t i = 0; i < n; ++i) {
        double day = i * g.params.dt;
        // round to int day if we recorded once per day
        fprintf(f, "%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.4f,%.4f,%.1f,%.1f,%.1f,%.1f\n",
            day,
            m.ts[i].S, m.ts[i].E, m.ts[i].I, m.ts[i].H,
            m.ts[i].R, m.ts[i].V, m.ts[i].D,
            m.ts[i].Rt, m.ts[i].beta_eff,
            lo.ts[i].I, hi.ts[i].I,
            lo.ts[i].D, hi.ts[i].D);
    }
    fclose(f);
    return "";
}

// ---- parameter panel --------------------------------------------------------

static void param_panel()
{
    ImGui::TextUnformatted("Disease");
    ImGui::Separator();

    static char dis_buf[128];
    if (dis_buf[0] == '\0')
        strncpy(dis_buf, g.params.disease_name.c_str(), 127);
    if (ImGui::InputText("Name##dis", dis_buf, sizeof(dis_buf)))
        g.params.disease_name = dis_buf;

    ImGui::Spacing();
    ImGui::TextUnformatted("Population");
    ImGui::Separator();

    ImGui::InputDouble("N (total)", &g.params.N, 1000, 100000, "%.0f");
    ImGui::InputDouble("I(0) initial infect.", &g.params.I0, 1, 100, "%.1f");
    ImGui::InputDouble("E(0) initial exposed", &g.params.E0, 1, 100, "%.1f");
    ImGui::InputDouble("mu (birth/death rate)", &g.params.mu, 0.0, 0.01, "%.5f");

    ImGui::Spacing();
    ImGui::TextUnformatted("Transmission");
    ImGui::Separator();

    SliderDouble("beta (contact rate)", &g.params.beta, 0.0, 2.0, "%.4f");
    SliderDouble("sigma (1/incubation)", &g.params.sigma, 0.01, 1.0, "%.4f");
    SliderDouble("gamma (1/infect. period)", &g.params.gamma, 0.01, 1.0, "%.4f");

    {
        double r0 = (g.params.gamma > 0) ? g.params.beta / g.params.gamma : 0.0;
        ImGui::LabelText("R0 (approx)", "%.2f", r0);
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Disease Severity");
    ImGui::Separator();

    SliderDouble("IFR (fatality ratio)", &g.params.ifr, 0.0, 0.3, "%.4f");
    SliderDouble("Hosp. rate (frac I)", &g.params.hosp_rate, 0.0, 0.5, "%.4f");
    SliderDouble("delta (hosp. extra mort)", &g.params.delta, 0.0, 0.1, "%.4f");

    ImGui::Spacing();
    ImGui::TextUnformatted("Immunity & Vaccination");
    ImGui::Separator();

    SliderDouble("omega_r (wane recov/day)", &g.params.omega_r, 0.0, 0.05, "%.5f");
    SliderDouble("omega_v (wane vax/day)",  &g.params.omega_v, 0.0, 0.05, "%.5f");
    SliderDouble("Vax rate (frac S/day)",   &g.params.vax_rate, 0.0, 0.05, "%.5f");
    SliderDouble("Vax efficacy",            &g.params.vax_eff,  0.0, 1.0,  "%.3f");
    SliderDouble("Vax coverage cap",        &g.params.vax_cov,  0.0, 1.0,  "%.3f");

    ImGui::Spacing();
    ImGui::TextUnformatted("Seasonality");
    ImGui::Separator();

    SliderDouble("Season amplitude", &g.params.season_amp, 0.0, 1.0, "%.3f");
    SliderDouble("Season phase (days)", &g.params.season_phi, 0.0, 365.0, "%.1f");

    ImGui::Spacing();
    ImGui::TextUnformatted("Stochastic Noise (OU on beta)");
    ImGui::Separator();

    SliderDouble("Noise sigma", &g.params.noise_sigma, 0.0, 0.5, "%.4f");
    SliderDouble("Noise theta (mean-rev)", &g.params.noise_theta, 0.0, 1.0, "%.4f");

    ImGui::Spacing();
    ImGui::TextUnformatted("Simulation");
    ImGui::Separator();

    ImGui::InputInt("Duration (days)", &g.params.T_days, 10, 100);
    SliderDouble("dt (step, days)", &g.params.dt, 0.01, 1.0, "%.3f");
}

// ---- results panel ----------------------------------------------------------

static void results_summary()
{
    std::lock_guard<std::mutex> lk(g.result_mtx);
    if (!g.has_result) {
        ImGui::TextUnformatted("No results yet. Run simulation.");
        return;
    }
    auto& r = g.result.mean;
    ImGui::LabelText("Peak infectious",    "%.0f  (day %.1f)", r.peak_I, r.peak_day);
    ImGui::LabelText("Total deaths",       "%.0f", r.total_D);
    ImGui::LabelText("Attack rate",        "%.2f%%", r.total_attack_rate * 100.0);
    ImGui::LabelText("Peak I / N",         "%.2f%%", 100.0 * r.peak_I / g.params.N);
    ImGui::LabelText("Ensemble runs",      "%d", g.result.n_runs);

    // final compartment values
    if (!r.ts.empty()) {
        auto& last = r.ts.back();
        ImGui::Separator();
        ImGui::TextUnformatted("Final state (mean):");
        ImGui::LabelText("S", "%.0f", last.S);
        ImGui::LabelText("E", "%.0f", last.E);
        ImGui::LabelText("I", "%.0f", last.I);
        ImGui::LabelText("R", "%.0f", last.R);
        ImGui::LabelText("V", "%.0f", last.V);
        ImGui::LabelText("D", "%.0f", last.D);
    }
}

// ---- plot panel  ------------------------------------------------------------

static void plot_panel()
{
    std::lock_guard<std::mutex> lk(g.result_mtx);
    if (!g.has_result) {
        ImGui::TextUnformatted("Run simulation to see plots.");
        return;
    }

    auto& m  = g.result.mean;
    auto& lo = g.result.lo;
    auto& hi = g.result.hi;
    size_t n = m.ts.size();

    static std::vector<double> xs, S_, E_, I_, R_, V_, D_, H_, Rt_,
                                I_lo, I_hi, D_lo, D_hi;
    xs.resize(n); S_.resize(n); E_.resize(n); I_.resize(n);
    R_.resize(n); V_.resize(n); D_.resize(n); H_.resize(n); Rt_.resize(n);
    I_lo.resize(n); I_hi.resize(n); D_lo.resize(n); D_hi.resize(n);

    for (size_t i = 0; i < n; ++i) {
        xs[i]   = i;
        S_[i]   = m.ts[i].S;  E_[i]  = m.ts[i].E;
        I_[i]   = m.ts[i].I;  R_[i]  = m.ts[i].R;
        V_[i]   = m.ts[i].V;  D_[i]  = m.ts[i].D;
        H_[i]   = m.ts[i].H;  Rt_[i] = m.ts[i].Rt;
        I_lo[i] = lo.ts[i].I; I_hi[i]= hi.ts[i].I;
        D_lo[i] = lo.ts[i].D; D_hi[i]= hi.ts[i].D;
    }

    float pw = ImGui::GetContentRegionAvail().x;
    float ph = 220;

    if (ImPlot::BeginPlot("Compartments##cp", ImVec2(pw, ph))) {
        ImPlot::SetupAxes("Day", "Population");
        ImPlot::PlotLine("S", xs.data(), S_.data(), (int)n);
        ImPlot::PlotLine("E", xs.data(), E_.data(), (int)n);
        ImPlot::PlotLine("I (mean)", xs.data(), I_.data(), (int)n);
        ImPlot::PlotLine("R", xs.data(), R_.data(), (int)n);
        ImPlot::PlotLine("V", xs.data(), V_.data(), (int)n);
        ImPlot::PlotShaded("I 10-90%", xs.data(), I_lo.data(), I_hi.data(), (int)n);
        ImPlot::EndPlot();
    }

    if (ImPlot::BeginPlot("Deaths##dp", ImVec2(pw, ph))) {
        ImPlot::SetupAxes("Day", "Cumulative Deaths");
        ImPlot::PlotLine("D (mean)", xs.data(), D_.data(), (int)n);
        ImPlot::PlotShaded("D 10-90%", xs.data(), D_lo.data(), D_hi.data(), (int)n);
        ImPlot::EndPlot();
    }

    if (ImPlot::BeginPlot("Hospitalisations##hp", ImVec2(pw, ph))) {
        ImPlot::SetupAxes("Day", "H (active)");
        ImPlot::PlotLine("H", xs.data(), H_.data(), (int)n);
        ImPlot::EndPlot();
    }

    if (ImPlot::BeginPlot("Rt##rp", ImVec2(pw, ph))) {
        ImPlot::SetupAxes("Day", "Rt");
        ImPlot::PlotLine("Rt", xs.data(), Rt_.data(), (int)n);
        // threshold line at 1
        double lx[2] = {xs.front(), xs.back()};
        double ly[2] = {1.0, 1.0};
        ImPlot::SetNextLineStyle(ImVec4(0.8f,0.0f,0.0f,1.0f));
        ImPlot::PlotLine("Rt=1", lx, ly, 2);
        ImPlot::EndPlot();
    }
}

// ---- about window -----------------------------------------------------------

static void about_window()
{
    ImGui::SetNextWindowSize(ImVec2(420, 280), ImGuiCond_Always);
    if (ImGui::Begin("About CLADES", &g.show_about, ImGuiWindowFlags_NoResize)) {
        ImGui::TextUnformatted("CLADES v0.1.0");
        ImGui::TextUnformatted("Compartmental Lightweight Agent-based");
        ImGui::TextUnformatted("Disease Epidemiology Simulator");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Model: SEIVRD + waning immunity");
        ImGui::TextUnformatted("Stochastic: Ornstein-Uhlenbeck noise on beta");
        ImGui::TextUnformatted("Ensemble: 5 runs, ensemble-averaged output");
        ImGui::TextUnformatted("Integration: Euler-Maruyama (SDE)");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("References:");
        ImGui::TextWrapped(
            "- Hernandez et al. PLoS ONE 2021 (arbitrary distributions)\n"
            "- Royal Soc. A 2023 (OU correlated uncertainty)\n"
            "- PMC9983535 (waning immunity + vaccination SEIR)\n"
            "- PMC11052727 (immuno-epidemio waning model)"
        );
    }
    ImGui::End();
}



// ---- multi-strain cross-immunity matrix editor -----------------------------

static void cross_immunity_window()
{
    ImGui::SetNextWindowSize(ImVec2(500, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Cross-Immunity Matrix", &g.show_cross_mtx)) {
        ImGui::End(); return;
    }
    int K = std::min(g.ms_params.n_strains, MAX_STRAINS);
    ImGui::TextUnformatted("X[k][j]: protection that prior infection with strain j");
    ImGui::TextUnformatted("         gives against strain k.  1=full, 0=none.");
    ImGui::Separator();

    // header
    ImGui::TextUnformatted("       ");
    for (int j = 0; j < K; ++j) {
        ImGui::SameLine();
        ImGui::Text("%10s", g.ms_params.strains[j].name.c_str());
    }
    for (int k = 0; k < K; ++k) {
        ImGui::Text("%8s", g.ms_params.strains[k].name.c_str());
        for (int j = 0; j < K; ++j) {
            ImGui::SameLine();
            if (k == j) {
                ImGui::TextDisabled("  [self] "); // diagonal is meaningless
                continue;
            }
            char lbl[32]; snprintf(lbl, sizeof(lbl), "##xi%d%d", k, j);
            ImGui::SetNextItemWidth(88);
            float tmp = (float)g.ms_params.cross_immunity[k][j];
            if (ImGui::SliderFloat(lbl, &tmp, 0.0f, 1.0f, "%.2f"))
                g.ms_params.cross_immunity[k][j] = tmp;
        }
    }
    ImGui::Spacing();
    if (ImGui::Button("Set all to 0 (no cross-immunity)")) {
        for (int k = 0; k < MAX_STRAINS; ++k)
            for (int j = 0; j < MAX_STRAINS; ++j)
                g.ms_params.cross_immunity[k][j] = 0.0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Set all to 0.5")) {
        for (int k = 0; k < MAX_STRAINS; ++k)
            for (int j = 0; j < MAX_STRAINS; ++j)
                if (k != j) g.ms_params.cross_immunity[k][j] = 0.5;
    }
    ImGui::End();
}

// ---- multi-strain parameter panel ------------------------------------------

static void ms_param_panel()
{
    ImGui::Checkbox("Enable multi-strain mode", &g.ms_mode);
    if (g.ms_mode && g.age_mode) {
        ImGui::TextColored(ImVec4(0.9f,0.3f,0.1f,1), "Note: age-stratified mode is also on.");
        ImGui::TextWrapped("Only one mode runs at a time. Multi-strain takes priority.");
    }
    if (!g.ms_mode) { ImGui::TextDisabled("Enable to unlock strain settings."); return; }

    ImGui::Spacing();
    ImGui::SliderInt("Number of strains", &g.ms_params.n_strains, 1, MAX_STRAINS);
    SliderDouble("Superinfection factor", &g.ms_params.superinf_factor, 0.0, 0.5, "%.4f");
    ImGui::Spacing();

    if (ImGui::Button("Edit Cross-Immunity Matrix..."))
        g.show_cross_mtx = true;

    ImGui::Separator();
    int K = std::min(g.ms_params.n_strains, MAX_STRAINS);
    for (int k = 0; k < K; ++k) {
        auto& sk = g.ms_params.strains[k];
        char hdr[64]; snprintf(hdr, sizeof(hdr), "Strain %d: %s###s%d", k, sk.name.c_str(), k);
        if (!ImGui::CollapsingHeader(hdr)) continue;

        // name buffer — keep one per strain slot
        static char nbuf[MAX_STRAINS][64];
        if (nbuf[k][0] == '\0') strncpy(nbuf[k], sk.name.c_str(), 63);
        char nlbl[16]; snprintf(nlbl, sizeof(nlbl), "Name##n%d", k);
        if (ImGui::InputText(nlbl, nbuf[k], 64)) sk.name = nbuf[k];

        ImGui::Checkbox(("Enabled##en" + std::to_string(k)).c_str(), &sk.enabled);

        char sl[32];
        snprintf(sl,sizeof(sl),"beta##b%d",k);   SliderDouble(sl, &sk.beta,   0.0, 3.0, "%.4f");
        snprintf(sl,sizeof(sl),"sigma##sg%d",k);  SliderDouble(sl, &sk.sigma,  0.01, 1.0,"%.4f");
        snprintf(sl,sizeof(sl),"gamma##g%d",k);   SliderDouble(sl, &sk.gamma,  0.01, 1.0,"%.4f");
        snprintf(sl,sizeof(sl),"IFR##ifr%d",k);   SliderDouble(sl, &sk.ifr,    0.0, 0.3, "%.5f");
        snprintf(sl,sizeof(sl),"Hosp##h%d",k);    SliderDouble(sl, &sk.hosp_rate,0.0,0.5,"%.4f");
        snprintf(sl,sizeof(sl),"omega##w%d",k);   SliderDouble(sl, &sk.omega,  0.0, 0.05,"%.5f");
        snprintf(sl,sizeof(sl),"Vax cross-prot##vc%d",k); SliderDouble(sl, &sk.vax_cross,0.0,1.0,"%.3f");

        double r0k = (sk.gamma > 0) ? sk.beta / sk.gamma : 0.0;
        ImGui::LabelText(("R0##r0" + std::to_string(k)).c_str(), "%.2f", r0k);

        ImGui::Spacing();
        ImGui::TextUnformatted("Introduction:");
        snprintf(sl,sizeof(sl),"Day##id%d",k);    ImGui::SetNextItemWidth(120);
        ImGui::InputDouble(sl, &sk.intro_day,  1, 10, "%.0f");
        snprintf(sl,sizeof(sl),"Size##is%d",k);   ImGui::SetNextItemWidth(120);
        ImGui::InputDouble(sl, &sk.intro_size, 1, 100,"%.0f");
    }
}

// ---- multi-strain plot panel -----------------------------------------------

static void ms_plot_panel()
{
    std::lock_guard<std::mutex> lk(g.result_mtx);
    if (!g.has_ms_result) {
        ImGui::TextUnformatted("Run simulation in multi-strain mode to see plots.");
        return;
    }
    auto& m  = g.ms_result.mean;
    auto& lo = g.ms_result.lo;
    auto& hi = g.ms_result.hi;
    size_t n = m.ts.size();
    int K = std::min(g.ms_params.n_strains, MAX_STRAINS);

    static std::vector<double> xs, S_, V_;
    xs.resize(n); S_.resize(n); V_.resize(n);
    for (size_t t = 0; t < n; ++t) { xs[t]=(double)t; S_[t]=m.ts[t].S; V_[t]=m.ts[t].V; }

    float pw = ImGui::GetContentRegionAvail().x;

    static const ImVec4 scol[MAX_STRAINS] = {
        {0.9f,0.2f,0.2f,1},{0.2f,0.6f,1.0f,1},{0.2f,0.85f,0.3f,1},
        {1.0f,0.7f,0.1f,1},{0.8f,0.1f,0.9f,1},{0.1f,0.9f,0.9f,1},
    };

    // Infectious per strain
    if (ImPlot::BeginPlot("Infectious by Strain##msI", ImVec2(pw, 240))) {
        ImPlot::SetupAxes("Day", "I(t)");
        static std::vector<double> Ik(0), Ilo(0), Ihi(0);
        Ik.resize(n); Ilo.resize(n); Ihi.resize(n);
        for (int k = 0; k < K; ++k) {
            if (!g.ms_params.strains[k].enabled) continue;
            for (size_t t = 0; t < n; ++t) {
                Ik[t]  = m.ts[t].c.I[k];
                Ilo[t] = lo.ts[t].c.I[k];
                Ihi[t] = hi.ts[t].c.I[k];
            }
            ImPlot::SetNextLineStyle(scol[k]);
            ImPlot::PlotLine(g.ms_params.strains[k].name.c_str(), xs.data(), Ik.data(), (int)n);
            ImPlot::SetNextFillStyle(scol[k], 0.15f);
            ImPlot::PlotShaded(("##shd"+std::to_string(k)).c_str(), xs.data(), Ilo.data(), Ihi.data(), (int)n);
        }
        ImPlot::EndPlot();
    }

    // Susceptible + Vaccinated shared pool
    if (ImPlot::BeginPlot("Shared S & V pool##msSV", ImVec2(pw, 180))) {
        ImPlot::SetupAxes("Day", "Population");
        ImPlot::PlotLine("S", xs.data(), S_.data(), (int)n);
        ImPlot::PlotLine("V", xs.data(), V_.data(), (int)n);
        ImPlot::EndPlot();
    }

    // Rt per strain
    if (ImPlot::BeginPlot("Rt by Strain##msRt", ImVec2(pw, 180))) {
        ImPlot::SetupAxes("Day", "Rt");
        static std::vector<double> Rk(0);
        Rk.resize(n);
        for (int k = 0; k < K; ++k) {
            if (!g.ms_params.strains[k].enabled) continue;
            for (size_t t = 0; t < n; ++t) Rk[t] = m.ts[t].Rt[k];
            ImPlot::SetNextLineStyle(scol[k]);
            ImPlot::PlotLine(g.ms_params.strains[k].name.c_str(), xs.data(), Rk.data(), (int)n);
        }
        double lx[2]={xs.front(),xs.back()}, ly[2]={1.0,1.0};
        ImPlot::SetNextLineStyle(ImVec4(0,0,0,0.5f));
        ImPlot::PlotLine("Rt=1", lx, ly, 2);
        ImPlot::EndPlot();
    }

    // Cumulative deaths per strain
    if (ImPlot::BeginPlot("Cumulative Deaths by Strain##msD", ImVec2(pw, 180))) {
        ImPlot::SetupAxes("Day", "D(t)");
        static std::vector<double> Dk(0);
        Dk.resize(n);
        for (int k = 0; k < K; ++k) {
            if (!g.ms_params.strains[k].enabled) continue;
            for (size_t t = 0; t < n; ++t) Dk[t] = m.ts[t].c.D[k];
            ImPlot::SetNextLineStyle(scol[k]);
            ImPlot::PlotLine(g.ms_params.strains[k].name.c_str(), xs.data(), Dk.data(), (int)n);
        }
        ImPlot::EndPlot();
    }
}

// ---- multi-strain summary panel --------------------------------------------

static void ms_summary_panel()
{
    std::lock_guard<std::mutex> lk(g.result_mtx);
    if (!g.has_ms_result) { ImGui::TextUnformatted("No multi-strain results."); return; }
    auto& r = g.ms_result.mean;
    int K = std::min(g.ms_params.n_strains, MAX_STRAINS);
    ImGui::LabelText("Overall attack rate", "%.2f%%", r.total_attack_rate * 100.0);
    ImGui::Separator();
    if (ImGui::BeginTable("##mssum", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Strain");
        ImGui::TableSetupColumn("R0");
        ImGui::TableSetupColumn("Peak I");
        ImGui::TableSetupColumn("Peak day");
        ImGui::TableSetupColumn("Total D");
        ImGui::TableHeadersRow();
        for (int k = 0; k < K; ++k) {
            if (!g.ms_params.strains[k].enabled) continue;
            auto& sk = g.ms_params.strains[k];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(sk.name.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", sk.gamma>0?sk.beta/sk.gamma:0.0);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.0f", r.peak_I[k]);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%.1f", r.peak_day[k]);
            ImGui::TableSetColumnIndex(4); ImGui::Text("%.0f", r.total_D[k]);
        }
        ImGui::EndTable();
    }
}

// ---- contact matrix editor -------------------------------------------------

static void contact_matrix_window()
{
    ImGui::SetNextWindowSize(ImVec2(600, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Contact Matrix Editor", &g.show_contact_matrix)) {
        ImGui::End(); return;
    }
    ImGui::TextUnformatted("C[i][j] = mean daily contacts age group i -> j");
    ImGui::TextUnformatted("Default: POLYMOD European average (Mossong 2008 / Prem 2017)");
    ImGui::Separator();

    ImGui::BeginChild("##cmtx", ImVec2(0, 360), false, ImGuiWindowFlags_HorizontalScrollbar);
    // header row
    ImGui::TextUnformatted("       ");
    for (int j = 0; j < N_AGE; ++j) {
        ImGui::SameLine();
        ImGui::Text("%6s", AGE_LABELS[j]);
    }
    for (int i = 0; i < N_AGE; ++i) {
        ImGui::Text("%6s", AGE_LABELS[i]);
        for (int j = 0; j < N_AGE; ++j) {
            ImGui::SameLine();
            char lbl[32]; snprintf(lbl, sizeof(lbl), "##c%d%d", i, j);
            ImGui::SetNextItemWidth(58);
            ImGui::InputDouble(lbl, &g.age_params.C[i][j], 0, 0, "%.2f");
        }
    }
    ImGui::EndChild();

    if (ImGui::Button("Reset to POLYMOD defaults")) {
        for (int i = 0; i < N_AGE; ++i)
            for (int j = 0; j < N_AGE; ++j)
                g.age_params.C[i][j] = POLYMOD_DEFAULT[i][j];
    }
    ImGui::SameLine();
    // Symmetrise: C_sym[i][j] = (C[i][j]*N[i] + C[j][i]*N[j]) / (2*N[i])
    if (ImGui::Button("Symmetrise (reciprocal)")) {
        double N_g[N_AGE];
        double Ntot = g.params.N;
        for (int i = 0; i < N_AGE; ++i) N_g[i] = g.age_params.pop_frac[i] * Ntot;
        for (int i = 0; i < N_AGE; ++i)
            for (int j = i+1; j < N_AGE; ++j) {
                double sym = (g.age_params.C[i][j]*N_g[i] + g.age_params.C[j][i]*N_g[j])
                              / (2.0 * std::max(N_g[i], 1.0));
                double symT= (g.age_params.C[i][j]*N_g[i] + g.age_params.C[j][i]*N_g[j])
                              / (2.0 * std::max(N_g[j], 1.0));
                g.age_params.C[i][j] = sym;
                g.age_params.C[j][i] = symT;
            }
    }
    ImGui::End();
}

// ---- age parameter panel (tab inside left pane) ----------------------------

static void age_param_panel()
{
    ImGui::Checkbox("Enable age-stratified mode", &g.age_mode);
    if (g.age_mode)
        ImGui::Checkbox("Also run homogeneous (both results)", &g.also_homogeneous);
    if (!g.age_mode) {
        ImGui::TextDisabled("Enable to unlock age-stratified settings.");
        return;
    }
    ImGui::Spacing();
    if (ImGui::Button("Edit Contact Matrix..."))
        g.show_contact_matrix = true;

    ImGui::Spacing();
    ImGui::TextUnformatted("Population fractions (must sum to 1):");
    double sum = 0;
    for (int i = 0; i < N_AGE; ++i) sum += g.age_params.pop_frac[i];
    bool sum_ok = std::fabs(sum - 1.0) <= 0.01;
    if (!sum_ok) ImGui::TextColored(ImVec4(0.8f,0.1f,0.1f,1), "Sum: %.4f  (!)", sum);
    else         ImGui::Text("Sum: %.4f  OK", sum);

    if (ImGui::BeginTable("##agefracs", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Group"); ImGui::TableSetupColumn("Pop frac"); ImGui::TableSetupColumn("Vax uptake");
        ImGui::TableHeadersRow();
        for (int i = 0; i < N_AGE; ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(AGE_LABELS[i]);
            ImGui::TableSetColumnIndex(1);
            char lbl[32]; snprintf(lbl, sizeof(lbl), "##pf%d", i);
            ImGui::SetNextItemWidth(80);
            ImGui::InputDouble(lbl, &g.age_params.pop_frac[i], 0, 0, "%.4f");
            ImGui::TableSetColumnIndex(2);
            snprintf(lbl, sizeof(lbl), "##vu%d", i);
            ImGui::SetNextItemWidth(80);
            ImGui::InputDouble(lbl, &g.age_params.vax_uptake[i], 0, 0, "%.3f");
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Per-group IFR and hospitalisation rate:");
    if (ImGui::BeginTable("##ageifr", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Group"); ImGui::TableSetupColumn("IFR"); ImGui::TableSetupColumn("Hosp rate");
        ImGui::TableHeadersRow();
        for (int i = 0; i < N_AGE; ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(AGE_LABELS[i]);
            ImGui::TableSetColumnIndex(1);
            char lbl[32]; snprintf(lbl, sizeof(lbl), "##ifr%d", i);
            ImGui::SetNextItemWidth(90);
            ImGui::InputDouble(lbl, &g.age_params.ifr[i], 0, 0, "%.5f");
            ImGui::TableSetColumnIndex(2);
            snprintf(lbl, sizeof(lbl), "##hr%d", i);
            ImGui::SetNextItemWidth(90);
            ImGui::InputDouble(lbl, &g.age_params.hosp_rate[i], 0, 0, "%.4f");
        }
        ImGui::EndTable();
    }

    if (ImGui::Button("Reset IFR to COVID-like defaults")) {
        for (int i = 0; i < N_AGE; ++i) {
            g.age_params.ifr[i]       = IFR_DEFAULT[i];
            g.age_params.hosp_rate[i] = HOSP_DEFAULT[i];
        }
    }
}

// ---- age results plot panel ------------------------------------------------

static void age_plot_panel()
{
    std::lock_guard<std::mutex> lk(g.result_mtx);
    if (!g.has_age_result) {
        ImGui::TextUnformatted("Run simulation in age-stratified mode to see age plots.");
        return;
    }

    auto& m = g.age_result.mean;
    size_t n = m.ts.size();
    static std::vector<double> xs;
    xs.resize(n);
    for (size_t t = 0; t < n; ++t) xs[t] = (double)t;

    float pw = ImGui::GetContentRegionAvail().x;

    // Infectious by age group
    if (ImPlot::BeginPlot("Infectious by Age Group##aI", ImVec2(pw, 240))) {
        ImPlot::SetupAxes("Day", "I(t)");
        static const ImVec4 age_colors[N_AGE] = {
            {0.2f,0.6f,1.0f,1}, {0.2f,0.9f,0.5f,1}, {1.0f,0.8f,0.1f,1}, {1.0f,0.4f,0.1f,1},
            {0.9f,0.1f,0.3f,1}, {0.6f,0.1f,0.9f,1}, {0.1f,0.7f,0.9f,1}, {0.5f,0.5f,0.5f,1},
        };
        static std::vector<double> Ig(0);
        Ig.resize(n);
        for (int i = 0; i < N_AGE; ++i) {
            for (size_t t = 0; t < n; ++t) Ig[t] = m.ts[t].s.I[i];
            ImPlot::SetNextLineStyle(age_colors[i]);
            ImPlot::PlotLine(AGE_LABELS[i], xs.data(), Ig.data(), (int)n);
        }
        ImPlot::EndPlot();
    }

    // Deaths by age group
    if (ImPlot::BeginPlot("Cumulative Deaths by Age##aD", ImVec2(pw, 240))) {
        ImPlot::SetupAxes("Day", "D(t)");
        static std::vector<double> Dg(0);
        Dg.resize(n);
        for (int i = 0; i < N_AGE; ++i) {
            for (size_t t = 0; t < n; ++t) Dg[t] = m.ts[t].s.D[i];
            ImPlot::PlotLine(AGE_LABELS[i], xs.data(), Dg.data(), (int)n);
        }
        ImPlot::EndPlot();
    }

    // Rt by age group
    if (ImPlot::BeginPlot("Rt by Age Group##aRt", ImVec2(pw, 200))) {
        ImPlot::SetupAxes("Day", "Rt");
        static std::vector<double> Rg(0);
        Rg.resize(n);
        for (int i = 0; i < N_AGE; ++i) {
            for (size_t t = 0; t < n; ++t) Rg[t] = m.ts[t].s.Rt[i];
            ImPlot::PlotLine(AGE_LABELS[i], xs.data(), Rg.data(), (int)n);
        }
        double lx[2] = {xs.front(), xs.back()}, ly[2] = {1.0, 1.0};
        ImPlot::SetNextLineStyle(ImVec4(0.8f,0,0,1));
        ImPlot::PlotLine("Rt=1", lx, ly, 2);
        ImPlot::EndPlot();
    }
}

// ---- age summary panel -----------------------------------------------------

static void age_summary_panel()
{
    std::lock_guard<std::mutex> lk(g.result_mtx);
    if (!g.has_age_result) { ImGui::TextUnformatted("No age results."); return; }
    auto& r = g.age_result.mean;
    ImGui::LabelText("Overall attack rate", "%.2f%%", r.total_attack_rate * 100.0);
    ImGui::Separator();
    if (ImGui::BeginTable("##agesum", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Group");
        ImGui::TableSetupColumn("Peak I");
        ImGui::TableSetupColumn("Peak day");
        ImGui::TableSetupColumn("Total D");
        ImGui::TableHeadersRow();
        for (int i = 0; i < N_AGE; ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(AGE_LABELS[i]);
            ImGui::TableSetColumnIndex(1); ImGui::Text("%.0f", r.peak_I[i]);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.1f", r.peak_day[i]);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%.0f", r.total_D[i]);
        }
        ImGui::EndTable();
    }
}

// ---- main -------------------------------------------------------------------

int main(int, char**)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window* win = SDL_CreateWindow(
        "CLADES - Epidemiology Simulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 800,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gl_ctx);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // 17px is the minimum comfortable size on 1080p+ displays
    ImFontConfig fcfg; fcfg.SizePixels = 17.0f;
    io.Fonts->AddFontDefault(&fcfg);
    io.FontDefault = io.Fonts->Fonts.back();

    apply_retro_style();

    ImGui_ImplSDL2_InitForOpenGL(win, gl_ctx);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    bool done = false;
    while (!done) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) done = true;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // --- menu bar -------------------------------------------------------
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Load Parameters...")) {
                    std::string err = load_params(g.params, g.load_path);
                    g.status_msg = err.empty() ? "Loaded." : "Load error: " + err;
                }
                if (ImGui::MenuItem("Save Parameters...")) {
                    std::string err = save_params(g.params, g.save_path);
                    g.status_msg = err.empty() ? "Saved." : "Save error: " + err;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Export CSV...")) {
                    std::string err = export_csv(g.export_csv_path);
                    g.status_msg = err.empty() ? "Exported." : "Export error: " + err;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit")) done = true;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("About")) g.show_about = true;
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // --- main dockspace layout  -----------------------------------------
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + 18));
        ImGui::SetNextWindowSize(ImVec2(vp->Size.x, vp->Size.y - 18));
        ImGui::SetNextWindowBgAlpha(1.0f);
        ImGui::Begin("##root", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

        float left_w  = 390;
        float right_w = ImGui::GetContentRegionAvail().x - left_w - 8;
        float h = ImGui::GetContentRegionAvail().y - 44;

        // Left pane: parameters
        ImGui::BeginChild("##params_child", ImVec2(left_w, h), true,
            ImGuiWindowFlags_HorizontalScrollbar);
        if (ImGui::BeginTabBar("##ptabs")) {
            if (ImGui::BeginTabItem("Parameters")) {
                param_panel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Strains")) {
                ms_param_panel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Age Groups")) {
                age_param_panel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Files")) {
                ImGui::TextUnformatted("Load/Save parameter file:");
                ImGui::InputText("##lpath", g.load_path, sizeof(g.load_path));
                if (ImGui::Button("Load")) {
                    std::string err = load_params(g.params, g.load_path);
                    g.status_msg = err.empty() ? "Loaded." : err;
                }
                ImGui::SameLine();
                snprintf(g.save_path, sizeof(g.save_path), "%s", g.load_path);
                if (ImGui::Button("Save")) {
                    std::string err = save_params(g.params, g.save_path);
                    g.status_msg = err.empty() ? "Saved." : err;
                }
                ImGui::Spacing();
                ImGui::TextUnformatted("Export CSV path:");
                ImGui::InputText("##cpath", g.export_csv_path, sizeof(g.export_csv_path));
                if (ImGui::Button("Export CSV")) {
                    std::string err = export_csv(g.export_csv_path);
                    g.status_msg = err.empty() ? "Exported." : err;
                }
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextUnformatted("Age-stratified parameter file:");
                static char age_path[512] = "clades_age.ini";
                ImGui::InputText("##agepath", age_path, sizeof(age_path));
                if (ImGui::Button("Load Age")) {
                    std::string err = load_age_params(g.age_params, age_path);
                    g.status_msg = err.empty() ? "Age params loaded." : err;
                }
                ImGui::SameLine();
                if (ImGui::Button("Save Age")) {
                    std::string err = save_age_params(g.age_params, age_path);
                    g.status_msg = err.empty() ? "Age params saved." : err;
                }
                ImGui::Spacing();
                ImGui::TextUnformatted("Multi-strain parameter file:");
                static char ms_path[512] = "clades_strains.ini";
                ImGui::InputText("##mspath", ms_path, sizeof(ms_path));
                if (ImGui::Button("Load Strains")) {
                    std::string err = load_ms_params(g.ms_params, ms_path);
                    g.status_msg = err.empty() ? "Strain params loaded." : err;
                }
                ImGui::SameLine();
                if (ImGui::Button("Save Strains")) {
                    std::string err = save_ms_params(g.ms_params, ms_path);
                    g.status_msg = err.empty() ? "Strain params saved." : err;
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Right pane: results + plots
        ImGui::BeginChild("##results_child", ImVec2(right_w, h), true);
        if (ImGui::BeginTabBar("##rtabs")) {
            if (ImGui::BeginTabItem("Plots")) {
                plot_panel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Summary")) {
                results_summary();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Age Plots")) {
                age_plot_panel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Age Summary")) {
                age_summary_panel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Strain Plots")) {
                ms_plot_panel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Strain Summary")) {
                ms_summary_panel();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();

        // Bottom bar: run / progress
        ImGui::Spacing();
        ImGui::Separator();

        bool sim_busy = g.running.load();
        if (sim_busy) {
            int r = g.progress_run.load() + 1;
            int d = g.progress_day.load();
            int td= g.total_days.load();
            float frac = (td > 0) ? (float)(r * td + d) / (float)(N_RUNS * td) : 0.0f;
            ImGui::ProgressBar(frac, ImVec2(200, 0));
            ImGui::SameLine();
            ImGui::Text("Run %d/%d  day %d", r, N_RUNS, d);
            ImGui::SameLine();
            if (ImGui::Button("Cancel##c")) {
                // bandaid — no proper cancellation yet, just marks flag
                g.running = false;
            }
        } else {
            if (ImGui::Button("  Run Simulation  ")) {
                g.running = true;
                g.status_msg = "Running...";
                std::thread(sim_thread_fn).detach();
            }
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(g.status_msg.c_str());

        ImGui::End();

        if (g.show_about) about_window();
        if (g.show_contact_matrix) contact_matrix_window();
        if (g.show_cross_mtx) cross_immunity_window();

        // render
        ImGui::Render();
        int W, H2;
        SDL_GetWindowSize(win, &W, &H2);
        glViewport(0, 0, W, H2);
        glClearColor(0.753f, 0.753f, 0.753f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(win);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
