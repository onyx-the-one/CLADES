// CLADES — Compartmental Lightweight Agent-based Disease Epidemiology Simulator
// Frontend: Dear ImGui + SDL2 + OpenGL3
// Retro "grey box" styling deliberately imitates early-2000s scientific GUIs.

#include "model.hpp"
#include "io.hpp"

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
};

static AppState g;

// ---- simulation thread ------------------------------------------------------

static void sim_thread_fn()
{
    Params p;
    { // snapshot under lock isn't needed (params are UI-side only) but let's be clear
        p = g.params;
    }
    g.progress_run = 0;
    g.total_days = p.T_days;

    auto cb = [](int ri, int rn, int day, int tdays) {
        g.progress_run = ri;
        g.progress_day = day;
        (void)rn; (void)tdays;
    };

    SimResult sr = run_ensemble(p, cb);

    {
        std::lock_guard<std::mutex> lk(g.result_mtx);
        g.result     = std::move(sr);
        g.has_result = true;
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

        float left_w  = 340;
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
            if (ImGui::BeginTabItem("Files")) {
                ImGui::TextUnformatted("Load/Save parameter file:");
                ImGui::InputText("##lpath", g.load_path, sizeof(g.load_path));
                if (ImGui::Button("Load")) {
                    std::string err = load_params(g.params, g.load_path);
                    g.status_msg = err.empty() ? "Loaded." : err;
                }
                ImGui::SameLine();
                strncpy(g.save_path, g.load_path, sizeof(g.save_path) - 1);
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
