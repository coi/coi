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
    // def f(...) : T = js { ... }: the body is JavaScript the page runs (js_definition, definitions.cc)
    bool is_js = false;
    std::string js_body;
    // a test block: test "what it checks" { ... }, a function with no params that `coi test` runs
    std::string test_name;
    std::string display_name() const { return test_name.empty() ? name : "test \"" + test_name + "\""; }

    std::string to_webcc(const std::string& injected_code = "");
    std::string free_declaration();
    std::string free_definition();
    void collect_modifications(std::set<std::string>& mods) const;
};

// call before validation and codegen
void register_free_functions(const std::vector<std::unique_ptr<FunctionDef>>& functions);

struct DataField {
    std::string type;
    std::string name;
};

// one alternative of a union: the tag values that mean it, and the pod it then is
struct UnionAlt {
    std::vector<std::string> tags;
    std::string type;   // as written (Geo::Point), qualified for C++ before codegen
    std::string name;   // the bare pod name, what a match arm says: Point(Point p)
};

struct DataDef : ASTNode {
    std::string name;
    std::string module_name;  // Module this type belongs to
    std::string source_file;  // Absolute path to the file this type is defined in
    bool is_public = false;   // Requires pub keyword to be importable
    std::vector<std::string> type_params;  // Generic type parameters (e.g., ["T"] or ["A", "B"])
    std::vector<DataField> fields;

    // a union: one of several pods at a time, told apart by a string field (the tag) whose value
    // picks the alternative. fields[0] is the tag; the other fields are on every alternative
    bool is_union = false;
    std::string tag;
    std::vector<UnionAlt> alts;

    std::string to_webcc() override;
};

// Enum definition: enum Mode { Idle, Running, Paused }
std::string emit_coi_enum(const std::string &cpp_name, const std::vector<std::string> &values);

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
