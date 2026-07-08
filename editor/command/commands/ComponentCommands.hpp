#pragma once

#include "command/IEditorCommand.hpp"
#include "EditorContext.hpp"
#include "function/scene/Components.hpp"

#include <entt/entt.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// CompositeCommand — groups several sub-commands into one undo/redo step.
// Execute runs them in order; Undo runs them in reverse. Used e.g. for
// multi-select delete so one Ctrl+Z restores the whole batch.
// ─────────────────────────────────────────────────────────────────────────────
class CompositeCommand final : public IEditorCommand {
public:
    CompositeCommand(std::string description,
                     std::vector<std::unique_ptr<IEditorCommand>> commands)
        : m_description(std::move(description)), m_commands(std::move(commands)) {}

    void Execute(EditorContext& ctx) override {
        for (auto& c : m_commands) if (c) c->Execute(ctx);
    }
    void Undo(EditorContext& ctx) override {
        for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it)
            if (*it) (*it)->Undo(ctx);
    }
    [[nodiscard]] std::string GetDescription() const override { return m_description; }

private:
    std::string                                  m_description;
    std::vector<std::unique_ptr<IEditorCommand>> m_commands;
};

// ─────────────────────────────────────────────────────────────────────────────
// CallbackCommand — undoable action expressed as redo/undo closures. Use for
// discrete mutations that don't fit SetFieldCommand<T>: enum combos and
// map/vector structural add/remove.
//
// Closures receive EditorContext and should re-fetch the target component via
// ctx.registry->try_get<T>() (rather than capturing a raw component pointer), so
// a stale entity or removed component is a safe no-op. CommandManager::Clear() on
// scene switch / play boundary still guards against captured entity reuse.
// ─────────────────────────────────────────────────────────────────────────────
class CallbackCommand final : public IEditorCommand {
public:
    using Fn = std::function<void(EditorContext&)>;

    CallbackCommand(std::string description, Fn redo, Fn undo)
        : m_description(std::move(description))
        , m_redo(std::move(redo))
        , m_undo(std::move(undo)) {}

    void Execute(EditorContext& ctx) override { if (m_redo) m_redo(ctx); }
    void Undo   (EditorContext& ctx) override { if (m_undo) m_undo(ctx); }
    [[nodiscard]] std::string GetDescription() const override { return m_description; }

private:
    std::string m_description;
    Fn          m_redo;
    Fn          m_undo;
};

// ─────────────────────────────────────────────────────────────────────────────
// AddComponentCommand<T> — undoable "Add Component" from the Inspector popup.
//
// Execute() default-constructs T on the entity; Undo() removes it. onApplied
// fires after both (dirty-flag propagation, e.g. Scene::MarkMaterialDirty).
// No value snapshot is needed: the component is freshly added with defaults, so
// undo simply removes it.
// ─────────────────────────────────────────────────────────────────────────────
template<typename T>
class AddComponentCommand final : public IEditorCommand {
public:
    using OnApplied = std::function<void()>;

    AddComponentCommand(entt::entity entity, std::string description,
                        OnApplied onApplied = {})
        : m_entity(entity)
        , m_description(std::move(description))
        , m_onApplied(std::move(onApplied)) {}

    void Execute(EditorContext& ctx) override {
        auto& reg = *ctx.registry;
        if (!reg.valid(m_entity)) return;
        reg.emplace_or_replace<T>(m_entity);
        if (m_onApplied) m_onApplied();
    }

    void Undo(EditorContext& ctx) override {
        auto& reg = *ctx.registry;
        if (!reg.valid(m_entity)) return;
        reg.remove<T>(m_entity);
        if (m_onApplied) m_onApplied();
    }

    [[nodiscard]] std::string GetDescription() const override { return m_description; }

private:
    entt::entity m_entity;
    std::string  m_description;
    OnApplied    m_onApplied;
};

// ─────────────────────────────────────────────────────────────────────────────
// RemoveComponentCommand<T> — undoable removal of a single component from one
// entity (Inspector drawer "×" button).
//
// Execute() snapshots the component value, removes it, and fires onApplied.
// Undo() re-emplaces the snapshot and fires onApplied again.
//
// Restore special cases (match DeleteEntityCommand / duplicate):
//   RigidBodyComponent   — bodyId reset to ~0u; the Jolt handle is invalid until
//                          PhysicsSystem recreates the body.
//   SkinnedMeshComponent — re-emplaced clean (meshAsset only); GPU handles are
//                          reacquired by the animation system after
//                          MarkSkinnedMeshDirty (supplied via onApplied), not
//                          restored from the stale snapshot.
// ─────────────────────────────────────────────────────────────────────────────
template<typename T>
class RemoveComponentCommand final : public IEditorCommand {
public:
    using OnApplied = std::function<void()>;

    RemoveComponentCommand(entt::entity entity, std::string description,
                           OnApplied onApplied = {})
        : m_entity(entity)
        , m_description(std::move(description))
        , m_onApplied(std::move(onApplied)) {}

    void Execute(EditorContext& ctx) override {
        auto& reg = *ctx.registry;
        if (!reg.valid(m_entity)) return;
        if (auto* c = reg.try_get<T>(m_entity)) {
            m_snapshot = *c;
            reg.remove<T>(m_entity);
            if (m_onApplied) m_onApplied();
        }
    }

    void Undo(EditorContext& ctx) override {
        auto& reg = *ctx.registry;
        if (!reg.valid(m_entity) || !m_snapshot) return;

        if constexpr (std::is_same_v<T, SkinnedMeshComponent>) {
            SkinnedMeshComponent clean{};
            clean.meshAsset     = m_snapshot->meshAsset;
            clean.skeletonAsset = m_snapshot->skeletonAsset;   // #83 P1
            reg.emplace_or_replace<T>(m_entity, clean);
        } else {
            reg.emplace_or_replace<T>(m_entity, *m_snapshot);
            if constexpr (std::is_same_v<T, RigidBodyComponent>)
                reg.get<T>(m_entity).bodyId = ~0u;
        }
        if (m_onApplied) m_onApplied();
    }

    [[nodiscard]] std::string GetDescription() const override { return m_description; }

private:
    entt::entity     m_entity;
    std::string      m_description;
    OnApplied        m_onApplied;
    std::optional<T> m_snapshot;
};

} // namespace StellarAlia::Editor
