#pragma once

#include "node.h"
#include "statements.h"

struct FunctionDef {
    std::string name;
    std::string return_type;
    bool is_public = false;
    // Set for top-level functions only
    std::string module_name;
    std::string source_file;
    int line = 0;
    std::vector<std::string> type_params;  // Generic type parameters (e.g., ["T"] or ["A", "B"])
    struct Param {
        std::string type;
        std::string name;
        bool is_mutable = false;
        bool is_reference = false;
    };
    std::vector<Param> params;
    std::vector<std::unique_ptr<Statement>> body;

    std::string to_webcc(const std::string& injected_code = "");
    // Top-level functions: emitted under qualified_name(module_name, name)
    std::string free_declaration();
    std::string free_definition();
    void collect_modifications(std::set<std::string>& mods) const;
};

// Fill FreeFunctionRegistry; call before validation and codegen
void register_free_functions(const std::vector<std::unique_ptr<FunctionDef>>& functions);

struct DataField {
    std::string type;
    std::string name;
};

struct DataDef : ASTNode {
    std::string name;
    std::string module_name;  // Module this type belongs to
    std::string source_file;  // Absolute path to the file this type is defined in
    bool is_public = false;   // Requires pub keyword to be importable
    std::vector<std::string> type_params;  // Generic type parameters (e.g., ["T"] or ["A", "B"])
    std::vector<DataField> fields;

    std::string to_webcc() override;
};

// Enum definition: enum Mode { Idle, Running, Paused }
struct EnumDef : ASTNode {
    std::string name;
    std::string module_name;  // Module this enum belongs to
    std::string source_file;  // Absolute path to the file this enum is defined in
    bool is_public = false;   // Requires pub keyword to be importable
    std::vector<std::string> values;
    bool is_shared = false;
    std::string owner_component;

    std::string to_webcc() override;
};
