#include "function/input/ActionMapJsonParser.hpp"

#include <nlohmann/json.hpp>

#include <iostream>

namespace StellarAlia {

using nlohmann::json;

// ─── Parse helpers ───────────────────────────────────────────────────────────

static ActionType ParseActionType(const std::string& s) {
    if (s == "Axis2D") return ActionType::Axis2D;
    if (s == "Axis")   return ActionType::Axis;
    return ActionType::Button;
}

// Apply optional processor fields (scale/deadZone/invert/normalize/clamp) on
// any binding kind that uses ProcessorChain (Direct / TwoButton / Composite).
static void ParseProcessors(const json& jb, ProcessorChain& out) {
    if (jb.contains("scale")) {
        const auto& s = jb.at("scale");
        if (s.is_number())
            out.Scale(s.get<float>());
        else if (s.is_array() && s.size() == 2)
            out.Scale(s[0].get<float>(), s[1].get<float>());
    }
    if (jb.contains("deadZone") && jb.at("deadZone").is_number())
        out.DeadZone(jb.at("deadZone").get<float>());
    if (jb.contains("invert") && jb.at("invert").get<bool>())
        out.Invert();
    if (jb.contains("clamp") && jb.at("clamp").is_array() && jb.at("clamp").size() == 2)
        out.Clamp(jb.at("clamp")[0].get<float>(), jb.at("clamp")[1].get<float>());
    if (jb.contains("normalize") && jb.at("normalize").is_boolean()
        && jb.at("normalize").get<bool>())
        out.Normalize();
}

static bool ParseBinding(const json& jb, BindingDef& out) {
    const std::string kind = jb.value("kind", std::string{"Direct"});

    if (kind == "Direct") {
        out = BindingDef::Direct(jb.value("path", std::string{}));
        ParseProcessors(jb, out.processors);
    } else if (kind == "WASD") {
        const bool norm = jb.value("normalize", true);
        out = BindingDef::WASD(norm,
            jb.value("up",    std::string{"Keyboard/W"}),
            jb.value("down",  std::string{"Keyboard/S"}),
            jb.value("left",  std::string{"Keyboard/A"}),
            jb.value("right", std::string{"Keyboard/D"}));
        // WASD does not consume processor fields (normalize lives on WASDKeys).
    } else if (kind == "TwoButton") {
        out = BindingDef::TwoButton(
            jb.value("negative", std::string{}),
            jb.value("positive", std::string{}));
        ParseProcessors(jb, out.processors);
    } else if (kind == "Composite") {
        std::vector<std::string> mods;
        if (jb.contains("modifiers") && jb.at("modifiers").is_array()) {
            for (const auto& m : jb.at("modifiers")) {
                if (m.is_string()) mods.push_back(m.get<std::string>());
            }
        }
        out = BindingDef::Composite(std::move(mods), jb.value("key", std::string{}));
        ParseProcessors(jb, out.processors);
    } else {
        std::cerr << "[InputMap] Unknown binding kind: " << kind << '\n';
        return false;
    }
    return true;
}

bool ActionMapJsonParser::Parse(std::string_view js, ActionMapDef& out) {
    json j;
    try {
        j = json::parse(js);
    } catch (const std::exception& e) {
        std::cerr << "[InputMap] JSON parse error: " << e.what() << '\n';
        return false;
    }

    if (!j.contains("name") || !j["name"].is_string()) {
        std::cerr << "[InputMap] missing required string \"name\"\n";
        return false;
    }
    if (!j.contains("actions") || !j["actions"].is_array()) {
        std::cerr << "[InputMap] missing required array \"actions\"\n";
        return false;
    }

    out = {};
    out.name        = j["name"].get<std::string>();
    out.passthrough = j.value("passthrough", false);

    for (const auto& ja : j["actions"]) {
        ActionDef a;
        a.name                = ja.value("name", std::string{});
        a.type                = ParseActionType(ja.value("type", std::string{"Button"}));
        a.activationThreshold = ja.value("activationThreshold", 0.5f);
        a.userConfigurable    = ja.value("userConfigurable", false);

        if (ja.contains("bindings") && ja["bindings"].is_array()) {
            for (const auto& jb : ja["bindings"]) {
                BindingDef b;
                if (ParseBinding(jb, b))
                    a.bindings.push_back(std::move(b));
            }
        }
        out.actions.push_back(std::move(a));
    }
    return true;
}

// ─── Serialize helpers ───────────────────────────────────────────────────────

static const char* ActionTypeToString(ActionType t) {
    switch (t) {
    case ActionType::Axis2D: return "Axis2D";
    case ActionType::Axis:   return "Axis";
    default:                 return "Button";
    }
}

// Emit processor chain fields (scale/deadZone/invert/normalize/clamp).
// WASD never carries a processor chain in current usage; this is a no-op for it.
static void SerializeProcessors(const ProcessorChain& chain, json& jb) {
    using Step = ProcessorChain::Step;
    for (const auto& s : chain.steps) {
        switch (s.type) {
        case Step::Type::Scale:
            if (s.x == s.y)
                jb["scale"] = s.x;
            else
                jb["scale"] = json::array({ s.x, s.y });
            break;
        case Step::Type::DeadZone:
            jb["deadZone"] = s.x;
            break;
        case Step::Type::Invert:
            jb["invert"] = true;
            break;
        case Step::Type::Clamp:
            jb["clamp"] = json::array({ s.x, s.y });
            break;
        case Step::Type::Normalize:
            jb["normalize"] = true;
            break;
        }
    }
}

static json SerializeBinding(const BindingDef& b) {
    json jb;
    switch (b.kind) {
    case BindingDef::Kind::Direct: {
        jb["kind"] = "Direct";
        jb["path"] = b.path;
        SerializeProcessors(b.processors, jb);
        break;
    }
    case BindingDef::Kind::WASD: {
        jb["kind"]      = "WASD";
        jb["up"]        = b.wasd.up;
        jb["down"]      = b.wasd.down;
        jb["left"]      = b.wasd.left;
        jb["right"]     = b.wasd.right;
        jb["normalize"] = b.wasd.normalize;
        break;
    }
    case BindingDef::Kind::TwoButtonAxis: {
        jb["kind"]     = "TwoButton";
        jb["negative"] = b.twoButton.negative;
        jb["positive"] = b.twoButton.positive;
        SerializeProcessors(b.processors, jb);
        break;
    }
    case BindingDef::Kind::Composite: {
        jb["kind"]      = "Composite";
        jb["modifiers"] = b.composite.modifierPaths;
        jb["key"]       = b.composite.keyPath;
        SerializeProcessors(b.processors, jb);
        break;
    }
    }
    return jb;
}

void ActionMapJsonParser::Serialize(const ActionMapDef& def, std::string& outJson) {
    json j;
    j["name"]        = def.name;
    j["passthrough"] = def.passthrough;
    auto& ja = j["actions"];
    ja = json::array();

    for (const auto& a : def.actions) {
        json action;
        action["name"]                = a.name;
        action["type"]                = ActionTypeToString(a.type);
        action["activationThreshold"] = a.activationThreshold;
        action["userConfigurable"]    = a.userConfigurable;
        auto& jbinds = action["bindings"];
        jbinds = json::array();
        for (const auto& b : a.bindings)
            jbinds.push_back(SerializeBinding(b));
        ja.push_back(std::move(action));
    }

    outJson = j.dump(2);
}

} // namespace StellarAlia
