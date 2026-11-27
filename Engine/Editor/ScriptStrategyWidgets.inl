    // A strategy is a typed slot for a class method. Its native C++ declaration
    // and calls carry the whole selection, including after a source-only import.
    struct StrategyMethod {
        std::string qualified, signature;
    };
    static std::vector<StrategyMethod> StrategyMethods(const Doc& doc) {
        std::vector<StrategyMethod> methods;
        for (const auto& function : doc.Nodes()) {
            if (function.kind != Kind::Function) continue;
            auto* klass = doc.Get(function.parent);
            if (!klass || klass->kind != Kind::Class || klass->parent) continue;
            StrategyMethod method;
            method.qualified = klass->name + "::" + function.name;
            std::vector<std::string> types{function.type.empty() ? "void" : function.type};
            for (const auto& parameter : Scripting::FunctionParameters(function.args))
                types.push_back(parameter.type);
            method.signature = "StrategySlot<" + Join(types) + ">";
            methods.push_back(std::move(method));
        }
        return methods;
    }
    static bool IsStrategy(const Node& n) {
        return n.kind == Kind::Variable && n.type.starts_with("StrategySlot<");
    }
    enum class StrategyAction { None, Replace, Invoke };
    struct StrategyLine {
        StrategyAction action{};
        std::string slot, argument, output;
        bool inClass{};
    };
    static StrategyLine ParseStrategy(const Node& n) {
        if (n.kind != Kind::Custom) return {};
        std::string_view line(n.value);
        StrategyLine result;
        constexpr std::string_view outputPrefix = "static_cast<void>(", assignment = " = co_await ";
        if (line.starts_with(outputPrefix) && line.ends_with("));")) {
            line.remove_prefix(outputPrefix.size());
            auto equals = line.find(assignment);
            if (equals == line.npos) return {};
            result.output = line.substr(0,equals);
            line.remove_prefix(equals+3);
            line.remove_suffix(1);
        }
        bool awaited = line.starts_with("co_await ");
        if (awaited) line.remove_prefix(9);
        const bool previousOwner=line.starts_with("(riceOwner).");
        if (previousOwner || line.starts_with("(*riceOwner).")) {
            result.inClass=true;line.remove_prefix(previousOwner?12:13);
        }
        const std::string_view replace = previousOwner ? ".Replace(MakeStrategy(riceOwner, &"
            : result.inClass ? ".Replace(MakeStrategy(*riceOwner, &" : ".Replace(MakeStrategy(*this, &";
        constexpr std::string_view invoke = ".Invoke(";
        auto position = line.find(replace);
        if (!awaited && position != line.npos && line.ends_with("));")) {
            auto method = position + replace.size();
            result.action=StrategyAction::Replace;
            result.slot=line.substr(0,position);
            result.argument=line.substr(method,line.size()-method-3);
            return result;
        }
        if (!awaited || (result.output.empty()?!line.ends_with(");"):!line.ends_with("))"))) return {};
        position = line.find(invoke);
        if (position == line.npos) return {};
        auto parameters = position + invoke.size();
        result.action=StrategyAction::Invoke;
        result.slot=line.substr(0,position);
        result.argument=line.substr(parameters,line.size()-parameters-2);
        return result;
    }
    static std::string StrategySource(const StrategyLine& line) {
        auto slot=(line.inClass?"(*riceOwner).":"")+line.slot;
        if (line.action == StrategyAction::Replace)
            return slot + ".Replace(MakeStrategy("+(line.inClass?"*riceOwner":"*this")+", &" + line.argument + "));";
        auto call="co_await " + slot + ".Invoke(" + line.argument + ")";
        return line.output.empty()?call+";":"static_cast<void>("+line.output+" = "+call+");";
    }
    static std::string StrategyCaption(const Node& n) {
        if (IsStrategy(n)) return "Strategy   " + n.name;
        auto line = ParseStrategy(n);
        if (line.action == StrategyAction::Replace) return "Change Strategy   " + line.slot + " / " + line.argument;
        if (line.action == StrategyAction::Invoke) return "Call Strategy   " + line.slot + "(" + line.argument + ")"+(line.output.empty()?"":" → "+line.output);
        return {};
    }
    static std::string StrategyKey(std::string type) {
        std::erase_if(type, [](unsigned char c){ return std::isspace(c); });
        return type;
    }
    bool StrategyFields(Workspace& w, Entry& e, View& view, Targets& targets, Node& n, bool& edited) {
        if (IsStrategy(n)) {
            ImGui::Text("Signature: %s",n.type.c_str());
            edited=TextField("Name",n.name);
            return true;
        }
        auto line=ParseStrategy(n);
        if (line.action==StrategyAction::None) return false;
        ImGui::Text("Strategy: %s",line.slot.c_str());
        if (line.action==StrategyAction::Replace && ImGui::BeginCombo("Behaviour",line.argument.c_str())) {
            for(const auto& slot:view.document.Nodes()) {
                if (!IsStrategy(slot)||slot.name!=line.slot) continue;
                for(const auto& method:StrategyMethods(view.document)) {
                    if (StrategyKey(slot.type)!=StrategyKey(method.signature)) continue;
                    if (ImGui::Selectable(method.qualified.c_str(),method.qualified==line.argument)) {
                        line.argument=method.qualified;
                        edited=true;
                    }
                }
            }
            ImGui::EndCombo();
        }
        if (line.action==StrategyAction::Invoke) {
            auto slot=std::find_if(view.document.Nodes().begin(),view.document.Nodes().end(),[&](const auto& item){
                return IsStrategy(item)&&item.name==line.slot&&view.document.CanSee(item.id,n.id);
            });
            std::vector<std::string> signature;
            if(slot!=view.document.Nodes().end())signature=Scripting::SplitArguments(Between(slot->type,'<','>'));
            if(signature.empty())edited=ArgumentList(w,e,view,targets,n.id,"Arguments",line.argument);
            else {
                std::vector<Scripting::ScriptParameter> parameters;
                for(size_t i=1;i<signature.size();++i){
                    Scripting::ScriptParameter parameter;
                    parameter.declaration=parameter.type=signature[i];
                    parameters.push_back(std::move(parameter));
                }
                edited=ArgumentFields(w,e,view,targets,n.id,"Arguments",parameters,line.argument);
            }
            if(edited)line.argument=view.document.NativeExpression(std::move(line.argument),n.parent);
            if(!signature.empty()&&signature.front()!="void"&&ImGui::BeginCombo("Output",line.output.empty()?"Discard":line.output.c_str())){
                if(ImGui::Selectable("Discard",line.output.empty())) {line.output.clear();edited=true;}
                for(auto& variable:view.document.Nodes()) {
                    if(variable.kind!=Kind::Variable||StrategyKey(variable.type)!=StrategyKey(signature.front())||
                       !view.document.CanSee(variable.id,n.parent)) continue;
                    if(ImGui::Selectable(variable.name.c_str(),line.output==variable.name)){line.output=variable.name;edited=true;}
                }
                ImGui::EndCombo();
            }
        }
        if (edited) n.value=StrategySource(line);
        return true;
    }
    void StrategyMenu(Entry& e,View& view,Id owner) {
        if (!ImGui::BeginMenu("Strategy")) return;
        auto& doc=view.document;
        auto methods=StrategyMethods(doc);
        std::string newSlotType;
        StrategyLine newAction;
        if (ImGui::BeginMenu("New Slot")) {
            if (methods.empty()) ImGui::TextDisabled("Add a class with a function first.");
            for (const auto& method:methods)
                if (ImGui::MenuItem(method.qualified.c_str())) newSlotType=method.signature;
            ImGui::EndMenu();
        }
        bool inCoroutine=false,synchronous=false;
        bool inClass=false;
        for (auto* scope=doc.Get(owner);scope;scope=doc.Get(scope->parent)) {
            if (scope->kind==Kind::Class) inClass=true;
            if (scope->kind==Kind::Start||scope->kind==Kind::Update||scope->kind==Kind::Function) inCoroutine=true;
            if (scope->kind==Kind::OnEnter||scope->kind==Kind::OnUpdate||scope->kind==Kind::OnExit||
                scope->kind==Kind::OnCreate||scope->kind==Kind::OnDestroy) synchronous=true;
        }
        for (const auto& slot:doc.Nodes()) {
            if (!IsStrategy(slot)||!doc.CanSee(slot.id,owner)||!ImGui::BeginMenu(slot.name.c_str())) continue;
            if (ImGui::BeginMenu("Change Strategy")) {
                for(const auto& method:methods)
                    if(StrategyKey(slot.type)==StrategyKey(method.signature)&&ImGui::MenuItem(method.qualified.c_str()))
                        newAction={StrategyAction::Replace,slot.name,method.qualified};
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Call Strategy",nullptr,false,inCoroutine&&!synchronous)) {
                auto types=Scripting::SplitArguments(Between(slot.type,'<','>'));
                if (!types.empty()) types.erase(types.begin());
                for (auto& type:types) type=DefaultValue(type);
                newAction={StrategyAction::Invoke,slot.name,Join(types)};
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
        if (newSlotType.empty()&&newAction.action==StrategyAction::None) return;
        const bool declaration=!newSlotType.empty();
        Id id=doc.Add(declaration?0:owner,declaration?Kind::Variable:Kind::Custom);
        if (auto* item=doc.Get(id)) {
            auto node=*item;
            if (declaration) {node.type=newSlotType;node.value="{}";}
            else {newAction.inClass=inClass;node.value=StrategySource(newAction);}
            if (doc.Edit(id,node)) {e.Changed(view,id);view.Reveal(id);}
        }
    }
