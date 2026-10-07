#pragma once

#include <filesystem>
#include <string>

struct AppConfig;

// <head> tags: favicon, theme-color, pwa links
std::string pwa_head_tags(const AppConfig &config);

// writes manifest.webmanifest and sw.js, run after the build
void generate_pwa_files(const std::filesystem::path &out_dir, const AppConfig &config);
