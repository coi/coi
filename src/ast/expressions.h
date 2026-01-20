#pragma once

#include "node.h"

struct IntLiteral : Expression {
    int value;
    IntLiteral(int v) : value(v){}
    std::string to_webcc() override;
    bool is_static() override { return true; }
};

struct FloatLiteral : Expression {
    double value;
    FloatLiteral(double v) : value(v){}
    std::string to_webcc() override;
    bool is_static() override { return true; }
};

struct BoolLiteral : Expression {
    bool value;
    BoolLiteral(bool v) : value(v){}
    std::string to_webcc() override { return value ? "true" : "false"; }
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
    void collect_member_dependencies(std::set<MemberDependency>& member_deps) override;
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

// Unified argument for function calls and component construction
// Supports both positional and named arguments with reference/move semantics
struct CallArg {
    std::string name;  // Empty for positional arguments
    std::unique_ptr<Expression> value;
    bool is_reference = false;
    bool is_move = false;
};

struct FunctionCall : Expression {
    std::string name;
    std::vector<CallArg> args;
    int line = 0;

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

// Reference expression: &expr - explicitly passes by reference (borrow)
struct ReferenceExpression : Expression {
    std::unique_ptr<Expression> operand;

    ReferenceExpression(std::unique_ptr<Expression> expr) : operand(std::move(expr)) {}
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    bool is_static() override { return false; }
};

// Move expression: :expr - explicitly transfers ownership
struct MoveExpression : Expression {
    std::unique_ptr<Expression> operand;

    MoveExpression(std::unique_ptr<Expression> expr) : operand(std::move(expr)) {}
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    bool is_static() override { return false; }
};

struct TernaryOp : Expression {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> true_expr;
    std::unique_ptr<Expression> false_expr;

    TernaryOp(std::unique_ptr<Expression> cond, std::unique_ptr<Expression> t, std::unique_ptr<Expression> f);
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

// Fixed-size array repeat initializer: [value; count] e.g., [0; 100]
struct ArrayRepeatLiteral : Expression {
    std::unique_ptr<Expression> value;  // The value to repeat
    int count;  // Number of times to repeat

    ArrayRepeatLiteral() = default;
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    bool is_static() override;
};

struct IndexAccess : Expression {
    std::unique_ptr<Expression> array;
    std::unique_ptr<Expression> index;

    IndexAccess(std::unique_ptr<Expression> arr, std::unique_ptr<Expression> idx);
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
};

// Enum value access: Mode::Idle or App.Mode::Idle
struct EnumAccess : Expression {
    std::string enum_name;      // e.g., "Mode"
    std::string value_name;     // e.g., "Idle"
    std::string component_name; // e.g., "App" (empty for local/global enums)

    EnumAccess(const std::string& enum_n, const std::string& val_n, const std::string& comp_n = "")
        : enum_name(enum_n), value_name(val_n), component_name(comp_n) {}
    std::string to_webcc() override;
    bool is_static() override { return true; }
};

// ComponentArg is now an alias for CallArg for backwards compatibility
using ComponentArg = CallArg;

// Component construction expression: NetworkManager(&url = currentUrl, port := 8080)
// Also supports positional args: NetworkManager(&value, :value, value)
struct ComponentConstruction : Expression {
    std::string component_name;
    std::vector<CallArg> args;

    ComponentConstruction(const std::string& name) : component_name(name) {}
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
};
