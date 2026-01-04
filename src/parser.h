#pragma once

#include "token.h"
#include "ast.h"
#include <vector>
#include <memory>
#include <string>

class Parser{
    private:
        std::vector<Token> tokens;
        size_t pos = 0;

        Token current();
        Token peek(int offset = 1);
        void advance();
        bool match(TokenType type);
        void expect(TokenType type, const std::string& msg);

        std::unique_ptr<Expression> parse_expression();
        std::unique_ptr<Expression> parse_equality();
        std::unique_ptr<Expression> parse_comparison();
        std::unique_ptr<Expression> parse_additive();
        std::unique_ptr<Expression> parse_postfix();
        std::unique_ptr<Expression> parse_unary();
        std::unique_ptr<Expression> parse_multiplicative();
        std::unique_ptr<Expression> parse_primary();
        std::unique_ptr<Statement> parse_statement();
        std::unique_ptr<StructDef> parse_struct();
        std::string parse_style_block();
        std::unique_ptr<ASTNode> parse_html_element();
        Component parse_component();
        void parse_app();

    public:
        std::vector<Component> components;
        AppConfig app_config;
        Parser(const std::vector<Token>& toks);
        void parse_file();
};
