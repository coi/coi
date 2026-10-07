#pragma once

#include <filesystem>
#include <string>

struct AppConfig;

// Tags for <head>: favicon, theme-color, and with pwa the manifest link and the
// service worker registration
std::string pwa_head_tags(const AppConfig &config);

// manifest.webmanifest and sw.js in out_dir. Runs after the build, since the
// service worker precaches (and is versioned by) every file in out_dir.
void generate_pwa_files(const std::filesystem::path &out_dir, const AppConfig &config);
