#pragma once

#include "node.h"
#include "expressions.h"
#include <map>

struct TextNode : ASTNode {
    std::string text;
    TextNode(const std::string& t) : text(t) {}
    std::string to_webcc() override;
};

struct HTMLAttribute {
    std::string name;
    std::unique_ptr<Expression> value;
};

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

// Deferred handles for one generated snippet, reserved in a single call when
// the snippet runs. Each snippet gets its own name so pasting one snippet
// into another never redeclares the variable.
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

    // Create a child context with a new parent element
    ViewCodegenContext with_parent(const std::string& new_parent) const {
        return ViewCodegenContext{ss, new_parent, counter, event_handlers, bindings,
            component_counters, method_names, parent_component_name, in_loop,
            loop_regions, loop_counter, if_regions, if_counter, loop_var_name, handles};
    }

    // Create a context for loop iteration (in_loop = true, clear region pointers).
    // The body runs once per iteration, so it writes to its own snippet.
    ViewCodegenContext for_loop(std::stringstream& body_ss, HandleBlock& body_handles,
                                const std::string& new_parent, const std::string& var_name) const {
        return ViewCodegenContext{body_ss, new_parent, counter, event_handlers, bindings,
            component_counters, method_names, parent_component_name, true,
            nullptr, nullptr, nullptr, nullptr, var_name, &body_handles};
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

// Route placeholder for router block - <route /> in view
struct RoutePlaceholder : ASTNode {
    int line = 0;
};
