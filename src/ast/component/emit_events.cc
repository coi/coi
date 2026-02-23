#include "component.h"

EventMasks compute_event_masks(const std::vector<EventHandler> &handlers)
{
    EventMasks masks;
    for (const auto &handler : handlers)
    {
        if (handler.element_id < 64)
        {
            uint64_t bit = 1ULL << handler.element_id;
            if (handler.event_type == "click")
                masks.click |= bit;
            else if (handler.event_type == "input")
                masks.input |= bit;
            else if (handler.event_type == "change")
                masks.change |= bit;
            else if (handler.event_type == "keydown")
                masks.keydown |= bit;
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
    if (masks.click)
        ss << "    static constexpr uint64_t _click_mask = 0x" << std::hex << masks.click << std::dec << "ULL;\n";
    if (masks.input)
        ss << "    static constexpr uint64_t _input_mask = 0x" << std::hex << masks.input << std::dec << "ULL;\n";
    if (masks.change)
        ss << "    static constexpr uint64_t _change_mask = 0x" << std::hex << masks.change << std::dec << "ULL;\n";
    if (masks.keydown)
        ss << "    static constexpr uint64_t _keydown_mask = 0x" << std::hex << masks.keydown << std::dec << "ULL;\n";
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
    if (masks.click)
    {
        emit_event_registration(ss, element_count, handlers, "click", "_click_mask", "g_dispatcher", "", "");
    }
    if (masks.input)
    {
        emit_event_registration(ss, element_count, handlers, "input", "_input_mask", "g_input_dispatcher", "const webcc::string& v", "v");
    }
    if (masks.change)
    {
        emit_event_registration(ss, element_count, handlers, "change", "_change_mask", "g_change_dispatcher", "const webcc::string& v", "v");
    }
    if (masks.keydown)
    {
        emit_event_registration(ss, element_count, handlers, "keydown", "_keydown_mask", "g_keydown_dispatcher", "int k", "k");
    }
}
