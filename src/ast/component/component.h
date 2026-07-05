#pragma once

#include "../node.h"
#include "../definitions.h"
#include "../statements.h"
#include "../view.h"
#include <cstdint>
#include <sstream>

// A dynamic segment captured from a route path, e.g. ":id" in "/users/:id".
struct RouteParam {
    std::string name;   // segment name without the ':' (e.g. "id"); binds to the
                        // target component's param of the same name
    std::string type;   // that param's declared type (string/int/bool), resolved
                        // by the type checker; drives runtime parsing
};

// One slot in the target component's constructor, in declaration order. The type
// checker computes this so codegen can fill each param from the right source.
struct RouteCtorSlot {
    bool is_path_param = false;  // true  -> value comes from a captured URL segment
    int index = 0;               // index into path_params (if path) or args (if explicit)
};

// Route entry for router block
struct RouteEntry {
    std::string path;                              // e.g., "/", "/users/:id" (empty for else route)
    std::string component_name;                    // e.g., "Landing", "Dashboard"
    std::string module_name;                       // Module of the target component (filled by type checker)
    std::vector<CallArg> args;                     // Explicit component arguments (same as component construction)
    std::vector<RouteParam> path_params;           // ':' segments in path order (names from parser, types from type checker)
    std::vector<RouteCtorSlot> ctor_order;         // How to fill the component constructor (built by type checker)
    bool is_default = false;                       // True for 'else' route (catch-all)
    int line = 0;
};

// Router definition block
struct RouterDef {
    std::vector<RouteEntry> routes;
    bool has_route_placeholder = false;  // Set during view validation
    int line = 0;
};

struct SignalParam {
    std::string type;
    std::string name;
};

struct SignalDef {
    std::string name;
    std::vector<SignalParam> params;
    bool is_public = false;
    int line = 0;
};

struct ListenEntry {
    std::string target_name;         // Component instance being listened to
    std::string signal_name;         // Signal name on target component
    std::string handler_method_name; // Synthesized method generated from listen body
    std::vector<std::string> param_types;
    // True when the listen target is a reference param/state (compiled as pointer).
    // Used by codegen to emit target->... instead of target.... for add/remove listener calls.
    bool target_is_reference = false;
    int line = 0;
};

struct Component : ASTNode {
    std::string name;
    std::string module_name;  // Module this component belongs to
    std::string source_file;  // Absolute path to the file this component is defined in
    bool is_public = false;   // Requires pub keyword to be importable
    std::string css;
    std::string global_css;
    std::vector<std::unique_ptr<DataDef>> data;
    std::vector<std::unique_ptr<EnumDef>> enums;
    std::vector<std::unique_ptr<VarDeclaration>> state;
    std::vector<std::unique_ptr<ComponentParam>> params;
    std::vector<SignalDef> signals;
    std::vector<ListenEntry> listen_entries;
    std::vector<FunctionDef> methods;
    std::vector<std::unique_ptr<ASTNode>> render_roots;
    std::unique_ptr<RouterDef> router;  // Optional router block

    void collect_child_components(ASTNode* node, std::map<std::string, int>& counts);
    void collect_child_updates(ASTNode* node, std::map<std::string, std::vector<std::string>>& updates, std::map<std::string, int>& counters);
    std::string to_webcc() override { static CompilerSession s; return to_webcc(s); }
    std::string to_webcc(CompilerSession& session);
};

struct AppConfig {
    std::string root_component;
    std::map<std::string, std::string> routes;
    std::string title;
    std::string description;
    std::string lang = "en";
    // Deploy base, emitted as <base href>. Set to a subpath (e.g. "/coi/") for
    // subpath deploys like GitHub project pages.
    std::string base = "/";
};

using EventMasks = std::map<std::string, uint64_t>;

// Centralized metadata for supported DOM events used by code generation.
struct EventSpec
{
    const char *type;                    // Event name in templates/handlers (e.g., "click", "input")
    const char *dispatcher_name;         // Global dispatcher symbol used for bind/remove
    const char *dispatcher_lambda_params; // Lambda signature used when registering dispatcher callback
    const char *dispatcher_call_arg;     // Argument forwarded from dispatcher lambda into _handler_* call
    const char *handler_param_decl;      // Generated _handler_* parameter declaration
    const char *handler_call_arg;        // Argument passed to user callback inside generated _handler_*
};

const std::vector<EventSpec> &get_event_specs();
const EventSpec *find_event_spec(const std::string &event_type);
bool has_event_mask(const EventMasks &masks, const std::string &event_type);
uint64_t get_event_mask(const EventMasks &masks, const std::string &event_type);
std::string get_event_mask_name(const std::string &event_type);

void emit_component_router_methods(std::stringstream &ss, const Component &component);

void emit_component_lifecycle_methods(std::stringstream &ss,
                                      CompilerSession &session,
                                      const Component &component,
                                      const EventMasks &masks,
                                      const std::vector<IfRegion> &if_regions,
                                      int element_count,
                                      const std::map<std::string, int> &component_members);

EventMasks compute_event_masks(const std::vector<EventHandler> &handlers);
std::set<int> get_elements_for_event(const std::vector<EventHandler> &handlers, const std::string &event_type);
void emit_event_mask_constants(std::stringstream &ss, const EventMasks &masks);
void emit_event_registration(std::stringstream &ss,
                             int element_count,
                             const std::vector<EventHandler> &handlers,
                             const std::string &event_type,
                             const std::string &mask_name,
                             const std::string &dispatcher_name,
                             const std::string &lambda_params,
                             const std::string &call_suffix);
void emit_all_event_registrations(std::stringstream &ss,
                                  int element_count,
                                  const std::vector<EventHandler> &handlers,
                                  const EventMasks &masks);
