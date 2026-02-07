#pragma once

#include "node.h"
#include "definitions.h"
#include "statements.h"
#include "view.h"

// Route entry for router block
struct RouteEntry {
    std::string path;                              // e.g., "/", "/dashboard", "/pricing"
    std::string component_name;                    // e.g., "Landing", "Dashboard"
    std::string module_name;                       // Module of the target component (filled by type checker)
    std::vector<CallArg> args;                     // Optional component arguments (same as component construction)
    int line = 0;
};

// Router definition block
struct RouterDef {
    std::vector<RouteEntry> routes;
    bool has_route_placeholder = false;  // Set during view validation
    int line = 0;
};

struct Component : ASTNode {
    std::string name;
    std::string module_name;  // Module this component belongs to
    std::string source_file;  // Absolute path to the file this component is defined in
    bool is_public = false;   // Requires pub keyword to be importable
    std::string css;
    std::string global_css;
    std::vector<std::unique_ptr<DataDef>> data;
    std::vector<std::unique_ptr<EnumDef>> enums;
    std::vector<std::unique_ptr<VarDeclaration>> state;
    std::vector<std::unique_ptr<ComponentParam>> params;
    std::vector<FunctionDef> methods;
    std::vector<std::unique_ptr<ASTNode>> render_roots;
    std::unique_ptr<RouterDef> router;  // Optional router block

    void collect_child_components(ASTNode* node, std::map<std::string, int>& counts);
    void collect_child_updates(ASTNode* node, std::map<std::string, std::vector<std::string>>& updates, std::map<std::string, int>& counters);
    std::string to_webcc() override { static CompilerSession s; return to_webcc(s); }
    std::string to_webcc(CompilerSession& session);
};

struct AppConfig {
    std::string root_component;
    std::map<std::string, std::string> routes;
    std::string title;
    std::string description;
    std::string lang = "en";
};
