#pragma once

#include <string>
#include <vector>
#include <filesystem>

// Forward declarations
struct Component;

// Generate CSS file with component styles and external stylesheets
void generate_css_file(
    const std::filesystem::path &css_path,
    const std::filesystem::path &input_file,
    const std::vector<Component> &all_components);
