#include "codegen_state.h"

std::set<std::string> g_ref_props;
std::map<std::string, UsedEvent> g_used_events;

std::map<std::string, ComponentArrayLoopInfo> g_component_array_loops;
std::map<std::string, std::vector<ArrayLoopInfo>> g_array_loops;
std::map<std::string, HtmlLoopVarInfo> g_html_loop_var_infos;
std::map<std::string, std::set<std::string>> g_method_reads;
bool g_method_reads_building = false;
