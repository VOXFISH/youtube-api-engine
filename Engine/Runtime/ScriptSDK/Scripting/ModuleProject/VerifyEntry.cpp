#include "ScriptRuntime.h"

// Make the linker check the callable module contract without executing any
// user code during Save. Initializers first run in a Play session.
extern "C" const Rice::Scripting::ModuleApi* RiceGetScriptApi();
extern "C" __declspec(dllexport) const Rice::Scripting::ModuleApi* RiceVerifyScriptEntry() {
    return RiceGetScriptApi();
}
