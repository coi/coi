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
};

struct Assignment : Statement {
    std::string name;
    std::unique_ptr<Expression> value;
    std::string target_type;
    bool is_move = false;  // true if assigned with &expr (move semantics)

    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
};

struct IndexAssignment : Statement {
    std::unique_ptr<Expression> array;
    std::unique_ptr<Expression> index;
    std::unique_ptr<Expression> value;
    std::string compound_op;
    bool is_move = false;  // true if assigned with &expr (move semantics)

    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
};

struct MemberAssignment : Statement {
    std::unique_ptr<Expression> object;
    std::string member;
    std::unique_ptr<Expression> value;
    std::string compound_op;
    bool is_move = false;  // true if assigned with &expr (move semantics)

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

// Recursively collect modified variables in statements
void collect_mods_recursive(Statement* stmt, std::set<std::string>& mods);
