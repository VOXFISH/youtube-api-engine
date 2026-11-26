    static bool Decl(Kind k){return k==Kind::Variable||k==Kind::Function||k==Kind::Class;}
    static bool Branch(Kind k){return k==Kind::Condition||k==Kind::ElseIf||k==Kind::Else;}
    static bool Scope(Kind k){
        return k==Kind::Start||k==Kind::Update||k==Kind::Function||k==Kind::Class||k==Kind::OnCreate||
               k==Kind::OnDestroy||k==Kind::OnEnter||k==Kind::OnUpdate||k==Kind::OnExit;
    }
    static bool HasBody(Kind k){return Scope(k)||Branch(k)||k==Kind::Loop||k==Kind::State;}
    static Id ScopeOf(const Doc& doc,Id id){
        for(auto* n=doc.Get(id);n;n=doc.Get(n->parent))if(Scope(n->kind))return n->id;
        return 0;
    }
    static const char* Caption(Kind k){
        switch(k){
            case Kind::Variable:return "Variable";case Kind::Function:return "Function";
            case Kind::Class:return "Class";case Kind::Start:return "Start";case Kind::Update:return "Update";
            case Kind::Wait:return "Wait";case Kind::Move:return "Move";case Kind::Loop:return "loop";case Kind::Change:return "Change Variable";
            case Kind::Call:return "Call Function";case Kind::Activate:return "Activate";case Kind::NewClass:return "New Class";
            case Kind::Condition:return "if";case Kind::ElseIf:return "else if";case Kind::Else:return "else";
            case Kind::EndFunction:return "End Function";case Kind::EndLoop:return "End Loop";
            case Kind::OnCreate:return "On Create";case Kind::OnDestroy:return "On Destroy";
            case Kind::State:return "State";case Kind::OnEnter:return "On Enter";case Kind::OnUpdate:return "On Update";
            case Kind::OnExit:return "On Exit";case Kind::SetState:return "Set State";case Kind::ChangeState:return "Change State";
            default:return "Custom Code";
        }
    }
    static std::string Title(const Node& n){
        if(auto caption=StrategyCaption(n);!caption.empty())return caption;
        if(Decl(n.kind))return n.name.empty()?Caption(n.kind):n.name;
        std::string label=Caption(n.kind);
        switch(n.kind){
            case Kind::Change:return label+"   "+n.name+" "+n.args+" "+n.value;
            case Kind::Move:return label+(n.value.empty()?"":" "+n.value)+" to ("+
                n.moveCoordinates[0]+", "+n.moveCoordinates[1]+", "+n.moveCoordinates[2]+") per "+n.args;
            case Kind::Wait:return label+"   "+n.value+" s";
            case Kind::Loop:return n.value.empty()?label:label+"   "+n.value;
            case Kind::Condition:case Kind::ElseIf:return label+"   "+n.value;
            case Kind::Call:return label+"   "+n.name+"("+n.args+")";
            case Kind::NewClass:return n.type+"   "+n.name;
            case Kind::SetState:case Kind::ChangeState:case Kind::State:return label+"   "+n.name+" / "+n.value;
            default:return label;
        }
    }
    struct TextBuffer {std::string model,text;};
    std::map<ImGuiID,TextBuffer> buffers;
    TextBuffer& Buffer(const char* label,const std::string& value){
        auto key=ImGui::GetID(label);
        auto [it,added]=buffers.try_emplace(key,TextBuffer{value,value});
        if(!added&&it->second.model!=value)it->second={value,value};
        return it->second;
    }
    bool TextField(const char* label,std::string& value,bool multi=false,const char* hint=nullptr){
        auto& buf=Buffer(label,value);
        const bool enter=multi?ImGui::InputTextMultiline(label,&buf.text,{-1,ImGui::GetTextLineHeight()*5}):
                               ImGui::InputTextWithHint(label,hint,&buf.text,ImGuiInputTextFlags_EnterReturnsTrue);
        if(((enter&&!multi)||ImGui::IsItemDeactivatedAfterEdit())&&value!=buf.text){
            value=buf.model=buf.text;return true;
        }
        return false;
    }
    static bool Match(std::string text,std::string filter){
        auto fold=[](unsigned char c){return static_cast<char>(std::tolower(c));};
        std::transform(text.begin(),text.end(),text.begin(),fold);
        std::transform(filter.begin(),filter.end(),filter.begin(),fold);
        return text.find(filter)!=std::string::npos;
    }
    static std::string Trim(std::string value){
        auto first=value.find_first_not_of(" \t\r\n"),last=value.find_last_not_of(" \t\r\n");
        return first==std::string::npos?std::string{}:value.substr(first,last-first+1);
    }
    static std::vector<std::string> Arguments(std::string_view value){
        std::vector<std::string> result;
        std::size_t start=0;int depth=0;char quote=0;
        for(std::size_t i=0;i<value.size();++i){
            char c=value[i];
            if(quote){if(c=='\\')++i;else if(c==quote)quote=0;continue;}
            if(c=='\''||c=='"')quote=c;
            else if(c=='{'||c=='('||c=='['||c=='<')++depth;
            else if(c=='}'||c==')'||c==']'||c=='>')--depth;
            else if(c==','&&!depth){result.push_back(Trim(std::string(value.substr(start,i-start))));start=i+1;}
        }
        if(start<value.size())result.push_back(Trim(std::string(value.substr(start))));
        return result;
    }
    static std::string Join(const std::vector<std::string>& values){
        std::string out;
        for(auto& item:values){if(!out.empty())out+=", ";out+=item;}
        return out;
    }
    static std::string Between(const std::string& value,char left,char right){
        auto a=value.find(left),b=value.rfind(right);
        return a!=std::string::npos&&b>a?value.substr(a+1,b-a-1):std::string{};
    }
    static std::string DefaultValue(const std::string& type){
        if(type=="bool")return "true";
        if(type=="int"||type=="float")return "0";
        return "{}";
    }
    static bool Toggle(const char* key,std::string& value,
                       std::initializer_list<std::pair<const char*,const char*>> choices){
        ImGui::PushID(key);
        auto it=std::find_if(choices.begin(),choices.end(),[&](auto option){return option.first==value;});
        bool hit=ImGui::SmallButton(value.c_str());
        if(it!=choices.end())ImGui::SetItemTooltip("%s",it->second);
        if(hit){if(it==choices.end()||++it==choices.end())it=choices.begin();value=it->first;}
        ImGui::PopID();
        return hit;
    }
    bool TypePicker(Workspace& w,Entry& e,View& view,const char* label,std::string& type,bool allowVoid=false,ChildSymbol* selectedChild=nullptr){
        ImGui::PushID(label);
        std::string printable=type=="Vec2"?"vector 2":type=="Vec3"?"vector 3":
                 type.starts_with("std::vector<")?"list":type.starts_with("std::unordered_map<")?"dictionary":type;
        if(ImGui::SmallButton((printable+"###type").c_str())){view.search.clear();ImGui::OpenPopup("Types");}
        bool changed=false;
        if(ImGui::BeginPopup("Types")){
            ImGui::SetNextItemWidth(220);ImGui::InputTextWithHint("##query","Search types",&view.search);
            unsigned matches=0;
            auto choose=[&](const std::string& name,const std::string& spelling){
                if(!Match(name,view.search))return;
                ++matches;
                if(ImGui::Selectable(name.c_str(),type==spelling)){type=spelling;changed=true;}
            };
            ImGui::SeparatorText("Basic");
            if(allowVoid)choose("void","void");
            choose("int","int");choose("float","float");choose("bool","bool");choose("vector 2","Vec2");choose("vector 3","Vec3");
            ImGui::SeparatorText("Container");
            choose("list","std::vector<int>");choose("dictionary","std::unordered_map<int, int>");
            ImGui::SeparatorText("Classes / Here");
            for(auto& n:view.document.Nodes())if(n.kind==Kind::Class)choose(n.name,n.name);
            bool flat=!view.search.empty();
            if(flat||ImGui::BeginMenu("Classes / Elsewhere...")){
                for(const auto& path:Scripting::ScriptFiles(w.project->root)){
                    if(path==e.path)continue;
                    auto label=Utf8(path.lexically_relative(w.project->root));
                    auto* other=DiskDocument(path);
                    if(!other||std::none_of(other->Nodes().begin(),other->Nodes().end(),[](auto& n){return n.kind==Kind::Class;}))continue;
                    if(flat||ImGui::BeginMenu(label.c_str())){
                        for(auto& n:other->Nodes())if(n.kind==Kind::Class&&Match(n.name,view.search)){
                            ++matches;
                            if(ImGui::Selectable((flat?label+" / "+n.name:n.name).c_str())){
                                if(view.document.ImportClass(*other,n.id)){
                                    type=n.name;changed=true;
                                    for(auto& local:view.document.Nodes())if(local.kind==Kind::Class&&local.name==type)e.Changed(view,local.id);
                                }else e.error="A declaration with this name already exists.";
                            }
                            ImGui::SetItemTooltip("Import an independent copy of this C++ class.");
                        }
                        if(!flat)ImGui::EndMenu();
                    }
                }
                if(!flat)ImGui::EndMenu();
            }
            if(flat||ImGui::BeginMenu("Children")){
                for(auto& child:ChildSymbols(w,e,true)){
                    auto name=child.path+" / "+ComponentLabel(child.component)+" / "+child.name+(child.kind==Kind::Function?"()":"");
                    if(!Match(name,view.search)||(!allowVoid&&child.type=="void"))continue;
                    ++matches;
                    if(ImGui::Selectable(name.c_str(),type==child.type)){
                        if(child.kind==Kind::Class)if(auto* source=DiskDocument(child.file);source&&view.document.ImportClass(*source,child.symbol))
                            for(auto& cls:view.document.Nodes())if(cls.kind==Kind::Class&&cls.name==child.name)e.Changed(view,cls.id);
                        type=child.type;changed=true;
                        if(selectedChild&&child.kind==Kind::Variable) *selectedChild=child;
                    }
                }
                if(!flat)ImGui::EndMenu();
            }
            if(!matches)ImGui::TextDisabled("no result...");
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return changed;
    }
    bool Initializer(Workspace& w,Entry& e,View& view,Node& n){
        if(n.value.find("ReadChild<")!=std::string::npos)return TextField("Initial value",n.value);
        bool list=n.type.starts_with("std::vector<"),dict=n.type.starts_with("std::unordered_map<");
        if(!list&&!dict){
            if(n.type=="bool"){
                if(ImGui::SmallButton((n.value+"###initial").c_str())){n.value=n.value=="true"?"false":"true";return true;}
                return false;
            }
            if(n.type=="Vec2"||n.type=="Vec3"){
                float values[3]{};
                auto args=Arguments(Between(n.value,'{','}'));
                for(size_t i=0;i<std::min<size_t>(3,args.size());++i)std::from_chars(args[i].data(),args[i].data()+args[i].size(),values[i]);
                bool hit=n.type=="Vec2"?ImGui::DragFloat2("Initial value",values,.1f):ImGui::DragFloat3("Initial value",values,.1f);
                if(hit){
                    std::ostringstream out;
                    out<<std::showpoint<<std::setprecision(std::numeric_limits<float>::max_digits10)
                       <<n.type<<"{"<<values[0]<<"f, "<<values[1]<<'f';
                    if(n.type=="Vec3")out<<", "<<values[2]<<'f';out<<"}";
                    n.value=out.str();
                }
                return hit;
            }
            ImGui::SetNextItemWidth(-1);return TextField("##initial",n.value);
        }
        bool edited=false;
        auto types=Arguments(Between(n.type,'<','>'));
        types.resize(dict?2:1,"int");
        ImGui::TextUnformatted(dict?"Key":"Element type");ImGui::SameLine();
        edited|=TypePicker(w,e,view,"elementType",types[0]);
        if(dict){ImGui::SameLine();ImGui::TextUnformatted("Value");ImGui::SameLine();edited|=TypePicker(w,e,view,"mappedType",types[1]);}
        if(edited)n.type=(dict?"std::unordered_map<":"std::vector<")+Join(types)+">";
        auto items=Arguments(Between(n.value,'{','}'));int erase=-1;
        ImGui::SameLine();
        if(ImGui::SmallButton("+##value")){
            items.push_back(dict?"{"+DefaultValue(types[0])+", "+DefaultValue(types[1])+"}":DefaultValue(types[0]));edited=true;
        }
        ImGui::SameLine();ImGui::BeginDisabled(items.empty());
        if(ImGui::SmallButton("-##last")){items.pop_back();edited=true;}
        ImGui::EndDisabled();
        for(size_t i=0;i<items.size();++i){
            ImGui::PushID(static_cast<int>(i));ImGui::Text("[%zu]",i);ImGui::SameLine();
            auto pair=dict?Arguments(Between(items[i],'{','}')):std::vector<std::string>{};
            if(dict){
                pair.resize(2);ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*.3f);
                bool edit=TextField("##key",pair[0]);ImGui::SetItemTooltip("Key");ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*.7f);
                edit|=TextField("##mapped",pair[1]);ImGui::SetItemTooltip("Value");
                if(edit){items[i]="{"+Join(pair)+"}";edited=true;}
            }else{
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*.8f);
                edited|=TextField("##value",items[i]);
            }
            ImGui::SameLine();if(ImGui::SmallButton("-"))erase=static_cast<int>(i);
            ImGui::PopID();
        }
        if(erase>=0){items.erase(items.begin()+erase);edited=true;}
        if(edited)n.value="{"+Join(items)+"}";
        return edited;
    }
    bool VariableFields(Workspace& w,Entry& e,View& view,Targets& targets,Node& n){
        bool edited=false,shared=HasProtected(w,e,n);
        ImGui::TextColored(shared?ImVec4(.35f,.88f,.43f,1):ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
                           "%s",shared?"protected":"private");
        ImGui::SetItemTooltip("%s",shared?"Readable by an ancestor script function through a saved reference.":"Available only within this script.");
        ImGui::SameLine();
        edited|=ImGui::Checkbox("##ricetoggle",&n.riceCon);ImGui::SameLine(0,3);
        if(ImGui::SmallButton("RiceCon"))ImGui::OpenPopup("RiceCon info");
        if(ImGui::BeginPopup("RiceCon info")){ImGui::TextUnformatted("RiceCon lets you change this variable through YouTube comments.");ImGui::EndPopup();}
        ImGui::SameLine();ImGui::BeginDisabled(!n.riceCon);
        if(ImGui::ArrowButton("##rice",ImGuiDir_Down))ImGui::OpenPopup("RiceCon settings");
        ImGui::EndDisabled();
        if(ImGui::BeginPopup("RiceCon settings")){
            ImGui::TextUnformatted("Scope");ImGui::SameLine();
            edited|=Toggle("scope",n.riceScope,{{"build","Scoped to this build."},{"global","Shared across builds in the same storage domain."}});
            if(n.riceScope=="global")n.riceLife="stored";
            ImGui::TextUnformatted("Life");ImGui::SameLine();ImGui::BeginDisabled(n.riceScope=="global");
            edited|=Toggle("life",n.riceLife,{{"game","Resets when the game session ends."},{"slot","Persists for this game in its main broadcast slot."},{"stored","Persists independently of the broadcast slot."}});
            ImGui::EndDisabled();
            ImGui::TextUnformatted("Access");ImGui::SameLine();
            edited|=Toggle("access",n.riceAccess,{{"locked","YouTube comments cannot change this variable."},{"policy","Only users authorized by the YouTube policy can change this variable."},{"open","Anyone can overwrite this variable through YouTube comments."}});
            if(n.riceAccess=="policy")edited|=TextField("Policy",n.policy);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ChildSymbol child{};
        const auto& diagnostics=&view==&e.main?e.diagnostics:view.diagnostics;
        bool invalid=std::any_of(diagnostics.begin(),diagnostics.end(),[&](auto& issue){return issue.node==n.id&&!issue.warning;});
        if(invalid)ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.3f,.3f,1));
        if(TypePicker(w,e,view,"Type",n.type,false,&child)){
            auto* scope=view.document.Get(ScopeOf(view.document,n.parent));
            auto* source=child.entity?DiskDocument(child.file):nullptr;
            n.value=source&&source->CanRead(child.symbol)&&scope&&scope->kind!=Kind::Class
                ?BindRead(e,view,targets,child,n.id):DefaultValue(n.type);
            edited=true;
        }
        if(invalid)ImGui::PopStyleColor();
        ImGui::SameLine();ImGui::SetNextItemWidth(std::max(80.f,ImGui::GetContentRegionAvail().x*.5f));
        edited|=TextField("##variable",n.name);ImGui::SetItemTooltip("Variable name");
        if(!n.type.starts_with("std::"))ImGui::SameLine();
        edited|=Initializer(w,e,view,n);
        return edited;
    }
    bool SymbolPicker(View& view,const char* label,Id at,Kind kind,std::string& name,bool changeTarget,Id& addOwner,bool onlyList=false){
        bool changed=false;
        ImGui::PushID(label);
        if(ImGui::Button((name.empty()?std::string(label):name+"###select").c_str())){view.search.clear();ImGui::OpenPopup("Defined");}
        if(ImGui::BeginPopup("Defined")){
            ImGui::SetNextItemWidth(180);ImGui::InputTextWithHint("##find",changeTarget?"Find a target":"Find a declaration",&view.search);
            if(changeTarget&&Match(Scripting::GameSpeedTarget,view.search)){
                ImGui::SeparatorText("Engine");
                if(ImGui::Selectable(Scripting::GameSpeedTarget,name==Scripting::GameSpeedTarget)){
                    name=Scripting::GameSpeedTarget;changed=true;
                }
                ImGui::SetItemTooltip("Game time multiplier (1 = normal speed).");
            }
            ImGui::SeparatorText("Defined");
            int found=0;
            for(auto& symbol:view.document.Nodes())if(symbol.kind==kind&&view.document.CanSee(symbol.id,at)&&
                    (!onlyList||symbol.type.starts_with("std::vector<"))&&Match(symbol.name,view.search)){
                ++found;
                if(ImGui::Selectable(symbol.name.c_str(),symbol.name==name)){
                    name=symbol.name;view.selectedReferences[at][name]=symbol.id;changed=true;
                }
            }
            if(!found)ImGui::TextDisabled("no result...");
            if(onlyList&&ImGui::Selectable("None")){name.clear();changed=true;}
            if(changeTarget&&ImGui::Selectable("Add Variable"))addOwner=ScopeOf(view.document,at);
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return changed;
    }
    bool Expression(Workspace& w,Entry& e,View& view,Targets& targets,Id owner,const char* label,std::string& value){
        auto& buf=Buffer(label,value);
        ImGui::SetNextItemWidth(std::max(120.f,ImGui::GetContentRegionAvail().x*.78f));
        bool submit=ImGui::InputText(label,&buf.text,ImGuiInputTextFlags_EnterReturnsTrue);
        bool done=ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip("Type a backtick to search for a variable.");
        auto tick=buf.text.rfind(char(96));
        if(tick!=std::string::npos){
            ImGui::PushID(label);
            if(view.searchingVariablesAt!=owner){view.searchingVariablesAt=owner;view.variableQuery.clear();}
            ImGui::SetNextItemWidth(200);ImGui::InputTextWithHint("##variables","Search variables",&view.variableQuery);
            bool picked=false;
            auto pick=[&](const std::string& name,auto expression){
                if(!picked&&Match(name,view.variableQuery)&&ImGui::Selectable(name.c_str())){
                    buf.text.replace(tick,1,expression());picked=true;
                }
            };
            for(auto& n:view.document.Nodes())if(n.kind==Kind::Variable&&view.document.CanSee(n.id,owner))
                pick(n.name,[&]{view.selectedReferences[owner][n.name]=n.id;return n.name;});
            for(auto& child:ChildSymbols(w,e))
                pick(child.path+" / "+ComponentLabel(child.component)+" / "+child.name,[&]{return BindRead(e,view,targets,child,owner);});
            ImGui::PopID();
            if(!picked)return false;
            view.searchingVariablesAt=0;submit=true;
        }
        if((submit||done)&&value!=buf.text){value=buf.model=buf.text;return true;}
        return false;
    }

    struct Term { std::string op,value; };
    static std::vector<Term> Terms(const std::string& expr){
        // The list editor writes one explicit pair of parentheses per operand.
        // Keep independently authored C++ as a single editable expression.
        if(expr.size()>6&&expr.front()=='('&&expr.back()==')'){
            int depth=0;size_t split=0;
            for(;split<expr.size();++split){
                if(expr[split]=='(')++depth;
                if(expr[split]==')'&&!--depth)break;
            }
            auto rest=expr.substr(std::min(split+1,expr.size()));
            if(rest.starts_with(" && (")||rest.starts_with(" || (")){
                auto lhs=Terms(expr.substr(1,split-1));
                lhs.push_back({rest.substr(1,2),rest.substr(5,rest.size()-6)});
                return lhs;
            }
        }
        return {{"",expr}};
    }
    static std::string Fold(const std::vector<Term>& terms){
        std::string value=terms.front().value;
        for(size_t i=1;i<terms.size();++i)value="("+value+") "+terms[i].op+" ("+terms[i].value+")";
        return value;
    }
    bool Predicate(Workspace& w,Entry& e,View& view,Targets& targets,Node& n){
        auto terms=Terms(n.value);bool edited=false;int remove=-1;
        std::size_t groupFirst=0,groupLast=0;
        for(size_t i=0;i<terms.size();++i){
            auto& t=terms[i];ImGui::PushID(static_cast<int>(i));
            if(i){
                if(ImGui::SmallButton(t.op=="&&"?"and":"or")){t.op=t.op=="&&"?"||":"&&";edited=true;}
                ImGui::SameLine();
            }
            const std::string prefix="Physics(Phase::";
            std::string phase,filter;
            if(t.value.starts_with(prefix)){
                auto comma=t.value.find(','),start=t.value.find("return "),end=t.value.rfind(";}");
                if(comma!=t.value.npos&&start!=t.value.npos&&end!=t.value.npos&&end>start){
                    phase=t.value.substr(prefix.size(),comma-prefix.size());filter=t.value.substr(start+7,end-start-7);
                    if(filter=="true")filter.clear();
                }
            }
            if(ImGui::SmallButton((phase.empty()?"set condition":phase.c_str())))ImGui::OpenPopup("Kinds");
            if(ImGui::BeginPopup("Kinds")){
                auto pick=[&](const char* label,std::string expression,const Node* symbol=nullptr){
                    if(ImGui::Selectable(label)){
                        t.value=std::move(expression);edited=true;
                        if(symbol)view.selectedReferences[n.id][symbol->name]=symbol->id;
                    }
                };
                if(ImGui::BeginMenu("Boolean variable")){
                    for(auto& v:view.document.Nodes())if(v.kind==Kind::Variable&&v.type=="bool"&&view.document.CanSee(v.id,n.id))pick(v.name.c_str(),v.name,&v);
                    ImGui::EndMenu();
                }
                pick("Compare values","0 == 0");
                if(ImGui::BeginMenu("Object active")){
                    pick("This","Active(\"\", \"\")");
                    for(auto& obj:w.editor.scene->GetObjects()){
                        auto path=Under(obj.get(),e.entity);
                        if(path.empty()||obj->IsPendingDestroy()||obj->IsPoolAvailable())continue;
                        ImGui::PushID(std::to_string(obj->GetID()).c_str());
                        if(ImGui::Selectable(path.c_str())){
                            auto key="active_"+std::to_string(n.id)+"_"+NewAssetID();
                            targets.children[key]=obj->GetID();
                            t.value="Active(\""+key+"\", \"\")";edited=true;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndMenu();
                }
                if(ImGui::BeginMenu("Function result")){
                    for(auto& fn:view.document.Nodes())if(fn.kind==Kind::Function&&fn.type!="void"&&view.document.CanSee(fn.id,n.id))
                        pick(fn.name.c_str(),"(co_await "+fn.name+"())",&fn);
                    ImGui::EndMenu();
                }
                if(ImGui::BeginMenu("Physics")){
                    for(auto p:{"Collision","Overlap","OverlapEnd"})pick(std::string_view(p)=="OverlapEnd"?"Overlap End":p,
                            prefix+p+", [&](const Contact& contact){return true;})");
                    ImGui::EndMenu();
                }
                ImGui::EndPopup();
            }
            if(phase.empty())edited|=Expression(w,e,view,targets,n.id,"Expression",t.value);
            else{
                bool filterChanged=Expression(w,e,view,targets,n.id,"Contact filter",filter);
                if(ImGui::SmallButton("Is Ground")){filter="contact.isGround";filterChanged=true;}
                ImGui::SameLine();if(ImGui::SmallButton("Is Trigger")){filter="contact.isTrigger";filterChanged=true;}
                if(filterChanged){t.value=prefix+phase+", [&](const Contact& contact){return "+(filter.empty()?"true":filter)+";})";edited=true;}
            }
            if(ImGui::SmallButton("( )"))ImGui::OpenPopup("Group conditions");
            ImGui::SetItemTooltip("Choose the last row to group with this condition");
            if(ImGui::BeginPopup("Group conditions")){
                for(size_t last=i;last<terms.size();++last){
                    std::string label=last==i?"This expression":"Through row "+std::to_string(last+1);
                    if(ImGui::Selectable(label.c_str())){
                        if(last==i){t.value="("+t.value+")";edited=true;}
                        else{groupFirst=i;groupLast=last;}
                    }
                }
                ImGui::EndPopup();
            }
            if(terms.size()>1){ImGui::SameLine();if(ImGui::SmallButton("-##term"))remove=static_cast<int>(i);}
            ImGui::PopID();
        }
        if(groupLast>groupFirst){
            std::vector<Term> subset(terms.begin()+groupFirst,terms.begin()+groupLast+1);
            terms[groupFirst].value="("+Fold(subset)+")";
            terms.erase(terms.begin()+groupFirst+1,terms.begin()+groupLast+1);edited=true;
        }
        if(remove>=0){terms.erase(terms.begin()+remove);terms.front().op.clear();edited=true;}
        if(ImGui::SmallButton("+##condition"))ImGui::OpenPopup("Extend condition");
        if(ImGui::BeginPopup("Extend condition")){
            if(ImGui::MenuItem("and")){terms.push_back({"&&","true"});edited=true;}
            if(ImGui::MenuItem("or")){terms.push_back({"||","true"});edited=true;}
            for(auto k:{Kind::ElseIf,Kind::Else})if(ImGui::MenuItem(Caption(k)))AddBranch(e,view,n,k);
            ImGui::EndPopup();
        }
        if(edited)n.value=Fold(terms);
        return edited;
    }
    void AddBranch(Entry& e,View& v,const Node& anchor,Kind k){
        auto id=v.document.AddBranch(anchor.id,k);
        if(!id)return;
        e.Changed(v,id);v.Reveal(id);
    }
    static std::string ChildLabel(Workspace& w,const Entry& e,const Targets& targets,const std::string& key){
        if(key.empty())return "This";
        auto found=targets.children.find(key);
        auto* target=found==targets.children.end()?nullptr:w.editor.scene->Find(found->second);
        auto label=Under(target,e.entity);
        return label.empty()?"Missing target":label;
    }
    bool TargetPicker(Workspace& w,Entry& e,Targets& targets,Node& n){
        bool edited=false;auto text=ChildLabel(w,e,targets,n.name);
        if(!n.type.empty())text+=" / "+ComponentLabel(n.type);
        if(ImGui::BeginCombo("Target",text.c_str())){
            auto group=[&](GameObject& object,bool self){
                auto set=[&](std::string component){
                    n.name=self?"":"target_"+std::to_string(n.id);
                    if(!self)targets.children[n.name]=object.GetID();
                    n.type=std::move(component);edited=true;
                };
                if(ImGui::Selectable("Object"))set({});
                if(ImGui::BeginMenu("Component")){
                    for(auto& type:w.registry.types)if(type.get(object)&&ImGui::Selectable(ComponentLabel(type.name).c_str()))set(type.name);
                    ImGui::EndMenu();
                }
            };
            if(auto* owner=w.editor.scene->Find(e.entity);owner&&ImGui::BeginMenu("This")){group(*owner,true);ImGui::EndMenu();}
            if(ImGui::BeginMenu("Child")){
                for(auto& obj:w.editor.scene->GetObjects()){
                    auto path=Under(obj.get(),e.entity);
                    if(path.empty()||obj->IsPendingDestroy()||obj->IsPoolAvailable())continue;
                    ImGui::PushID(std::to_string(obj->GetID()).c_str());
                    if(ImGui::BeginMenu(path.c_str())){group(*obj,false);ImGui::EndMenu();}
                    ImGui::PopID();
                }
                ImGui::EndMenu();
            }
            ImGui::EndCombo();
        }
        bool active=n.value!="false";
        if(ImGui::Checkbox(active?"Active":"Inactive",&active)){n.value=active?"true":"false";edited=true;}
        return edited;
    }
    static std::vector<Id> Children(const Doc& doc,Id parent){
        std::vector<Id> ids;
        for(auto& n:doc.Nodes())if(n.parent==parent)ids.push_back(n.id);
        return ids;
    }
    void Add(Entry& e,View& view,Id parent,Kind kind){
        auto& doc=view.document;
        auto ids=Children(doc,parent);
        auto id=doc.Add(parent,kind);
        if(!id){e.error="This item cannot be added here.";return;}
        for(auto& n:doc.Nodes())if(n.parent==parent&&std::find(ids.begin(),ids.end(),n.id)==ids.end())e.Changed(view,n.id);
        view.Reveal(id);
    }
    void Remove(Entry& e,View& view,Id id){
        std::set<Id> removed;
        for(auto& n:view.document.Nodes())
            for(auto* current=&n;current;current=view.document.Get(current->parent))
                if(current->id==id){removed.insert(n.id);break;}
        if(view.document.Erase(id)){
            for(auto item:removed)e.Changed(view,item);
            view.selected=0;
        }
    }
    void MarkEdit(Entry& e,View& view,Node n){
        if(auto it=view.selectedReferences.find(n.id);it!=view.selectedReferences.end()){
            for(auto& [name,id]:it->second)n.references[name]=id;
            view.selectedReferences.erase(it);
        }
        if(view.document.Edit(n.id,n))e.Changed(view,n.id);
    }
    bool ItemFields(Workspace& w,Entry& e,View& view,Targets& targets,Node& n){
        bool edit=false;
        if(StrategyFields(view,n,edit))return edit;
        switch(n.kind){
        case Kind::Variable:return VariableFields(w,e,view,targets,n);
        case Kind::Function:{
            if(ImGui::SmallButton(n.isPublic?"public":"private")){n.isPublic=!n.isPublic;edit=true;}
            ImGui::SameLine();edit|=TypePicker(w,e,view,"Return Type",n.type,true);
            ImGui::SetItemTooltip("Return type");ImGui::SameLine();edit|=TextField("Name",n.name);
            edit|=TextField("Parameters",n.args);ImGui::SetItemTooltip("Enter typed parameters, for example: int score, float amount");
            break;
        }
        case Kind::Class:{
            edit|=TextField("Class name",n.name);
            for(auto k:{Kind::OnCreate,Kind::OnDestroy}){
                auto kids=Children(view.document,n.id);
                auto it=std::find_if(kids.begin(),kids.end(),[&](Id id){return view.document.Get(id)->kind==k;});
                bool on=it!=kids.end();
                if(ImGui::Checkbox(Caption(k),&on)){if(on)Add(e,view,n.id,k);else Remove(e,view,*it);}
                if(k==Kind::OnCreate)ImGui::SameLine();
            }
            break;
        }
        case Kind::Change:{
            ImGui::TextUnformatted("change");ImGui::SameLine();
            Id requested=std::numeric_limits<Id>::max();
            bool valid=n.name==Scripting::GameSpeedTarget;
            for(auto& decl:view.document.Nodes())if(decl.kind==Kind::Variable&&decl.name==n.name&&view.document.CanSee(decl.id,n.id)){valid=true;break;}
            if(!valid)ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.3f,.3f,1));
            edit|=SymbolPicker(view,"add parameter",n.id,Kind::Variable,n.name,true,requested);
            if(!valid)ImGui::PopStyleColor();
            if(requested!=std::numeric_limits<Id>::max())Add(e,view,requested,Kind::Variable);
            ImGui::SameLine();ImGui::TextUnformatted("into");ImGui::SameLine();
            if(ImGui::SmallButton("add value"))ImGui::OpenPopup("Values");
            if(ImGui::BeginPopup("Values")){
                if(ImGui::BeginMenu("Defined")){
                    for(auto& variable:view.document.Nodes())if(variable.kind==Kind::Variable&&view.document.CanSee(variable.id,n.id)&&ImGui::Selectable(variable.name.c_str())){
                        n.value=variable.name;view.selectedReferences[n.id][variable.name]=variable.id;edit=true;
                    }
                    ImGui::EndMenu();
                }
                if(ImGui::Selectable("Expression"))view.Reveal(n.id);
                if(ImGui::BeginMenu("Children")){
                    for(auto& child:ChildSymbols(w,e))if(ImGui::Selectable((child.path+" / "+ComponentLabel(child.component)+" / "+child.name).c_str())){
                        n.value=BindRead(e,view,targets,child,n.id);edit=true;
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndPopup();
            }
            edit|=Expression(w,e,view,targets,n.id,"Expression",n.value);
            break;
        }
        case Kind::Call:{
            Id unused{};edit|=SymbolPicker(view,"Function",n.id,Kind::Function,n.name,false,unused);
            edit|=Expression(w,e,view,targets,n.id,"Arguments",n.args);
            break;
        }
        case Kind::Activate:return TargetPicker(w,e,targets,n);
        case Kind::Move:{
            const float available=std::max(1.f,ImGui::GetContentRegionAvail().x);
            const float right=ImGui::GetCursorScreenPos().x+available;
            const float width=std::min(available,std::clamp(available*.1f,35.f,90.f));
            const float valueWidth=std::min(available,width*1.5f);
            const float comboWidth=std::min(available,ImGui::CalcTextSize("game second").x+ImGui::GetFrameHeight()+2*ImGui::GetStyle().FramePadding.x);
            auto sameLineIfFits=[&](float nextWidth){
                if(ImGui::GetItemRectMax().x+ImGui::GetStyle().ItemSpacing.x+nextWidth<=right)ImGui::SameLine();
            };
            ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("Move");sameLineIfFits(valueWidth);
            ImGui::SetNextItemWidth(valueWidth);
            edit|=TextField("##movement",n.value,false,"value");
            ImGui::SetItemTooltip("Leave empty to use X, Y and Z as the displacement per unit.");
            sameLineIfFits(ImGui::CalcTextSize("to").x);ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("to");
            constexpr const char* axes[]={"X","Y","Z"};
            for(size_t i=0;i<n.moveCoordinates.size();++i){
                sameLineIfFits(ImGui::CalcTextSize(axes[i]).x+ImGui::GetStyle().ItemSpacing.x+width);
                ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(axes[i]);sameLineIfFits(width);
                ImGui::SetNextItemWidth(width);
                ImGui::PushID(axes[i]);edit|=TextField("##coordinate",n.moveCoordinates[i]);ImGui::PopID();
            }
            sameLineIfFits(ImGui::CalcTextSize("per").x+ImGui::GetStyle().ItemSpacing.x+comboWidth);
            ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("per");sameLineIfFits(comboWidth);
            ImGui::SetNextItemWidth(comboWidth);
            if(ImGui::BeginCombo("##move unit",n.args.c_str())){
                for(auto unit:{"second","game second","frame"})
                    if(ImGui::Selectable(unit,n.args==unit)){n.args=unit;edit=true;}
                ImGui::EndCombo();
            }
            break;
        }
        case Kind::Loop:{ImGui::TextUnformatted("loop");ImGui::SameLine();Id unused{};
            edit|=SymbolPicker(view,"set Scope",n.id,Kind::Variable,n.value,false,unused,true);break;}
        case Kind::Wait:return Expression(w,e,view,targets,n.id,"Seconds",n.value);
        case Kind::Condition:case Kind::ElseIf:return Predicate(w,e,view,targets,n);
        case Kind::EndFunction:{
            for(auto* owner=view.document.Get(n.parent);owner;owner=view.document.Get(owner->parent)){
                if(owner->kind==Kind::Function){if(owner->type!="void")edit|=Expression(w,e,view,targets,n.id,"set Output",n.value);break;}
                if(owner->kind==Kind::Start||owner->kind==Kind::Update)break;
            }
            break;
        }
        case Kind::NewClass:{
            Id unused{};auto type=n.type;
            bool picked=SymbolPicker(view,"Class",n.id,Kind::Class,type,false,unused);
            std::string newName=n.name;
            bool named=TextField("set name",newName);
            if(picked||named){
                for(auto& decl:view.document.Nodes())if(decl.kind==Kind::Variable&&decl.name==n.name){
                    auto storage=decl;storage.type="std::optional<"+type+">";storage.name=newName;
                    MarkEdit(e,view,storage);break;
                }
                n.name=newName;n.type=type;edit=true;
            }
            break;
        }
        case Kind::State:case Kind::ChangeState:case Kind::SetState:
            edit|=TextField("Group",n.name);edit|=TextField("State",n.value);break;
        case Kind::Custom:edit|=TextField("C++ source",n.value,true);break;
        default:break;
        }
        return edit;
    }

    static bool Belongs(const Doc& doc,Id item,Id owner){
        for(auto* n=doc.Get(item);n;n=doc.Get(n->parent))if(n->id==owner)return true;
        return false;
    }
    static std::string WindowLabel(const Entry& entry,const View& view,unsigned id){
        std::string title="RiceMonkey - "+(entry.path.empty()?std::string("Untitled"):Utf8(entry.path.stem()));
        if(id){auto* node=view.document.Get(view.root);title=node?Title(*node):"Deleted item";}
        return title+"###RiceScript_"+std::to_string(entry.serial)+"_"+std::to_string(id);
    }
    void Context(Entry& e,View& view,const Node& n,const Targets& targets){
        if(!Decl(n.kind)||!ImGui::BeginPopupContextItem("Script item"))return;
        if(n.kind==Kind::Function||n.kind==Kind::Variable)if(ImGui::MenuItem("Find Instances")){
            view.Find(n.id);view.findOpen=true;
        }
        if(n.kind==Kind::Function||n.kind==Kind::Class)if(ImGui::MenuItem("Separate Window")){
            auto window=e.Separate(n.id);
            auto& next=e.detached.at(window);
            next.document=view.document;next.base=view.document.Source();
            e.detachedTargets[window]=e.targetBases[window]=targets;
        }
        ImGui::EndPopup();
    }
    void Heading(const char* text,bool& open){
        auto width=ImGui::GetContentRegionAvail().x;
        if(ImGui::Selectable(text,false,ImGuiSelectableFlags_None,{std::max(1.f,width-ImGui::GetFrameHeight()-8),0}))open=!open;
        ImGui::SameLine();
        if(ImGui::ArrowButton("##expand",open?ImGuiDir_Up:ImGuiDir_Down))open=!open;
    }
    void Group(Workspace& w,Entry& e,View& view,Targets& targets,Id owner,Kind kind,bool compact=false){
        const char* name=kind==Kind::Variable?"Variables":kind==Kind::Function?(compact?"Functions":"Function Editor"):
                         kind==Kind::Class?(compact?"Classes":"Class Editor"):"States";
        ImGui::PushID(name);
        auto* store=ImGui::GetStateStorage();auto key=ImGui::GetID("open");
        bool open=store->GetBool(key,true);
        if(view.scroll)open=true;
        Heading(name,open);store->SetBool(key,open);
        if(open){
            ImGui::Indent(12);
            auto ids=Children(view.document,owner);
            for(auto id:ids){
                auto* n=view.document.Get(id);if(!n||n->kind!=kind)continue;
                if(compact){
                    ImGui::PushID(std::to_string(id).c_str());
                    if(ImGui::Selectable(n->name.c_str(),view.selected==id))view.Reveal(id);
                    Context(e,view,*n,targets);ImGui::PopID();
                }else DrawNode(w,e,view,targets,id);
            }
            if(ImGui::SmallButton("+"))Add(e,view,owner,kind);
            ImGui::SetItemTooltip("Add %s",Caption(kind));ImGui::SameLine();
            auto* selected=view.document.Get(view.selected);
            ImGui::BeginDisabled(!selected||selected->parent!=owner||selected->kind!=kind);
            if(ImGui::SmallButton("-"))Remove(e,view,view.selected);
            ImGui::EndDisabled();
            ImGui::Unindent(12);
        }
        ImGui::PopID();
    }
    void AddMenu(Entry& e,View& view,Id owner){
        if(!ImGui::BeginPopup("Add action"))return;
        if(ImGui::MenuItem("New Function"))Add(e,view,ScopeOf(view.document,owner),Kind::Function);
        if(ImGui::MenuItem("Set Variable"))Add(e,view,ScopeOf(view.document,owner),Kind::Variable);
        StrategyMenu(e,view,owner);
        for(auto k:{Kind::NewClass,Kind::Change,Kind::Call,Kind::Activate,Kind::Move,Kind::Loop,Kind::Wait,Kind::Condition}){
            if(ImGui::MenuItem(k==Kind::Loop?"Loop":k==Kind::Condition?"Condition":Caption(k)))
                Add(e,view,owner,k);
        }
        if(ImGui::BeginMenu("End")){
            if(ImGui::MenuItem("Function"))Add(e,view,owner,Kind::EndFunction);
            bool loop=false;
            for(auto* n=view.document.Get(owner);n;n=view.document.Get(n->parent)){
                if(n->kind==Kind::Loop){loop=true;break;}
                if(Scope(n->kind))break;
            }
            if(ImGui::MenuItem("Loop",nullptr,false,loop))Add(e,view,owner,Kind::EndLoop);
            ImGui::EndMenu();
        }
        for(auto k:{Kind::SetState,Kind::ChangeState}){
            if(ImGui::BeginMenu(k==Kind::SetState?"Set":"Change")){
                if(ImGui::MenuItem("State"))Add(e,view,owner,k);
                ImGui::EndMenu();
            }
        }
        if(ImGui::MenuItem("Custom Code"))Add(e,view,owner,Kind::Custom);
        ImGui::EndPopup();
    }
    void Actions(Workspace& w,Entry& e,View& view,Targets& targets,Id owner){
        auto ids=Children(view.document,owner);
        for(auto id:ids){
            auto* node=view.document.Get(id);
            if(node&&!Decl(node->kind))DrawNode(w,e,view,targets,id);
        }
        if(ImGui::SmallButton("+##actions"))ImGui::OpenPopup("Add action");
        ImGui::SetItemTooltip("Add an action");ImGui::SameLine();
        auto* selected=view.document.Get(view.selected);
        ImGui::BeginDisabled(!selected||selected->parent!=owner||Decl(selected->kind));
        if(ImGui::SmallButton("-##action"))Remove(e,view,view.selected);
        ImGui::SameLine();
        if(ImGui::ArrowButton("##earlier",ImGuiDir_Up)&&view.document.Move(view.selected,-1))e.Changed(view,view.selected);
        ImGui::SameLine();
        if(ImGui::ArrowButton("##later",ImGuiDir_Down)&&view.document.Move(view.selected,1))e.Changed(view,view.selected);
        ImGui::EndDisabled();
        AddMenu(e,view,owner);
    }
    void DrawNode(Workspace& w,Entry& e,View& view,Targets& targets,Id id){
        auto* ref=view.document.Get(id);if(!ref)return;
        Node item=*ref;
        ImGui::PushID(std::to_string(id).c_str());
        auto [fold,inserted]=view.expanded.try_emplace(id,item.kind==Kind::Start||item.kind==Kind::Update);
        bool& open=fold->second;
        const auto& diagnostics=&view==&e.main?e.diagnostics:view.diagnostics;
        auto red=std::any_of(diagnostics.begin(),diagnostics.end(),[&](auto& d){return !d.warning&&Belongs(view.document,d.node,id);});
        const auto& runtime=RuntimeErrors(w,e.path);
        red|=std::any_of(runtime.begin(),runtime.end(),[&](const auto& item){return Belongs(view.document,item.first,id);});
        bool green=item.kind==Kind::Variable&&HasProtected(w,e,item);
        if(red||green)ImGui::PushStyleColor(ImGuiCol_Text,red?ImVec4(1,.3f,.3f,1):ImVec4(.35f,.88f,.43f,1));
        const bool reveal=view.scroll&&view.reveal==id;
        if(reveal)ImGui::SetKeyboardFocusHere();
        auto label=Title(item)+"###item";
        if(ImGui::Selectable(label.c_str(),id==view.selected,ImGuiSelectableFlags_None,
                {std::max(1.f,ImGui::GetContentRegionAvail().x-ImGui::GetFrameHeight()-8),0})){
            view.selected=id;open=true;
        }
        if(red||green)ImGui::PopStyleColor();
        if(reveal){ImGui::SetScrollHereY(.4f);view.scroll=false;}
        Context(e,view,item,targets);ImGui::SameLine();
        if(ImGui::ArrowButton("##expand",open?ImGuiDir_Up:ImGuiDir_Down))open=!open;
        if(open){
            ImGui::Indent(12);
            if(ItemFields(w,e,view,targets,item))MarkEdit(e,view,item);
            if(HasBody(item.kind)){
                const bool locals=Scope(item.kind);
                if(locals){
                    Group(w,e,view,targets,id,Kind::Variable);
                    Group(w,e,view,targets,id,Kind::Function);
                    Group(w,e,view,targets,id,Kind::Class);
                }
                if(item.kind==Kind::Class||item.kind==Kind::State){
                    for(auto child:Children(view.document,id)){
                        auto* n=view.document.Get(child);
                        if(n&&!Decl(n->kind))DrawNode(w,e,view,targets,child);
                    }
                }else{
                    ImGui::SeparatorText("Definition");
                    Actions(w,e,view,targets,id);
                }
            }
            ImGui::Unindent(12);
        }
        ImGui::PopID();
    }
    void BindMissingReads(Workspace& w,Entry& e,View& view,Targets& targets){
        for(auto& ref:Reads(view)){
            if(view.root&&!Belongs(view.document,view.document.AtLine(ref.line),view.root))continue;
            auto* problem=ReadProblem(w,e,targets,ref);
            if(!problem)continue;
            ImGui::PushID(ref.key.c_str());
            ImGui::TextColored({1,.35f,.35f,1},"%s%s.",problem,ref.name.c_str());
            ImGui::SameLine();
            if(ImGui::BeginCombo("##child binding","Choose child...")){
                for(auto& child:ChildSymbols(w,e))if(child.name==ref.name&&child.type==ref.type&&
                    ImGui::Selectable((child.path+" / "+ComponentLabel(child.component)).c_str())){
                    targets.children[ref.key]=child.entity;targets.reads[ref.key]={child.component,child.name,child.symbol};
                    e.Changed(view,view.document.AtLine(ref.line));
                    if(&view==&e.main)ValidateTargets(w,e,targets);
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }
    }
    void FindPopup(View& view){
        if(!view.findOpen)return;
        if(!ImGui::IsPopupOpen("Find Instances"))ImGui::OpenPopup("Find Instances");
        if(!ImGui::BeginPopupModal("Find Instances",nullptr,ImGuiWindowFlags_AlwaysAutoResize))return;
        bool finish=false;
        if(view.matches.empty()){
            ImGui::TextUnformatted("No instances found.");
            finish=ImGui::Button("OK");
        }else{
            if(ImGui::IsKeyPressed(ImGuiKey_UpArrow)||ImGui::IsKeyPressed(ImGuiKey_LeftArrow))view.Navigate(-1);
            if(ImGui::IsKeyPressed(ImGuiKey_DownArrow)||ImGui::IsKeyPressed(ImGuiKey_RightArrow))view.Navigate(1);
            auto* n=view.document.Get(view.matches[view.match]);
            if(n)ImGui::Text("%zu / %zu   Line %zu   %s",view.match+1,view.matches.size(),n->line,Title(*n).c_str());
            ImGui::TextUnformatted("Use the arrow keys to browse. Press Enter or click anywhere to go to the selected instance.");
        }
        if(!ImGui::IsWindowAppearing())finish|=ImGui::IsKeyPressed(ImGuiKey_Enter)||ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)||ImGui::IsMouseClicked(0);
        if(finish){
            view.findOpen=false;
            if(!view.matches.empty())view.Reveal(view.matches[view.match]);
            view.focus=true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    void DrawWindow(Workspace& w,Entry& e,View& view,unsigned id,const std::string& ide){
        auto label=WindowLabel(e,view,id);
        const auto& diagnostics=id?view.diagnostics:e.diagnostics;
        bool red=std::any_of(diagnostics.begin(),diagnostics.end(),[&](auto& d){return !d.warning&&(!id||Belongs(view.document,d.node,view.root));})||
                 !e.error.empty()||!RuntimeErrors(w,e.path).empty();
        if(view.focus){ImGui::SetNextWindowFocus();view.focus=false;}
        ImGui::SetNextWindowSize({680,600},ImGuiCond_FirstUseEver);
        if(red){
            ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.34f,.36f,1));
            ImGui::PushStyleColor(ImGuiCol_TitleBg,ImVec4(.48f,.09f,.1f,1));
            ImGui::PushStyleColor(ImGuiCol_TitleBgActive,ImVec4(.72f,.12f,.12f,1));
            ImGui::PushStyleColor(ImGuiCol_TabSelected,ImVec4(.62f,.1f,.1f,1));
        }
        auto dirty=[&]{return id?view.document.Source()!=view.base||e.detachedTargets[id]!=e.targetBases[id]:e.Dirty();};
        auto flags=(id?0:ImGuiWindowFlags_MenuBar)|(dirty()?ImGuiWindowFlags_UnsavedDocument:0);
        bool visible=ImGui::Begin(label.c_str(),&view.open,flags);
        if(red)ImGui::PopStyleColor(4);
        bool current=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        focused|=current;
        const bool closeRequested=!view.open;
        auto& targets=id?e.detachedTargets[id]:e.targets;
        if(visible){
            if(!id)Toolbar(w,e,ide);
            else{
                if(ImGui::Button("Apply Changes"))ApplyWindow(e,id);
                ImGui::SameLine();if(ImGui::Button("Open RiceMonkey"))e.main.focus=e.main.open=true;
            }
            if(!e.error.empty())ImGui::TextWrapped("%s",e.error.c_str());
            for(auto& [row,message]:RuntimeErrors(w,e.path))if(!id||Belongs(view.document,row,view.root)){
                auto* node=view.document.Get(row);
                ImGui::TextColored({1,.35f,.35f,1},"Line %zu  %s",node?node->line:size_t{},message.c_str());
            }
            for(auto& d:diagnostics)if(!d.warning&&(!id||Belongs(view.document,d.node,view.root)))
                ImGui::TextColored({1,.35f,.35f,1},"Line %zu  %s",d.line,d.message.c_str());
            if(ImGui::BeginChild("Script content",{},ImGuiChildFlags_None)){
                BindMissingReads(w,e,view,targets);
                if(id){
                    if(view.document.Get(view.root))DrawNode(w,e,view,targets,view.root);
                    else ImGui::TextDisabled("This declaration was deleted.");
                }else{
                    Group(w,e,view,targets,0,Kind::Variable);
                    Group(w,e,view,targets,0,Kind::Function,true);
                    Group(w,e,view,targets,0,Kind::Class,true);
                    Group(w,e,view,targets,0,Kind::Function);
                    Group(w,e,view,targets,0,Kind::Class);
                    Group(w,e,view,targets,0,Kind::State);
                    for(auto child:Children(view.document,0)){
                        auto* node=view.document.Get(child);
                        if(node&&!Decl(node->kind)&&node->kind!=Kind::State)DrawNode(w,e,view,targets,child);
                    }
                }
            }
            ImGui::EndChild();
            if(current&&!id&&ImGui::GetIO().KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_S,false))
                try{SaveDialog(w,e,false);}catch(const std::exception& ex){LogIssue(w,e,ex.what());}
        }
        if(closeRequested&&dirty()){view.open=true;ImGui::OpenPopup("Unsaved changes");}
        FindPopup(view);
        if(ImGui::BeginPopupModal("Unsaved changes",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::TextUnformatted("You have unsaved changes. Close this window?");
            if(ImGui::Button("Save")){
                try{
                    if(id)ApplyWindow(e,id);
                    if(SaveDialog(w,e,false)){view.open=false;ImGui::CloseCurrentPopup();}
                }catch(const std::exception& ex){LogIssue(w,e,ex.what());}
            }
            ImGui::SameLine();
            if(ImGui::Button("Don't Save")){
                if(!id)e.Discard();
                view.open=false;ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::End();
    }
    static void ApplyWindow(Entry& entry,unsigned id){
        auto& view=entry.detached.at(id);
        auto& bindings=entry.detachedTargets[id];
        const auto& base=entry.targetBases[id];
        auto merge=[&](auto& result,const auto& initial,const auto& edited){
            for(auto& [key,value]:edited){
                auto original=initial.find(key);
                if(original!=initial.end()&&original->second==value)continue;
                auto p=key.find('_');
                Id row=0;
                if(p!=std::string::npos)std::from_chars(key.data()+p+1,key.data()+key.size(),row);
                if(view.edits.contains(row)&&view.edits[row]>=entry.main.edits[row])result[key]=value;
            }
        };
        merge(entry.targets.children,base.children,bindings.children);
        merge(entry.targets.reads,base.reads,bindings.reads);
        entry.Apply(view);
        bindings=entry.targetBases[id]=entry.targets;
    }
