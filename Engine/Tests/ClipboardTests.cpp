#include "Editor/EditorClipboard.h"
#include <iostream>
using namespace Rice;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    {
        ComponentRegistry scripts;EditorSession session(scripts);SceneCodec codec(scripts);
        auto& first=session.scene->CreateGameObject("First");auto firstID=first.GetID();
        auto& second=session.scene->CreateGameObject("Second");auto secondID=second.GetID();
        first.AddComponent<ObjectScript>().scriptFile="Scripts/Player movement.cpp";
        first.AddComponent<BlankScript>().scriptFile="Scripts/도구.hpp";
        first.AddComponent<SingletonManager>().scriptFile="Scripts/GameManager.h";
        second.AddComponent<ObjectScript>().scriptFile="Scripts/Enemy.cpp";
        auto saved=codec.Encode(*session.scene);auto loaded=codec.Decode(saved);
        Check(loaded->Find(firstID)->GetComponent<ObjectScript>()->scriptFile=="Scripts/Player movement.cpp","Object script file persists");
        Check(loaded->Find(firstID)->GetComponent<BlankScript>()->scriptFile=="Scripts/도구.hpp","Unicode script file persists");
        Check(loaded->Find(firstID)->GetComponent<SingletonManager>()->scriptFile=="Scripts/GameManager.h","Singleton script file persists");
        Check(loaded->Find(secondID)->GetComponent<ObjectScript>()->scriptFile=="Scripts/Enemy.cpp","Same script type keeps independent file per component");
        auto& duplicate=codec.Duplicate(*loaded,firstID);
        Check(duplicate.GetComponent<ObjectScript>()->scriptFile=="Scripts/Player movement.cpp","Duplicate retains linked script");
        auto snapshot=ComponentSnapshot::CaptureAll(first,scripts);
        auto& target=loaded->CreateGameObject("Snapshot");ComponentSnapshot::Apply(target,snapshot,scripts);
        Check(target.GetComponent<BlankScript>()->scriptFile=="Scripts/도구.hpp","Prefab and clipboard payload retains script");
        session.Edit([&](Scene& scene){scene.Find(firstID)->GetComponent<ObjectScript>()->scriptFile="Scripts/Changed.cpp";});
        session.Undo();Check(session.scene->Find(firstID)->GetComponent<ObjectScript>()->scriptFile=="Scripts/Player movement.cpp","Undo script connection");
        session.Redo();Check(session.scene->Find(firstID)->GetComponent<ObjectScript>()->scriptFile=="Scripts/Changed.cpp","Redo script connection");
        std::istringstream lines(saved);std::ostringstream legacy;std::string line;
        while(std::getline(lines,line))if(!line.starts_with("\"Script file\""))legacy<<line<<'\n';
        auto old=codec.Decode(legacy.str());
        Check(old->Find(firstID)->GetComponent<ObjectScript>()->scriptFile.empty(),"Older scene loads with unassigned script");
        ComponentSnapshot::Apply(target,{{"ObjectScript","1\n"},{"BlankScript","1\n"},{"SingletonManager","1\n"}},scripts);
        Check(target.GetComponent<ObjectScript>()->scriptFile.empty()&&target.GetComponent<BlankScript>()->scriptFile.empty()&&target.GetComponent<SingletonManager>()->scriptFile.empty(),"Older prefab payload clears missing script links");
    }

    ComponentRegistry registry;EditorSession editor(registry);EditorClipboard clipboard;
    {
        Scene scene;
        auto& base=scene.CreateGameObject();
        auto& numbered=scene.CreateGameObject();
        Check(base.name=="New Object"&&numbered.name=="New Object (1)","Default object names are numbered on collision");
        SceneCodec codec(registry);
        auto& next=codec.Duplicate(scene,numbered.GetID());
        Check(next.name=="New Object (2)","Duplicating a numbered copy continues its series");
        Check(scene.UniqueObjectName(numbered.name,&numbered)==numbered.name,"Renaming ignores the object's current name");
        scene.Destroy(numbered.GetID());
        Check(scene.CreateGameObject().name=="New Object (1)","A removed suffix becomes available again");
        next.name=base.name;
        auto restored=codec.Decode(codec.Encode(scene));
        Check(restored->Find(next.GetID())->name==base.name,"Scene loading preserves pre-existing duplicate names");
        auto& system=scene.CreateGameObject("System");
        system.isGroup=true;system.AddComponent<SceneLayer>().role=SceneLayer::System;
        EditorClipboard protectedClipboard;protectedClipboard.CopyObject(scene,system.GetID(),registry);
        Check(!protectedClipboard.HasObjects(),"Required layers cannot enter the object clipboard");
        bool rejected=false;
        try{codec.Duplicate(scene,system.GetID());}catch(const std::invalid_argument&){rejected=true;}
        Check(rejected,"Duplicate cannot create another required layer");
    }
    {
        Scene scene;
        auto& target=scene.CreateGameObject("Legacy clipboard target");
        ComponentSnapshot::Apply(target,{{"Rigidbody","0\n1 2 3\n7\n0.25\n0\n1\n"}},registry);
        auto* body=target.GetComponent<Rigidbody>();
        Check(body&&!body->enabled&&body->velocity==Vector3(1,2,3)&&body->mass==7&&body->restitution==.25f&&!body->useGravity&&body->kinematic,"Old Rigidbody clipboard and prefab fields survive the rename");
        auto snapshot=ComponentSnapshot::CaptureAll(target,registry);
        Check(snapshot.size()==1&&snapshot.front().type=="Physics","Copied physics components use the new name");
    }
    auto& parent=editor.scene->CreateGameObject("parent");parent.transform.position={10,2,3};auto parentID=parent.GetID();
    auto& obj=editor.scene->CreateGameObject("source");obj.SetParent(&parent);obj.transform.position={1,2,3};obj.transform.rotation={15,25,35};obj.transform.localScale={2,3,4};auto sourceID=obj.GetID();auto originalWorld=Vector3(obj.GetWorldMatrix()[3]);
    auto& light=obj.AddComponent<Light>();light.intensity=3.1415927f;light.enabled=false;obj.AddComponent<SpriteRenderer>().asset="image-id";
    auto& child=editor.scene->CreateGameObject("child");child.SetParent(&obj);child.AddComponent<Rigidbody>().mass=7;
    editor.selected=sourceID;clipboard.CopyObject(*editor.scene,sourceID,registry);light.intensity=99;
    auto copyID=clipboard.PasteObject(editor,registry);auto* copy=editor.scene->Find(copyID);
    Check(copyID!=sourceID&&copy->name=="source (1)","Pasted object gets a fresh ID and numbered name");Check(copy->GetParent()->GetID()==parentID,"Parent preserved");Check(glm::length(Vector3(copy->GetWorldMatrix()[3])-originalWorld)<.0001f,"Same position");Check(copy->GetComponent<Light>()->intensity==3.1415927f&&!copy->GetComponent<Light>()->enabled,"Snapshot preserves exact values and enabled");
    bool foundChild=false;for(auto& node:editor.scene->GetObjects())if(node->GetParent()==copy)foundChild=node->name=="child (1)"&&node->GetComponent<Rigidbody>()->mass==7;Check(foundChild,"Subtree paste numbers child names");
    auto count=editor.scene->GetObjectCount();editor.Undo();Check(editor.scene->GetObjectCount()==count-2,"Paste undo atomic");editor.Redo();Check(editor.scene->GetObjectCount()==count,"Paste redo");
    editor.selected=sourceID;clipboard.CutObject(editor,registry);Check(!editor.scene->Find(sourceID),"Cut removes source");editor.Undo();Check(editor.scene->Find(sourceID)!=nullptr,"Cut undo");
    auto cutPaste=clipboard.PasteObject(editor,registry);Check(glm::length(Vector3(editor.scene->Find(cutPaste)->GetWorldMatrix()[3])-originalWorld)<.0001f,"Clipboard survives undo and stays in place");
    Check(editor.scene->Find(cutPaste)->name=="source (2)","Repeated paste continues numbering after undo");
    auto& destination=editor.scene->CreateGameObject("destination");destination.AddComponent<MeshRenderer>();auto& existing=destination.AddComponent<Light>();existing.intensity=1;
    auto* source=editor.scene->Find(sourceID);clipboard.CopyComponent(*source,"Light",registry);auto before=SceneCodec(registry).Encode(*editor.scene);clipboard.PasteComponents(destination,registry);editor.Record(before);
    Check(destination.GetComponent<Light>()==&existing&&existing.intensity==99&&!existing.enabled,"Existing type overwritten without replacement");size_t attached=0;destination.EachComponent([&](Component&){++attached;});Check(attached==2,"No duplicate component");
    auto destID=destination.GetID();editor.Undo();Check(editor.scene->Find(destID)->GetComponent<Light>()->intensity==1,"Component paste undo");editor.Redo();Check(editor.scene->Find(destID)->GetComponent<Light>()->intensity==99,"Component paste redo");
    source=editor.scene->Find(sourceID);clipboard.CopyAllComponents(*source,registry);clipboard.PasteComponents(*editor.scene->Find(destID),registry);Check(editor.scene->Find(destID)->GetComponent<SpriteRenderer>()->asset=="image-id","Missing component added");Check(editor.scene->Find(destID)->GetComponent<MeshRenderer>()!=nullptr,"Unrelated destination component retained");Check(editor.scene->Find(destID)->transform.position==source->transform.position,"All includes Transform");
    EditorSession other(registry);auto otherID=clipboard.PasteObject(other,registry);Check(glm::length(Vector3(other.scene->Find(otherID)->GetWorldMatrix()[3])-originalWorld)<.0001f,"Cross scene world position");
    other.selected=otherID;clipboard.DeleteObject(other);Check(other.scene->GetObjectCount()==0,"Recursive delete");other.Undo();Check(other.scene->GetObjectCount()==2,"Delete undo");
    other.Play();auto runningCount=other.scene->GetObjectCount();Check(clipboard.PasteObject(other,registry)==0&&other.scene->GetObjectCount()==runningCount,"No editing during play");other.Stop();
    EditorSession multi(registry);
    auto& root=multi.scene->CreateGameObject("root");auto rootID=root.GetID();
    auto& nested=multi.scene->CreateGameObject("nested");nested.SetParent(&root);auto nestedID=nested.GetID();
    auto& leaf=multi.scene->CreateGameObject("leaf");leaf.SetParent(&nested);auto leafID=leaf.GetID();
    auto& sibling=multi.scene->CreateGameObject("sibling");auto siblingID=sibling.GetID();
    multi.Select(nestedID);multi.Select(siblingID,true);
    Check(multi.IsSelected(nestedID)&&multi.IsSelected(siblingID)&&multi.selected==siblingID,"Shared additive selection");
    multi.DeleteSelection();Check(!multi.scene->Find(nestedID)&&!multi.scene->Find(leafID)&&!multi.scene->Find(siblingID)&&multi.scene->Find(rootID),"Batch delete includes descendants");
    multi.Undo();Check(multi.scene->GetObjectCount()==4,"Batch delete one undo");
    multi.SelectChildren(rootID);Check(multi.selection.size()==3&&multi.selected==rootID,"Select subtree with parent pivot");
    auto* restoredRoot=multi.scene->Find(rootID);auto* restoredLeaf=multi.scene->Find(leafID);
    restoredRoot->transform.position={3,4,5};restoredRoot->transform.rotation={0,0,90};restoredRoot->transform.localScale={2,2,2};
    multi.scene->Find(nestedID)->transform.position={1,0,0};restoredLeaf->transform.position={1,0,0};
    Check(glm::length(Vector3(restoredLeaf->GetWorldMatrix()[3])-Vector3(3,8,5))<.001f,"Nested children inherit parent translation rotation scale");
    multi.Select(rootID);Check(multi.selection.size()==1&&restoredLeaf->GetParent()!=nullptr,"Selection keeps hierarchy");
    multi.SelectChildren(rootID);multi.DeleteSelection();Check(multi.scene->GetObjectCount()==1,"Overlapping selected parent children deleted safely");
    multi.Undo();multi.SyncSelection();Check(multi.scene->GetObjectCount()==4,"Overlapping delete atomic undo");
    std::cout<<"Clipboard object/component/undo tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
