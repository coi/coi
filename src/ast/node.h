#pragma once

#include <string>
#include <vector>
#include <memory>
#include <set>
#include <map>
#include <sstream>
#include <functional>

// Forward declarations
struct Expression;
struct Statement;

// Info about a component's pub mut members (for parent-child reactivity wiring)
struct ComponentMemberInfo {
    std::set<std::string> pub_mut_members;  // Names of pub mut params (e.g., "x", "y" for Vector)
};

// Cross-component state that persists across all components in one compilation
struct CompilerSession {
    std::set<std::string> components_with_tick;  // Components that have tick methods
    std::map<std::string, ComponentMemberInfo> component_info;  // Component name -> member info
};

// Represents a dependency on a member of an object (e.g., net.connected)
struct MemberDependency {
    std::string object;   // e.g., "net"
    std::string member;   // e.g., "connected"
    
    bool operator<(const MemberDependency& other) const {
        if (object != other.object) return object < other.object;
        return member < other.member;
    }
};

// Base AST node
struct ASTNode {
    virtual ~ASTNode() = default;
    virtual std::string to_webcc() = 0;
    virtual void collect_dependencies(std::set<std::string>& deps) {}
    virtual void collect_member_dependencies(std::set<MemberDependency>& member_deps) {}
    int line = 0;
};

// Base for expressions (things that return values)
struct Expression : ASTNode {
    virtual bool is_static() { return false; }
};

// Base for statements (actions)
struct Statement : ASTNode {};

// Context for component-local type resolution and method signature tracking
struct ComponentTypeContext {
    std::string component_name;                // Current component being compiled
    std::set<std::string> local_data_types;    // Data types defined in this component
    std::set<std::string> local_enum_types;    // Enum types defined in this component
    std::map<std::string, int> method_param_counts;  // Method name -> param count
    
    static ComponentTypeContext& instance() {
        static ComponentTypeContext ctx;
        return ctx;
    }
    
    void set(const std::string& comp_name, 
             const std::set<std::string>& data_types,
             const std::set<std::string>& enum_types) {
        component_name = comp_name;
        local_data_types = data_types;
        local_enum_types = enum_types;
        method_param_counts.clear();
    }
    
    void clear() {
        component_name.clear();
        local_data_types.clear();
        local_enum_types.clear();
        method_param_counts.clear();
    }
    
    // Register a method's param count
    void register_method(const std::string& name, int param_count) {
        method_param_counts[name] = param_count;
    }
    
    // Get a method's param count (-1 if unknown)
    int get_method_param_count(const std::string& name) const {
        auto it = method_param_counts.find(name);
        return it != method_param_counts.end() ? it->second : -1;
    }
    
    // Check if a type is component-local and return prefixed name if so
    std::string resolve(const std::string& type) const {
        if (component_name.empty()) return type;
        if (local_data_types.count(type) || local_enum_types.count(type)) {
            return component_name + "_" + type;
        }
        return type;
    }
    
    bool is_local(const std::string& type) const {
        return local_data_types.count(type) || local_enum_types.count(type);
    }
};

// Type conversion utility
std::string convert_type(const std::string& type);

// Generate qualified name with module prefix (e.g., "TurboUI_Button" for module "TurboUI", name "Button")
inline std::string qualified_name(const std::string& module_name, const std::string& name) {
    if (module_name.empty()) return name;
    return module_name + "_" + name;
}
