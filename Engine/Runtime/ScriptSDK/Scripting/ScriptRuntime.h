#pragma once
#include "ScriptRuntimeValue.h"

// Standalone native script SDK. The host and each module own their respective
// allocations; only versioned callbacks and plain values cross the DLL boundary.
#include <algorithm>
#include <cmath>
#include <coroutine>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Rice::Scripting {
inline constexpr std::uint32_t ModuleAbi = 5;
enum class SignalKind : int { Function, Event, CollisionStart, CollisionEnd, ComponentChange, GameSeconds, Seconds };
struct EventTag {};
class TriggerLifetime {
    mutable std::shared_ptr<char> identity;
public:
    TriggerLifetime() = default;
    TriggerLifetime(const TriggerLifetime&) {}
    TriggerLifetime& operator=(const TriggerLifetime&) { identity.reset(); return *this; }
    std::weak_ptr<void> Token() const {
        if (!identity) identity=std::make_shared<char>();
        return identity;
    }
};
enum class Severity : int { Info, Warning, Error };
enum class Phase : int { Collision, Overlap, OverlapEnd };
enum class MoveUnit : int { Second, GameSecond, Frame };
struct Contact {
    std::uint64_t other{};
    bool isGround{}, isTrigger{};
    const char* name = "";  // Valid until the host's next physics update.
};
struct FieldBinding {
    const char* id;
    const char* name;
    const char* scope;
    const char* life;
    const char* access;
    const char* policy;
};
struct Host {
    void* user{};
    void (*log)(void*, int severity, const char* message){};
    int (*activate)(void*, const char* binding, const char* component, bool active){};
    bool (*isActive)(void*, const char* binding, const char* component){};
    bool (*readChild)(void*, const char* binding, const char* name, Value*){};
    bool (*contact)(void*, int phase, int index, Contact*){};
    bool (*readField)(void*, const FieldBinding*, Value* defaultAndResult){};
    bool (*writeField)(void*, const FieldBinding*, const Value*){};
    bool (*bindField)(void*, const FieldBinding*, const Value* initial){};
    void (*diagnostic)(void*, std::uint64_t row, const char* message){};
    // ABI 4 appends callbacks. ABI 3 modules consume the original prefix.
    bool (*move)(void*, double x, double y, double z, MoveUnit){};
    float (*gameSpeed)(void*){};
    void (*setGameSpeed)(void*, float speed){};
    float (*timerDelta)(void*,bool gameSeconds){};
    std::uint64_t (*signalVersion)(void*, SignalKind, const char*){};
    void (*publishSignal)(void*, SignalKind, const char*){};
};
struct ModuleApi {
    std::uint32_t abi;
    void* (*create)(const Host*);
    void (*tick)(void*, float dt);
    void (*destroy)(void*);
    bool (*read)(void*, const char*, Value*);
    bool (*triggerBlocking)(void*, std::uint64_t);
};

template<class T> std::vector<T> Snapshot(const std::vector<T>& list) { return list; }

namespace Detail {
struct EndInvocation {};
struct EndTrigger {};
struct Suspension {
    std::coroutine_handle<> suspended{};
    double remaining{};
    std::optional<bool> timerClock;
};
struct Execution : Suspension {
    explicit Execution(Host callbacks) : host(callbacks) {}
    Host host;
    std::map<std::pair<SignalKind,std::string>,std::uint64_t> signals;
    bool LocalSignal(SignalKind kind,const char* key) const {
        return (kind==SignalKind::Function||kind==SignalKind::Event)&&std::string_view(key).starts_with("private:");
    }
    std::uint64_t Version(SignalKind kind,const char* key) {
        if (!LocalSignal(kind,key)&&host.signalVersion) return host.signalVersion(host.user,kind,key);
        auto it=signals.find({kind,key});
        return it==signals.end()?0:it->second;
    }
    void Publish(SignalKind kind,const char* key) {
        if (!LocalSignal(kind,key)&&host.publishSignal) host.publishSignal(host.user,kind,key);
        else ++signals[{kind,key}];
    }
    float Elapsed(std::optional<bool> clock) const {
        return std::max(0.f,clock&&host.timerDelta?host.timerDelta(host.user,*clock):dt);
    }
    std::size_t budget{};
    float dt{};
    bool pendingStop{};
    void CheckInvocation() {
        if (std::exchange(pendingStop,false)) throw EndInvocation{};
    }
};
inline thread_local Execution* current{};
struct ContextGuard {
    explicit ContextGuard(Execution& execution) : previous(std::exchange(current,&execution)) {}
    ~ContextGuard() { current=previous; }
    Execution* previous;
};
template<class T> struct TaskResult {
    std::optional<T> value;
    template<class U> void return_value(U&& result) { value.emplace(std::forward<U>(result)); }
    T Take() { return std::move(value.value()); }
};
template<> struct TaskResult<void> {
    void return_void() noexcept {}
    void Take() const noexcept {}
};
}

inline void SignalCurrentFunction(const char* key) {
    if (Detail::current) Detail::current->Publish(SignalKind::Function,key);
}

// A parent owns its awaited child. Symmetric transfer links completion back
// to the parent; cancellation destroys the whole suspended invocation tree.
template<class T = void> class Task {
public:
    struct promise_type : Detail::TaskResult<T> {
        std::exception_ptr error;
        std::coroutine_handle<> parent = std::noop_coroutine();
        Task get_return_object() noexcept { return Task(Handle::from_promise(*this)); }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        struct Final {
            bool await_ready() const noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) const noexcept {
                return h.promise().parent;
            }
            void await_resume() const noexcept {}
        };
        Final final_suspend() const noexcept { return {}; }
        void unhandled_exception() noexcept { error = std::current_exception(); }
    };
    using Handle = std::coroutine_handle<promise_type>;
    explicit Task(Handle h) noexcept : coroutine(h) {}
    Task(Task&& other) noexcept : coroutine(std::exchange(other.coroutine, {})) {}
    Task(const Task&) = delete;
    ~Task() { if (coroutine) coroutine.destroy(); }
    bool Done() const noexcept { return !coroutine||coroutine.done(); }
    void Resume() { coroutine.resume(); }
    T Finish() {
        if (coroutine.promise().error) std::rethrow_exception(coroutine.promise().error);
        auto finishFrame=[this] {
            std::exchange(coroutine,{}).destroy();
            if (Detail::current) Detail::current->CheckInvocation();
        };
        if constexpr (std::is_void_v<T>) {
            finishFrame();
        } else {
            auto value=coroutine.promise().Take();
            finishFrame();
            return value;
        }
    }
    bool await_ready() const noexcept { return false; }
    Handle await_suspend(std::coroutine_handle<> caller) noexcept {
        coroutine.promise().parent = caller;
        return coroutine;
    }
    T await_resume() { return Finish(); }
private:
    Handle coroutine;
};

namespace Detail {
template<class> class Instance;
}

// Destructors cannot unwind through optional::reset or a coroutine frame's
// destruction. Deliver a pending stop before constructing a replacement.
template<class T,class... Args> T& CheckedEmplace(std::optional<T>& slot,Args&&... args) {
    slot.reset();
    if (Detail::current) Detail::current->CheckInvocation();
    return slot.emplace(std::forward<Args>(args)...);
}

class ScriptBase {
    template<class> friend class Detail::Instance;
    Detail::Execution* execution = Detail::current;
    struct StateActions { std::function<void()> enter, update, exit; };
    using StateKey = std::pair<std::string, std::string>;
    std::map<StateKey, StateActions> definitions;
    std::map<std::string, std::string> states;
    std::deque<StateKey> changes;
    bool processing{};
    struct TriggerClock {
        double interval;
        std::uint64_t armedTick;
        double elapsed{};
        bool due{};
        void Advance(double delta,std::uint64_t tick) {
            if(tick==armedTick)return;
            elapsed+=delta;
            if(elapsed>=interval){
                // Retain the phase after a long frame. A busy handler needs only one pending firing.
                elapsed=interval>0?std::fmod(elapsed,interval):0;
                due=true;
            }
        }
    };
    struct TriggerState {
        std::uint64_t row{}, seen{}, completed{}, consumed{};
        std::weak_ptr<void> owner;
        SignalKind kind{};
        std::string key;
        std::optional<TriggerClock> clock;
        bool every{}, armed=true;
        unsigned gates{};
        std::function<void()> sync;
        std::function<Task<>()> async;
        Detail::Suspension suspension;
        std::optional<Task<>> running;
        bool Expired() const {
            std::weak_ptr<void> empty;
            return owner.expired()&&(empty.owner_before(owner)||owner.owner_before(empty));
        }
        bool ConsumeOccurrence(Detail::Execution& execution) {
            if(clock)return std::exchange(clock->due,false);
            if(seen==execution.Version(kind,key.c_str()))return false;
            ++seen;
            return true;
        }
        void Disarm() {
            armed=false;
            running.reset();
            sync={};
            async={};
        }
    };
    std::list<std::shared_ptr<TriggerState>> triggers;
    std::uint64_t triggerTick{};
    std::shared_ptr<TriggerState> FindTrigger(std::uint64_t row,const std::weak_ptr<void>& owner) const {
        for(const auto& trigger:triggers)
            if(trigger->row==row&&!trigger->owner.owner_before(owner)&&!owner.owner_before(trigger->owner))
                return trigger;
        return {};
    }
    template<class Source>
    std::pair<std::shared_ptr<TriggerState>,bool> Subscribe(std::uint64_t row,SignalKind kind,Source source,
                                                            bool every,const std::weak_ptr<void>& owner) {
        if(auto previous=FindTrigger(row,owner))return {previous,false};
        auto state=std::make_shared<TriggerState>();
        state->row=row;state->kind=kind;state->every=every;state->owner=owner;
        const bool timed=kind==SignalKind::GameSeconds||kind==SignalKind::Seconds;
        if constexpr(std::is_convertible_v<Source,const char*>){
            if(timed)RowError(row,"A timed trigger requires a numeric duration.");
            state->key=source;
            state->seen=execution->Version(kind,source);
        }else{
            const double seconds=static_cast<double>(source);
            if(!timed||!std::isfinite(seconds)||seconds<0)
                RowError(row,"Trigger duration must be a finite, non-negative number.");
            state->clock=TriggerClock{seconds,triggerTick};
        }
        triggers.push_back(state);
        return {state,true};
    }
    void TickTriggers() {
        ++triggerTick;
        for(auto it=triggers.begin();it!=triggers.end();){
            auto& trigger=**it;
            if(trigger.Expired()){
                trigger.Disarm();
                it=triggers.erase(it);
                continue;
            }
            ++it;
            if(!trigger.armed)continue;
            if(trigger.clock)
                trigger.clock->Advance(execution->Elapsed(trigger.kind==SignalKind::GameSeconds),triggerTick);
            auto& foreground=static_cast<Detail::Suspension&>(*execution);
            struct Restore {
                Detail::Suspension& foreground;
                Detail::Suspension& background;
                ~Restore(){std::swap(foreground,background);}
            } restore{foreground,trigger.suspension};
            std::swap(foreground,trigger.suspension);
            try {
                if(trigger.running){
                    foreground.remaining-=execution->Elapsed(foreground.timerClock);
                    if(foreground.remaining>0)continue;
                    auto resume=std::exchange(foreground.suspended,{});
                    if(!resume)throw std::logic_error("Trigger suspended outside a script wait or loop.");
                    resume.resume();
                }else{
                    if(!trigger.ConsumeOccurrence(*execution))continue;
                    if(trigger.sync)trigger.sync();
                    else{
                        trigger.running.emplace(trigger.async());
                        trigger.running->Resume();
                    }
                }
                execution->CheckInvocation();
                if(trigger.running){
                    if(!trigger.running->Done())continue;
                    trigger.running->Finish();
                    trigger.running.reset();
                }
                ++trigger.completed;
                if(!trigger.every)trigger.Disarm();
            } catch(const Detail::EndTrigger&) {
                trigger.Disarm();
            }
        }
    }
    [[noreturn]] void RowError(std::uint64_t row, const char* message) const {
        if (row && execution && execution->host.diagnostic)
            execution->host.diagnostic(execution->host.user,row,message);
        throw std::invalid_argument(message);
    }
    void UpdateStates() {
        std::vector<std::string> groups;
        for (const auto& entry : states) groups.push_back(entry.first);
        for (const auto& group : groups) {
            auto found = definitions.find({group, states.at(group)});
            if (found != definitions.end()) {
                auto callback = found->second.update;
                if (callback) callback();
                CheckInvocation();
            }
        }
    }
public:
    Task<> Start() { co_return; }
    Task<> Update() { co_return; }
    template<class T> T RunNow(Task<T> task) {
        // These callbacks cannot retain a suspended invocation past their exit.
        // Restore the outer frame's checkpoint even when validation was bypassed.
        const auto pending=static_cast<const Detail::Suspension&>(*execution);
        task.Resume();
        if (!task.Done()) {
            static_cast<Detail::Suspension&>(*execution)=pending;
            throw std::logic_error("A synchronous callback cannot wait or yield.");
        }
        return task.Finish();
    }
    bool Read(const char*, Value&) const noexcept { return false; }
    float DeltaTime() const noexcept { return execution ? execution->dt : 0; }
    void Move(std::optional<double> value, double x, double y, double z, MoveUnit unit, std::uint64_t row=0) {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || (value && !std::isfinite(*value)))
            RowError(row,"Move requires finite numeric inputs.");
        if (value) {
            // Scale before normalizing so very large and very small directions keep their orientation.
            const double largest=std::max({std::abs(x),std::abs(y),std::abs(z)});
            if (largest==0 || *value==0) return;
            x/=largest; y/=largest; z/=largest;
            const double length=std::sqrt(x*x+y*y+z*z);
            x=(x/length)*(*value); y=(y/length)*(*value); z=(z/length)*(*value);
        }
        const auto& h=execution->host;
        if (!h.move || !h.move(h.user,x,y,z,unit))
            RowError(row,"Cannot apply Move to this transform.");
    }
    void ChangeGameSpeed(std::string_view operation, double operand, std::uint64_t row=0) {
        const auto& h=execution->host;
        if (!h.setGameSpeed || !h.gameSpeed) RowError(row,"Game Speed is unavailable.");
        if (!std::isfinite(operand)) RowError(row,"Game Speed requires a finite number.");
        double speed=operand;
        if (operation!="=") {
            const double current=h.gameSpeed(h.user);
            if (operation=="+=") speed=current+operand;
            else if (operation=="-=") speed=current-operand;
            else if (operation=="*=") speed=current*operand;
            else if (operation=="/=" && operand!=0) speed=current/operand;
            else RowError(row,"Invalid Game Speed operation.");
        }
        if (!std::isfinite(speed) || speed>std::numeric_limits<float>::max())
            RowError(row,"Game Speed exceeds the supported range.");
        h.setGameSpeed(h.user,static_cast<float>(std::max(0.,speed)));
    }
    void Log(Severity severity, const char* text) const {
        if (execution && execution->host.log)
            execution->host.log(execution->host.user, static_cast<int>(severity), text);
    }
    template<class F> void SafeDestroy(F&& action) noexcept {
        try { std::forward<F>(action)(); }
        catch(const Detail::EndInvocation&) {
            if(execution) execution->pendingStop=true;
        } catch(const std::exception& error) {
            if(execution) execution->pendingStop=true;
            try { Log(Severity::Error,error.what()); } catch(...) {}
        } catch(...) {
            if(execution) execution->pendingStop=true;
            try { Log(Severity::Error,"Unknown error in On Destroy."); } catch(...) {}
        }
    }
    void CheckInvocation() const { if(execution) execution->CheckInvocation(); }
    struct WaitPoint {
        Detail::Execution& execution;
        double seconds;
        std::optional<bool> timerClock;
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> invocation) const noexcept {
            execution.remaining = seconds;
            execution.timerClock = timerClock;
            execution.suspended = invocation;
        }
        void await_resume() const noexcept {}
    };
    WaitPoint Wait(double seconds,std::uint64_t row=0) {
        if (!std::isfinite(seconds) || seconds < 0)
            RowError(row,"Wait duration must be a finite, non-negative number.");
        return {*execution, seconds};
    }
    Task<bool> TimerSeconds(double seconds,bool gameSeconds,std::uint64_t row=0) {
        auto delay=Wait(seconds,row);
        delay.timerClock=gameSeconds;
        co_await delay;
        co_return true;
    }
    Task<bool> TimerSignal(SignalKind kind,std::string key,std::uint64_t =0) {
        auto initial=execution->Version(kind,key.c_str());
        do { co_await Wait(0); } while(execution->Version(kind,key.c_str())==initial);
        co_return true;
    }
    void EmitEvent(const char* key) { execution->Publish(SignalKind::Event,key); }
    void SignalFunction(const char* key) { execution->Publish(SignalKind::Function,key); }
    [[noreturn]] void BreakTrigger() { throw Detail::EndTrigger{}; }
    bool HasTrigger(std::uint64_t row,const std::weak_ptr<void>& owner={}) const {
        return static_cast<bool>(FindTrigger(row,owner));
    }
    template<class F,class Source> void WatchTrigger(std::uint64_t row,SignalKind kind,Source source,bool every,
                                        F body,std::weak_ptr<void> owner={}) {
        auto [state,added]=Subscribe(row,kind,source,every,owner);
        if(added)state->async=std::move(body);
    }
    template<class F,class Source> Task<> BlockTrigger(std::uint64_t row,SignalKind kind,Source source,bool every,
                                          F body,std::weak_ptr<void> owner={}) {
        auto [state,added]=Subscribe(row,kind,source,every,owner);
        if(added)state->sync=std::move(body);
        struct Gate {
            TriggerState& trigger;
            bool ownsBorrowedBody;
            ~Gate(){
                --trigger.gates;
                if(ownsBorrowedBody&&trigger.armed)trigger.Disarm();
            }
        } gate{*state,added&&!every};
        ++state->gates;
        while(state->armed&&state->completed==state->consumed)co_await Wait(0);
        if(state->consumed<state->completed)++state->consumed;
    }
    bool TriggerBlocking(std::uint64_t row) const {
        for(const auto& trigger:triggers)if(trigger->row==row&&trigger->gates&&trigger->armed)return true;
        return false;
    }
    struct LoopPoint {
        Detail::Execution& execution;
        bool await_ready() const noexcept { return execution.budget > 0; }
        void await_suspend(std::coroutine_handle<> invocation) const noexcept {
            execution.remaining = 0;
            execution.suspended = invocation;
        }
        void await_resume() const noexcept { --execution.budget; }
    };
    LoopPoint Checkpoint() { return {*execution}; }
    void Activate(const char* binding, const char* component, bool active) {
        const auto& h = execution->host;
        int result = h.activate ? h.activate(h.user, binding, component, active) : 0;
        if (result < 0) throw Detail::EndInvocation{};
        if (result == 0) {
            const std::string message = std::string("Activation target is unavailable: ") +
                (*binding ? binding : "This") + (*component ? " / " : "") + component;
            Log(Severity::Error, message.c_str());
        }
    }
    bool Active(const char* binding = "", const char* component = "") const {
        const auto& h = execution->host;
        return h.isActive && h.isActive(h.user, binding, component);
    }
    template<class T> T ReadChild(const char* binding, const char* name) const {
        const auto& h = execution->host;
        Value value;
        if (!h.readChild || !h.readChild(h.user, binding, name, &value))
            throw std::runtime_error(std::string("Child variable is unavailable: ") + name);
        return NumberValue<T>(value);
    }
    template<class Predicate> bool Physics(Phase phase, Predicate predicate) const {
        const auto& h = execution->host;
        Contact event;
        for (int i = 0; h.contact && h.contact(h.user, static_cast<int>(phase), i, &event); ++i)
            if (predicate(event)) return true;
        return false;
    }
    bool Physics(Phase phase) const { return Physics(phase, [](const Contact&) { return true; }); }

    void DefineState(std::string group, std::string name, std::function<void()> onEnter = {},
                     std::function<void()> onUpdate = {}, std::function<void()> onExit = {}) {
        definitions.insert_or_assign({std::move(group), std::move(name)},
            StateActions{std::move(onEnter), std::move(onUpdate), std::move(onExit)});
    }
    std::string State(const std::string& group) const {
        auto found = states.find(group);
        return found == states.end() ? std::string{} : found->second;
    }
    void SetState(std::string group, std::string name) {
        ChangeState(std::move(group), std::move(name));
    }
    void ChangeState(std::string group, std::string name) {
        changes.emplace_back(std::move(group), std::move(name));
        if (processing) return;
        struct DrainGuard {
            ScriptBase& script;
            ~DrainGuard() { script.processing = false; script.changes.clear(); }
        } guard{*this};
        processing = true;
        std::vector<StateKey> path;
        bool warned = false;
        while (!changes.empty()) {
            auto key = std::move(changes.front());
            changes.pop_front();
            auto old = states.find(key.first);
            if (old != states.end() && old->second == key.second) continue;
            if (!warned) {
                if (std::find(path.begin(), path.end(), key) != path.end()) {
                    std::string warning = "Possible state transition cycle: ";
                    for (const auto& step : path) warning += step.first + "." + step.second + " -> ";
                    warning += key.first + "." + key.second;
                    Log(Severity::Warning, warning.c_str());
                    warned = true;
                } else path.push_back(key);
            }
            if (old != states.end()) {
                auto found = definitions.find({key.first, old->second});
                if (found != definitions.end()) {
                    auto callback = found->second.exit;
                    if (callback) callback();
                    CheckInvocation();
                }
            }
            states.insert_or_assign(key.first, key.second);
            auto found = definitions.find(key);
            if (found != definitions.end()) {
                auto callback = found->second.enter;
                if (callback) callback();
                CheckInvocation();
            }
        }
    }

    Value LoadField(const FieldBinding& binding, Value initial) const {
        const auto& h = execution->host;
        if (!h.readField || !h.readField(h.user, &binding, &initial))
            throw std::runtime_error(std::string("Cannot read RiceCon variable: ") + binding.name);
        return initial;
    }
    void StoreField(const FieldBinding& binding, Value value) {
        const auto& h = execution->host;
        if (!h.writeField || !h.writeField(h.user, &binding, &value))
            throw std::runtime_error(std::string("Cannot write RiceCon variable: ") + binding.name);
    }
    void BindField(const FieldBinding& binding, Value initial) {
        const auto& h = execution->host;
        if (!h.bindField || !h.bindField(h.user, &binding, &initial))
            throw std::runtime_error(std::string("Cannot bind RiceCon variable: ") + binding.name);
    }
};

// Each factory creates a new behaviour object. Re-selecting a previously used
// strategy therefore starts with that strategy's initial local state.
template<class Signature> class Strategy;
namespace Detail {
template<class T> struct IsTask : std::false_type { using Result = T; };
template<class T> struct IsTask<Task<T>> : std::true_type { using Result = T; };
}
template<class R, class... Args> class Strategy<R(Args...)> {
    using Behavior = std::function<R(Args...)>;
public:
    void Define(std::string name, std::function<Behavior()> factory) {
        factories.insert_or_assign(std::move(name), std::move(factory));
    }
    void Select(const std::string& name) {
        auto found = factories.find(name);
        if (found == factories.end()) throw std::invalid_argument("Unknown strategy: " + name);
        Replace(found->second);
    }
    void Replace(std::function<Behavior()> factory) {
        auto next = factory();
        if (!next) throw std::invalid_argument("This strategy has no behaviour.");
        current = std::make_shared<Behavior>(std::move(next));
    }
    R Invoke(Args... args) {
        if (!current) throw std::runtime_error("Choose a strategy first.");
        if constexpr (Detail::IsTask<R>::value)
            return AwaitInvocation(current, std::forward<Args>(args)...);
        else {
            auto selected = current;
            return (*selected)(std::forward<Args>(args)...);
        }
    }
    R operator()(Args... args) { return Invoke(std::forward<Args>(args)...); }
private:
    static R AwaitInvocation(std::shared_ptr<Behavior> selected, Args... args)
        requires Detail::IsTask<R>::value {
        if constexpr (std::is_void_v<typename Detail::IsTask<R>::Result>) {
            co_await (*selected)(std::forward<Args>(args)...);
            co_return;
        } else co_return co_await (*selected)(std::forward<Args>(args)...);
    }
    std::map<std::string, std::function<Behavior()>> factories;
    std::shared_ptr<Behavior> current;
};

template<class Output, class... Args> using StrategySlot = Strategy<Task<Output>(Args...)>;
template<class Owner, class Class, class Return, class... Args>
auto MakeStrategy(Owner& owner, Return (Class::*method)(Args...)) {
    return [&owner, method] {
        auto object = [&] {
            if constexpr (std::is_constructible_v<Class, Owner&>) return std::make_shared<Class>(owner);
            else return std::make_shared<Class>();
        }();
        if constexpr (Detail::IsTask<Return>::value) {
            return [object=std::move(object), method](Args... args) {
                return ((*object).*method)(std::forward<Args>(args)...);
            };
        } else {
            return [object=std::move(object), method](Args... args) -> Task<Return> {
                if constexpr (std::is_void_v<Return>) {
                    ((*object).*method)(std::forward<Args>(args)...);
                    co_return;
                } else co_return ((*object).*method)(std::forward<Args>(args)...);
            };
        }
    };
}

template<class T> class Routed;
namespace Detail {
template<class V> struct RoutedAxis {
    Routed<V>& field;
    float V::* member;
    operator float() const { return field.Get().*member; }
    RoutedAxis& operator=(float n) { auto v=field.Get(); v.*member=n; field.Set(v); return *this; }
    RoutedAxis& operator=(const RoutedAxis& other) { return *this=static_cast<float>(other); }
    RoutedAxis& operator+=(float n) { return *this=static_cast<float>(*this)+n; }
    RoutedAxis& operator-=(float n) { return *this=static_cast<float>(*this)-n; }
};
template<class T> struct RoutedMembers { explicit RoutedMembers(Routed<T>&) {} };
template<> struct RoutedMembers<Vec2> {
    RoutedAxis<Vec2> x,y;
    explicit RoutedMembers(Routed<Vec2>& v):x{v,&Vec2::x},y{v,&Vec2::y}{}
};
template<> struct RoutedMembers<Vec3> {
    RoutedAxis<Vec3> x,y,z;
    explicit RoutedMembers(Routed<Vec3>& v):x{v,&Vec3::x},y{v,&Vec3::y},z{v,&Vec3::z}{}
};
}

template<class T> class Routed : public Detail::RoutedMembers<T> {
public:
    Routed(ScriptBase& owner, FieldBinding binding, T initial)
        : Detail::RoutedMembers<T>(*this), owner(owner), binding(binding), initial(std::move(initial)) {}
    Routed(const Routed& other)
        : Detail::RoutedMembers<T>(*this), owner(other.owner), binding(other.binding), initial(other.initial), bound(other.bound) {}
    T Get() const {
        auto result = NumberValue<T>(owner.LoadField(binding, ToValue(initial)));
        bound = true;
        return result;
    }
    void Set(T value) {
        // Establish the declared default even if the first user action is a write.
        if (!bound) {
            owner.BindField(binding, ToValue(initial));
            bound = true;
        }
        owner.StoreField(binding, ToValue(value));
    }
    operator T() const { return Get(); }
    Routed& operator=(T value) { Set(value); return *this; }
    Routed& operator=(const Routed& value) { return *this = value.Get(); }
    Routed& operator+=(T value) { return *this = Get() + value; }
    Routed& operator-=(T value) { return *this = Get() - value; }
    Routed& operator++() { return *this += 1; }
    T operator++(int) { auto old = Get(); Set(old + 1); return old; }
    std::size_t size() const requires requires(T v){v.size();} { return Get().size(); }
    bool empty() const requires requires(T v){v.empty();} { return Get().empty(); }
    void clear() requires requires(T v){v.clear();} { auto value=Get();value.clear();Set(std::move(value)); }
    template<class V> void push_back(V&& item) requires requires(T v){v.push_back(std::forward<V>(item));} {
        auto value=Get();value.push_back(std::forward<V>(item));Set(std::move(value));
    }
    template<class K,class V> struct Element {
        Routed& field;
        K key;
        operator V() const { return field.Get().at(key); }
        Element& operator=(V value) {
            auto copy=field.Get();
            if constexpr (requires{typename T::mapped_type;}) copy.insert_or_assign(key,std::move(value));
            else copy.at(key)=std::move(value);
            field.Set(std::move(copy));return *this;
        }
        Element& operator=(const Element& other) { return *this=static_cast<V>(other); }
        Element& operator+=(const V& n) {
            auto copy=field.Get();
            copy.at(key)+=n;
            field.Set(std::move(copy));
            return *this;
        }
    };
    template<class Key> auto operator[](Key&& key) requires requires(T v){v.at(key);} {
        if constexpr(requires{typename T::mapped_type;})
            return Element<typename T::key_type,typename T::mapped_type>{*this,std::forward<Key>(key)};
        else return Element<std::size_t,typename T::value_type>{*this,static_cast<std::size_t>(key)};
    }
private:
    ScriptBase& owner;
    FieldBinding binding;
    T initial;
    mutable bool bound = false;
};
template<class T> std::vector<T> Snapshot(const Routed<std::vector<T>>& list) { return list.Get(); }

namespace Detail {
template<class Script> class Instance {
public:
    explicit Instance(Host host, std::size_t budget = 1024)
        : execution{host}, iterationBudget(std::max<std::size_t>(1, budget)) {
        ContextGuard guard(execution);
        script.emplace();
    }
    ~Instance() {
        ContextGuard guard(execution);
        script->triggers.clear();
        running.reset();
        script.reset();
    }
    void Tick(float dt) noexcept {
        if (failed) return;
        ContextGuard guard(execution);
        try {
            execution.CheckInvocation();
            execution.dt = dt;
            execution.budget = iterationBudget;
            script->TickTriggers();
            if (running) {
                execution.remaining = std::max(0.0, execution.remaining - execution.Elapsed(execution.timerClock));
                if (execution.remaining > 0) return;
                auto resume = std::exchange(execution.suspended, {});
                if (!resume) throw std::logic_error("Script suspended outside a script wait or loop.");
                resume.resume();
                if (!Complete() || started) return;
                started = true;
            } else if (!started) {
                running.emplace(BeginCall([this]{ return script->Start(); }));
                running->Resume();
                if (!Complete()) return;
                started = true;
            }
            script->UpdateStates();
            execution.CheckInvocation();
            running.emplace(BeginCall([this,dt]{
                if constexpr (requires { script->Update(dt); }) return script->Update(dt);
                else return script->Update();
            }));
            running->Resume();
            Complete();
        } catch (const EndInvocation&) {
            // Self-deactivation completes this invocation, including its caller.
            started = true;
            script->triggers.clear();
            ResetCall();
        } catch (const std::exception& error) {
            Fail(error.what());
        } catch (...) {
            Fail("Unknown script runtime error.");
        }
    }
    bool Read(const char* name, Value& out) const { return script->Read(name, out); }
    bool TriggerBlocking(std::uint64_t row) const { return script->TriggerBlocking(row); }
private:
    template<class F> static Task<> SynchronousCall(F action) {
        action();
        co_return;
    }
    template<class F> static Task<> BeginCall(F action) {
        if constexpr(std::is_void_v<std::invoke_result_t<F>>) return SynchronousCall(std::move(action));
        else return action();
    }
    bool Complete() {
        execution.CheckInvocation();
        if (!running->Done()) return false;
        running->Finish();
        ResetCall();
        return true;
    }
    void ResetCall() noexcept {
        execution.suspended = {}; execution.remaining = 0; running.reset(); execution.pendingStop=false;
    }
    void Fail(const char* reason) noexcept {
        failed = true;
        script->Log(Severity::Error, reason);
        script->triggers.clear();
        ResetCall();
    }
    Detail::Execution execution;
    std::optional<Script> script;
    std::size_t iterationBudget;
    bool started{}, failed{};
    std::optional<Task<>> running; // Destroy suspended calls before their owning script.
};
}

template<class Script> const ModuleApi* ModuleFor() {
    using Instance = Detail::Instance<Script>;
    static const ModuleApi api{ModuleAbi,
        [](const Host* host) -> void* {
            if (!host) return nullptr;
            try { return new Instance(*host); }
            catch (...) {
                if (host->log) host->log(host->user, int(Severity::Error), "Cannot initialize script.");
                return nullptr;
            }
        },
        [](void* p, float dt) { if (p) static_cast<Instance*>(p)->Tick(dt); },
        [](void* p) { delete static_cast<Instance*>(p); },
        [](void* p, const char* name, Value* out) {
            if (!p || !name || !out) return false;
            try { return static_cast<Instance*>(p)->Read(name, *out); }
            catch (...) { return false; }
        },
        [](void* p,std::uint64_t row){return p&&static_cast<Instance*>(p)->TriggerBlocking(row);}};
    return &api;
}

} // namespace Rice::Scripting

#ifdef _WIN32
#define RICE_SCRIPT_EXPORT __declspec(dllexport)
#else
#define RICE_SCRIPT_EXPORT __attribute__((visibility("default")))
#endif
#define RICE_SCRIPT_MODULE(Type) extern "C" RICE_SCRIPT_EXPORT const ::Rice::Scripting::ModuleApi* RiceGetScriptApi() { return ::Rice::Scripting::ModuleFor<Type>(); }
