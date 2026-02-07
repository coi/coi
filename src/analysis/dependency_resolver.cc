#include "dependency_resolver.h"
#include "ast/ast.h"
#include "ast/node.h"
#include "../cli/error.h"
#include <map>
#include <queue>

// Collect child component names from a node (returns qualified names)
void collect_component_deps(ASTNode *node, std::set<std::string> &deps)
{
    if (!node)
        return;
    if (auto *comp_inst = dynamic_cast<ComponentInstantiation *>(node))
    {
        // Use qualified name (module_prefix + component_name)
        std::string qname = comp_inst->module_prefix.empty() 
            ? comp_inst->component_name 
            : comp_inst->module_prefix + "_" + comp_inst->component_name;
        deps.insert(qname);
    }
    else if (auto *el = dynamic_cast<HTMLElement *>(node))
    {
        for (const auto &child : el->children)
        {
            collect_component_deps(child.get(), deps);
        }
    }
    else if (auto *viewIf = dynamic_cast<ViewIfStatement *>(node))
    {
        for (const auto &child : viewIf->then_children)
        {
            collect_component_deps(child.get(), deps);
        }
        for (const auto &child : viewIf->else_children)
        {
            collect_component_deps(child.get(), deps);
        }
    }
    else if (auto *viewFor = dynamic_cast<ViewForRangeStatement *>(node))
    {
        for (const auto &child : viewFor->children)
        {
            collect_component_deps(child.get(), deps);
        }
    }
    else if (auto *viewForEach = dynamic_cast<ViewForEachStatement *>(node))
    {
        for (const auto &child : viewForEach->children)
        {
            collect_component_deps(child.get(), deps);
        }
    }
}

// Extract base type name from array types (e.g., "Ball[]" -> "Ball")
static std::string extract_base_type_name(const std::string &type)
{
    size_t bracket = type.find('[');
    if (bracket != std::string::npos)
    {
        return type.substr(0, bracket);
    }
    return type;
}

// Topologically sort components so dependencies come first
std::vector<Component *> topological_sort_components(std::vector<Component> &components)
{
    std::map<std::string, Component *> comp_map;
    std::map<std::string, std::set<std::string>> dependencies;
    std::map<std::string, int> in_degree;

    for (auto &comp : components)
    {
        std::string qname = qualified_name(comp.module_name, comp.name);
        comp_map[qname] = &comp;
        in_degree[qname] = 0;
    }

    // Build dependency graph
    for (auto &comp : components)
    {
        std::string comp_qname = qualified_name(comp.module_name, comp.name);
        std::set<std::string> deps;
        // Collect dependencies from view
        for (const auto &root : comp.render_roots)
        {
            collect_component_deps(root.get(), deps);
        }
        // Collect dependencies from router routes
        if (comp.router)
        {
            for (const auto &route : comp.router->routes)
            {
                deps.insert(qualified_name(route.module_name, route.component_name));
            }
        }
        // Collect dependencies from parameter types (e.g., Vector pos)
        for (const auto &param : comp.params)
        {
            std::string base_type = extract_base_type_name(param->type);
            // Handle Module::Type syntax
            size_t dcolon = base_type.find("::");
            if (dcolon != std::string::npos)
            {
                std::string module = base_type.substr(0, dcolon);
                std::string name = base_type.substr(dcolon + 2);
                base_type = module + "_" + name;
            }
            if (comp_map.count(base_type))
            {
                deps.insert(base_type);
            }
        }
        // Collect dependencies from state variable types
        for (const auto &var : comp.state)
        {
            std::string base_type = extract_base_type_name(var->type);
            // Handle Module::Type syntax
            size_t dcolon = base_type.find("::");
            if (dcolon != std::string::npos)
            {
                std::string module = base_type.substr(0, dcolon);
                std::string name = base_type.substr(dcolon + 2);
                base_type = module + "_" + name;
            }
            if (comp_map.count(base_type))
            {
                deps.insert(base_type);
            }
        }
        dependencies[comp_qname] = deps;
    }

    // Calculate in-degrees
    for (auto &[name, deps] : dependencies)
    {
        for (auto &dep : deps)
        {
            if (comp_map.count(dep))
            {
                in_degree[name]++;
            }
        }
    }

    // Kahn's algorithm
    std::queue<std::string> queue;
    for (auto &[name, degree] : in_degree)
    {
        if (degree == 0)
        {
            queue.push(name);
        }
    }

    std::vector<Component *> sorted;
    while (!queue.empty())
    {
        std::string curr = queue.front();
        queue.pop();
        sorted.push_back(comp_map[curr]);

        // For each component that depends on curr, decrease in-degree
        for (auto &[name, deps] : dependencies)
        {
            if (deps.count(curr))
            {
                in_degree[name]--;
                if (in_degree[name] == 0)
                {
                    queue.push(name);
                }
            }
        }
    }

    // Check for cycles
    if (sorted.size() != components.size())
    {
        ErrorHandler::compiler_error("Circular dependency detected among components", -1);
    }

    return sorted;
}
