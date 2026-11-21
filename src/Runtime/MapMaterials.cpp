#include "Runtime/MapMaterials.hpp"
#include "Runtime/MapRemap.hpp"
#include "Core/Log.hpp"
#include "json.hpp"
#include <Windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#pragma comment(lib,"advapi32.lib")

namespace
{
SRWLOCK lock = SRWLOCK_INIT;
std::map<std::string,std::string,std::less<>> routes;
nskinzcs2::runtime::MapMaterialStatus state;
std::atomic<std::uint64_t> hits {0};
struct ExclusiveLock
{
    ExclusiveLock() { AcquireSRWLockExclusive(&lock); }
    ~ExclusiveLock() { ReleaseSRWLockExclusive(&lock); }
};
struct SharedLock
{
    SharedLock() { AcquireSRWLockShared(&lock); }
    ~SharedLock() { ReleaseSRWLockShared(&lock); }
};

std::string Sha(std::span<const std::uint8_t> bytes)
{
    HCRYPTPROV provider=0;HCRYPTHASH hash=0;
    if (!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT))
        throw std::runtime_error("SHA-256 provider unavailable");
    struct Provider {HCRYPTPROV p;~Provider(){CryptReleaseContext(p,0);}} provider_scope{provider};
    if (!CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)) throw std::runtime_error("SHA-256 initialization failed");
    struct Hash {HCRYPTHASH h;~Hash(){CryptDestroyHash(h);}} hash_scope{hash};
    if (bytes.size()>MAXDWORD || !CryptHashData(hash,bytes.data(),static_cast<DWORD>(bytes.size()),0))
        throw std::runtime_error("SHA-256 input failed");
    BYTE digest[32]{};DWORD size=32;
    if (!CryptGetHashParam(hash,HP_HASHVAL,digest,&size,0)||size!=32) throw std::runtime_error("SHA-256 result failed");
    constexpr char hex[]="0123456789abcdef";std::string result;result.reserve(64);
    for(auto byte:digest){result+=hex[byte>>4];result+=hex[byte&15];}return result;
}
std::vector<std::uint8_t> Read(const std::filesystem::path& path)
{
    std::error_code error;const auto size=std::filesystem::file_size(path,error);
    if(error||size>2*1024*1024) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::ifstream stream(path,std::ios::binary);
    if(!stream.read(reinterpret_cast<char*>(bytes.data()),bytes.size())) return {};
    return bytes;
}
std::string Utf8(const std::filesystem::path& path)
{
    const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};
}
std::span<const std::uint8_t> EmbeddedPack()
{
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&nskinzcs2::runtime::PrepareMapMaterials),&module)) return {};
    const auto resource=FindResourceW(module,MAKEINTRESOURCEW(500),RT_RCDATA);
    if(!resource)return {};
    const auto size=SizeofResource(module,resource);
    const auto data=LockResource(LoadResource(module,resource));
    return {static_cast<const std::uint8_t*>(data),data?size:0};
}
bool ValidMaterial(std::span<const std::uint8_t> bytes)
{
    if(bytes.size()<40||bytes.size()>2*1024*1024)return false;
    std::uint16_t version=0;std::uint32_t relative=0,count=0,file_size=0;
    std::memcpy(&file_size,bytes.data(),4);std::memcpy(&version,bytes.data()+4,2);
    std::memcpy(&relative,bytes.data()+8,4);std::memcpy(&count,bytes.data()+12,4);
    if(version!=12||file_size!=bytes.size()||count<1||count>64||relative>bytes.size()-8)return false;
    const auto directory=std::size_t(relative)+8;
    if(count>(bytes.size()-directory)/12)return false;
    bool data=false;
    for(std::size_t i=0;i<count;++i){
        const auto row=directory+12*i;std::uint32_t delta=0,size=0;
        std::memcpy(&delta,bytes.data()+row+4,4);std::memcpy(&size,bytes.data()+row+8,4);
        if(delta>bytes.size()-(row+4)||size>bytes.size()-(row+4+delta))return false;
        if(std::memcmp(bytes.data()+row,"DATA",4)==0)data=true;
    }return data;
}
bool Activate(const nskinzcs2::runtime::PreparedMapMaterials& prepared,std::string_view map)
{
    std::map<std::string,std::string,std::less<>> next;
    for(const auto& path:prepared.files)next.emplace(path,Utf8(nskinzcs2::runtime::MapMaterialCacheFile(prepared.root,path)));
    ExclusiveLock guard;
    routes.swap(next);state.enabled=true;state.scene_matches=false;state.map=map;
    state.file_count=routes.size();state.issue.clear();hits.store(0,std::memory_order_relaxed);
    return true;
}
}

bool nskinzcs2::runtime::IsMaterialMapSupported(std::string_view map) noexcept
{return std::find(kMaterialMaps.begin(),kMaterialMaps.end(),map)!=kMaterialMaps.end();}
std::filesystem::path nskinzcs2::runtime::MapMaterialCacheFile(const std::filesystem::path& root,std::string_view path)
{
    const auto bytes=std::span(reinterpret_cast<const std::uint8_t*>(path.data()),path.size());
    return root/(Sha(bytes).substr(0,32)+".vmat_c");
}
bool nskinzcs2::runtime::IsMapMaterialPathSafe(std::string_view path) noexcept
{
    return path.starts_with("materials/")&&path.ends_with(".vmat_c")&&path.size()<1024&&path.find("..") == path.npos&&
        std::all_of(path.begin(),path.end(),[](unsigned char c){return(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='/'||c=='_'||c=='-'||c=='.';});
}
bool nskinzcs2::runtime::MaterialSceneMatches(std::string_view basis,std::string_view scene) noexcept
{
    if(!IsMaterialMapSupported(basis))return false;
    if(scene.starts_with("maps/"))scene.remove_prefix(5);
    if(scene.ends_with(".vmap_c"))scene.remove_suffix(7);
    if(scene.ends_with(".vmap"))scene.remove_suffix(5);
    return scene==basis||(basis=="de_mirage"&&scene=="de_mirage_water")||
        (basis=="de_inferno"&&scene=="de_inferno_water");
}
nskinzcs2::runtime::PreparedMapMaterials nskinzcs2::runtime::PrepareMapMaterialBytes(
    std::span<const std::uint8_t> pack,std::string_view map,const std::filesystem::path& cache)
{
    PreparedMapMaterials result;
    try{
        if(!IsMaterialMapSupported(map)||!cache.is_absolute())throw std::runtime_error("unsupported map/cache");
        if(pack.size()<12||pack.size()>32*1024*1024||std::memcmp(pack.data(),"NSKMAT01",8))throw std::runtime_error("invalid material pack");
        std::uint32_t length=0;std::memcpy(&length,pack.data()+8,4);
        if(length>2*1024*1024||length>pack.size()-12)throw std::runtime_error("invalid material manifest size");
        const auto manifest=nlohmann::json::parse(pack.begin()+12,pack.begin()+12+length);
        const auto& files=manifest.at("files");
        if(manifest.at("version")!=1||!files.is_array()||files.size()>5000)throw std::runtime_error("unsupported material manifest");
        const auto blob=pack.subspan(12+length);
        result.root=cache/Sha(pack)/std::string(map);
        // Validate every selected record before creating any cache files.
        struct File {std::string path;std::span<const std::uint8_t> bytes;std::string sha;};
        std::vector<File> selected;
        for(const auto& file:files){
            if(file.at("map").get<std::string>()!=map)continue;
            const auto path=file.at("path").get<std::string>();
            const auto offset=file.at("offset").get<std::uint64_t>(),size=file.at("size").get<std::uint64_t>();
            if(!IsMapMaterialPathSafe(path)||offset>blob.size()||size>blob.size()-offset||size>2*1024*1024)
                throw std::runtime_error("unsafe material record");
            const auto bytes=blob.subspan(static_cast<std::size_t>(offset),static_cast<std::size_t>(size));
            const auto sha=file.at("sha256").get<std::string>();
            if(!ValidMaterial(bytes)||sha!=Sha(bytes))throw std::runtime_error("material resource/hash mismatch");
            if(std::any_of(selected.begin(),selected.end(),[&](const auto& old){return old.path==path;}))throw std::runtime_error("duplicate material path");
            selected.push_back({path,bytes,sha});
        }
        if(selected.empty())throw std::runtime_error("map has no material records");
        for(const auto& item:selected){
            const auto target=MapMaterialCacheFile(result.root,item.path);
            for(auto part=target;part!=cache.parent_path()&&!part.empty();part=part.parent_path())
                if(const auto attributes=GetFileAttributesW(part.c_str());
                    attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))
                    throw std::runtime_error("material cache contains a reparse point");
            std::filesystem::create_directories(target.parent_path());
            if(Sha(Read(target))!=item.sha){
                auto temporary=target;temporary+=L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
                {std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);
                 stream.write(reinterpret_cast<const char*>(item.bytes.data()),item.bytes.size());stream.close();
                 if(!stream)throw std::runtime_error("material cache write failed");}
                if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error("material cache publication failed");
            }
            result.files.push_back(item.path);
        }
        result.success=true;
    }catch(const std::exception& error){result.issue=L"Material preparation failed: ";const std::string text=error.what();result.issue.append(text.begin(),text.end());result.files.clear();}
    return result;
}
nskinzcs2::runtime::PreparedMapMaterials nskinzcs2::runtime::PrepareMapMaterials(std::string_view map)
{
    std::error_code error;
    const auto temporary = std::filesystem::temp_directory_path(error);
    if (error) return {false, {}, {}, L"Material preparation failed: temporary folder unavailable"};
    return PrepareMapMaterialBytes(EmbeddedPack(),map,temporary/L"nSkinzCS2"/L"map-materials");
}
bool nskinzcs2::runtime::ConfigureMapMaterials(const model::MapMaterialProfile& profile)
{
    if(!profile.enabled){StopMapMaterials();return true;}
    const auto prepared=PrepareMapMaterials(profile.map);
    if(!prepared.success){StopMapMaterials();ExclusiveLock guard;state.issue=prepared.issue;return false;}
    std::wstring issue;
    if(!ArmMapMaterialFileLoading(prepared.root,issue)){
        StopMapMaterials();ExclusiveLock guard;state.issue=issue;return false;
    }
    Activate(prepared,profile.map);
    Logger::Info(L"[MapMaterials] verified and armed "+std::to_wstring(prepared.files.size())+L" material files; waiting for matching map/resource requests.");
    return true;
}
void nskinzcs2::runtime::SetMapMaterialContext(std::string_view scene) noexcept
{
    ExclusiveLock guard;state.scene_matches=state.enabled&&MaterialSceneMatches(state.map,scene);
}
bool nskinzcs2::runtime::ResolveMapMaterialFile(std::string_view request,char* output,std::size_t capacity) noexcept
{
    if(request.size()>1024||!output||!capacity)return false;
    char normalized[1030]{};std::size_t size=0;
    for(char c:request){if(c=='\\')c='/';if(c>='A'&&c<='Z')c+=('a'-'A');normalized[size++]=c;}
    std::string_view key(normalized,size);
    if(key.ends_with(".vmat")){normalized[size++]='_';normalized[size++]='c';key={normalized,size};}
    SharedLock guard;
    const auto found=state.enabled&&state.scene_matches?routes.find(key):routes.end();
    const bool match=found!=routes.end()&&found->second.size()<capacity;
    if(match)std::memcpy(output,found->second.c_str(),found->second.size()+1);
    if(match)hits.fetch_add(1,std::memory_order_relaxed);return match;
}
nskinzcs2::runtime::MapMaterialStatus nskinzcs2::runtime::SnapshotMapMaterials()
{
    SharedLock guard;auto result=state;
    result.redirected_requests=hits.load(std::memory_order_relaxed);return result;
}
void nskinzcs2::runtime::StopMapMaterials() noexcept
{
    ExclusiveLock guard;routes.clear();state={};hits.store(0,std::memory_order_relaxed);
}
#ifdef NSKINZCS2_TESTS
bool nskinzcs2::runtime::ActivatePreparedMapMaterialsForTests(const PreparedMapMaterials& prepared,std::string_view map)
{return prepared.success&&Activate(prepared,map);}
#endif
