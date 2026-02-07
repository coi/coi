#pragma once

#include "node.h"
#include "statements.h"

struct FunctionDef {
    std::string name;
    std::string return_type;
    bool is_public = false;
    struct Param {
        std::string type;
        std::string name;
        bool is_mutable = false;
        bool is_reference = false;
    };
    std::vector<Param> params;
    std::vector<std::unique_ptr<Statement>> body;

    std::string to_webcc(const std::string& injected_code = "");
    void collect_modifications(std::set<std::string>& mods) const;
};

struct DataField {
    std::string type;
    std::string name;
};

struct DataDef : ASTNode {
    std::string name;
    std::string module_name;  // Module this type belongs to
    std::string source_file;  // Absolute path to the file this type is defined in
    bool is_public = false;   // Requires pub keyword to be importable
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
