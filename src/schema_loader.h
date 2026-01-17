#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include "coi_schema.h"

class SchemaLoader {
public:
    static SchemaLoader& instance();
    
    // Initialize the lookup map from the generated schema
    void init();
    
    // Lookup a function by its snake_case name (e.g. "set_size")
    // Returns nullptr if not found
    const coi::SchemaEntry* lookup(const std::string& func_name) const;

    bool is_handle(const std::string& type) const;
    
    // Get the namespace for a type name (e.g., "DOMElement" -> "dom", "System" -> "system")
    // Returns empty string if not found
    std::string get_namespace_for_type(const std::string& type_name) const;
    
    // Check if 'derived' can be used where 'base' is expected
    // e.g., is_assignable_to("Canvas", "DOMElement") returns true
    bool is_assignable_to(const std::string& derived, const std::string& base) const;

    // Helper to convert camelCase to snake_case
    static std::string to_snake_case(const std::string& camel);

private:
    std::unordered_map<std::string, const coi::SchemaEntry*> entries_;
    std::unordered_set<std::string> handles_;  
    std::unordered_map<std::string, std::string> handle_inheritance_; // derived -> base
    std::unordered_map<std::string, std::string> type_to_ns_;  // type name -> namespace
};
