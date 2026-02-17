#pragma once

#include <string>
#include <vector>
#include <map>
#include <filesystem>

namespace fs = std::filesystem;

// Package information from registry
struct PackageInfo {
    std::string name;
    std::string version;
    std::string repository;
    int pond = -1;
    int min_drop = 0;
    std::string commit;   // Git commit SHA for this release
    std::string sha256;   // SHA256 of the release tarball
};

// Lock file entry
struct LockEntry {
    std::string version;
    std::string repository;
    int pond = -1;
    int min_drop = 0;
    std::string commit;   // Git commit SHA pinned for this version
    std::string sha256;   // SHA256 of the release tarball for verification
};

// Add a package to the project
// Fetches from registry, downloads to .coi/pkgs/, updates coi.lock
// Returns 0 on success, non-zero on error
int add_package(const std::string& package_name, const std::string& version = "");

// Install all packages from coi.lock
// Returns 0 on success, non-zero on error
int install_packages();

// Remove a package from the project
// Removes from .coi/pkgs/ and coi.lock
// Returns 0 on success, non-zero on error
int remove_package(const std::string& package_name);

// List installed packages
// Returns 0 on success, non-zero on error
int list_packages();

// Update a package to latest version
// Returns 0 on success, non-zero on error
int update_package(const std::string& package_name);

// Update all packages to latest versions
// Returns 0 on success, non-zero on error
int update_all_packages();

// Parse coi.lock file
// Returns map of package name -> lock entry
std::map<std::string, LockEntry> read_lock_file(const fs::path& lock_path);

// Write coi.lock file
bool write_lock_file(const fs::path& lock_path, const std::map<std::string, LockEntry>& packages);

// Fetch package info from registry
// Returns true on success and fills package_info
bool fetch_package_info(const std::string& package_name, PackageInfo& package_info, const std::string& version = "");

// Download/clone a package to destination
bool download_package(const PackageInfo& info, const fs::path& dest);
