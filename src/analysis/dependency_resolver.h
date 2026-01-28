#pragma once

#include <string>
#include <set>
#include <vector>

// Forward declarations
struct Component;
class ASTNode;

// Collect child component names from a node
void collect_component_deps(ASTNode *node, std::set<std::string> &deps);

// Topologically sort components so dependencies come first
std::vector<Component *> topological_sort_components(std::vector<Component> &components);

