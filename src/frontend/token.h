#pragma once

#include <string>

enum class TokenType {
    // Keywords
    COMPONENT, DEF, RETURN, POD, VIEW, IF, ELSE, FOR, TICK, INIT, MOUNT, STYLE, MUT, IMPORT, SHARED, IN, PUB, KEY, ENUM, ROUTER, MODULE,
    // Types
    INT, FLOAT, FLOAT32, STRING, BOOL, VOID,
    // Literals
    INT_LITERAL, FLOAT_LITERAL, STRING_LITERAL, TEMPLATE_STRING, TRUE, FALSE,
    // Identifiers
    IDENTIFIER,
    // Operators
    PLUS, MINUS, STAR, SLASH, PERCENT, ASSIGN, MOVE_ASSIGN, EQ, NEQ, LT, GT, LTE, GTE,
    PLUS_ASSIGN, MINUS_ASSIGN, STAR_ASSIGN, SLASH_ASSIGN, PERCENT_ASSIGN,
    PLUS_PLUS, MINUS_MINUS, AND, OR, NOT, QUESTION,
    // Bitwise operators
    PIPE, CARET, TILDE, LSHIFT, RSHIFT,
    // Bitwise compound assignment
    AMPERSAND_ASSIGN, PIPE_ASSIGN, CARET_ASSIGN, LSHIFT_ASSIGN, RSHIFT_ASSIGN,
    // Delimiters
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET,
    SEMICOLON, COMMA, DOT, COLON, DOUBLE_COLON, ARROW, AMPERSAND,
    // Special
    END_OF_FILE, UNKNOWN
};

struct Token {
    TokenType type;
    std::string value;
    int line;
    int column;
};
