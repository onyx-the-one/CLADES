#pragma once
#include "model.hpp"
#include <string>

// Returns "" on success, error string on failure
std::string save_params(const Params& p, const std::string& path);
std::string load_params(Params& p, const std::string& path);
