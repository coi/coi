// =============================================================================
// JSON Code Generation for Coi
// 
// Generates C++ code for Json.parse() intrinsic calls.
// Uses static schema mapping - no runtime parser, just hardcoded field extraction.
// =============================================================================

#pragma once

#include "ast/definitions.h"
#include <string>
#include <map>
#include <vector>
#include <ostream>

// Registry for data types - populated before code generation
class DataTypeRegistry {
public:
    static DataTypeRegistry& instance();
    
    // Register a data type
    void register_type(const std::string& name, const std::vector<DataField>& fields);
    
    // Look up a data type's fields
    const std::vector<DataField>* lookup(const std::string& name) const;
    
    // Clear all registrations
    void clear();
    
private:
    DataTypeRegistry() = default;
    std::map<std::string, std::vector<DataField>> types_;
};

// Generate the JSON parse code for a specific data type and callbacks
// Returns empty string if type is not found
std::string generate_json_parse(
    const std::string& data_type,           // e.g., "User"
    const std::string& json_expr,            // e.g., "jsonString"
    const std::string& on_success_callback,  // e.g., "handleUser" or empty
    const std::string& on_error_callback     // e.g., "handleError" or empty
);

// Generate the Meta struct definition for a data type
// Returns the struct code (e.g., "struct UserMeta : json::MetaBase { ... }")
std::string generate_meta_struct(const std::string& data_type);

// Emit the JSON runtime helpers directly into the output stream
// This is called once at the top of app.cc when Json.parse is used
void emit_json_runtime(std::ostream& out);

