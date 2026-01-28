#pragma once

#include <iostream>
#include <string>
#include <set>
#include <vector>

// Forward declarations
struct Component;

// Feature flags detected from code analysis
struct FeatureFlags
{
    // DOM event dispatchers
    bool click = false;   // onclick handlers
    bool input = false;   // oninput handlers
    bool change = false;  // onchange handlers
    bool keydown = false; // onkeydown handlers (element-level)
    // Runtime features
    bool keyboard = false;    // Global key state tracking (Input.isKeyDown)
    bool router = false;      // Browser history/popstate (any component)
    bool websocket = false;   // WebSocket connections
    bool fetch = false;       // HTTP fetch requests
    bool json = false;        // JSON parsing (Json.parse)
};

// Detect which features are actually used by analyzing components
FeatureFlags detect_features(const std::vector<Component> &components,
                              const std::set<std::string> &headers);

// Emit global declarations for enabled features
void emit_feature_globals(std::ostream &out, const FeatureFlags &f);

// Emit event handlers for enabled features
void emit_feature_event_handlers(std::ostream &out, const FeatureFlags &f);

// Check if the Dispatcher template is needed
bool needs_dispatcher(const FeatureFlags &f);

// Emit initialization code for enabled features
void emit_feature_init(std::ostream &out, const FeatureFlags &f, const std::string &root_comp);

