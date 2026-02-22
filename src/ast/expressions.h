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
    bool is_template = false;  // true for backtick strings, false for double-quote strings
    StringLiteral(const std::string& v, bool tmpl = false) : value(v), is_template(tmpl){}
    
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

// Type literal expression (for passing types as arguments, e.g., Json.parse(User[], ...))
struct TypeLiteral : Expression {
    std::string type_name;  // e.g., "User" or "User[]"
    TypeLiteral(const std::string& t) : type_name(t) {}
    std::string to_webcc() override { return type_name; }
    bool is_static() override { return true; }
};

struct BinaryOp : Expression {
    std::unique_ptr<Expression> left;
    std::string op;
    std::unique_ptr<Expression> right;

    BinaryOp(std::unique_ptr<Expression> l, const std::string& o, std::unique_ptr<Expression> r);
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    void collect_member_dependencies(std::set<MemberDependency>& member_deps) override;
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
    void collect_member_dependencies(std::set<MemberDependency>& member_deps) override;
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
    void collect_dependencies(std::set<std::string>& deps) override { operand->collect_dependencies(deps); }
    void collect_member_dependencies(std::set<MemberDependency>& member_deps) override { operand->collect_member_dependencies(member_deps); }
};

struct UnaryOp : Expression {
    std::string op;
    std::unique_ptr<Expression> operand;

    UnaryOp(const std::string& o, std::unique_ptr<Expression> expr);
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override { operand->collect_dependencies(deps); }
    void collect_member_dependencies(std::set<MemberDependency>& member_deps) override { operand->collect_member_dependencies(member_deps); }
    bool is_static() override;
};

// Reference expression: &expr - explicitly passes by reference (borrow)
struct ReferenceExpression : Expression {
    std::unique_ptr<Expression> operand;

    ReferenceExpression(std::unique_ptr<Expression> expr) : operand(std::move(expr)) {}
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override { operand->collect_dependencies(deps); }
    void collect_member_dependencies(std::set<MemberDependency>& member_deps) override { operand->collect_member_dependencies(member_deps); }
};

// Move expression: :expr - explicitly transfers ownership
struct MoveExpression : Expression {
    std::unique_ptr<Expression> operand;

    MoveExpression(std::unique_ptr<Expression> expr) : operand(std::move(expr)) {}
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override { operand->collect_dependencies(deps); }
    void collect_member_dependencies(std::set<MemberDependency>& member_deps) override { operand->collect_member_dependencies(member_deps); }
};

struct TernaryOp : Expression {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> true_expr;
    std::unique_ptr<Expression> false_expr;

    TernaryOp(std::unique_ptr<Expression> cond, std::unique_ptr<Expression> t, std::unique_ptr<Expression> f);
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    void collect_member_dependencies(std::set<MemberDependency>& member_deps) override;
    bool is_static() override;
};

struct ArrayLiteral : Expression {
    std::vector<std::unique_ptr<Expression>> elements;
    std::string element_type;  // Inferred or specified type of elements

    ArrayLiteral() = default;
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    bool is_static() override;
    
    // Propagate element type to anonymous struct literals (ComponentConstruction with empty name)
    void propagate_element_type(const std::string& type);
};

// Fixed-size array repeat initializer: [value; count] e.g., [0; 100] or [0; NUM_ITEMS]
struct ArrayRepeatLiteral : Expression {
    std::unique_ptr<Expression> value;  // The value to repeat
    std::unique_ptr<Expression> count;  // Count expression (must be compile-time constant integer)

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
    void collect_member_dependencies(std::set<MemberDependency>& member_deps) override;
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

// Match pattern for pattern matching in match expressions
// Supports: enum patterns (Role::Admin), pod patterns (User{name = "x"}), 
// pod capture patterns (User{name}), literal patterns (42, "hello"), and else (default) pattern
struct MatchPattern {
    enum class Kind {
        Literal,    // Direct value match: 42, "hello", true
        Enum,       // EnumType::Value
        Pod,        // PodType{field = value, ...} or PodType{field, ...} (capture/binding)
        Variant,    // Variant(arg, ...) e.g., Success(User user, Meta meta)
        Else        // else (default case)
    };
    
    Kind kind = Kind::Else;
    std::string type_name;  // Enum or Pod type name (empty for Else/Literal)
    std::string enum_value; // For Kind::Enum, the enum variant name
    std::unique_ptr<Expression> literal_value;  // For Kind::Literal, the value to match
    
    // For Kind::Pod: field patterns - name is field name, value is the match expression 
    // (nullptr means capture/bind the field to a local variable)
    struct FieldPattern {
        std::string name;
        std::unique_ptr<Expression> value;  // nullptr for binding pattern like User{name}
    };
    std::vector<FieldPattern> fields;

    // For Kind::Variant: typed bindings in constructor-like patterns
    struct VariantBinding {
        std::string type;
        std::string name;
    };
    std::vector<VariantBinding> variant_bindings;
    
    MatchPattern() = default;
    MatchPattern(MatchPattern&&) = default;
    MatchPattern& operator=(MatchPattern&&) = default;
};

// A single arm in a match expression
struct MatchArm {
    MatchPattern pattern;
    std::unique_ptr<Expression> body;
    int line = 0;
    
    MatchArm() = default;
    MatchArm(MatchArm&&) = default;
    MatchArm& operator=(MatchArm&&) = default;
};

// Match expression: match (subject) { pattern => result, ... }
struct MatchExpr : Expression {
    std::unique_ptr<Expression> subject;
    std::vector<MatchArm> arms;
    int line = 0;
    
    MatchExpr() = default;
    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    bool is_static() override;
};

// Block expression for match arm bodies that contain statements
// Generated as an IIFE expression so it can be used where an expression is required.
struct BlockExpr : Expression {
    std::vector<std::unique_ptr<Statement>> statements;

    std::string to_webcc() override;
    void collect_dependencies(std::set<std::string>& deps) override;
    bool is_static() override { return false; }
};
