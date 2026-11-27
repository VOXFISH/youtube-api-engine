#include "Scripting/ScriptRuntime.h"
#include <iostream>

using namespace Rice::Scripting;

namespace {
void Check(bool ok, const char* reason) {
    if (!ok) throw std::runtime_error(reason);
}
struct Observed {
    std::vector<std::string> log;
    std::vector<std::string> activations;
    Host Callbacks() {
        Host host{};
        host.user = this;
        host.log = [](void* p, int severity, const char* message) {
            static_cast<Observed*>(p)->log.push_back(std::to_string(severity) + " " + message);
        };
        host.activate = [](void* p, const char* binding, const char*, bool active) {
            static_cast<Observed*>(p)->activations.emplace_back(binding);
            if (std::string_view(binding) == "deleted") return 0;
            return !*binding && !active ? -1 : 1;
        };
        return host;
    }
};
struct WaitScript : ScriptBase {
    int step{}, updates{};
    Task<int> Delay(int input) {
        ++step;
        co_await Wait(1);
        co_return input + 1;
    }
    Task<> Start() {
        co_await Wait(0);
        step = co_await Delay(40);
    }
    Task<> Update() {
        ++updates;
        co_return;
    }
    bool Read(const char* name, Value& result) const {
        if (std::string_view(name) == "step") result = ToValue(step);
        else result = ToValue(updates);
        return true;
    }
};
template<class T> int Read(const T& runtime, const char* name) {
    Value result;
    Check(runtime.Read(name, result), "Cannot read script member");
    return NumberValue<int>(result);
}
struct LoopScript : ScriptBase {
    int number{};
    std::vector<int> values{1,2,3};
    Task<> Update() {
        for (auto item : Snapshot(values)) {
            co_await Checkpoint();
            values.clear();
            number = number * 10 + item;
        }
        Activate("deleted", "", true);
        Activate("sibling", "", true);
        Activate("", "", false);
        number = -1;
    }
    bool Read(const char*, Value& result) const { result = ToValue(number); return true; }
};
struct StateScript : ScriptBase {
    int entries{}, exits{}, updates{};
    Task<> Start() {
        auto exit = [this] { ++exits; };
        DefineState("movement", "Idle", [this] {
            if (++entries < 6) ChangeState("movement", "Move");
        }, {}, exit);
        DefineState("movement", "Move", [this] {
            if (++entries < 6) ChangeState("movement", "Idle");
        }, [this]{ ++updates; }, exit);
        SetState("movement", "Idle");
        ChangeState("movement", State("movement"));
        co_return;
    }
    bool Read(const char* name, Value& value) const {
        std::string_view n(name);
        value = ToValue(n == "entries" ? entries : (n == "exits" ? exits : updates));
        return true;
    }
};
struct BudgetScript : ScriptBase {
    int steps{};
    Task<> Part() { co_await Checkpoint(); ++steps; }
    Task<> Start() { co_await Part(); }
    Task<> Update() { co_await Part(); co_await Part(); co_await Wait(0); ++steps; }
    bool Read(const char*, Value& out) const { out=ToValue(steps);return true; }
};
struct NativeLifecycle : ScriptBase {
    int starts{},updates{};
    float seconds{};
    void Start(){++starts;}
    void Update(float dt){++updates;seconds+=dt;}
    bool Read(const char* name,Value& out)const {
        out=std::string_view(name)=="start"?ToValue(starts):std::string_view(name)=="updates"?ToValue(updates):ToValue(seconds);
        return true;
    }
};
struct TriggerProbe : ScriptBase {
    int frames{},pulses{},once{},privateHits{},continued{};
    Task<> Start(){
        WatchTrigger(1,SignalKind::Function,"public:go",true,[this]() -> Task<> {
            ++pulses;
            co_await Wait(0);
            if(pulses==2)BreakTrigger();
        });
        WatchTrigger(2,SignalKind::Event,"private:ping",false,[this]() -> Task<> {
            ++privateHits; co_return;
        });
        localPublisher=this;
        co_return;
    }
    static inline TriggerProbe* localPublisher{};
    Task<> Update(){
        ++frames;
        co_await BlockTrigger(3,SignalKind::Event,"public:unlock",false,[&]{++once;});
        ++continued;
    }
    bool Read(const char* name,Value& out) const {
        const std::string_view n=name;
        out=ToValue(n=="frames"?frames:n=="once"?once:n=="private"?privateHits:n=="pulses"?pulses:continued);
        return true;
    }
};
struct TimerProbe : ScriptBase {
    int stage{};
    Task<> Start(){
        if(co_await TimerSeconds(1,false,11))++stage;
        if(co_await TimerSeconds(1,true,12))++stage;
        if(co_await TimerSignal(SignalKind::Function,"public:clock",13))++stage;
    }
    bool Read(const char*,Value& out) const {out=ToValue(stage);return true;}
};
struct TimedTriggerProbe : ScriptBase {
    int realSingle{},realEvery{},gameEvery{},zero{},busyStarts{},busyEnds{},samples{},unlocked{},suffix{};
    TriggerLifetime owner;
    static inline TimedTriggerProbe* current{};
    TimedTriggerProbe(){current=this;}
    Task<> Start(){
        WatchTrigger(31,SignalKind::Seconds,.5,false,[this]() -> Task<>{++realSingle;co_return;},owner.Token());
        WatchTrigger(32,SignalKind::Seconds,.5,true,[this]() -> Task<>{++realEvery;co_return;});
        WatchTrigger(33,SignalKind::GameSeconds,1,true,[this]() -> Task<>{++gameEvery;co_return;});
        WatchTrigger(34,SignalKind::Seconds,0,true,[this]() -> Task<>{if(++zero==2)BreakTrigger();co_return;});
        WatchTrigger(35,SignalKind::Seconds,.25,true,[this]() -> Task<>{
            ++busyStarts;
            co_await Wait(0);
            co_await Wait(0);
            ++busyEnds;
        });
        co_return;
    }
    Task<> Update(){
        co_await BlockTrigger(36,SignalKind::GameSeconds,HasTrigger(36)?0.0:(++samples)*.5,
                              false,[this]{++unlocked;});
        ++suffix;
    }
    bool Read(const char*,Value&) const {return false;}
};
struct LifetimeProbe : ScriptBase {
    int startDone{},persistent{},borrowed{},ended{};
    std::optional<TriggerLifetime> scope{std::in_place};
    Task<> Start(){
        WatchTrigger(21,SignalKind::Event,"private:nested",true,[this]() -> Task<> {
            int local=7;
            co_await BlockTrigger(22,SignalKind::Event,"private:finish",false,[&]{borrowed=local;});
            ++ended;
        },scope->Token());
        co_await BlockTrigger(23,SignalKind::Event,"private:pulse",true,[this]{
            if(++persistent==2)BreakTrigger();
        });
        ++startDone;
    }
    bool Read(const char*,Value&)const{return false;}
    static inline LifetimeProbe* current{};
    LifetimeProbe(){current=this;}
};
struct SignalBus {
    std::map<std::pair<SignalKind,std::string>,std::uint64_t> versions;
    float game{},real{};
    Host Callbacks(){
        Host host{};
        host.user=this;
        host.signalVersion=[](void* p,SignalKind kind,const char* key){return static_cast<SignalBus*>(p)->versions[{kind,key}];};
        host.publishSignal=[](void* p,SignalKind kind,const char* key){++static_cast<SignalBus*>(p)->versions[{kind,key}];};
        host.timerDelta=[](void* p,bool game){auto& bus=*static_cast<SignalBus*>(p);return game?bus.game:bus.real;};
        return host;
    }
};
void TestTimedTriggers(){
    SignalBus bus;
    Detail::Instance<TimedTriggerProbe> instance(bus.Callbacks());
    auto& probe=*TimedTriggerProbe::current;
    instance.Tick(0);
    Check(probe.HasTrigger(31,probe.owner.Token())&&!probe.HasTrigger(31)&&instance.TriggerBlocking(36),
          "Timed triggers are registered per owner and the blocking caller waits");
    Check(probe.zero==0&&probe.suffix==0,"A zero-duration trigger first becomes due on the next active tick");
    bus.real=.75f;
    instance.Tick(0);
    bus.real=.25f;
    instance.Tick(0);
    Check(probe.realSingle==1&&probe.realEvery==2&&probe.zero==2&&probe.gameEvery==0&&probe.suffix==0,
          "Seconds keeps its fractional phase while the game clock is stopped");
    bus.real=4;
    instance.Tick(0);
    Check(probe.realEvery==3&&probe.busyStarts==1&&probe.busyEnds==1,
          "A long tick coalesces elapsed intervals without overlapping a suspended handler");
    bus.real=0;
    bus.game=.5f;
    instance.Tick(0);
    Check(probe.busyStarts==2&&probe.unlocked==1&&probe.suffix==1&&!instance.TriggerBlocking(36),
          "A pending timed firing runs after its body completes and the scaled gate releases its caller");
    instance.Tick(0);
    bus.game=1;
    instance.Tick(0);
    Check(probe.realEvery==3&&probe.realSingle==1&&probe.gameEvery==2&&probe.zero==2&&probe.samples==1&&probe.suffix==3,
          "Single and break stay retired, every follows game time, and revisiting a row does not resample the duration");
    for(double invalid:{-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        bool rejected=false;
        try {probe.WatchTrigger(37,SignalKind::Seconds,invalid,true,[]() -> Task<>{co_return;});}
        catch(const std::invalid_argument&){rejected=true;}
        Check(rejected&&!probe.HasTrigger(37),"A bad duration is rejected without registering a listener");
    }
}
void TestTriggersAndTimers(){
    SignalBus bus,otherScene;
    Detail::Instance<TriggerProbe> one(bus.Callbacks()),two(bus.Callbacks()),elsewhere(otherScene.Callbacks());
    one.Tick(0); auto* emitter=TriggerProbe::localPublisher;
    two.Tick(0);elsewhere.Tick(0);
    Check(one.TriggerBlocking(3)&&Read(one,"continued")==0,"Blocking trigger suspends the Update suffix");
    emitter->SignalFunction("public:go");emitter->SignalFunction("public:go");emitter->SignalFunction("public:go");
    emitter->EmitEvent("private:ping"); emitter->EmitEvent("public:unlock");
    for(int frame=0;frame<6;++frame){one.Tick(0);two.Tick(0);elsewhere.Tick(0);}
    Check(!one.TriggerBlocking(3)&&Read(one,"once")==1&&Read(one,"continued")==6,
          "A blocking once resumes its caller and skips the retired trigger on later frames");
    Check(Read(one,"pulses")==2&&Read(two,"pulses")==2&&Read(elsewhere,"pulses")==0,
          "Public function pulses queue across instances; break retires a suspended every handler");
    Check(Read(one,"private")==1&&Read(two,"private")==0&&elsewhere.TriggerBlocking(3),
          "Private events stay in their instance and public events stay in their play scene");

    Detail::Instance<TimerProbe> timer(bus.Callbacks());
    bus.real=.5f;
    timer.Tick(0);timer.Tick(0);timer.Tick(0);
    Check(Read(timer,"stage")==1,"Seconds counts unscaled time even at zero game speed");
    timer.Tick(0);bus.game=.5f;
    timer.Tick(0);timer.Tick(0);
    Check(Read(timer,"stage")==2,"Game Seconds counts the scaled scene clock");
    emitter->SignalFunction("public:other");timer.Tick(0);
    Check(Read(timer,"stage")==2,"A timer waits for the selected function");
    emitter->SignalFunction("public:clock");timer.Tick(0);
    Check(Read(timer,"stage")==3,"A selected function invocation completes its timer");

    Detail::Instance<LifetimeProbe> lifetime(bus.Callbacks());
    lifetime.Tick(0);
    auto& probe=*LifetimeProbe::current;
    probe.EmitEvent("private:nested");
    probe.EmitEvent("private:pulse");
    lifetime.Tick(0);
    Check(lifetime.TriggerBlocking(22)&&probe.startDone==1,"A nested trigger can await its own gate while Start returns");
    probe.scope.reset();
    probe.EmitEvent("private:finish");
    for(int i=0;i<3;++i){probe.EmitEvent("private:pulse");lifetime.Tick(0);}
    Check(!lifetime.TriggerBlocking(22)&&probe.ended==0&&probe.borrowed==0&&probe.persistent==2,
          "Expiration cancels the borrowed gate before destroying its locals; every stays armed past Start until break");
}
struct DestroyingScript : ScriptBase {
    int created{},destroyed{},continued{};
    struct Local {
        DestroyingScript& owner;
        explicit Local(DestroyingScript& owner):owner(owner){++owner.created;}
        ~Local(){owner.SafeDestroy([&]{
            ++owner.destroyed;
            owner.Activate("","",false);
            ++owner.continued;
        });}
    };
    Task<> Nested(){ Local local(*this);co_return; }
    Task<> Start(){ co_await Nested();++continued; }
    Task<> Update(){
        std::optional<Local> local;
        CheckedEmplace(local,*this);
        CheckedEmplace(local,*this);
        ++continued;
        co_return;
    }
    bool Read(const char* key,Value& result)const {
        auto name=std::string_view(key);
        result=ToValue(name=="created"?created:name=="destroyed"?destroyed:continued);
        return true;
    }
};
struct BadWait : ScriptBase {
    double duration=-1.0;
    Task<> Start(){ co_await Wait(duration,987); }
};
struct CancelScript : ScriptBase {
    static inline int live{}, completed{};
    struct Guard { Guard(){++live;} ~Guard(){--live;} };
    Task<> Inner() { Guard local; co_await Wait(2); ++completed; }
    Task<> Start() { Guard outer; co_await Inner(); ++completed; }
};
struct TypedStrategyScript : ScriptBase {
    struct Walk {
        TypedStrategyScript& owner;
        int calls{};
        explicit Walk(TypedStrategyScript& owner):owner(owner){}
        Task<int> Step(int argument) {
            ++calls;
            owner.movement.Replace(MakeStrategy(owner,&Walk::Step));
            co_await owner.Wait(0);
            co_return argument + calls;
        }
    };
    StrategySlot<int,int> movement;
    int result{};
    Task<> Start() {
        movement.Replace(MakeStrategy(*this,&Walk::Step));
        result = co_await movement.Invoke(10);
        result += co_await movement.Invoke(20);
    }
    bool Read(const char*,Value& out) const {out=ToValue(result);return true;}
};
struct Row {
    static constexpr std::string_view RiceType="Inventory";
    int count{};
    std::vector<bool> flags;
    auto RiceFields() { return std::tie(count,flags); }
    auto RiceFields() const { return std::tie(count,flags); }
    bool operator==(const Row&) const = default;
};
struct DataScript : ScriptBase {
    Routed<std::vector<int>> list{*this,{"inventory","items","global","","locked",""},{2}};
    Task<> Start() {
        list=std::vector<int>{7,8};
        list[0]+=2;
        list.push_back(11);
        co_return;
    }
};
void TestCompoundValues() {
    using Map=std::unordered_map<int,Row>;
    Map input{{2,Row{3,{true,false,true}}},{1,Row{5,{}}}};
    auto value=ToValue(input);
    std::vector<std::uint8_t> expected(value.bytes,value.bytes+value.length);
    Check(NumberValue<Map>(value)==input,"Reflected data and nested containers must round trip");
    Map reversed;
    reversed.emplace(1,input.at(1));
    reversed.emplace(2,input.at(2));
    value=ToValue(reversed);
    Check(std::equal(expected.begin(),expected.end(),value.bytes,value.bytes+value.length),"Dictionary storage must be deterministic");
    --value.length;
    bool invalid=false;
    try { (void)NumberValue<Map>(value); } catch(const std::exception&){invalid=true;}
    Check(invalid,"Incomplete compound data must fail without partial decode");
    struct Storage {std::vector<int> values;std::vector<std::string> operations;} storage;
    Host host{};host.user=&storage;
    host.bindField=[](void* p,const FieldBinding*,const Value* initial) {
        auto& s=*static_cast<Storage*>(p);s.operations.push_back("bind");
        return NumberValue<std::vector<int>>(*initial)==std::vector<int>{2};
    };
    host.readField=[](void* p,const FieldBinding*,Value* out) {
        auto& s=*static_cast<Storage*>(p);s.operations.push_back("read");*out=ToValue(s.values);return true;
    };
    host.writeField=[](void* p,const FieldBinding*,const Value* out) {
        auto& s=*static_cast<Storage*>(p);s.operations.push_back("write");s.values=NumberValue<std::vector<int>>(*out);return true;
    };
    Detail::Instance<DataScript> script(host);
    script.Tick(0.f);
    Check(storage.values==std::vector<int>({9,8,11}),"Routed element changes must reach the host");
    Check(storage.operations==std::vector<std::string>({"bind","write","read","write","read","write"}),
          "A first write binds metadata without materializing or reading the declared default");
}
}

int main() {
    try {
        TestTriggersAndTimers();
        TestTimedTriggers();
        Observed io;
        Detail::Instance<WaitScript> first(io.Callbacks()), other(io.Callbacks());
        first.Tick(.3f); other.Tick(.2f);
        Check(Read(first,"step")==0 && Read(first,"updates")==0, "Wait(0) must yield the start call");
        first.Tick(.1f);
        Check(Read(first,"step")==1 && Read(first,"updates")==0, "Caller must await its child");
        first.Tick(.4f); first.Tick(0);
        Check(Read(first,"step")==1, "Only active game time may advance a timed wait");
        first.Tick(.6f);
        Check(Read(first,"step")==41 && Read(first,"updates")==1, "Start completes before Update");
        Check(Read(other,"step")==0, "Script instances must be independent");
        first.Tick(.1f);
        Check(Read(first,"updates")==2, "Start must not rerun");
        const auto* api = ModuleFor<WaitScript>();
        auto host = io.Callbacks();
        auto* cancelled = api->create(&host);
        api->tick(cancelled, .1f); api->tick(cancelled, .1f);
        api->destroy(cancelled);

        Detail::Instance<LoopScript> loop(io.Callbacks(), 2);
        loop.Tick(.1f);
        Check(Read(loop,"number")==12, "Loop budget must suspend after two elements");
        loop.Tick(.1f);
        Check(Read(loop,"number")==123, "Snapshot iteration, exact continuation and self-stop");
        Check(io.activations==std::vector<std::string>({"deleted","sibling",""}), "Missing target must skip one action");
        Check(io.log.size()==1 && io.log[0].starts_with("2 "), "Missing target requires an error diagnostic");

        Detail::Instance<StateScript> states(io.Callbacks());
        states.Tick(.1f);
        Check(Read(states,"entries")==6 && Read(states,"exits")==5 && Read(states,"updates")==1,
              "Immediate state changes must run until the state callback stops requesting changes");
        Check(io.log.size()==2 && io.log[1].starts_with("1 Possible state transition cycle:"), "Warn once per transition chain");
        Strategy<int()> strategy;
        strategy.Define("walk", [] { return [n=0]() mutable { return ++n; }; });
        strategy.Define("fly", [] { return [] { return 5; }; });
        strategy.Select("walk"); Check(strategy()==1 && strategy()==2,"Strategy keeps its own state");
        strategy.Select("fly"); Check(strategy()==5,"Strategy replacement");
        strategy.Select("walk"); Check(strategy()==1,"Replaced strategy starts fresh");
        Detail::Instance<BudgetScript> budget(io.Callbacks(),2);
        budget.Tick(.1f);Check(Read(budget,"steps")==2,"Start, Update and nested calls share a tick budget");
        budget.Tick(.1f);Check(Read(budget,"steps")==3,"Wait immediately following a yielded loop must suspend");
        budget.Tick(.1f);Check(Read(budget,"steps")==4,"Pending Update completes exactly once");
        {
            Detail::Instance<CancelScript> cancel(io.Callbacks());cancel.Tick(.1f);
            Check(CancelScript::live==2,"Both caller and child retain invocation locals");
        }
        Check(CancelScript::live==0&&CancelScript::completed==0,"Stopping cancels and destroys the whole call tree");
        Detail::Instance<TypedStrategyScript> typed(io.Callbacks());
        typed.Tick(0);typed.Tick(0);typed.Tick(0);
        Check(Read(typed,"result")==32,"A replaced strategy may finish its current call; new calls start fresh");
        Detail::Instance<NativeLifecycle> native(io.Callbacks());
        native.Tick(.4f);native.Tick(.6f);
        Check(Read(native,"start")==1&&Read(native,"updates")==2&&Read(native,"seconds")==1,
              "Unannotated synchronous C++ lifecycle functions receive the active time delta");
        Detail::Instance<DestroyingScript> destructors(io.Callbacks());
        destructors.Tick(0);
        Check(Read(destructors,"continued")==0&&Read(destructors,"created")==1&&Read(destructors,"destroyed")==1,
              "A self-deactivation inside a nested local destructor must end its caller safely");
        destructors.Tick(0);
        Check(Read(destructors,"continued")==0&&Read(destructors,"created")==2&&Read(destructors,"destroyed")==2,
              "Replacing a class must honor a self-deactivation before constructing the replacement");
        std::uint64_t located=0;
        Host badHost{};badHost.user=&located;
        badHost.diagnostic=[](void* p,std::uint64_t row,const char* text){
            if(std::string_view(text)=="Wait duration must be a finite, non-negative number.")*static_cast<std::uint64_t*>(p)=row;
        };
        Detail::Instance<BadWait> bad(badHost);bad.Tick(0);
        Check(located==987,"A runtime duration error must report the originating Wait row");
        TestCompoundValues();
        std::cout << "Native script runtime passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
