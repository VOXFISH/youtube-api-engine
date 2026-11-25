#pragma once
#include "Rice/Benchmark.h"
#include "Rice/AssetID.h"
#include "ProjectFile.h"
#include "Scene/Gameplay.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
namespace Rice {
namespace fs=std::filesystem;
inline void AtomicWrite(const fs::path& path,const std::string& data){
    RICE_BENCHMARK_FUNCTION();
    if(!path.parent_path().empty())fs::create_directories(path.parent_path());auto temp=path;temp+=".tmp";
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out.write(data.data(),data.size());out.flush();if(!out)throw std::runtime_error("Cannot write "+temp.string());}
    // Windows rename cannot replace; retain a recoverable backup until replacement succeeds.
    auto backup=path;backup+=".bak";bool existed=fs::exists(path);
    if(existed){fs::copy_file(path,backup,fs::copy_options::overwrite_existing);fs::remove(path);}
    try{fs::rename(temp,path);}catch(...){if(existed)fs::copy_file(backup,path,fs::copy_options::overwrite_existing);throw;}
}
inline std::string ReadFile(const fs::path& path){ RICE_BENCHMARK_FUNCTION(); std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot read "+path.string());std::ostringstream out;out<<in.rdbuf();return out.str();}
struct AssetEntry{std::string id;fs::path path;};
class AssetDatabase {
    fs::path root;std::vector<AssetEntry> entries;std::map<std::string,std::weak_ptr<Texture2D>> textures;
public:
    explicit AssetDatabase(fs::path directory):root(fs::absolute(std::move(directory))){ RICE_BENCHMARK_FUNCTION(); fs::create_directories(root);Refresh();}
    const fs::path& Root()const{ RICE_BENCHMARK_FUNCTION(); return root;}
    const auto& Entries()const{ RICE_BENCHMARK_FUNCTION(); return entries;}
    void Refresh(){ RICE_BENCHMARK_FUNCTION(); std::vector<AssetEntry> next;for(auto& file:fs::recursive_directory_iterator(root)){if(!file.is_regular_file())continue;auto ext=file.path().extension();if(ext==".meta"||ext==".bak"||ext==".tmp")continue;
        auto meta=file.path();meta+=".meta";std::string id;if(fs::exists(meta)){std::istringstream input(ReadFile(meta));input>>id;}else{id=NewAssetID();AtomicWrite(meta,id);}
        if(id.empty()||std::any_of(next.begin(),next.end(),[&](auto& e){ RICE_BENCHMARK_FUNCTION(); return e.id==id;}))throw std::runtime_error("Invalid/duplicate asset ID");next.push_back({id,fs::relative(file.path(),root)});}entries=std::move(next);}
    fs::path Resolve(const std::string& id)const{ RICE_BENCHMARK_FUNCTION(); for(auto& e:entries)if(e.id==id)return root/e.path;throw std::runtime_error("Missing asset: "+id);}
    std::string Import(const fs::path& source){ RICE_BENCHMARK_FUNCTION(); if(!fs::is_regular_file(source))throw std::runtime_error("Asset file not found");auto destination=root/source.filename();if(fs::exists(destination))throw std::runtime_error("Asset name already exists");fs::copy_file(source,destination);Refresh();for(auto& e:entries)if(e.path==source.filename())return e.id;throw std::runtime_error("Import failed");}
    void Rename(const std::string& id,const fs::path& relative){ RICE_BENCHMARK_FUNCTION(); if(relative.empty()||relative.is_absolute())throw std::invalid_argument("Relative asset path required");for(auto& part:relative)if(part=="..")throw std::invalid_argument("Asset path escapes project");auto old=Resolve(id),target=root/relative;if(fs::exists(target))throw std::runtime_error("Asset already exists");fs::create_directories(target.parent_path());fs::rename(old,target);try{fs::rename(fs::path(old).concat(".meta"),fs::path(target).concat(".meta"));}catch(...){fs::rename(target,old);throw;}Refresh();}
    Ref<Texture2D> Texture(const std::string& id){ RICE_BENCHMARK_FUNCTION(); if(auto cached=textures[id].lock())return cached;auto result=Texture2D::Create(ProjectFile::Utf8(Resolve(id)));if(!result||!result->GetWidth())throw std::runtime_error("Image load failed");textures[id]=result;return result;}
};
}
