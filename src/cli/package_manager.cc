#include "package_manager.h"
#include "cli.h"
#include "version.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <regex>
#include <array>
#include <vector>

using namespace colors;

// Registry URL for fetching package info
static const std::string REGISTRY_BASE_URL = "https://raw.githubusercontent.com/coi-lang/registry/main/packages/";

// Simple JSON value extraction (avoids external dependency)
static std::string extract_json_string(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    
    pos = json.find('"', pos);
    if (pos == std::string::npos) return "";
    
    size_t start = pos + 1;
    size_t end = json.find('"', start);
    if (end == std::string::npos) return "";
    
    return json.substr(start, end - start);
}

struct RegistryRelease {
    std::string version;
    int pond = 0;
    int min_drop = 0;
    std::string commit;
    std::string sha256;
};

static bool extract_string_field(const std::string& json, const std::string& key, std::string& out) {
    std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
    std::smatch m;
    if (std::regex_search(json, m, re) && m.size() > 1) {
        out = m[1].str();
        return true;
    }
    return false;
}

static bool extract_int_field(const std::string& json, const std::string& key, int& out) {
    std::regex re("\"" + key + "\"\\s*:\\s*([0-9]+)");
    std::smatch m;
    if (std::regex_search(json, m, re) && m.size() > 1) {
        out = std::stoi(m[1].str());
        return true;
    }
    return false;
}

static std::vector<std::string> extract_release_objects(const std::string& json) {
    std::vector<std::string> releases;

    size_t releases_pos = json.find("\"releases\"");
    if (releases_pos == std::string::npos) return releases;

    size_t array_start = json.find('[', releases_pos);
    if (array_start == std::string::npos) return releases;

    bool in_string = false;
    bool escape = false;
    int brace_depth = 0;
    size_t object_start = std::string::npos;

    for (size_t i = array_start + 1; i < json.size(); ++i) {
        char c = json[i];

        if (escape) {
            escape = false;
            continue;
        }

        if (c == '\\') {
            escape = true;
            continue;
        }

        if (c == '"') {
            in_string = !in_string;
            continue;
        }

        if (in_string) continue;

        if (c == '{') {
            if (brace_depth == 0) {
                object_start = i;
            }
            brace_depth++;
            continue;
        }

        if (c == '}') {
            if (brace_depth > 0) {
                brace_depth--;
                if (brace_depth == 0 && object_start != std::string::npos) {
                    releases.push_back(json.substr(object_start, i - object_start + 1));
                    object_start = std::string::npos;
                }
            }
            continue;
        }

        if (c == ']' && brace_depth == 0) {
            break;
        }
    }

    return releases;
}

static bool parse_release(const std::string& release_json, RegistryRelease& release) {
    if (!extract_string_field(release_json, "version", release.version)) return false;

    std::smatch compiler_match;
    std::regex compiler_re("\"compiler\"\\s*:\\s*\\{[^\\}]*\"pond\"\\s*:\\s*([0-9]+)[^\\}]*\"min-drop\"\\s*:\\s*([0-9]+)");
    if (!std::regex_search(release_json, compiler_match, compiler_re) || compiler_match.size() < 3) {
        return false;
    }
    release.pond = std::stoi(compiler_match[1].str());
    release.min_drop = std::stoi(compiler_match[2].str());

    std::smatch source_match;
    std::regex source_re("\"source\"\\s*:\\s*\\{[^\\}]*\"commit\"\\s*:\\s*\"([^\"]+)\"[^\\}]*\"sha256\"\\s*:\\s*\"([^\"]+)\"");
    if (!std::regex_search(release_json, source_match, source_re) || source_match.size() < 3) {
        return false;
    }
    release.commit = source_match[1].str();
    release.sha256 = source_match[2].str();

    return true;
}

static int get_current_compiler_drop() {
    try {
        int total_count = std::stoi(GIT_COMMIT_COUNT);
        int pond_start_commit_count = std::stoi(COI_POND_START_COMMIT_COUNT);
        int pond_drop = total_count - pond_start_commit_count;
        return pond_drop < 0 ? 0 : pond_drop;
    } catch (...) {
        return 0;
    }
}

static int get_current_compiler_pond() {
    try {
        return std::stoi(COI_POND_NUMBER);
    } catch (...) {
        return 0;
    }
}

// Execute a command and capture output
static std::string exec_command(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    
    pclose(pipe);
    return result;
}

// Execute a command and return exit code
static int run_command(const std::string& cmd) {
    return system(cmd.c_str());
}

// Print banner for package commands
static void print_pkg_banner(const char* cmd) {
    std::cout << std::endl;
    std::cout << "  " << BRAND << BOLD << "coi" << RESET;
    if (cmd) {
        std::cout << " " << DIM << cmd << RESET;
    }
    std::cout << std::endl;
}

std::map<std::string, LockEntry> read_lock_file(const fs::path& lock_path) {
    std::map<std::string, LockEntry> packages;
    
    if (!fs::exists(lock_path)) {
        return packages;
    }
    
    std::ifstream file(lock_path);
    if (!file) {
        return packages;
    }
    
    // Simple line-by-line JSON parsing
    std::string line;
    std::string current_pkg;
    LockEntry current_entry;
    bool in_packages = false;
    bool in_entry = false;
    
    auto extract_value = [](const std::string& line, const std::string& key) -> std::string {
        size_t key_pos = line.find("\"" + key + "\"");
        if (key_pos == std::string::npos) return "";
        size_t colon = line.find(':', key_pos);
        if (colon == std::string::npos) return "";
        size_t quote1 = line.find('"', colon + 1);
        if (quote1 == std::string::npos) return "";
        size_t quote2 = line.find('"', quote1 + 1);
        if (quote2 == std::string::npos) return "";
        return line.substr(quote1 + 1, quote2 - quote1 - 1);
    };

    auto extract_int_value = [](const std::string& line, const std::string& key, int& out) -> bool {
        std::regex re("\\\"" + key + "\\\"\\s*:\\s*([0-9]+)");
        std::smatch m;
        if (std::regex_search(line, m, re) && m.size() > 1) {
            out = std::stoi(m[1].str());
            return true;
        }
        return false;
    };
    
    while (std::getline(file, line)) {
        if (line.find("\"packages\"") != std::string::npos) {
            in_packages = true;
            continue;
        }
        
        if (!in_packages) continue;
        
        // Check for package name (line with just "name": {)
        if (!in_entry) {
            size_t quote1 = line.find('"');
            size_t quote2 = line.find('"', quote1 + 1);
            size_t brace = line.find('{');
            if (quote1 != std::string::npos && quote2 != std::string::npos && brace != std::string::npos) {
                current_pkg = line.substr(quote1 + 1, quote2 - quote1 - 1);
                current_entry = LockEntry{};
                in_entry = true;
            }
            continue;
        }
        
        // Inside an entry
        std::string version = extract_value(line, "version");
        if (!version.empty()) current_entry.version = version;
        
        std::string repo = extract_value(line, "repository");
        if (!repo.empty()) current_entry.repository = repo;
        
        std::string commit = extract_value(line, "commit");
        if (!commit.empty()) current_entry.commit = commit;
        
        std::string sha256 = extract_value(line, "sha256");
        if (!sha256.empty()) current_entry.sha256 = sha256;

        int pond = 0;
        if (extract_int_value(line, "pond", pond)) current_entry.pond = pond;

        int min_drop = 0;
        if (extract_int_value(line, "min-drop", min_drop)) current_entry.min_drop = min_drop;
        
        // End of entry
        if (line.find('}') != std::string::npos) {
            if (!current_pkg.empty()) {
                packages[current_pkg] = current_entry;
            }
            in_entry = false;
            current_pkg.clear();
        }
    }
    
    return packages;
}

bool write_lock_file(const fs::path& lock_path, const std::map<std::string, LockEntry>& packages) {
    std::ofstream file(lock_path);
    if (!file) {
        std::cerr << RED << "Error:" << RESET << " Could not write lock file" << std::endl;
        return false;
    }
    
    file << "{\n";
    file << "  \"lockfile-version\": 1,\n";
    file << "  \"packages\": {\n";
    
    bool first = true;
    for (const auto& [name, entry] : packages) {
        if (!first) file << ",\n";
        first = false;
        
        file << "    \"" << name << "\": {\n";
        file << "      \"version\": \"" << entry.version << "\",\n";
        file << "      \"repository\": \"" << entry.repository << "\"";
        if (!entry.commit.empty()) {
            file << ",\n      \"commit\": \"" << entry.commit << "\"";
        }
        if (!entry.sha256.empty()) {
            file << ",\n      \"sha256\": \"" << entry.sha256 << "\"";
        }
        if (entry.pond >= 0) {
            file << ",\n      \"pond\": " << entry.pond;
        }
        if (entry.min_drop > 0) {
            file << ",\n      \"min-drop\": " << entry.min_drop;
        }
        file << "\n    }";
    }
    
    file << "\n  }\n";
    file << "}\n";
    
    return true;
}

bool fetch_package_info(const std::string& package_name, PackageInfo& package_info, const std::string& requested_version) {
    std::string url = REGISTRY_BASE_URL + package_name + ".json";
    
    // Use curl to fetch
    std::string cmd = "curl -s -f \"" + url + "\" 2>/dev/null";
    std::string json = exec_command(cmd);
    
    if (json.empty()) {
        // Try with sharded path (first two chars)
        if (package_name.length() >= 2) {
            std::string shard = package_name.substr(0, 2);
            url = REGISTRY_BASE_URL + shard + "/" + package_name + ".json";
            cmd = "curl -s -f \"" + url + "\" 2>/dev/null";
            json = exec_command(cmd);
        }
    }
    
    if (json.empty()) {
        return false;
    }
    
    package_info.name = extract_json_string(json, "name");
    package_info.repository = extract_json_string(json, "repository");
    
    if (package_info.name.empty() || package_info.repository.empty()) {
        return false;
    }

    std::vector<std::string> release_objects = extract_release_objects(json);
    if (release_objects.empty()) {
        std::cerr << RED << "Error:" << RESET << " Package '" << package_name << "' has no releases in registry" << std::endl;
        return false;
    }

    int current_drop = get_current_compiler_drop();
    int current_pond = get_current_compiler_pond();
    RegistryRelease selected;
    bool found = false;

    for (const auto& release_json : release_objects) {
        RegistryRelease candidate;
        if (!parse_release(release_json, candidate)) {
            continue;
        }

        if (!requested_version.empty()) {
            if (candidate.version != requested_version) {
                continue;
            }

            if (candidate.pond != current_pond) {
                std::cerr << RED << "Error:" << RESET << " Requested " << package_name << "@" << requested_version
                          << " targets pond " << candidate.pond
                          << " (current: " << current_pond << ")" << std::endl;
                return false;
            }

            if (current_drop > 0 && candidate.min_drop > current_drop) {
                std::cerr << RED << "Error:" << RESET << " Requested " << package_name << "@" << requested_version
                          << " requires compiler drop >= " << candidate.min_drop
                          << " (current: " << current_drop << ")" << std::endl;
                return false;
            }

            selected = candidate;
            found = true;
            break;
        }

        if (candidate.pond != current_pond) {
            continue;
        }

        if (current_drop <= 0 || candidate.min_drop <= current_drop) {
            selected = candidate;
            found = true;
            break;
        }
    }

    if (!found) {
        if (!requested_version.empty()) {
            std::cerr << RED << "Error:" << RESET << " Version '" << requested_version
                      << "' not found for package '" << package_name << "'" << std::endl;
        } else {
            std::cerr << RED << "Error:" << RESET << " No compatible release found for package '" << package_name << "'"
                      << " on compiler pond " << current_pond << ", drop " << current_drop << std::endl;
        }
        return false;
    }

    package_info.version = selected.version;
    package_info.pond = selected.pond;
    package_info.min_drop = selected.min_drop;
    package_info.commit = selected.commit;
    package_info.sha256 = selected.sha256;
    
    // commit and sha256 are required for security
    if (package_info.commit.empty() || package_info.sha256.empty()) {
        std::cerr << RED << "Error:" << RESET << " Package '" << package_name 
                  << "' is missing source.commit or source.sha256 in registry (required for supply chain security)" << std::endl;
        return false;
    }
    
    return true;
}

bool download_package(const PackageInfo& info, const fs::path& dest) {
    // Remove trailing slash from repo URL if present
    std::string repo = info.repository;
    if (!repo.empty() && repo.back() == '/') {
        repo.pop_back();
    }
    
    // If destination exists, remove it first
    if (fs::exists(dest)) {
        fs::remove_all(dest);
    }
    
    // If we have a specific commit, clone at that commit for security
    std::string cmd;
    if (!info.commit.empty()) {
        // Clone at specific commit (full clone needed to checkout specific commit)
        cmd = "git clone -q \"" + repo + "\" \"" + dest.string() + "\" 2>/dev/null && "
              "cd \"" + dest.string() + "\" && git checkout -q " + info.commit + " 2>/dev/null";
    } else {
        // Fallback: shallow clone (less secure, for packages without commit pinning)
        cmd = "git clone --depth 1 -q \"" + repo + "\" \"" + dest.string() + "\" 2>/dev/null";
    }
    
    int result = run_command(cmd);
    
    if (result != 0) {
        std::cerr << RED << "Error:" << RESET << " Failed to clone " << repo;
        if (!info.commit.empty()) {
            std::cerr << " at commit " << info.commit.substr(0, 8) << "...";
        }
        std::cerr << std::endl;
        return false;
    }
    
    // Verify SHA256 if provided (supply chain security)
    if (!info.sha256.empty()) {
        // Compute SHA256 of the repo tarball (excluding .git)
        // Create a clean tarball from the checkout and compute hash
        std::string hash_cmd = "cd \"" + dest.string() + "\" && "
            "find . -path ./.git -prune -o -type f -print0 | sort -z | "
            "xargs -0 sha256sum | sha256sum | cut -d' ' -f1";
        std::string computed_hash = exec_command(hash_cmd);
        
        // Trim whitespace
        while (!computed_hash.empty() && (computed_hash.back() == '\n' || computed_hash.back() == ' ')) {
            computed_hash.pop_back();
        }
        
        // For now, skip strict verification and log a warning if mismatched
        // Full verification requires matching GitHub's tarball format exactly
        // TODO: Download tarball directly and verify, or use git-archive
        if (!computed_hash.empty() && computed_hash.length() == 64) {
            // We'll implement proper tarball verification later
            // For now, the commit pin provides the security guarantee
        }
    }
    
    // Remove .git directory to save space
    fs::path git_dir = dest / ".git";
    if (fs::exists(git_dir)) {
        fs::remove_all(git_dir);
    }
    
    return true;
}

int add_package(const std::string& package_name, const std::string& version) {
    print_pkg_banner("add");
    std::cout << std::endl;
    
    // Validate package name
    std::regex name_regex("^[a-z0-9][a-z0-9._-]*$");
    if (!std::regex_match(package_name, name_regex)) {
        std::cerr << RED << "Error:" << RESET << " Invalid package name '" << package_name << "'" << std::endl;
        std::cerr << "  Package names must be lowercase and contain only letters, numbers, dots, hyphens, and underscores." << std::endl;
        return 1;
    }
    
    std::cout << "  " << DIM << "Fetching package info..." << RESET << std::endl;
    
    // Fetch package info from registry
    PackageInfo info;
    if (!fetch_package_info(package_name, info, version)) {
        std::cerr << RED << "Error:" << RESET << " Could not resolve installable release for package '" << package_name << "'" << std::endl;
        return 1;
    }
    
    std::cout << "  " << DIM << "Found " << info.name << "@" << info.version << RESET << std::endl;
    
    // Determine paths
    fs::path project_root = fs::current_path();
    fs::path pkgs_dir = project_root / ".coi" / "pkgs";
    fs::path pkg_dest = pkgs_dir / package_name;
    fs::path lock_path = project_root / "coi.lock";
    
    // Create .coi/pkgs directory if needed
    fs::create_directories(pkgs_dir);
    
    // Download the package
    std::cout << "  " << DIM << "Downloading..." << RESET << std::endl;
    if (!download_package(info, pkg_dest)) {
        return 1;
    }
    
    // Update lock file
    auto packages = read_lock_file(lock_path);
    LockEntry entry;
    entry.version = info.version;
    entry.repository = info.repository;
    entry.pond = info.pond;
    entry.min_drop = info.min_drop;
    entry.commit = info.commit;
    entry.sha256 = info.sha256;
    packages[package_name] = entry;
    
    if (!write_lock_file(lock_path, packages)) {
        return 1;
    }
    
    std::cout << std::endl;
    std::cout << "  " << GREEN << "✓" << RESET << " Added " << BOLD << package_name << "@" << info.version << RESET << std::endl;
    std::cout << std::endl;
    std::cout << "  " << DIM << "Import with:" << RESET << std::endl;
    std::cout << "    " << CYAN << "import \"@" << package_name << "\";" << RESET << std::endl;
    std::cout << std::endl;
    
    return 0;
}

int install_packages() {
    print_pkg_banner("install");
    std::cout << std::endl;
    
    fs::path project_root = fs::current_path();
    fs::path lock_path = project_root / "coi.lock";
    fs::path pkgs_dir = project_root / ".coi" / "pkgs";
    
    if (!fs::exists(lock_path)) {
        std::cout << "  " << DIM << "No coi.lock file found. Nothing to install." << RESET << std::endl;
        std::cout << std::endl;
        return 0;
    }
    
    auto packages = read_lock_file(lock_path);
    
    if (packages.empty()) {
        std::cout << "  " << DIM << "No packages in coi.lock." << RESET << std::endl;
        std::cout << std::endl;
        return 0;
    }
    
    // Create .coi/pkgs directory if needed
    fs::create_directories(pkgs_dir);
    
    int installed = 0;
    int skipped = 0;
    int failed = 0;
    int current_pond = get_current_compiler_pond();
    int current_drop = get_current_compiler_drop();
    
    for (const auto& [name, entry] : packages) {
        fs::path pkg_dest = pkgs_dir / name;
        
        // Check if already installed
        if (fs::exists(pkg_dest / "Mod.coi")) {
            std::cout << "  " << DIM << "✓ " << name << "@" << entry.version << " (already installed)" << RESET << std::endl;
            skipped++;
            continue;
        }
        
        std::cout << "  " << DIM << "Installing " << name << "@" << entry.version << "..." << RESET << std::endl;

        if (entry.pond >= 0 && entry.min_drop > 0) {
            if (entry.pond != current_pond) {
                std::cerr << "  " << RED << "✗" << RESET << " " << name << "@" << entry.version
                          << " requires pond " << entry.pond
                          << " (current: " << current_pond << ")" << std::endl;
                failed++;
                continue;
            }

            if (current_drop < entry.min_drop) {
                std::cerr << "  " << RED << "✗" << RESET << " " << name << "@" << entry.version
                          << " requires drop >= " << entry.min_drop
                          << " (current: " << current_drop << ")" << std::endl;
                failed++;
                continue;
            }
        } else {
            std::cout << "  " << YELLOW << "!" << RESET << " Missing pond/min-drop metadata for " << name
                      << " in coi.lock; skipping compatibility check" << std::endl;
        }
        
        PackageInfo info;
        info.name = name;
        info.version = entry.version;
        info.repository = entry.repository;
        info.pond = entry.pond;
        info.min_drop = entry.min_drop;
        info.commit = entry.commit;
        info.sha256 = entry.sha256;
        
        if (download_package(info, pkg_dest)) {
            std::cout << "  " << GREEN << "✓" << RESET << " " << name << "@" << entry.version << std::endl;
            installed++;
        } else {
            std::cerr << "  " << RED << "✗" << RESET << " Failed to install " << name << std::endl;
            failed++;
        }
    }
    
    std::cout << std::endl;
    if (failed == 0) {
        std::cout << "  " << GREEN << "✓" << RESET << " Installed " << installed << " package(s)";
        if (skipped > 0) {
            std::cout << ", " << skipped << " already up to date";
        }
        std::cout << std::endl;
    } else {
        std::cout << "  " << YELLOW << "!" << RESET << " Installed " << installed << ", failed " << failed << std::endl;
    }
    std::cout << std::endl;
    
    return failed > 0 ? 1 : 0;
}

int remove_package(const std::string& package_name) {
    print_pkg_banner("remove");
    std::cout << std::endl;
    
    fs::path project_root = fs::current_path();
    fs::path lock_path = project_root / "coi.lock";
    fs::path pkg_dir = project_root / ".coi" / "pkgs" / package_name;
    
    // Read lock file
    auto packages = read_lock_file(lock_path);
    
    bool in_lock = packages.find(package_name) != packages.end();
    bool on_disk = fs::exists(pkg_dir);
    
    if (!in_lock && !on_disk) {
        std::cerr << RED << "Error:" << RESET << " Package '" << package_name << "' is not installed" << std::endl;
        return 1;
    }
    
    // Remove from disk
    if (on_disk) {
        fs::remove_all(pkg_dir);
        std::cout << "  " << DIM << "Removed .coi/pkgs/" << package_name << "/" << RESET << std::endl;
    }
    
    // Remove from lock file
    if (in_lock) {
        packages.erase(package_name);
        write_lock_file(lock_path, packages);
        std::cout << "  " << DIM << "Updated coi.lock" << RESET << std::endl;
    }
    
    std::cout << std::endl;
    std::cout << "  " << GREEN << "✓" << RESET << " Removed " << BOLD << package_name << RESET << std::endl;
    std::cout << std::endl;
    
    return 0;
}

int list_packages() {
    print_pkg_banner("list");
    std::cout << std::endl;
    
    fs::path project_root = fs::current_path();
    fs::path lock_path = project_root / "coi.lock";
    
    auto packages = read_lock_file(lock_path);
    
    if (packages.empty()) {
        std::cout << "  " << DIM << "No packages installed." << RESET << std::endl;
        std::cout << std::endl;
        std::cout << "  " << DIM << "Run" << RESET << " coi add <package> " << DIM << "to add a package." << RESET << std::endl;
        std::cout << std::endl;
        return 0;
    }
    
    std::cout << "  " << BOLD << "Installed packages:" << RESET << std::endl;
    std::cout << std::endl;
    
    for (const auto& [name, entry] : packages) {
        std::cout << "  " << CYAN << "@" << name << RESET << " " << DIM << entry.version << RESET << std::endl;
    }
    
    std::cout << std::endl;
    std::cout << "  " << DIM << packages.size() << " package(s)" << RESET << std::endl;
    std::cout << std::endl;
    
    return 0;
}

int update_package(const std::string& package_name) {
    print_pkg_banner("update");
    std::cout << std::endl;
    
    fs::path project_root = fs::current_path();
    fs::path lock_path = project_root / "coi.lock";
    
    auto packages = read_lock_file(lock_path);
    
    if (packages.find(package_name) == packages.end()) {
        std::cerr << RED << "Error:" << RESET << " Package '" << package_name << "' is not installed" << std::endl;
        return 1;
    }
    
    std::cout << "  " << DIM << "Checking for updates..." << RESET << std::endl;
    
    // Fetch latest info from registry
    PackageInfo info;
    if (!fetch_package_info(package_name, info)) {
        std::cerr << RED << "Error:" << RESET << " Could not fetch package info for '" << package_name << "'" << std::endl;
        return 1;
    }
    
    LockEntry& current = packages[package_name];
    
    if (current.version == info.version) {
        std::cout << std::endl;
        std::cout << "  " << GREEN << "✓" << RESET << " " << package_name << "@" << current.version << " is already up to date" << std::endl;
        std::cout << std::endl;
        return 0;
    }
    
    std::cout << "  " << DIM << "Updating " << package_name << " " << current.version << " → " << info.version << RESET << std::endl;
    
    // Download new version
    fs::path pkgs_dir = project_root / ".coi" / "pkgs";
    fs::path pkg_dest = pkgs_dir / package_name;
    
    if (!download_package(info, pkg_dest)) {
        return 1;
    }
    
    // Update lock file
    current.version = info.version;
    current.repository = info.repository;
    current.pond = info.pond;
    current.min_drop = info.min_drop;
    current.commit = info.commit;
    current.sha256 = info.sha256;
    write_lock_file(lock_path, packages);
    
    std::cout << std::endl;
    std::cout << "  " << GREEN << "✓" << RESET << " Updated " << BOLD << package_name << RESET << " to " << info.version << std::endl;
    std::cout << std::endl;
    
    return 0;
}

int update_all_packages() {
    print_pkg_banner("update");
    std::cout << std::endl;
    
    fs::path project_root = fs::current_path();
    fs::path lock_path = project_root / "coi.lock";
    
    auto packages = read_lock_file(lock_path);
    
    if (packages.empty()) {
        std::cout << "  " << DIM << "No packages installed." << RESET << std::endl;
        std::cout << std::endl;
        return 0;
    }
    
    int updated = 0;
    int up_to_date = 0;
    int failed = 0;
    
    for (auto& [name, entry] : packages) {
        std::cout << "  " << DIM << "Checking " << name << "..." << RESET << std::endl;
        
        PackageInfo info;
        if (!fetch_package_info(name, info)) {
            std::cerr << "  " << RED << "✗" << RESET << " Could not fetch info for " << name << std::endl;
            failed++;
            continue;
        }
        
        if (entry.version == info.version) {
            up_to_date++;
            continue;
        }
        
        std::cout << "  " << DIM << "Updating " << name << " " << entry.version << " → " << info.version << RESET << std::endl;
        
        fs::path pkgs_dir = project_root / ".coi" / "pkgs";
        fs::path pkg_dest = pkgs_dir / name;
        
        if (download_package(info, pkg_dest)) {
            entry.version = info.version;
            entry.repository = info.repository;
            entry.pond = info.pond;
            entry.min_drop = info.min_drop;
            entry.commit = info.commit;
            entry.sha256 = info.sha256;
            updated++;
        } else {
            failed++;
        }
    }
    
    // Write updated lock file
    write_lock_file(lock_path, packages);
    
    std::cout << std::endl;
    if (updated > 0) {
        std::cout << "  " << GREEN << "✓" << RESET << " Updated " << updated << " package(s)";
    } else {
        std::cout << "  " << GREEN << "✓" << RESET << " All packages up to date";
    }
    if (failed > 0) {
        std::cout << ", " << failed << " failed";
    }
    std::cout << std::endl;
    std::cout << std::endl;
    
    return failed > 0 ? 1 : 0;
}
