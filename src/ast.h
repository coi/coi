#pragma once

#include <string>
#include <vector>
#include <memory>
#include <set>
#include <map>
#include <sstream>
#include <functional>

std::string convert_type(const std::string& type);

struct ASTNode {
    virtual ~ASTNode() = default;
    virtual std::string to_webcc() = 0;
    virtual void collect_dependencies(std::set<std::string>& deps) {}
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

struct VarDeclaration : Statement {
    std::string type;
    std::string name;
    std::unique_ptr<Expression> initializer;

    std::string to_webcc() override;
};

struct PropDeclaration : Statement {
    std::string type;
    std::string name;
    std::unique_ptr<Expression> default_value;

    std::string to_webcc() override;
};

struct Assignment : Statement {
    std::string name;
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

void collect_mods_recursive(Statement* stmt, std::set<std::string>& mods);

struct FunctionDef {
    std::string name;
    std::string return_type;
    std::vector<std::pair<std::string, std::string>> params;
    std::vector<std::unique_ptr<Statement>> body;

    std::string to_webcc(const std::string& injected_code = "");
    void collect_modifications(std::set<std::string>& mods);
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
};

struct ComponentInstantiation : ASTNode {
    std::string component_name;
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> props;
    
    std::string to_webcc() override;

    void generate_code(std::stringstream& ss, const std::string& parent, int& counter, 
                      std::vector<std::pair<int, std::string>>& click_handlers,
                      std::vector<Binding>& bindings,
                      std::map<std::string, int>& component_counters,
                      const std::set<std::string>& method_names,
                      const std::string& parent_component_name);
    
    void collect_dependencies(std::set<std::string>& deps) override;
};

struct HTMLElement : ASTNode {
    std::string tag;
    std::vector<HTMLAttribute> attributes;
    std::vector<std::unique_ptr<ASTNode>> children;

    std::string to_webcc() override;

    void generate_code(std::stringstream& ss, const std::string& parent, int& counter, 
                      std::vector<std::pair<int, std::string>>& click_handlers,
                      std::vector<Binding>& bindings,
                      std::map<std::string, int>& component_counters,
                      const std::set<std::string>& method_names,
                      const std::string& parent_component_name);
    void collect_dependencies(std::set<std::string>& deps) override;
};

struct Component : ASTNode {
    std::string name;
    std::string css;
    std::string global_css;
    std::vector<std::unique_ptr<StructDef>> structs;
    std::vector<std::unique_ptr<VarDeclaration>> state;
    std::vector<std::unique_ptr<PropDeclaration>> props;
    std::vector<FunctionDef> methods;
    std::unique_ptr<ASTNode> render_root;

    void collect_child_components(ASTNode* node, std::map<std::string, int>& counts);
    std::string to_webcc() override;
};

struct AppConfig {
    std::string root_component;
    std::map<std::string, std::string> routes;
};
