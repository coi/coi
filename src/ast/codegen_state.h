#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>
#include "node.h"

// Shared mutable state used during component->C++ lowering.
// Declared here and defined in codegen_state.cc so ownership is explicit.

extern std::set<std::string> g_ref_props;

// state a child made in the view was handed by reference (<Counter &n={count} />): the
// component defines _ref_written_<name>, which the child's change callback calls, once the
// whole view is known (bindings after the child count too)
extern std::set<std::string> g_inline_ref_writes;

// the file of the test being generated, as its failures should name it; "" outside a test
extern std::string g_test_file;

// webcc events with a registered callback
struct UsedEvent
{
    std::string ns, name, key, struct_name; // struct_name: webcc::ns::NameEvent
    std::string handle_type;                // Coi type of the handle in `key`
    bool last;                              // drop the handle's callbacks after it
};
extern std::map<std::string, UsedEvent> g_used_events; // "ns::NAME" -> event

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
    std::string key_expr;
    bool is_only_child;
};
extern std::map<std::string, std::vector<ArrayLoopInfo>> g_array_loops;

struct HtmlLoopVarInfo
{
    int loop_id;
    std::string iterable_expr;
};
extern std::map<std::string, HtmlLoopVarInfo> g_html_loop_var_infos;

// state each method of the current component reads, so a binding that calls it
// (class={isOn(i)}) refreshes when that state changes
extern std::map<std::string, std::set<std::string>> g_method_reads;
// true while g_method_reads is being computed: a plain call then counts as a dependency on the callee
extern bool g_method_reads_building;
// child members (a.b) each method reads, including through the methods it calls
extern std::map<std::string, std::set<MemberDependency>> g_method_member_reads;
// per component (qualified name), the pub mut members each of its methods reads, so an owner's
// binding that calls box.isOpen() refreshes through box.onOpenChange like one reading box.open
extern std::map<std::string, std::map<std::string, std::set<std::string>>> g_component_method_reads;
// for the component being emitted: "box.isOpen" -> {box, open}, one entry per member component's method
extern std::map<std::string, std::set<MemberDependency>> g_child_method_reads;
