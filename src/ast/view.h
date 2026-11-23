#pragma once

#include "node.h"
#include "expressions.h"
#include <map>
#include <memory>
#include <set>

struct TextNode : ASTNode {
    std::string text;
    TextNode(const std::string& t) : text(t) {}
    std::string to_webcc() override;
};

struct HTMLAttribute {
    std::string name;
    std::unique_ptr<Expression> value;
    std::string braced;   // width={d.hole}: the expression as written, for errors; empty when quoted or bare
    int line = 0;
};

// an attribute that is on by being there at all (disabled="false" still disables), and the DOM
// property that turns it on and off; nullptr for the others. These are set as the property, with
// "false" (or nothing) for off, so disabled={busy ? "true" : "false"} means what it says
inline const char *bool_attr_property(const std::string &name)
{
    static const std::map<std::string, const char *> props = {
        {"checked", "checked"}, {"selected", "selected"}, {"disabled", "disabled"}, {"hidden", "hidden"}, {"readonly", "readOnly"}, {"required", "required"},
        {"multiple", "multiple"}, {"autofocus", "autofocus"}, {"open", "open"}, {"inert", "inert"},
        {"novalidate", "noValidate"}, {"autoplay", "autoplay"}, {"controls", "controls"}, {"loop", "loop"},
        {"muted", "muted"}, {"playsinline", "playsInline"}, {"reversed", "reversed"}};
    auto it = props.find(name);
    return it == props.end() ? nullptr : it->second;
}

struct EventHandler {
    int element_id;
    std::string event_type;      // "click", "input", "change", "keydown"
    std::string handler_code;
    bool is_function_call;
};

struct Binding {
    int element_id;
    std::string type;            // "attr" or "text"
    std::string name;            // attribute name
    std::string value_code;
    std::set<std::string> dependencies;
    std::set<MemberDependency> member_dependencies;  // tracks object.member pairs
    Expression* expr = nullptr;
    int if_region_id = -1;          // innermost enclosing <if> region
    bool in_then_branch = true;
    // every enclosing <if> region, innermost first: (region id, in then branch)
    std::vector<std::pair<int, bool>> if_chain;
};

struct ComponentProp {
    std::string name;
    std::unique_ptr<Expression> value;
    bool is_reference = false;
    bool is_move = false;
    bool is_mutable_def = false;
    bool is_callback = false;
    std::vector<std::string> callback_param_types;
};

// A row of a loop of elements, kept from one sync to the next: what its update has to find again.
// The row's code is the component's own (it reads the component's members and calls its methods
// as the rest of the view does); a struct of its own (_row) keeps its handles, the last value of
// each binding, the side each <if> in it took, its components and the rows of a loop inside it
struct RowPlan {
    // an <if> in the row, in the order the row's code meets them; parent -1 at the row's top
    struct If { std::string cond; int parent; bool in_then; };
    // a value shown in the row: slot is the element's handle in _row.h, kind how it is set
    // (attr, value, prop, text, textnode, html)
    struct Bind { int slot; std::string kind; std::string name; std::string value_code; int iff; bool in_then; };
    // a component in the row: its instance in _row.cN (made in the row's own list of its type),
    // its props set again on an update
    struct Comp { int index; std::string type; std::string update_code; int iff; bool in_then; };
    // a handler registration in the row (its element as _row.h[n]), set again on an update
    struct Handler { std::string code; int iff; bool in_then; };
    // a loop inside the row: its rows are the row's (_row.sN), synced when the row updates
    struct Sub { std::shared_ptr<RowPlan> plan; int iff; bool in_then; };

    std::string name;           // _loop_3, or _loop_3_s0 for a loop inside its rows
    std::string var;            // the loop variable
    bool keyed = false;         // a keyed loop's row stands for an item, a range row for an index
    std::string start_expr, end_expr;       // a range
    std::string iterable_expr, key_expr;    // a keyed loop
    std::string make_code;      // the row's code; it appends to _sp, the loop's parent
    std::string root_var;       // the row's first element, its handle span noted on it

    std::vector<If> ifs;
    std::vector<Bind> binds;
    std::vector<Comp> comps;
    std::vector<Handler> handlers;
    std::vector<Sub> subs;
    std::set<std::string> comp_types;
    int slots = 0;
    // a keyed row: its item can change while the row is kept, so its handlers are set again
    bool rebind = false;
    // a loop inside the row that it can't keep (a loop without a key, or one whose body doesn't
    // start with an element): the row is built again on every sync
    bool rebuild = false;
};

// a kept row's own list of a component type: "_row.k_<Type>"
std::string row_comp_list(const std::string &qname);

// Struct to track reactive loop regions 
struct LoopRegion {
    int loop_id;
    std::string parent_element;
    std::string component_type;
    std::string start_expr;
    std::string end_expr;
    std::set<std::string> dependencies;
    std::string item_creation_code;
    std::string item_update_code;
    std::string var_name;
    std::string root_element_var;
    bool is_html_loop = false;
    bool is_keyed = false;
    bool is_member_ref_loop = false;  // True when iterating over component array with <varName/>
    bool is_only_child = false;       // True when loop is the only child of its parent element
    std::string key_expr;
    std::string key_type;
    std::string iterable_expr;
    std::string iterable_raw;     // the Coi spelling: the loop registries are looked up by it
    std::shared_ptr<RowPlan> row; // a loop over a range of elements: its rows are kept and updated
};

// Struct to track reactive if/else regions
struct IfRegion {
    int if_id;
    std::string condition_code;
    std::set<std::string> dependencies;
    std::set<MemberDependency> member_dependencies;
    std::string then_creation_code;
    std::string else_creation_code;
    std::string then_destroy_code;
    std::string else_destroy_code;
    std::vector<int> then_element_ids;
    std::vector<int> else_element_ids;
    std::vector<std::pair<std::string, int>> then_components;
    std::vector<std::pair<std::string, int>> else_components;
    std::vector<int> then_loop_ids;
    std::vector<int> else_loop_ids;
    std::vector<int> then_if_ids;
    std::vector<int> else_if_ids;
    std::vector<std::string> then_member_refs;  // Member component references in then branch
    std::vector<std::string> else_member_refs;  // Member component references in else branch
};

// handles for one snippet, reserved in one call
struct HandleBlock {
    std::string name;
    int count = 0;

    HandleBlock();
    // Expression for the next handle in this block
    std::string next();
    // Snippet code with the reservation prepended (if any handles were used)
    std::string wrap(const std::string& code) const;
};

// Context for view code generation - bundles common parameters
struct Skeleton;

struct ViewCodegenContext {
    std::stringstream& ss;
    std::string parent;
    int& counter;
    std::vector<EventHandler>& event_handlers;
    std::vector<Binding>& bindings;
    std::map<std::string, int>& component_counters;
    const std::set<std::string>& method_names;
    std::string parent_component_name;
    bool in_loop = false;
    std::vector<LoopRegion>* loop_regions = nullptr;
    int* loop_counter = nullptr;
    std::vector<IfRegion>* if_regions = nullptr;
    int* if_counter = nullptr;
    std::string loop_var_name;
    HandleBlock* handles = nullptr;  // block of the snippet that ss belongs to
    // inside a static skeleton (see Skeleton in view.cc): the template being built, the node
    // index of the parent (-1 at the root) and of this element
    Skeleton* tpl = nullptr;
    int tpl_parent = -1;
    int tpl_k = 0;
    // in a kept row (RowPlan): what it records, and the <if> of the row it is in (-1: none)
    RowPlan* row = nullptr;
    int row_if = -1;
    bool row_then = true;

    // Create a child context with a new parent element
    ViewCodegenContext with_parent(const std::string& new_parent) const {
        ViewCodegenContext c{ss, new_parent, counter, event_handlers, bindings,
            component_counters, method_names, parent_component_name, in_loop,
            loop_regions, loop_counter, if_regions, if_counter, loop_var_name, handles};
        c.row = row;
        c.row_if = row_if;
        c.row_then = row_then;
        return c;
    }

    // records a value of the kept row this element shows; returns the binding's index
    // (nothing outside a kept row: -1)
    int row_bind(const std::string& el_var, const std::string& kind, const std::string& name, const std::string& value_code);
    // a handler set on an element of a kept row: recorded to be set again with the current item
    void row_handler(const std::string& el_var, const std::string& code);

    // loop body context, writes to its own snippet
    // a nested loop's handlers capture every enclosing loop variable: "p, nb"
    ViewCodegenContext for_loop(std::stringstream& body_ss, HandleBlock& body_handles,
                                const std::string& new_parent, const std::string& var_name) const {
        return ViewCodegenContext{body_ss, new_parent, counter, event_handlers, bindings,
            component_counters, method_names, parent_component_name, true,
            nullptr, nullptr, nullptr, nullptr, loop_var_name.empty() ? var_name : loop_var_name + ", " + var_name, &body_handles};
    }

    std::string next_handle() { return handles->next(); }
};

struct ComponentInstantiation : ASTNode {
    std::string component_name;
    std::string module_prefix;        // Module prefix for cross-module access (e.g., "TurboUI" in TurboUI::Button)
    std::vector<ComponentProp> props;
    bool is_member_reference = false;  // True if this refers to a member variable (e.g., <a/> for "mut Test a;")
    std::string member_name;           // Name of the member variable if is_member_reference is true
    
    std::string to_webcc() override;
    void generate_code(ViewCodegenContext& ctx);
    void collect_dependencies(std::set<std::string>& deps) override;
};

struct HTMLElement : ASTNode {
    std::string tag;
    std::vector<HTMLAttribute> attributes;
    std::vector<std::unique_ptr<ASTNode>> children;
    std::string ref_binding;
    int tpl_size = 0;   // nodes of the static skeleton rooted here, counted before it is generated

    std::string to_webcc() override;
    void generate_code(ViewCodegenContext& ctx);
    void collect_dependencies(std::set<std::string>& deps) override;
};

// Conditional rendering in view (if/else)
struct ViewIfStatement : ASTNode {
    std::unique_ptr<Expression> condition;
    std::vector<std::unique_ptr<ASTNode>> then_children;
    std::vector<std::unique_ptr<ASTNode>> else_children;
    int if_id = -1;

    void generate_code(ViewCodegenContext& ctx);
    void collect_dependencies(std::set<std::string>& deps) override;
};

// For range loop in view (for i in 0:10)
struct ViewForRangeStatement : ASTNode {
    std::string var_name;
    std::unique_ptr<Expression> start;
    std::unique_ptr<Expression> end;
    std::vector<std::unique_ptr<ASTNode>> children;
    int loop_id = -1;

    void generate_code(ViewCodegenContext& ctx);
    void collect_dependencies(std::set<std::string>& deps) override;
};

// For each loop in view (for item in items)
struct ViewForEachStatement : ASTNode {
    std::string var_name;
    std::unique_ptr<Expression> iterable;
    std::unique_ptr<Expression> key_expr;
    std::vector<std::unique_ptr<ASTNode>> children;
    int loop_id = -1;
    bool is_only_child = false;  // Set by parent HTMLElement if this loop is its only child

    void generate_code(ViewCodegenContext& ctx);
    void collect_dependencies(std::set<std::string>& deps) override;
};

// Raw HTML injection in view - <raw>{htmlString}</raw>
struct ViewRawElement : ASTNode {
    std::vector<std::unique_ptr<ASTNode>> children;
    int raw_id = -1;

    void generate_code(ViewCodegenContext& ctx);
    void collect_dependencies(std::set<std::string>& deps) override;
};

// what a view's <lang key="..."> is made into: its nodes, put in its parent's place by the
// parser, so nothing after the parser ever sees one
struct ViewFragment : ASTNode {
    std::vector<std::unique_ptr<ASTNode>> children;
};

// Route placeholder for router block - <route /> in view
struct RoutePlaceholder : ASTNode {
    int line = 0;
};
