#pragma once

#include <string>
#include <sstream>
#include <iostream>
#include <stdexcept>

// ANSI color codes for error messages
namespace error_colors {
    constexpr const char* RESET = "\033[0m";
    constexpr const char* BOLD  = "\033[1m";
    constexpr const char* DIM   = "\033[2m";
    constexpr const char* RED   = "\033[31m";
}

// Centralized error handler for consistent error reporting across the codebase
class ErrorHandler {
public:
    // Compilation/parsing errors (throws exception)
    [[noreturn]] static void compiler_error(const std::string& message, int line = -1) {
        std::ostringstream oss;
        oss << error_colors::RED << error_colors::BOLD << "Error:" << error_colors::RESET << " " << message;
        if (line > 0) {
            oss << " at line " << line;
        }
        throw std::runtime_error(oss.str());
    }

    // Type checking errors (prints to stderr)
    static void type_error(const std::string& message, int line = -1) {
        std::cerr << error_colors::RED << error_colors::BOLD << "Error:" << error_colors::RESET << " " << message;
        if (line > 0) {
            std::cerr << " at line " << line;
        }
        std::cerr << std::endl;
    }

    // CLI/runtime errors (prints to stderr)
    static void cli_error(const std::string& message) {
        std::cerr << error_colors::RED << "error" << error_colors::RESET << ": " << message << std::endl;
    }

    // CLI/runtime errors with additional context (prints to stderr)
    static void cli_error(const std::string& message, const std::string& context) {
        std::cerr << error_colors::RED << "error" << error_colors::RESET << ": " << message << std::endl;
        std::cerr << error_colors::DIM << context << error_colors::RESET << std::endl;
    }

    // Build failure message
    static void build_failed() {
        std::cerr << std::endl;
        std::cerr << error_colors::RED << "✗" << error_colors::RESET << " Build failed" << std::endl;
    }

    // Warning message (non-fatal)
    static void warning(const std::string& message, int line = -1) {
        std::cerr << "\033[33m" << error_colors::BOLD << "Warning:" << error_colors::RESET << " " << message;
        if (line > 0) {
            std::cerr << " at line " << line;
        }
        std::cerr << std::endl;
    }
};
