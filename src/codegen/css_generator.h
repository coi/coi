#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <ostream>

// Forward declarations
struct Component;

// Generate CSS file with component styles and external stylesheets
void generate_css_file(
    const std::filesystem::path &css_path,
    const std::filesystem::path &input_file,
    const std::vector<Component> &all_components);

// One component's CSS: `style global` as is, `style` scoped with [coi-scope="Module_Name"]
void write_component_css(std::ostream &css_out, const Component &comp);
