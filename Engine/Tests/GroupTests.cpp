#include "Editor/EditorGroups.h"
#include "Editor/EditorClipboard.h"
#include <iostream>
using namespace Rice;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool Near(const Matrix4& a,const Matrix4& b){for(int i=0;i<4;++i)for(int j=0;j<4;++j)if(std::abs(a[i][j]-b[i][j])>.003f)return false;return true;}
int main(){try{
    ComponentRegistry registry;EditorSession e(registry);
    auto& p=e.scene->CreateGameObject("parent");p.transform.position={5,3,1};auto pid=p.GetID();
    auto& a=e.scene->CreateGameObject("a");a.SetParent(&p);a.transform.position={-2,0,0};a.AddComponent<MeshRenderer>();auto aid=a.GetID();
    auto& b=e.scene->CreateGameObject("b");b.SetParent(&p);b.transform.position={2,0,0};b.AddComponent<MeshRenderer>();auto bid=b.GetID();
    auto aw=a.GetWorldMatrix(),bw=b.GetWorldMatrix();
    auto gid=EditorGroups::Create(e,{aid,bid});auto* g=e.scene->Find(gid);
    Check(g&&g->isGroup&&g->GetParent()==&p,"Group is parentable object");
    Check(g->name=="New Object","New grouping objects use the default object name");
    Check(a.GetParent()==&p&&a.GetGroup()==g&&Near(a.GetWorldMatrix(),aw)&&Near(b.GetWorldMatrix(),bw),"Group preserves original parent and world");
    auto [low,high]=GizmoMath::SpaceBounds(*g,Matrix4(1));Check(glm::length(high-low-Vector3(5,1,1))<.001f,"Group world bounds");
    auto initial=g->GetWorldMatrix();g->transform.rotation={0,0,35};g->transform.localScale={2,1,3};g->transform.position+=Vector3(1,2,0);
    auto delta=g->GetWorldMatrix()*glm::inverse(initial);
    Check(Near(a.GetWorldMatrix(),delta*aw)&&Near(b.GetWorldMatrix(),delta*bw),"Group TRS reaches members");
    auto before=a.GetWorldMatrix();p.transform.position.x+=4;
    Check(Near(a.GetWorldMatrix(),glm::translate(Matrix4(1),Vector3(4,0,0))*before),"Common parent moves member once");
    g->AddComponent<Light>().intensity=7;
    auto saved=SceneCodec(registry).Encode(*e.scene);auto loaded=SceneCodec(registry).Decode(saved);
    Check(Near(loaded->Find(aid)->GetWorldMatrix(),a.GetWorldMatrix())&&loaded->Find(gid)->GetComponent<Light>()->intensity==7,"Save/load group links components matrices");
    EditorClipboard clip;clip.CopyObject(*e.scene,gid,registry);auto copyID=clip.PasteObject(e,registry);auto* copy=e.scene->Find(copyID);Check(copy->isGroup&&copy->GetParent()->GetID()==pid,"Group clone same hierarchy");
    int members=0;for(auto& obj:e.scene->GetObjects())if(obj->GetGroup()==copy)++members;Check(members==2,"Group clone remaps member links");
    EditorSession other(registry);auto cross=clip.PasteObject(other,registry);Check(other.scene->Find(cross)->isGroup&&other.scene->GetObjectCount()==3,"Group cross scene clipboard");
    for(auto& obj:other.scene->GetObjects())if(obj->name=="a")Check(Near(obj->GetWorldMatrix(),a.GetWorldMatrix()),"Cross scene member world preserved");
    auto nestedID=EditorGroups::Create(e,{gid,copyID});auto* nested=e.scene->Find(nestedID);auto nestedBefore=a.GetWorldMatrix();nested->transform.position.y+=2;
    Check(Near(a.GetWorldMatrix(),glm::translate(Matrix4(1),Vector3(0,2,0))*nestedBefore),"Nested group moves once");
    auto finalWorld=a.GetWorldMatrix();EditorGroups::Ungroup(e,nestedID);Check(!e.scene->Find(nestedID)&&Near(a.GetWorldMatrix(),finalWorld),"Nested ungroup preserves world");
    EditorGroups::Ungroup(e,gid);Check(e.scene->Find(aid)->GetParent()->GetID()==pid&&!e.scene->Find(aid)->GetGroup()&&Near(e.scene->Find(aid)->GetWorldMatrix(),finalWorld),"Ungroup preserves affine world and parent");
    e.Undo();Check(e.scene->Find(gid)&&Near(e.scene->Find(aid)->GetWorldMatrix(),finalWorld),"Ungroup undo");e.Redo();Check(!e.scene->Find(gid),"Ungroup redo");
    auto controlled=EditorGroups::ControlChildren(e,pid);Check(controlled&&e.scene->Find(controlled)->GetParent()->GetID()==pid,"Control Children creates child group");
    auto* control=e.scene->Find(controlled);auto* member=e.scene->Find(aid);auto previous=member->GetWorldMatrix();
    auto& newParent=e.scene->CreateGameObject("new parent");newParent.transform.position={20,0,0};EditorGroups::Reparent(*control,&newParent);
    Check(Near(member->GetWorldMatrix(),previous),"Group reparent preserves world");newParent.transform.position.y+=5;
    Check(Near(member->GetWorldMatrix(),glm::translate(Matrix4(1),Vector3(0,5,0))*previous),"Group follows new parent");
    bool rejected=false;try{control->SetParent(member);}catch(const std::exception&){rejected=true;}Check(rejected,"Group dependency cycle rejected");
    EditorSession shared(registry);
    auto& ancestor=shared.scene->CreateGameObject("ancestor");
    auto& left=shared.scene->CreateGameObject("left");left.SetParent(&ancestor);
    auto& right=shared.scene->CreateGameObject("right");right.SetParent(&ancestor);right.transform.position={3,0,0};
    auto& item=shared.scene->CreateGameObject("item");item.SetParent(&left);item.transform.position={1,2,0};
    auto groupID=EditorGroups::Create(shared,{item.GetID()});auto* commonGroup=shared.scene->Find(groupID);
    auto duplicateID=SceneCodec(registry).Duplicate(*shared.scene,groupID).GetID();
    Check(shared.scene->Find(duplicateID)->isGroup,"Code API duplicates group");
    bool duplicateMember=false;for(auto& obj:shared.scene->GetObjects())if(obj->GetGroup()==shared.scene->Find(duplicateID)){duplicateMember=Near(obj->GetWorldMatrix(),item.GetWorldMatrix());}
    Check(duplicateMember,"Code API preserves group member transforms");shared.scene->Destroy(duplicateID);shared.scene->Flush();
    EditorGroups::Reparent(*commonGroup,&right);auto original=item.GetWorldMatrix();ancestor.transform.position.x+=6;
    Check(Near(item.GetWorldMatrix(),glm::translate(Matrix4(1),Vector3(6,0,0))*original),"Shared ancestor contribution applied once");
    original=item.GetWorldMatrix();left.transform.position.z+=2;
    Check(Near(item.GetWorldMatrix(),glm::translate(Matrix4(1),Vector3(0,0,2))*original),"Original parent remains a live dependency");
    original=item.GetWorldMatrix();EditorGroups::Ungroup(shared,groupID);Check(Near(item.GetWorldMatrix(),original),"Mixed parent ungroup preserves world");
    auto legacy=SceneCodec(registry).Decode("RICE_SCENE 1\nObject 500 0 \"legacy\" 1 0 0 0 0 0 0 1 1 1\nEndObject\nEndScene\n");Check(legacy->Find(500)!=nullptr,"Version 1 compatibility");
    EditorSession order(registry);auto first=order.scene->CreateGameObject("first").GetID();auto middle=order.scene->CreateGameObject("middle").GetID();auto last=order.scene->CreateGameObject("last").GetID();
    auto siblingOrder=[&](Scene& scene,EntityID parentID){std::vector<EntityID> result;for(auto& node:scene.GetObjects())if(!node->IsPendingDestroy()&&(node->GetHierarchyParent()?node->GetHierarchyParent()->GetID():0)==parentID)result.push_back(node->GetID());return result;};
    EditorGroups::InsertSibling(order,last,middle,false);Check(siblingOrder(*order.scene,0)==std::vector<EntityID>{first,last,middle},"Insert before root sibling");
    order.Undo();Check(siblingOrder(*order.scene,0)==std::vector<EntityID>{first,middle,last},"Reorder undo");order.Redo();
    EditorGroups::InsertSibling(order,first,middle,true);Check(siblingOrder(*order.scene,0)==std::vector<EntityID>{last,middle,first},"Insert after root sibling");
    auto nestedChild=order.scene->CreateGameObject("nested child").GetID();order.scene->Find(nestedChild)->SetParent(order.scene->Find(middle));order.scene->Find(middle)->transform.position={4,5,6};
    auto nestedWorld=order.scene->Find(nestedChild)->GetWorldMatrix();EditorGroups::InsertSibling(order,nestedChild,last,true);
    Check(!order.scene->Find(nestedChild)->GetHierarchyParent()&&Near(order.scene->Find(nestedChild)->GetWorldMatrix(),nestedWorld),"Lift child to root without changing world");
    auto destinationChild=order.scene->CreateGameObject("destination child").GetID();order.scene->Find(destinationChild)->SetParent(order.scene->Find(middle));
    EditorGroups::InsertSibling(order,nestedChild,destinationChild,false);Check(siblingOrder(*order.scene,middle)==std::vector<EntityID>{nestedChild,destinationChild},"Cross parent sibling insertion");
    auto orderedCopy=SceneCodec(registry).Decode(SceneCodec(registry).Encode(*order.scene));Check(siblingOrder(*orderedCopy,middle)==std::vector<EntityID>{nestedChild,destinationChild},"Save load preserves sibling order");
    auto unchanged=order.Snapshot();bool invalid=false;try{EditorGroups::InsertSibling(order,middle,nestedChild,true);}catch(const std::exception&){invalid=true;}
    Check(invalid&&order.Snapshot()==unchanged,"Invalid descendant drop cannot mutate scene");
    auto groupOrder=EditorGroups::Create(order,{nestedChild,destinationChild});auto memberWorld=order.scene->Find(destinationChild)->GetWorldMatrix();
    EditorGroups::InsertSibling(order,destinationChild,nestedChild,false);Check(siblingOrder(*order.scene,groupOrder)==std::vector<EntityID>{destinationChild,nestedChild}&&Near(order.scene->Find(destinationChild)->GetWorldMatrix(),memberWorld),"Group siblings reorder without rebinding transforms");
    EditorSession layers(registry);SceneLayers::CreateDefaults(*layers.scene);
    auto defaults=SceneLayers::Ordered(*layers.scene);
    Check(defaults.size()==3,"A layered scene starts with three layers");
    auto* system=defaults[0];auto* ui=defaults[1];auto* game=defaults[2];
    auto& extra=SceneLayers::Create(*layers.scene,"Game");
    auto& piece=layers.scene->CreateGameObject("Piece");SceneLayers::PlaceRoot(piece,game);
    auto& board=layers.scene->CreateGameObject("Board");SceneLayers::PlaceRoot(board,&extra);
    const auto initialLayers=layers.Snapshot();
    for(auto* parent:{game,&piece}){
        bool refused=false;try{EditorGroups::Reparent(extra,parent);}catch(const std::invalid_argument&){refused=true;}
        Check(refused,"Layers cannot become children of layers or objects");
    }
    Check(!EditorGroups::Create(layers,{ui->GetID(),piece.GetID()}),"Grouping a mixed selection cannot nest a layer");
    EditorGroups::Ungroup(layers,system->GetID());
    EditorGroups::InsertSibling(layers,piece.GetID(),game->GetID(),true);
    EditorGroups::InsertSibling(layers,game->GetID(),ui->GetID(),false);
    Check(layers.Snapshot()==initialLayers,"Reserved slots and layer membership survive invalid hierarchy edits");
    EditorGroups::InsertSibling(layers,extra.GetID(),game->GetID(),false);
    Check(SceneLayers::Ordered(*layers.scene)==std::vector<GameObject*>{system,ui,&extra,game},"Game layers reorder below the two required slots");
    auto crossGroup=EditorGroups::Create(layers,{piece.GetID(),board.GetID()});
    Check(crossGroup&&layers.scene->Find(crossGroup)->GetHierarchyParent()==game,"A cross-layer group belongs to its first object's layer");
    EditorGroups::Ungroup(layers,crossGroup);
    Check(piece.GetHierarchyParent()==game&&board.GetHierarchyParent()==&extra,"Ungroup restores the objects' original layer membership");
    std::cout<<"Group hierarchy, bounds, affine transforms, serialization, clipboard and undo passed\n";return 0;
}catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}}
