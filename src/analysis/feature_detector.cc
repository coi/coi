#include "feature_detector.h"
#include "ast/ast.h"
#include <functional>

// Scan view nodes for event handler attributes
static void scan_view_for_events(ASTNode *node, FeatureFlags &flags)
{
    if (!node)
        return;

    if (auto *el = dynamic_cast<HTMLElement *>(node))
    {
        for (const auto &attr : el->attributes)
        {
            if (attr.name == "onclick")
                flags.click = true;
            else if (attr.name == "oninput")
                flags.input = true;
            else if (attr.name == "onchange")
                flags.change = true;
            else if (attr.name == "onkeydown")
                flags.keydown = true;
        }
        for (const auto &child : el->children)
        {
            scan_view_for_events(child.get(), flags);
        }
    }
    else if (auto *viewIf = dynamic_cast<ViewIfStatement *>(node))
    {
        for (const auto &child : viewIf->then_children)
            scan_view_for_events(child.get(), flags);
        for (const auto &child : viewIf->else_children)
            scan_view_for_events(child.get(), flags);
    }
    else if (auto *viewFor = dynamic_cast<ViewForRangeStatement *>(node))
    {
        for (const auto &child : viewFor->children)
            scan_view_for_events(child.get(), flags);
    }
    else if (auto *viewForEach = dynamic_cast<ViewForEachStatement *>(node))
    {
        for (const auto &child : viewForEach->children)
            scan_view_for_events(child.get(), flags);
    }
}

// Detect which features are actually used by analyzing components
FeatureFlags detect_features(const std::vector<Component> &components,
                              const std::set<std::string> &headers)
{
    FeatureFlags flags;
    flags.websocket = headers.count("websocket") > 0;
    flags.fetch = headers.count("fetch") > 0;

    // Scan all components for routers
    for (const auto &comp : components)
    {
        if (comp.router)
        {
            flags.router = true;
            break;
        }
    }

    // Scan views for event handlers
    for (const auto &comp : components)
    {
        for (const auto &root : comp.render_roots)
        {
            scan_view_for_events(root.get(), flags);
        }
    }

    // Detect keyboard usage (Input.isKeyDown) and Json.parse usage
    // by scanning for specific patterns in method bodies
    std::function<void(Expression *)> scan_expr = [&](Expression *expr)
    {
        if (!expr)
            return;
        if (auto *call = dynamic_cast<FunctionCall *>(expr))
        {
            // Check for Input.isKeyDown pattern
            if (call->name.find("Input.isKeyDown") != std::string::npos)
            {
                flags.keyboard = true;
            }
            // Check for Json.parse pattern
            if (call->name == "Json.parse")
            {
                flags.json = true;
            }
            for (auto &arg : call->args)
                scan_expr(arg.value.get());
        }
        else if (auto *member = dynamic_cast<MemberAccess *>(expr))
        {
            // Check for isKeyDown method call
            if (member->member == "isKeyDown")
            {
                if (auto *id = dynamic_cast<Identifier *>(member->object.get()))
                {
                    if (id->name == "Input")
                        flags.keyboard = true;
                }
            }
            scan_expr(member->object.get());
        }
        else if (auto *binary = dynamic_cast<BinaryOp *>(expr))
        {
            scan_expr(binary->left.get());
            scan_expr(binary->right.get());
        }
        else if (auto *ternary = dynamic_cast<TernaryOp *>(expr))
        {
            scan_expr(ternary->condition.get());
            scan_expr(ternary->true_expr.get());
            scan_expr(ternary->false_expr.get());
        }
        else if (auto *match = dynamic_cast<MatchExpr *>(expr))
        {
            // Scan match subject (e.g., Json.parse(User, json))
            scan_expr(match->subject.get());
            // Scan each arm's body expression
            for (auto &arm : match->arms)
            {
                scan_expr(arm.body.get());
            }
        }
        else if (auto *block = dynamic_cast<BlockExpr *>(expr))
        {
            // Forward-declare scan_stmt handling via recursive lambda call
            for (auto &s : block->statements)
            {
                // scan_stmt is defined below, need to handle inline
                if (auto *expr_stmt = dynamic_cast<ExpressionStatement *>(s.get()))
                {
                    scan_expr(expr_stmt->expression.get());
                }
                else if (auto *var_decl = dynamic_cast<VarDeclaration *>(s.get()))
                {
                    scan_expr(var_decl->initializer.get());
                }
                else if (auto *assign = dynamic_cast<Assignment *>(s.get()))
                {
                    scan_expr(assign->value.get());
                }
                else if (auto *ret = dynamic_cast<ReturnStatement *>(s.get()))
                {
                    scan_expr(ret->value.get());
                }
            }
        }
    };

    std::function<void(Statement *)> scan_stmt = [&](Statement *stmt)
    {
        if (!stmt)
            return;
        if (auto *expr_stmt = dynamic_cast<ExpressionStatement *>(stmt))
        {
            scan_expr(expr_stmt->expression.get());
        }
        else if (auto *var_decl = dynamic_cast<VarDeclaration *>(stmt))
        {
            scan_expr(var_decl->initializer.get());
        }
        else if (auto *assign = dynamic_cast<Assignment *>(stmt))
        {
            scan_expr(assign->value.get());
        }
        else if (auto *if_stmt = dynamic_cast<IfStatement *>(stmt))
        {
            scan_expr(if_stmt->condition.get());
            scan_stmt(if_stmt->then_branch.get());
            scan_stmt(if_stmt->else_branch.get());
        }
        else if (auto *block = dynamic_cast<BlockStatement *>(stmt))
        {
            for (auto &s : block->statements)
                scan_stmt(s.get());
        }
        else if (auto *ret = dynamic_cast<ReturnStatement *>(stmt))
        {
            scan_expr(ret->value.get());
        }
    };

    for (const auto &comp : components)
    {
        for (const auto &method : comp.methods)
        {
            for (const auto &stmt : method.body)
            {
                scan_stmt(stmt.get());
            }
        }
    }

    return flags;
}

// Emit global declarations for enabled features
void emit_feature_globals(std::ostream &out, const FeatureFlags &f)
{
    // DOM event dispatchers
    if (f.click)
    {
        out << "Dispatcher<webcc::function<void()>, 128> g_dispatcher;\n";
    }
    if (f.input)
    {
        out << "Dispatcher<webcc::function<void(const webcc::string&)>> g_input_dispatcher;\n";
    }
    if (f.change)
    {
        out << "Dispatcher<webcc::function<void(const webcc::string&)>> g_change_dispatcher;\n";
    }
    if (f.keydown)
    {
        out << "Dispatcher<webcc::function<void(int)>> g_keydown_dispatcher;\n";
    }
    // Runtime features
    if (f.keyboard)
    {
        out << "bool g_key_state[256] = {};\n";
    }
    if (f.router)
    {
        out << "webcc::function<void(const webcc::string&)> g_popstate_callback;\n";
    }
    if (f.websocket)
    {
        out << "Dispatcher<webcc::function<void(const webcc::string&)>> g_ws_message_dispatcher;\n";
        out << "Dispatcher<webcc::function<void()>> g_ws_open_dispatcher;\n";
        out << "Dispatcher<webcc::function<void()>> g_ws_close_dispatcher;\n";
        out << "Dispatcher<webcc::function<void()>> g_ws_error_dispatcher;\n";
    }
    if (f.fetch)
    {
        out << "Dispatcher<webcc::function<void(const webcc::string&)>> g_fetch_success_dispatcher;\n";
        out << "Dispatcher<webcc::function<void(const webcc::string&)>> g_fetch_error_dispatcher;\n";
    }
}

// Emit event handlers for enabled features
void emit_feature_event_handlers(std::ostream &out, const FeatureFlags &f)
{
    // DOM events
    if (f.click)
    {
        out << "        } else if (e.opcode == webcc::dom::ClickEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::dom::ClickEvent>()) g_dispatcher.dispatch(evt->handle);\n";
    }
    if (f.input)
    {
        out << "        } else if (e.opcode == webcc::dom::InputEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::dom::InputEvent>()) g_input_dispatcher.dispatch(evt->handle, webcc::string(evt->value));\n";
    }
    if (f.change)
    {
        out << "        } else if (e.opcode == webcc::dom::ChangeEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::dom::ChangeEvent>()) g_change_dispatcher.dispatch(evt->handle, webcc::string(evt->value));\n";
    }
    if (f.keydown)
    {
        out << "        } else if (e.opcode == webcc::dom::KeydownEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::dom::KeydownEvent>()) g_keydown_dispatcher.dispatch(evt->handle, evt->keycode);\n";
    }
    // Runtime features
    if (f.keyboard)
    {
        out << "        } else if (e.opcode == webcc::input::KeyDownEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::input::KeyDownEvent>()) { if (evt->key_code >= 0 && evt->key_code < 256) g_key_state[evt->key_code] = true; }\n";
        out << "        } else if (e.opcode == webcc::input::KeyUpEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::input::KeyUpEvent>()) { if (evt->key_code >= 0 && evt->key_code < 256) g_key_state[evt->key_code] = false; }\n";
    }
    if (f.router)
    {
        out << "        } else if (e.opcode == webcc::system::PopstateEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::system::PopstateEvent>()) { if (g_popstate_callback) g_popstate_callback(webcc::string(evt->path)); }\n";
    }
    if (f.websocket)
    {
        out << "        } else if (e.opcode == webcc::websocket::MessageEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::websocket::MessageEvent>()) g_ws_message_dispatcher.dispatch(evt->handle, webcc::string(evt->data));\n";
        out << "        } else if (e.opcode == webcc::websocket::OpenEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::websocket::OpenEvent>()) g_ws_open_dispatcher.dispatch(evt->handle);\n";
        out << "        } else if (e.opcode == webcc::websocket::CloseEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::websocket::CloseEvent>()) {\n";
        out << "                g_ws_close_dispatcher.dispatch(evt->handle);\n";
        out << "                g_ws_message_dispatcher.remove(evt->handle);\n";
        out << "                g_ws_open_dispatcher.remove(evt->handle);\n";
        out << "                g_ws_close_dispatcher.remove(evt->handle);\n";
        out << "                g_ws_error_dispatcher.remove(evt->handle);\n";
        out << "            }\n";
        out << "        } else if (e.opcode == webcc::websocket::ErrorEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::websocket::ErrorEvent>()) {\n";
        out << "                g_ws_error_dispatcher.dispatch(evt->handle);\n";
        out << "                g_ws_message_dispatcher.remove(evt->handle);\n";
        out << "                g_ws_open_dispatcher.remove(evt->handle);\n";
        out << "                g_ws_close_dispatcher.remove(evt->handle);\n";
        out << "                g_ws_error_dispatcher.remove(evt->handle);\n";
        out << "            }\n";
    }
    if (f.fetch)
    {
        out << "        } else if (e.opcode == webcc::fetch::SuccessEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::fetch::SuccessEvent>()) {\n";
        out << "                g_fetch_success_dispatcher.dispatch(evt->id, webcc::string(evt->data));\n";
        out << "                g_fetch_success_dispatcher.remove(evt->id);\n";
        out << "                g_fetch_error_dispatcher.remove(evt->id);\n";
        out << "            }\n";
        out << "        } else if (e.opcode == webcc::fetch::ErrorEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::fetch::ErrorEvent>()) {\n";
        out << "                g_fetch_error_dispatcher.dispatch(evt->id, webcc::string(evt->error));\n";
        out << "                g_fetch_success_dispatcher.remove(evt->id);\n";
        out << "                g_fetch_error_dispatcher.remove(evt->id);\n";
        out << "            }\n";
    }
}

// Check if the Dispatcher template is needed
bool needs_dispatcher(const FeatureFlags &f)
{
    return f.click || f.input || f.change || f.keydown || f.websocket || f.fetch;
}

// Emit initialization code for enabled features
void emit_feature_init(std::ostream &out, const FeatureFlags &f, const std::string &root_comp)
{
    if (f.keyboard)
    {
        out << "    webcc::input::init_keyboard();\n";
    }
    if (f.router)
    {
        out << "    g_popstate_callback = [](const webcc::string& path) {\n";
        out << "        if (app) app->_handle_popstate(path);\n";
        out << "    };\n";
        out << "    webcc::system::init_popstate();\n";
    }
}
