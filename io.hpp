#pragma once
#include "model.hpp"
#include <string>

// Returns "" on success, error string on failure
std::string save_params(const Params& p, const std::string& path);
std::string load_params(Params& p, const std::string& path);

struct AgeParams;
std::string save_age_params(const AgeParams& ap, const std::string& path);
std::string load_age_params(AgeParams& ap, const std::string& path);

struct MultiStrainParams;
std::string save_ms_params(const MultiStrainParams& mp, const std::string& path);
std::string load_ms_params(MultiStrainParams& mp, const std::string& path);
