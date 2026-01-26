#include "parser.h"
#include "def_parser.h"
#include "error.h"
#include <stdexcept>
#include <iostream>
#include <limits>

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
        ErrorHandler::compiler_error(msg, current().line);
    }
}

// Check if current token is a type keyword (INT, STRING, FLOAT, etc.) or identifier
bool Parser::is_type_token() {
    TokenType t = current().type;
    return t == TokenType::INT || t == TokenType::STRING ||
           t == TokenType::FLOAT || t == TokenType::FLOAT32 ||
           t == TokenType::BOOL || t == TokenType::IDENTIFIER ||
           t == TokenType::VOID;
}

// Check if current token can be used as an identifier (including keywords that are allowed as names)
bool Parser::is_identifier_token() {
    TokenType t = current().type;
    return t == TokenType::IDENTIFIER || t == TokenType::KEY || t == TokenType::DATA;
}

// Parse comma-separated arguments until end_token (RPAREN or RBRACE)
// Supports: positional, named (name = val), reference (&val or &name = val), move (:val or name := val)
std::vector<CallArg> Parser::parse_call_args(TokenType end_token) {
    std::vector<CallArg> args;
    
    while (current().type != end_token) {
        CallArg arg;

        // Check for reference prefix &
        if (current().type == TokenType::AMPERSAND) {
            arg.is_reference = true;
            advance();
        }
        // Check for move prefix :
        else if (current().type == TokenType::COLON) {
            arg.is_move = true;
            advance();
        }

        // Check if this is a named argument: name = value or name := value
        bool is_named = false;
        if (is_identifier_token()) {
            if (peek().type == TokenType::ASSIGN || peek().type == TokenType::MOVE_ASSIGN) {
                is_named = true;
            }
        }

        if (is_named) {
            arg.name = current().value;
            advance();

            // Check for := (move) or = (copy/reference)
            if (match(TokenType::MOVE_ASSIGN)) {
                arg.is_move = true;
            } else {
                expect(TokenType::ASSIGN, "Expected '=' or ':=' after argument name");
            }

            arg.value = parse_expression();
        } else {
            arg.value = parse_expression();
        }

        args.push_back(std::move(arg));

        if (current().type == TokenType::COMMA) {
            advance();
            if (current().type == end_token) break; // Allow trailing comma
        }
    }
    
    return args;
}

std::unique_ptr<Expression> Parser::parse_expression(){
    return parse_ternary();
}

std::unique_ptr<Expression> Parser::parse_ternary(){
    auto expr = parse_or();

    if (current().type == TokenType::QUESTION) {
        advance(); // skip '?'
        auto true_expr = parse_expression();  // Allow nested ternary
        expect(TokenType::COLON, "Expected ':' in ternary expression");
        auto false_expr = parse_ternary();  // Right-associative
        expr = std::make_unique<TernaryOp>(std::move(expr), std::move(true_expr), std::move(false_expr));
    }

    return expr;
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
    // Unary operators: -, +, !
    if (current().type == TokenType::MINUS || current().type == TokenType::PLUS || current().type == TokenType::NOT) {
        std::string op = current().value;
        advance();
        auto operand = parse_unary();
        return std::make_unique<UnaryOp>(op, std::move(operand));
    }
    // Reference expression: &expr (borrow, pass by reference)
    if (current().type == TokenType::AMPERSAND) {
        advance();
        auto operand = parse_unary();
        return std::make_unique<ReferenceExpression>(std::move(operand));
    }
    // Move expression: :expr (transfer ownership)
    if (current().type == TokenType::COLON) {
        advance();
        auto operand = parse_unary();
        return std::make_unique<MoveExpression>(std::move(operand));
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
        int value;
        try {
            long long ll_value = std::stoll(current().value);
            if (ll_value > std::numeric_limits<int>::max() || ll_value < std::numeric_limits<int>::min()) {
                throw std::out_of_range("overflow");
            }
            value = static_cast<int>(ll_value);
        } catch (const std::out_of_range&) {
            ErrorHandler::compiler_error("Integer literal '" + current().value + "' is too large", current().line);
        } catch (const std::invalid_argument&) {
            ErrorHandler::compiler_error("Invalid integer literal '" + current().value + "'", current().line);
        }
        advance();
        return std::make_unique<IntLiteral>(value);
    }

    // Float literal
    if(current().type == TokenType::FLOAT_LITERAL){
        double value;
        try {
            value = std::stod(current().value);
        } catch (const std::out_of_range&) {
            ErrorHandler::compiler_error("Float literal '" + current().value + "' is too large", current().line);
        } catch (const std::invalid_argument&) {
            ErrorHandler::compiler_error("Invalid float literal '" + current().value + "'", current().line);
        }
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

    // Identifer or function call (also allow 'key' and 'data' keywords as identifier)
    if(is_identifier_token()){
        std::string name = current().value;
        int identifier_line = current().line;
        advance();

        // Check for enum access: EnumName::Value
        if(current().type == TokenType::DOUBLE_COLON){
            advance();
            std::string value_name = current().value;
            expect(TokenType::IDENTIFIER, "Expected enum value name after '::'");
            return std::make_unique<EnumAccess>(name, value_name);
        }

        std::unique_ptr<Expression> expr = std::make_unique<Identifier>(name);

        while(true) {
            // Data literal initialization: TypeName{val1, val2, ...} or TypeName{name = val, ...}
            if(current().type == TokenType::LBRACE && std::isupper(name[0])){
                advance();
                auto parsed_args = parse_call_args(TokenType::RBRACE);
                expect(TokenType::RBRACE, "Expected '}'");
                
                // Use ComponentConstruction for data types too (same aggregate init semantics)
                auto data_expr = std::make_unique<ComponentConstruction>(name);
                data_expr->args = std::move(parsed_args);
                return data_expr;
            }
            if(current().type == TokenType::LPAREN){
                advance();

                // Check if this is a component construction (uppercase) vs function call
                bool is_component = false;
                if (dynamic_cast<Identifier*>(expr.get())) {
                    // Simple identifier - component if uppercase
                    is_component = std::isupper(name[0]);
                }

                auto parsed_args = parse_call_args(TokenType::RPAREN);
                expect(TokenType::RPAREN, "Expected ')'");

                if (is_component) {
                    // Component construction
                    auto comp_expr = std::make_unique<ComponentConstruction>(expr->to_webcc());
                    comp_expr->args = std::move(parsed_args);
                    expr = std::move(comp_expr);
                } else {
                    // Function call
                    auto call = std::make_unique<FunctionCall>(expr->to_webcc());
                    call->line = identifier_line;
                    call->args = std::move(parsed_args);
                    expr = std::move(call);
                }
            }
            else if(current().type == TokenType::DOT){
                advance();
                std::string member = current().value;
                expect(TokenType::IDENTIFIER, "Expected member name");

                // Check for Component.EnumName::Value syntax for shared enums
                if(current().type == TokenType::DOUBLE_COLON){
                    advance();
                    std::string value_name = current().value;
                    expect(TokenType::IDENTIFIER, "Expected enum value name after '::'");
                    // name is the component name, member is the enum name
                    return std::make_unique<EnumAccess>(member, value_name, name);
                }

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
                ErrorHandler::compiler_error("Array repeat count must be an integer literal", current().line);
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
        // Re-enable > comparison inside parentheses since it's unambiguous
        bool old_allow_gt = allow_gt_comparison;
        allow_gt_comparison = true;
        auto expr = parse_expression();
        allow_gt_comparison = old_allow_gt;
        expect(TokenType::RPAREN, "Expected ')'");
        return expr;
    }

    ErrorHandler::compiler_error("Unexpected token in expression: " + current().value + " (Type: " + std::to_string((int)current().type) + ")", current().line);
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

        ErrorHandler::compiler_error("Unexpected token after 'for'. Expected range 'i in start:end' or foreach 'i in array'. C-style for loops are not supported.", -1);
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
       current().type == TokenType::FLOAT || current().type == TokenType::FLOAT32 ||
       current().type == TokenType::BOOL) {
        is_type = true;
    } else if (current().type == TokenType::IDENTIFIER) {
        // Distinguish between Variable Declaration and other statements starting with Identifier
        // Declaration: Type Name ... | Type[] Name ... | Type[N] Name ... | Type& Name ...
        // Assignment:  Name = ... | Name[index] = ...
        // Call:        Name(...)

        Token next = peek(1);
        if (next.type == TokenType::IDENTIFIER) {
            is_type = true; // "Type Name"
        } else if (next.type == TokenType::AMPERSAND) {
            is_type = true; // "Type& Name"
        } else if (next.type == TokenType::LBRACKET) {
            // Check for "Type[] Name" (dynamic) or "Type[N] Name" (fixed-size)
            if (peek(2).type == TokenType::RBRACKET && peek(3).type == TokenType::IDENTIFIER) {
                is_type = true; // Type[] Name
            } else if (peek(2).type == TokenType::INT_LITERAL && peek(3).type == TokenType::RBRACKET && peek(4).type == TokenType::IDENTIFIER) {
                is_type = true; // Type[N] Name
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
            if (current().type == TokenType::INT_LITERAL) {
                // Fixed-size array: Type[N]
                std::string size = current().value;
                advance();
                expect(TokenType::RBRACKET, "Expected ']'");
                type += "[" + size + "]";
            } else {
                // Dynamic array: Type[]
                expect(TokenType::RBRACKET, "Expected ']'");
                type += "[]";
            }
        }

        std::string name = current().value;
        if (is_identifier_token()) {
            advance();
        } else {
            expect(TokenType::IDENTIFIER, "Expected variable name");
        }

        auto var_decl = std::make_unique<VarDeclaration>();
        var_decl->type = type;
        var_decl->name = name;
        var_decl->is_mutable = is_mutable;
        var_decl->is_reference = is_reference;

        // Check for := (move) or = (copy)
        if(match(TokenType::MOVE_ASSIGN)){
            var_decl->is_move = true;
            var_decl->initializer = parse_expression();
        } else if(match(TokenType::ASSIGN)){
            var_decl->initializer = parse_expression();
        }

        expect(TokenType::SEMICOLON, "Expected ';'");
        return var_decl;
    } else if (is_mutable) {
        ErrorHandler::compiler_error("Expected type after 'mut'", -1);
    }

    // Assignment to array element: arr[i] = value or arr[i] += value etc.
    // Also handles arr[i].member = value (member assignment on array element)
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

        // Check if followed by assignment operator (including compound assignments)
        TokenType after_bracket = current().type;
        bool is_index_assign = (after_bracket == TokenType::ASSIGN ||
                                after_bracket == TokenType::PLUS_ASSIGN ||
                                after_bracket == TokenType::MINUS_ASSIGN ||
                                after_bracket == TokenType::STAR_ASSIGN ||
                                after_bracket == TokenType::SLASH_ASSIGN ||
                                after_bracket == TokenType::PERCENT_ASSIGN);

        // Check if followed by .member = value (member assignment on array element)
        bool is_index_member_assign = false;
        if (after_bracket == TokenType::DOT) {
            // Skip through member chain to see if there's an assignment at the end
            while (current().type == TokenType::DOT) {
                advance(); // skip '.'
                if (current().type != TokenType::IDENTIFIER) break;
                advance(); // skip member name
            }
            TokenType assign_op = current().type;
            is_index_member_assign = (assign_op == TokenType::ASSIGN ||
                                      assign_op == TokenType::PLUS_ASSIGN ||
                                      assign_op == TokenType::MINUS_ASSIGN ||
                                      assign_op == TokenType::STAR_ASSIGN ||
                                      assign_op == TokenType::SLASH_ASSIGN ||
                                      assign_op == TokenType::PERCENT_ASSIGN);
        }

        // Restore position
        pos = saved_pos;

        if (is_index_assign) {
            advance(); // skip identifier
            expect(TokenType::LBRACKET, "Expected '['");
            auto index_expr = parse_expression();
            expect(TokenType::RBRACKET, "Expected ']'");

            TokenType opType = current().type;
            advance(); // skip assignment operator

            auto idx_assign = std::make_unique<IndexAssignment>();
            idx_assign->array = std::make_unique<Identifier>(name);
            idx_assign->index = std::move(index_expr);
            idx_assign->value = parse_expression();

            // Set compound operator if not plain assignment
            if (opType == TokenType::PLUS_ASSIGN) idx_assign->compound_op = "+";
            else if (opType == TokenType::MINUS_ASSIGN) idx_assign->compound_op = "-";
            else if (opType == TokenType::STAR_ASSIGN) idx_assign->compound_op = "*";
            else if (opType == TokenType::SLASH_ASSIGN) idx_assign->compound_op = "/";
            else if (opType == TokenType::PERCENT_ASSIGN) idx_assign->compound_op = "%";

            expect(TokenType::SEMICOLON, "Expected ';'");
            return idx_assign;
        }

        if (is_index_member_assign) {
            // Parse arr[i].member = value as MemberAssignment with IndexAccess as object
            advance(); // skip identifier
            expect(TokenType::LBRACKET, "Expected '['");
            auto index_expr = parse_expression();
            expect(TokenType::RBRACKET, "Expected ']'");

            // Build object as IndexAccess
            std::unique_ptr<Expression> obj_expr = std::make_unique<IndexAccess>(
                std::make_unique<Identifier>(name), std::move(index_expr));

            // Now parse the member chain
            expect(TokenType::DOT, "Expected '.'");
            std::string last_member = current().value;
            expect(TokenType::IDENTIFIER, "Expected member name");

            // Handle chained member access (arr[i].a.b = value)
            while (current().type == TokenType::DOT) {
                advance(); // skip '.'
                obj_expr = std::make_unique<MemberAccess>(std::move(obj_expr), last_member);
                last_member = current().value;
                expect(TokenType::IDENTIFIER, "Expected member name");
            }

            TokenType opType = current().type;
            advance(); // skip assignment operator

            auto member_assign = std::make_unique<MemberAssignment>();
            member_assign->object = std::move(obj_expr);
            member_assign->member = last_member;
            member_assign->value = parse_expression();

            // Set compound operator if not plain assignment
            if (opType == TokenType::PLUS_ASSIGN) member_assign->compound_op = "+";
            else if (opType == TokenType::MINUS_ASSIGN) member_assign->compound_op = "-";
            else if (opType == TokenType::STAR_ASSIGN) member_assign->compound_op = "*";
            else if (opType == TokenType::SLASH_ASSIGN) member_assign->compound_op = "/";
            else if (opType == TokenType::PERCENT_ASSIGN) member_assign->compound_op = "%";

            expect(TokenType::SEMICOLON, "Expected ';'");
            return member_assign;
        }
    }

    // Member assignment: obj.member = value or obj.a.b = value
    if(current().type == TokenType::IDENTIFIER && peek().type == TokenType::DOT) {
        // Look ahead to find if this is a member assignment
        size_t saved_pos = pos;
        advance(); // skip identifier

        // Track the chain of member accesses
        while (current().type == TokenType::DOT) {
            advance(); // skip '.'
            if (current().type != TokenType::IDENTIFIER) break;
            advance(); // skip member name
        }

        // Check if followed by assignment operator
        TokenType assign_op = current().type;
        bool is_member_assign = (assign_op == TokenType::ASSIGN ||
                                 assign_op == TokenType::PLUS_ASSIGN ||
                                 assign_op == TokenType::MINUS_ASSIGN ||
                                 assign_op == TokenType::STAR_ASSIGN ||
                                 assign_op == TokenType::SLASH_ASSIGN ||
                                 assign_op == TokenType::PERCENT_ASSIGN);

        // Restore position
        pos = saved_pos;

        if (is_member_assign) {
            // Parse the object part (all but the last member)
            std::unique_ptr<Expression> obj_expr = std::make_unique<Identifier>(current().value);
            advance(); // skip first identifier
            advance(); // skip first '.'

            std::string last_member = current().value;
            expect(TokenType::IDENTIFIER, "Expected member name");

            // Handle chained member access (a.b.c = value means object is a.b, member is c)
            while (current().type == TokenType::DOT) {
                advance(); // skip '.'
                // Previous member becomes part of the object
                obj_expr = std::make_unique<MemberAccess>(std::move(obj_expr), last_member);
                last_member = current().value;
                expect(TokenType::IDENTIFIER, "Expected member name");
            }

            TokenType opType = current().type;
            advance(); // skip assignment operator

            auto member_assign = std::make_unique<MemberAssignment>();
            member_assign->object = std::move(obj_expr);
            member_assign->member = last_member;
            member_assign->value = parse_expression();

            // Set compound operator if not plain assignment
            if (opType == TokenType::PLUS_ASSIGN) member_assign->compound_op = "+";
            else if (opType == TokenType::MINUS_ASSIGN) member_assign->compound_op = "-";
            else if (opType == TokenType::STAR_ASSIGN) member_assign->compound_op = "*";
            else if (opType == TokenType::SLASH_ASSIGN) member_assign->compound_op = "/";
            else if (opType == TokenType::PERCENT_ASSIGN) member_assign->compound_op = "%";

            expect(TokenType::SEMICOLON, "Expected ';'");
            return member_assign;
        }
    }

    // Assignment
    if(current().type == TokenType::IDENTIFIER &&
        (peek().type == TokenType::ASSIGN ||
        peek().type == TokenType::MOVE_ASSIGN ||
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
        
        // Check for move assignment
        if (opType == TokenType::MOVE_ASSIGN) {
            assign->is_move = true;
        }

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

std::unique_ptr<DataDef> Parser::parse_data(){
    expect(TokenType::DATA, "Expected 'data'");
    std::string name = current().value;
    int name_line = current().line;
    expect(TokenType::IDENTIFIER, "Expected data name");
    
    // Data type names must start with uppercase (convention for type names)
    if (!name.empty() && !std::isupper(name[0])) {
        ErrorHandler::compiler_error("Data type name '" + name + "' must start with an uppercase letter", name_line);
    }
    
    expect(TokenType::LBRACE, "Expected '{'");

    auto def = std::make_unique<DataDef>();
    def->name = name;

    while(current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE){
        std::string type = current().value;
        // Handle types (excluding VOID - not valid for data fields)
        if(current().type == TokenType::INT || current().type == TokenType::STRING ||
            current().type == TokenType::FLOAT || current().type == TokenType::FLOAT32 ||
            current().type == TokenType::BOOL || current().type == TokenType::IDENTIFIER){
            advance();
        } else {
            ErrorHandler::compiler_error("Expected type in data field", current().line);
        }

        std::string fieldName = current().value;
        expect(TokenType::IDENTIFIER, "Expected field name");
        expect(TokenType::SEMICOLON, "Expected ';'");

        def->fields.push_back({type, fieldName});
    }
    expect(TokenType::RBRACE, "Expected '}'");
    return def;
}

std::unique_ptr<EnumDef> Parser::parse_enum(){
    expect(TokenType::ENUM, "Expected 'enum'");
    std::string name = current().value;
    int name_line = current().line;
    expect(TokenType::IDENTIFIER, "Expected enum name");
    
    // Enum type names must start with uppercase (convention for type names)
    if (!name.empty() && !std::isupper(name[0])) {
        ErrorHandler::compiler_error("Enum type name '" + name + "' must start with an uppercase letter", name_line);
    }
    
    expect(TokenType::LBRACE, "Expected '{'");

    auto def = std::make_unique<EnumDef>();
    def->name = name;

    while(current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE){
        std::string valueName = current().value;
        expect(TokenType::IDENTIFIER, "Expected enum value name");
        def->values.push_back(valueName);

        // Allow optional comma between values
        if(current().type == TokenType::COMMA){
            advance();
        }
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

    // Check for component variable syntax: <{varName} props... />
    // Used to project component variables into the view
    if (current().type == TokenType::LBRACE) {
        advance(); // consume '{'

        // Parse the expression (typically just an identifier)
        auto expr = parse_expression();
        expect(TokenType::RBRACE, "Expected '}' after component variable expression");

        // Get the variable name from the expression
        std::string member_name;
        std::string component_type;

        if (auto* ident = dynamic_cast<Identifier*>(expr.get())) {
            member_name = ident->name;
            // Look up the component type
            auto it = component_member_types.find(member_name);
            if (it != component_member_types.end()) {
                component_type = it->second;

                // Error if type is a built-in handle (not a component)
                if (DefSchema::instance().is_handle(component_type)) {
                    throw std::runtime_error("Variable '" + member_name + "' has type '" + component_type + "' which is a built-in type, not a component. Usage: <{" + member_name + "}/> is only for components at line " + std::to_string(start_line));
                }
            } else {
                throw std::runtime_error("Variable '" + member_name + "' is not a known component member. Use <{var}/> only for component-typed variables at line " + std::to_string(start_line));
            }
        } else {
            throw std::runtime_error("Expected identifier in <{...}/> syntax at line " + std::to_string(start_line));
        }

        auto comp = std::make_unique<ComponentInstantiation>();
        comp->line = start_line;
        comp->is_member_reference = true;
        comp->member_name = member_name;
        comp->component_name = component_type;

        // Parse props (same as regular component props): &prop={value} = reference, :prop={value} = move
        while(current().type == TokenType::IDENTIFIER || current().type == TokenType::AMPERSAND || current().type == TokenType::COLON){
            bool is_ref_prop = false;
            bool is_move_prop = false;
            if(match(TokenType::AMPERSAND)){
                is_ref_prop = true;
            } else if(match(TokenType::COLON)){
                is_move_prop = true;
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
                    prop_value = std::make_unique<FloatLiteral>(std::stod(current().value));
                    advance();
                } else if(match(TokenType::MINUS)){
                    if(current().type == TokenType::INT_LITERAL){
                        prop_value = std::make_unique<IntLiteral>(-std::stoi(current().value));
                        advance();
                    } else if(current().type == TokenType::FLOAT_LITERAL){
                        prop_value = std::make_unique<FloatLiteral>(-std::stod(current().value));
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
                prop_value = std::make_unique<StringLiteral>("true");
            }
            ComponentProp cprop;
            cprop.name = prop_name;
            cprop.value = std::move(prop_value);
            cprop.is_reference = is_ref_prop;
            cprop.is_move = is_move_prop;
            comp->props.push_back(std::move(cprop));
        }

        // Must be self-closing: <{var}/>
        expect(TokenType::SLASH, "Expected '/>' - component variable projection must be self-closing: <{" + member_name + "}/>");
        expect(TokenType::GT, "Expected '>'");

        return comp;
    }

    std::string tag = current().value;
    expect(TokenType::IDENTIFIER, "Expected tag name");

    // Special tag: <route /> - placeholder for router
    if (tag == "route") {
        auto route_placeholder = std::make_unique<RoutePlaceholder>();
        route_placeholder->line = start_line;
        
        // Must be self-closing
        if (current().type != TokenType::SLASH) {
            throw std::runtime_error("<route> must be self-closing: <route /> at line " + std::to_string(start_line));
        }
        expect(TokenType::SLASH, "Expected '/>'");
        expect(TokenType::GT, "Expected '>'");
        
        return route_placeholder;
    }

    // Components must start with uppercase
    // Lowercase tags are always HTML elements
    // Use <{var}/> syntax for component variables
    bool is_component = std::isupper(tag[0]);

    if(is_component){
        // Error if tag is a built-in handle type
        if (DefSchema::instance().is_handle(tag)) {
            ErrorHandler::compiler_error("Type '" + tag + "' cannot be used as a component tag", start_line);
        }

        auto comp = std::make_unique<ComponentInstantiation>();
        comp->line = start_line;
        comp->component_name = tag;

        // Props: &prop={value} = reference, :prop={value} = move, prop={value} = copy
        while(current().type == TokenType::IDENTIFIER || current().type == TokenType::AMPERSAND || current().type == TokenType::COLON){
            bool is_ref_prop = false;
            bool is_move_prop = false;
            if(match(TokenType::AMPERSAND)){
                is_ref_prop = true;
            } else if(match(TokenType::COLON)){
                is_move_prop = true;
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
                    prop_value = std::make_unique<FloatLiteral>(std::stod(current().value));
                    advance();
                } else if(match(TokenType::MINUS)){
                    if(current().type == TokenType::INT_LITERAL){
                        prop_value = std::make_unique<IntLiteral>(-std::stoi(current().value));
                        advance();
                    } else if(current().type == TokenType::FLOAT_LITERAL){
                        prop_value = std::make_unique<FloatLiteral>(-std::stod(current().value));
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
            cprop.is_move = is_move_prop;
            comp->props.push_back(std::move(cprop));
        }

        // Self-closing
        if(match(TokenType::SLASH)){
            expect(TokenType::GT, "Expected '>'");
            return comp;
        }

        expect(TokenType::GT, "Expected '>'");
        ErrorHandler::compiler_error("Custom components must be self-closing for now: " + tag, -1);
    }

    auto el = std::make_unique<HTMLElement>();
    el->line = start_line;
    el->tag = tag;

    // Attributes - accept any token as attribute name except those that end the tag
    while(current().type != TokenType::SLASH && current().type != TokenType::GT && current().type != TokenType::END_OF_FILE){
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

        // Handle hyphenated attribute names (e.g., fill-opacity, stroke-width, data-id)
        while(current().type == TokenType::MINUS && peek().type == TokenType::IDENTIFIER){
            attrName += "-";
            advance(); // consume '-'
            attrName += current().value;
            advance(); // consume identifier part
        }

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
        // ForEach: <for item in items key={item.id}>
        auto viewForEach = std::make_unique<ViewForEachStatement>();
        viewForEach->line = start_line;
        viewForEach->var_name = var_name;
        viewForEach->iterable = std::move(first_expr);

        // Require key attribute for foreach loops
        if (current().type != TokenType::KEY) {
            throw std::runtime_error("Expected 'key' for foreach loop at line " + std::to_string(start_line) + ". Use: <for " + var_name + " in array key={" + var_name + ".id}>");
        }
        advance(); // consume 'key'
        expect(TokenType::ASSIGN, "Expected '=' after 'key'");
        expect(TokenType::LBRACE, "Expected '{' for key expression");
        viewForEach->key_expr = parse_expression();
        expect(TokenType::RBRACE, "Expected '}' after key expression");

        expect(TokenType::GT, "Expected '>'");

        // If iterating over a component array, temporarily add loop var to component_member_types
        // so that <var_name/> syntax works inside the loop
        std::string loop_var_comp_type;
        if (auto* ident = dynamic_cast<Identifier*>(viewForEach->iterable.get())) {
            auto it = component_array_types.find(ident->name);
            if (it != component_array_types.end()) {
                loop_var_comp_type = it->second;
                component_member_types[var_name] = loop_var_comp_type;
            }
        }

        // Parse children until </for>
        while (current().type != TokenType::END_OF_FILE) {
            if (current().type == TokenType::LT && peek().type == TokenType::SLASH && peek(2).type == TokenType::FOR) {
                break;
            }
            viewForEach->children.push_back(parse_view_node());
        }

        // Remove the temporary loop variable from component_member_types
        if (!loop_var_comp_type.empty()) {
            component_member_types.erase(var_name);
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

    // Clear component member types from previous component
    component_member_types.clear();
    component_array_types.clear();

    expect(TokenType::COMPONENT, "Expected 'component'");
    comp.name = current().value;
    comp.line = current().line;

    // Check for collisions with built-in types
    if (DefSchema::instance().is_handle(comp.name)) {
        throw std::runtime_error("Component name '" + comp.name + "' conflicts with a built-in type name at line " + std::to_string(current().line));
    }

    // Validate component name starts with uppercase
    if (!comp.name.empty() && !std::isupper(comp.name[0])) {
        throw std::runtime_error("Component name '" + comp.name + "' must start with an uppercase letter at line " + std::to_string(current().line));
    }

    expect(TokenType::IDENTIFIER, "Expected component name");

    // Parse component parameters (constructor-style): component Name(pub mut int& value = 0)
    if (match(TokenType::LPAREN)) {
        while (current().type != TokenType::RPAREN) {
            auto param = std::make_unique<ComponentParam>();

            // Check for pub keyword (makes param accessible from outside)
            if (current().type == TokenType::PUB) {
                param->is_public = true;
                advance();
            }

            // Check for mut keyword
            if (current().type == TokenType::MUT) {
                param->is_mutable = true;
                advance();
            }

            // Parse type
            if (current().type == TokenType::DEF) {
                // Function parameter: def onclick : void  OR  def onRemove(int) : void
                advance();
                param->is_callback = true;
                param->name = current().value;
                if (is_identifier_token()) {
                    advance();
                } else {
                    expect(TokenType::IDENTIFIER, "Expected param name");
                }

                // Check for optional parameter list: (type1, type2, ...)
                std::vector<std::string> callback_params;
                if (current().type == TokenType::LPAREN) {
                    advance();
                    while (current().type != TokenType::RPAREN && current().type != TokenType::END_OF_FILE) {
                        std::string param_type = current().value;
                        if(current().type == TokenType::INT || current().type == TokenType::STRING ||
                            current().type == TokenType::FLOAT || current().type == TokenType::FLOAT32 ||
                            current().type == TokenType::BOOL ||
                            current().type == TokenType::IDENTIFIER || current().type == TokenType::VOID){
                            advance();
                        } else {
                            throw std::runtime_error("Expected parameter type in callback definition");
                        }

                        // Handle array type
                        if(current().type == TokenType::LBRACKET){
                            advance();
                            expect(TokenType::RBRACKET, "Expected ']'");
                            param_type += "[]";
                        }

                        callback_params.push_back(param_type);
                        param->callback_param_types.push_back(param_type);

                        if (current().type == TokenType::COMMA) {
                            advance();
                        } else {
                            break;
                        }
                    }
                    expect(TokenType::RPAREN, "Expected ')' after callback parameters");
                }

                expect(TokenType::COLON, "Expected ':'");

                std::string retType = current().value;
                if(is_type_token()){
                    advance();
                } else {
                    throw std::runtime_error("Expected return type");
                }

                // Build the webcc::function type with parameter types
                std::string params_str;
                for (size_t i = 0; i < callback_params.size(); ++i) {
                    if (i > 0) params_str += ", ";
                    params_str += convert_type(callback_params[i]);
                }
                param->type = "webcc::function<" + retType + "(" + params_str + ")>";
            } else {
                param->type = current().value;
                if(is_type_token()){
                    advance();
                } else {
                    throw std::runtime_error("Expected param type");
                }

                // Handle reference type
                if(current().type == TokenType::AMPERSAND){
                    param->is_reference = true;
                    advance();
                }

                // Handle array type
                if(current().type == TokenType::LBRACKET){
                    advance();
                    expect(TokenType::RBRACKET, "Expected ']'");
                    param->type += "[]";
                }

                param->name = current().value;
                if (is_identifier_token()) {
                    advance();
                } else {
                    expect(TokenType::IDENTIFIER, "Expected param name");
                }
            }

            // Parse default value
            if(match(TokenType::ASSIGN)){
                param->default_value = parse_expression();
            }

            comp.params.push_back(std::move(param));

            if (current().type == TokenType::COMMA) {
                advance();
            }
        }
        expect(TokenType::RPAREN, "Expected ')'");
    }

    expect(TokenType::LBRACE, "Expected '{'");

    // Parse state variables, methods, view, style, and router blocks
    while(current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE){
        bool is_public = false;
        bool is_mutable = false;
        bool is_shared = false;

        // Check for shared keyword (for enums)
        if (current().type == TokenType::SHARED) {
            is_shared = true;
            advance();
        }

        // Check for pub keyword
        if (current().type == TokenType::PUB) {
            is_public = true;
            advance();
        }

        // Check for mut keyword
        if (current().type == TokenType::MUT) {
            is_mutable = true;
            advance();
        }

        // Variable declaration (note: VOID not valid here, only in return types)
        if(current().type == TokenType::INT || current().type == TokenType::STRING ||
            current().type == TokenType::FLOAT || current().type == TokenType::FLOAT32 ||
            current().type == TokenType::BOOL || current().type == TokenType::IDENTIFIER){
            auto var_decl = std::make_unique<VarDeclaration>();
            var_decl->type = current().value;
            var_decl->is_public = is_public;
            advance();

            // Handle Component.EnumName type syntax for shared enums
            if(current().type == TokenType::DOT){
                advance();
                var_decl->type += "." + current().value;
                expect(TokenType::IDENTIFIER, "Expected enum name after '.'");
            }

            // Handle reference type
            if(current().type == TokenType::AMPERSAND){
                var_decl->is_reference = true;
                advance();
            }

            if(current().type == TokenType::LBRACKET){
                advance();
                if (current().type == TokenType::INT_LITERAL) {
                    // Fixed-size array: Type[N]
                    std::string size = current().value;
                    advance();
                    expect(TokenType::RBRACKET, "Expected ']'");
                    var_decl->type += "[" + size + "]";
                } else {
                    // Dynamic array: Type[]
                    expect(TokenType::RBRACKET, "Expected ']'");
                    var_decl->type += "[]";
                }
            }

            var_decl->name = current().value;
            if (is_identifier_token()) {
                advance();
            } else {
                expect(TokenType::IDENTIFIER, "Expected variable name");
            }
            var_decl->is_mutable = is_mutable;

            if(match(TokenType::ASSIGN)){
                var_decl->initializer = parse_expression();
            }

            if (var_decl->is_reference && !var_decl->initializer) {
                throw std::runtime_error("Reference variable '" + var_decl->name + "' must be initialized immediately.");
            }

            // Track component-type members for view parsing (e.g., "mut Test a;" -> can use <a/> in view)
            // Component types start with uppercase and are not arrays
            if (!var_decl->type.empty() && std::isupper(var_decl->type[0]) &&
                var_decl->type.find('[') == std::string::npos) {
                component_member_types[var_decl->name] = var_decl->type;
            }

            // Track component array types (e.g., "Row[] rows" -> can use <row/> in for loops)
            if (!var_decl->type.empty() && var_decl->type.ends_with("[]")) {
                std::string elem_type = var_decl->type.substr(0, var_decl->type.length() - 2);
                if (!elem_type.empty() && std::isupper(elem_type[0])) {
                    component_array_types[var_decl->name] = elem_type;
                }
            }

            expect(TokenType::SEMICOLON, "Expected ';'");
            comp.state.push_back(std::move(var_decl));
        }
        else if (is_mutable && !is_public && current().type != TokenType::DEF) {
            throw std::runtime_error("Expected variable declaration after 'mut'");
        }
        // Data definition
        else if(current().type == TokenType::DATA){
            comp.data.push_back(parse_data());
        }
        // Enum definition (with optional shared prefix)
        else if(current().type == TokenType::ENUM){
            auto enum_def = parse_enum();
            enum_def->is_shared = is_shared;
            if (is_shared) {
                enum_def->owner_component = comp.name;
            }
            comp.enums.push_back(std::move(enum_def));
        }
        // Function definition (with optional pub prefix)
        else if(current().type == TokenType::DEF){
            advance();
            FunctionDef func;
            func.is_public = is_public;
            func.name = current().value;
            int func_line = current().line;
            expect(TokenType::IDENTIFIER, "Expected function name");
            
            // Method names must start with lowercase (to distinguish from component/type construction)
            if (!func.name.empty() && std::isupper(func.name[0])) {
                ErrorHandler::compiler_error("Method name '" + func.name + "' must start with a lowercase letter", func_line);
            }
            
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
                    current().type == TokenType::FLOAT32 ||
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
                // Allow 'key' and 'data' keywords as parameter name
                if (current().type == TokenType::IDENTIFIER || current().type == TokenType::KEY || current().type == TokenType::DATA) {
                    advance();
                } else {
                    throw std::runtime_error("Expected parameter name at line " + std::to_string(current().line));
                }

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

            // Parameters are optional - tick {} or tick(float dt) {}
            if(current().type == TokenType::LPAREN) {
                advance();
                // Parse parameters
                while(current().type != TokenType::RPAREN){
                    bool is_mutable = false;
                    if (current().type == TokenType::MUT) {
                        is_mutable = true;
                        advance();
                    }

                    std::string paramType = current().value;
                    if(current().type == TokenType::INT || current().type == TokenType::FLOAT ||
                        current().type == TokenType::FLOAT32 ||
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
                    // Allow 'key' and 'data' keywords as parameter name
                    if (current().type == TokenType::IDENTIFIER || current().type == TokenType::KEY || current().type == TokenType::DATA) {
                        advance();
                    } else {
                        throw std::runtime_error("Expected parameter name at line " + std::to_string(current().line));
                    }

                    func.params.push_back({paramType, paramName, is_mutable, is_reference});

                    if(current().type == TokenType::COMMA){
                        advance();
                    }
                }
                expect(TokenType::RPAREN, "Expected ')'");
            }

            expect(TokenType::LBRACE, "Expected '{'");

            while(current().type != TokenType::RBRACE){
                func.body.push_back(parse_statement());
            }

            expect(TokenType::RBRACE, "Expected '}'");
            comp.methods.push_back(std::move(func));
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
        // Router block
        else if(current().type == TokenType::ROUTER){
            if (comp.router) {
                throw std::runtime_error("Component '" + comp.name + "' already has a router block at line " + std::to_string(current().line));
            }
            comp.router = parse_router();
        }
        // View block
        else if(current().type == TokenType::VIEW){
            advance();
            expect(TokenType::LBRACE, "Expected '{'");
            while(current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE){
                comp.render_roots.push_back(parse_view_node());
            }
            expect(TokenType::RBRACE, "Expected '}'");
        }
        else {
            advance();
        }
    }

    return comp;
}

std::unique_ptr<RouterDef> Parser::parse_router() {
    auto router = std::make_unique<RouterDef>();
    router->line = current().line;
    
    expect(TokenType::ROUTER, "Expected 'router'");
    expect(TokenType::LBRACE, "Expected '{'");
    
    while (current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE) {
        RouteEntry entry;
        entry.line = current().line;
        
        // Parse route path (string literal)
        if (current().type != TokenType::STRING_LITERAL) {
            throw std::runtime_error("Expected route path string at line " + std::to_string(current().line));
        }
        entry.path = current().value;
        advance();
        
        // Expect =>
        if (current().type != TokenType::ARROW) {
            throw std::runtime_error("Expected '=>' after route path at line " + std::to_string(current().line));
        }
        advance();
        
        // Parse component name
        if (current().type != TokenType::IDENTIFIER) {
            throw std::runtime_error("Expected component name after '=>' at line " + std::to_string(current().line));
        }
        entry.component_name = current().value;
        advance();
        
        // Optional: parse component arguments (ComponentName(arg1, arg2))
        // Uses same syntax as component construction: &ref, :move, name = value
        if (current().type == TokenType::LPAREN) {
            advance();
            entry.args = parse_call_args(TokenType::RPAREN);
            expect(TokenType::RPAREN, "Expected ')' after component arguments");
        }
        
        router->routes.push_back(std::move(entry));
        
        // Optional comma between entries
        if (current().type == TokenType::COMMA) {
            advance();
        }
    }
    
    expect(TokenType::RBRACE, "Expected '}'");
    
    if (router->routes.empty()) {
        throw std::runtime_error("Router block must have at least one route at line " + std::to_string(router->line));
    }
    
    return router;
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
            } else if(key == "title"){
                app_config.title = current().value;
                expect(TokenType::STRING_LITERAL, "Expected string");
            } else if(key == "description"){
                app_config.description = current().value;
                expect(TokenType::STRING_LITERAL, "Expected string");
            } else if(key == "lang"){
                app_config.lang = current().value;
                expect(TokenType::STRING_LITERAL, "Expected string");
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
        } else if(current().type == TokenType::ENUM){
            // Global enum (outside any component)
            global_enums.push_back(parse_enum());
        } else if(current().type == TokenType::DATA){
            // Global data type (outside any component)
            global_data.push_back(parse_data());
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
