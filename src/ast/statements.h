#pragma once

#include "node.h"
#include "expressions.h"

struct VarDeclaration : Statement {
    std::string type;
    std::string name;
    std::unique_ptr<Expression> initializer;
    bool is_mutable = false;
    bool is_reference = false;
    bool is_public = false;
    bool is_move = false;  // true if initialized with &expr (move semantics)

    std::string to_webcc() override;
    std::vector<ASTNode*> get_child_nodes() override { return {initializer.get()}; }
};

// Component constructor parameter
struct ComponentParam : Statement {
    std::string type;
    std::string name;
    std::unique_ptr<Expression> default_value;
    bool is_mutable = false;
    bool is_reference = false;
    bool is_public = false;
    std::vector<std::string> callback_param_types;
    bool is_callback = false;

    std::string to_webcc() override;
    std::vector<ASTNode*> get_child_nodes() override { return {default_value.get()}; }
};

struct Assignment : Statement {
    std::string name;
    std::unique_ptr<Expression> value;
    std::string target_type;
    bool is_move = false;  // true if assigned with &expr (move semantics)

    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    std::vector<ASTNode*> get_child_nodes() override { return {value.get()}; }
};

struct IndexAssignment : Statement {
    std::unique_ptr<Expression> array;
    std::unique_ptr<Expression> index;
    std::unique_ptr<Expression> value;
    std::string compound_op;
    bool is_move = false;  // true if assigned with &expr (move semantics)

    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    std::vector<ASTNode*> get_child_nodes() override { return {array.get(), index.get(), value.get()}; }
};

struct MemberAssignment : Statement {
    std::unique_ptr<Expression> object;
    std::string member;
    std::unique_ptr<Expression> value;
    std::string compound_op;
    bool is_move = false;  // true if assigned with &expr (move semantics)

    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    std::vector<ASTNode*> get_child_nodes() override { return {object.get(), value.get()}; }
};

struct ReturnStatement : Statement {
    std::unique_ptr<Expression> value;

    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    std::vector<ASTNode*> get_child_nodes() override { return {value.get()}; }
};

struct ExpressionStatement : Statement {
    std::unique_ptr<Expression> expression;
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    std::vector<ASTNode*> get_child_nodes() override { return {expression.get()}; }
};

struct BlockStatement : Statement {
    std::vector<std::unique_ptr<Statement>> statements;
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    std::vector<ASTNode*> get_child_nodes() override {
        std::vector<ASTNode*> nodes;
        for (auto& s : statements) nodes.push_back(s.get());
        return nodes;
    }
};

struct IfStatement : Statement {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Statement> then_branch;
    std::unique_ptr<Statement> else_branch;

    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    std::vector<ASTNode*> get_child_nodes() override { return {condition.get(), then_branch.get(), else_branch.get()}; }
};

struct ForRangeStatement : Statement {
    std::string var_name;
    std::unique_ptr<Expression> start;
    std::unique_ptr<Expression> end;
    std::unique_ptr<Statement> body;

    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    std::vector<ASTNode*> get_child_nodes() override { return {start.get(), end.get(), body.get()}; }
};

struct ForEachStatement : Statement {
    std::string var_name;
    std::unique_ptr<Expression> iterable;
    std::unique_ptr<Statement> body;

    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    std::vector<ASTNode*> get_child_nodes() override { return {iterable.get(), body.get()}; }
};

struct EmitStatement : Statement {
    std::string signal_name;
    std::vector<std::unique_ptr<Expression>> args;

    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    std::vector<ASTNode*> get_child_nodes() override {
        std::vector<ASTNode*> nodes;
        for (auto& a : args) nodes.push_back(a.get());
        return nodes;
    }
};

// Recursively collect the component fields a subtree mutates. Walks any node via
// get_child_nodes(), so new node types are covered without editing this walk.
void collect_mods_recursive(ASTNode* node, std::set<std::string>& mods);
