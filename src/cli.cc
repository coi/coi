#include "cli.h"
#include "error.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <filesystem>
#include <unistd.h>
#include <limits.h>

namespace fs = std::filesystem;

using namespace colors;

// Fish logo ASCII art
static void print_logo() {
    std::cout << BRAND << "       ><(((º>" << RESET << std::endl;
}

static void print_banner(const char* cmd) {
    std::cout << std::endl;
    std::cout << "  " << BRAND << BOLD << "coi" << RESET;
    if (cmd) {
        std::cout << " " << DIM << cmd << RESET;
    }
    std::cout << std::endl;
}

// Get the directory where the coi executable is located
std::filesystem::path get_executable_dir() {
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        return fs::path(path).parent_path();
    }
    // Fallback: empty path
    return fs::path();
}

// Get the templates directory relative to the executable
static fs::path get_templates_dir() {
    fs::path exe_dir = get_executable_dir();
    if (exe_dir.empty()) {
        return fs::path();
    }
    
    // The coi binary is at repo root, templates/ is a sibling
    fs::path templates = exe_dir / "templates";
    if (fs::exists(templates)) {
        return templates;
    }
    
    return fs::path();
}

// Replace __PLACEHOLDER__ patterns in a string
static std::string replace_placeholders(const std::string& content, 
                                         const std::map<std::string, std::string>& vars) {
    std::string result = content;
    for (const auto& [key, value] : vars) {
        std::string placeholder = "__" + key + "__";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), value);
            pos += value.length();
        }
    }
    return result;
}

// Copy a template file with placeholder replacement
static bool copy_template_file(const fs::path& src, const fs::path& dest,
                                const std::map<std::string, std::string>& vars) {
    std::ifstream in(src);
    if (!in) {
        ErrorHandler::cli_error("Cannot read template file: " + src.string());
        return false;
    }
    
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string content = replace_placeholders(buffer.str(), vars);
    
    std::ofstream out(dest);
    if (!out) {
        ErrorHandler::cli_error("Cannot write file: " + dest.string());
        return false;
    }
    out << content;
    return true;
}

// Validate project name (alphanumeric, hyphens, underscores)
static bool is_valid_project_name(const std::string& name) {
    if (name.empty()) return false;
    for (char c : name) {
        if (!std::isalnum(c) && c != '-' && c != '_') {
            return false;
        }
    }
    // Must start with letter or underscore
    return std::isalpha(name[0]) || name[0] == '_';
}

int init_project(const std::string& project_name_arg) {
    fs::path templates_dir = get_templates_dir();
    if (templates_dir.empty() || !fs::exists(templates_dir)) {
        ErrorHandler::cli_error("Could not find templates directory.",
                               "Make sure you're running the coi binary from the repository.");
        return 1;
    }
    
    std::string project_name = project_name_arg;
    
    // Helper to read input with default
    auto prompt = [](const std::string& msg, const std::string& default_val) -> std::string {
        if (!default_val.empty()) {
            std::cout << msg << " " << DIM << "(" << default_val << ")" << RESET << ": ";
        } else {
            std::cout << msg << ": ";
        }
        std::string input;
        std::getline(std::cin, input);
        // Trim whitespace
        input.erase(0, input.find_first_not_of(" \t\n\r"));
        if (!input.empty()) {
            input.erase(input.find_last_not_of(" \t\n\r") + 1);
        }
        return input.empty() ? default_val : input;
    };
    
    print_banner("init");
    
    // If no name provided, prompt for it
    if (project_name.empty()) {
        project_name = prompt("  Project name", "");
    }
    
    if (!is_valid_project_name(project_name)) {
        std::cerr << std::endl;
        ErrorHandler::cli_error("Invalid project name '" + project_name + "'",
                               "Project name must start with a letter or underscore, and contain only\nletters, numbers, hyphens, and underscores.");
        return 1;
    }
    
    fs::path project_dir = fs::current_path() / project_name;
    
    if (fs::exists(project_dir)) {
        ErrorHandler::cli_error("Directory '" + project_name + "' already exists.");
        return 1;
    }
    
    // Placeholder variables
    std::map<std::string, std::string> vars = {
        {"PROJECT_NAME", project_name}
    };
    
    // Copy entire templates directory recursively
    for (const auto& entry : fs::recursive_directory_iterator(templates_dir)) {
        fs::path rel_path = fs::relative(entry.path(), templates_dir);
        fs::path dest_path = project_dir / rel_path;
        
        if (entry.is_directory()) {
            fs::create_directories(dest_path);
        } else if (entry.is_regular_file()) {
            // Create parent directories if needed
            fs::create_directories(dest_path.parent_path());
            
            // Check if file needs placeholder replacement
            std::string ext = entry.path().extension().string();
            if (ext == ".coi" || ext == ".md" || ext == ".sh") {
                if (!copy_template_file(entry.path(), dest_path, vars)) {
                    return 1;
                }
            } else {
                // Binary/other files: copy as-is
                fs::copy_file(entry.path(), dest_path, fs::copy_options::overwrite_existing);
            }
        }
    }
    
    std::cout << "  " << GREEN << "✓" << RESET << " Created " << BOLD << project_name << "/" << RESET << std::endl;
    std::cout << std::endl;
    std::cout << "  " << DIM << "Next steps:" << RESET << std::endl;
    std::cout << "    " << CYAN << "cd " << project_name << RESET << std::endl;
    std::cout << "    " << CYAN << "coi dev" << RESET << std::endl;
    std::cout << std::endl;
    
    return 0;
}

// Find entry point (src/App.coi) in current directory
static fs::path find_entry_point() {
    fs::path entry = fs::current_path() / "src" / "App.coi";
    if (fs::exists(entry)) {
        return entry;
    }
    return fs::path();
}

int build_project(bool keep_cc, bool cc_only, bool silent_banner) {
    if (!silent_banner) {
        print_banner("build");
    }
    
    fs::path entry = find_entry_point();
    if (entry.empty()) {
        ErrorHandler::cli_error("No src/App.coi found in current directory.",
                               "Make sure you're in a Coi project directory.");
        return 1;
    }
    
    fs::path project_dir = fs::current_path();
    fs::path dist_dir = project_dir / "dist";
    
    // Create dist directory
    fs::create_directories(dist_dir);
    
    // Copy assets folder if it exists
    fs::path assets_dir = project_dir / "assets";
    if (fs::exists(assets_dir) && fs::is_directory(assets_dir)) {
        std::cout << DIM << "Copying assets..." << RESET << std::endl;
        fs::path dest_assets = dist_dir / "assets";
        for (const auto& asset : fs::recursive_directory_iterator(assets_dir)) {
            fs::path rel = fs::relative(asset.path(), assets_dir);
            fs::path dest = dest_assets / rel;
            if (asset.is_directory()) {
                fs::create_directories(dest);
            } else if (asset.is_regular_file()) {
                fs::create_directories(dest.parent_path());
                fs::copy_file(asset.path(), dest, fs::copy_options::overwrite_existing);
            }
        }
    }
    
    // Get executable directory to find coi binary path
    fs::path exe_dir = get_executable_dir();
    fs::path coi_bin = exe_dir / "coi";
    
    // Build command - use bash pipefail to preserve coi's exit code through the pipe
    std::string extra_flags;
    if (keep_cc) extra_flags += " --keep-cc";
    if (cc_only) extra_flags += " --cc-only";
    std::string cmd = "bash -c 'set -o pipefail; " + coi_bin.string() + " " + entry.string() + " --out " + dist_dir.string() + extra_flags + " 2>&1 | grep -v \"Success! Run\"'";
    
    std::cout << BRAND << "▶" << RESET << " Building..." << std::endl;
    int ret = system(cmd.c_str());
    
    if (ret != 0) {
        ErrorHandler::build_failed();
        return 1;
    }
    
    std::cout << GREEN << "✓" << RESET << " Built to " << BOLD << "dist/" << RESET << std::endl;
    return 0;
}

int dev_project(bool keep_cc, bool cc_only) {
    print_banner("dev");
    
    // First build (silent banner since dev already showed one)
    int ret = build_project(keep_cc, cc_only, true);
    if (ret != 0) {
        return ret;
    }
    
    fs::path dist_dir = fs::current_path() / "dist";
    
    std::cout << "  " << GREEN << "➜" << RESET << "  Local:   " << CYAN << BOLD << "http://localhost:8000" << RESET << std::endl;
    std::cout << "  " << DIM << "Press Ctrl+C to stop" << RESET << std::endl;
    std::cout << std::endl;
    
    // Start server (suppress startup message, keep request logs)
    std::string cmd = "cd " + dist_dir.string() + " && python3 -m http.server 8000 2>&1 | grep -v 'Serving HTTP'";
    return system(cmd.c_str());
}

void print_help(const char* program_name) {
    std::cout << std::endl;
    print_logo();
    std::cout << std::endl;
    std::cout << "  " << BRAND << BOLD << "Coi" << RESET << " " << DIM << "- WebAssembly for the Modern Web" << RESET << std::endl;
    std::cout << std::endl;
    std::cout << "  " << BOLD << "Usage:" << RESET << std::endl;
    std::cout << "    " << CYAN << program_name << " init" << RESET << " [name]              Create a new project" << std::endl;
    std::cout << "    " << CYAN << program_name << " build" << RESET << "                    Build the project" << std::endl;
    std::cout << "    " << CYAN << program_name << " dev" << RESET << "                      Build and start dev server" << std::endl;
    std::cout << "    " << CYAN << program_name << RESET << " <file.coi> [options]    Compile a .coi file" << std::endl;
    std::cout << std::endl;
    std::cout << "  " << BOLD << "Options:" << RESET << std::endl;
    std::cout << "    " << DIM << "--out, -o <dir>" << RESET << "    Output directory" << std::endl;
    std::cout << "    " << DIM << "--cc-only" << RESET << "         Generate C++ only, skip WASM" << std::endl;
    std::cout << "    " << DIM << "--keep-cc" << RESET << "         Keep generated C++ files" << std::endl;
    std::cout << std::endl;
    std::cout << "  " << BOLD << "Examples:" << RESET << std::endl;
    std::cout << "    " << DIM << "$" << RESET << " coi init my-app" << std::endl;
    std::cout << "    " << DIM << "$" << RESET << " cd my-app && coi dev" << std::endl;
    std::cout << std::endl;
}
