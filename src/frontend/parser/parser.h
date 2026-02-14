#pragma once

#include "../token.h"
#include "ast/ast.h"
#include <vector>
#include <memory>
#include <string>
#include <map>

// Represents an import declaration
struct ImportDecl {
    std::string path;      // The import path
    bool is_public;        // Whether this is a pub import (re-export)
    
    ImportDecl(const std::string& p, bool pub = false) : path(p), is_public(pub) {}
};

class Parser{
    private:
        std::vector<Token> tokens;
        size_t pos = 0;
        bool allow_gt_comparison = true;  // When false, > is not treated as comparison op
        bool allow_brace_init = true;     // When false, Name{ is not treated as data literal
        
        // Maps member variable names to their component types (for detecting <memberName/> in views)
        std::map<std::string, std::string> component_member_types;
        
        // Maps member variable names to their component array element types (e.g., "rows" -> "Row" for Row[] rows)
        std::map<std::string, std::string> component_array_types;

        Token current();
        Token peek(int offset = 1);
        void advance();
        bool match(TokenType type);
        void expect(TokenType type, const std::string& msg);
        
        // Helper methods
        bool is_type_token();                    // INT, STRING, FLOAT, FLOAT32, BOOL, IDENTIFIER, VOID
        bool is_identifier_token();              // IDENTIFIER, KEY, DATA (keywords usable as names)
        std::vector<CallArg> parse_call_args(TokenType end_token);  // Parse (args) or {args}

        std::unique_ptr<Expression> parse_expression();
        std::unique_ptr<Expression> parse_expression_no_gt();  // Parse expression without > as comparison
        std::unique_ptr<Expression> parse_ternary();
        std::unique_ptr<Expression> parse_or();
        std::unique_ptr<Expression> parse_and();
        std::unique_ptr<Expression> parse_bitwise_or();
        std::unique_ptr<Expression> parse_bitwise_xor();
        std::unique_ptr<Expression> parse_bitwise_and();
        std::unique_ptr<Expression> parse_equality();
        std::unique_ptr<Expression> parse_comparison();
        std::unique_ptr<Expression> parse_shift();
        std::unique_ptr<Expression> parse_additive();
        std::unique_ptr<Expression> parse_postfix();
        std::unique_ptr<Expression> parse_unary();
        std::unique_ptr<Expression> parse_multiplicative();
        std::unique_ptr<Expression> parse_primary();
        std::unique_ptr<Expression> parse_match();  // Parse match expression
        std::unique_ptr<Expression> parse_prop_or_attr_value();
        std::unique_ptr<Statement> parse_statement();
        std::unique_ptr<DataDef> parse_data();
        std::unique_ptr<EnumDef> parse_enum();
        std::string parse_style_block();
        std::unique_ptr<ASTNode> parse_html_element();
        std::unique_ptr<ASTNode> parse_view_node();  // Parse HTML element or view if/else/for
        std::unique_ptr<ViewIfStatement> parse_view_if();  // Parse if/else in view
        std::unique_ptr<ASTNode> parse_view_for();  // Parse for loop in view
        std::unique_ptr<RouterDef> parse_router();  // Parse router block
        Component parse_component();
        void parse_app();

    public:
        std::string module_name;  // Current file's module (defaults to empty)
        std::vector<Component> components;
        std::vector<std::unique_ptr<DataDef>> global_data;    // Data types declared outside components
        std::vector<std::unique_ptr<EnumDef>> global_enums;  // Enums declared outside components
        std::vector<ImportDecl> imports;  // Import declarations (path + pub status)
        AppConfig app_config;
        Parser(const std::vector<Token>& toks);
        void parse_file();
};
