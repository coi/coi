#include "cli.h"
#include "error.h"
#include "version.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <filesystem>
#include <unistd.h>
#include <limits.h>
#include <chrono>
#include <iomanip>
#include <array>
#include <regex>
#include <sys/wait.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

using namespace colors;

// Fish logo ASCII art
static void print_logo()
{
    std::cout << BRAND << "       ><(((º>" << RESET << std::endl;
}

static void print_banner(const char *cmd)
{
    std::cout << std::endl;
    std::cout << "  " << BRAND << BOLD << "coi" << RESET;
    if (cmd)
    {
        std::cout << " " << DIM << cmd << RESET;
    }
    std::cout << std::endl;
}

static std::string get_pond_name(int pond_number)
{
    if (pond_number < 0)
    {
        pond_number = 0;
    }
    return "Filling Pond " + std::to_string(pond_number);
}

static int get_current_drop_from_macros()
{
    int total_count = 0;
    int pond_start_commit_count = 0;
    try
    {
        total_count = std::stoi(GIT_COMMIT_COUNT);
        pond_start_commit_count = std::stoi(COI_POND_START_COMMIT_COUNT);
    }
    catch (...)
    {
        return 0;
    }

    int pond_drop = total_count - pond_start_commit_count;
    return pond_drop < 0 ? 0 : pond_drop;
}

static bool exec_command_capture(const std::string &cmd, std::string &output)
{
    std::array<char, 256> buffer;
    output.clear();

    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe)
    {
        return false;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
    {
        output += buffer.data();
    }

    int status = pclose(pipe);
    if (status == -1)
    {
        return false;
    }

    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int self_upgrade()
{
    print_banner("self-upgrade");

    fs::path repo_root = get_executable_dir();
    if (repo_root.empty())
    {
        ErrorHandler::cli_error("Could not determine Coi installation directory.");
        return 1;
    }

    fs::path git_dir = repo_root / ".git";
    fs::path build_script = repo_root / "build.sh";
    fs::path coi_binary = repo_root / "coi";

    if (!fs::exists(git_dir) || !fs::exists(build_script) || !fs::exists(coi_binary))
    {
        ErrorHandler::cli_error("self-upgrade requires a git checkout of the Coi repository.",
                                "Expected .git, build.sh, and coi binary in: " + repo_root.string());
        return 1;
    }

    int before_pond = 0;
    try
    {
        before_pond = std::stoi(COI_POND_NUMBER);
    }
    catch (...)
    {
        before_pond = 0;
    }
    int before_drop = get_current_drop_from_macros();
    std::string before_hash = GIT_COMMIT_HASH;

    fs::path previous_cwd = fs::current_path();
    fs::current_path(repo_root);

    std::cout << "  " << DIM << "Pulling latest changes..." << RESET << std::endl;
    int pull_ret = system("git pull --ff-only");
    if (pull_ret != 0)
    {
        fs::current_path(previous_cwd);
        ErrorHandler::cli_error("git pull failed.", "Resolve git issues and try again.");
        return 1;
    }

    std::cout << "  " << DIM << "Rebuilding compiler..." << RESET << std::endl;
    int build_ret = system("./build.sh");
    fs::current_path(previous_cwd);

    if (build_ret != 0)
    {
        ErrorHandler::cli_error("build.sh failed.", "Fix build issues, then run coi self-upgrade again.");
        return 1;
    }

    std::string version_output;
    std::string version_cmd = "\"" + coi_binary.string() + "\" --version";
    int after_pond = before_pond;
    int after_drop = before_drop;
    std::string after_hash = before_hash;

    if (exec_command_capture(version_cmd, version_output))
    {
        std::smatch m;
        std::regex pond_re("Filling Pond ([0-9]+)");
        std::regex drop_re("Drop ([0-9]+)");

        if (std::regex_search(version_output, m, pond_re) && m.size() > 1)
        {
            after_pond = std::stoi(m[1].str());
        }
        if (std::regex_search(version_output, m, drop_re) && m.size() > 1)
        {
            after_drop = std::stoi(m[1].str());
        }
    }

    std::string git_hash_output;
    std::string git_hash_cmd = "cd \"" + repo_root.string() + "\" && git rev-parse --short HEAD";
    if (exec_command_capture(git_hash_cmd, git_hash_output))
    {
        while (!git_hash_output.empty() &&
               (git_hash_output.back() == '\n' || git_hash_output.back() == '\r' || git_hash_output.back() == ' ' || git_hash_output.back() == '\t'))
        {
            git_hash_output.pop_back();
        }
        if (!git_hash_output.empty())
        {
            after_hash = git_hash_output;
        }
    }

    std::cout << std::endl;
    if (before_pond == after_pond && before_drop == after_drop)
    {
        std::cout << "  " << GREEN << "✓" << RESET << " Coi is already up to date" << std::endl;
    }
    else
    {
        std::cout << "  " << GREEN << "✓" << RESET << " Upgraded Coi" << std::endl;
    }
    std::cout << "  " << CYAN << "From Pond " << before_pond << " · Drop " << before_drop
              << " (" << before_hash << ")" << RESET << std::endl;
    std::cout << "  " << CYAN << "To   Pond " << after_pond << " · Drop " << after_drop
              << " (" << after_hash << ")" << RESET << std::endl;
    std::cout << std::endl;

    return 0;
}

// Get the directory where the coi executable is located
std::filesystem::path get_executable_dir()
{
    char path[PATH_MAX];
    
#ifdef __APPLE__
    // macOS: use _NSGetExecutablePath
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0)
    {
        char real_path[PATH_MAX];
        if (realpath(path, real_path) != nullptr)
        {
            return fs::path(real_path).parent_path();
        }
        return fs::path(path).parent_path();
    }
#else
    // Linux: use /proc/self/exe
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1)
    {
        path[len] = '\0';
        return fs::path(path).parent_path();
    }
#endif

    return fs::path();
}

// Get the template directory relative to the executable
static fs::path get_template_dir(TemplateType template_type)
{
    fs::path exe_dir = get_executable_dir();
    if (exe_dir.empty())
    {
        return fs::path();
    }

    // Determine which template to use
    std::string template_name;
    switch (template_type)
    {
    case TemplateType::Pkg:
        template_name = "pkg";
        break;
    case TemplateType::App:
    default:
        template_name = "app";
        break;
    }

    // The coi binary is at repo root, templates/ is a sibling
    fs::path tpl_dir = exe_dir / "templates" / template_name;
    if (fs::exists(tpl_dir))
    {
        return tpl_dir;
    }

    return fs::path();
}

// Replace __PLACEHOLDER__ patterns in a string
static std::string replace_placeholders(const std::string &content,
                                        const std::map<std::string, std::string> &vars)
{
    std::string result = content;
    for (const auto &[key, value] : vars)
    {
        std::string placeholder = "__" + key + "__";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos)
        {
            result.replace(pos, placeholder.length(), value);
            pos += value.length();
        }
    }
    return result;
}

// Copy a template file with placeholder replacement
static bool copy_template_file(const fs::path &src, const fs::path &dest,
                               const std::map<std::string, std::string> &vars)
{
    std::ifstream in(src);
    if (!in)
    {
        ErrorHandler::cli_error("Cannot read template file: " + src.string());
        return false;
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string content = replace_placeholders(buffer.str(), vars);

    std::ofstream out(dest);
    if (!out)
    {
        ErrorHandler::cli_error("Cannot write file: " + dest.string());
        return false;
    }
    out << content;
    return true;
}

// Convert project name to PascalCase for module name (my-lib -> MyLib)
static std::string to_pascal_case(const std::string &name)
{
    std::string result;
    bool capitalize_next = true;
    for (char c : name)
    {
        if (c == '-' || c == '_')
        {
            capitalize_next = true;
        }
        else if (capitalize_next)
        {
            result += std::toupper(c);
            capitalize_next = false;
        }
        else
        {
            result += c;
        }
    }
    return result;
}

// Validate project name (alphanumeric, hyphens, underscores)
static bool is_valid_project_name(const std::string &name)
{
    if (name.empty())
        return false;
    for (char c : name)
    {
        if (!std::isalnum(c) && c != '-' && c != '_')
        {
            return false;
        }
    }
    // Must start with letter or underscore
    return std::isalpha(name[0]) || name[0] == '_';
}

int init_project(const std::string &project_name_arg, TemplateType template_type)
{
    fs::path tpl_dir = get_template_dir(template_type);
    if (tpl_dir.empty() || !fs::exists(tpl_dir))
    {
        ErrorHandler::cli_error("Could not find template directory.",
                                "Make sure you're running the coi binary from the repository.");
        return 1;
    }

    std::string project_name = project_name_arg;

    // Helper to read input with default
    auto prompt = [](const std::string &msg, const std::string &default_val) -> std::string
    {
        if (!default_val.empty())
        {
            std::cout << msg << " " << DIM << "(" << default_val << ")" << RESET << ": ";
        }
        else
        {
            std::cout << msg << ": ";
        }
        std::string input;
        std::getline(std::cin, input);
        // Trim whitespace
        input.erase(0, input.find_first_not_of(" \t\n\r"));
        if (!input.empty())
        {
            input.erase(input.find_last_not_of(" \t\n\r") + 1);
        }
        return input.empty() ? default_val : input;
    };

    // Determine banner suffix based on template type
    std::string banner_suffix = "init";
    if (template_type == TemplateType::Pkg)
    {
        banner_suffix = "init --pkg";
    }
    print_banner(banner_suffix.c_str());

    // If no name provided, prompt for it
    if (project_name.empty())
    {
        project_name = prompt("  Project name", "");
    }

    if (!is_valid_project_name(project_name))
    {
        std::cerr << std::endl;
        ErrorHandler::cli_error("Invalid project name '" + project_name + "'",
                                "Project name must start with a letter or underscore, and contain only\nletters, numbers, hyphens, and underscores.");
        return 1;
    }

    fs::path project_dir = fs::current_path() / project_name;

    if (fs::exists(project_dir))
    {
        ErrorHandler::cli_error("Directory '" + project_name + "' already exists.");
        return 1;
    }

    // Placeholder variables
    std::string module_name = to_pascal_case(project_name);
    int current_pond = 0;
    try
    {
        current_pond = std::stoi(COI_POND_NUMBER);
    }
    catch (...)
    {
        current_pond = 0;
    }
    int current_drop = get_current_drop_from_macros();
    
    // Get today's date as YYYY-MM-DD
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now = *std::localtime(&time_t_now);
    std::ostringstream date_stream;
    date_stream << std::put_time(&tm_now, "%Y-%m-%d");
    std::string today_date = date_stream.str();
    
    std::map<std::string, std::string> vars = {
        {"PROJECT_NAME", project_name},
        {"MODULE_NAME", module_name},
        {"COI_POND", std::to_string(current_pond)},
        {"COI_MIN_DROP", std::to_string(current_drop)},
        {"COI_DROP", GIT_COMMIT_COUNT},
        {"TODAY_DATE", today_date}};

    // Copy entire template directory recursively
    for (const auto &entry : fs::recursive_directory_iterator(tpl_dir))
    {
        fs::path rel_path = fs::relative(entry.path(), tpl_dir);
        fs::path dest_path = project_dir / rel_path;

        if (entry.is_directory())
        {
            fs::create_directories(dest_path);
        }
        else if (entry.is_regular_file())
        {
            // Create parent directories if needed
            fs::create_directories(dest_path.parent_path());

            // Check if file needs placeholder replacement
            std::string ext = entry.path().extension().string();
            if (ext == ".coi" || ext == ".md" || ext == ".sh" || ext == ".json")
            {
                if (!copy_template_file(entry.path(), dest_path, vars))
                {
                    return 1;
                }
            }
            else
            {
                // Binary/other files: copy as-is
                fs::copy_file(entry.path(), dest_path, fs::copy_options::overwrite_existing);
            }
        }
    }

    std::cout << "  " << GREEN << "✓" << RESET << " Created " << BOLD << project_name << "/" << RESET << std::endl;
    std::cout << std::endl;
    std::cout << "  " << DIM << "Next steps:" << RESET << std::endl;
    std::cout << "    " << CYAN << "cd " << project_name << RESET << std::endl;
    if (template_type == TemplateType::App)
    {
        std::cout << "    " << CYAN << "coi dev" << RESET << std::endl;
    }
    else
    {
        std::cout << "    " << DIM << "# Import this package into an app project" << RESET << std::endl;
    }
    std::cout << std::endl;

    return 0;
}

// Find entry point (src/App.coi) in current directory
static fs::path find_entry_point()
{
    fs::path entry = fs::current_path() / "src" / "App.coi";
    if (fs::exists(entry))
    {
        return entry;
    }
    return fs::path();
}

int build_project(bool keep_cc, bool cc_only, bool silent_banner)
{
    if (!silent_banner)
    {
        print_banner("build");
    }

    fs::path entry = find_entry_point();
    if (entry.empty())
    {
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
    if (fs::exists(assets_dir) && fs::is_directory(assets_dir))
    {
        std::cout << DIM << "Copying assets..." << RESET << std::endl;
        fs::path dest_assets = dist_dir / "assets";
        for (const auto &asset : fs::recursive_directory_iterator(assets_dir))
        {
            fs::path rel = fs::relative(asset.path(), assets_dir);
            fs::path dest = dest_assets / rel;
            if (asset.is_directory())
            {
                fs::create_directories(dest);
            }
            else if (asset.is_regular_file())
            {
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
    if (keep_cc)
        extra_flags += " --keep-cc";
    if (cc_only)
        extra_flags += " --cc-only";
    std::string cmd = "bash -c 'set -o pipefail; " + coi_bin.string() + " " + entry.string() + " --out " + dist_dir.string() + extra_flags + " 2>&1 | grep -v \"Success! Run\"'";

    std::cout << BRAND << "▶" << RESET << " Building..." << std::endl;
    int ret = system(cmd.c_str());

    if (ret != 0)
    {
        ErrorHandler::build_failed();
        return 1;
    }

    std::cout << GREEN << "✓" << RESET << " Built to " << BOLD << "dist/" << RESET << std::endl;
    return 0;
}

int dev_project(bool keep_cc, bool cc_only, bool hot_reloading)
{
    print_banner("dev");

    // First build (silent banner since dev already showed one)
    int ret = build_project(keep_cc, cc_only, true);
    if (ret != 0)
    {
        return ret;
    }

    fs::path project_dir = fs::current_path();
    fs::path dist_dir = project_dir / "dist";
    fs::path exe_dir = get_executable_dir();
    fs::path coi_bin = exe_dir / "coi";
    fs::path dev_script = exe_dir / "scripts" / "dev_server.py";

    std::cout << "  " << GREEN << "➜" << RESET << "  Local:   " << CYAN << BOLD << "http://localhost:8000" << RESET << std::endl;
    if (!hot_reloading)
    {
        std::cout << "  " << DIM << "↻ Hot reload: disabled" << RESET << std::endl;
    }
    std::cout << "  " << DIM << "Press Ctrl+C to stop" << RESET << std::endl;
    std::cout << std::endl;

    if (!fs::exists(dev_script))
    {
        ErrorHandler::cli_error("Dev server script not found.",
                                "Expected at: " + dev_script.string());
        return 1;
    }

    std::string cmd = "python3 " + dev_script.string() +
                     " " + project_dir.string() +
                     " " + coi_bin.string() +
                     " " + dist_dir.string();
    
    if (!hot_reloading) cmd += " --no-watch";
    if (keep_cc) cmd += " --keep-cc";
    if (cc_only) cmd += " --cc-only";

    return system(cmd.c_str());
}

void print_version()
{
    int total_count = 0;
    int pond_number = 0;
    int pond_start_commit_count = 0;
    try
    {
        total_count = std::stoi(GIT_COMMIT_COUNT);
        pond_number = std::stoi(COI_POND_NUMBER);
        pond_start_commit_count = std::stoi(COI_POND_START_COMMIT_COUNT);
    }
    catch (...)
    {
        total_count = 0;
        pond_number = 0;
        pond_start_commit_count = 0;
    }
    int pond_drop = total_count - pond_start_commit_count;
    if (pond_drop < 0)
    {
        pond_drop = 0;
    }

    std::cout << std::endl;
    std::cout << BRAND << "              .  o  ..          " << RESET << std::endl;
    std::cout << BRAND << "          o  .    '   .  o      " << RESET << std::endl;
    std::cout << BRAND << "       o     ><(((º>    '       " << RESET << DIM << "just keep compiling." << RESET << std::endl;
    std::cout << BRAND << "          .    o   '  .         " << RESET << std::endl;
    std::cout << BRAND << "              '  .    o         " << RESET << std::endl;
    std::cout << std::endl;
    std::cout << "  " << BRAND << BOLD << "Coi Compiler" << RESET
              << " " << DIM << "·" << RESET << " " << CYAN << get_pond_name(pond_number) << RESET
              << " "  << "(" << CYAN << "Drop " << pond_drop << " · " << GIT_COMMIT_HASH << RESET << ")" << RESET << std::endl;
    std::cout << "  " << DIM << "Source Date: " << GIT_COMMIT_DATE << RESET << std::endl;
    std::cout << std::endl;
}

void print_help(const char *program_name)
{
    std::cout << std::endl;
    print_logo();
    std::cout << std::endl;
    std::cout << "  " << BRAND << BOLD << "Coi" << RESET << " " << DIM << "- WebAssembly for the Modern Web" << RESET << std::endl;
    std::cout << std::endl;
    std::cout << "  " << BOLD << "Usage:" << RESET << std::endl;
    std::cout << "    " << CYAN << program_name << " init" << RESET << " [name] [--pkg]      Create a new project" << std::endl;
    std::cout << "    " << CYAN << program_name << " build" << RESET << "                    Build the project" << std::endl;
    std::cout << "    " << CYAN << program_name << " dev" << RESET << " [--no-watch]         Build and start dev server" << std::endl;
    std::cout << "    " << CYAN << program_name << " add" << RESET << " <package>            Add a package from registry (scope/name)" << std::endl;
    std::cout << "    " << CYAN << program_name << " install" << RESET << "                  Install packages from coi.lock" << std::endl;
    std::cout << "    " << CYAN << program_name << " remove" << RESET << " <package>         Remove a package" << std::endl;
    std::cout << "    " << CYAN << program_name << " upgrade" << RESET << " [package]        Upgrade package(s)" << std::endl;
    std::cout << "    " << CYAN << program_name << " self-upgrade" << RESET << "              Pull and rebuild Coi" << std::endl;
    std::cout << "    " << CYAN << program_name << " list" << RESET << "                     List installed packages" << std::endl;
    std::cout << "    " << CYAN << program_name << " version" << RESET << "                  Show version" << std::endl;
    std::cout << "    " << CYAN << program_name << RESET << " <file.coi> [options]    Compile a .coi file" << std::endl;
    std::cout << std::endl;
    std::cout << "  " << BOLD << "Options:" << RESET << std::endl;
    std::cout << "    " << DIM << "--out, -o <dir>" << RESET << "   Output directory" << std::endl;
    std::cout << "    " << DIM << "--cc-only" << RESET << "         Generate C++ only, skip WASM" << std::endl;
    std::cout << "    " << DIM << "--keep-cc" << RESET << "         Keep generated C++ files" << std::endl;
    std::cout << "    " << DIM << "--no-watch" << RESET << "        Disable hot reloading (dev only)" << std::endl;
    std::cout << "    " << DIM << "--pkg" << RESET << "             Create a package (init only)" << std::endl;
    std::cout << std::endl;
    std::cout << "  " << BOLD << "Examples:" << RESET << std::endl;
    std::cout << "    " << DIM << "$" << RESET << " coi init my-app" << std::endl;
    std::cout << "    " << DIM << "$" << RESET << " cd my-app && coi dev" << std::endl;
    std::cout << "    " << DIM << "$" << RESET << " coi add supabase" << std::endl;
    std::cout << "    " << DIM << "$" << RESET << " coi add @google/package" << std::endl;
    std::cout << "    " << DIM << "$" << RESET << " coi self-upgrade" << std::endl;
    std::cout << std::endl;
}
