#pragma once

#include <string>
#include <set>
#include <vector>

// Forward declarations
struct Component;

// Build type-to-header mapping from DefSchema
std::set<std::string> get_required_headers(const std::vector<Component> &components);
