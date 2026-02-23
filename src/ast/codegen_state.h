#pragma once

#include <map>
#include <set>
#include <string>

// Shared mutable state used during component->C++ lowering.
// Declared here and defined in codegen_state.cc so ownership is explicit.

extern std::set<std::string> g_ref_props;
extern std::string g_ws_assignment_target;

struct ComponentArrayLoopInfo
{
    int loop_id;
    std::string component_type;
    std::string parent_var;
    std::string var_name;
    std::string item_creation_code;
    bool is_member_ref_loop;
    bool is_only_child;
};
extern std::map<std::string, ComponentArrayLoopInfo> g_component_array_loops;

struct ArrayLoopInfo
{
    int loop_id;
    std::string parent_var;
    std::string anchor_var;
    std::string elements_vec_name;
    std::string var_name;
    std::string item_creation_code;
    std::string root_element_var;
    bool is_only_child;
};
extern std::map<std::string, ArrayLoopInfo> g_array_loops;

struct HtmlLoopVarInfo
{
    int loop_id;
    std::string iterable_expr;
};
extern std::map<std::string, HtmlLoopVarInfo> g_html_loop_var_infos;
