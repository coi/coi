#include "codegen_state.h"

std::set<std::string> g_ref_props;
std::string g_ws_assignment_target;

std::map<std::string, ComponentArrayLoopInfo> g_component_array_loops;
std::map<std::string, ArrayLoopInfo> g_array_loops;
std::map<std::string, HtmlLoopVarInfo> g_html_loop_var_infos;
