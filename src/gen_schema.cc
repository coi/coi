// Generates a minimal schema header for the coi compiler
// Only includes fields coi actually needs (no JS action strings)
// Uses the already-parsed webcc_schema.h for consistency
// Also generates .coi definition files for LSP/documentation support

#include <iostream>
#include <fstream>
#include <set>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <cctype>

// Include webcc's schema definitions
#include "../deps/webcc/src/cli/schema.h"
#include "../deps/webcc/src/cli/webcc_schema.h"

// Functions that are handled by COI language constructs (not exposed directly)
static const std::set<std::string> EXCLUDED_FUNCTIONS = {
    "set_main_loop",       // Handled by tick {}
    "add_click_listener",  // Handled by onClick attribute
    // Add more as needed
};

// Convert snake_case to camelCase for COI function names
std::string to_camel_case(const std::string& snake) {
    std::string result;
    bool capitalize_next = false;
    for (char c : snake) {
        if (c == '_') {
            capitalize_next = true;
        } else {
            if (capitalize_next) {
                result += std::toupper(c);
                capitalize_next = false;
            } else {
                result += c;
            }
        }
    }
    return result;
}

// Convert webcc type to COI type
std::string to_coi_type(const std::string& type, const std::string& handle_type) {
    if (type == "handle" && !handle_type.empty()) {
        return handle_type;
    }
    if (type == "int32") return "int";
    if (type == "uint32") return "int";
    if (type == "uint8") return "int";
    if (type == "float32") return "float";
    if (type == "float64") return "float";
    if (type == "string") return "string";
    if (type == "bool") return "bool";
    if (type == "func_ptr") return "func";  // Special case
    return type;
}

int main() {
    std::set<std::string> handles;

    // Collect all handle types from commands
    for (const auto* c = webcc::SCHEMA_COMMANDS; !c->ns.empty(); ++c) {
        // Check return handle type
        if (!c->return_handle_type.empty()) {
            handles.insert(c->return_handle_type);
        }
        // Check param handle types
        for (const auto& p : c->params) {
            if (!p.handle_type.empty()) {
                handles.insert(p.handle_type);
            }
        }
    }

    // Collect from events too
    for (const auto* e = webcc::SCHEMA_EVENTS; !e->ns.empty(); ++e) {
        for (const auto& p : e->params) {
            if (!p.handle_type.empty()) {
                handles.insert(p.handle_type);
            }
        }
    }

    // Collect from inheritance
    for (const auto* kv = webcc::HANDLE_INHERITANCE; kv->first != nullptr; ++kv) {
        handles.insert(kv->first);
        handles.insert(kv->second);
    }

    // Generate Header
    {
        std::ofstream out("src/coi_schema.h");
        if (!out) {
            std::cerr << "[COI] Error: Cannot create src/coi_schema.h" << std::endl;
            return 1;
        }

        out << R"(// GENERATED FILE - DO NOT EDIT
// Minimal schema for coi compiler (no JS action strings)
#pragma once
#include <string>
#include <vector>

namespace coi {

struct SchemaParam {
    std::string type;        // For handles, this is the actual handle type (e.g., "DOMElement")
    std::string name;
};

struct SchemaEntry {
    std::string ns;
    std::string func_name;
    std::vector<SchemaParam> params;
    std::string return_type; // For handles, this is the actual handle type (e.g., "Canvas")
};

extern const SchemaEntry SCHEMA[];
extern const size_t SCHEMA_COUNT;

extern const char* HANDLES[];
extern const size_t HANDLE_COUNT;

// Handle inheritance: maps derived type -> base type
// e.g., Canvas -> DOMElement means Canvas can be used where DOMElement is expected
extern const std::pair<const char*, const char*> HANDLE_INHERITANCE[];

} // namespace coi
)";
        out.close();
    }

    // Generate Source
    {
        std::ofstream out("src/coi_schema.cc");
        if (!out) {
            std::cerr << "[COI] Error: Cannot create src/coi_schema.cc" << std::endl;
            return 1;
        }

        out << R"(// GENERATED FILE - DO NOT EDIT
#include "coi_schema.h"

namespace coi {

const SchemaEntry SCHEMA[] = {
)";

        size_t count = 0;
        for (const auto* c = webcc::SCHEMA_COMMANDS; !c->ns.empty(); ++c) {
            out << "    {\"" << c->ns << "\", \"" << c->func_name << "\", {";
            
            bool first = true;
            for (const auto& p : c->params) {
                if (!first) out << ", ";
                first = false;
                
                // Use handle_type if it's a handle, otherwise use the base type
                std::string type = p.type;
                if (p.type == "handle" && !p.handle_type.empty()) {
                    type = p.handle_type;
                }
                out << "{\"" << type << "\", \"" << p.name << "\"}";
            }
            out << "}, ";
            
            // Return type: use handle_type if it's a handle return
            std::string ret_type = c->return_type;
            if (c->return_type == "handle" && !c->return_handle_type.empty()) {
                ret_type = c->return_handle_type;
            }
            out << "\"" << ret_type << "\"},\n";
            
            count++;
        }

        out << "};\n\n";
        out << "const size_t SCHEMA_COUNT = " << count << ";\n\n";

        out << "const char* HANDLES[] = {\n";
        for (const auto& h : handles) {
            out << "    \"" << h << "\",\n";
        }
        out << "};\n\n";
        out << "const size_t HANDLE_COUNT = " << handles.size() << ";\n\n";

        // Generate inheritance table
        out << "const std::pair<const char*, const char*> HANDLE_INHERITANCE[] = {\n";
        for (const auto* kv = webcc::HANDLE_INHERITANCE; kv->first != nullptr; ++kv) {
            out << "    { \"" << kv->first << "\", \"" << kv->second << "\" },\n";
        }
        out << "    { nullptr, nullptr }\n";
        out << "};\n\n";

        out << "} // namespace coi\n";
        out.close();
        
        std::cout << "[COI] Generated coi_schema.h and coi_schema.cc with " << count << " entries and " << handles.size() << " handles" << std::endl;
    }

    // =========================================================
    // Generate .coi definition files in /def folder
    // =========================================================
    namespace fs = std::filesystem;
    
    // Create def directory
    fs::create_directories("def");
    
    // Group commands by namespace
    std::map<std::string, std::vector<const webcc::SchemaCommand*>> commands_by_ns;
    std::map<std::string, std::set<std::string>> handles_by_ns;  // Track which handles belong to which namespace
    
    for (const auto* c = webcc::SCHEMA_COMMANDS; !c->ns.empty(); ++c) {
        // Skip excluded functions
        if (EXCLUDED_FUNCTIONS.count(c->func_name)) {
            continue;
        }
        // Skip functions with func_ptr params (not supported in COI)
        bool has_func_ptr = false;
        for (const auto& p : c->params) {
            if (p.type == "func_ptr") {
                has_func_ptr = true;
                break;
            }
        }
        if (has_func_ptr) continue;
        
        commands_by_ns[c->ns].push_back(c);
        
        // Track handle types for this namespace
        if (!c->return_handle_type.empty()) {
            handles_by_ns[c->ns].insert(c->return_handle_type);
        }
    }
    
    // Map namespaces to webcc header files
    std::map<std::string, std::string> ns_to_header = {
        {"dom", "webcc/dom.h"},
        {"canvas", "webcc/canvas.h"},
        {"audio", "webcc/audio.h"},
        {"input", "webcc/input.h"},
        {"system", "webcc/system.h"},
        {"storage", "webcc/storage.h"},
        {"fetch", "webcc/fetch.h"},
        {"websocket", "webcc/websocket.h"},
        {"image", "webcc/image.h"},
        {"webgl", "webcc/webgl.h"},
        {"wgpu", "webcc/wgpu.h"},
    };
    
    // Group commands by their "receiver" handle type (first param if it's a handle)
    // This lets us show methods on handle types properly
    struct MethodInfo {
        const webcc::SchemaCommand* cmd;
        std::string receiver_type;  // Empty if standalone function
    };
    
    // Helper to capitalize first letter
    auto capitalize = [](const std::string& s) -> std::string {
        if (s.empty()) return s;
        std::string result = s;
        result[0] = std::toupper(result[0]);
        return result;
    };
    
    // Generate a .coi file for each namespace
    for (const auto& [ns, commands] : commands_by_ns) {
        std::string filename = "def/" + ns + ".d.coi";
        std::ofstream out(filename);
        if (!out) {
            std::cerr << "[COI] Error: Cannot create " << filename << std::endl;
            continue;
        }
        
        std::string header_file = ns_to_header.count(ns) ? ns_to_header[ns] : "webcc/" + ns + ".h";
        std::string ns_type = capitalize(ns);  // e.g., "storage" -> "Storage"
        
        out << "// GENERATED FILE - DO NOT EDIT\n";
        out << "// COI definitions for " << ns << " namespace\n";
        out << "// Maps to: " << header_file << "\n";
        out << "\n";
        
        // Categorize functions:
        // 1. Methods on handle types (first param is handle)
        // 2. Static factories (returns handle matching namespace, e.g., Image.load)
        // 3. Namespace utilities (everything else -> Storage.clear, System.log)
        
        std::vector<const webcc::SchemaCommand*> static_factories;
        std::vector<const webcc::SchemaCommand*> namespace_utils;
        std::map<std::string, std::vector<const webcc::SchemaCommand*>> methods_by_handle;
        
        for (const auto* cmd : commands) {
            // Check if first param is a handle (making this an instance method)
            if (!cmd->params.empty() && cmd->params[0].type == "handle" && !cmd->params[0].handle_type.empty()) {
                methods_by_handle[cmd->params[0].handle_type].push_back(cmd);
            }
            // Check if it returns a handle that matches the namespace (static factory)
            else if (!cmd->return_handle_type.empty() && 
                     (cmd->return_handle_type == ns_type || 
                      std::tolower(cmd->return_handle_type[0]) == ns[0])) {
                static_factories.push_back(cmd);
            }
            // Everything else is a namespace utility
            else {
                namespace_utils.push_back(cmd);
            }
        }
        
        // Group static factories by return type
        std::map<std::string, std::vector<const webcc::SchemaCommand*>> factories_by_type;
        for (const auto* cmd : static_factories) {
            factories_by_type[cmd->return_handle_type].push_back(cmd);
        }
        
        // Collect all handle types that need to be generated (either have factories or methods)
        std::set<std::string> all_handle_types;
        for (const auto& [type, _] : factories_by_type) all_handle_types.insert(type);
        for (const auto& [type, _] : methods_by_handle) all_handle_types.insert(type);
        
        // Generate each handle type with both static and instance methods combined
        for (const auto& handle_type : all_handle_types) {
            // Check for inheritance
            std::string extends = "";
            for (const auto* kv = webcc::HANDLE_INHERITANCE; kv->first != nullptr; ++kv) {
                if (kv->first == handle_type) {
                    extends = kv->second;
                    break;
                }
            }
            
            out << "// =========================================================\n";
            out << "// " << handle_type;
            if (!extends.empty()) {
                out << " (extends " << extends << ")";
            }
            out << "\n";
            out << "// =========================================================\n\n";
            
            out << "type " << handle_type << " {\n";
            
            // Shared (static) factory methods first
            if (factories_by_type.count(handle_type)) {
                out << "    // Shared methods (call as " << handle_type << ".methodName(...))\n";
                for (const auto* cmd : factories_by_type[handle_type]) {
                    std::string coi_name = to_camel_case(cmd->func_name);
                    std::string return_type = to_coi_type(cmd->return_type, cmd->return_handle_type);
                    
                    out << "    shared def " << coi_name << "(";
                    
                    bool first = true;
                    for (const auto& p : cmd->params) {
                        if (!first) out << ", ";
                        first = false;
                        std::string param_type = to_coi_type(p.type, p.handle_type);
                        std::string param_name = p.name.empty() ? "arg" : p.name;
                        out << param_name << ": " << param_type;
                    }
                    
                    out << "): " << return_type << " {\n";
                    out << "        // maps to: " << ns << "::" << cmd->func_name << "\n";
                    out << "    }\n";
                }
                out << "\n";
            }
            
            // Instance methods
            if (methods_by_handle.count(handle_type)) {
                out << "    // Instance methods (call as instance.methodName(...))\n";
                for (const auto* cmd : methods_by_handle[handle_type]) {
                    std::string coi_name = to_camel_case(cmd->func_name);
                    std::string return_type = to_coi_type(cmd->return_type, cmd->return_handle_type);
                    if (return_type.empty()) return_type = "void";
                    
                    out << "    def " << coi_name << "(";
                    
                    // Skip first param (it's the receiver/this)
                    bool first = true;
                    for (size_t i = 1; i < cmd->params.size(); ++i) {
                        const auto& p = cmd->params[i];
                        if (!first) out << ", ";
                        first = false;
                        std::string param_type = to_coi_type(p.type, p.handle_type);
                        std::string param_name = p.name.empty() ? "arg" : p.name;
                        out << param_name << ": " << param_type;
                    }
                    
                    out << "): " << return_type << " {\n";
                    out << "        // maps to: " << ns << "::" << cmd->func_name << "\n";
                    out << "    }\n";
                }
            }
            
            out << "}\n\n";
        }
        
        // Generate namespace utilities as a type with shared methods (e.g., Storage.clear, System.log)
        // These are types with only shared (static) methods - not instantiable
        if (!namespace_utils.empty()) {
            // Check if we already generated this type (with factories or instance methods)
            if (!all_handle_types.count(ns_type)) {
                out << "// =========================================================\n";
                out << "// " << ns_type << " (static utilities - not instantiable)\n";
                out << "// =========================================================\n";
                out << "// Usage: " << ns_type << ".methodName(...)\n\n";
                
                out << "type " << ns_type << " {\n";
                out << "    // Shared methods (call as " << ns_type << ".methodName(...))\n";
            } else {
                // Type already exists, just add a comment
                out << "    // Additional shared methods\n";
            }
            
            for (const auto* cmd : namespace_utils) {
                std::string coi_name = to_camel_case(cmd->func_name);
                std::string return_type = to_coi_type(cmd->return_type, cmd->return_handle_type);
                if (return_type.empty()) return_type = "void";
                
                out << "    shared def " << coi_name << "(";
                
                bool first = true;
                for (const auto& p : cmd->params) {
                    if (!first) out << ", ";
                    first = false;
                    std::string param_type = to_coi_type(p.type, p.handle_type);
                    std::string param_name = p.name.empty() ? "arg" : p.name;
                    out << param_name << ": " << param_type;
                }
                
                out << "): " << return_type << " {\n";
                out << "        // maps to: " << ns << "::" << cmd->func_name << "\n";
                out << "    }\n";
            }
            
            if (!all_handle_types.count(ns_type)) {
                out << "}\n\n";
            }
        }
        
        out.close();
        std::cout << "[COI] Generated " << filename << " with " << commands.size() << " functions" << std::endl;
    }
    
    // =========================================================
    // Generate main index file (def/index.d.coi)
    // =========================================================
    {
        std::ofstream out("def/index.d.coi");
        if (!out) {
            std::cerr << "[COI] Error: Cannot create def/index.d.coi" << std::endl;
            return 1;
        }
        
        out << "// GENERATED FILE - DO NOT EDIT\n";
        out << "// COI Standard Library Index\n";
        out << "//\n";
        out << "// This file lists all available COI definitions.\n";
        out << "// These map to the webcc library for web platform access.\n";
        out << "//\n";
        out << "// Available modules:\n";
        
        for (const auto& [ns, commands] : commands_by_ns) {
            out << "//   - " << ns << ".d.coi (" << commands.size() << " functions)\n";
        }
        
        out << "\n";
        out << "// =========================================================\n";
        out << "// All Handle Types\n";
        out << "// =========================================================\n\n";
        
        // List all handles with their inheritance
        for (const auto& handle : handles) {
            std::string extends = "";
            for (const auto* kv = webcc::HANDLE_INHERITANCE; kv->first != nullptr; ++kv) {
                if (kv->first == handle) {
                    extends = kv->second;
                    break;
                }
            }
            
            if (!extends.empty()) {
                out << "// " << handle << " extends " << extends << "\n";
            } else {
                out << "// " << handle << "\n";
            }
            out << "type " << handle << " {}\n\n";
        }
        
        out << "// =========================================================\n";
        out << "// Language Constructs (built into COI)\n";
        out << "// =========================================================\n";
        out << "//\n";
        out << "// The following functionality is handled by COI language constructs:\n";
        out << "//\n";
        out << "// - init { ... }          : Runs once when component mounts\n";
        out << "// - tick { ... }          : Main loop (replaces setMainLoop)\n";
        out << "// - style { ... }         : Scoped CSS styles for this component\n";
        out << "// - style global { ... }  : Global CSS styles (not scoped)\n";
        out << "// - onClick={handler}     : Click events (replaces addEventListener)\n";
        out << "// - view { ... }          : DOM generation\n";
        out << "// - component Name { }    : Component definition\n";
        out << "// - prop Type name        : Component properties\n";
        out << "// - mut Type name         : Mutable state\n";
        out << "//\n";
        
        out.close();
        std::cout << "[COI] Generated def/index.d.coi" << std::endl;
    }
    
    // =========================================================
    // Generate types definition file (def/types.d.coi)
    // =========================================================
    {
        std::ofstream out("def/types.d.coi");
        if (!out) {
            std::cerr << "[COI] Error: Cannot create def/types.d.coi" << std::endl;
            return 1;
        }
        
        out << "// GENERATED FILE - DO NOT EDIT\n";
        out << "// COI Built-in Types\n";
        out << "//\n";
        out << "// These are the primitive types available in COI.\n";
        out << "\n";
        out << "// =========================================================\n";
        out << "// Primitive Types\n";
        out << "// =========================================================\n";
        out << "//\n";
        out << "// int     - 32-bit signed integer\n";
        out << "// float   - 32-bit floating point\n";
        out << "// string  - UTF-8 string\n";
        out << "// bool    - Boolean (true/false)\n";
        out << "// void    - No return value\n";
        out << "//\n";
        out << "\n";
        out << "// =========================================================\n";
        out << "// Type Mappings (COI -> WebAssembly)\n";
        out << "// =========================================================\n";
        out << "//\n";
        out << "// int     -> i32\n";
        out << "// float   -> f32\n";
        out << "// string  -> i32 (pointer to memory)\n";
        out << "// bool    -> i32 (0 or 1)\n";
        out << "// Handle  -> i32 (handle ID)\n";
        out << "//\n";
        
        out.close();
        std::cout << "[COI] Generated def/types.d.coi" << std::endl;
    }

    return 0;
}
