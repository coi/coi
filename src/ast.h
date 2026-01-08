#pragma once

#include <string>
#include <vector>
#include <memory>
#include <set>
#include <map>
#include <sstream>
#include <functional>
#include <tuple>

std::string convert_type(const std::string& type);

// Represents a dependency on a member of an object (e.g., net.connected)
struct MemberDependency {
    std::string object;   // e.g., "net"
    std::string member;   // e.g., "connected"
    
    bool operator<(const MemberDependency& other) const {
        if (object != other.object) return object < other.object;
        return member < other.member;
    }
};

struct ASTNode {
    virtual ~ASTNode() = default;
    virtual std::string to_webcc() = 0;
    virtual void collect_dependencies(std::set<std::string>& deps) {}
    virtual void collect_member_dependencies(std::set<MemberDependency>& member_deps) {}
    int line = 0;
};

struct Expression : ASTNode {
    virtual bool is_static() { return false; }
};

struct Statement : ASTNode{};

struct IntLiteral : Expression {
    int value;
    IntLiteral(int v) : value(v){}
    std::string to_webcc() override;
    bool is_static() override { return true; }
};

struct FloatLiteral : Expression {
    float value;
    FloatLiteral(float v) : value(v){}
    std::string to_webcc() override;
    bool is_static() override { return true; }
};

struct StringLiteral : Expression {
    std::string value;
    StringLiteral(const std::string& v) : value(v){}
    
    struct Part {
        bool is_expr;
        std::string content;
    };

    std::vector<Part> parse();
    std::string to_webcc() override;
    bool is_static() override;
    void collect_dependencies(std::set<std::string>& deps) override;
};

struct Identifier : Expression {
    std::string name;
    Identifier(const std::string& n) : name(n) {}
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
};

struct BinaryOp : Expression {
    std::unique_ptr<Expression> left;
    std::string op;
    std::unique_ptr<Expression> right;

    BinaryOp(std::unique_ptr<Expression> l, const std::string& o, std::unique_ptr<Expression> r);
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
};

struct FunctionCall : Expression {
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;

    FunctionCall(const std::string& n) : name(n){}
    std::string args_to_string();
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
};

struct MemberAccess : Expression {
    std::unique_ptr<Expression> object;
    std::string member;

    MemberAccess(std::unique_ptr<Expression> obj, const std::string& mem);
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    void collect_member_dependencies(std::set<MemberDependency>& member_deps) override;
};

struct PostfixOp : Expression {
    std::unique_ptr<Expression> operand;
    std::string op;

    PostfixOp(std::unique_ptr<Expression> expr, const std::string& o);
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    bool is_static() override { return false; }
};

struct UnaryOp : Expression {
    std::string op;
    std::unique_ptr<Expression> operand;

    UnaryOp(const std::string& o, std::unique_ptr<Expression> expr);
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    bool is_static() override;
};

struct ArrayLiteral : Expression {
    std::vector<std::unique_ptr<Expression>> elements;
    std::string element_type;  // Inferred or specified type of elements

    ArrayLiteral() = default;
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    bool is_static() override;
};

// Fixed-size array repeat initializer: [value; count] e.g., [0; 100] creates array<int, 100> filled with 0
struct ArrayRepeatLiteral : Expression {
    std::unique_ptr<Expression> value;  // The value to repeat
    int count;  // Number of times to repeat (must be compile-time constant)

    ArrayRepeatLiteral() = default;
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    bool is_static() override;
};

// Component construction expression: NetworkManager(&url: currentUrl, port: 8080)
struct ComponentArg {
    std::string name;                      // Parameter name
    std::unique_ptr<Expression> value;     // Value to pass
    bool is_reference = false;             // & prefix means pass by reference
};

struct ComponentConstruction : Expression {
    std::string component_name;
    std::vector<ComponentArg> args;

    ComponentConstruction(const std::string& name) : component_name(name) {}
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
};

struct IndexAccess : Expression {
    std::unique_ptr<Expression> array;
    std::unique_ptr<Expression> index;

    IndexAccess(std::unique_ptr<Expression> arr, std::unique_ptr<Expression> idx);
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
};

struct VarDeclaration : Statement {
    std::string type;
    std::string name;
    std::unique_ptr<Expression> initializer;
    bool is_mutable = false;
    bool is_reference = false;
    bool is_public = false;

    std::string to_webcc() override;
};

// Component constructor parameter (e.g., component Counter(mut int& count = 0))
struct ComponentParam : Statement {
    std::string type;
    std::string name;
    std::unique_ptr<Expression> default_value;
    bool is_mutable = false;
    bool is_reference = false;
    bool is_public = false;  // pub keyword makes param accessible from outside
    std::vector<std::string> callback_param_types;  // For def params: stores parameter types (e.g., ["int"] for def onRemove(int) : void)
    bool is_callback = false;  // True if this is a def (callback) parameter

    std::string to_webcc() override;
};

struct Assignment : Statement {
    std::string name;
    std::unique_ptr<Expression> value;
    std::string target_type;  // Type of the variable being assigned to (for handle casts)

    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
};

struct IndexAssignment : Statement {
    std::unique_ptr<Expression> array;
    std::unique_ptr<Expression> index;
    std::unique_ptr<Expression> value;

    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
};

struct ReturnStatement : Statement {
    std::unique_ptr<Expression> value;
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
};

struct ExpressionStatement : Statement {
    std::unique_ptr<Expression> expression;
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
};

struct BlockStatement : Statement {
    std::vector<std::unique_ptr<Statement>> statements;
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
};

struct IfStatement : Statement {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Statement> then_branch;
    std::unique_ptr<Statement> else_branch;

    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
};

struct ForRangeStatement : Statement {
    std::string var_name;
    std::unique_ptr<Expression> start;
    std::unique_ptr<Expression> end;
    std::unique_ptr<Statement> body;

    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
};

struct ForEachStatement : Statement {
    std::string var_name;
    std::unique_ptr<Expression> iterable;
    std::unique_ptr<Statement> body;

    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
};

void collect_mods_recursive(Statement* stmt, std::set<std::string>& mods);

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

struct StructField {
    std::string type;
    std::string name;
};

struct StructDef : ASTNode {
    std::string name;
    std::vector<StructField> fields;

    std::string to_webcc() override;
};

struct TextNode : ASTNode {
    std::string text;
    TextNode(const std::string& t) : text(t) {}
    std::string to_webcc() override;
};

struct HTMLAttribute {
    std::string name;
    std::unique_ptr<Expression> value;
};

struct Binding {
    int element_id;
    std::string type; // "attr" or "text"
    std::string name; // attribute name
    std::string value_code;
    std::set<std::string> dependencies;
    Expression* expr = nullptr;
    int if_region_id = -1;      // If >= 0, this binding is inside an if region
    bool in_then_branch = true; // True if in then branch, false if in else branch
};

struct ComponentProp {
    std::string name;
    std::unique_ptr<Expression> value;
    bool is_reference = false;
    bool is_mutable_def = false;
};

// Struct to track reactive loop regions 
struct LoopRegion {
    int loop_id;
    std::string parent_element;     // Element handle to append children to
    std::string component_type;     // Component type in the loop (or empty for HTML elements)
    std::string start_expr;         // Start expression code
    std::string end_expr;           // End expression code  
    std::set<std::string> dependencies;  // Variables this loop depends on
    std::string item_creation_code; // Code to create one loop item
    std::string item_update_code;   // Code to update an existing item's props (for reconciliation)
    std::string var_name;           // Loop variable name
    std::string root_element_var;   // For HTML-only loops: the variable name of the root element (e.g., "_el_7")
    bool is_html_loop = false;      // True if this loop contains HTML elements (not components)
    bool is_keyed = false;          // True if this loop uses key-based diffing
    std::string key_expr;           // Key expression code (e.g., "item.id")
    std::string key_type;           // Type of the key (e.g., "int", "string")
    std::string iterable_expr;      // For foreach loops: the array expression
};

// Struct to track reactive if/else regions
struct IfRegion {
    int if_id;
    std::string condition_code;         // The condition expression code
    std::set<std::string> dependencies; // Variables this if depends on
    std::set<MemberDependency> member_dependencies; // Child member access (e.g., net.connected)
    std::string then_creation_code;     // Code to create then branch elements
    std::string else_creation_code;     // Code to create else branch elements
    std::string then_destroy_code;      // Code to destroy then branch elements
    std::string else_destroy_code;      // Code to destroy else branch elements
    std::vector<int> then_element_ids;  // Element IDs created in then branch
    std::vector<int> else_element_ids;  // Element IDs created in else branch
    std::vector<std::pair<std::string, int>> then_components; // Component types and instance IDs in then branch
    std::vector<std::pair<std::string, int>> else_components; // Component types and instance IDs in else branch
    std::vector<int> then_loop_ids;     // Loop IDs in then branch
    std::vector<int> else_loop_ids;     // Loop IDs in else branch
    std::vector<int> then_if_ids;       // Nested if IDs in then branch
    std::vector<int> else_if_ids;       // Nested if IDs in else branch
};

struct ComponentInstantiation : ASTNode {
    std::string component_name;
    std::vector<ComponentProp> props;
    
    std::string to_webcc() override;

    void generate_code(std::stringstream& ss, const std::string& parent, int& counter, 
                      std::vector<std::tuple<int, std::string, bool>>& click_handlers,
                      std::vector<Binding>& bindings,
                      std::map<std::string, int>& component_counters,
                      const std::set<std::string>& method_names,
                      const std::string& parent_component_name,
                      bool in_loop = false,
                      std::vector<LoopRegion>* loop_regions = nullptr,
                      int* loop_counter = nullptr,
                      std::vector<IfRegion>* if_regions = nullptr,
                      int* if_counter = nullptr);
    
    void collect_dependencies(std::set<std::string>& deps) override;
};

struct HTMLElement : ASTNode {
    std::string tag;
    std::vector<HTMLAttribute> attributes;
    std::vector<std::unique_ptr<ASTNode>> children;
    std::string ref_binding;  // Variable name to bind this element to (e.g., &={canvas})

    std::string to_webcc() override;

    void generate_code(std::stringstream& ss, const std::string& parent, int& counter, 
                      std::vector<std::tuple<int, std::string, bool>>& click_handlers,
                      std::vector<Binding>& bindings,
                      std::map<std::string, int>& component_counters,
                      const std::set<std::string>& method_names,
                      const std::string& parent_component_name,
                      bool in_loop = false,
                      std::vector<LoopRegion>* loop_regions = nullptr,
                      int* loop_counter = nullptr,
                      std::vector<IfRegion>* if_regions = nullptr,
                      int* if_counter = nullptr);
    void collect_dependencies(std::set<std::string>& deps) override;
};

   

struct Component : ASTNode {
    std::string name;
    std::string css;
    std::string global_css;
    std::vector<std::unique_ptr<StructDef>> structs;
    std::vector<std::unique_ptr<VarDeclaration>> state;
    std::vector<std::unique_ptr<ComponentParam>> params;  // Constructor parameters
    std::vector<FunctionDef> methods;
    std::vector<std::unique_ptr<ASTNode>> render_roots;

    void collect_child_components(ASTNode* node, std::map<std::string, int>& counts);
    void collect_child_updates(ASTNode* node, std::map<std::string, std::vector<std::string>>& updates, std::map<std::string, int>& counters);
    std::string to_webcc() override;
};

struct AppConfig {
    std::string root_component;
    std::map<std::string, std::string> routes;
};

struct BoolLiteral : Expression {
    bool value;
    BoolLiteral(bool v) : value(v){}
    std::string to_webcc() override { return value ? "true" : "false"; }
    bool is_static() override { return true; }
};

// Conditional rendering in view (if/else)
struct ViewIfStatement : ASTNode {
    std::unique_ptr<Expression> condition;
    std::vector<std::unique_ptr<ASTNode>> then_children;
    std::vector<std::unique_ptr<ASTNode>> else_children;
    int if_id = -1;  // Assigned during code generation

    std::string to_webcc() override { return ""; }
    void generate_code(std::stringstream& ss, const std::string& parent, int& counter, 
                      std::vector<std::tuple<int, std::string, bool>>& click_handlers,
                      std::vector<Binding>& bindings,
                      std::map<std::string, int>& component_counters,
                      const std::set<std::string>& method_names,
                      const std::string& parent_component_name,
                      bool in_loop = false,
                      std::vector<LoopRegion>* loop_regions = nullptr,
                      int* loop_counter = nullptr,
                      std::vector<IfRegion>* if_regions = nullptr,
                      int* if_counter = nullptr);
    void collect_dependencies(std::set<std::string>& deps) override;
};

// For range loop in view (for i in 0:10)
struct ViewForRangeStatement : ASTNode {
    std::string var_name;
    std::unique_ptr<Expression> start;
    std::unique_ptr<Expression> end;
    std::vector<std::unique_ptr<ASTNode>> children;
    int loop_id = -1;  // Assigned during code generation

    std::string to_webcc() override { return ""; }
    void generate_code(std::stringstream& ss, const std::string& parent, int& counter, 
                      std::vector<std::tuple<int, std::string, bool>>& click_handlers,
                      std::vector<Binding>& bindings,
                      std::map<std::string, int>& component_counters,
                      const std::set<std::string>& method_names,
                      const std::string& parent_component_name,
                      bool in_loop = false,
                      std::vector<LoopRegion>* loop_regions = nullptr,
                      int* loop_counter = nullptr,
                      std::vector<IfRegion>* if_regions = nullptr,
                      int* if_counter = nullptr);
    void collect_dependencies(std::set<std::string>& deps) override;
};

// For each loop in view (for item in items)
struct ViewForEachStatement : ASTNode {
    std::string var_name;
    std::unique_ptr<Expression> iterable;
    std::unique_ptr<Expression> key_expr;  // Optional key expression for efficient diffing
    std::vector<std::unique_ptr<ASTNode>> children;
    int loop_id = -1;  // Assigned during code generation

    std::string to_webcc() override { return ""; }
    void generate_code(std::stringstream& ss, const std::string& parent, int& counter, 
                      std::vector<std::tuple<int, std::string, bool>>& click_handlers,
                      std::vector<Binding>& bindings,
                      std::map<std::string, int>& component_counters,
                      const std::set<std::string>& method_names,
                      const std::string& parent_component_name,
                      bool in_loop = false,
                      std::vector<LoopRegion>* loop_regions = nullptr,
                      int* loop_counter = nullptr,
                      std::vector<IfRegion>* if_regions = nullptr,
                      int* if_counter = nullptr);
    void collect_dependencies(std::set<std::string>& deps) override;
};
