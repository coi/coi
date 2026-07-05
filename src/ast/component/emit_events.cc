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
    auto it = masks.find(event_type);
    if (it == masks.end())
    {
        return false;
    }
    for (uint64_t word : it->second)
    {
        if (word != 0)
        {
            return true;
        }
    }
    return false;
}

bool event_mask_test(const EventMasks &masks, const std::string &event_type, int element_id)
{
    auto it = masks.find(event_type);
    if (it == masks.end())
    {
        return false;
    }
    size_t word = static_cast<size_t>(element_id) >> 6;
    if (word >= it->second.size())
    {
        return false;
    }
    return (it->second[word] & (1ULL << (element_id & 63))) != 0;
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
        if (!find_event_spec(handler.event_type))
        {
            continue;
        }
        EventMask &mask = masks[handler.event_type];
        size_t word = static_cast<size_t>(handler.element_id) >> 6;
        if (word >= mask.size())
        {
            mask.resize(word + 1, 0);
        }
        mask[word] |= 1ULL << (handler.element_id & 63);
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

void emit_event_mask_constants(std::stringstream &ss, const EventMasks &masks, int element_count)
{
    // Every mask is padded to cover all elements so runtime loops can index
    // `mask[i >> 6]` for any i < element_count without going out of bounds.
    size_t words = (element_count <= 0) ? 1 : ((static_cast<size_t>(element_count) + 63) >> 6);
    for (const auto &spec : get_event_specs())
    {
        if (!has_event_mask(masks, spec.type))
        {
            continue;
        }
        EventMask mask = masks.at(spec.type);
        mask.resize(words, 0);
        ss << "    static constexpr uint64_t " << get_event_mask_name(spec.type) << "[" << words << "] = {";
        for (size_t w = 0; w < mask.size(); ++w)
        {
            if (w > 0)
            {
                ss << ", ";
            }
            ss << "0x" << std::hex << mask[w] << std::dec << "ULL";
        }
        ss << "};\n";
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
       << "[i >> 6] & (1ULL << (i & 63))) && el[i].is_valid()) " << dispatcher_name << ".set(el[i], [this, i](" << lambda_params << ") {\n";
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
