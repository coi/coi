#pragma once

#include <string>

enum class TokenType {
    // Keywords
    COMPONENT, DEF, RETURN, STRUCT, VIEW, IF, ELSE, FOR, WHILE, TICK, PROP, STYLE,
    // Types
    INT, FLOAT, STRING, BOOL, VOID,
    // Literals
    INT_LITERAL, FLOAT_LITERAL, STRING_LITERAL, TRUE, FALSE,
    // Identifiers
    IDENTIFIER,
    // Operatos
    PLUS, MINUS, STAR, SLASH, PERCENT, ASSIGN, EQ, NEQ, LT, GT, LTE, GTE,
    PLUS_ASSIGN, MINUS_ASSIGN, PLUS_PLUS, MINUS_MINUS,
    // Delimiters
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET, LANGLE, RANGLE,
    SEMICOLON, COMMA, DOT, COLON, ARROW,
    // Special
    END_OF_FILE, UNKNOWN
};

struct Token {
    TokenType type;
    std::string value;
    int line;
    int column;
};
