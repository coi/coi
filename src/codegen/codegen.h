#pragma once

#include <string>
#include <vector>
#include <memory>
#include <set>

// Forward declarations
struct Component;
struct DataDef;
struct EnumDef;
struct AppConfig;
struct FeatureFlags;
struct CompilerSession;

// Generate C++ code from components
void generate_cpp_code(
    std::ostream &out,
    std::vector<Component> &all_components,
    const std::vector<std::unique_ptr<DataDef>> &all_global_data,
    const std::vector<std::unique_ptr<EnumDef>> &all_global_enums,
    const AppConfig &final_app_config,
    const std::set<std::string> &required_headers,
    const FeatureFlags &features);

