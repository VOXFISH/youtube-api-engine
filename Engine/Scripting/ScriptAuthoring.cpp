#include "ScriptAuthoring.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Rice::Scripting {
namespace {
constexpr auto missing = std::string::npos;
constexpr std::string_view marker = "/*rice:";
bool Space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }
bool Letter(char c) { return c == '_' || std::isalpha(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) >= 128; }
bool Word(char c) { return Letter(c) || std::isdigit(static_cast<unsigned char>(c)); }
std::string Trim(std::string_view s) {
    while (!s.empty() && Space(s.front())) s.remove_prefix(1);
    while (!s.empty() && Space(s.back())) s.remove_suffix(1);
    return std::string(s);
}
bool Identifier(std::string_view s) {
    return !s.empty() && Letter(s.front()) && std::all_of(s.begin()+1,s.end(),Word);
}
bool Decl(NodeKind k) { return k == NodeKind::Variable || k == NodeKind::Function || k == NodeKind::Class; }
bool Callback(NodeKind k) { return k==NodeKind::Start || k==NodeKind::Update || k==NodeKind::OnCreate || k==NodeKind::OnDestroy; }
bool Branch(NodeKind k) { return k==NodeKind::Condition || k==NodeKind::ElseIf || k==NodeKind::Else; }
bool StateCallback(NodeKind k) {return k==NodeKind::OnEnter||k==NodeKind::OnUpdate||k==NodeKind::OnExit;}
bool Owner(NodeKind k) { return (Decl(k) && k!=NodeKind::Variable) || Callback(k) || Branch(k) || k==NodeKind::Loop || k==NodeKind::State || StateCallback(k); }
bool Returning(NodeKind k) { return k==NodeKind::Start || k==NodeKind::Update || k==NodeKind::Function; }
std::vector<std::string_view> References(const ScriptNode&);

std::string Quote(std::string_view s) {
    std::ostringstream out;
    out << std::quoted(std::string(s));
    return out.str();
}
std::string Unquote(std::string_view s) {
    std::istringstream in{std::string(s)};
    std::string result;
    in >> std::quoted(result);
    return result;
}

struct Token {
    std::size_t from, to;
    std::string_view value;
    std::size_t mate = missing;
};
struct CppTokens {
    std::string_view source;
    std::vector<Token> items;
    std::vector<std::size_t> lines{0};
    std::size_t bad = missing;
    explicit CppTokens(std::string_view text) : source(text) {
        for(std::size_t i=0;i<text.size();++i) if(text[i]=='\n')lines.push_back(i+1);
        std::vector<std::size_t> brackets;
        for(std::size_t i=0;i<text.size();) {
            const std::size_t start=i;
            const char c=text[i++];
            if(Space(c))continue;
            auto tail=text.substr(start);
            if(tail.starts_with("//")) {
                auto e=text.find('\n',i);i=e==missing?text.size():e; continue;
            }
            if(tail.starts_with("/*")) {
                auto e=text.find("*/",start+2);
                if(e==missing){bad=start;break;}
                i=e+2;
                if(!tail.starts_with(marker))continue;
            } else if(c=='#' && (start==0 || Trim(text.substr(lines[Line(start)-1],start-lines[Line(start)-1])).empty())) {
                // A macro body may contain arbitrary unmatched C++ brackets.
                do { auto e=text.find('\n',i); i=e==missing?text.size():e+1; }
                while(i<text.size()&&i>=2&&(text[i-2]=='\\'||(i>=3&&text[i-2]=='\r'&&text[i-3]=='\\')));
            } else if(tail.starts_with("R\"")||tail.starts_with("u8R\"")||tail.starts_with("uR\"")||
                      tail.starts_with("LR\"")||tail.starts_with("UR\"")) {
                auto delimiter=start+tail.find('"')+1;
                auto open=text.find('(',delimiter);
                if(open==missing||open-delimiter>16){bad=start;break;}
                std::string close=")"+std::string(text.substr(delimiter,open-delimiter))+"\"";
                auto e=text.find(close,open+1);if(e==missing){bad=start;break;}
                i=e+close.size();
            } else if(c=='"'||c=='\'') {
                bool closed=false;
                while(i<text.size()) {char d=text[i++];if(d=='\\'&&i<text.size())++i;else if(d==c){closed=true;break;}}
                if(!closed){bad=start;break;}
            } else if(Word(c)) {while(i<text.size()&&Word(text[i]))++i;}
            else {
                for(auto op:{"<<=",">>=","::","->","==","!=","<=",">=","&&","||","+=","-=","*=","/=","++","--"}) {
                    if(tail.starts_with(op)){i=start+std::char_traits<char>::length(op);break;}
                }
            }
            auto v=text.substr(start,i-start);
            auto at=items.size();
            items.push_back({start,i,v});
            if(v=="{"||v=="("||v=="[")brackets.push_back(at);
            else if(v=="}"||v==")"||v=="]") {
                if(brackets.empty()){bad=start;break;}
                auto b=brackets.back();brackets.pop_back();
                if((v=="}"&&items[b].value!="{")||(v==")"&&items[b].value!="(")||(v=="]"&&items[b].value!="[")){bad=start;break;}
                items[b].mate=at;items[at].mate=b;
            }
        }
        if(bad==missing&&!brackets.empty())bad=items[brackets.back()].from;
    }
    std::size_t Line(std::size_t pos) const {return std::upper_bound(lines.begin(),lines.end(),pos)-lines.begin();}
    std::string_view V(std::size_t i) const {return i<items.size()?items[i].value:std::string_view{};}
    std::string Slice(std::size_t a,std::size_t b) const {
        if(a>=b||b>items.size())return {};
        return Trim(source.substr(items[a].from,items[b-1].to-items[a].from));
    }
    std::size_t Next(std::size_t p) const {auto x=items[p].mate;return x!=missing&&x>p?x+1:p+1;}
    std::vector<std::string> Args(std::size_t a, std::size_t b) const {
        std::vector<std::string> out;
        for(auto p=a;p<b;p=Next(p)) if(V(p)==","){out.push_back(Slice(a,p));a=p+1;}
        if(a<b)out.push_back(Slice(a,b));
        return out;
    }
};

bool HasToken(std::string_view text,std::string_view name) {
    if(name.empty())return false;
    CppTokens toks(text);
    return std::any_of(toks.items.begin(),toks.items.end(),[&](const auto& t){return t.value==name;});
}
ScriptId FreshId() {
    static std::atomic<ScriptId> serial{static_cast<ScriptId>(std::chrono::system_clock::now().time_since_epoch().count())|1};
    return ++serial;
}
ScriptId Hash(std::string_view text,ScriptId h=1469598103934665603ull) {
    for(unsigned char c:text)h=(h^c)*1099511628211ull;
    return h?h:1;
}
std::string PlainExpression(const std::string& expression) {
    CppTokens t(expression);
    std::vector<std::pair<std::size_t,std::size_t>> drops;
    for(std::size_t i=0;i<t.items.size();++i){
        if(t.V(i)=="riceOwner"&&(t.V(i+1)=="."||t.V(i+1)=="->")){drops.push_back({t.items[i].from,t.items[i+1].to-t.items[i].from});continue;}
        if(t.V(i)!="("||t.V(i+1)!="co_await")continue;
        auto fn=i+2;if(t.V(fn)=="riceOwner"&&(t.V(fn+1)=="."||t.V(fn+1)=="->"))fn+=2;
        if(!Identifier(t.V(fn))||t.V(fn+1)!="(")continue;
        auto end=t.items[i].mate;
        if(end==missing||t.items[fn+1].mate!=end-1)continue;
        drops.push_back({t.items[i].from,t.items[i+2].from-t.items[i].from});
        drops.push_back({t.items[end].from,t.items[end].to-t.items[end].from});
    }
    std::sort(drops.rbegin(),drops.rend());
    auto out=expression;for(auto [p,n]:drops)out.erase(p,n);return out;
}
std::string Hex(std::string_view s) {
    static constexpr char digits[]="0123456789abcdef";
    std::string out;
    for(unsigned char c:s){out+=digits[c/16];out+=digits[c%16];}
    return out;
}
std::string Unhex(std::string_view s) {
    std::string out;
    for(std::size_t p=0;p+1<s.size();p+=2){
        unsigned x=0;
        if(std::from_chars(s.data()+p,s.data()+p+2,x,16).ec!=std::errc{})return {};
        out+=static_cast<char>(x);
    }
    return out;
}
std::string Mark(const ScriptNode& n) {
    std::string out=std::string(marker)+std::to_string(n.id);
    if(n.isPublic)out+=" public";
    if(n.riceCon)out+=" route="+n.riceScope+","+n.riceLife+","+n.riceAccess+","+Hex(n.policy);
    for(const auto& [name,id]:n.references)out+=" ref="+Hex(name)+":"+std::to_string(id);
    return out+"*/ ";
}
void ReadMark(std::string_view tok,ScriptNode& node) {
    if(!tok.starts_with(marker)||!tok.ends_with("*/"))return;
    auto value=tok.substr(marker.size(),tok.size()-marker.size()-2);
    if(std::from_chars(value.data(),value.data()+value.size(),node.id).ec!=std::errc{})node.id=0;
    node.isPublic=value.find(" public")!=missing;
    if(auto p=value.find(" route=");p!=missing){
        auto route=value.substr(p+7);route=route.substr(0,route.find(' '));
        auto part=[&](){auto j=route.find(',');auto s=route.substr(0,j);route.remove_prefix(j==missing?route.size():j+1);return std::string(s);};
        node.riceCon=true; node.riceScope=part();node.riceLife=part();node.riceAccess=part();node.policy=Unhex(part());
    }
    auto start=value.find(" ref=");
    while(start!=missing){
        auto pair=value.substr(start+5);pair=pair.substr(0,pair.find(' '));
        auto colon=pair.find(':');ScriptId ref=0;
        if(colon!=missing&&std::from_chars(pair.data()+colon+1,pair.data()+pair.size(),ref).ec==std::errc{}&&ref)
            node.references[Unhex(pair.substr(0,colon))]=ref;
        start=value.find(" ref=",start+5);
    }
}
std::string Indent(std::string_view s,std::size_t p) {
    auto line=p?s.rfind('\n',p-1):missing;
    std::size_t i=line==missing?0:line+1, j=i;
    while(j<p&&(s[j]==' '||s[j]=='\t'))++j;
    return std::string(s.substr(i,j-i));
}
std::string DefaultName(NodeKind kind) {
    if(kind==NodeKind::Function)return "NewFunction";
    if(kind==NodeKind::Class)return "NewClass";
    return "variable";
}
}

struct AuthoringParser {
    ScriptDocument& doc;
    CppTokens t;
    std::set<ScriptId> ids;
    std::vector<std::pair<std::size_t,std::string>> duplicateMarkers;
    explicit AuthoringParser(ScriptDocument& d):doc(d),t(d.source_) {}
    std::string_view v(std::size_t p) const {return t.V(p);}
    ScriptId Record(ScriptNode n, std::size_t begin,std::size_t code,std::size_t head,std::size_t end,std::size_t close,bool block) {
        if(begin!=code){
            ScriptNode tag;ReadMark(v(begin),tag);
            n.id=tag.id;n.isPublic=tag.isPublic;n.references=std::move(tag.references);
        }
        if(n.id==0)n.id=Hash(std::to_string(n.parent)+"/"+std::to_string(int(n.kind))+"/"+n.name);
        const auto originalId=n.id;
        while(!ids.insert(n.id).second)n.id=Hash("next",n.id);
        if(originalId!=n.id&&begin!=code)duplicateMarkers.emplace_back(t.items[begin].from,Mark(n));
        n.line=t.Line(t.items[code].from);
        const auto id=n.id;
        doc.nodes_.push_back(std::move(n));
        doc.ranges_.push_back({t.items[begin].from,t.items[code].from,head,end,close,t.Line(end?end-1:0),block});
        return id;
    }
    void ScanBody(std::size_t at,std::size_t limit,ScriptId parent,std::string className);
    void Run();
};

void AuthoringParser::Run() {
    if(t.bad!=missing) {
        doc.parseErrors_.push_back({0,t.Line(t.bad),false,"Unterminated or mismatched C++ delimiter."});
    } else {
        std::string script;
        for(std::size_t i=0;i+3<t.items.size();++i)
            if(v(i)=="RICE_SCRIPT_MODULE"&&v(i+1)=="("&&Identifier(v(i+2))&&v(i+3)==")")script=v(i+2);
        for(std::size_t i=0;i+2<t.items.size();++i){
            if(v(i)!="struct"&&v(i)!="class")continue;
            const std::string name(v(i+1));
            if(!script.empty()&&name!=script)continue;
            bool base=false;
            auto p=i+2;
            while(p<t.items.size()&&v(p)!="{"&&v(p)!=";"){base|=v(p)=="ScriptBase";++p;}
            if(v(p)!="{"||(!base&&script.empty()))continue;
            auto close=t.items[p].mate;
            doc.rootName_=name;
            for(std::size_t j=0;j<i;++j)if(v(j)=="namespace"){
                auto b=j+1;std::string ns;
                while(b<i&&(Identifier(v(b))||v(b)=="::"))ns+=v(b++);
                if(!ns.empty()&&v(b)=="{"&&t.items[b].mate>i)doc.qualifiedName_+=ns+"::";
            }
            doc.qualifiedName_+=name;
            doc.rootOpen_=t.items[p].to;
            doc.rootClose_=t.items[close].from;
            ScanBody(p+1,close,0,name);
            return;
        }
    }
    if(!doc.source_.empty()){
        ScriptNode n;n.id=Hash("opaque-translation-unit");n.name="Custom Code";n.value=doc.source_;
        doc.nodes_.push_back(std::move(n));
        doc.ranges_.push_back({0,0,doc.source_.size(),doc.source_.size(),doc.source_.size(),t.Line(doc.source_.size()),false});
    }
}

void AuthoringParser::ScanBody(std::size_t at,std::size_t limit,ScriptId parent,std::string className) {
    while(at<limit) {
        if(v(at)=="/*rice:traits*/"){
            auto end=at+1;
            while(end<limit&&v(end)!="/*rice:endtraits*/")++end;
            if(end<limit){at=end+1;continue;}
        }
        auto begin=at;
        bool annotated=v(at).starts_with(marker);
        const std::string_view tag=annotated?v(at):std::string_view{};
        if(annotated&&++at==limit)break;
        auto a=at;
        if((v(a)=="public"||v(a)=="private"||v(a)=="protected")&&v(a+1)==":"){at=a+2;continue;}
        if(v(a)==";"){at=a+1;continue;}
        std::size_t p=a;
        bool block=false;
        if(v(a).starts_with("#")){p=a+1;}
        else for(;p<limit;p=t.Next(p)){
            if(v(p)==";"){++p;break;}
            if(v(p)=="{"){
                // Braced initializers terminate in a semicolon just like declarations.
                block=true;break;
            }
        }
        auto open=block?p:missing;
        auto close=block?t.items[open].mate:missing;
        auto stop=block?close+1:p;
        if(block&&v(stop)==";")++stop;
        if(stop<=a||stop>limit)stop=limit;
        at=stop;
        if(tag=="/*rice:reader*/"){
            doc.readerBegin_=t.items[begin].from;doc.readerEnd_=t.items[stop-1].to;continue;
        }
        if(tag=="/*rice:finish*/"||tag=="/*rice:budget*/"||tag=="/*rice:setup*/"||tag=="/*rice:owner*/"||tag=="/*rice:default*/")continue;
        const auto originalStart=a;
        if(v(a)=="riceOwner"&&(v(a+1)=="."||v(a+1)=="->"))a+=2;
        ScriptNode n;
        n.parent=parent;
        n.value=t.Slice(a,stop);
        std::size_t semi=stop;
        if(v(semi-1)==";")--semi;
        auto funcArgs=[&](std::size_t pos){return t.Slice(pos+1,t.items[pos].mate);};
        if(block&&v(a)=="void"&&v(a+1).starts_with("rice_state_")&&v(open+1)=="DefineState"&&v(open+2)=="("){
            auto call=open+2;
            auto params=t.Args(call+1,t.items[call].mate);
            if(params.size()==5){
                n.kind=NodeKind::State;n.name=Unquote(params[0]);n.value=Unquote(params[1]);
                auto id=Record(n,begin,a,t.items[open].from,t.items[stop-1].to,t.items[close].from,true);
                std::size_t index=0;
                for(auto q=call+1;q<t.items[call].mate&&index<3;++q){
                    if(v(q)!="{"||v(q-1)!=")")continue;
                    ScriptNode cb;cb.kind=std::array{NodeKind::OnEnter,NodeKind::OnUpdate,NodeKind::OnExit}[index];
                    cb.name=std::array{"On Enter","On Update","On Exit"}[index++];cb.parent=id;
                    auto end=t.items[q].mate;
                    auto subid=Record(cb,q,q,t.items[q].from,t.items[end].to,t.items[end].from,true);
                    ScanBody(q+1,end,subid,{});
                    q=end;
                }
                continue;
            }
        }
        if((v(a)=="struct"||v(a)=="class")&&block){
            n.kind=NodeKind::Class;n.name=v(a+1);n.value.clear();
        } else if((v(a)=="if"||v(a)=="else")&&block){
            bool cond=v(a)=="if"||v(a+1)=="if";
            n.kind=v(a)=="if"?NodeKind::Condition:cond?NodeKind::ElseIf:NodeKind::Else;
            n.value=cond?funcArgs(a+(v(a)=="if"?1:2)):std::string{};
        } else if((v(a)=="while"||v(a)=="for")&&block&&v(a+1)=="(") {
            if(v(a)=="while"&&Trim(funcArgs(a+1))=="true"){n.kind=NodeKind::Loop;n.value.clear();}
            else if(v(a)=="for"){
                for(auto q=a+2;q+1<open;++q)if(v(q)=="Snapshot"&&v(q+1)=="("){n.kind=NodeKind::Loop;n.value=funcArgs(q+1);break;}
            }
        } else if(block&&((v(a)==className&&v(a+1)=="(")||(v(a)=="explicit"&&v(a+1)==className&&v(a+2)=="("))){
            n.kind=NodeKind::OnCreate;n.name="On Create";n.value.clear();
        } else if(block&&v(a)=="~"&&v(a+1)==className&&v(a+2)=="("){
            n.kind=NodeKind::OnDestroy;n.name="On Destroy";n.value.clear();
        } else if(block&&(v(a)=="Task"||(v(a)=="auto"&&t.Slice(a,open).find("-> Task<")!=missing)||
                  (Identifier(v(a))&&Identifier(v(a+1))&&v(a+2)=="("))){
            std::size_t paren=a;
            while(paren<open&&v(paren)!="(")++paren;
            if(paren<open&&paren>a){
                n.name=v(v(a)=="auto"?a+1:paren-1);
                n.args=funcArgs(paren);
                n.kind=n.name=="Start"&&!parent?NodeKind::Start:n.name=="Update"&&!parent?NodeKind::Update:NodeKind::Function;
                auto temp=a;
                while(temp<open&&v(temp)!="Task")++temp;
                if(temp<open&&v(temp+1)=="<"){
                    auto e=temp+2;
                    while(e<open&&v(e)!=">")++e;
                    n.type=t.Slice(temp+2,e);
                }
                n.coroutine=temp<open;
                if(!n.coroutine){n.type=t.Slice(a,paren-1);n.qualifiers=t.Slice(t.items[paren].mate+1,open);}
                if(n.type.empty())n.type="void";
                n.value.clear();
            }
        } else if(v(a)=="co_return"||v(a)=="return"){
            n.kind=NodeKind::EndFunction;n.value=t.Slice(a+1,semi);
        } else if(v(a)=="break"){n.kind=NodeKind::EndLoop;n.value.clear();}
        else {
            auto fn=a;
            if(v(fn)=="co_await")++fn;
            if(v(fn)=="riceOwner"&&(v(fn+1)=="."||v(fn+1)=="->"))fn+=2;
            const bool call=fn+1<semi&&Identifier(v(fn))&&v(fn+1)=="("&&t.items[fn+1].mate+1==semi;
            if(call){
                auto args=t.Args(fn+2,t.items[fn+1].mate);
                if(v(fn)=="Wait"&&(args.size()==1||args.size()==2)){n.kind=NodeKind::Wait;n.value=args[0];}
                else if(v(fn)=="CheckedEmplace"&&!args.empty()&&Identifier(args[0])){
                    n.kind=NodeKind::NewClass;n.name=args[0];n.value.clear();
                    if(args.size()>1&&args[1]!="*this"&&args[1]!="*riceOwner")n.args=t.Slice(fn+4,t.items[fn+1].mate);
                    for(const auto& prev:doc.nodes_)if(prev.kind==NodeKind::Variable&&prev.name==n.name&&prev.type.starts_with("std::optional<"))n.type=prev.type.substr(14,prev.type.size()-15);
                }
                else if(v(fn)=="Activate"&&args.size()==3&&v(fn+2).starts_with("\"")){
                    n.kind=NodeKind::Activate;n.name=Unquote(args[0]);n.type=Unquote(args[1]);n.value=args[2];
                } else if((v(fn)=="SetState"||v(fn)=="ChangeState")&&args.size()==2){
                    n.kind=v(fn)=="SetState"?NodeKind::SetState:NodeKind::ChangeState;
                    n.name=Unquote(args[0]);n.value=Unquote(args[1]);
                } else if(v(fn)=="RunNow"&&args.size()==1){
                    auto target=fn+2;if(v(target)=="riceOwner"&&(v(target+1)=="."||v(target+1)=="->"))target+=2;
                    if(Identifier(v(target))&&v(target+1)=="("){n.kind=NodeKind::Call;n.name=v(target);n.value.clear();n.args=funcArgs(target+1);}
                } else if(v(a)=="co_await"||std::any_of(doc.nodes_.begin(),doc.nodes_.end(),[&](const auto& declared){
                    return declared.kind==NodeKind::Function&&declared.name==v(fn);
                })){n.kind=NodeKind::Call;n.name=v(fn);n.value.clear();n.args=funcArgs(fn+1);}
            } else if(Identifier(v(a))&&v(a+1)=="."&&v(a+2)=="emplace"&&v(a+3)=="("){
                n.kind=NodeKind::NewClass;n.name=v(a);n.args=funcArgs(a+3);n.value.clear();
                if(n.args=="*this"||n.args=="riceOwner"||n.args=="*riceOwner")n.args.clear();
                for(auto& prev:doc.nodes_)if(prev.kind==NodeKind::Variable&&prev.name==n.name&&prev.type.starts_with("std::optional<"))n.type=prev.type.substr(14,prev.type.size()-15);
            } else if(Identifier(v(a))&&(v(a+1)=="="||v(a+1)=="+="||v(a+1)=="-="||v(a+1)=="*="||v(a+1)=="/=")){
                n.kind=NodeKind::Change;n.name=v(a);n.args=v(a+1);n.value=t.Slice(a+2,semi);
            } else {
                auto eq=a;
                while(eq<semi&&v(eq)!="="&&v(eq)!="{")eq=t.Next(eq);
                auto name=eq>0?eq-1:0;
                bool plausible=name>a&&Identifier(v(name))&&Identifier(v(a));
                // Only recognize declarators that this editor can reconstruct.
                for(auto k=a;plausible&&k<name;++k){
                    auto s=v(k);
                    plausible=Identifier(s)||s=="::"||s=="<"||s==">"||s==",";
                }
                if(plausible){
                    n.kind=NodeKind::Variable;n.name=v(name);n.type=t.Slice(a,name);
                    n.value=eq==semi?std::string{}:v(eq)=="{"?t.Slice(eq,semi):t.Slice(eq+1,semi);
                    if(n.type.starts_with("Routed<")&&n.type.ends_with(">")&&v(eq)=="{"){
                        auto args=t.Args(eq+1,t.items[eq].mate);
                        if(args.size()==3){
                            n.type=n.type.substr(7,n.type.size()-8);n.value=args[2];n.riceCon=true;
                            CppTokens fields(args[1]);
                            if(fields.V(0)=="{"&&fields.items[0].mate!=missing){
                                auto values=fields.Args(1,fields.items[0].mate);
                                if(values.size()==6){
                                    n.riceScope=Unquote(values[2]);n.riceLife=Unquote(values[3]);
                                    n.riceAccess=Unquote(values[4]);n.policy=Unquote(values[5]);
                                }
                            }
                        }
                    }
                }
            }
        }
        if(n.kind==NodeKind::Custom){n.name="Custom Code";n.value=t.Slice(a,stop);}
        else{
            if(n.kind==NodeKind::Change&&n.value.starts_with("CheckedInt(")&&n.value.ends_with(")")){
                CppTokens expr(n.value);
                if(expr.items.size()>=3&&expr.items[1].mate==expr.items.size()-1)n.value=expr.Slice(2,expr.items.size()-1);
            }
            n.value=PlainExpression(n.value);
            if(n.kind==NodeKind::Call||n.kind==NodeKind::NewClass)n.args=PlainExpression(n.args);
        }
        bool hasBody=block&&Owner(n.kind);
        auto childrenEnd=hasBody?t.items[close].from:t.items[stop-1].to;
        auto head=hasBody?t.items[open].from:t.items[stop-1].to;
        auto id=Record(n,begin,originalStart,head,t.items[stop-1].to,childrenEnd,hasBody);
        if(hasBody){
            if(n.kind==NodeKind::OnDestroy&&v(open+1)=="riceOwner"&&v(open+2)=="->"&&v(open+3)=="SafeDestroy"&&v(open+4)=="("){
                auto lambda=open+5;
                while(lambda<close&&v(lambda)!="{")++lambda;
                if(lambda<close&&t.items[lambda].mate<close){
                    open=lambda;close=t.items[lambda].mate;
                    doc.ranges_[doc.Index(id)].childrenEnd=t.items[close].from;
                }
            }
            ScanBody(open+1,close,id,n.kind==NodeKind::Class?n.name:std::string{});
            // New declarations/actions must go before the implicit coroutine return.
            if(Returning(n.kind))
                for(auto k=open+1;k<close;k=t.Next(k)){
                    if(v(k)=="/*rice:finish*/"){doc.ranges_[doc.Index(id)].childrenEnd=t.items[k].from;break;}
                }
        }
    }
}

namespace {
std::string Type(const ScriptNode& n){return n.type.empty()?"void":n.type;}
std::string Header(const ScriptNode& n,const ScriptNode* parent,std::string_view rootName={}) {
    switch(n.kind){
    case NodeKind::Class:return "struct "+n.name;
    case NodeKind::State:return "void rice_state_"+std::to_string(n.id)+"()";
    case NodeKind::OnCreate:return parent?parent->name+(rootName.empty()?"()":"("+std::string(rootName)+"& riceSource) : riceOwner(&riceSource)"):"";
    case NodeKind::OnDestroy:return parent?"~"+parent->name+"()":"";
    case NodeKind::Function:
    case NodeKind::Start:
    case NodeKind::Update:{
        std::string result=n.coroutine?"Task<"+(Type(n)=="void"?std::string{}:Type(n))+">":Type(n);
        if(n.kind==NodeKind::Function&&parent&&parent->kind!=NodeKind::Class)
            return "auto "+n.name+" = [this]("+n.args+") -> "+result;
        return result+" "+n.name+"("+n.args+")"+(n.qualifiers.empty()?"":" "+n.qualifiers);
    }
    case NodeKind::Condition:return "if ("+n.value+")";
    case NodeKind::ElseIf:return "else if ("+n.value+")";
    case NodeKind::Else:return "else";
    case NodeKind::Loop:return n.value.empty()?"while (true)":"for (auto item : Snapshot("+n.value+"))";
    default:return {};
    }
}
std::string Statement(const ScriptNode& n) {
    switch(n.kind){
    case NodeKind::Variable:
        if(n.riceCon)return "Routed<"+n.type+"> "+n.name+"{*this, {"+Quote(std::to_string(n.id))+", "+Quote(n.name)+", "+Quote(n.riceScope)+", "+Quote(n.riceLife)+", "+Quote(n.riceAccess)+", "+Quote(n.policy)+"}, "+(n.value.empty()?n.type+"{}":n.value)+"};";
        return n.type+" "+n.name+(n.value.empty()?"{}":" = "+n.value)+";";
    case NodeKind::Change:return n.name+" "+(n.args.empty()?"=":n.args)+" "+n.value+";";
    case NodeKind::Call:return "co_await "+n.name+"("+n.args+");";
    case NodeKind::Activate:return "Activate("+Quote(n.name)+", "+Quote(n.type)+", "+n.value+");";
    case NodeKind::SetState:case NodeKind::ChangeState:
        return std::string(n.kind==NodeKind::SetState?"SetState(":"ChangeState(")+Quote(n.name)+", "+Quote(n.value)+");";
    case NodeKind::Wait:return "co_await Wait("+n.value+", "+std::to_string(n.id)+"ULL);";
    case NodeKind::EndFunction:return "co_return"+(n.value.empty()?std::string{}:" "+n.value)+";";
    case NodeKind::EndLoop:return "break;";
    case NodeKind::Custom:return n.value;
    case NodeKind::NewClass:return "CheckedEmplace("+n.name+(n.args.empty()?"":", "+n.args)+");";
    default:return {};
    }
}
std::string DefaultCtor(const std::string& name,const std::string& rootName){
    return "/*rice:default*/ "+name+"("+rootName+"& riceSource) : riceOwner(&riceSource) {}";
}
std::string BuildItem(const ScriptNode& n,const ScriptNode* parent,const std::string& pad,const std::string& rootName) {
    if(!Owner(n.kind))return Mark(n)+Statement(n);
    std::string out=Mark(n)+Header(n,parent,rootName)+" {\n";
    auto inner=pad+"    ";
    if(n.kind==NodeKind::Class)out+=inner+"/*rice:owner*/ "+rootName+"* riceOwner = nullptr;\n"+inner+DefaultCtor(n.name,rootName)+"\n"+inner+"/*rice:traits*/ /*rice:endtraits*/\n";
    if(n.kind==NodeKind::OnDestroy&&!rootName.empty())out+=inner+"riceOwner->SafeDestroy([&](){\n"+inner+"});\n";
    if(n.kind==NodeKind::Loop)out+=inner+"/*rice:budget*/ co_await Checkpoint();\n";
    if(Returning(n.kind)&&n.coroutine&&Type(n)=="void")out+=inner+"/*rice:finish*/ co_return;\n";
    if(n.kind==NodeKind::State)out+=inner+"DefineState("+Quote(n.name)+", "+Quote(n.value)+", [this](){\n"+inner+"}, [this](){\n"+inner+"}, [this](){\n"+inner+"});\n";
    out+=pad+"}";
    if(n.kind==NodeKind::Class||(n.kind==NodeKind::Function&&parent&&parent->kind!=NodeKind::Class))out+=";";
    return out;
}
std::size_t OnlyIndentFrom(std::string_view source,std::size_t pos) {
    auto prev=pos?source.rfind('\n',pos-1):missing;
    std::size_t start=prev==missing?0:prev+1;
    if(std::all_of(source.begin()+start,source.begin()+pos,Space))return start;
    return pos;
}
bool Exportable(std::string_view t) {
    CppTokens tokens(t);
    static const std::unordered_set<std::string_view> parts{
        "int","float","double","bool","Vec2","Vec3","std","::","string","vector","unordered_map","<",">",","
    };
    return !tokens.items.empty()&&std::all_of(tokens.items.begin(),tokens.items.end(),[&](const auto& tok){return parts.contains(tok.value);});
}
}

ScriptDocument ScriptDocument::New() {
    ScriptDocument d;
    d.SetSource(
        "#include \"Scripting/ScriptRuntime.h\"\n"
        "#include <optional>\n#include <unordered_map>\n#include <vector>\n"
        "using namespace Rice::Scripting;\n\n"
        "struct GameScript : ScriptBase {\n"
        "    Task<> Start() {\n        /*rice:setup*/ RiceConfigureStates();\n        /*rice:finish*/ co_return;\n    }\n"
        "    Task<> Update() {\n        /*rice:finish*/ co_return;\n    }\n"
        "    /*rice:reader*/ bool Read(const char*, Value&) const { return false; }\n"
        "    /*rice:setup*/ void RiceConfigureStates() {}\n"
        "};\nRICE_SCRIPT_MODULE(GameScript)\n");
    return d;
}
void ScriptDocument::SetSource(std::string source) {
    source_=std::move(source);nodes_.clear();ranges_.clear();parseErrors_.clear();
    rootOpen_=rootClose_=readerBegin_=readerEnd_=missing;rootName_.clear();qualifiedName_.clear();
    AuthoringParser parser(*this);parser.Run();
    if(!parser.duplicateMarkers.empty()){
        auto fixed=source_;
        for(auto i=parser.duplicateMarkers.rbegin();i!=parser.duplicateMarkers.rend();++i){
            auto end=fixed.find("*/",i->first);
            fixed.replace(i->first,end+2-i->first,i->second.substr(0,i->second.size()-1));
        }
        SetSource(std::move(fixed));
    }
}
std::size_t ScriptDocument::Index(ScriptId id) const {
    auto it=std::find_if(nodes_.begin(),nodes_.end(),[&](const auto& n){return n.id==id;});
    return it==nodes_.end()?missing:static_cast<std::size_t>(it-nodes_.begin());
}
bool ScriptDocument::HasModuleEntry() const {
    CppTokens t(source_);
    for(std::size_t i=0;i<t.items.size();++i)
        if((t.V(i)=="RICE_SCRIPT_MODULE"||t.V(i)=="RiceGetScriptApi")&&t.V(i+1)=="(")return true;
    return false;
}
bool ScriptDocument::SameType(std::string_view a,std::string_view b){
    CppTokens left(a),right(b);
    if(left.items.size()!=right.items.size())return false;
    for(std::size_t i=0;i<left.items.size();++i)if(left.V(i)!=right.V(i))return false;
    return true;
}
namespace {
bool DecodeString(std::string_view token,std::string& result){
    if(token.starts_with("R\"")&&token.ends_with('"')){
        auto p=token.find('(');
        if(p==missing)return false;
        const auto suffix=p; // closing parenthesis, same delimiter, and quote
        if(token.size()<p+1+suffix)return false;
        result.append(token.substr(p+1,token.size()-p-1-suffix));return true;
    }
    if(token.size()<2||token.front()!='"'||token.back()!='"')return false;
    token.remove_prefix(1);token.remove_suffix(1);
    for(std::size_t i=0;i<token.size();++i){
        if(token[i]!='\\'){result+=token[i];continue;}
        if(++i>=token.size())return false;
        char ch=token[i];
        constexpr std::string_view escapes="abfnrtv\\\'\"?";
        constexpr std::string_view decoded="\a\b\f\n\r\t\v\\\'\"?";
        if(auto p=escapes.find(ch);p!=missing){result+=decoded[p];continue;}
        if(ch=='\n')continue;
        if(ch=='\r'&&i+1<token.size()&&token[i+1]=='\n'){++i;continue;}
        if(ch=='u'||ch=='U'){
            const std::size_t digits=ch=='u'?4:8;
            if(i+digits>=token.size())return false;
            std::uint32_t cp=0;auto last=token.data()+i+1+digits;
            auto read=std::from_chars(token.data()+i+1,last,cp,16);
            if(read.ec!=std::errc{}||read.ptr!=last||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return false;
            i+=digits;
            if(cp<=0x7f){result+=static_cast<char>(cp);continue;}
            unsigned part=cp<=0x7ff?1:cp<=0xffff?2:3;
            result+=static_cast<char>((part==1?0xc0:part==2?0xe0:0xf0)|(cp>>(6*part)));
            while(part--)result+=static_cast<char>(0x80|((cp>>(6*part))&0x3f));
            continue;
        }
        auto start=i;
        int base=8;
        if(ch=='x'){base=16;++start;}
        else if(ch<'0'||ch>'7')return false;
        auto end=start;
        while(end<token.size()&&(base==16?std::isxdigit(static_cast<unsigned char>(token[end]))!=0:
                    token[end]>='0'&&token[end]<='7'&&end-start<3))++end;
        unsigned code=0;
        if(start==end||std::from_chars(token.data()+start,token.data()+end,code,base).ec!=std::errc{}||code>255)return false;
        result+=static_cast<char>(code);i=end-1;
    }
    return true;
}
}
std::vector<ScriptChildReference> ScriptDocument::ChildReferences(std::string_view source){
    std::vector<ScriptChildReference> result;
    if(source.find("ReadChild")==missing)return result;
    CppTokens t(source);
    for(std::size_t i=0;i<t.items.size();++i){
        if(t.V(i)!="ReadChild"||t.V(i+1)!="<")continue;
        auto p=i+2;unsigned depth=1;
        for(;p<t.items.size();++p){
            if(t.V(p)=="<")++depth;
            else if(t.V(p)==">"&&!--depth)break;
            else if(t.V(p)==";"||t.V(p)=="{")break;
        }
        if(depth||p==i+2||t.V(p+1)!="("||t.items[p+1].mate==missing)continue;
        auto key=p+2,comma=key;
        auto close=t.items[p+1].mate;
        while(comma<close&&t.V(comma)!=",")++comma;
        if(comma==key||comma+1>=close)continue;
        ScriptChildReference ref;
        auto literal=[&](auto first,auto last,std::string& out){
            for(auto q=first;q<last;++q)if(!DecodeString(t.V(q),out))return false;
            return true;
        };
        if(!literal(key,comma,ref.key)||!literal(comma+1,close,ref.name))continue;
        ref.type=t.Slice(i+2,p);
        ref.line=t.Line(t.items[i].from);
        result.push_back(std::move(ref));
    }
    return result;
}
int ScriptDocument::FreeHookArity(std::string_view name) const{
    CppTokens t(source_);
    for(std::size_t i=0;i<t.items.size();i=t.Next(i)){
        if(t.V(i)!="void"||t.V(i+1)!=name||t.V(i+2)!="(")continue;
        auto end=t.items[i+2].mate;
        if(end==missing)continue;
        auto body=end+1;
        if(t.V(body)=="noexcept")++body;
        if(t.V(body)!="{")continue;
        if(end==i+3||(end==i+4&&t.V(i+3)=="void"))return 0;
        if(name=="Update"&&t.V(i+3)=="float"&&(end==i+4||(end==i+5&&Identifier(t.V(i+4)))))return 1;
    }
    return -1;
}
const ScriptNode* ScriptDocument::Get(ScriptId id) const {auto i=Index(id);return i==missing?nullptr:&nodes_[i];}
std::string ScriptDocument::Text(ScriptId id) const {
    auto i=Index(id);return i==missing?std::string{}:source_.substr(ranges_[i].begin,ranges_[i].end-ranges_[i].begin);
}
ScriptId ScriptDocument::AtLine(std::size_t line) const {
    ScriptId hit=0;
    for(std::size_t i=0;i<nodes_.size();++i)if(nodes_[i].line<=line&&line<=ranges_[i].lastLine)hit=nodes_[i].id;
    return hit;
}
bool ScriptDocument::Within(ScriptId node,ScriptId owner) const {
    while(node&&node!=owner){auto* n=Get(node);if(!n)return false;node=n->parent;}
    return node==owner;
}
bool ScriptDocument::CanSee(ScriptId symbol,ScriptId owner) const {
    auto* decl=Get(symbol);
    return decl&&Decl(decl->kind)&&Within(owner,decl->parent);
}
void ScriptDocument::TrackReferences(ScriptNode& n) const{
    auto fields=References(n);
    auto used=[&](std::string_view name){
        return std::any_of(fields.begin(),fields.end(),[&](auto field){return HasToken(field,name);});
    };
    std::erase_if(n.references,[&](const auto& ref){return !used(ref.first);});
    for(const auto& decl:nodes_)
        if(decl.id!=n.id&&CanSee(decl.id,n.parent)&&!n.references.contains(decl.name)&&used(decl.name))
            n.references.emplace(decl.name,decl.id);
}
std::string ScriptDocument::AwaitCalls(std::string text, ScriptId owner) const {
    bool coroutine=false;
    for(auto* n=Get(owner);n;n=Get(n->parent)){
        if(Returning(n->kind)){coroutine=n->coroutine;break;}
        if(StateCallback(n->kind)||Callback(n->kind)||n->kind==NodeKind::Class)break;
    }
    if(!coroutine)return text;
    CppTokens t(text);
    std::vector<std::pair<std::size_t,std::string>> insertions;
    for(std::size_t i=0;i+1<t.items.size();++i){
        if(t.V(i+1)!="("||!Identifier(t.V(i))||t.items[i+1].mate==missing)continue;
        if(i&&(t.V(i-1)=="co_await"||t.V(i-1)=="."||t.V(i-1)=="::"||t.V(i-1)=="->"))continue;
        bool known=std::any_of(nodes_.begin(),nodes_.end(),[&](const auto& f){return f.coroutine&&f.kind==NodeKind::Function&&f.name==t.V(i)&&CanSee(f.id,owner);});
        if(!known)continue;
        insertions.push_back({t.items[i].from,"(co_await "});
        insertions.push_back({t.items[t.items[i+1].mate].to,")"});
    }
    std::sort(insertions.rbegin(),insertions.rend());
    for(auto& [pos,prefix]:insertions)text.insert(pos,prefix);
    return text;
}
bool ScriptDocument::Synchronous(ScriptId owner) const{
    for(auto* n=Get(owner);n;n=Get(n->parent)){
        if(Returning(n->kind))return !n->coroutine;
        if(Callback(n->kind)||StateCallback(n->kind))return true;
    }
    return false;
}
std::string ScriptDocument::BindOwner(const std::string& text,ScriptId owner) const{
    bool inClass=false;
    for(auto* n=Get(owner);n;n=Get(n->parent))
        if(n->kind==NodeKind::Class){inClass=Text(n->id).find("/*rice:owner*/")!=missing;break;}
    if(!inClass)return text;
    static const std::unordered_set<std::string_view> sdk{
        "Activate","Active","ReadChild","Wait","Checkpoint","SetState","ChangeState","State","Physics","DeltaTime","RunNow"
    };
    CppTokens t(text);auto out=text;
    for(auto i=t.items.size();i>0;--i){
        auto tok=t.V(i-1);
        if(i>=2&&(t.V(i-2)=="."||t.V(i-2)=="::"||t.V(i-2)=="->"))continue;
        if(!Identifier(tok))continue;
        bool field=sdk.contains(tok)||std::any_of(nodes_.begin(),nodes_.end(),[&](const auto& n){
            return !n.parent&&(n.kind==NodeKind::Variable||n.kind==NodeKind::Function)&&n.name==tok;
        });
        if(field)out.insert(t.items[i-1].from,"riceOwner->");
    }
    return out;
}
bool ScriptDocument::ValueType(std::string_view type,std::vector<ScriptId>& visiting) const{
    if(Exportable(type))return true;
    CppTokens tokens(type);
    if(tokens.items.empty())return false;
    for(const auto& token:tokens.items){
        if(Exportable(token.value))continue;
        auto cls=std::find_if(nodes_.begin(),nodes_.end(),[&](const auto& n){return n.kind==NodeKind::Class&&n.name==token.value;});
        if(cls==nodes_.end()||Text(cls->id).find("/*rice:traits*/")==missing)return false;
        if(std::find(visiting.begin(),visiting.end(),cls->id)!=visiting.end())return false;
        visiting.push_back(cls->id);
        for(const auto& member:nodes_)if(member.parent==cls->id){
            if(member.kind!=NodeKind::Variable||member.riceCon||!ValueType(member.type,visiting))return false;
        }
        visiting.pop_back();
    }
    return true;
}
bool ScriptDocument::CanRead(ScriptId variable) const{
    auto* n=Get(variable);std::vector<ScriptId> visiting;
    return n&&!n->parent&&n->kind==NodeKind::Variable&&ValueType(n->type,visiting);
}
void ScriptDocument::RefreshTraits(){
    struct Change {std::size_t from,length;std::string text;};
    std::vector<Change> changes;
    for(std::size_t i=0;i<nodes_.size();++i){
        const auto& cls=nodes_[i];
        if(cls.kind!=NodeKind::Class)continue;
        constexpr std::string_view markerBegin="/*rice:traits*/",markerEnd="/*rice:endtraits*/";
        auto from=source_.find(markerBegin,ranges_[i].code);
        if(from==missing||from>=ranges_[i].end)continue;
        // A hand-written enclosing class may contain a generated nested class.
        bool nested=false;for(std::size_t j=0;j<nodes_.size();++j)
            if(nodes_[j].parent==cls.id&&ranges_[j].begin<=from&&from<ranges_[j].end)nested=true;
        if(nested)continue;
        from+=markerBegin.size();auto to=source_.find(markerEnd,from);
        if(to==missing||to>=ranges_[i].end)continue;
        std::string reflect=" ";
        std::vector<ScriptId> visiting;
        if(ValueType(cls.name,visiting)){
            std::string members,signature=cls.name;
            for(auto& field:nodes_)if(field.parent==cls.id&&field.kind==NodeKind::Variable){
                if(!members.empty())members+=", ";
                members+=field.name;signature+="/"+field.name+":"+field.type;
            }
            auto pad=Indent(source_,from-markerBegin.size());
            reflect="\n"+pad+cls.name+"() = default;\n"+pad+"static constexpr std::string_view RiceType = "+Quote(signature)+";\n"+
                pad+"auto RiceFields() { return std::tie("+members+"); }\n"+pad+"auto RiceFields() const { return std::tie("+members+"); }\n"+pad;
        }
        if(source_.compare(from,to-from,reflect)!=0)changes.push_back({from,to-from,std::move(reflect)});
    }
    if(changes.empty())return;
    std::sort(changes.begin(),changes.end(),[](auto& a,auto& b){return a.from>b.from;});
    auto s=source_;for(auto& x:changes)s.replace(x.from,x.length,x.text);SetSource(std::move(s));
}
void ScriptDocument::RefreshReader(){
    RefreshTraits();
    if(readerBegin_==missing)return;
    auto indentation=Indent(source_,readerBegin_);
    std::string read="/*rice:reader*/ bool Read(const char* key, Value& out) const {\n";
    std::string states;
    for(auto& n:nodes_)if(!n.parent){
        if(CanRead(n.id)&&Identifier(n.name))
            read+=indentation+"    if (std::string_view(key) == "+Quote(n.name)+") { out = ToValue("+n.name+(n.riceCon?".Get()":"")+"); return true; }\n";
        if(n.kind==NodeKind::State)states+="rice_state_"+std::to_string(n.id)+"(); ";
    }
    read+=indentation+"    return false;\n"+indentation+"}";
    std::string s=source_;
    // This exact marker belongs to generated glue. Never search/replace user code by name.
    constexpr std::string_view sig="/*rice:setup*/ void RiceConfigureStates() {";
    auto p=s.find(sig);
    if(p!=missing){
        auto from=p+sig.size(),end=s.find('}',from);
        if(end!=missing&&p>=readerEnd_)s.replace(from,end-from,states);
    }
    s.replace(readerBegin_,readerEnd_-readerBegin_,read);
    if(s!=source_)SetSource(std::move(s));
}
bool ScriptDocument::Insert(ScriptNode n,std::string text){
    if(!HasScriptBody())return false;
    const auto parentIndex=Index(n.parent);
    auto* parent=Get(n.parent);
    if(n.parent&&(!parent||!ranges_[parentIndex].block))return false;
    if(parent&&parent->kind==NodeKind::State)return false;
    bool member=!parent||parent->kind==NodeKind::Class;
    if(member&&!(Decl(n.kind)||Callback(n.kind)||n.kind==NodeKind::Custom||n.kind==NodeKind::State))return false;
    auto end=n.parent?ranges_[parentIndex].childrenEnd:rootClose_;
    if(Decl(n.kind)){
        for(std::size_t i=0;i<nodes_.size();++i)if(nodes_[i].parent==n.parent&&!Decl(nodes_[i].kind)){
            end=ranges_[i].begin;break;
        }
    }
    auto base=n.parent?Indent(source_,ranges_[parentIndex].begin):Indent(source_,rootClose_);
    auto pad=base+"    ";
    auto loc=OnlyIndentFrom(source_,end);
    if(text.empty()){
        TrackReferences(n);
        if(n.kind==NodeKind::NewClass){
            auto variable=std::find_if(nodes_.begin(),nodes_.end(),[&](const auto& x){return x.kind==NodeKind::Class&&x.name==n.type;});
            if(variable!=nodes_.end()&&Text(variable->id).find("/*rice:owner*/")!=missing){
                bool memberContext=false;for(auto* context=parent;context;context=Get(context->parent))if(context->kind==NodeKind::Class)memberContext=true;
                n.args=memberContext?"*riceOwner":"*this";
            }
        }
        auto useOwner=(n.kind!=NodeKind::OnCreate&&n.kind!=NodeKind::OnDestroy)||!parent||Text(parent->id).find("/*rice:owner*/")!=missing;
        text=BuildItem(n,parent,pad,useOwner?rootName_:std::string{});
        if(n.kind==NodeKind::Call){
            auto target=std::find_if(nodes_.begin(),nodes_.end(),[&](const auto& fn){return fn.kind==NodeKind::Function&&fn.name==n.name&&CanSee(fn.id,n.parent);});
            auto call=n.name+"("+n.args+")";
            if(target!=nodes_.end()&&!target->coroutine)text=Mark(n)+call+";";
            else if(Synchronous(n.parent))text=Mark(n)+"RunNow("+call+");";
        }
        if(n.kind==NodeKind::EndFunction&&Synchronous(n.parent))text=Mark(n)+"return"+(n.value.empty()?"":" "+n.value)+";";
        if(!Decl(n.kind)&&!Callback(n.kind))text=BindOwner(text,n.parent);
    }
    std::string change=(loc==end&&(loc>0&&source_[loc-1]!='\n')?"\n":"")+pad+text+"\n";
    auto src=source_;src.insert(loc,change);
    if(n.kind==NodeKind::OnCreate&&parent){
        auto oldCtor=DefaultCtor(parent->name,rootName_);
        auto old=src.find(oldCtor,ranges_[parentIndex].code);
        if(old!=missing&&old<ranges_[parentIndex].end+change.size())src.erase(old,oldCtor.size());
    }
    SetSource(std::move(src));
    return Get(n.id)!=nullptr;
}
ScriptId ScriptDocument::Add(ScriptId parent,NodeKind kind,std::string name) {
    if((kind==NodeKind::ElseIf||kind==NodeKind::Else)&&Get(parent)&&Branch(Get(parent)->kind))
        return AddBranch(parent,kind);
    if((kind==NodeKind::OnCreate||kind==NodeKind::OnDestroy)&&(!Get(parent)||Get(parent)->kind!=NodeKind::Class))return 0;
    for(auto& n:nodes_)if(n.parent==parent&&n.kind==kind&&(Callback(kind)||StateCallback(kind)))return n.id;
    if(StateCallback(kind))return 0;
    ScriptNode n;n.id=FreshId();n.parent=parent;n.kind=kind;n.name=std::move(name);
    if(Decl(kind)&&n.name.empty()){
        std::string base=DefaultName(kind);n.name=base;
        for(unsigned i=2;std::any_of(nodes_.begin(),nodes_.end(),[&](const auto& x){return Decl(x.kind)&&x.name==n.name;});++i)
            n.name=base+std::to_string(i);
    }
    auto visible=[&](NodeKind k)->const ScriptNode*{
        for(auto& x:nodes_)if(x.kind==k&&CanSee(x.id,parent))return &x;
        return nullptr;
    };
    switch(kind){
    case NodeKind::Variable:n.type="int";n.value="0";break;
    case NodeKind::Function:n.type="void";break;
    case NodeKind::Condition:case NodeKind::ElseIf:n.value="true";break;
    case NodeKind::Change:if(n.name.empty()){auto* x=visible(NodeKind::Variable);n.name=x?x->name:"missingVariable";}n.value="0";n.args="=";break;
    case NodeKind::Call:if(n.name.empty()){auto* x=visible(NodeKind::Function);n.name=x?x->name:"missingFunction";}break;
    case NodeKind::Activate:n.value="true";break;
    case NodeKind::Wait:n.value="1.0";break;
    case NodeKind::Custom:n.value="/* Custom Code */ (void)0;";break;
    case NodeKind::State:case NodeKind::SetState:case NodeKind::ChangeState:if(n.name.empty())n.name="Mode";n.value="Idle";break;
    case NodeKind::NewClass:{
        auto* klass=visible(NodeKind::Class);if(!klass)return 0;
        n.type=klass->name;
        ScriptId owner=parent;
        while(auto* ancestor=Get(owner)){
            if(Returning(ancestor->kind)||Callback(ancestor->kind)||ancestor->kind==NodeKind::Class)break;
            owner=ancestor->parent;
        }
        auto id=Add(owner,NodeKind::Variable,n.name);
        if(!id)return 0;
        ScriptNode storage=*Get(id);
        n.name=storage.name;storage.type="std::optional<"+n.type+">";storage.value="{}";
        Edit(id,storage);
        break;
    }
    case NodeKind::Start:n.name="Start";n.type="void";break;
    case NodeKind::Update:n.name="Update";n.type="void";break;
    default:break;
    }
    if(!Insert(n))return 0;
    if(Decl(kind)||Callback(kind)||kind==NodeKind::State||kind==NodeKind::Custom)RefreshReader();
    return n.id;
}
ScriptId ScriptDocument::AddBranch(ScriptId branch,NodeKind kind){
    auto i=Index(branch);
    if(i==missing||!Branch(nodes_[i].kind)||(kind!=NodeKind::Else&&kind!=NodeKind::ElseIf))return 0;
    auto owner=nodes_[i].parent;
    auto pos=ranges_[i].end;
    bool after=true;
    for(auto j=i;j<nodes_.size();++j){
        if(nodes_[j].parent!=owner)continue;
        if(j!=i&&nodes_[j].kind!=NodeKind::ElseIf&&nodes_[j].kind!=NodeKind::Else)break;
        if(nodes_[j].kind==NodeKind::Else){
            if(kind==NodeKind::Else)return nodes_[j].id;
            pos=ranges_[j].begin;after=false;break;
        }
        pos=ranges_[j].end;
    }
    ScriptNode b;b.id=FreshId();b.parent=owner;b.kind=kind;
    if(kind==NodeKind::ElseIf)b.value="true";
    auto pad=Indent(source_,ranges_[i].begin);
    auto newItem=BindOwner(BuildItem(b,Get(owner),pad,rootName_),owner);
    auto s=source_;s.insert(pos,after?"\n"+pad+newItem:newItem+"\n"+pad);
    SetSource(std::move(s));return Get(b.id)?b.id:0;
}
bool ScriptDocument::ImportClass(const ScriptDocument& source,ScriptId classId){
    auto* raw=source.Get(classId);if(!raw||raw->kind!=NodeKind::Class)return false;
    auto plain=[](std::string_view text,std::string_view script){
        CppTokens t(text);std::string out;
        for(const auto& tok:t.items)if(!tok.value.starts_with(marker)){out+=tok.value==script?"$script":tok.value;out+=' ';}
        return out;
    };
    auto body=source.Text(classId);
    for(const auto& n:nodes_)if(Decl(n.kind)&&n.name==raw->name)
        return n.kind==NodeKind::Class&&plain(Text(n.id),rootName_)==plain(body,source.rootName_);
    ScriptNode copy=*raw;copy.id=FreshId();copy.parent=0;
    const auto original=body;
    CppTokens tokens(original);
    std::unordered_map<ScriptId,ScriptId> remap;
    for(const auto& tok:tokens.items)if(tok.value.starts_with(marker)){
        ScriptNode tag;ReadMark(tok.value,tag);if(tag.id)remap[tag.id]=tag.id==classId?copy.id:FreshId();
    }
    for(std::size_t i=tokens.items.size();i>0;--i){
        const auto& tok=tokens.items[i-1];
        if(tok.value==source.rootName_&&tok.value!=rootName_){
            body.replace(tok.from,tok.to-tok.from,rootName_);continue;
        }
        if(!tok.value.starts_with(marker))continue;
        ScriptNode tagged;ReadMark(tok.value,tagged);
        if(!tagged.id)continue;
        tagged.id=(i==1)?copy.id:remap.at(tagged.id);
        std::erase_if(tagged.references,[&](const auto& p){return !remap.contains(p.second);});
        for(auto& [_,ref]:tagged.references)ref=remap.at(ref);
        auto tag=Mark(tagged);tag.pop_back();body.replace(tok.from,tok.to-tok.from,tag);
    }
    if(!body.starts_with(marker))body=Mark(copy)+body;
    if(!Insert(copy,body))return false;
    RefreshReader();return true;
}
bool ScriptDocument::Edit(ScriptId id,const ScriptNode& next){
    auto i=Index(id);if(i==missing)return false;
    auto n=next;auto old=nodes_[i];n.id=id;n.parent=old.parent;
    if(n.kind!=old.kind)return false;
    TrackReferences(n);
    if(n.kind==NodeKind::NewClass&&(n.name!=old.name||n.type!=old.type)){
        auto storage=std::find_if(nodes_.begin(),nodes_.end(),[&](const auto& field){
            return field.kind==NodeKind::Variable&&field.name==old.name&&field.type=="std::optional<"+old.type+">"&&CanSee(field.id,n.parent);
        });
        if(storage!=nodes_.end()){
            auto variable=*storage;
            variable.name=n.name;variable.type="std::optional<"+n.type+">";
            if(!Edit(variable.id,variable))return false;
            i=Index(id);if(i==missing)return false;
        }
    }
    if(n.kind==NodeKind::Change||n.kind==NodeKind::Condition||n.kind==NodeKind::ElseIf||n.kind==NodeKind::EndFunction||n.kind==NodeKind::Wait||n.kind==NodeKind::Activate)
        n.value=AwaitCalls(n.value,n.parent);
    if(n.kind==NodeKind::Call||n.kind==NodeKind::NewClass)n.args=AwaitCalls(n.args,n.parent);
    if(n.kind==NodeKind::Change&&(n.args.empty()||n.args=="="))
        for(const auto& field:nodes_)if(field.kind==NodeKind::Variable&&field.name==n.name&&field.type=="int"&&CanSee(field.id,n.parent)&&!n.value.empty()){
            n.value="CheckedInt("+n.value+")";break;
        }
    auto r=ranges_[i];
    std::string s=source_;
    if(!HasScriptBody()&&n.kind==NodeKind::Custom)s=n.value;
    else if(n.kind==NodeKind::State){
        auto where=s.find("DefineState(",r.code);
        if(where==missing||where>r.end)return false;
        CppTokens state(std::string_view(s).substr(where,r.end-where));
        if(state.V(1)!="("||state.items.size()<6)return false;
        s.replace(where+state.items[2].from,state.items[4].to-state.items[2].from,Quote(n.name)+", "+Quote(n.value));
    } else if(StateCallback(n.kind))return false;
    else if(r.block){
        bool useOwner=n.kind!=NodeKind::OnCreate||Text(n.parent).find("/*rice:owner*/")!=missing;
        auto head=Header(n,Get(n.parent),useOwner?rootName_:std::string{});if(head.empty())return false;
        if(Branch(n.kind)||n.kind==NodeKind::Loop)head=BindOwner(head,n.parent);
        if(n.kind==NodeKind::Function&&n.coroutine&&Type(n)!=Type(old)){
            auto before=s.find("/*rice:finish*/",r.headerEnd);
            if(before!=missing&&before<r.end&&Type(n)!="void")s.erase(before,std::string_view("/*rice:finish*/ co_return;").size());
            else if(Type(n)=="void"&&before>=r.end)s.insert(r.childrenEnd,"/*rice:finish*/ co_return;\n");
        }
        s.replace(r.begin,r.headerEnd-r.begin,Mark(n)+head+" ");
    } else{
        if(n.kind==NodeKind::NewClass){
            auto klass=std::find_if(nodes_.begin(),nodes_.end(),[&](const auto& c){return c.kind==NodeKind::Class&&c.name==n.type;});
            if(klass!=nodes_.end()&&n.args.empty()&&Text(klass->id).find("/*rice:owner*/")!=missing){
                bool member=false;for(auto* context=Get(n.parent);context;context=Get(context->parent))if(context->kind==NodeKind::Class)member=true;
                n.args=member?"*riceOwner":"*this";
            }
        }
        std::string code=Statement(n);
        if(n.kind==NodeKind::Call){
            auto target=std::find_if(nodes_.begin(),nodes_.end(),[&](const auto& fn){return fn.kind==NodeKind::Function&&fn.name==n.name&&CanSee(fn.id,n.parent);});
            auto call=n.name+"("+n.args+")";
            if(target!=nodes_.end()&&!target->coroutine)code=call+";";
            else if(Synchronous(n.parent))code="RunNow("+call+");";
        }
        if(n.kind==NodeKind::EndFunction&&Synchronous(n.parent))code="return"+(n.value.empty()?"":" "+n.value)+";";
        if(!Decl(n.kind)&&n.kind!=NodeKind::Custom)code=BindOwner(code,n.parent);
        s.replace(r.begin,r.end-r.begin,Mark(n)+code);
    }
    ScriptDocument rebuilt;
    rebuilt.SetSource(s);
    const auto* again=rebuilt.Get(id);
    if(HasScriptBody()&&(!again||again->kind!=old.kind)){
        // Typing a temporarily incomplete expression must not replace the active
        // list with one opaque file or move focus to a different element.
        std::size_t prefix=0;
        while(prefix<source_.size()&&prefix<s.size()&&source_[prefix]==s[prefix])++prefix;
        std::size_t suffix=0;
        while(suffix<source_.size()-prefix&&suffix<s.size()-prefix&&source_[source_.size()-1-suffix]==s[s.size()-1-suffix])++suffix;
        auto shift=[&](std::size_t& offset){
            if(offset==missing||offset<=prefix)return;
            if(offset>=source_.size()-suffix)offset=s.size()-(source_.size()-offset);
            else offset=std::min(offset,s.size()-suffix);
        };
        for(auto& range:ranges_){shift(range.begin);shift(range.code);shift(range.headerEnd);shift(range.end);shift(range.childrenEnd);}
        shift(rootOpen_);shift(rootClose_);shift(readerBegin_);shift(readerEnd_);
        source_=std::move(s);
        nodes_[i]=next;nodes_[i].id=id;nodes_[i].parent=old.parent;
        CppTokens tokens(source_);
        for(std::size_t j=0;j<nodes_.size();++j){nodes_[j].line=tokens.Line(ranges_[j].code);ranges_[j].lastLine=tokens.Line(ranges_[j].end);}
        parseErrors_={{id,nodes_[i].line,false,"This item contains incomplete C++."}};
        return true;
    }
    *this=std::move(rebuilt);
    if(Decl(n.kind)&&Identifier(n.name)&&Identifier(old.name)&&old.name!=n.name){
        // Names are unique within a script. Rename identifier tokens in this
        // declaration's lexical region while preserving comments and literals.
        auto owner=Index(n.parent);
        auto first=owner==missing?rootOpen_:ranges_[owner].code;
        auto last=owner==missing?rootClose_:ranges_[owner].end;
        auto cpp=source_;CppTokens tokens(source_);
        for(std::size_t j=tokens.items.size();j>0;--j){
            const auto& token=tokens.items[j-1];
            if(token.from<first||token.from>=last)continue;
            if(token.value.starts_with(marker)){
                ScriptNode tag;ReadMark(token.value,tag);
                if(tag.references.contains(old.name)&&tag.references.at(old.name)==id){
                    tag.references.erase(old.name);tag.references[n.name]=id;
                    auto updated=Mark(tag);updated.pop_back();
                    cpp.replace(token.from,token.to-token.from,updated);
                }
                continue;
            }
            if(token.value!=old.name)continue;
            if(j>=2&&(tokens.V(j-2)=="::"||((tokens.V(j-2)=="."||tokens.V(j-2)=="->")&&(j<3||tokens.V(j-3)!="riceOwner"))))continue;
            cpp.replace(token.from,token.to-token.from,n.name);
        }
        if(cpp!=source_)SetSource(std::move(cpp));
    }
    if(Decl(n.kind)||Callback(n.kind)||n.kind==NodeKind::State||n.kind==NodeKind::Custom)RefreshReader();
    return true;
}
bool ScriptDocument::Erase(ScriptId id){
    auto i=Index(id);if(i==missing)return false;
    auto n=nodes_[i];
    if(n.kind==NodeKind::Start||n.kind==NodeKind::Update||StateCallback(n.kind))return false;
    if(Decl(n.kind)){
        auto tracked=source_;auto uses=Instances(id);
        for(auto it=uses.rbegin();it!=uses.rend();++it){
            auto j=Index(*it);auto related=nodes_[j];const auto& range=ranges_[j];
            if(!related.references.contains(n.name)){
                related.references[n.name]=id;
                tracked.replace(range.begin,range.code-range.begin,Mark(related));
            }
        }
        if(tracked!=source_){SetSource(std::move(tracked));i=Index(id);if(i==missing)return false;}
    }
    auto r=ranges_[i];
    auto start=OnlyIndentFrom(source_,r.begin),end=r.end;
    if(end<source_.size()&&source_[end]=='\r')++end;
    if(end<source_.size()&&source_[end]=='\n')++end;
    auto s=source_;
    if(n.kind==NodeKind::OnCreate&&Get(n.parent)&&Text(n.parent).find("/*rice:owner*/")!=missing)
        s.replace(start,end-start,Indent(s,r.begin)+DefaultCtor(Get(n.parent)->name,rootName_)+"\n");
    else s.erase(start,end-start);
    SetSource(std::move(s));
    if(Decl(n.kind)||Callback(n.kind)||n.kind==NodeKind::State||n.kind==NodeKind::Custom)RefreshReader();
    return true;
}
bool ScriptDocument::Move(ScriptId id,int direction){
    auto i=Index(id);if(i==missing||!direction)return false;
    std::vector<std::size_t> siblings;
    for(std::size_t j=0;j<nodes_.size();++j)if(nodes_[j].parent==nodes_[i].parent)siblings.push_back(j);
    auto pos=static_cast<std::size_t>(std::find(siblings.begin(),siblings.end(),i)-siblings.begin());
    auto kind=[&](std::size_t p){return nodes_[siblings[p]].kind;};
    auto groupEnd=[&](std::size_t p){
        auto end=p;
        if(kind(p)==NodeKind::Condition)
            while(end+1<siblings.size()&&(kind(end+1)==NodeKind::ElseIf||kind(end+1)==NodeKind::Else))++end;
        return end;
    };
    bool withinChain=kind(pos)==NodeKind::ElseIf||
        (kind(pos)==NodeKind::Condition&&direction>0&&pos+1<siblings.size()&&kind(pos+1)==NodeKind::ElseIf);
    if(kind(pos)==NodeKind::Else)return false;
    std::size_t first{},firstEnd{},second{},secondEnd{};
    if(withinChain){
        if((direction<0&&pos==0)||(direction>0&&pos+1>=siblings.size()))return false;
        auto adjacent=direction<0?pos-1:pos+1;
        if(kind(adjacent)!=NodeKind::ElseIf&&!(direction<0&&kind(adjacent)==NodeKind::Condition))return false;
        first=firstEnd=std::min(pos,adjacent);second=secondEnd=std::max(pos,adjacent);
    }else if(direction<0){
        if(!pos)return false;
        first=firstEnd=pos-1;second=pos;secondEnd=groupEnd(pos);
        while(first>0&&(kind(first)==NodeKind::ElseIf||kind(first)==NodeKind::Else))--first;
    }else{
        first=pos;firstEnd=groupEnd(pos);second=firstEnd+1;
        if(second>=siblings.size())return false;
        secondEnd=groupEnd(second);
    }
    auto left=ranges_[siblings[first]],right=ranges_[siblings[second]];
    left.end=ranges_[siblings[firstEnd]].end;right.end=ranges_[siblings[secondEnd]].end;
    std::string s=source_,a=s.substr(left.begin,left.end-left.begin),b=s.substr(right.begin,right.end-right.begin);
    if(withinChain&&kind(first)==NodeKind::Condition){
        a.insert(left.code-left.begin,"else ");
        auto keyword=right.code-right.begin;
        auto from=b.find("if",keyword+4);
        if(from==missing)return false;
        b.erase(keyword,from-keyword);
    }
    s.replace(right.begin,right.end-right.begin,a);
    s.replace(left.begin,left.end-left.begin,b);
    SetSource(std::move(s));return true;
}

namespace {
std::vector<std::string_view> References(const ScriptNode& n){
    switch(n.kind){
    case NodeKind::Variable:return {n.type,n.value};
    case NodeKind::Function:return {n.type,n.args};
    case NodeKind::Change:case NodeKind::NewClass:return {n.name,n.value,n.args,n.type};
    case NodeKind::Call:return {n.name,n.args};
    case NodeKind::Wait:case NodeKind::Condition:case NodeKind::ElseIf:case NodeKind::Loop:
    case NodeKind::EndFunction:case NodeKind::Custom:case NodeKind::Activate:return {n.value};
    default:return {};
    }
}
bool Keyword(std::string_view name) {
    static constexpr std::string_view words=
        " alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t char32_t class compl concept const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not not_eq nullptr operator or or_eq private protected public register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while xor xor_eq ";
    return words.find(" "+std::string(name)+" ")!=missing;
}
bool MaySuspend(std::string_view text){
    return HasToken(text,"co_await")||HasToken(text,"co_yield");
}
}
std::vector<ScriptId> ScriptDocument::Instances(ScriptId symbol,ScriptId window) const {
    std::vector<ScriptId> found;
    auto* declared=Get(symbol);
    if(!declared||!Decl(declared->kind))return found;
    for(const auto& n:nodes_){
        if(n.id==symbol||!Within(n.id,window)||!CanSee(symbol,n.parent))continue;
        if(auto ref=n.references.find(declared->name);ref!=n.references.end()&&ref->second!=symbol)continue;
        for(auto text:References(n))if(HasToken(text,declared->name)){
            found.push_back(n.id);break;
        }
    }
    return found;
}
std::vector<ScriptDiagnostic> ScriptDocument::Validate(const std::vector<std::string_view>& otherSources) const {
    std::vector<ScriptDiagnostic> result=parseErrors_;
    std::unordered_map<std::string,ScriptId> names;
    for(const auto& n:nodes_){
        auto error=[&](std::string msg){result.push_back({n.id,n.line,false,std::move(msg)});};
        for(const auto& [name,id]:n.references){
            auto fields=References(n);
            if(std::none_of(fields.begin(),fields.end(),[&](auto field){return HasToken(field,name);}))continue;
            auto* sym=Get(id);
            if(!sym||sym->name!=name||!CanSee(id,n.parent))error("Unresolved reference: "+name);
        }
        if(Decl(n.kind)){
            if(!Identifier(n.name)||Keyword(n.name))error("Invalid C++ identifier: "+n.name);
            auto [it,added]=names.emplace(n.name,n.id);
            if(!added){error("Duplicate declaration: "+n.name);
                if(const auto* first=Get(it->second))result.push_back({first->id,first->line,false,"Duplicate declaration: "+first->name});}
        }
        if(n.kind==NodeKind::Variable){
            if(n.type.empty()||n.type=="void")error("Choose a variable type.");
            if(n.riceCon){
                if(!CanRead(n.id))error("This type cannot be exposed through RiceCon.");
                if(n.parent)error("RiceCon fields must be script members.");
                if(n.riceAccess=="policy"&&n.policy.empty())error("Choose a policy for this variable.");
                if(n.riceScope=="global"&&n.riceLife!="stored")error("Global variables require stored lifetime.");
            }
        }
        if(n.kind==NodeKind::Function&&Instances(n.id).empty()&&
            std::none_of(otherSources.begin(),otherSources.end(),[&](auto src){return HasToken(src,n.name);}))
            result.push_back({n.id,n.line,true,"No uses found for function '"+n.name+"'."});
        if(n.kind==NodeKind::Wait){
            if(n.value.empty())error("Choose a wait duration.");
            else{
                double seconds=0;auto end=n.value.data()+n.value.size();
                if(n.value.ends_with("f")||n.value.ends_with("F"))--end;
                auto converted=std::from_chars(n.value.data(),end,seconds);
                if(converted.ec==std::errc{}&&converted.ptr==end&&(!std::isfinite(seconds)||seconds<0))
                    error("Wait duration must be a finite, non-negative number.");
            }
        }
        if(n.kind==NodeKind::Change||n.kind==NodeKind::Call){
            bool known=false;
            for(const auto& decl:nodes_)
                if(decl.name==n.name&&decl.kind==(n.kind==NodeKind::Change?NodeKind::Variable:NodeKind::Function)&&CanSee(decl.id,n.parent)){known=true;break;}
            // Parameter declarations are local to a function, not independent member nodes.
            for(auto parent=Get(n.parent);!known&&parent;parent=Get(parent->parent))
                known=n.kind==NodeKind::Change&&parent->kind==NodeKind::Function&&HasToken(parent->args,n.name);
            if(!known)error("Unknown or out-of-scope "+std::string(n.kind==NodeKind::Change?"variable: ":"function: ")+n.name);
        }
        if(n.kind==NodeKind::EndLoop){
            bool inLoop=false;
            for(auto* parent=Get(n.parent);parent;parent=Get(parent->parent)){
                if(parent->kind==NodeKind::Loop){inLoop=true;break;}
                if(Returning(parent->kind))break;
            }
            if(!inLoop)error("End Loop requires an enclosing loop.");
        }
    }
    // Validate transitive calls from callbacks that must finish synchronously.
    std::set<ScriptId> suspending;
    bool changed=true;
    while(changed){
        changed=false;
        for(const auto& n:nodes_){
            bool suspend=n.kind==NodeKind::Wait||n.kind==NodeKind::Loop||(n.kind==NodeKind::Custom&&MaySuspend(n.value));
            if(!suspend)for(const auto& fn:nodes_)if(suspending.contains(fn.id)&&fn.kind==NodeKind::Function){
                for(auto text:References(n))if(HasToken(text,fn.name)){suspend=true;break;}
                if(suspend)break;
            }
            if(!suspend)continue;
            for(auto* owner=Get(n.parent);owner;owner=Get(owner->parent)){
                if(owner->kind==NodeKind::Function||Callback(owner->kind)||StateCallback(owner->kind)){
                    changed|=suspending.insert(owner->id).second;break;
                }
            }
        }
    }
    for(auto id:suspending){const auto* fn=Get(id);if(fn&&(fn->kind==NodeKind::OnCreate||fn->kind==NodeKind::OnDestroy))
        result.push_back({id,fn->line,false,"Creation and destruction callbacks must complete synchronously."});}
    return result;
}

void ScriptDocument::MergeExternal(std::string_view baseText,std::string_view external,const std::function<bool(ScriptId)>& allow){
    if(external==baseText||external==source_)return;
    if(!allow&&source_==baseText){SetSource(std::string(external));return;}
    ScriptDocument base,theirs;
    base.SetSource(std::string(baseText));theirs.SetSource(std::string(external));
    if(!base.HasScriptBody()||!HasScriptBody()||!theirs.HasScriptBody()){
        if(!allow||allow(0))SetSource(std::string(external));
        return;
    }
    auto ownPart=[](const ScriptDocument& doc,ScriptId id){
        auto i=doc.Index(id);if(i==missing)return std::string{};
        const auto& r=doc.ranges_[i];
        auto text=doc.source_.substr(r.begin,(r.block?r.headerEnd:r.end)-r.begin);
        if(doc.nodes_[i].kind==NodeKind::State)text+="\n"+doc.nodes_[i].name+"\n"+doc.nodes_[i].value;
        return text;
    };
    auto applyItem=[&](const ScriptDocument& draft,const ScriptNode& n){
        auto target=Index(n.id),origin=draft.Index(n.id);
        if(target==missing){Insert(n,draft.Text(n.id));return;}
        if(nodes_[target].kind!=n.kind&&ranges_[target].block&&draft.ranges_[origin].block){
            auto old=ranges_[target],next=draft.ranges_[origin];
            auto s=source_;
            s.replace(old.begin,old.headerEnd-old.begin,draft.source_.substr(next.begin,next.headerEnd-next.begin));
            SetSource(std::move(s));
        }else Edit(n.id,n);
    };
    auto restoreOrder=[&](const ScriptDocument& draft,const ScriptDocument& latest,bool filtered){
        std::set<ScriptId> parents;
        for(const auto& n:draft.nodes_)parents.insert(n.parent);
        for(auto parent:parents){
            std::vector<ScriptId> wanted,current,original,latestOrder;
            for(const auto& n:draft.nodes_)if(n.parent==parent&&Get(n.id))wanted.push_back(n.id);
            auto matches=[&](const ScriptNode& n){
                return n.parent==parent&&std::find(wanted.begin(),wanted.end(),n.id)!=wanted.end();
            };
            for(const auto& n:nodes_)if(matches(n))current.push_back(n.id);
            if(current==wanted||current.size()!=wanted.size())continue;
            for(const auto& n:base.nodes_)if(matches(n))original.push_back(n.id);
            for(const auto& n:latest.nodes_)if(matches(n))latestOrder.push_back(n.id);
            auto reordered=[&](const std::vector<ScriptId>& order,ScriptId id){
                auto ref=std::find(original.begin(),original.end(),id);
                auto now=std::find(order.begin(),order.end(),id);
                if(ref==original.end()||now==order.end())return ref==original.end();
                for(auto it=original.begin();it!=original.end();++it){
                    auto other=std::find(order.begin(),order.end(),*it);
                    if(other!=order.end()&&(it<ref)!=(other<now))return true;
                }
                return false;
            };
            auto result=current;
            for(std::size_t j=0;j<wanted.size();++j){
                auto id=wanted[j];
                if(!reordered(wanted,id)||(filtered?!allow(id):(base.Get(id)&&reordered(latestOrder,id))))continue;
                auto located=std::find(result.begin(),result.end(),id);
                result.erase(located);
                auto place=j?std::find(result.begin(),result.end(),wanted[j-1])+1:result.begin();
                result.insert(place,id);
            }
            if(result==current)continue;
            auto s=source_;
            for(auto j=current.size();j>0;--j){
                auto r=ranges_[Index(current[j-1])];
                s.replace(r.begin,r.end-r.begin,Text(result[j-1]));
            }
            SetSource(std::move(s));
        }
    };
    if(allow){
        // Applying a detached window: import just the items actually changed in that window.
        for(const auto& prev:base.nodes_)if(!theirs.Get(prev.id)&&allow(prev.id))Erase(prev.id);
        for(const auto& n:theirs.nodes_){
            if(!allow(n.id))continue;
            if(!base.Get(n.id)||ownPart(theirs,n.id)!=ownPart(base,n.id))applyItem(theirs,n);
        }
        restoreOrder(theirs,base,true);
        RefreshReader();return;
    }
    // Use the external source as the foundation so comments, includes and untouched
    // unsupported C++ are preserved exactly. Reapply independent local item changes.
    ScriptDocument local=*this;
    *this=theirs;
    for(const auto& before:base.nodes_)
        if(!local.Get(before.id)&&Get(before.id)&&Text(before.id)==base.Text(before.id))Erase(before.id);
    for(const auto& n:local.nodes_){
        if(!base.Get(n.id)){
            if(!Get(n.id))Insert(n,local.Text(n.id));
            continue;
        }
        auto baseline=ownPart(base,n.id),current=ownPart(*this,n.id);
        if(!current.empty()&&current==baseline&&ownPart(local,n.id)!=baseline)applyItem(local,n);
    }
    restoreOrder(local,theirs,false);
    RefreshReader();
}
}
