#include "Streaming/ReplacementLifecycle.h"
#include <iostream>
#include <stdexcept>
using namespace RiceController::Streaming;
namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
Slot Make(const std::string& id) {
    Slot s; s.slotId=id; s.name=id;
    s.connection={"broadcast-"+id,"chat-"+id,"account","ingest-"+id}; return s;
}
SlotRegistry Model(bool shared = false) {
    SlotRegistry slots;
    slots.Add(Make("a")); slots.ConfigureInstance("a","old","old-type");
    if (shared) { slots.Add(Make("b")); slots.ConfigureInstance("b","old","old-type"); slots.SetMain("b"); }
    slots.BindEntry("old","old-run"); slots.MarkConnecting("a"); slots.MarkLive("a");
    if (shared) { slots.MarkConnecting("b"); slots.MarkLive("b"); }
    return slots;
}
}
int main() {
    try {
        int saves=0, stops=0;
        std::promise<void> promise;
        bool current=true;
        ReplacementLifecycle lifecycle([&](const auto& instance) {
            Check(instance.runtimeId=="old-run","Save identifies exact old run"); ++saves; return promise.get_future();
        },[&](const auto& instance) {
            Check(instance.runtimeId=="old-run","Stop identifies exact old run"); ++stops;
        },[&](const auto&) { return current; });
        auto slots=Model(true);
        lifecycle.Commit(slots,"a",{"new","new-run"},"new-type");
        Check(saves==0 && stops==0,"Shared old game remains alive without retirement");
        lifecycle.Commit(slots,"b",{"new","new-run"},"new-type");
        Check(saves==1 && stops==0 && lifecycle.Pending()==1,"Last replaced slot starts save but does not stop");
        lifecycle.Poll(slots); Check(stops==0,"Pending save cannot stop game");
        promise.set_exception(std::make_exception_ptr(std::runtime_error("Disk full")));
        lifecycle.Poll(slots); Check(stops==0 && lifecycle.Error("old-run")=="Disk full","Save failure preserves old process");
        promise=std::promise<void>{}; lifecycle.Retry("old-run"); promise.set_value();
        lifecycle.Poll(slots); lifecycle.Poll(slots);
        Check(stops==1 && lifecycle.Pending()==0,"Successful retry stops exactly once");

        slots=Model(); promise=std::promise<void>{};
        lifecycle.Commit(slots,"a",{"new","new-run"},"new-type");
        current=false; promise.set_value(); lifecycle.Poll(slots);
        Check(stops==1 && lifecycle.Pending()==0,"Stale save acknowledgement cannot stop restarted run");

        current=true; slots=Model(true); promise=std::promise<void>{};
        const auto before=slots.Serialize(); const int previousSaves=saves;
        bool failed=false;
        try { lifecycle.Commit(slots,"b",{"new","new-run"},"new-type"); } catch(const std::runtime_error&) { failed=true; }
        Check(failed && saves==previousSaves && slots.Serialize()==before,"Rejected main-slot departure never starts retirement");

        slots=Model(); promise=std::promise<void>{};
        lifecycle.Commit(slots,"a",{"new","new-run"},"new-type");
        slots.Add(Make("c")); slots.ConfigureInstance("c","old","old-type"); slots.BindEntry("old","old-run");
        slots.MarkConnecting("c"); promise.set_value(); lifecycle.Poll(slots);
        Check(stops==1,"Another slot connecting to old run prevents its termination");
        std::cout<<"Replacement lifecycle tests passed\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
