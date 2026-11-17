#pragma once
#include "ShadowMaps.h"
#include <unordered_map>

namespace Rice {
// Exact CPU snapshots: no hash collisions and no retained GameObject pointers.
// Texture ownership is weak so the cache cannot change GPU resource lifetimes.
class ShadowInvalidation {
public:
    struct Caster {
        EntityID id=0;
        SpatialBounds bounds;
        size_t begin=0,end=0,textureBegin=0,textureEnd=0;
    };
    struct Snapshot {
        std::vector<uint64_t> data;
        std::vector<std::weak_ptr<Texture2D>> textures;
        std::vector<Caster> casters;
    };
    struct Stats {size_t changedCasters=0,changedBounds=0,affectedLights=0,bvhBuilds=0;};
private:
    Snapshot previous;
    uint64_t scene=0;
    bool selective=true,previousSelective=true,changed=false;
    std::array<uint64_t,8> revisions{};
    Stats stats;
    SpatialBVH changes;
    static bool Valid(const SpatialBounds& box){
        for(int axis=0;axis<3;++axis)
            if(!std::isfinite(box.low[axis])||!std::isfinite(box.high[axis])||box.low[axis]>box.high[axis])return false;
        return true;
    }
    static bool SameTextures(const Snapshot& a,size_t begin,size_t end,const Snapshot& b,size_t otherBegin,size_t otherEnd){
        if(end-begin!=otherEnd-otherBegin)return false;
        for(size_t i=0;i<end-begin;++i){
            const auto& first=a.textures[begin+i];const auto& second=b.textures[otherBegin+i];
            if(first.owner_before(second)||second.owner_before(first))return false;
        }
        return true;
    }
    static bool Same(const Snapshot& a,const Caster& first,const Snapshot& b,const Caster& second){
        return first.bounds.low==second.bounds.low&&first.bounds.high==second.bounds.high&&
            first.end-first.begin==second.end-second.begin&&
            std::equal(a.data.begin()+first.begin,a.data.begin()+first.end,b.data.begin()+second.begin)&&
            SameTextures(a,first.textureBegin,first.textureEnd,b,second.textureBegin,second.textureEnd);
    }
public:
    void SetSelectiveEnabled(bool enabled){selective=enabled;}
    const auto& Revisions()const{return revisions;}
    const Stats& GetStats()const{return stats;}
    bool Changed()const{return changed;}
    void Update(uint64_t sceneID,Snapshot next,const std::vector<ShadowLight>& lights){
        stats={};changed=false;
        bool forceAll=scene!=sceneID||selective!=previousSelective;
        std::vector<SpatialBounds> dirtyBounds;
        auto add=[&](const SpatialBounds& bounds){
            if(!Valid(bounds)){forceAll=true;return;}
            dirtyBounds.push_back(bounds);
        };
        if(!selective){
            // Benchmark/control path retains the previous global comparison.
            // Per-caster change counts and the dirty BVH are not computed here.
            changed=forceAll||next.data!=previous.data||
                !SameTextures(next,0,next.textures.size(),previous,0,previous.textures.size());
            forceAll=changed;
        }else if(scene!=sceneID){
            changed=true;stats.changedCasters=next.casters.size();
        }else{
            std::vector<bool> seen(previous.casters.size(),false);
            std::unordered_map<EntityID,size_t> byID;
            bool indexed=false;
            for(size_t i=0;i<next.casters.size();++i){
                const auto& current=next.casters[i];size_t old=previous.casters.size();
                if(i<previous.casters.size()&&previous.casters[i].id==current.id)old=i;
                else{
                    if(!indexed){
                        byID.reserve(previous.casters.size());
                        for(size_t j=0;j<previous.casters.size();++j)byID.emplace(previous.casters[j].id,j);
                        indexed=true;
                    }
                    if(auto found=byID.find(current.id);found!=byID.end())old=found->second;
                }
                if(old<previous.casters.size()){
                    seen[old]=true;
                    if(Same(next,current,previous,previous.casters[old]))continue;
                    add(previous.casters[old].bounds);
                }
                ++stats.changedCasters;add(current.bounds);
            }
            for(size_t i=0;i<previous.casters.size();++i)if(!seen[i]){
                ++stats.changedCasters;add(previous.casters[i].bounds);
            }
            changed=forceAll||stats.changedCasters!=0;
        }
        stats.changedBounds=dirtyBounds.size();
        const size_t count=std::min(lights.size(),revisions.size());
        bool needTree=false;
        for(size_t i=0;i<count;++i)needTree=needTree||(lights[i].castsShadow&&lights[i].type==0);
        if(changed&&!forceAll&&needTree&&!dirtyBounds.empty()){
            changes.Build(dirtyBounds);++stats.bvhBuilds;
        }
        for(size_t i=0;i<count;++i){
            const auto& light=lights[i];if(!light.castsShadow)continue;
            bool affected=forceAll||(changed&&light.type==1);
            if(changed&&!affected&&light.type==0){
                // The six rendered frusta cover a cube, not a sphere. Keeping
                // its corners also preserves PCF/normal-offset dependencies.
                float range=std::max(light.range,.001f);
                float magnitude=1.f+range+std::max({std::abs(light.position.x),std::abs(light.position.y),std::abs(light.position.z)});
                Vector3 extent(range+1e-5f*magnitude);
                SpatialBounds influence{light.position-extent,light.position+extent};
                if(!std::isfinite(light.range)||!Valid(influence))affected=true;
                else if(needTree&&!dirtyBounds.empty())changes.Query(
                    [&](const SpatialBounds& box){return !affected&&box.Overlaps(influence);},
                    [&](size_t){affected=true;});
            }
            if(affected){++revisions[i];++stats.affectedLights;}
        }
        previous=std::move(next);scene=sceneID;previousSelective=selective;
    }
};
}
