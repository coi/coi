#pragma once

#include "node.h"
#include <vector>
#include <string>

// =========================================================
// STRING CONCATENATION OPTIMIZATION
// =========================================================
// Detects string + value chains and generates formatter-based code
// instead of allocating temporary strings for each concatenation.

constexpr int FORMATTER_BUFFER_SIZE = 512;

// Check if an expression is a string literal or starts a string concat chain
bool is_string_expr(Expression* expr);

// Flatten a string concatenation chain into a vector of expressions
void flatten_string_concat(Expression* expr, std::vector<Expression*>& parts);

// Generate the formatter append statements for a list of parts
std::string generate_formatter_appends(const std::vector<std::string>& parts);
std::string generate_formatter_appends(const std::vector<Expression*>& parts);

// Generate formatter code that returns a coi::string (expression context)
std::string generate_formatter_expr(const std::vector<Expression*>& parts);

// Generate formatter code as a statement block that calls a function with c_str()
std::string generate_formatter_block(const std::vector<std::string>& parts,
                                     const std::string& callback_prefix,
                                     const std::string& callback_suffix = ")");

std::string generate_formatter_block(const std::vector<Expression*>& parts,
                                     const std::string& callback_prefix,
                                     const std::string& callback_suffix = ")");

// Forward declare StringLiteral for the specialized function
struct StringLiteral;

// Generate formatter block for StringLiteral with interpolations
std::string generate_formatter_block_from_string_literal(StringLiteral* strLit,
                                                         const std::string& callback_prefix,
                                                         const std::string& callback_suffix = ")");
