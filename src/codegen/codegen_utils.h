#pragma once

#include <string>
#include <vector>

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

// Generate a lambda wrapper for a member function reference
// type: "webcc::function<void(webcc::string)>"
// method_name: "handleNoopEvent"
// Returns: "[this](const webcc::string& _arg0) { this->handleNoopEvent(_arg0); }"
inline std::string generate_member_function_lambda(const std::string &type, const std::string &method_name)
{
    // Parse function type to extract parameters
    // Format: webcc::function<return_type(param_types...)>
    size_t left_angle = type.find('<');
    size_t right_angle = type.rfind('>');
    if (left_angle == std::string::npos || right_angle == std::string::npos)
        return method_name;

    std::string inner = type.substr(left_angle + 1, right_angle - left_angle - 1);
    // inner is "void(webcc::string)" or "void()" etc.

    size_t left_paren = inner.find('(');
    size_t right_paren = inner.rfind(')');
    if (left_paren == std::string::npos || right_paren == std::string::npos)
        return method_name;

    std::string return_type = inner.substr(0, left_paren);
    std::string params_str = inner.substr(left_paren + 1, right_paren - left_paren - 1);

    // Parse parameters (comma-separated)
    std::vector<std::string> param_types;
    if (!params_str.empty())
    {
        int depth = 0;
        std::string current;
        for (char c : params_str)
        {
            if (c == '<')
                depth++;
            else if (c == '>')
                depth--;
            else if (c == ',' && depth == 0)
            {
                // Trim whitespace
                size_t start = current.find_first_not_of(" \t");
                size_t end = current.find_last_not_of(" \t");
                if (start != std::string::npos)
                {
                    param_types.push_back(current.substr(start, end - start + 1));
                }
                current.clear();
                continue;
            }
            current += c;
        }
        if (!current.empty())
        {
            size_t start = current.find_first_not_of(" \t");
            size_t end = current.find_last_not_of(" \t");
            if (start != std::string::npos)
            {
                param_types.push_back(current.substr(start, end - start + 1));
            }
        }
    }

    // Generate lambda
    std::string result = "[this](";
    for (size_t i = 0; i < param_types.size(); ++i)
    {
        if (i > 0)
            result += ", ";
        result += "const " + param_types[i] + "& _arg" + std::to_string(i);
    }
    result += ") { this->" + method_name + "(";
    for (size_t i = 0; i < param_types.size(); ++i)
    {
        if (i > 0)
            result += ", ";
        result += "_arg" + std::to_string(i);
    }
    result += "); }";
    return result;
}
