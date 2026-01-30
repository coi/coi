#pragma once

#include <string>

// Helper to strip redundant outer parentheses from a condition expression
// This avoids warnings like: if((x == 1)) -> if(x == 1)
inline std::string strip_outer_parens(const std::string& expr) {
    if (expr.size() >= 2 && expr.front() == '(' && expr.back() == ')') {
        // Check if the outer parens are actually matching
        int depth = 0;
        for (size_t i = 0; i < expr.size() - 1; i++) {
            if (expr[i] == '(') depth++;
            else if (expr[i] == ')') depth--;
            // If depth reaches 0 before the end, outer parens don't match
            if (depth == 0) return expr;
        }
        // The outer parens are matching, strip them
        return expr.substr(1, expr.size() - 2);
    }
    return expr;
}
