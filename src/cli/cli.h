#pragma once

#include <string>
#include <filesystem>

// ANSI color codes for terminal output
namespace colors {
    constexpr const char* RESET   = "\033[0m";
    constexpr const char* BOLD    = "\033[1m";
    constexpr const char* DIM     = "\033[2m";
    
    constexpr const char* RED     = "\033[31m";
    constexpr const char* GREEN   = "\033[32m";
    constexpr const char* YELLOW  = "\033[33m";
    constexpr const char* BLUE    = "\033[34m";
    constexpr const char* MAGENTA = "\033[35m";
    constexpr const char* CYAN    = "\033[36m";
    constexpr const char* WHITE   = "\033[37m";
    
    // Brand color (purple)
    constexpr const char* BRAND   = "\033[38;5;141m";  // Close to #9477ff
}

// Initialize a new Coi project
// Returns 0 on success, non-zero on error
int init_project(const std::string& project_name_arg);

// Build a Coi project in the current directory
// Returns 0 on success, non-zero on error
int build_project(bool keep_cc = false, bool cc_only = false, bool silent_banner = false);

// Build and start dev server
// Returns 0 on success, non-zero on error  
int dev_project(bool keep_cc = false, bool cc_only = false);

// Print help message
void print_help(const char* program_name);

// Get the directory where the coi executable is located
std::filesystem::path get_executable_dir();
