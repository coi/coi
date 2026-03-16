#include "component.h"

const std::vector<EventSpec> &get_event_specs()
{
    static const std::vector<EventSpec> kSpecs = {
        {"click", "g_dispatcher", "", "", "", ""},
        {"input", "g_input_dispatcher", "const coi::string& _value", "_value", "const coi::string& _value", "_value"},
        {"change", "g_change_dispatcher", "const coi::string& _value", "_value", "const coi::string& _value", "_value"},
        {"keydown", "g_keydown_dispatcher", "int _keycode", "_keycode", "int _keycode", "_keycode"},
    };
    return kSpecs;
}

const EventSpec *find_event_spec(const std::string &event_type)
{
    for (const auto &spec : get_event_specs())
    {
        if (event_type == spec.type)
        {
            return &spec;
        }
    }
    return nullptr;
}

bool has_event_mask(const EventMasks &masks, const std::string &event_type)
{
    return get_event_mask(masks, event_type) != 0;
}

uint64_t get_event_mask(const EventMasks &masks, const std::string &event_type)
{
    auto it = masks.find(event_type);
    if (it == masks.end())
    {
        return 0;
    }
    return it->second;
}

std::string get_event_mask_name(const std::string &event_type)
{
    return "_" + event_type + "_mask";
}

EventMasks compute_event_masks(const std::vector<EventHandler> &handlers)
{
    EventMasks masks;
    for (const auto &handler : handlers)
    {
        if (handler.element_id < 64)
        {
            uint64_t bit = 1ULL << handler.element_id;
            if (find_event_spec(handler.event_type))
            {
                masks[handler.event_type] |= bit;
            }
        }
    }
    return masks;
}

std::set<int> get_elements_for_event(const std::vector<EventHandler> &handlers, const std::string &event_type)
{
    std::set<int> elements;
    for (const auto &handler : handlers)
    {
        if (handler.event_type == event_type)
        {
            elements.insert(handler.element_id);
        }
    }
    return elements;
}

void emit_event_mask_constants(std::stringstream &ss, const EventMasks &masks)
{
    for (const auto &spec : get_event_specs())
    {
        uint64_t mask = get_event_mask(masks, spec.type);
        if (mask == 0)
        {
            continue;
        }
        ss << "    static constexpr uint64_t " << get_event_mask_name(spec.type) << " = 0x" << std::hex << mask << std::dec << "ULL;\n";
    }
}

static void emit_handler_switch_cases(std::stringstream &ss,
                                      const std::vector<EventHandler> &handlers,
                                      const std::string &event_type,
                                      const std::string &suffix = "")
{
    for (const auto &handler : handlers)
    {
        if (handler.event_type == event_type)
        {
            ss << "                case " << handler.element_id << ": _handler_"
               << handler.element_id << "_" << event_type << "(" << suffix << "); break;\n";
        }
    }
}

void emit_event_registration(std::stringstream &ss,
                             int element_count,
                             const std::vector<EventHandler> &handlers,
                             const std::string &event_type,
                             const std::string &mask_name,
                             const std::string &dispatcher_name,
                             const std::string &lambda_params,
                             const std::string &call_suffix)
{
    ss << "        for (int i = 0; i < " << element_count << "; i++) if ((" << mask_name
       << " & (1ULL << i)) && el[i].is_valid()) " << dispatcher_name << ".set(el[i], [this, i](" << lambda_params << ") {\n";
    ss << "            switch(i) {\n";
    emit_handler_switch_cases(ss, handlers, event_type, call_suffix);
    ss << "            }\n";
    ss << "        });\n";
}

void emit_all_event_registrations(std::stringstream &ss,
                                  int element_count,
                                  const std::vector<EventHandler> &handlers,
                                  const EventMasks &masks)
{
    for (const auto &spec : get_event_specs())
    {
        if (!has_event_mask(masks, spec.type))
        {
            continue;
        }
        emit_event_registration(ss,
                                element_count,
                                handlers,
                                spec.type,
                                get_event_mask_name(spec.type),
                                spec.dispatcher_name,
                                spec.dispatcher_lambda_params,
                                spec.dispatcher_call_arg);
    }
}
