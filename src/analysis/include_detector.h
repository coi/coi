#pragma once

#include <string>
#include <set>
#include <vector>
#include <memory>

// Forward declarations
struct Component;
struct FunctionDef;

// Build type-to-header mapping from DefSchema
std::set<std::string> get_required_headers(const std::vector<Component> &components,
                                           const std::vector<std::unique_ptr<FunctionDef>> &functions = {});
