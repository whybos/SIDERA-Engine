#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sidera::editor {

class IEditorCommand {
public:
    virtual ~IEditorCommand() = default;
    [[nodiscard]] virtual std::string getDescription() const = 0;
    [[nodiscard]] virtual bool validate() const { return true; }
    virtual bool execute() = 0;
    virtual bool undo() = 0;
};

// Transactional composite command: if step k fails, steps k-1..0 are rolled back automatically.
class CompositeEditorCommand final : public IEditorCommand {
public:
    explicit CompositeEditorCommand(std::string desc) : m_description(std::move(desc)) {}

    void addSubCommand(std::unique_ptr<IEditorCommand> cmd) {
        if (cmd) m_commands.push_back(std::move(cmd));
    }

    std::string getDescription() const override { return m_description; }

    bool validate() const override {
        if (m_commands.empty()) return false;
        for (const auto& c : m_commands) {
            if (!c->validate()) return false;
        }
        return true;
    }

    bool execute() override {
        size_t executedCount = 0;
        for (auto& c : m_commands) {
            if (!c->execute()) {
                // Roll back previously executed sub-commands in reverse order
                for (size_t i = executedCount; i > 0; --i) {
                    m_commands[i - 1]->undo();
                }
                return false;
            }
            ++executedCount;
        }
        return true;
    }

    bool undo() override {
        bool ok = true;
        for (size_t i = m_commands.size(); i > 0; --i) {
            if (!m_commands[i - 1]->undo()) ok = false;
        }
        return ok;
    }

private:
    std::string m_description;
    std::vector<std::unique_ptr<IEditorCommand>> m_commands;
};

class EditorCommandManager {
public:
    explicit EditorCommandManager(size_t maxHistory = 128) : m_maxHistory(maxHistory) {}

    bool execute(std::unique_ptr<IEditorCommand> cmd) {
        if (!cmd || !cmd->validate()) return false;
        if (!cmd->execute()) return false;
        m_redoStack.clear();
        m_undoStack.push_back(std::move(cmd));
        if (m_undoStack.size() > m_maxHistory) {
            m_undoStack.erase(m_undoStack.begin());
        }
        if (m_onHistoryChanged) m_onHistoryChanged();
        return true;
    }

    bool undo() {
        if (m_undoStack.empty()) return false;
        auto cmd = std::move(m_undoStack.back());
        m_undoStack.pop_back();
        const bool ok = cmd->undo();
        if (ok) m_redoStack.push_back(std::move(cmd));
        if (m_onHistoryChanged) m_onHistoryChanged();
        return ok;
    }

    bool redo() {
        if (m_redoStack.empty()) return false;
        auto cmd = std::move(m_redoStack.back());
        m_redoStack.pop_back();
        const bool ok = cmd->execute();
        if (ok) m_undoStack.push_back(std::move(cmd));
        if (m_onHistoryChanged) m_onHistoryChanged();
        return ok;
    }

    void clear() noexcept {
        m_undoStack.clear();
        m_redoStack.clear();
    }

    [[nodiscard]] bool canUndo() const noexcept { return !m_undoStack.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !m_redoStack.empty(); }
    [[nodiscard]] size_t getUndoCount() const noexcept { return m_undoStack.size(); }
    [[nodiscard]] size_t getRedoCount() const noexcept { return m_redoStack.size(); }
    [[nodiscard]] std::string getLastUndoDescription() const {
        return m_undoStack.empty() ? std::string{} : m_undoStack.back()->getDescription();
    }
    void setOnHistoryChanged(std::function<void()> cb) { m_onHistoryChanged = std::move(cb); }

private:
    size_t m_maxHistory = 128;
    std::vector<std::unique_ptr<IEditorCommand>> m_undoStack;
    std::vector<std::unique_ptr<IEditorCommand>> m_redoStack;
    std::function<void()> m_onHistoryChanged;
};

} // namespace sidera::editor
