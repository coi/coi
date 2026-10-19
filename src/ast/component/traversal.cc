#include "component.h"
#include "../../codegen/codegen_utils.h"

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

void Component::collect_child_updates(ASTNode *node, std::map<std::string, std::vector<std::string>> &updates, std::map<std::string, int> &counters,
                                      std::map<MemberDependency, std::vector<std::string>> *member_updates)
{
    if (auto comp = dynamic_cast<ComponentInstantiation *>(node))
    {
        std::string instance_name;
        if (comp->is_member_reference)
        {
            instance_name = cpp_name(comp->member_name);
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
            std::string assign = prop.is_reference ? "" : instance_name + "." + cpp_name(prop.name) + " = " + prop.value->to_webcc() + "; ";
            std::string call = "        " + assign + instance_name + "._refresh_" + prop.name + "();\n";
            for (const auto &dep : deps)
            {
                updates[dep].push_back(call);
            }
            if (member_updates)
            {
                std::set<MemberDependency> member_deps;
                prop.value->collect_member_dependencies(member_deps);
                for (const auto &dep : member_deps)
                    (*member_updates)[dep].push_back(call);
            }
        }
    }
    if (auto el = dynamic_cast<HTMLElement *>(node))
    {
        for (auto &child : el->children)
        {
            collect_child_updates(child.get(), updates, counters, member_updates);
        }
    }
    if (auto viewIf = dynamic_cast<ViewIfStatement *>(node))
    {
        for (auto &child : viewIf->then_children)
        {
            collect_child_updates(child.get(), updates, counters, member_updates);
        }
        for (auto &child : viewIf->else_children)
        {
            collect_child_updates(child.get(), updates, counters, member_updates);
        }
    }
}
