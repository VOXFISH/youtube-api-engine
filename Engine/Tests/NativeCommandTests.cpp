#include "Streaming/NativeCommand.h"

#include <iostream>
#include <stdexcept>

using namespace Rice::Streaming;
namespace {
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
#define MOVEMENT_ARGUMENTS(X) X(int, direction), X(int, steps)
int observedDirection = 0, observedSteps = 0;
void Move(RICE_YOUTUBE_DECLARE_PARAMETERS(MOVEMENT_ARGUMENTS)) {
    observedDirection = direction; observedSteps = steps;
}
struct Target {
    std::string greeting;
    void Greet(const std::string& username, bool show = true) { greeting = show ? username : "hidden"; }
};
}
int main() {
    try {
        CommandRegistry registry;
        registry.Add(MakeNativeBinding<&Move>("move-left", "Move", "!회피 {direction : (좌, -1), (우, 1)} {steps}칸",
            RICE_YOUTUBE_PARAMETERS(MOVEMENT_ARGUMENTS)));
        auto result = registry.Dispatch("!회피 좌 3칸");
        Check(result.items.size() == 1 && result.items[0].status == CommandDispatchStatus::Executed &&
            observedDirection == -1 && observedSteps == 3, "Native free function binds inferred argument types and shared X-macro names");
        Target target;
        Target* live = &target;
        registry.Add(MakeNativeMemberBinding<&Target::Greet>("greet", "Target.Greet", "!hi {username}",
            [&] { return live; }, std::tuple{ Parameter<std::string>("username"), Parameter<bool>("show").Default(true) }));
        result = registry.Dispatch("!hi \"Jane Doe\"");
        Check(result.items[0].status == CommandDispatchStatus::Executed && target.greeting == "Jane Doe", "Native const-reference argument and explicitly registered default");
        live = nullptr;
        result = registry.Dispatch("!hi ignored");
        Check(result.items[0].status == CommandDispatchStatus::Unavailable, "Member resolver rechecks lifetime at invocation");
        bool missingRejected = false;
        try {
            MakeNativeMemberBinding<&Target::Greet>("missing", "Target.Greet", "!hi {username}",
                [&] { return live; }, std::tuple{ Parameter<std::string>("username"), Parameter<bool>("show") });
        } catch (const std::invalid_argument&) { missingRejected = true; }
        Check(missingRejected, "C++ source default is never incorrectly inferred through function pointer");
        bool narrowedRejected = false;
        try {
            MakeNativeBinding<&Move>("narrow", "Move", "!move {direction}",
                std::tuple{ Parameter<int>("direction"), Parameter<int>("steps").Default(3.5) });
        } catch (const std::invalid_argument&) { narrowedRejected = true; }
        Check(narrowedRejected, "Registration default preserves source value and rejects lossy conversion");
        std::cout << "Native command tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Native command test failed: " << error.what() << '\n';
        return 1;
    }
}
