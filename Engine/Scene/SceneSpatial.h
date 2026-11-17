#pragma once
#include "Scene.h"
#include "SpatialBVH.h"

namespace Rice {
// A prepared render snapshot is reused by all six capture views. Refreshing
// compares live inputs, so direct edits and parent/group/anchor motion are seen.
class SceneSpatialCache {
public:
    struct Entry {
        GameObject* object=nullptr;
        EntityID id=0;
        Matrix4 world{1};
        SpatialBounds local,bounds;
    };
    struct Stats {size_t worldSamples=0,boundsUpdates=0,builds=0,refits=0;};
private:
    uint64_t sceneID=0;
    std::vector<Entry> entries;
    SpatialBVH tree;
    Stats stats;
    unsigned refitsSinceBuild=0;
public:
    const auto& Entries()const{return entries;}
    const SpatialBVH& Tree()const{return tree;}
    const Stats& GetStats()const{return stats;}
    template<class Provider> void Prepare(Scene& scene,Provider localBounds){
        stats={};std::vector<Entry> next;next.reserve(scene.GetObjects().size());
        bool membership=sceneID!=scene.GetInstanceID(),changed=false;
        for(auto& object:scene.GetObjects()){
            if(!object->IsActive())continue;
            auto local=localBounds(*object);if(!local)continue;
            Entry entry;entry.object=object.get();entry.id=object->GetID();
            entry.world=object->GetWorldMatrix();++stats.worldSamples;
            entry.local={local->first,local->second};
            const size_t i=next.size();
            bool same=!membership&&i<entries.size()&&entries[i].id==entry.id;
            if(!same)membership=true;
            if(same&&entries[i].world==entry.world&&entries[i].local.low==entry.local.low&&entries[i].local.high==entry.local.high)
                entry.bounds=entries[i].bounds;
            else {entry.bounds=SpatialBounds::Transform(entry.world,entry.local);++stats.boundsUpdates;changed=true;}
            next.push_back(entry);
        }
        membership=membership||next.size()!=entries.size();
        entries=std::move(next);sceneID=scene.GetInstanceID();
        if(membership||changed){
            std::vector<SpatialBounds> bounds;bounds.reserve(entries.size());
            for(const auto& entry:entries)bounds.push_back(entry.bounds);
            // Periodic rebuild bounds the quality loss from repeated refits.
            if(membership||refitsSinceBuild>=64){tree.Build(bounds);++stats.builds;refitsSinceBuild=0;}
            else {tree.Refit(bounds);++stats.refits;++refitsSinceBuild;}
        }
    }
};
}
