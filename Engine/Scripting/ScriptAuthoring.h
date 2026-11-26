#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Rice::Scripting {
using ScriptId = std::uint64_t;
inline constexpr char GameSpeedTarget[] = "Game Speed";

enum class NodeKind {
    Start, Update, Variable, Function, Class, OnCreate, OnDestroy,
    Change, Call, NewClass, Activate, Loop, Condition, ElseIf, Else, Wait,
    EndFunction, EndLoop, Custom, SetState, ChangeState, State, OnEnter, OnUpdate, OnExit, Move
};

struct ScriptNode {
    ScriptId id = 0, parent = 0;
    NodeKind kind = NodeKind::Custom;
    std::string name, type, value, args;
    std::array<std::string,3> moveCoordinates{};
    bool riceCon = false, isPublic = false, coroutine = true;
    std::string qualifiers;
    std::string riceScope = "build", riceLife = "game", riceAccess = "locked", policy;
    std::map<std::string, ScriptId> references;
    std::size_t line = 1;
};

struct ScriptDiagnostic {
    ScriptId node = 0;
    std::size_t line = 1;
    bool warning = false;
    std::string message;
};
struct ScriptChildReference {
    std::string key, name, type;
    std::size_t line = 1;
};

// All source ranges are measured in bytes in the authoritative UTF-8 C++ file.
// Unrecognized C++ has a range too; a structured edit only rewrites its own item.
class ScriptDocument {
public:
    static ScriptDocument New();
    void SetSource(std::string source);
    void MergeExternal(std::string_view base, std::string_view external,
                       const std::function<bool(ScriptId)>& allow = {});
    const std::string& Source() const { return source_; }
    const std::string& ScriptClassName() const { return qualifiedName_; }
    bool HasModuleEntry() const;
    // The editor can stage an SDK adapter for global, unwrapped native hooks.
    int FreeHookArity(std::string_view name) const;
    static std::vector<ScriptChildReference> ChildReferences(std::string_view source);
    static bool SameType(std::string_view a,std::string_view b);
    const std::vector<ScriptNode>& Nodes() const { return nodes_; }
    const ScriptNode* Get(ScriptId id) const;
    ScriptId AtLine(std::size_t line) const;
    ScriptId Add(ScriptId parent, NodeKind kind, std::string name = {});
    // Adds a sibling to an existing if/else-if chain, before an existing else.
    ScriptId AddBranch(ScriptId branch, NodeKind kind);
    bool ImportClass(const ScriptDocument& source, ScriptId classId);
    bool Edit(ScriptId id, const ScriptNode& value);
    bool Erase(ScriptId id);
    bool Move(ScriptId id, int direction);
    bool CanSee(ScriptId symbol, ScriptId owner) const;
    bool CanRead(ScriptId variable) const;
    std::vector<ScriptId> Instances(ScriptId symbol, ScriptId window = 0) const;
    std::vector<ScriptDiagnostic> Validate(const std::vector<std::string_view>& otherSources = {}) const;
    std::string Text(ScriptId id) const;
    bool HasScriptBody() const { return rootClose_ != std::string::npos; }

private:
    friend struct AuthoringParser;
    struct Range {
        std::size_t begin{}, code{}, headerEnd{}, end{}, childrenEnd{}, lastLine{};
        bool block = false;
    };
    std::size_t Index(ScriptId id) const;
    bool Within(ScriptId node, ScriptId owner) const;
    bool Insert(ScriptNode node, std::string text = {});
    void RefreshReader();
    bool ValueType(std::string_view type,std::vector<ScriptId>& visiting) const;
    void RefreshTraits();
    std::string AwaitCalls(std::string text, ScriptId owner) const;
    std::string BindOwner(const std::string& text, ScriptId context) const;
    bool Synchronous(ScriptId context) const;
    void TrackReferences(ScriptNode& node) const;
    std::string source_;
    std::string rootName_, qualifiedName_;
    std::vector<ScriptNode> nodes_;
    std::vector<Range> ranges_;
    std::vector<ScriptDiagnostic> parseErrors_;
    std::size_t rootOpen_ = std::string::npos, rootClose_ = std::string::npos;
    std::size_t readerBegin_ = std::string::npos, readerEnd_ = std::string::npos;
};
}
