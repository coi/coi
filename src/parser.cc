#include "parser.h"
#include <stdexcept>
#include <iostream>

Parser::Parser(const std::vector<Token>& toks) : tokens(toks){}

Token Parser::current(){
    return pos < tokens.size() ? tokens[pos] : tokens.back();
}

Token Parser::peek(int offset){
    return (pos + offset) < tokens.size() ? tokens[pos + offset] : tokens.back();
}

void Parser::advance() { pos++; }

bool Parser::match(TokenType type){
    if(current().type == type){
        advance();
        return true;
    }
    return false;
}

void Parser::expect(TokenType type, const std::string& msg){
    if(!match(type)){
        throw std::runtime_error(msg + " at line " + std::to_string(current().line));
    }
}

std::unique_ptr<Expression> Parser::parse_expression(){
    return parse_or();
}

std::unique_ptr<Expression> Parser::parse_expression_no_gt(){
    // Parse expression but don't treat > as comparison operator
    // Used for expressions inside view tags like <if condition>
    bool old_allow_gt = allow_gt_comparison;
    allow_gt_comparison = false;
    auto expr = parse_or();
    allow_gt_comparison = old_allow_gt;
    return expr;
}

std::unique_ptr<Expression> Parser::parse_or(){
    auto left = parse_and();

    while(current().type == TokenType::OR){
        std::string op = current().value;
        advance();
        auto right = parse_and();
        left = std::make_unique<BinaryOp>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_and(){
    auto left = parse_equality();

    while(current().type == TokenType::AND){
        std::string op = current().value;
        advance();
        auto right = parse_equality();
        left = std::make_unique<BinaryOp>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_equality(){
    auto left = parse_comparison();

    while(current().type == TokenType::EQ || current().type == TokenType::NEQ){
        std::string op = current().value;
        advance();
        auto right = parse_comparison();
        left = std::make_unique<BinaryOp>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_comparison(){
    auto left = parse_additive();

    while(current().type == TokenType::LT || 
          (current().type == TokenType::GT && allow_gt_comparison) ||
            current().type == TokenType::LTE || current().type == TokenType::GTE){
        std::string op = current().value;
        advance();
        auto right = parse_additive();
        left = std::make_unique<BinaryOp>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_additive(){
    auto left = parse_multiplicative();

    while(current().type == TokenType::PLUS || current().type == TokenType::MINUS){
        std::string op = current().value;
        advance();
        auto right = parse_multiplicative();
        left = std::make_unique<BinaryOp>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_postfix() {
    auto expr = parse_primary();
    
    while(true) {
        if (current().type == TokenType::PLUS_PLUS) {
            advance();
            expr = std::make_unique<PostfixOp>(std::move(expr), "++");
        } else if (current().type == TokenType::MINUS_MINUS) {
            advance();
            expr = std::make_unique<PostfixOp>(std::move(expr), "--");
        } else {
            break;
        }
    }
    return expr;
}

std::unique_ptr<Expression> Parser::parse_unary() {
    if (current().type == TokenType::MINUS || current().type == TokenType::PLUS || current().type == TokenType::NOT) {
        std::string op = current().value;
        advance();
        auto operand = parse_unary();
        return std::make_unique<UnaryOp>(op, std::move(operand));
    }
    return parse_postfix();
}

std::unique_ptr<Expression> Parser::parse_multiplicative(){
    auto left = parse_unary();

    while(current().type == TokenType::STAR || current().type == TokenType::SLASH || current().type == TokenType::PERCENT){
        std::string op = current().value;
        advance();
        auto right = parse_unary();
        left = std::make_unique<BinaryOp>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_primary(){
    // Integer literal
    if(current().type == TokenType::INT_LITERAL){
        int value = std::stoi(current().value);
        advance();
        return std::make_unique<IntLiteral>(value);
    }

    // Float literal
    if(current().type == TokenType::FLOAT_LITERAL){
        float value = std::stof(current().value);
        advance();
        return std::make_unique<FloatLiteral>(value);
    }

    // String literal
    if(current().type == TokenType::STRING_LITERAL){
        std::string value = current().value;
        advance();
        return std::make_unique<StringLiteral>(value);
    }

    // Boolean literal
    if(current().type == TokenType::TRUE){
        advance();
        return std::make_unique<BoolLiteral>(true);
    }
    if(current().type == TokenType::FALSE){
        advance();
        return std::make_unique<BoolLiteral>(false);
    }

    // Identifer or function call
    if(current().type == TokenType::IDENTIFIER){
        std::string name = current().value;
        advance();
        std::unique_ptr<Expression> expr = std::make_unique<Identifier>(name);

        while(true) {
            if(current().type == TokenType::LPAREN){
                // Call
                advance();
                auto call = std::make_unique<FunctionCall>(expr->to_webcc());
                
                while(current().type != TokenType::RPAREN){
                    call->args.push_back(parse_expression());
                    if(current().type == TokenType::COMMA) advance();
                }
                expect(TokenType::RPAREN, "Expected ')'");
                expr = std::move(call);
            }
            else if(current().type == TokenType::DOT){
                advance();
                std::string member = current().value;
                expect(TokenType::IDENTIFIER, "Expected member name");
                expr = std::make_unique<MemberAccess>(std::move(expr), member);
            }
            else if(current().type == TokenType::LBRACKET){
                // Array index access
                advance();
                auto index = parse_expression();
                expect(TokenType::RBRACKET, "Expected ']'");
                expr = std::make_unique<IndexAccess>(std::move(expr), std::move(index));
            }
            else {
                break;
            }
        }
        return expr;
    }

    // Array literal: [expr, expr, ...] or repeat initializer: [value; count]
    if(current().type == TokenType::LBRACKET){
        advance();
        
        // Empty array
        if(current().type == TokenType::RBRACKET){
            advance();
            return std::make_unique<ArrayLiteral>();
        }
        
        // Parse first expression
        auto first_expr = parse_expression();
        
        // Check for repeat initializer syntax: [value; count]
        if(current().type == TokenType::SEMICOLON){
            advance();
            auto repeat = std::make_unique<ArrayRepeatLiteral>();
            repeat->value = std::move(first_expr);
            
            // Count must be an integer literal (compile-time constant)
            if (current().type != TokenType::INT_LITERAL) {
                throw std::runtime_error("Array repeat count must be an integer literal at line " + std::to_string(current().line));
            }
            repeat->count = std::stoi(current().value);
            advance();
            
            expect(TokenType::RBRACKET, "Expected ']'");
            return repeat;
        }
        
        // Regular array literal
        auto arr = std::make_unique<ArrayLiteral>();
        arr->elements.push_back(std::move(first_expr));
        
        while(current().type == TokenType::COMMA){
            advance();
            if(current().type == TokenType::RBRACKET) break; // Allow trailing comma
            arr->elements.push_back(parse_expression());
        }
        expect(TokenType::RBRACKET, "Expected ']'");
        return arr;
    }

    // Parenthesized expression
    if(match(TokenType::LPAREN)){
        auto expr = parse_expression();
        expect(TokenType::RPAREN, "Expected ')'");
        return expr;
    }

    throw std::runtime_error("Unexpected token in expression: " + current().value + " (Type: " + std::to_string((int)current().type) + ") at line " + std::to_string(current().line));
}

std::unique_ptr<Statement> Parser::parse_statement(){
    // Block
    if(current().type == TokenType::LBRACE){
        advance();
        auto block = std::make_unique<BlockStatement>();
        while(current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE){
            block->statements.push_back(parse_statement());
        }
        expect(TokenType::RBRACE, "Expected '}'");
        return block;
    }

    // If
    if(current().type == TokenType::IF){
        advance();
        expect(TokenType::LPAREN, "Expected '('");
        auto cond = parse_expression();
        expect(TokenType::RPAREN, "Expected ')'");
        
        auto ifStmt = std::make_unique<IfStatement>();
        ifStmt->condition = std::move(cond);
        ifStmt->then_branch = parse_statement();
        
        if(match(TokenType::ELSE)){
            ifStmt->else_branch = parse_statement();
        }
        return ifStmt;
    }

    // For (three syntaxes: traditional, range-based, and foreach)
    if(current().type == TokenType::FOR){
        advance();
        
        // Check for range-based or foreach syntax: for i in start:end { } OR for e in array { }
        if(current().type == TokenType::IDENTIFIER && peek().type == TokenType::IN) {
            std::string var_name = current().value;
            advance(); // skip identifier
            advance(); // skip 'in'
            
            auto first_expr = parse_expression();
            
            // If we see ':', it's a range: for i in start:end
            if(current().type == TokenType::COLON) {
                advance(); // skip ':'
                auto rangeFor = std::make_unique<ForRangeStatement>();
                rangeFor->var_name = var_name;
                rangeFor->start = std::move(first_expr);
                rangeFor->end = parse_expression();
                rangeFor->body = parse_statement();
                return rangeFor;
            }
            
            // Otherwise it's foreach: for e in array
            auto forEach = std::make_unique<ForEachStatement>();
            forEach->var_name = var_name;
            forEach->iterable = std::move(first_expr);
            forEach->body = parse_statement();
            return forEach;
        }
        
        // Traditional C-style for loop: for (init; cond; update) { }
        expect(TokenType::LPAREN, "Expected '('");
        
        auto forStmt = std::make_unique<ForStatement>();
        
        // Parse init (can be variable declaration or expression)
        if(current().type != TokenType::SEMICOLON) {
            forStmt->init = parse_statement(); // This handles var decl with semicolon
        } else {
            advance(); // skip semicolon
        }
        
        // Parse condition
        if(current().type != TokenType::SEMICOLON) {
            forStmt->condition = parse_expression();
        }
        expect(TokenType::SEMICOLON, "Expected ';' after for condition");
        
        // Parse update
        if(current().type != TokenType::RPAREN) {
            forStmt->update = parse_expression();
        }
        expect(TokenType::RPAREN, "Expected ')'");
        
        forStmt->body = parse_statement();
        return forStmt;
    }

    // Return
    if(current().type == TokenType::RETURN){
        advance();
        auto ret = std::make_unique<ReturnStatement>();
        if (current().type != TokenType::SEMICOLON) {
            ret->value = parse_expression();
        }
        expect(TokenType::SEMICOLON, "Expected ';'");
        return ret;
    }

    // Variable declaration
    bool is_mutable = false;
    if (current().type == TokenType::MUT) {
        is_mutable = true;
        advance();
    }

    bool is_type = false;
    if(current().type == TokenType::INT || current().type == TokenType::STRING ||
       current().type == TokenType::FLOAT || current().type == TokenType::BOOL) {
        is_type = true;
    } else if (current().type == TokenType::IDENTIFIER) {
        // Distinguish between Variable Declaration and other statements starting with Identifier
        // Declaration: Type Name ... | Type[] Name ... | Type& Name ...
        // Assignment:  Name = ... | Name[index] = ...
        // Call:        Name(...)
        
        Token next = peek(1);
        if (next.type == TokenType::IDENTIFIER) {
            is_type = true; // "Type Name"
        } else if (next.type == TokenType::AMPERSAND) {
            is_type = true; // "Type& Name"
        } else if (next.type == TokenType::LBRACKET) {
            // Check for "Type[] Name"
            if (peek(2).type == TokenType::RBRACKET && peek(3).type == TokenType::IDENTIFIER) {
                is_type = true;
            }
        }
    }

    if(is_type){

        std::string type = current().value;
        advance();

        // Handle reference type
        bool is_reference = false;
        if(current().type == TokenType::AMPERSAND){
            is_reference = true;
            advance();
        }

        // Handle array type
        if(current().type == TokenType::LBRACKET){
            advance();
            expect(TokenType::RBRACKET, "Expected ']'");
            type += "[]";
        }

        std::string name = current().value;
        expect(TokenType::IDENTIFIER, "Expected variable name");

        auto var_decl = std::make_unique<VarDeclaration>();
        var_decl->type = type;
        var_decl->name = name;
        var_decl->is_mutable = is_mutable;
        var_decl->is_reference = is_reference;

        if(match(TokenType::ASSIGN)){
            var_decl->initializer = parse_expression();
        }

        expect(TokenType::SEMICOLON, "Expected ';'");
        return var_decl;
    } else if (is_mutable) {
        throw std::runtime_error("Expected type after 'mut'");
    }

    // Assignment to array element: arr[i] = value
    if(current().type == TokenType::IDENTIFIER && peek().type == TokenType::LBRACKET) {
        // Could be an index assignment or an expression statement with index access
        // Need to look ahead to see if there's an assignment operator after the bracket
        std::string name = current().value;
        size_t saved_pos = pos;
        advance(); // skip identifier
        advance(); // skip '['
        
        // Skip to find matching ']'
        int bracket_depth = 1;
        while (bracket_depth > 0 && current().type != TokenType::END_OF_FILE) {
            if (current().type == TokenType::LBRACKET) bracket_depth++;
            else if (current().type == TokenType::RBRACKET) bracket_depth--;
            advance();
        }
        
        // Check if followed by assignment operator
        bool is_index_assign = (current().type == TokenType::ASSIGN);
        
        // Restore position
        pos = saved_pos;
        
        if (is_index_assign) {
            advance(); // skip identifier
            expect(TokenType::LBRACKET, "Expected '['");
            auto index_expr = parse_expression();
            expect(TokenType::RBRACKET, "Expected ']'");
            
            expect(TokenType::ASSIGN, "Expected '='");
            
            auto idx_assign = std::make_unique<IndexAssignment>();
            idx_assign->array = std::make_unique<Identifier>(name);
            idx_assign->index = std::move(index_expr);
            idx_assign->value = parse_expression();
            
            expect(TokenType::SEMICOLON, "Expected ';'");
            return idx_assign;
        }
    }

    // Assignment
    if(current().type == TokenType::IDENTIFIER && 
        (peek().type == TokenType::ASSIGN || 
        peek().type == TokenType::PLUS_ASSIGN || 
        peek().type == TokenType::MINUS_ASSIGN ||
        peek().type == TokenType::STAR_ASSIGN ||
        peek().type == TokenType::SLASH_ASSIGN ||
        peek().type == TokenType::PERCENT_ASSIGN)){
        
        std::string name = current().value;
        advance();
        
        TokenType opType = current().type;
        advance(); // skip op

        auto assign = std::make_unique<Assignment>();
        assign->name = name;
        
        auto val = parse_expression();
        
        if (opType == TokenType::PLUS_ASSIGN) {
            auto left = std::make_unique<Identifier>(name);
            auto binOp = std::make_unique<BinaryOp>(std::move(left), "+", std::move(val));
            assign->value = std::move(binOp);
        } else if (opType == TokenType::MINUS_ASSIGN) {
            auto left = std::make_unique<Identifier>(name);
            auto binOp = std::make_unique<BinaryOp>(std::move(left), "-", std::move(val));
            assign->value = std::move(binOp);
        } else if (opType == TokenType::STAR_ASSIGN) {
            auto left = std::make_unique<Identifier>(name);
            auto binOp = std::make_unique<BinaryOp>(std::move(left), "*", std::move(val));
            assign->value = std::move(binOp);
        } else if (opType == TokenType::SLASH_ASSIGN) {
            auto left = std::make_unique<Identifier>(name);
            auto binOp = std::make_unique<BinaryOp>(std::move(left), "/", std::move(val));
            assign->value = std::move(binOp);
        } else if (opType == TokenType::PERCENT_ASSIGN) {
            auto left = std::make_unique<Identifier>(name);
            auto binOp = std::make_unique<BinaryOp>(std::move(left), "%", std::move(val));
            assign->value = std::move(binOp);
        } else {
            assign->value = std::move(val);
        }

        expect(TokenType::SEMICOLON, "Expected ';'");
        return assign;
    }

    // Expression statement
    // try {
        auto expr = parse_expression();
        expect(TokenType::SEMICOLON, "Expected ';'");
        auto stmt = std::make_unique<ExpressionStatement>();
        stmt->expression = std::move(expr);
        return stmt;
    // } catch (...) {
        // Fall through to error
    // }

    // throw std::runtime_error("Unexpected statement");
}

std::unique_ptr<StructDef> Parser::parse_struct(){
    expect(TokenType::STRUCT, "Expected 'struct'");
    std::string name = current().value;
    expect(TokenType::IDENTIFIER, "Expected struct name");
    expect(TokenType::LBRACE, "Expected '{'");

    auto def = std::make_unique<StructDef>();
    def->name = name;

    while(current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE){
        std::string type = current().value;
        // Handle types
        if(current().type == TokenType::INT || current().type == TokenType::STRING || 
            current().type == TokenType::FLOAT || current().type == TokenType::BOOL ||
            current().type == TokenType::IDENTIFIER){
            advance();
        } else {
            throw std::runtime_error("Expected type in struct");
        }
        
        std::string fieldName = current().value;
        expect(TokenType::IDENTIFIER, "Expected field name");
        expect(TokenType::SEMICOLON, "Expected ';'");

        def->fields.push_back({type, fieldName});
    }
    expect(TokenType::RBRACE, "Expected '}'");
    return def;
}

std::string Parser::parse_style_block() {
    expect(TokenType::LBRACE, "Expected '{'");
    std::string css = "";
    int brace_count = 1;
    
    Token prev = tokens[pos-1]; // The '{' we just consumed
    
    while (current().type != TokenType::END_OF_FILE) {
        if (current().type == TokenType::RBRACE && brace_count == 1) {
            advance(); // Consume closing '}'
            break;
        }
        
        if (current().type == TokenType::LBRACE) brace_count++;
        if (current().type == TokenType::RBRACE) brace_count--;
        
        Token tok = current();
        
        int prev_len = prev.value.length();
        if (prev.type == TokenType::STRING_LITERAL) prev_len += 2;

        if (tok.line > prev.line) {
            css += " ";
        } else if (tok.column > prev.column + prev_len) {
            css += " ";
        }
        
        if (tok.type == TokenType::STRING_LITERAL) {
                css += "\"" + tok.value + "\"";
        } else {
                css += tok.value;
        }
        
        prev = tok;
        advance();
    }
    return css;
}

std::unique_ptr<ASTNode> Parser::parse_html_element(){
    expect(TokenType::LT, "Expected '<'");
    int start_line = current().line;
    std::string tag = current().value;
    expect(TokenType::IDENTIFIER, "Expected tag name");

    // Check if component
    if(std::isupper(tag[0])){
        auto comp = std::make_unique<ComponentInstantiation>();
        comp->line = start_line;
        comp->component_name = tag;

        // Props
        while(current().type == TokenType::IDENTIFIER || current().type == TokenType::AMPERSAND){
            bool is_ref_prop = false;
            if(match(TokenType::AMPERSAND)){
                is_ref_prop = true;
            }
            std::string prop_name = current().value;
            advance();
            
            std::unique_ptr<Expression> prop_value;
            if(match(TokenType::ASSIGN)){
                if(current().type == TokenType::STRING_LITERAL){
                    prop_value = std::make_unique<StringLiteral>(current().value);
                    advance();
                } else if(current().type == TokenType::INT_LITERAL){
                    prop_value = std::make_unique<IntLiteral>(std::stoi(current().value));
                    advance();
                } else if(current().type == TokenType::FLOAT_LITERAL){
                    prop_value = std::make_unique<FloatLiteral>(std::stof(current().value));
                    advance();
                } else if(match(TokenType::MINUS)){
                    if(current().type == TokenType::INT_LITERAL){
                        prop_value = std::make_unique<IntLiteral>(-std::stoi(current().value));
                        advance();
                    } else if(current().type == TokenType::FLOAT_LITERAL){
                        prop_value = std::make_unique<FloatLiteral>(-std::stof(current().value));
                        advance();
                    } else {
                        throw std::runtime_error("Expected number after '-' in prop value");
                    }
                } else if(match(TokenType::LBRACE)){
                    prop_value = parse_expression();
                    expect(TokenType::RBRACE, "Expected '}'");
                } else {
                    throw std::runtime_error("Expected prop value");
                }
            } else {
                // Boolean prop?
                prop_value = std::make_unique<StringLiteral>("true");
            }
            ComponentProp cprop;
            cprop.name = prop_name;
            cprop.value = std::move(prop_value);
            cprop.is_reference = is_ref_prop;
            comp->props.push_back(std::move(cprop));
        }

        // Self-closing
        if(match(TokenType::SLASH)){
            expect(TokenType::GT, "Expected '>'");
            return comp;
        }

        expect(TokenType::GT, "Expected '>'");
        throw std::runtime_error("Custom components must be self-closing for now: " + tag);
    }

    auto el = std::make_unique<HTMLElement>();
    el->line = start_line;
    el->tag = tag;

    // Attributes
    while(current().type == TokenType::IDENTIFIER || current().type == TokenType::STYLE || current().type == TokenType::AMPERSAND){
        // Check for element ref binding: &={varName}
        if(match(TokenType::AMPERSAND)){
            expect(TokenType::ASSIGN, "Expected '=' after '&' for element binding");
            expect(TokenType::LBRACE, "Expected '{' after '&='");
            if(current().type != TokenType::IDENTIFIER){
                throw std::runtime_error("Expected variable name in element binding &={varName}");
            }
            el->ref_binding = current().value;
            advance();
            expect(TokenType::RBRACE, "Expected '}' after variable name");
            continue;
        }
        
        std::string attrName = current().value;
        advance();
        
        std::unique_ptr<Expression> attrValue;
        if(match(TokenType::ASSIGN)){
            if(current().type == TokenType::STRING_LITERAL){
                attrValue = std::make_unique<StringLiteral>(current().value);
                advance();
            } else if(match(TokenType::LBRACE)){
                attrValue = parse_expression();
                expect(TokenType::RBRACE, "Expected '}'");
            } else {
                throw std::runtime_error("Expected attribute value");
            }
        } else {
            // Boolean attribute? Treat as "true"
            attrValue = std::make_unique<StringLiteral>("true");
        }
        el->attributes.push_back({attrName, std::move(attrValue)});
    }

    // Self-closing
    if(match(TokenType::SLASH)){
        expect(TokenType::GT, "Expected '>'");
        return el;
    }

    expect(TokenType::GT, "Expected '>'");

    // Children
    while(true){
        if(current().type == TokenType::LT){
            if(peek().type == TokenType::SLASH){
                // Closing tag
                break;
            }
            // Check for special tags: <if>, <for>
            if(peek().type == TokenType::IF){
                el->children.push_back(parse_view_if());
            } else if(peek().type == TokenType::FOR){
                el->children.push_back(parse_view_for());
            } else {
                // Regular child element
                el->children.push_back(parse_html_element());
            }
        } else if(current().type == TokenType::LBRACE){
            // Expression
            advance();
            el->children.push_back(parse_expression());
            expect(TokenType::RBRACE, "Expected '}'");
        } else {
            // Text content
            std::string text;
            bool first = true;
            Token prev_token = current();
            // Text continues until we hit '<' or '{'
            while(current().type != TokenType::LT && current().type != TokenType::LBRACE && 
                  current().type != TokenType::END_OF_FILE){
                if(!first){
                    int prev_len = prev_token.value.length();
                    if (prev_token.type == TokenType::STRING_LITERAL) prev_len += 2;
                    
                    if (prev_token.line != current().line || prev_token.column + prev_len != current().column) {
                        text += " ";
                    }
                }
                if(current().type == TokenType::STRING_LITERAL) text += current().value;
                else text += current().value;
                
                prev_token = current();
                advance();
                first = false;
            }
            if(!text.empty()){
                el->children.push_back(std::make_unique<TextNode>(text));
            }
            
            if(current().type == TokenType::END_OF_FILE) break;
        }
    }

    expect(TokenType::LT, "Expected '<'");
    expect(TokenType::SLASH, "Expected '/'");
    if(current().value != tag){
        throw std::runtime_error("Mismatched closing tag: expected " + tag + ", got " + current().value);
    }
    expect(TokenType::IDENTIFIER, "Expected tag name");
    expect(TokenType::GT, "Expected '>'");

    return el;
}

std::unique_ptr<ASTNode> Parser::parse_view_node() {
    // Must start with '<'
    if (current().type != TokenType::LT) {
        throw std::runtime_error("Expected '<' at line " + std::to_string(current().line));
    }
    
    // Check for special tags
    if (peek().type == TokenType::IF) {
        return parse_view_if();
    }
    if (peek().type == TokenType::FOR) {
        return parse_view_for();
    }
    // Regular HTML element
    return parse_html_element();
}

std::unique_ptr<ViewIfStatement> Parser::parse_view_if() {
    // Syntax: <if condition> ... <else> ... </else> </if>
    //     or: <if condition> ... </if>
    auto viewIf = std::make_unique<ViewIfStatement>();
    viewIf->line = current().line;
    
    expect(TokenType::LT, "Expected '<'");
    expect(TokenType::IF, "Expected 'if'");
    
    // Parse condition (everything until '>')
    // Use parse_expression_no_gt so > is not treated as comparison
    viewIf->condition = parse_expression_no_gt();
    expect(TokenType::GT, "Expected '>'");
    
    // Parse then children until we hit </if> or <else>
    while (current().type != TokenType::END_OF_FILE) {
        if (current().type == TokenType::LT) {
            if (peek().type == TokenType::SLASH && peek(2).type == TokenType::IF) {
                // </if> - end of if block
                break;
            }
            if (peek().type == TokenType::ELSE) {
                // <else> block
                break;
            }
        }
        viewIf->then_children.push_back(parse_view_node());
    }
    
    // Check for <else>
    if (current().type == TokenType::LT && peek().type == TokenType::ELSE) {
        advance(); // <
        advance(); // else
        expect(TokenType::GT, "Expected '>'");
        
        // Parse else children until </else>
        while (current().type != TokenType::END_OF_FILE) {
            if (current().type == TokenType::LT && peek().type == TokenType::SLASH && peek(2).type == TokenType::ELSE) {
                break;
            }
            viewIf->else_children.push_back(parse_view_node());
        }
        
        // </else>
        expect(TokenType::LT, "Expected '<'");
        expect(TokenType::SLASH, "Expected '/'");
        expect(TokenType::ELSE, "Expected 'else'");
        expect(TokenType::GT, "Expected '>'");
    }
    
    // </if>
    expect(TokenType::LT, "Expected '<'");
    expect(TokenType::SLASH, "Expected '/'");
    expect(TokenType::IF, "Expected 'if'");
    expect(TokenType::GT, "Expected '>'");
    
    return viewIf;
}

std::unique_ptr<ASTNode> Parser::parse_view_for() {
    // Syntax: <for var in start:end> ... </for>
    //     or: <for var in iterable> ... </for>
    int start_line = current().line;
    
    expect(TokenType::LT, "Expected '<'");
    expect(TokenType::FOR, "Expected 'for'");
    
    std::string var_name = current().value;
    expect(TokenType::IDENTIFIER, "Expected loop variable name");
    expect(TokenType::IN, "Expected 'in'");
    
    // Use parse_expression_no_gt so > is not treated as comparison
    auto first_expr = parse_expression_no_gt();
    
    // Check if this is a range (has colon) or foreach
    if (current().type == TokenType::COLON) {
        // Range: <for i in 0:10>
        advance();
        auto end_expr = parse_expression_no_gt();
        expect(TokenType::GT, "Expected '>'");
        
        auto viewFor = std::make_unique<ViewForRangeStatement>();
        viewFor->line = start_line;
        viewFor->var_name = var_name;
        viewFor->start = std::move(first_expr);
        viewFor->end = std::move(end_expr);
        
        // Parse children until </for>
        while (current().type != TokenType::END_OF_FILE) {
            if (current().type == TokenType::LT && peek().type == TokenType::SLASH && peek(2).type == TokenType::FOR) {
                break;
            }
            viewFor->children.push_back(parse_view_node());
        }
        
        // </for>
        expect(TokenType::LT, "Expected '<'");
        expect(TokenType::SLASH, "Expected '/'");
        expect(TokenType::FOR, "Expected 'for'");
        expect(TokenType::GT, "Expected '>'");
        
        return viewFor;
    } else {
        // ForEach: <for item in items>
        expect(TokenType::GT, "Expected '>'");
        
        auto viewForEach = std::make_unique<ViewForEachStatement>();
        viewForEach->line = start_line;
        viewForEach->var_name = var_name;
        viewForEach->iterable = std::move(first_expr);
        
        // Parse children until </for>
        while (current().type != TokenType::END_OF_FILE) {
            if (current().type == TokenType::LT && peek().type == TokenType::SLASH && peek(2).type == TokenType::FOR) {
                break;
            }
            viewForEach->children.push_back(parse_view_node());
        }
        
        // </for>
        expect(TokenType::LT, "Expected '<'");
        expect(TokenType::SLASH, "Expected '/'");
        expect(TokenType::FOR, "Expected 'for'");
        expect(TokenType::GT, "Expected '>'");
        
        return viewForEach;
    }
}

Component Parser::parse_component(){
    Component comp;

    expect(TokenType::COMPONENT, "Expected 'component'");
    comp.name = current().value;
    expect(TokenType::IDENTIFIER, "Expected component name");
    expect(TokenType::LBRACE, "Expected '{'");

    // Parse state variables and methods
    while(current().type != TokenType::VIEW && current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE){
        bool is_mutable = false;
        if (current().type == TokenType::MUT) {
            is_mutable = true;
            advance();
        }

        // Variable declaration
        if(current().type == TokenType::INT || current().type == TokenType::STRING || 
            current().type == TokenType::FLOAT || current().type == TokenType::BOOL || 
            current().type == TokenType::IDENTIFIER){
            auto var_decl = std::make_unique<VarDeclaration>();
            var_decl->type = current().value;
            advance();

            // Handle reference type
            if(current().type == TokenType::AMPERSAND){
                var_decl->is_reference = true;
                advance();
            }

            if(current().type == TokenType::LBRACKET){
                advance();
                expect(TokenType::RBRACKET, "Expected ']'");
                var_decl->type += "[]";
            }

            var_decl->name = current().value;
            expect(TokenType::IDENTIFIER, "Expected variable name");
            var_decl->is_mutable = is_mutable;

            if(match(TokenType::ASSIGN)){
                var_decl->initializer = parse_expression();
            }

            expect(TokenType::SEMICOLON, "Expected ';'");
            comp.state.push_back(std::move(var_decl));
        }
        else if (is_mutable) {
            throw std::runtime_error("Expected variable declaration after 'mut'");
        }
        // Struct definition
        else if(current().type == TokenType::STRUCT){
            comp.structs.push_back(parse_struct());
        }
        // Function definition
        else if(current().type == TokenType::DEF){
            advance();
            FunctionDef func;
            func.name  = current().value;
            expect(TokenType::IDENTIFIER, "Expected function name");
            expect(TokenType::LPAREN, "Expected '('");
            
            // Parse parameters
            while(current().type != TokenType::RPAREN){
                bool is_mutable = false;
                if (current().type == TokenType::MUT) {
                    is_mutable = true;
                    advance();
                }

                std::string paramType = current().value;
                if(current().type == TokenType::INT || current().type == TokenType::FLOAT || 
                    current().type == TokenType::STRING || current().type == TokenType::BOOL || 
                    current().type == TokenType::IDENTIFIER){
                    advance();
                } else {
                        throw std::runtime_error("Expected parameter type");
                }

                bool is_reference = false;
                if(current().type == TokenType::AMPERSAND){
                    is_reference = true;
                    advance();
                }

                std::string paramName = current().value;
                expect(TokenType::IDENTIFIER, "Expected parameter name");
                
                func.params.push_back({paramType, paramName, is_mutable, is_reference});

                if(current().type == TokenType::COMMA){
                    advance();
                }
            }

            expect(TokenType::RPAREN, "Expected ')'");
            if (current().type == TokenType::LBRACE) {
                 throw std::runtime_error("Missing return type for function '" + func.name + "'. Expected ':' followed by return type at line " + std::to_string(current().line));
            }
            expect(TokenType::COLON, "Expected ':' for return type");
            func.return_type = current().value;
            advance();
            expect(TokenType::LBRACE, "Expected '{'");

            while(current().type != TokenType::RBRACE){
                func.body.push_back(parse_statement());
            }

            expect(TokenType::RBRACE, "Expected '}'");
            comp.methods.push_back(std::move(func));
        }
        // Init block
        else if(current().type == TokenType::INIT){
            advance();
            FunctionDef func;
            func.name = "init";
            func.return_type = "void";
            expect(TokenType::LBRACE, "Expected '{'");

            while(current().type != TokenType::RBRACE){
                func.body.push_back(parse_statement());
            }

            expect(TokenType::RBRACE, "Expected '}'");
            comp.methods.push_back(std::move(func));
        }
        // Mount block (runs after view is created)
        else if(current().type == TokenType::MOUNT){
            advance();
            FunctionDef func;
            func.name = "mount";
            func.return_type = "void";
            expect(TokenType::LBRACE, "Expected '{'");

            while(current().type != TokenType::RBRACE){
                func.body.push_back(parse_statement());
            }

            expect(TokenType::RBRACE, "Expected '}'");
            comp.methods.push_back(std::move(func));
        }
        // Tick definition
        else if(current().type == TokenType::TICK){
            advance();
            FunctionDef func;
            func.name = "tick";
            func.return_type = "void";
            expect(TokenType::LPAREN, "Expected '('");
            
            // Parse parameters
            while(current().type != TokenType::RPAREN){
                bool is_mutable = false;
                if (current().type == TokenType::MUT) {
                    is_mutable = true;
                    advance();
                }

                std::string paramType = current().value;
                if(current().type == TokenType::INT || current().type == TokenType::FLOAT || 
                    current().type == TokenType::STRING || current().type == TokenType::BOOL || 
                    current().type == TokenType::IDENTIFIER){
                    advance();
                } else {
                        throw std::runtime_error("Expected parameter type");
                }

                bool is_reference = false;
                if(current().type == TokenType::AMPERSAND){
                    is_reference = true;
                    advance();
                }

                std::string paramName = current().value;
                expect(TokenType::IDENTIFIER, "Expected parameter name");
                
                func.params.push_back({paramType, paramName, is_mutable, is_reference});

                if(current().type == TokenType::COMMA){
                    advance();
                }
            }

            expect(TokenType::RPAREN, "Expected ')'");
            expect(TokenType::LBRACE, "Expected '{'");

            while(current().type != TokenType::RBRACE){
                func.body.push_back(parse_statement());
            }

            expect(TokenType::RBRACE, "Expected '}'");
            comp.methods.push_back(std::move(func));
        }
        // Prop declaration
        else if(current().type == TokenType::PROP){
            advance();
            
            bool is_mutable = false;
            if (current().type == TokenType::MUT) {
                is_mutable = true;
                advance();
            }

            auto prop_decl = std::make_unique<PropDeclaration>();
            prop_decl->is_mutable = is_mutable;
            
            if (current().type == TokenType::DEF) {
                advance();
                prop_decl->name = current().value;
                expect(TokenType::IDENTIFIER, "Expected prop name");
                expect(TokenType::COLON, "Expected ':'");
                
                std::string retType = current().value;
                if(current().type == TokenType::INT || current().type == TokenType::STRING || 
                    current().type == TokenType::FLOAT || current().type == TokenType::BOOL || 
                    current().type == TokenType::IDENTIFIER || current().type == TokenType::VOID){
                    advance();
                } else {
                        throw std::runtime_error("Expected return type");
                }
                prop_decl->type = "webcc::function<" + retType + "()>";
            } else {
                prop_decl->type = current().value;
                // Check type
                if(current().type == TokenType::INT || current().type == TokenType::STRING || 
                    current().type == TokenType::FLOAT || current().type == TokenType::BOOL || 
                    current().type == TokenType::IDENTIFIER || current().type == TokenType::VOID){
                    advance();
                } else {
                        throw std::runtime_error("Expected prop type");
                }

                // Handle reference type
                if(current().type == TokenType::AMPERSAND){
                    prop_decl->is_reference = true;
                    advance();
                }

                if(current().type == TokenType::LBRACKET){
                    advance();
                    expect(TokenType::RBRACKET, "Expected ']'");
                    prop_decl->type += "[]";
                }

                prop_decl->name = current().value;
                expect(TokenType::IDENTIFIER, "Expected prop name");
            }

            if(match(TokenType::ASSIGN)){
                prop_decl->default_value = parse_expression();
            }

            expect(TokenType::SEMICOLON, "Expected ';'");
            comp.props.push_back(std::move(prop_decl));
        }
        // Style block
        else if(current().type == TokenType::STYLE){
            advance();
            bool is_global = false;
            if (current().type == TokenType::IDENTIFIER && current().value == "global") {
                is_global = true;
                advance();
            }
            std::string css = parse_style_block();
            if (is_global) {
                comp.global_css += css + "\n";
            } else {
                comp.css += css + "\n";
            }
        }
        else {
            advance();
        }
    }

    // Parse render block
    if(match(TokenType::VIEW)){
        expect(TokenType::LBRACE, "Expected '{'");
        while(current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE){
            comp.render_roots.push_back(parse_view_node());
        }
        expect(TokenType::RBRACE, "Expected '}'");
    }

    return comp;
}

void Parser::parse_app() {
        expect(TokenType::LBRACE, "Expected '{'");
        
        while(current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE){
            std::string key = current().value;
            expect(TokenType::IDENTIFIER, "Expected key");
            expect(TokenType::ASSIGN, "Expected '='");
            
            if(key == "root"){
                app_config.root_component = current().value;
                expect(TokenType::IDENTIFIER, "Expected component name");
            } else if(key == "routes"){
                expect(TokenType::LBRACE, "Expected '{'");
                while(current().type != TokenType::RBRACE){
                    std::string route = current().value;
                    expect(TokenType::STRING_LITERAL, "Expected route string");
                    expect(TokenType::COLON, "Expected ':'");
                    std::string comp = current().value;
                    expect(TokenType::IDENTIFIER, "Expected component name");
                    app_config.routes[route] = comp;
                    
                    if(current().type == TokenType::COMMA) advance();
                }
                expect(TokenType::RBRACE, "Expected '}'");
            }
            expect(TokenType::SEMICOLON, "Expected ';'");
        }
        expect(TokenType::RBRACE, "Expected '}'");
}

void Parser::parse_file(){
    while(current().type != TokenType::END_OF_FILE){
        if(current().type == TokenType::IMPORT){
            advance();
            expect(TokenType::STRING_LITERAL, "Expected import path");
            imports.push_back(tokens[pos-1].value);
            expect(TokenType::SEMICOLON, "Expected ';'");
        } else if(current().type == TokenType::COMPONENT){
            components.push_back(parse_component());
        } else if(current().type == TokenType::IDENTIFIER && current().value == "app"){
            advance();
            parse_app();
        } else {
            advance();
        }
    }

    // Default to Main if no app config
    if(app_config.root_component.empty()){
        for(const auto& comp : components){
            if(comp.name == "Main"){
                app_config.root_component = "Main";
                break;
            }
        }
    }
}
