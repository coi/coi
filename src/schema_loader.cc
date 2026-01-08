#include "schema_loader.h"
#include <iostream>
#include <algorithm>

SchemaLoader& SchemaLoader::instance() {
    static SchemaLoader instance;
    return instance;
}

void SchemaLoader::init() {
    for (size_t i = 0; i < coi::SCHEMA_COUNT; ++i) {
        const auto& entry = coi::SCHEMA[i];
        entries_[entry.func_name] = &entry;
    }
    for (size_t i = 0; i < coi::HANDLE_COUNT; ++i) {
        handles_.push_back(coi::HANDLES[i]);
    }
    // Load inheritance table
    for (const auto* kv = coi::HANDLE_INHERITANCE; kv->first != nullptr; ++kv) {
        handle_inheritance_[kv->first] = kv->second;
    }
}

const coi::SchemaEntry* SchemaLoader::lookup(const std::string& func_name) const {
    auto it = entries_.find(func_name);
    if (it != entries_.end()) {
        return it->second;
    }
    return nullptr;
}

bool SchemaLoader::is_handle(const std::string& type) const {
    return std::find(handles_.begin(), handles_.end(), type) != handles_.end();
}

bool SchemaLoader::is_assignable_to(const std::string& derived, const std::string& base) const {
    // Same type is always assignable
    if (derived == base) {
        return true;
    }

    // Walk up the inheritance chain
    std::string current = derived;
    while (true) {
        auto it = handle_inheritance_.find(current);
        if (it == handle_inheritance_.end()) {
            // No more parents
            return false;
        }
        if (it->second == base) {
            return true;
        }
        current = it->second;
    }
}

std::string SchemaLoader::to_snake_case(const std::string& camel) {
    std::string snake;
    for (size_t i = 0; i < camel.size(); ++i) {
        char c = camel[i];
        if (std::isupper(c)) {
            if (!snake.empty()) {
                snake += '_';
            }
            snake += std::tolower(c);
        } else if (std::isdigit(c)) {
            // Add underscore before digit if previous char was a lowercase letter
            if (!snake.empty() && i > 0 && std::islower(camel[i-1])) {
                snake += '_';
            }
            snake += c;
        } else {
            snake += c;
        }
    }
    return snake;
}
