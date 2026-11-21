#include "Runtime/LivePresentation.hpp"
#include "Runtime/KnifeHudName.hpp"
#include "Core/Log.hpp"
#include "../../third_party/live_cosmetics/src/game.h"
#include "../../third_party/live_cosmetics/src/skins.h"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <mutex>
#include <vector>
#include <algorithm>
#include <format>

namespace nskinzcs2::live_test::presentation {
namespace {
namespace port=live_port;
void Note(const std::string& text) {
    const int count=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(count<=0)return;
    std::wstring wide(static_cast<std::size_t>(count),0);
    MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),wide.data(),count);
    Logger::Info(wide);
}
using Bytes=std::span<const std::uint8_t>;
bool Match(Bytes b,std::size_t at,std::initializer_list<int> pattern) noexcept {
    if(at>b.size() || pattern.size()>b.size()-at)return false;
    for(int c:pattern) {if(c>=0 && b[at]!=c)return false;++at;} return true;
}
template<class T> T Read(Bytes b,std::size_t at) noexcept {
    T v{};if(at<=b.size() && sizeof(v)<=b.size()-at)std::memcpy(&v,b.data()+at,sizeof(v));return v;
}
std::uint32_t Relative(Bytes b,std::size_t at,int length,int displacement) noexcept {
    if(at>b.size() || static_cast<std::size_t>(length)>b.size()-at)return 0;
    const auto result=static_cast<std::int64_t>(at)+length+Read<std::int32_t>(b,at+displacement);
    return result>0 && result<static_cast<std::int64_t>(b.size()) ? static_cast<std::uint32_t>(result):0;
}
struct Section {std::uint32_t start=0,size=0;};
Section Text(Bytes b) noexcept {
    if(Read<std::uint16_t>(b,0)!=IMAGE_DOS_SIGNATURE)return {};
    const auto pe=Read<std::uint32_t>(b,0x3c);
    if(pe>b.size() || b.size()-pe<0x100 || Read<std::uint32_t>(b,pe)!=IMAGE_NT_SIGNATURE ||
        Read<std::uint16_t>(b,pe+24)!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return {};
    const auto count=Read<std::uint16_t>(b,pe+6),optional=Read<std::uint16_t>(b,pe+20);
    if(count>96)return {};
    for(unsigned i=0;i<count;++i) {
        const std::size_t a=pe+24+optional+i*40;
        if(a>b.size() || b.size()-a<40)return {};
        if(std::memcmp(b.data()+a,".text\0",6))continue;
        const auto start=Read<std::uint32_t>(b,a+12),size=Read<std::uint32_t>(b,a+8);
        if(start<=b.size() && size<=b.size()-start)return {start,size};
    }return {};
}
}
HudEntrypoints DiscoverHud(Bytes b) noexcept {
    HudEntrypoints out;const auto text=Text(b);if(!text.size)return {};
    unsigned getters=0,invalidators=0,rows=0,bindings=0;
    for(std::size_t p=text.start;p<text.start+text.size;++p) {
        if(Match(b,p,{0x48,0x89,0x5c,0x24,8,0x57,0x48,0x83,0xec,0x20,0x48,0x83,0xb9,0,2,0,0,0,0x0f,0xb6,0xfa,0x48,0x8b,0xd9,0x75,-1})) {out.getter=static_cast<std::uint32_t>(p);++getters;}
        if(Match(b,p,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48,0x8d,0xb9,0,2,0,0,0x48,0x8b,0xf1,0x48,0x8b,0x1f,0x48,0x85,0xdb,0x0f,0x84})) {out.invalidate=static_cast<std::uint32_t>(p);++invalidators;}
        if(p>=0x80 && Match(b,p,{0x48,0x63,0xc7,0x48,0x8d,0x0c,0xc0,0x49,0x8b,0x47,0x58,0x8b,0x54,0xc8,0x38}) &&
            Match(b,p-0x80,{0x48,0x85,0xd2,0x0f,0x84,-1,-1,-1,-1,0x48,0x8b,0xc4,0x48,0x89,0x50,0x10})) {out.update=static_cast<std::uint32_t>(p-0x80);++rows;}
    }
    if(getters!=1 || invalidators!=1 || rows!=1)return {};
    // Recover the game's own bounded row-removal operation from the verified
    // view update's stale-entity branch. Do not write cached definitions or vectors.
    unsigned removals=0;
    for(std::size_t p=out.update;p<out.update+0x200 && p<b.size();++p) {
        if(!Match(b,p,{0x8b,0xd7,0x49,0x8b,0xcf,0xe8,-1,-1,-1,-1,0x8b,0xf8,0xc6,0x84,0x24,0xb8,0,0,0,1}))continue;
        const auto target=Relative(b,p+5,5,1);
        if(Match(b,target,{0x4c,0x8b,0xdc,0x55,0x57,0x48,0x83,0xec,0x48,0x48,0x63,0x41,0x68,0x48,0x8b,0xf9,0x48,0x63,0xea})) {out.removeRow=target;++removals;}
    }
    if(removals!=1)return {};
    for(std::size_t p=text.start;p<text.start+text.size;++p) {
        // The HUD consumer reads this exact item, binds WeaponIcon--name and
        // calls the setter. Resolve both calls; a generic setter prologue is ambiguous.
        if(p>=10 && Match(b,p-10,{0x33,0xd2,0x49,0x8b,0xcf,0xe8,-1,-1,-1,-1}) &&
            Relative(b,p-5,5,1)==out.getter &&
            Match(b,p,{0x48,0x8d,0x0d,-1,-1,-1,-1,0xc7,0x44,0x24,0x40,0x51,0x45,0x61,1,0x48,0x89,0x4c,0x24,0x48,0x48,0x8d,0x54,0x24,0x40,0x48,0x8b,0xcb,0x4c,0x8b,0xc0,0xe8})) {
            const auto name=Relative(b,p,7,3),setter=Relative(b,p+31,5,1);
            if(name && Match(b,name,{'W','e','a','p','o','n','I','c','o','n','-','-','n','a','m','e',0}) &&
                Match(b,setter,{0x4c,0x8b,0xdc,0x49,0x89,0x5b,8,0x49,0x89,0x6b,0x10,0x49,0x89,0x73,0x18,0x57,0x48,0x83,0xec,0x60}) &&
                Match(b,setter+0x6c,{0x48,0x8b,0xcf,0x48,0x8b,7,0xff,0x10,0x4c,0x8b,0xc6,0x48,0x8b,0xd5,0x48,0x8b,0xc8,0x4c,0x8b,8,0x41,0xff,0x91,0x60,9,0,0})) {out.setter=setter;++bindings;}
        }
        if(Match(b,p,{0x48,0x8d,5,-1,-1,-1,-1,0xc7,0x44,0x24,0x20,0,0,0,0,0x48,0x89,1})) {
            const auto table=Relative(b,p,7,3);const auto preferred=Read<std::uint64_t>(b,Read<std::uint32_t>(b,0x3c)+24+24);
            auto first=Read<std::uint64_t>(b,table);if(first>=preferred && first-preferred<b.size())first-=preferred;
            else if(first>=reinterpret_cast<std::uintptr_t>(b.data()) && first-reinterpret_cast<std::uintptr_t>(b.data())<b.size())first-=reinterpret_cast<std::uintptr_t>(b.data());
            else continue;
            if(!Match(b,static_cast<std::size_t>(first),{0x48,0x8b,0x41,8,0xc3}))continue;
            if(out.panelTable && out.panelTable!=table)return {};out.panelTable=table;
        }
    }
    return bindings==1 && out ? out:HudEntrypoints{};
}
namespace {
HudEntrypoints entry;std::uintptr_t client=0;std::atomic_bool active=false,observing=false,broken=false;
std::atomic<std::uint64_t> hud_callbacks{0};
using UpdateFn=void(*)(void*,void*,bool);UpdateFn original=nullptr;
struct NativeCalls {
    void*(*resolve)(std::uint32_t)=&port::Game::FromHandleExact;
    bool(*demo)()=&port::Game::IsPlayingDemo;
    void(*invalidate)(void*)=nullptr;
    const char*(*name)(void*,bool)=nullptr;
    void(*set)(void*,const void*,const char*)=nullptr;
    int(*remove)(void*,int)=nullptr;
} calls;
struct Request {
    port::Skins::WeaponPresentation update;
    std::uint32_t owner=0,high=0,low=0;
    std::string name;runtime::KnifeHudLabelCommit commit;
    unsigned rebuilds=0;
    std::uint16_t previousDefinition=0;
    ULONGLONG queuedAt=0;
    bool submitted=false,pendingReported=false;
    const char* waiting="native view callback not matched yet";
};
std::mutex mutex;std::vector<Request> requests;
class Reader final:public runtime::KnifeGraphReader {
public:
    bool Read(std::uintptr_t p,void* out,std::size_t n)noexcept override {
        if(!port::Readable(reinterpret_cast<void*>(p),n))return false;
        __try {std::memcpy(out,reinterpret_cast<void*>(p),n);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool IsIdParameterNode(std::uintptr_t)noexcept override{return false;}
} reader;
bool Alive(const Request& r) {
    const auto& u=r.update;
    return calls.resolve(u.handle)==u.weapon &&
        static_cast<std::uint8_t*>(u.weapon)+port::O.econ_AttributeManager+port::O.container_Item==u.item &&
        port::Get<std::uint16_t>(u.item,port::O.item_DefIndex)==u.definition &&
        port::Get<std::uint32_t>(u.item,port::O.item_ItemIDHigh)==r.high &&
        port::Get<std::uint32_t>(u.item,port::O.item_ItemIDLow)==r.low &&
        port::Get<std::uint32_t>(u.weapon,port::O.ent_hOwnerEntity)==r.owner &&
        port::Get<int>(u.weapon,port::O.econ_FallbackPaintKit)==u.paint;
}
bool Generate(Request& r) noexcept {
    if(!active.load() || broken.load())return false;
    __try {
        if(!Alive(r))return false;
        calls.invalidate(r.update.item);
        if(!Alive(r) || port::Get<void*>(r.update.item,runtime::kKnifeItemNameCacheOffset))return false;
        const char* name=calls.name(r.update.item,true);
        if(!name)return false;
        std::size_t n=0;for(;n<1024;++n) {if(!port::Readable(name+n,1))return false;if(!name[n])break;}
        if(!n || n==1024)return false;
        r.name.assign(name,n);return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){broken.store(true);return false;}
}
void OnPresentation(const port::Skins::WeaponPresentation& update) {
    if(!observing.load())return;
    std::unique_lock guard(mutex,std::try_to_lock);if(!guard.owns_lock())return;
    const auto high=port::Get<std::uint32_t>(update.item,port::O.item_ItemIDHigh),low=port::Get<std::uint32_t>(update.item,port::O.item_ItemIDLow);
    auto found=std::find_if(requests.begin(),requests.end(),[&](const Request& r){return r.update.handle==update.handle;});
    if(found!=requests.end() && found->update.weapon==update.weapon && found->update.signature==update.signature &&
        found->update.paint==update.paint && found->high==high && found->low==low &&
        found->owner==port::Get<std::uint32_t>(update.weapon,port::O.ent_hOwnerEntity))return;
    const auto previous=(found!=requests.end() && found->update.weapon==update.weapon && found->update.item==update.item &&
        found->owner==port::Get<std::uint32_t>(update.weapon,port::O.ent_hOwnerEntity)) ? found->update.definition:0;
    if(found!=requests.end())requests.erase(found);
    if(requests.size()>=64)requests.erase(requests.begin());
    Request r;r.update=update;r.high=high;r.low=low;r.owner=port::Get<std::uint32_t>(update.weapon,port::O.ent_hOwnerEntity);
    r.previousDefinition=static_cast<std::uint16_t>(previous);r.queuedAt=GetTickCount64();
    if(Generate(r)) {
        Note(std::format("live HUD native full name queued: handle {}, definition {}, paint {}, name '{}'",update.handle,update.definition,update.paint,r.name));
    }else Note(std::format("live HUD native name unavailable: handle {}; identical request will not retry every frame",update.handle));
    requests.push_back(std::move(r));
}
bool Submit(void* view,void* pawn,bool changed=false) {
    if(!active.load() || broken.load() || !pawn || !calls.demo())return true;
    std::unique_lock guard(mutex,std::try_to_lock);if(!guard.owns_lock())return true;
    std::erase_if(requests,[](const Request& r){return !Alive(r);});
    for(auto& r:requests) {
        if(r.name.empty())continue;
        if(calls.resolve(r.owner)!=pawn){r.waiting="configured weapon is not owned by this HUD pawn";continue;}
        if(changed)r.commit.Reset(); // Native refresh can retain/reuse the same row and panel addresses.
        auto row=runtime::FindKnifeHudLabelRow(reinterpret_cast<std::uintptr_t>(view),r.update.handle,r.update.definition,reader);
        if(!row && r.rebuilds<2 && calls.remove && port::Skins::IsKnife(r.update.definition))
        for(const auto cached:{r.previousDefinition,r.update.sourceDefinition}) {
            if(cached==r.update.definition || !port::Skins::IsKnife(cached))continue;
            const auto stale=runtime::FindKnifeHudLabelRow(reinterpret_cast<std::uintptr_t>(view),r.update.handle,cached,reader);
            std::uintptr_t rows=0,table=0;
            if(stale && reader.Read(stale->panel,&table,sizeof(table)) && table==client+entry.panelTable &&
                reader.Read(reinterpret_cast<std::uintptr_t>(view)+0x58,&rows,sizeof(rows)) && stale->address>=rows && (stale->address-rows)%0x48==0) {
                const auto index=(stale->address-rows)/0x48;
                if(index<64) {
                    ++r.rebuilds;r.commit.Reset();calls.remove(view,static_cast<int>(index));
                    r.waiting="old model row retired; waiting for native recreation";
                    Note(std::format("live HUD old-model row retired: handle {}, cached {}, target {}; native recreation on next view update",r.update.handle,cached,r.update.definition));
                    break;
                }
            }
        }
        if(!row){r.commit.Reset();if(!r.rebuilds)r.waiting="no matching full-handle/definition HUD row";continue;}
        r.waiting="HUD row found; checking panel identity";
        std::uintptr_t table=0,panel=0;
        if(!reader.Read(row->panel,&table,sizeof(table)) || table!=client+entry.panelTable ||
            !reader.Read(row->panel+8,&panel,sizeof(panel)) || !port::Readable(reinterpret_cast<void*>(panel),sizeof(void*)) ||
            !r.commit.Begin(r.update.signature,*row))continue;
        struct alignas(8) Token {std::uint32_t hash=0x01614551,padding=0;const char* text="WeaponIcon--name";} token;
        calls.set(reinterpret_cast<void*>(row->panel),&token,r.name.c_str());
        r.commit.Complete(true);r.submitted=true;
        Note(std::format("live right HUD label submitted: handle {}, definition {}, paint {}; exact native row",r.update.handle,r.update.definition,r.update.paint));
    }return true;
}
bool GuardedSubmit(void* view,void* pawn,bool changed) noexcept {
    __try{return Submit(view,pawn,changed);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void Hook(void* view,void* pawn,bool changed) {
    Logger::DeferredScope deferred;
    if(!original)return;original(view,pawn,changed);
    hud_callbacks.fetch_add(1,std::memory_order_relaxed);
    try {
        if(!GuardedSubmit(view,pawn,changed) && !broken.exchange(true))
            Note("live HUD submission faulted; label bridge suspended, cosmetic engine unchanged");
    }catch(...){broken.store(true);}
}
}
bool Initialize() {
    observing.store(true);port::Skins::SetPresentationListener(&OnPresentation);
    if(active.load())return true;if(broken.load())return false;
    const auto module=GetModuleHandleW(L"client.dll");if(!module)return false;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(reinterpret_cast<const std::uint8_t*>(module)+dos->e_lfanew);
    entry=DiscoverHud({reinterpret_cast<const std::uint8_t*>(module),nt->OptionalHeader.SizeOfImage});
    if(!entry) {Note("live HUD unavailable: native evidence not unique; no old RVA fallback");return false;}
    const auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    client=reinterpret_cast<std::uintptr_t>(module);void* trampoline=nullptr;
    calls.invalidate=reinterpret_cast<decltype(calls.invalidate)>(client+entry.invalidate);
    calls.name=reinterpret_cast<decltype(calls.name)>(client+entry.getter);
    calls.set=reinterpret_cast<decltype(calls.set)>(client+entry.setter);
    calls.remove=reinterpret_cast<decltype(calls.remove)>(client+entry.removeRow);
    if(MH_CreateHook(reinterpret_cast<void*>(client+entry.update),reinterpret_cast<void*>(&Hook),&trampoline)!=MH_OK)return false;
    original=reinterpret_cast<UpdateFn>(trampoline);
    if(MH_EnableHook(reinterpret_cast<void*>(client+entry.update))!=MH_OK) {MH_RemoveHook(reinterpret_cast<void*>(client+entry.update));original=nullptr;return false;}
    active.store(true);
    Note(std::format("live HUD bridge ready: getter +{:x}, invalidate +{:x}, update +{:x}, setter +{:x}, remove row +{:x}; original update runs first",entry.getter,entry.invalidate,entry.update,entry.setter,entry.removeRow));
    return true;
}
void Tick() {
    if(!observing.load())return;
    static ULONGLONG next=0;const auto now=GetTickCount64();if(now<next)return;next=now+250;
    std::unique_lock guard(mutex,std::try_to_lock);if(!guard.owns_lock())return;
    std::erase_if(requests,[](const Request& r){return !calls.demo() || !Alive(r);});
    for(auto& r:requests)if(!r.name.empty() && !r.submitted && !r.pendingReported && now-r.queuedAt>=1500) {
        r.pendingReported=true;
        Note(std::format("live HUD awaiting label: handle {}, source {}, previous {}, target {}, callbacks {}, stage '{}'",r.update.handle,
            r.update.sourceDefinition,r.previousDefinition,r.update.definition,hud_callbacks.load(std::memory_order_relaxed),r.waiting));
    }
    // Do not conflate a valid attribute list with a rendered charm. Captured
    // a matching module is evidence separate from the upstream replay request.
    // Owner matching alone cannot establish whether a rendered charm exists.
    std::string note;
    for(const auto& r:requests)if(r.update.keychain>=0) {
        const auto& modules=port::Skins::liveKeychainModules;
        const auto module=std::find_if(modules.begin(),modules.end(),[&](const auto& m){return m.owner==r.update.handle;});
        if(module==modules.end() && r.update.keychain>0)
            note="Keychain awaiting native verification: no module matched weapon "+std::to_string(r.update.definition)+" by full owner handle (modules observed: "+std::to_string(modules.size())+"). Upstream current-Demo replay requested on apply; visible result remains unverified.";
        else if(module!=modules.end() && module->definition!=r.update.keychain)
            note="Keychain awaiting native verification: module "+std::to_string(module->handle)+" has definition "+std::to_string(module->definition)+", requested "+std::to_string(r.update.keychain)+". Use Refresh charm to retry the upstream one-tick replay.";
        if(!note.empty())break;
    }
    if(note!=port::Skins::keychainNote) {
        port::Skins::keychainNote=note;
        if(!note.empty())Note("live keychain presentation: "+note);
    }
}
void Stop() noexcept {observing.store(false);active.store(false);}
#ifdef NSKINZCS2_TESTS
namespace {
struct Fixture {
    port::Skins::WeaponPresentation update;
    std::array<std::uint8_t,0x60> view{};
    std::array<std::uint8_t,0x48> row{};
    std::array<std::uintptr_t,2> panel{};
    int pawn=0,invalidates=0,names=0,sets=0,originals=0,removes=0,cachedDefinition=0;
    bool full=true,order=true,serialCorrect=true,passed=false;
    std::string label;
}* fixture=nullptr;
void* TestResolve(std::uint32_t h) {
    if(h==fixture->update.handle)return fixture->update.weapon;
    if(h==0x2e321)return &fixture->pawn;return nullptr;
}
bool TestDemo(){return true;}
void TestInvalidate(void* item){++fixture->invalidates;port::At<void*>(item,runtime::kKnifeItemNameCacheOffset)=nullptr;}
const char* TestName(void* item,bool full) {
    ++fixture->names;fixture->full&=full && fixture->invalidates==fixture->names;
    const auto def=port::Get<uint16_t>(item,port::O.item_DefIndex);
    if(def==508)return "M9 Bayonet | Gamma Doppler";
    if(def==515)return "Butterfly | Doppler";
    if(def==507)return "Karambit | Original";
    return port::Get<int>(fixture->update.weapon,port::O.econ_FallbackPaintKit)==548 ? "M4A1-S | source finish":"M4A1-S | changed finish";
}
void TestSet(void* panel,const void* token,const char* label) {
    fixture->order&=fixture->originals>0 && panel==fixture->panel.data() &&
        port::Get<std::uint32_t>(token,0)==0x01614551 && std::strcmp(port::Get<const char*>(token,8),"WeaponIcon--name")==0;
    ++fixture->sets;fixture->label=label;
}
void TestOriginal(void* view,void*,bool) {
    ++fixture->originals;
    port::At<int>(view,0x50)=1;port::At<void*>(view,0x58)=fixture->row.data();
    port::At<void*>(fixture->row.data(),8)=fixture->panel.data();
    port::At<std::uint32_t>(fixture->row.data(),0x38)=fixture->update.handle+(fixture->serialCorrect ? 0:0x8000);
    port::At<int>(fixture->row.data(),0x3c)=fixture->cachedDefinition ? fixture->cachedDefinition:fixture->update.definition;
}
int TestRemove(void* view,int index) {
    fixture->order&=index==0 && fixture->originals>0;
    ++fixture->removes;port::At<int>(view,0x50)=0;fixture->cachedDefinition=fixture->update.definition;
    return -1;
}
void TestPresentation(const port::Skins::WeaponPresentation& update) {
    fixture->update=update;fixture->panel={client+entry.panelTable,reinterpret_cast<std::uintptr_t>(&fixture->pawn)};
    // Runs the actual queue, cache invalidation/full-name generation, native
    // callback ordering and strict row consumer against a fake engine boundary.
    port::At<void*>(update.item,runtime::kKnifeItemNameCacheOffset)=reinterpret_cast<void*>(1);
    OnPresentation(update);OnPresentation(update);
    Submit(fixture->view.data(),&fixture->pawn);
    bool ok=fixture->sets==0 && fixture->names==1;
    Hook(fixture->view.data(),&fixture->pawn,false);
    ok&=fixture->sets==1 && fixture->label=="M4A1-S | source finish";
    Hook(fixture->view.data(),&fixture->pawn,false);ok&=fixture->sets==1;
    Hook(fixture->view.data(),&fixture->pawn,true);
    ok&=fixture->sets==2 && fixture->names==1; // Same panel, explicit native refresh, no needless name regeneration.
    port::At<int>(update.weapon,port::O.econ_FallbackPaintKit)=568;
    auto changed=update;changed.paint=568;changed.signature^=0x20;OnPresentation(changed);
    fixture->serialCorrect=false;Hook(fixture->view.data(),&fixture->pawn,false);ok&=fixture->sets==2;
    fixture->serialCorrect=true;Hook(fixture->view.data(),&fixture->pawn,false);
    ok&=fixture->sets==3 && fixture->label=="M4A1-S | changed finish" && fixture->names==2;
    port::At<std::uint32_t>(update.item,port::O.item_ItemIDHigh)^=0x20;
    Hook(fixture->view.data(),&fixture->pawn,false);ok&=fixture->sets==3;
    // Captured failure: item/native full name is already 508, while the retained
    // right-hand HUD row still caches the original Karambit definition 507.
    auto knife=update;knife.definition=508;knife.sourceDefinition=507;knife.paint=568;knife.signature^=0x4400;
    port::At<uint16_t>(update.item,port::O.item_DefIndex)=508;
    port::At<int>(update.weapon,port::O.econ_FallbackPaintKit)=568;
    fixture->update=knife;fixture->cachedDefinition=500;OnPresentation(knife);
    Hook(fixture->view.data(),&fixture->pawn,false);ok&=fixture->removes==0; // unrelated cached definition is never removed
    fixture->cachedDefinition=507;
    Hook(fixture->view.data(),&fixture->pawn,false);ok&=fixture->removes==1 && fixture->sets==3;
    Hook(fixture->view.data(),&fixture->pawn,false);
    ok&=fixture->sets==4 && fixture->label=="M9 Bayonet | Gamma Doppler" && fixture->removes==1;
    auto second=knife;second.definition=515;second.signature^=0x8800;fixture->update=second;
    port::At<uint16_t>(update.item,port::O.item_DefIndex)=515;OnPresentation(second);
    Hook(fixture->view.data(),&fixture->pawn,false);Hook(fixture->view.data(),&fixture->pawn,false);
    ok&=fixture->sets==5 && fixture->label=="Butterfly | Doppler" && fixture->removes==2;
    auto restored=second;restored.definition=507;restored.signature^=0x880000;fixture->update=restored;
    port::At<uint16_t>(update.item,port::O.item_DefIndex)=507;OnPresentation(restored);
    Hook(fixture->view.data(),&fixture->pawn,false);Hook(fixture->view.data(),&fixture->pawn,false);
    ok&=fixture->sets==6 && fixture->label=="Karambit | Original" && fixture->removes==3;
    port::At<uint16_t>(update.item,port::O.item_DefIndex)=60;
    port::At<int>(update.weapon,port::O.econ_FallbackPaintKit)=548;
    fixture->passed=ok && fixture->full && fixture->order && !broken.load();
}
}
bool ProbeHudPipelineForTests() {
    const auto savedCalls=calls;const auto savedEntry=entry;const auto savedClient=client;const auto savedOriginal=original;
    const auto savedActive=active.load(),savedObserving=observing.load(),savedBroken=broken.load();auto savedRequests=std::move(requests);
    struct Restore {
        NativeCalls calls;HudEntrypoints entry;std::uintptr_t client;UpdateFn original;bool active,observing,broken;std::vector<Request> requests;
        ~Restore(){presentation::calls=calls;presentation::entry=entry;presentation::client=client;presentation::original=original;
            presentation::active.store(active);presentation::observing.store(observing);presentation::broken.store(broken);presentation::requests=std::move(requests);fixture=nullptr;}
    } restore{savedCalls,savedEntry,savedClient,savedOriginal,savedActive,savedObserving,savedBroken,std::move(savedRequests)};
    Fixture f;fixture=&f;calls={&TestResolve,&TestDemo,&TestInvalidate,&TestName,&TestSet,&TestRemove};
    entry.panelTable=0x20;client=0x100000;original=&TestOriginal;active.store(true);observing.store(true);broken.store(false);requests.clear();
    return port::Skins::ProbeWeaponPresentationForTests(&TestPresentation) && f.passed;
}
#endif
}
