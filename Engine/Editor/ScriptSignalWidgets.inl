    static std::string SignalName(const Doc& doc,const Node& signal){
        std::string label=signal.name;
        for(auto* p=doc.Get(signal.parent);p;p=doc.Get(p->parent))label=Title(*p)+" / "+label;
        return label;
    }
    std::string SignalPreview(Workspace& w,const View& view,const Node& row,Kind kind){
        if(row.value.empty())return kind==Kind::Function?"Choose function":"Choose event";
        std::string_view key=row.value;
        const auto colon=key.find(':');
        if(colon==key.npos)return "Missing selection";
        key.remove_prefix(colon+1);
        const Doc* doc=&view.document;
        std::string script;
        if(auto hash=key.rfind('#');hash!=key.npos){
            script=key.substr(0,hash);
            if(script.empty())return "Missing script";
            doc=DiskDocument(Scripting::ScriptSourcePath(w.project->root,Path(script)));
            key.remove_prefix(hash+1);
        }
        Id id{};
        auto result=std::from_chars(key.data(),key.data()+key.size(),id);
        const auto* node=doc?doc->Get(id):nullptr;
        if(result.ec!=std::errc{}||result.ptr!=key.data()+key.size()||!node||node->kind!=kind||
           Scripting::SignalKey(*node,script)!=row.value||(script.empty()?!doc->CanSee(id,row.parent):!node->isPublic))
            return kind==Kind::Function?"Missing function":"Missing event";
        return (script.empty()?"":script+" / ")+SignalName(*doc,*node);
    }
    bool SignalChoices(Workspace& w,const Entry& e,const View& view,Node& row,Kind kind){
        bool edited=false;
        auto eligible=[&](const Doc& doc,const Node& node,bool external){
            return node.kind==kind&&(external?node.isPublic:doc.CanSee(node.id,row.parent));
        };
        auto list=[&](const Doc& doc,const std::string& script){
            bool any=false;
            for(const auto& node:doc.Nodes()){
                if(!eligible(doc,node,!script.empty()))continue;
                any=true;
                auto key=Scripting::SignalKey(node,script);
                ImGui::PushID(key.c_str());
                if(ImGui::MenuItem(SignalName(doc,node).c_str(),nullptr,row.value==key)){
                    if(row.kind!=Kind::EmitEvent)row.type=Caption(kind);
                    row.value=std::move(key);edited=true;
                }
                ImGui::PopID();
            }
            if(!any)ImGui::TextDisabled("No visible declarations");
        };
        if(ImGui::BeginMenu("Here")){
            list(view.document,{});
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("Elsewhere")){
            bool any=false;
            for(const auto& path:Scripting::ScriptFiles(w.project->root)){
                if(path==e.path)continue;
                const auto* doc=DiskDocument(path);
                if(!doc||std::none_of(doc->Nodes().begin(),doc->Nodes().end(),[&](const auto& n){return eligible(*doc,n,true);}))continue;
                any=true;
                auto key=Scripting::ScriptKey(w.project->root,path);
                if(ImGui::BeginMenu(key.c_str())){
                    list(*doc,key);
                    ImGui::EndMenu();
                }
            }
            if(!any)ImGui::TextDisabled("No public declarations");
            ImGui::EndMenu();
        }
        return edited;
    }
    bool SignalPicker(Workspace& w,const Entry& e,const View& view,Node& row,Kind kind){
        const auto preview=SignalPreview(w,view,row,kind);
        if(!ImGui::BeginCombo(Caption(kind),preview.c_str()))return false;
        const bool edited=SignalChoices(w,e,view,row,kind);
        ImGui::EndCombo();
        return edited;
    }
    static bool TimeSource(std::string_view type){return type=="Game Seconds"||type=="Seconds";}
    bool TriggerFields(Workspace& w,Entry& e,View& view,Targets& targets,Node& row){
        const bool inlineWait=row.kind==Kind::Timer;
        const char* inlineHint="This inline wait pauses its caller each time execution reaches it.";
        bool edited=false;
        ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("When");ImGui::SameLine();
        ImGui::BeginDisabled(inlineWait);
        if(ImGui::SmallButton(row.args=="every"?"every###frequency":"single###frequency")){
            row.args=row.args=="every"?"once":"every";edited=true;
        }
        ImGui::EndDisabled();
        if(inlineWait&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",inlineHint);
        else ImGui::SetItemTooltip("Single handles the first occurrence. Every keeps listening.");
        ImGui::SameLine();
        std::string preview=row.type;
        if(row.type=="Function"||row.type=="Event")
            preview+=" / "+SignalPreview(w,view,row,row.type=="Event"?Kind::Event:Kind::Function);
        if(row.type=="Component Change")preview+=" / "+(row.value.empty()?"Any component":ComponentLabel(row.value));
        const float available=ImGui::GetContentRegionAvail().x;
        const float checkWidth=ImGui::GetFrameHeight()+ImGui::CalcTextSize("block others").x+2*ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(std::max(ImGui::GetFrameHeight()*3.f,available-checkWidth));
        if(ImGui::BeginCombo("##source",preview.c_str())){
            for(const char* clock:{"Game Seconds","Seconds"}){
                if(ImGui::MenuItem(clock,nullptr,row.type==clock)&&row.type!=clock){
                    if(!TimeSource(row.type))row.value="1";
                    row.type=clock;edited=true;
                }
            }
            for(auto kind:{Kind::Function,Kind::Event}){
                if(ImGui::BeginMenu(kind==Kind::Function?"Functions":"Event")){
                    edited|=SignalChoices(w,e,view,row,kind);
                    ImGui::EndMenu();
                }
            }
            if(!inlineWait)for(const char* collision:{"Collision Start","Collision End"}){
                if(ImGui::MenuItem(collision,nullptr,row.type==collision)){
                    row.type=collision;row.value.clear();edited=true;
                }
            }
            if(!inlineWait&&ImGui::BeginMenu("Component Change")){
                auto component=[&](const std::string& name,const std::string& label){
                    if(ImGui::MenuItem(label.c_str(),nullptr,row.type=="Component Change"&&row.value==name)){
                        row.type="Component Change";row.value=name;edited=true;
                    }
                };
                component({},"Any component");
                component("Transform","Transform");
                for(auto& type:w.registry.types)component(type.name,ComponentLabel(type.name));
                ImGui::EndMenu();
            }
            ImGui::EndCombo();
        }
        if(available>=checkWidth+ImGui::GetFrameHeight()*3.f)ImGui::SameLine();
        ImGui::BeginDisabled(inlineWait);
        edited|=ImGui::Checkbox("block others",&row.blockOthers);
        ImGui::EndDisabled();
        if(inlineWait&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",inlineHint);
        else ImGui::SetItemTooltip("Pause the following actions until this trigger finishes.");
        if(TimeSource(row.type)){
            ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("Duration");ImGui::SameLine();
            edited|=Expression(w,e,view,targets,row.id,"##duration",row.value,"float",0.f,"Seconds");
        }
        return edited;
    }
    static std::set<Id> UpdateBlockers(Workspace& w,const Entry& e,const Doc& doc,Id owner){
        std::set<Id> result;
        auto* scope=doc.Get(ScopeOf(doc,owner));
        if(!scope||scope->kind!=Kind::Update)return result;
        const auto* script=w.editor.playing&&e.attached?Owner(w,e):nullptr;
        for(const auto& row:doc.Nodes()){
            if(row.kind!=Kind::Trigger)continue;
            if(w.editor.playing?(!script||!script->TriggerBlocking(row.id)):!row.blockOthers)continue;
            auto* branch=&row;
            while(branch->parent!=owner){
                branch=doc.Get(branch->parent);
                if(!branch||Scope(branch->kind))break;
            }
            if(branch&&branch->parent==owner&&(branch==&row||!Scope(branch->kind)))result.insert(branch->id);
        }
        return result;
    }
