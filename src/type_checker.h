#pragma once

#include "ast/ast.h"
#include <string>
#include <map>
#include <vector>

// Type normalization: converts user-facing types to internal representation
// e.g., "int" -> "int32", "float" -> "float32"
std::string normalize_type(const std::string &type);

// Check if source type can be assigned to target type
// Handles arrays, handle inheritance, numeric conversions
bool is_compatible_type(const std::string &source, const std::string &target);

// Infer the type of an expression given a scope of variable->type mappings
std::string infer_expression_type(Expression *expr, const std::map<std::string, std::string> &scope);

// Validate types across all components:
// - Parameter and state variable initialization
// - Method body statements
// - Return types
void validate_types(const std::vector<Component> &components, 
                    const std::vector<std::unique_ptr<EnumDef>> &global_enums = {},
                    const std::vector<std::unique_ptr<DataDef>> &global_data = {});

// Validate mutability constraints:
// - Only mutable variables can be modified
void validate_mutability(const std::vector<Component> &components);

// Validate view hierarchy:
// - Component instantiation props match declarations
// - Reference params are passed correctly
// - Callback argument types match
void validate_view_hierarchy(const std::vector<Component> &components);
