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
    std::set<std::string> data_type_names;  // Fully-qualified data type names (e.g., "Supabase_Credentials")
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
    virtual std::string to_webcc() { return ""; }
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
    std::string module_name;                   // Current component module name
    std::set<std::string> local_data_types;    // Data types defined in this component
    std::set<std::string> local_enum_types;    // Enum types defined in this component
    std::set<std::string> global_data_types;   // Fully-qualified global data type names
    std::map<std::string, int> method_param_counts;  // Method name -> param count
    std::map<std::string, std::string> component_symbol_types; // Component params/state name -> type
    std::map<std::string, std::string> method_symbol_types;    // Current method params/locals name -> type
    
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
        component_symbol_types.clear();
        method_symbol_types.clear();
    }

    void set_module_scope(const std::string& mod_name,
                          const std::set<std::string>& global_types) {
        module_name = mod_name;
        global_data_types = global_types;
    }
    
    void clear() {
        component_name.clear();
        module_name.clear();
        local_data_types.clear();
        local_enum_types.clear();
        global_data_types.clear();
        method_param_counts.clear();
        component_symbol_types.clear();
        method_symbol_types.clear();
    }

    void set_component_symbol_type(const std::string& name, const std::string& type) {
        component_symbol_types[name] = type;
    }

    void begin_method_scope() {
        method_symbol_types.clear();
    }

    void end_method_scope() {
        method_symbol_types.clear();
    }

    void set_method_symbol_type(const std::string& name, const std::string& type) {
        method_symbol_types[name] = type;
    }

    std::string get_symbol_type(const std::string& name) const {
        auto method_it = method_symbol_types.find(name);
        if (method_it != method_symbol_types.end()) {
            return method_it->second;
        }
        auto component_it = component_symbol_types.find(name);
        if (component_it != component_symbol_types.end()) {
            return component_it->second;
        }
        return "";
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
        std::string module_scoped = module_name.empty() ? type : (module_name + "_" + type);
        if (global_data_types.count(module_scoped)) {
            return module_scoped;
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
