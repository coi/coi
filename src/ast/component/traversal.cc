#include "component.h"

void Component::collect_child_components(ASTNode *node, std::map<std::string, int> &counts)
{
    if (auto comp = dynamic_cast<ComponentInstantiation *>(node))
    {
        if (!comp->is_member_reference)
        {
            counts[qualified_name(comp->module_prefix, comp->component_name)]++;
        }
    }
    if (auto el = dynamic_cast<HTMLElement *>(node))
    {
        for (auto &child : el->children)
        {
            collect_child_components(child.get(), counts);
        }
    }
    if (auto viewIf = dynamic_cast<ViewIfStatement *>(node))
    {
        for (auto &child : viewIf->then_children)
        {
            collect_child_components(child.get(), counts);
        }
        for (auto &child : viewIf->else_children)
        {
            collect_child_components(child.get(), counts);
        }
    }
}

void Component::collect_child_updates(ASTNode *node, std::map<std::string, std::vector<std::string>> &updates, std::map<std::string, int> &counters)
{
    if (auto comp = dynamic_cast<ComponentInstantiation *>(node))
    {
        std::string instance_name;
        if (comp->is_member_reference)
        {
            instance_name = comp->member_name;
        }
        else
        {
            instance_name = comp->component_name + "_" + std::to_string(counters[comp->component_name]++);
        }

        for (const auto &prop : comp->props)
        {
            if (prop.is_callback || prop.is_move || !prop.value)
                continue;
            std::set<std::string> deps;
            prop.value->collect_dependencies(deps);
            // a reference already points at the parent's value; a value prop is copied again
            std::string assign = prop.is_reference ? "" : instance_name + "." + prop.name + " = " + prop.value->to_webcc() + "; ";
            for (const auto &dep : deps)
            {
                updates[dep].push_back("        " + assign + instance_name + "._refresh_" + prop.name + "();\n");
            }
        }
    }
    if (auto el = dynamic_cast<HTMLElement *>(node))
    {
        for (auto &child : el->children)
        {
            collect_child_updates(child.get(), updates, counters);
        }
    }
    if (auto viewIf = dynamic_cast<ViewIfStatement *>(node))
    {
        for (auto &child : viewIf->then_children)
        {
            collect_child_updates(child.get(), updates, counters);
        }
        for (auto &child : viewIf->else_children)
        {
            collect_child_updates(child.get(), updates, counters);
        }
    }
}
