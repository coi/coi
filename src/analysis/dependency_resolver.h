#pragma once

#include <string>
#include <set>
#include <vector>

// Forward declarations
struct Component;
class ASTNode;

// Collect child component names from a node
void collect_component_deps(ASTNode *node, std::set<std::string> &deps);

// The components the app can reach from its root (qualified names), through views, routes,
// params, state and method bodies
std::set<std::string> reachable_components(std::vector<Component> &components, const std::string &root_qname);

// Topologically sort components so dependencies come first
std::vector<Component *> topological_sort_components(std::vector<Component> &components);

