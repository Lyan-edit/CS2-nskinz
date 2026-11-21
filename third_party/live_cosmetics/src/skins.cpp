// Copyright (c) 2026 janglijoshi. All rights reserved.
// Third-party component. Original copyright and terms: ../LICENSE and ../NOTICE.md.
#include "skins.h"
#include "extras.gen.h"
#include "catalog.gen.h"
#include "game.h"
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

// Local authorized integration: isolate upstream symbols from nSkinz.
namespace nskinzcs2::live_port {

namespace Skins {
static PresentationListener presentationListener=nullptr;
std::vector<KeychainModuleInfo> liveKeychainModules;
std::string keychainNote;
void SetPresentationListener(PresentationListener listener) {presentationListener=listener;}
std::mutex lock;
std::map<uint64_t, PlayerRules> rules;
std::vector<PlayerInfo> players;
std::map<std::pair<uint64_t,uint16_t>,bool> liveWeaponModels;
Settings settings;
bool demoPlaying = false;
std::string lastStatus = "Waiting for a demo";
std::string knifeNote;
std::string gloveNote;
std::vector<uint16_t> knivesInDemo;
static std::unordered_set<uint16_t> g_knivesSeen;
static std::unordered_set<uint16_t> g_glovesSeen;
std::vector<uint16_t> glovesInDemo;
std::vector<std::string> agentsInDemo;
static std::set<std::string> g_agentsSeen;
static std::set<std::string> g_agentsSetByUs;   // models nSkinzLive put on someone in this demo

static bool g_seekWanted = false;
static int g_lastTick = -1;
static ULONGLONG g_seekAt = 0;
static ULONGLONG g_nextSnapshot = 0;

// Item attribute definition indexes (items_game.txt).
enum : uint16_t { ATTR_PAINT = 6, ATTR_SEED = 7, ATTR_WEAR = 8, ATTR_KILLEATER = 80, ATTR_KILLEATER_TYPE = 81,
                  ATTR_STICKER0_ID = 113,   // slot N: id 113+4N, wear 114+4N, scale 115+4N, rotation 116+4N
                  ATTR_KEYCHAIN0_ID = 299, ATTR_KEYCHAIN0_OFFX = 300, ATTR_KEYCHAIN0_OFFY = 301, ATTR_KEYCHAIN0_OFFZ = 302,
                  ATTR_KEYCHAIN0_SEED = 306 };

bool IsKnife(uint16_t def) {
    if (def == 42 || def == 59) return true;
    for (int i = 0; i < kNumWeapons; ++i) if (kWeapons[i].def == def) return kWeapons[i].cat == 5;
    return false;
}
bool IsGloves(uint16_t def) {
    for (int i = 0; i < kNumWeapons; ++i) if (kWeapons[i].def == def) return kWeapons[i].cat == 6;
    return false;
}

// ------------------------------------------------------------------ config file
static int AgentTeam(const std::string& path);
static std::string ConfigPath() { return DllFolder() + "nSkinzLive.cfg"; }

static GunSkin ParseSkin(std::istringstream& in) {
    GunSkin g;
    std::string tok;
    while (in >> tok) {
        auto eq = tok.find('=');
        if (eq == std::string::npos) continue;
        std::string k = tok.substr(0, eq), v = tok.substr(eq + 1);
        if (k == "paint") g.paint = std::atoi(v.c_str());
        else if (k == "wear") g.wear = (float)std::atof(v.c_str());
        else if (k == "seed") g.seed = std::atoi(v.c_str());
        else if (k == "stattrak") g.stattrak = std::atoi(v.c_str());
        else if (k == "stickers") {   // id,id,id,id,id
            std::stringstream vs(v);
            std::string x;
            for (int i = 0; i < 5 && std::getline(vs, x, ','); ++i) g.stickers[i] = std::max(0, std::atoi(x.c_str()));
        }
        else if (k == "charm") g.charm = std::max(0, std::atoi(v.c_str()));
        else if (k == "charmseed") g.charmSeed = std::max(0, std::atoi(v.c_str()));
        else if (k == "name") {   // the rest of the line
            std::string rest;
            std::getline(in, rest);
            g.nametag = v + rest;
            break;
        }
    }
    return g;
}

static void WriteSkin(std::ofstream& f, const char* kind, uint16_t def, const GunSkin& g) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s %u paint=%d wear=%.6f seed=%d stattrak=%d", kind, def, g.paint, g.wear, g.seed, g.stattrak);
    f << buf;
    bool anySticker = false;
    for (int s : g.stickers) anySticker |= s != 0;
    if (anySticker) f << " stickers=" << g.stickers[0] << "," << g.stickers[1] << "," << g.stickers[2] << "," << g.stickers[3] << "," << g.stickers[4];
    if (g.charm) f << " charm=" << g.charm << " charmseed=" << g.charmSeed;
    if (!g.nametag.empty()) f << " name=" << g.nametag;   // last: it takes the rest of the line
    f << "\n";
}

// Demo versions where CS2's own animation data worked / crashed (learned, saved in nSkinzLive.cfg).
static std::set<int> g_animOk, g_animBad;
static std::string AnimTestPath() { return DllFolder() + "nSkinzLive-animtest.txt"; }

// Local integration: compatibility memory must load even when no standalone
// config exists. Do not import standalone loadouts or vtable overrides.
static void SaveAnimationHistory(const std::string& folder) {
    std::ofstream f(folder+"nSkinzLive-live-animation-history.cfg",std::ios::trunc);
    auto line=[&](const char* key,const std::set<int>& values) {
        f<<key<<"=";bool comma=false;
        for(int v:values) { if(comma) f<<",";f<<v;comma=true; }f<<"\n";
    };
    line("anim_ok_versions",g_animOk);line("anim_bad_versions",g_animBad);
}
static void ReadAnimationHistory(const std::string& folder) {
    // Preserve safety decisions written by earlier file-name conventions.
    // Only compatibility keys are read; never import loadouts or function indexes.
    std::vector<std::string> histories,pending_tests;
    WIN32_FIND_DATAA file{};
    const HANDLE search=FindFirstFileA((folder+"*").c_str(),&file);
    if(search!=INVALID_HANDLE_VALUE) {
        do {
            const std::string name=file.cFileName;
            if((file.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || file.nFileSizeHigh || file.nFileSizeLow>1024*1024)continue;
            if(name.ends_with(".cfg") && histories.size()<64)histories.push_back(name);
            if(name.ends_with("-animtest.txt") && pending_tests.size()<64)pending_tests.push_back(name);
        }while(FindNextFileA(search,&file));
        FindClose(search);
    }
    for(const auto& name:histories) {
        std::ifstream f(folder+name);std::string line;
        while(std::getline(f,line)) {
            const auto eq=line.find('=');if(eq==std::string::npos) continue;
            const auto key=line.substr(0,eq);
            if(key!="anim_ok_versions" && key!="anim_bad_versions") continue;
            std::istringstream values(line.substr(eq+1));std::string token;
            while(std::getline(values,token,',')) {
                const int v=std::atoi(token.c_str());
                if(v>0) (key=="anim_ok_versions" ? g_animOk : g_animBad).insert(v);
            }
        }
    }
    for(const auto& name:pending_tests) {
    std::ifstream pending(folder+name);int interrupted=0;
    if(pending>>interrupted && interrupted>0) {
        pending.close();g_animBad.insert(interrupted);
        SaveAnimationHistory(folder);DeleteFileA((folder+name).c_str());
        Log("Older-Demo animation test for version %d did not complete last session; model opt-in remains blocked for it",interrupted);
    }
    }
    Log("Loaded animation compatibility memory: %u completed, %u failed/interrupted version(s)",
        (unsigned)g_animOk.size(),(unsigned)g_animBad.size());
}
void LoadAnimationHistory() { ReadAnimationHistory(DllFolder()); }

void Load() {
    std::ifstream f(ConfigPath());
    if (!f) {
        // Coming from LegacyLive: take over its settings once (nSkinzLive.cfg is written on the next change).
        f.open(DllFolder() + "LegacyLive.cfg");
        if (f) Log("no nSkinzLive.cfg yet - using LegacyLive.cfg from the same folder");
    }
    if (!f) { Log("no nSkinzLive.cfg yet, starting empty"); return; }
    std::string line;
    PlayerRules* cur = nullptr;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string word;
        in >> word;
        if (word == "player") {
            uint64_t xuid = 0;
            in >> xuid;
            std::string name;
            std::getline(in, name);
            if (!name.empty() && name[0] == ' ') name.erase(0, 1);
            cur = xuid ? &rules[xuid] : nullptr;
            if (cur) cur->name = name;
        } else if (cur && (word.rfind("t.", 0) == 0 || word.rfind("ct.", 0) == 0 || word == "gun" || word == "knife" || word == "gloves" ||
                           word == "agent")) {
            // "t.gun ...", "ct.knife ...": that side only; no prefix: both sides
            Loadout* L = cur;
            if (word.rfind("t.", 0) == 0) { L = &cur->t; word = word.substr(2); }
            else if (word.rfind("ct.", 0) == 0) { L = &cur->ct; word = word.substr(3); }
            if (word == "agent") {
                std::string path;
                in >> path;
                if (path.rfind("agents/models/", 0) != 0) continue;
                // Agents are per side. An agent saved for both sides (1.7 - 1.8.3) moves to the side it belongs to.
                if (L == cur) {
                    const int team = AgentTeam(path);
                    L = team == 2 ? &cur->t : team == 3 ? &cur->ct : nullptr;
                    if (!L || !L->agent.empty()) continue;
                }
                L->agent = path;
                continue;
            }
            if (word != "gun" && word != "knife" && word != "gloves") continue;
            int def = 0;
            in >> def;
            GunSkin g = ParseSkin(in);
            if (def <= 0) continue;
            if (word == "gloves") L->gloves = {(uint16_t)def, g};
            else if (word == "knife" || IsKnife((uint16_t)def)) L->knife = {(uint16_t)def, g};   // 1.0 stored knives as guns
            else if (g.paint > 0 || g.charm || g.stickers[0] || g.stickers[1] || g.stickers[2] || g.stickers[3] || g.stickers[4])
                L->guns[(uint16_t)def] = g;
        } else {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq);
            int n = std::atoi(line.c_str() + eq + 1);
            if (k == "menu_key") settings.menuKey = n;
            else if (k == "refresh") settings.refresh = n;
            else if (k == "knife_models") settings.knifeModels = n != 0;
            else if (k == "gloves") settings.gloves = n != 0;
            else if (k == "agents") settings.agents = n != 0;
            else if (k == "any_model") settings.anyModel = n != 0;
            else if (k == "any_model_old_demos") settings.anyModelOldDemos = n != 0;
            else if (k == "any_model_versions_back") settings.anyModelVersionsBack = n;
            else if (k == "anim_ok_versions" || k == "anim_bad_versions") {
                std::stringstream vs(line.substr(eq + 1));
                std::string v;
                while (std::getline(vs, v, ',')) if (int x = std::atoi(v.c_str())) (k == "anim_ok_versions" ? g_animOk : g_animBad).insert(x);
            }
            else if (k == "index.client_frame_stage_notify") Game::I.clientFrameStageNotify = n;
            else if (k == "index.engine_is_playing_demo") Game::I.engineIsPlayingDemo = n;
            else if (k == "index.engine_execute_client_cmd") Game::I.engineExecuteClientCmd = n;
            else if (k == "index.engine_get_demo_file") Game::I.engineGetDemoFile = n;
            else if (k == "index.demo_get_tick") Game::I.demoGetDemoTick = n;
            else if (k == "index.entity_system_offset") Game::I.entitySystemOffset = n;
        }
    }
    Log("loaded %u player(s) from nSkinzLive.cfg", (unsigned)rules.size());

    // A test of CS2's animation data in an older demo was running when the game last closed: it crashed (or was
    // closed within 30 s). Don't do it again for that demo version.
    {
        std::ifstream t(AnimTestPath());
        int v = 0;
        if (t && (t >> v) && v > 0) {
            g_animBad.insert(v);
            g_animOk.erase(v);
            Log("last time CS2 closed or crashed within 30 s of using its own animation data in a demo of version %d: "
                "knife/glove models outside the demo stay off for that version (anim_bad_versions in nSkinzLive.cfg)", v);
            t.close();
            DeleteFileA(AnimTestPath().c_str());
            Save();
        }
    }
}

void Save() {
    {   // what the window set, so the log shows it next to what the demo has
        std::string all;
        for (auto& kv : rules) {
            char b[96];
            std::snprintf(b, sizeof(b), " %llu:", (unsigned long long)kv.first);
            all += b;
            for (auto& g : kv.second.guns) { std::snprintf(b, sizeof(b), " gun %u=%d", g.first, g.second.paint); all += b; }
            if (kv.second.knife.def) { std::snprintf(b, sizeof(b), " knife %u=%d", kv.second.knife.def, kv.second.knife.skin.paint); all += b; }
            if (kv.second.gloves.def) { std::snprintf(b, sizeof(b), " gloves %u=%d", kv.second.gloves.def, kv.second.gloves.skin.paint); all += b; }
            for (int side = Side_T; side <= Side_CT; ++side) {
                const Loadout& L = kv.second.For(side);
                if (L.Empty()) continue;
                all += side == Side_T ? " T:" : " CT:";
                for (auto& g : L.guns) { std::snprintf(b, sizeof(b), " gun %u=%d", g.first, g.second.paint); all += b; }
                if (L.knife.def) { std::snprintf(b, sizeof(b), " knife %u=%d", L.knife.def, L.knife.skin.paint); all += b; }
                if (L.gloves.def) { std::snprintf(b, sizeof(b), " gloves %u=%d", L.gloves.def, L.gloves.skin.paint); all += b; }
                if (!L.agent.empty()) all += " agent";
            }
            all += ";";
        }
        static int logged = 0;
        if (logged++ < 40) Log("skins set:%s", all.empty() ? " (none)" : all.c_str());
    }
    std::ofstream f(ConfigPath(), std::ios::trunc);
    if (!f) { Log("ERROR: cannot write %s", ConfigPath().c_str()); return; }
    f << "# nSkinzLive settings. The menu writes this file; you can also edit it while the game is closed.\n";
    f << "menu_key=" << settings.menuKey << "      # virtual-key code, 45 = Insert\n";
    f << "knife_models=" << (settings.knifeModels ? 1 : 0) << "  # 0 = knife finishes only, never change the model\n";
    f << "gloves=" << (settings.gloves ? 1 : 0) << "        # 0 = leave everyone's gloves alone\n";
    f << "agents=" << (settings.agents ? 1 : 0) << "        # 0 = leave everyone's player model alone\n";
    f << "any_model=" << (settings.anyModel ? 1 : 0) << "     # 1 = allow knives/gloves nobody in the demo has (see README)\n";
    f << "any_model_old_demos=" << (settings.anyModelOldDemos ? 1 : 0) << "     # 1 = ...also in demos from ANY older CS2 version (usually crashes CS2)\n";
    f << "any_model_versions_back=" << settings.anyModelVersionsBack << "  # ...in demos up to this many CS2 versions old (tried once per version)\n";
    auto list = [](const std::set<int>& v) { std::string o; for (int x : v) o += (o.empty() ? "" : ",") + std::to_string(x); return o; };
    f << "anim_ok_versions=" << list(g_animOk) << "   # demo versions where that worked (learned)\n";
    f << "anim_bad_versions=" << list(g_animBad) << "   # demo versions where CS2 crashed or closed right after it (learned)\n";
    f << "refresh=" << settings.refresh << "       # 1 = refresh in place, 2 = jump the demo back one tick, 3 = both\n\n";
    f << "# Advanced: function indexes inside CS2. Only change these if a CS2 update breaks nSkinzLive and you know the new values.\n";
    f << "index.client_frame_stage_notify=" << Game::I.clientFrameStageNotify << "\n";
    f << "index.engine_is_playing_demo=" << Game::I.engineIsPlayingDemo << "\n";
    f << "index.engine_execute_client_cmd=" << Game::I.engineExecuteClientCmd << "\n";
    f << "index.engine_get_demo_file=" << Game::I.engineGetDemoFile << "\n";
    f << "index.demo_get_tick=" << Game::I.demoGetDemoTick << "\n";
    f << "index.entity_system_offset=" << Game::I.entitySystemOffset << "\n\n";
    f << "# player <xuid> <name>, then: gun|knife|gloves <definition index> paint= wear= seed= stattrak= [stickers=a,b,c,d,e]\n"
         "#   [charm=id charmseed=n] name=   |   agent agents/models/....vmdl\n"
         "#   prefix t. or ct. (t.gun, ct.knife, t.agent ...) for one side only; no prefix = both sides\n";
    for (auto& [xuid, pr] : rules) {
        if (pr.Empty()) continue;
        f << "player " << xuid << " " << pr.name << "\n";
        for (int side = Side_Both; side <= Side_CT; ++side) {
            const Loadout& L = pr.For(side);
            const std::string pre = side == Side_T ? "t." : side == Side_CT ? "ct." : "";
            for (auto& [def, g] : L.guns) WriteSkin(f, (pre + "gun").c_str(), def, g);
            if (L.knife.def) WriteSkin(f, (pre + "knife").c_str(), L.knife.def, L.knife.skin);
            if (L.gloves.def) WriteSkin(f, (pre + "gloves").c_str(), L.gloves.def, L.gloves.skin);
            if (!L.agent.empty() && side != Side_Both) f << pre << "agent " << L.agent << "\n";
        }
        f << "\n";
    }
}

void RequestSeek() {
    g_seekWanted = true;
    g_seekAt = GetTickCount64() + 400;   // wait until the slider stops moving
}

// ------------------------------------------------------------------ item attributes
// CAttributeList::m_Attributes is a CUtlVector: +0 count, +8 data, +0x10 capacity, +0x14 grow size
// (bit 31 = memory not owned by the list).
struct AttrVec { int* count; uint8_t** data; int* capacity; int* grow; };

static bool OpenList(uint8_t* item, ptrdiff_t listOff, AttrVec& v) {
    uint8_t* vec = item + listOff + O.attrlist_Attributes;
    v = {(int*)vec, (uint8_t**)(vec + 8), (int*)(vec + 0x10), (int*)(vec + 0x14)};
    int n = *v.count;
    if (n < 0 || n > 256 || *v.capacity < n || *v.capacity > 4096) return false;
    if (n == 0) return true;
    uint8_t* d = *v.data;
    if ((uintptr_t)d & 7) return false;
    // Checking memory is a system call; remember blocks already checked (lists are re-read every frame).
    static uint8_t* lastOk[64] = {};
    static int lastN[64] = {};
    const unsigned slot = (unsigned)(((uintptr_t)d >> 4) & 63);
    if (!(lastOk[slot] == d && lastN[slot] >= n)) {
        if (!Readable(d, (size_t)n * O.attrSize)) return false;
        lastOk[slot] = d;
        lastN[slot] = n;
    }
    // Every entry must look like a real attribute, or this isn't the layout we expect: leave it alone.
    for (int i = 0; i < n; ++i) {
        uint8_t* a = d + i * O.attrSize;
        uint16_t def = Get<uint16_t>(a, O.attr_DefIndex);
        if (def == 0 || def >= 10000) return false;
        if (Game::attributeVtable && Get<void*>(a, 0) != Game::attributeVtable) return false;
    }
    return true;
}

struct ListUndo {
    int count = -1;
    std::map<uint16_t, float> values;
    bool cleared = false;              // the item's own attributes were taken out (stickers, keychain, ...)
    std::vector<uint8_t> bytes;        // ...and these are they, to put back
    std::set<uint16_t> added; // Integration: remove only nodes we appended, not new source attributes.
};

// The attributes nSkinzLive sets for this rule: the finish, StatTrak, and the sticker slots / keychain the rule fills.
// Everything else on the item (the real gun's stickers and charm) is removed.
static bool IsOurAttr(uint16_t def, const GunSkin* g) {
    if (def == ATTR_PAINT || def == ATTR_SEED || def == ATTR_WEAR || def == ATTR_KILLEATER || def == ATTR_KILLEATER_TYPE) return true;
    if (!g) return false;
    if (def >= ATTR_STICKER0_ID && def < ATTR_STICKER0_ID + 4 * 5) return g->stickers[(def - ATTR_STICKER0_ID) / 4] != 0;
    if (g->charm && (def == ATTR_KEYCHAIN0_ID || def == ATTR_KEYCHAIN0_OFFX || def == ATTR_KEYCHAIN0_OFFY ||
                     def == ATTR_KEYCHAIN0_OFFZ || def == ATTR_KEYCHAIN0_SEED)) return true;
    return false;
}

// A gun given a new skin is a new item: the stickers, keychain and other extras of the player's real gun must not
// stay on it (as WeaponPaints does: it removes all attributes before setting its own). Everything that isn't ours
// is taken out of both lists; the originals are kept and put back when the skin is removed. Runs every pass, because
// the demo can send the original list again.
static bool ClearOthers(uint8_t* item, int li, ListUndo& undo, const GunSkin* g) {
    const ptrdiff_t listOff = li == 0 ? O.item_AttributeList : O.item_NetworkedDynamicAttributes;
    AttrVec v;
    if (!OpenList(item, listOff, v)) return false;
    if (!undo.cleared) {
        if (undo.count < 0) undo.count = *v.count;
        if (*v.count) undo.bytes.assign(*v.data, *v.data + (size_t)*v.count * O.attrSize);
        undo.cleared = true;
    }
    int n = *v.count, keep = 0;
    bool removed = false;
    if (li == 1) {
        // The networked list keeps its length (the network code owns it); its extras are set to 0 instead:
        // sticker / keychain id 0 = empty slot.
        for (int i = 0; i < n; ++i) {
            uint8_t* a = *v.data + i * O.attrSize;
            if (IsOurAttr(Get<uint16_t>(a, O.attr_DefIndex), g)) continue;
            if (Get<uint32_t>(a, O.attr_Value) != 0 || Get<uint32_t>(a, O.attr_InitialValue) != 0) {
                At<uint32_t>(a, O.attr_Value) = 0;
                At<uint32_t>(a, O.attr_InitialValue) = 0;
                removed = true;
            }
        }
        return removed;
    }
    for (int i = 0; i < n; ++i) {
        uint8_t* a = *v.data + i * O.attrSize;
        if (IsOurAttr(Get<uint16_t>(a, O.attr_DefIndex), g)) {
            if (keep != i) std::memmove(*v.data + keep * O.attrSize, a, O.attrSize);
            ++keep;
        } else {
            removed = true;
        }
    }
    if (keep != n) *v.count = keep;
    return removed;
}

// Sets an attribute in one list, adding it if missing. Returns false if the list can't be changed safely.
static bool SetAttr(uint8_t* item, int li, uint16_t def, float value, ListUndo& undo) {
    const ptrdiff_t listOff = li == 0 ? O.item_AttributeList : O.item_NetworkedDynamicAttributes;
    AttrVec v;
    if (!OpenList(item, listOff, v)) return false;
    if (undo.count < 0) undo.count = *v.count;
    for (int i = 0; i < *v.count; ++i) {
        uint8_t* a = *v.data + i * O.attrSize;
        if (Get<uint16_t>(a, O.attr_DefIndex) != def) continue;
        if(!undo.added.count(def) && !undo.values.count(def)) undo.values.emplace(def, Get<float>(a, O.attr_Value));
        SetIfDiff<float>(a, O.attr_Value, value);
        SetIfDiff<float>(a, O.attr_InitialValue, value);
        return true;
    }
    if (!Game::attributeVtable) return false;
    // Only ever add to m_AttributeList: it is the list the game reads first (C_EconItemView::FindAttribute), and the
    // networked list is also touched by the game's network and worker code, where a hand-made entry is riskier.
    if (li != 0) return false;

    // Add one. If the list is full, move it into memory nSkinzLive owns and mark that memory "external" (bit 31 of the
    // grow size): the game then never frees or resizes it. Handing the game memory from a different allocator than the
    // one it frees with can corrupt its heap and crash CS2 much later, so we never do that.
    int n = *v.count;
    if (n >= *v.capacity) {
        int cap = n + 8;
        static HANDLE heap = HeapCreate(0, 0, 0);   // nSkinzLive' own heap; blocks in it are never freed
        uint8_t* fresh = heap ? (uint8_t*)HeapAlloc(heap, HEAP_ZERO_MEMORY, (size_t)cap * O.attrSize) : nullptr;
        if (!fresh) return false;
        Breadcrumb("attribute list of item %p moved to nSkinzLive memory (%d -> %d slots) for attribute %u", (void*)item, n, cap, def);
        if (n) std::memcpy(fresh, *v.data, (size_t)n * O.attrSize);   // the old block stays where it is (never freed twice)
        *v.data = fresh;
        *v.capacity = cap;
        *v.grow = (*v.grow & 0x3fffffff) | (int)0x80000000;
    }
    uint8_t* a = *v.data + n * O.attrSize;
    std::memset(a, 0, O.attrSize);
    if (n) std::memcpy(a, *v.data, O.attr_DefIndex);   // same vtable and owner link as its neighbours
    else At<void*>(a, 0) = Game::attributeVtable;
    At<uint16_t>(a, O.attr_DefIndex) = def;
    At<float>(a, O.attr_Value) = value;
    At<float>(a, O.attr_InitialValue) = value;
    *v.count = n + 1;
    undo.added.insert(def);
    return true;
}

static void RestoreList(uint8_t* item, int li, const ListUndo& undo) {
    AttrVec v;
    if (undo.count < 0 || !OpenList(item, li == 0 ? O.item_AttributeList : O.item_NetworkedDynamicAttributes, v)) return;
    if (undo.cleared) {   // put the item's own attributes back as they were
        const int n = (int)(undo.bytes.size() / O.attrSize);
        if (n <= *v.capacity && (li == 0 || n == *v.count)) {   // the networked list: only if its length didn't change
            if (n) std::memcpy(*v.data, undo.bytes.data(), undo.bytes.size());
            *v.count = n;
        } else if (li == 1) {
            // it changed: the demo has sent it again since, with the real values
            return;
        } else {
            *v.count = 0;   // the list shrank meanwhile: leave it empty rather than write past it
        }
        return;
    }
    for (int i = 0; i < *v.count; ++i) {
        uint8_t* a = *v.data + i * O.attrSize;
        auto it = undo.values.find(Get<uint16_t>(a, O.attr_DefIndex));
        if (it != undo.values.end()) { At<float>(a, O.attr_Value) = it->second; At<float>(a, O.attr_InitialValue) = it->second; }
    }
    // Networked lists keep the exact source length. On the primary list drop
    // only our appended keys, preserving unrelated attributes sent since then.
    if(li==0 && !undo.added.empty()) {
        int keep=0;
        for(int i=0;i<*v.count;++i) {
            auto* a=*v.data+i*O.attrSize;
            if(undo.added.count(Get<uint16_t>(a,O.attr_DefIndex))) continue;
            if(keep!=i) std::memmove(*v.data+keep*O.attrSize,a,O.attrSize);++keep;
        }
        *v.count=keep;
    }
}

static float IntBits(int n) { float f; uint32_t u = (uint32_t)n; std::memcpy(&f, &u, 4); return f; }

// ------------------------------------------------------------------ what we changed, to undo it
struct Undo {
    bool weapon = false;
    bool wroteFinish=false;
    std::vector<uint16_t> activePatches;
    uint16_t def;
    int paint, seed, stattrak;
    float wear;
    int32_t idHigh, quality;
    uint32_t account;
    char nametag[161];
    ListUndo lists[2];
    uint64_t key = 0;        // which rule is applied
    bool written = false;
    int retries = 0;         // refreshes after the game reset our values
    ULONGLONG lastRefresh = 0;
    bool checked = false;    // gloves: reported what the game did with our re-apply request
    void* ent = nullptr;     // the entity this belongs to (a demo jump can reuse the handle for a new one)
    ULONGLONG checkAt = 0;   // guns: when to report what the game made of our values
    bool gunChecked = true;
    ULONGLONG reapplyAt = 0; // gloves: one more re-apply after their model finished loading
    uint32_t idLow = 0;      // the item's real ID, to put back
    uint64_t itemId = 0;
    uint64_t fakeId = 0;     // our item ID for the current rule (a new one per change, like a freshly dropped item)
    uint64_t idKey = 0;
    uint64_t origMask = 0;   // weapon mesh mask before we touched it
    bool maskSaved = false;
    int bodyWant = -1;       // weapon: "body" group choice we asked for (1 = legacy model), -1 none yet
    ULONGLONG bodyCheckAt = 0;
    ULONGLONG hudRemoveAt = 0; // weapon: first-person model re-made because its model was wrong (rate limit)
    int hudRemoves = 0;
    ULONGLONG bgCheckAt = 0; // gloves: next check of the player's default-glove body group
    bool bgChanged = false;  // gloves: we switched it to "hide default gloves"
    ULONGLONG gloveFixAt = 0;  // gloves: next time a missing glove model may be asked for again
    ULONGLONG matCheckAt = 0;  // weapon: next check that its skin material really exists
    int matRetries = 0, hudMatRemakes = 0;
    uint8_t meshWant = 0;      // weapon: mesh bits we want (1 current model, 2 legacy model), 0 none
    bool secondCheck = false;
};

// Item IDs we hand out. A real-looking, unique ID per change makes CS2 treat the item as a new one and build its skin
// fresh (WeaponPaints does the same on servers). The high bits keep them clear of real Steam item IDs.
static uint64_t NextFakeId() {
    static uint64_t next = 0x00F0000000000000ull | (GetTickCount64() & 0xFFFFFF) << 8;
    return ++next;
}

// An item nSkinzLive has written (our item IDs start with 0x00F0...): its definition is ours, not the demo's.
static bool IsOurItem(uint8_t* item) { return (Get<uint32_t>(item, O.item_ItemIDHigh) & 0xFFF00000u) == 0x00F00000u; }

static bool IsLegacy(uint16_t def, int paint) {
    for (int i = 0; i < kNumSkins; ++i) if (kSkins[i].def == def && kSkins[i].paint == paint) return kSkins[i].legacy != 0;
    return false;
}

static int g_logBudget = 150;   // refresh reasons logged per session (enough to diagnose, never a flood)

// Rebuilding a skin is expensive (CS2 regenerates every weapon's material). Rebuild when the rule changes; if the
// game resets our values afterwards, try again at most 3 times, 3 seconds apart - never in a loop.
static bool ShouldRefresh(Undo& u, uint64_t key, bool overwritten, const char* what, uint8_t* item, bool fresh = false) {
    const ULONGLONG now = GetTickCount64();
    // 2.0-2.1 skipped the rebuild for a weapon created this very frame, expecting CS2 to build it from our values.
    // Weapons bought in a new round then sometimes kept the demo's skin, so every new weapon is rebuilt now.
    (void)fresh;
    if (u.key != key) {
        u.key = key; u.retries = 0; u.lastRefresh = now;
        return true;
    }
    if (now - u.lastRefresh > 20000) u.retries = 0;   // a later demo jump may reset it again: allow fresh retries
    if (overwritten && u.retries < 3 && now - u.lastRefresh > 1500) {
        ++u.retries; u.lastRefresh = now;
        if (g_logBudget-- > 0)
            Log("%s: the game reset it (def %u, item id high %d) - refreshing again (%d/3)", what, Get<uint16_t>(item, O.item_DefIndex),
                Get<int32_t>(item, O.item_ItemIDHigh), u.retries);
        return true;
    }
    return false;
}
static std::unordered_map<uint32_t, Undo> g_weapons;   // by weapon handle
static std::unordered_map<uint32_t, Undo> g_gloves;    // by pawn handle
static void* g_lastEntitySystem = nullptr;

static uint64_t Hash(uint64_t xuid, uint16_t def, const GunSkin& g, bool legacy=true) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void* p, size_t n) { for (size_t i = 0; i < n; ++i) { h ^= ((const uint8_t*)p)[i]; h *= 1099511628211ull; } };
    mix(&xuid, 8); mix(&def, 2); mix(&g.paint, 4); mix(&g.wear, 4); mix(&g.seed, 4); mix(&g.stattrak, 4); mix(g.nametag.data(), g.nametag.size());
    mix(g.stickers, sizeof(g.stickers)); mix(&g.charm, 4); mix(&g.charmSeed, 4);
    if(!g.writeFinish || !g.patches.empty()) {
        mix(&g.writeFinish,sizeof(g.writeFinish));mix(&legacy,sizeof(legacy));
        for(const auto& patch:g.patches) {mix(&patch.definition,2);const auto value=legacy ? patch.legacy : patch.modern;mix(&value,4);}
    }
    return h | 1;
}

static void SaveItem(Undo& u, uint8_t* item) {
    u.def = Get<uint16_t>(item, O.item_DefIndex);
    u.idHigh = Get<int32_t>(item, O.item_ItemIDHigh);
    u.idLow = Get<uint32_t>(item, O.item_ItemIDLow);
    u.itemId = Get<uint64_t>(item, O.item_ItemID);
    u.quality = Get<int32_t>(item, O.item_EntityQuality);
    u.account = Get<uint32_t>(item, O.item_AccountID);
    std::memcpy(u.nametag, item + O.item_CustomName, sizeof(u.nametag));
}

static void RestoreItem(const Undo& u, uint8_t* item) {
    At<uint16_t>(item, O.item_DefIndex) = u.def;
    At<int32_t>(item, O.item_ItemIDHigh) = u.idHigh;
    At<uint32_t>(item, O.item_ItemIDLow) = u.idLow;
    At<uint64_t>(item, O.item_ItemID) = u.itemId;
    At<int32_t>(item, O.item_EntityQuality) = u.quality;
    At<uint32_t>(item, O.item_AccountID) = u.account;
    std::memcpy(item + O.item_CustomName, u.nametag, sizeof(u.nametag));
    RestoreList(item, 0, u.lists[0]);
    RestoreList(item, 1, u.lists[1]);
}

// Writes the finish into an item (weapon or gloves). Returns true if the game had changed our values since last time
// (a network update), which means the item must be refreshed again.
static bool WriteItem(uint8_t* item, Undo& u, uint64_t xuid, uint16_t def, const GunSkin& g, bool stattrakAllowed, bool legacy=true) {
    const uint64_t key = Hash(xuid, def, g, legacy);
    const bool newRule = u.idKey != key;
    if (newRule) { u.fakeId = NextFakeId(); u.idKey = key; }
    bool overwritten = u.written && !newRule &&
                       (Get<uint32_t>(item, O.item_ItemIDHigh) != (uint32_t)(u.fakeId >> 32) || Get<uint16_t>(item, O.item_DefIndex) != def);
    SetIfDiff<uint16_t>(item, O.item_DefIndex, def);
    SetIfDiff<uint64_t>(item, O.item_ItemID, u.fakeId);
    SetIfDiff<uint32_t>(item, O.item_ItemIDHigh, (uint32_t)(u.fakeId >> 32));
    SetIfDiff<uint32_t>(item, O.item_ItemIDLow, (uint32_t)u.fakeId);
    SetIfDiff<uint32_t>(item, O.item_AccountID, (uint32_t)(xuid & 0xffffffffu));
    SetIfDiff<bool>(item, O.item_Initialized, true);
    if (g.writeFinish && stattrakAllowed) {
        // Knives are always "unusual" (quality 3, the star), like WeaponPaints sets them; a player's default knife is 0.
        int32_t q = g.stattrak >= 0 ? 9 : IsKnife(def) ? 3 : (u.quality == 9 ? 4 : u.quality);
        SetIfDiff<int32_t>(item, O.item_EntityQuality, q);
    }
    char tag[161] = {};
    std::strncpy(tag, g.nametag.c_str(), sizeof(tag) - 1);
    if (g.writeFinish && std::memcmp(item + O.item_CustomName, tag, sizeof(tag)) != 0) std::memcpy(item + O.item_CustomName, tag, sizeof(tag));

    if (!g.preserveExtras && !CrashedLastTime("clear-extras")) {
        Step step("removing stickers and keychain");
        FirstRunBegin("clear-extras");
        for (int li = 0; li < 2; ++li) ClearOthers(item, li, u.lists[li], &g);
        FirstRunEnd("clear-extras");
    }
    for (int li = 0; li < 2; ++li) {
        if(g.writeFinish) {
            SetAttr(item, li, ATTR_PAINT, (float)g.paint, u.lists[li]);
            SetAttr(item, li, ATTR_SEED, (float)g.seed, u.lists[li]);
            SetAttr(item, li, ATTR_WEAR, g.wear, u.lists[li]);
        }
        if (g.writeFinish && stattrakAllowed && g.stattrak >= 0) {
            SetAttr(item, li, ATTR_KILLEATER, IntBits(g.stattrak), u.lists[li]);
            SetAttr(item, li, ATTR_KILLEATER_TYPE, 0.f, u.lists[li]);
        }
        // Stickers (slot N: id, wear, scale, rotation) and the charm (keychain slot 0). Ids are stored as integers.
        for (int n = 0; !g.preserveExtras && n < 5; ++n) {
            if (!g.stickers[n]) continue;
            const uint16_t base = (uint16_t)(ATTR_STICKER0_ID + 4 * n);
            SetAttr(item, li, base, IntBits(g.stickers[n]), u.lists[li]);
            SetAttr(item, li, base + 1, 0.f, u.lists[li]);   // wear: new
            SetAttr(item, li, base + 2, 1.f, u.lists[li]);   // scale
            SetAttr(item, li, base + 3, 0.f, u.lists[li]);   // rotation
        }
        if (!g.preserveExtras && g.charm) {
            SetAttr(item, li, ATTR_KEYCHAIN0_ID, IntBits(g.charm), u.lists[li]);
            SetAttr(item, li, ATTR_KEYCHAIN0_OFFX, 0.f, u.lists[li]);
            SetAttr(item, li, ATTR_KEYCHAIN0_OFFY, 0.f, u.lists[li]);
            SetAttr(item, li, ATTR_KEYCHAIN0_OFFZ, 0.f, u.lists[li]);
            SetAttr(item, li, ATTR_KEYCHAIN0_SEED, IntBits(g.charmSeed), u.lists[li]);
        }
    }
    const bool patch_keys_changed=u.activePatches.size()!=g.patches.size() ||
        !std::equal(u.activePatches.begin(),u.activePatches.end(),g.patches.begin(),[](uint16_t old,const AttributePatch& patch){return old==patch.definition;});
    for(int li=0;li<2;++li) {
        // A removed override restores its source value without changing other slots.
        if(patch_keys_changed) for(auto old:u.activePatches) {
            if(std::any_of(g.patches.begin(),g.patches.end(),[&](const auto& patch){return patch.definition==old;})) continue;
            AttrVec v;if(!OpenList(item,li==0 ? O.item_AttributeList : O.item_NetworkedDynamicAttributes,v)) continue;
            for(int i=0;i<*v.count;++i) {
                auto* a=*v.data+i*O.attrSize;if(Get<uint16_t>(a,O.attr_DefIndex)!=old) continue;
                const auto value=u.lists[li].values.find(old);
                if(value!=u.lists[li].values.end()) {At<float>(a,O.attr_Value)=value->second;At<float>(a,O.attr_InitialValue)=value->second;}
                else if(li==0 && u.lists[li].added.count(old)) {
                    std::memmove(a,a+O.attrSize,static_cast<size_t>(*v.count-i-1)*O.attrSize);--*v.count;u.lists[li].added.erase(old);
                }
                break;
            }
        }
        for(const auto& patch:g.patches) {
            const uint32_t value=legacy ? patch.legacy : patch.modern;float encoded;std::memcpy(&encoded,&value,4);
            if(li==0) {
                AttrVec v;bool matches=false;
                if(OpenList(item,O.item_AttributeList,v)) for(int i=0;i<*v.count;++i) {
                    auto* a=*v.data+i*O.attrSize;
                    if(Get<uint16_t>(a,O.attr_DefIndex)==patch.definition) {matches=Get<uint32_t>(a,O.attr_Value)==value && Get<uint32_t>(a,O.attr_InitialValue)==value;break;}
                }
                overwritten |= u.written && !newRule && !matches;
            }
            SetAttr(item,li,patch.definition,encoded,u.lists[li]);
        }
    }
    if(newRule && !g.patches.empty() && g_logBudget-- > 0) {
        AttrVec a,b;const bool aok=OpenList(item,O.item_AttributeList,a),bok=OpenList(item,O.item_NetworkedDynamicAttributes,b);
        Log("live extras: weapon %u, %u selected fields, finish %s, %s mesh, primary/networked attributes %d/%d",def,
            (unsigned)g.patches.size(),g.writeFinish ? "configured" : "preserved",legacy ? "legacy" : "modern",aok ? *a.count : -1,bok ? *b.count : -1);
    }
    if(patch_keys_changed) {
        u.activePatches.clear();u.activePatches.reserve(g.patches.size());
        for(const auto& patch:g.patches) u.activePatches.push_back(patch.definition);
    }
    u.wroteFinish |= g.writeFinish;
    u.written = true;
    return overwritten;
}

// ------------------------------------------------------------------ weapons (guns and knives)
static bool g_regenWanted = false;
// Rebuild one weapon's skin. A weapon whose model just changed (a swapped knife) has no materials for a moment; it
// waits in g_pendingRebuild and is retried each pass, then forced after 1.5 s.
static std::unordered_map<uint32_t, ULONGLONG> g_pendingRebuild;   // weapon handle -> when it was asked

static bool TryRebuild(void* weapon, bool force) {
    Step step("rebuilding one weapon skin");
    FirstRunBegin("regenerate");
    int r = Game::RegenerateWeapon(weapon, force);
    FirstRunEnd("regenerate");
    if (r == 1) Breadcrumb("rebuilt skin of weapon %p%s", weapon, force ? " (forced)" : "");
    return r != 0;   // -1 (not available) also ends the wait: the all-weapons fallback runs instead
}

// Skins are rebuilt with the game's own regenerate_weapon_skins routine. It first clears CS2's cache of built
// skins and then rebuilds every weapon; rebuilding one weapon alone (1.5-1.6.3) reused the cached old skin, which is
// why changes often didn't show. It runs once per batch of changes: 0.4 s after the last change, then once more 1.6 s
// later for weapons whose materials weren't made yet the first time (a knife that just changed model, a new gun).
static ULONGLONG g_regenDue = 0, g_regenSecond = 0, g_lastRegen = 0;

// First-person models take the weapon's skin material when they are made. After a rebuild the weapon has its new
// material but the first-person model still shows the old one (the stock AK in first person, the knife finish that
// "went back to default"), so it is made again once the weapon's new material exists.
// Parts switched off for this session because they were running when the game crashed last time (safe mode).
static bool HudOff() { static int v = -1; if (v < 0) v = (CrashedLastTime("hud-fix") || CrashedLastTime("hud-remake")) ? 1 : 0; return v == 1; }
static bool BodyOff() { static int v = -1; if (v < 0) v = CrashedLastTime("body-group") ? 1 : 0; return v == 1; }
static bool SetBody(void* ent, const char* group, int choice) {
    if (BodyOff()) return false;
    Step step("setting a body group");
    FirstRunBegin("body-group");
    bool ok = Game::SetBodyGroupIndex(ent, group, choice);
    FirstRunEnd("body-group");
    return ok;
}

static int g_meshResets = 0, g_lateResets = 0;   // how often the game undid our mesh choice (logged every 10 s)

static std::unordered_map<uint32_t, ULONGLONG> g_hudDue;   // weapon handle -> when its first-person model is re-made
static int g_hudLog = 12;

static void RunHudRemakes(ULONGLONG now) {
    if (HudOff()) { g_hudDue.clear(); return; }
    for (auto it = g_hudDue.begin(); it != g_hudDue.end();) {
        if (now < it->second) { ++it; continue; }
        void* w = Game::FromHandle(it->first);
        if (!w) { it = g_hudDue.erase(it); continue; }
        int a = 0, b = 0;
        Game::MaterialCounts(w, a, b);
        if (a <= 0 && b <= 0 && now - it->second < 2000) { ++it; continue; }   // its material isn't made yet: wait (2 s at most)
        if (void* hud = Game::HudModel(w)) {
            Breadcrumb("first-person model of weapon %p re-made (new skin)", w);
            Game::RemoveEntity(hud);
            if (g_hudLog-- > 0) Log("first-person model of weapon def %u re-made to pick up its new skin", Get<uint16_t>((uint8_t*)w + O.econ_AttributeManager + O.container_Item, O.item_DefIndex));
        }
        it = g_hudDue.erase(it);
    }
}

static void RefreshWeapon(void*) {
    g_regenDue = GetTickCount64() + 400;
    g_regenSecond = 0;
}

static void RunPendingRebuilds(ULONGLONG now) {
    for (auto it = g_pendingRebuild.begin(); it != g_pendingRebuild.end();) {
        void* w = Game::FromHandle(it->first);
        if (!w || now - it->second > 5000) { it = g_pendingRebuild.erase(it); continue; }
        bool force = now - it->second > 1500;
        if (TryRebuild(w, force)) it = g_pendingRebuild.erase(it); else ++it;
    }
}

static uint64_t ControllerXuid(void* controller) { return controller ? Get<uint64_t>(controller, O.ctrl_steamID) : 0; }

static uint64_t OwnerXuid(void* weapon) {
    void* pawn = Game::FromHandle(Get<uint32_t>(weapon, O.ent_hOwnerEntity));
    if (pawn) {
        void* ctrl = Game::FromHandle(Get<uint32_t>(pawn, O.pawn_hController));
        if (uint64_t x = ControllerXuid(ctrl)) return x;
    }
    return 0; // No current holder: nSkinz must not target the original buyer.
}

// Integration: nSkinz configures the selected player, including picked-up guns.
// Upstream's buyer-based fallback is deliberately not used by nSkinz.
static uint64_t RuleOwnerXuid(uint64_t /*original*/,uint64_t holder) {
    return holder;
}
static uint64_t SkinOwnerXuid(void* weapon) {
    return RuleOwnerXuid(0,OwnerXuid(weapon));
}

// ---- knife model swap
// A weapon's subclass (m_nSubclassID) picks its VData: model, animations, sounds. We change it the way a network update
// would (set the token, run the game's own change handler), then load the model the new VData names.
static const char* WeaponClass(uint16_t def) {
    if (def == 42) return "weapon_knife";
    if (def == 59) return "weapon_knife_t";
    for (int i = 0; i < kNumWeapons; ++i) if (kWeapons[i].def == def) return kWeapons[i].cls;
    return nullptr;
}

static bool LooksLikeModel(const char* p) {
    if (!Readable(p, 8)) return false;
    size_t n = 0;
    while (n < 260 && Readable(p + n, 1) && p[n]) { if ((uint8_t)p[n] < 32 || (uint8_t)p[n] > 126) return false; ++n; }
    return n > 5 && n < 260 && std::strstr(p, ".vmdl") != nullptr;
}

// How to get from a weapon's VData to its model path. The game's own layout of that field isn't documented, so it is
// learned once from the player's real knife: we search the VData (up to three pointers deep) for the path of the
// model that knife is showing right now, and remember the route.
struct Route { int depth = -1; ptrdiff_t off[3] = {}; bool inlineText = false; };
static Route g_route;
static bool g_routeTried = false;

static const char* Follow(uint8_t* vd, const Route& r) {
    uint8_t* p = vd;
    for (int i = 0; i < r.depth; ++i) {
        if (!Readable(p + r.off[i], 8)) return nullptr;
        p = Get<uint8_t*>(p, r.off[i]);
    }
    if (r.inlineText) { const char* t = (const char*)(p + r.off[r.depth]); return LooksLikeModel(t) ? t : nullptr; }
    if (!Readable(p + r.off[r.depth], 8)) return nullptr;
    const char* t = Get<const char*>(p, r.off[r.depth]);
    return LooksLikeModel(t) ? t : nullptr;
}

static bool SameModel(const char* a, const char* b) { return a && b && !_stricmp(a, b); }

static bool FindRoute(uint8_t* vd, const char* want) {
    const ptrdiff_t span[3] = {0x400, 0x140, 0x40};
    std::function<bool(uint8_t*, int, Route&)> walk = [&](uint8_t* base, int depth, Route& r) -> bool {
        if (!Readable(base, span[depth])) return false;
        for (ptrdiff_t o = 0; o + 8 <= span[depth]; o += 8) {
            r.off[depth] = o;
            r.depth = depth;
            r.inlineText = true;
            if (SameModel((const char*)(base + o), want)) return true;
            r.inlineText = false;
            const char* t = Get<const char*>(base, o);
            if (Readable(t, 8) && SameModel(t, want)) return true;
        }
        if (depth == 2) return false;
        for (ptrdiff_t o = 0; o + 8 <= span[depth]; o += 8) {
            uint8_t* next = Get<uint8_t*>(base, o);
            if (!Readable(next, 8)) continue;
            r.off[depth] = o;
            if (walk(next, depth + 1, r)) return true;
        }
        return false;
    };
    Route r;
    if (!walk(vd, 0, r)) return false;
    g_route = r;
    return true;
}

static void Dump(const char* what, uint8_t* p, ptrdiff_t from, ptrdiff_t len) {
    if (!Readable(p + from, len)) { Log("%s: not readable", what); return; }
    for (ptrdiff_t o = from; o < from + len; o += 32) {
        char line[200];
        int n = std::snprintf(line, sizeof(line), "%s +%03x:", what, (unsigned)o);
        for (int k = 0; k < 32; k += 8) n += std::snprintf(line + n, sizeof(line) - n, " %016llx", (unsigned long long)Get<uint64_t>(p, o + k));
        Log("%s", line);
    }
}

struct KnifeSwap {
    bool saved = false, swapped = false;
    uint32_t origToken = 0, newToken = 0;
    std::string origModel;
    uint16_t to = 0;
    ULONGLONG retryAt = 0;   // after a failure, wait before trying again
    void* ent = nullptr;
};
static std::unordered_map<uint32_t, KnifeSwap> g_knives;   // by weapon handle
static int g_knifeScheme = -1;   // 0: subclass named by definition index ("507"), 1: by class ("weapon_knife_karambit")
static int g_knifeLog = 30;

// Is this knife entity really that knife model right now? (its subclass decides the model, not the item number)
static bool KnifeIsModel(void* ent, uint16_t def) {
    char num[8];
    std::snprintf(num, sizeof(num), "%u", def);
    const char* cls = WeaponClass(def);
    uint32_t cur = Get<uint32_t>(ent, O.ent_nSubclassID);
    return cur == Game::StringToken(num) || (cls && cur == Game::StringToken(cls));
}

static bool SwapKnife(void* ent, uint32_t handle, uint16_t from, uint16_t to, std::string& note) {
    KnifeSwap& ks = g_knives[handle];
    const ULONGLONG now = GetTickCount64();
    if (!ks.saved) {
        ks.ent = ent;
        ks.origToken = Get<uint32_t>(ent, O.ent_nSubclassID);
        const char* mn = Game::ModelName(ent);
        ks.origModel = mn ? mn : "";
        if (g_knifeScheme < 0) {
            char num[8];
            std::snprintf(num, sizeof(num), "%u", from);
            const char* cls = WeaponClass(from);
            if (ks.origToken == Game::StringToken(num)) g_knifeScheme = 0;
            else if (cls && ks.origToken == Game::StringToken(cls)) g_knifeScheme = 1;
            Log("knife %u: subclass token %08x, naming %s, model '%s'", from, ks.origToken,
                g_knifeScheme == 0 ? "by definition index" : g_knifeScheme == 1 ? "by class name" : "UNKNOWN", ks.origModel.c_str());
        }
        uint8_t* vdNow = Get<uint8_t*>(ent, O.ent_VData);
        if (!g_routeTried && vdNow && !ks.origModel.empty()) {
            g_routeTried = true;
            uint8_t* vd = vdNow;
            if (FindRoute(vd, ks.origModel.c_str()))
                Log("knife models: path found in VData at depth %d, offsets %x/%x/%x%s", g_route.depth, (unsigned)g_route.off[0],
                    (unsigned)g_route.off[1], (unsigned)g_route.off[2], g_route.inlineText ? " (inline)" : "");
            else {
                Log("knife models: could not find '%s' in the knife's VData %p - dump follows", ks.origModel.c_str(), (void*)vd);
                if (vd) { Dump("vdata", vd, 0, 0x60); Dump("vdata", vd, 0x100, 0x40); }
            }
        }
        ks.saved = true;
    }
    if (ks.swapped && ks.to == to && Get<uint32_t>(ent, O.ent_nSubclassID) == ks.newToken) return true;
    {
        // Already that knife (e.g. it survived a demo jump with our change still on it): nothing to swap.
        char num0[8];
        std::snprintf(num0, sizeof(num0), "%u", to);
        const char* cls0 = WeaponClass(to);
        uint32_t cur = Get<uint32_t>(ent, O.ent_nSubclassID);
        uint32_t tokNum = Game::StringToken(num0), tokCls = cls0 ? Game::StringToken(cls0) : 0;
        if (cur == tokNum || (tokCls && cur == tokCls)) {
            uint8_t* vd = Get<uint8_t*>(ent, O.ent_VData);
            const char* path = (vd && g_route.depth >= 0) ? Follow(vd, g_route) : nullptr;
            const char* nowModel = Game::ModelName(ent);
            if (path && !SameModel(path, nowModel)) { Game::PrecacheModel(path); Game::SetModel(ent, path); }
            if (ks.origToken == cur) {   // we never saw its real model: assume the player's default knife class
                const char* fromCls = WeaponClass(from);
                char numF[8];
                std::snprintf(numF, sizeof(numF), "%u", from);
                ks.origToken = Game::StringToken(g_knifeScheme == 0 ? numF : (fromCls ? fromCls : numF));
                ks.origModel.clear();
            }
            ks.swapped = true; ks.newToken = cur; ks.to = to;
            return true;
        }
    }
    if (g_route.depth < 0) {
        // Not learned (yet): the layout found on CS2 build 14197 - the model path is stored as text at VData + 0x30.
        Route known; known.depth = 0; known.off[0] = 0x30; known.inlineText = true;
        uint8_t* vd = Get<uint8_t*>(ent, O.ent_VData);
        if (vd && Follow(vd, known)) { g_route = known; Log("knife models: using the known path location (VData + 0x30)"); }
    }
    if (now < ks.retryAt) { note = "Couldn't change this knife's model (see nSkinzLive.log)."; return false; }

    char num[8];
    std::snprintf(num, sizeof(num), "%u", to);
    const char* cls = WeaponClass(to);
    const char* names[2] = {num, cls};
    if (g_knifeScheme == 1) std::swap(names[0], names[1]);
    void* oldVData = Get<void*>(ent, O.ent_VData);
    for (int i = 0; i < 2; ++i) {
        if (!names[i] || (g_knifeScheme >= 0 && i == 1)) continue;
        uint32_t tok = Game::StringToken(names[i]);
        Game::ChangeSubclass(ent, tok);
        uint8_t* vd = Get<uint8_t*>(ent, O.ent_VData);
        const char* nowModel = Game::ModelName(ent);
        if (g_knifeLog > 0) {
            --g_knifeLog;
            Log("knife %u -> %u via '%s' (token %08x): VData %p -> %p, model now '%s'", from, to, names[i], tok, oldVData, (void*)vd,
                nowModel ? nowModel : "?");
        }
        if (!vd || vd == oldVData) continue;
        const char* path = g_route.depth >= 0 ? Follow(vd, g_route) : nullptr;
        if (path) { Game::PrecacheModel(path); Game::SetModel(ent, path); }
        nowModel = Game::ModelName(ent);
        bool changed = nowModel && !SameModel(nowModel, ks.origModel.c_str());
        if (g_knifeLog > 0) { --g_knifeLog; Log("knife %u: model path '%s', model now '%s'", to, path ? path : "(none)", nowModel ? nowModel : "?"); }
        if (!changed) continue;
        ks.swapped = true; ks.newToken = tok; ks.to = to;
        Breadcrumb("knife %p: model %u -> %u", ent, from, to);
        if (g_knifeScheme < 0) g_knifeScheme = (names[i] == num) ? 0 : 1;
        return true;
    }
    // Nothing worked: put the knife back as it was, and don't try again for a while.
    Game::ChangeSubclass(ent, ks.origToken);
    ks.retryAt = now + 10000;
    note = "Couldn't change this knife's model (see nSkinzLive.log).";
    if (g_knifeLog > 0) { --g_knifeLog; Log("knife %u -> %u: model swap failed, retrying in 10 s", from, to); }
    return false;
}

static void UnswapKnife(void* ent, uint32_t handle) {
    auto it = g_knives.find(handle);
    if (it == g_knives.end()) return;
    if (it->second.swapped) {
        Breadcrumb("knife %p: model put back", ent);
        Game::ChangeSubclass(ent, it->second.origToken);
        if (!it->second.origModel.empty()) Game::SetModel(ent, it->second.origModel.c_str());
    }
    g_knives.erase(it);
}

static ULONGLONG g_swapAllowedAt = 0;   // knife models aren't changed for a moment after a demo jump

static bool EffectiveWeaponLegacy(void* ent,uint8_t* item,uint16_t def) {
    const auto mask=Game::MeshGroupMask(ent)&3;
    if(mask==1) return false;if(mask==2) return true;
    for(int li=0;li<2;++li) {
        AttrVec v;if(!OpenList(item,li==0 ? O.item_AttributeList : O.item_NetworkedDynamicAttributes,v)) continue;
        for(int i=0;i<*v.count;++i) {
            auto* a=*v.data+i*O.attrSize;
            if(Get<uint16_t>(a,O.attr_DefIndex)==ATTR_PAINT) return IsLegacy(def,static_cast<int>(Get<float>(a,O.attr_Value)));
        }
    }
    return IsLegacy(def,Get<int>(ent,O.econ_FallbackPaintKit));
}

static void PresentWeapon(void* ent,uint8_t* item,uint32_t handle,uint64_t xuid,uint64_t signature,uint16_t sourceDef,const GunSkin* rule,bool legacy) {
    if(!presentationListener)return;
    WeaponPresentation update;update.weapon=ent;update.item=item;update.handle=handle;update.xuid=xuid;
    update.signature=signature|1;update.definition=Get<uint16_t>(item,O.item_DefIndex);
    update.sourceDefinition=sourceDef;
    update.paint=Get<int>(ent,O.econ_FallbackPaintKit);update.finish=rule && rule->writeFinish;
    if(rule)for(const auto& patch:rule->patches) {
        const auto bits=legacy ? patch.legacy:patch.modern;
        if(patch.definition==299)update.keychain=static_cast<int>(bits);
        else if(patch.definition>=300 && patch.definition<=302)std::memcpy(&update.keychainPosition[patch.definition-300],&bits,4);
    }
    presentationListener(update);
}

static void ApplyWeapon(void* ent, uint8_t* item, uint32_t handle, uint64_t xuid, uint16_t realDef, uint16_t def, const GunSkin* rule,
                        std::string& note, bool fresh, bool ready, bool keepModel) {
    auto it = g_weapons.find(handle);
    if(rule && !rule->writeFinish && it!=g_weapons.end() && it->second.wroteFinish) {
        // Finish was disabled while extras stay active: restore it before
        // capturing the source baseline for this extras-only transaction.
        ApplyWeapon(ent,item,handle,xuid,realDef,realDef,nullptr,note,fresh,ready,true);
        it=g_weapons.end();
    }
    if (!keepModel && rule && IsKnife(def) && (def != realDef || !KnifeIsModel(ent, def))) {
        // The model swap needs a finished knife; until then leave it alone (no finish on the wrong model either).
        if (!ready || GetTickCount64() < g_swapAllowedAt) return;
        if (CrashedLastTime("knife-swap")) { rule = nullptr; note = "Knife model swap is off this session (it crashed last time)."; }
        else {
            Step step("knife model swap");
            FirstRunBegin("knife-swap");
            if (!SwapKnife(ent, handle, realDef, def, note)) rule = nullptr;   // never paint the wrong model
            FirstRunEnd("knife-swap");
        }
    }
    if (!rule) {
        if (it != g_weapons.end()) {
            Undo& u = it->second;
            UnswapKnife(ent, handle);
            if (u.bodyWant >= 0) SetBody(ent, "body", (u.maskSaved && (u.origMask & 2)) ? 1 : 0);
            if (u.meshWant && !BodyOff()) Game::SetNetworkedBodyGroup(ent, "body", (u.maskSaved && (u.origMask & 2)) ? 1 : 0);
            if (u.maskSaved && Game::MeshGroupMask(ent) != u.origMask) Game::SetMeshGroupMask(ent, u.origMask);
            if (void* hud = HudOff() ? nullptr : Game::HudModel(ent)) {
                if (u.bodyWant >= 0) SetBody(hud, "body", (u.maskSaved && (u.origMask & 2)) ? 1 : 0);
                uint64_t hm = Game::MeshGroupMask(hud);
                if (u.maskSaved && hm && (hm & 3) != (u.origMask & 3)) Game::SetMeshGroupMask(hud, (hm & ~3ull) | (u.origMask & 3));
            }
            At<int>(ent, O.econ_FallbackPaintKit) = u.paint;
            At<int>(ent, O.econ_FallbackSeed) = u.seed;
            At<float>(ent, O.econ_FallbackWear) = u.wear;
            At<int>(ent, O.econ_FallbackStatTrak) = u.stattrak;
            RestoreItem(u, item);
            RefreshWeapon(ent);
            PresentWeapon(ent,item,handle,xuid,u.key^0x726573746f7265ull,realDef,nullptr,true);
            g_weapons.erase(it);
        }
        return;
    }
    if (!keepModel && def == realDef) UnswapKnife(ent, handle);   // the rule now matches the real model
    if (it == g_weapons.end()) {
        Undo u{};
        u.weapon = true;
        u.ent = ent;
        SaveItem(u, item);
        u.paint = Get<int>(ent, O.econ_FallbackPaintKit);
        u.seed = Get<int>(ent, O.econ_FallbackSeed);
        u.wear = Get<float>(ent, O.econ_FallbackWear);
        u.stattrak = Get<int>(ent, O.econ_FallbackStatTrak);
        it = g_weapons.emplace(handle, std::move(u)).first;
    }
    Undo& u = it->second;
    {   // CS2 hides stickers and charms ("identifying UGC") on some weapons; ours must show.
        static ptrdiff_t ugcOff = -2;
        if (ugcOff == -2 && !Game::SchemaField("C_CSWeaponBase", "m_bClearWeaponIdentifyingUGC", ugcOff)) ugcOff = -1;
        bool extras = rule->charm != 0 || !rule->patches.empty();
        for (int sId : rule->stickers) extras |= sId != 0;
        if (extras && ugcOff > 0) SetIfDiff<bool>(ent, ugcOff, false);
    }
    if(!rule->writeFinish) {
        // Extra-only changes never touch fallback paint, quality, name or mesh.
        const bool legacy=EffectiveWeaponLegacy(ent,item,def);
        const bool overwritten=WriteItem(item,u,xuid,def,*rule,false,legacy);
        if(ShouldRefresh(u,Hash(xuid,def,*rule,legacy),overwritten,"weapon extras",item)) RefreshWeapon(ent);
        PresentWeapon(ent,item,handle,xuid,u.key,realDef,rule,legacy);
        return;
    }
    bool overwritten = u.written && Get<int>(ent, O.econ_FallbackPaintKit) != rule->paint;
    SetIfDiff<int>(ent, O.econ_FallbackPaintKit, rule->paint);
    SetIfDiff<int>(ent, O.econ_FallbackSeed, rule->seed);
    SetIfDiff<float>(ent, O.econ_FallbackWear, rule->wear);
    SetIfDiff<int>(ent, O.econ_FallbackStatTrak, rule->stattrak);
    const bool legacy = IsLegacy(def, rule->paint);
    overwritten |= WriteItem(item, u, xuid, def, *rule, true,legacy);
    const ULONGLONG now = GetTickCount64();
    {   // Legacy-model finishes are painted for the weapon's older mesh. Like WeaponPaints on servers, choose it with
        // the "body" body group (1 = legacy); the mesh mask (bit 1 legacy, bit 0 current) follows from it. If the body
        // group can't be set, set the mask directly.
        uint64_t mask = Game::MeshGroupMask(ent);
        if (!u.maskSaved && mask) { u.origMask = mask; u.maskSaved = true; }
        const int want = legacy ? 1 : 0;
        if (u.bodyWant != want || now >= u.bodyCheckAt) {
            if (SetBody(ent, "body", want)) {
                if (u.bodyWant != want) Breadcrumb("weapon %p: body group %d (%s finish)", ent, want, legacy ? "legacy" : "current");
                u.bodyWant = want;
            }
            u.bodyCheckAt = now + 500;
            mask = Game::MeshGroupMask(ent);
        }
        // The demo's packets keep writing the weapon's own body group and mesh (an older demo every few ticks: the
        // 2.2.1 log showed the mesh going back to 1 again and again), and CS2 rebuilds the mesh from the stored body
        // group. So the stored value itself is set every pass, then the mesh.
        if (!BodyOff()) Game::SetNetworkedBodyGroup(ent, "body", want);
        mask = Game::MeshGroupMask(ent);
        u.meshWant = legacy ? 2 : 1;
        if (mask) {
            uint64_t wantMask = (mask & ~3ull) | u.meshWant;
            if (wantMask != mask && Game::SetMeshGroupMask(ent, wantMask)) {
                Breadcrumb("weapon %p: mesh %llx -> %llx (%s finish)", ent, (unsigned long long)mask, (unsigned long long)wantMask,
                           legacy ? "legacy" : "current");
                static int logged = 0;
                if (logged++ < 3) Log("weapon def %u: mesh mask %llx -> %llx for %s finish %d", def, (unsigned long long)mask,
                                      (unsigned long long)wantMask, legacy ? "legacy-model" : "current-model", rule->paint);
                ++g_meshResets;
            }
        }
    }
    if (!HudOff()) {   // The first-person model: same model, same mesh as the weapon.
        Step step("first-person model");
        FirstRunBegin("hud-fix");
        void* hud = Game::HudModel(ent);
        const char* wm = hud ? Game::ModelName(ent) : nullptr;
        const char* hm = hud ? Game::ModelName(hud) : nullptr;
        if (hud && wm && hm && !SameModel(wm, hm)) {
            // e.g. a swapped knife: the first-person model is still the old knife. Have it made again (what the game
            // does itself when a knife's model changes), at most 3 times per change.
            if (now >= u.hudRemoveAt && u.hudRemoves < 3) {
                ++u.hudRemoves;
                u.hudRemoveAt = now + 1000;
                Breadcrumb("first-person model of weapon %p re-made (model %s -> %s)", ent, hm, wm);
                if (g_hudLog-- > 0) Log("first-person model of weapon def %u was '%s', weapon is '%s': re-made", def, hm, wm);
                Game::RemoveEntity(hud);
                g_hudDue.erase(handle);
            }
        } else if (hud) {
            if (u.bodyWant >= 0) SetBody(hud, "body", u.bodyWant);   // no-op when already asked for
            uint64_t wmask = Game::MeshGroupMask(ent), hmask = Game::MeshGroupMask(hud);
            if (wmask && hmask && (hmask & 3) != (wmask & 3)) {
                uint64_t want = (hmask & ~3ull) | (wmask & 3);
                Game::SetMeshGroupMask(hud, want);
                static int logged = 0;
                if (logged++ < 6) Log("first-person model of weapon def %u: mesh %llx -> %llx (%s finish)", def, (unsigned long long)hmask,
                                      (unsigned long long)want, legacy ? "legacy-model" : "current-model");
            }
        }
        FirstRunEnd("hud-fix");
    }
    if (ShouldRefresh(u, Hash(xuid, def, *rule,legacy), overwritten, "gun", item, fresh && def == realDef)) {
        RefreshWeapon(ent);
        u.hudRemoves = 0;
        u.matRetries = 0;
        u.hudMatRemakes = 0;
        u.checkAt = now + 1200;
        u.gunChecked = false;
        u.secondCheck = false;
    } else if (rule->paint > 0 && now - u.lastRefresh > 2500 && now >= u.matCheckAt) {
        // Did a skin material really get made? A knife model the demo never had may still be loading during the
        // first rebuilds, and then the knife (and its first-person model) stays without its finish. Check every 3 s
        // and rebuild again if there is no skin material (at most 5 times per change).
        u.matCheckAt = now + 3000;
        int a = -1, b = -1;
        Game::MaterialCounts(ent, a, b);
        if (a == 0 && b == 0 && u.matRetries < 5) {
            ++u.matRetries;
            u.lastRefresh = now;
            RefreshWeapon(ent);
            if (g_logBudget-- > 0) Log("weapon def %u has no skin material yet - rebuilding again (%d/5)", def, u.matRetries);
        }
    } else if (!u.gunChecked && now >= u.checkAt) {
        // What did the game make of it? (logged twice per gun type, to diagnose guns whose skin doesn't show)
        u.gunChecked = true;
        if (IsKnife(def) && !u.secondCheck) { u.secondCheck = true; u.gunChecked = false; u.checkAt = now + 4000; }
        static std::unordered_map<uint16_t, int> logged;
        if (logged[def]++ < (IsKnife(def) ? 6 : 2)) {
            Step step("gun check");
            using PaintFn = int (*)(void*);
            int paint = ((PaintFn)VFunc(item, 3))(item);
            AttrVec v0, v1;
            bool ok0 = OpenList(item, O.item_AttributeList, v0), ok1 = OpenList(item, O.item_NetworkedDynamicAttributes, v1);
            int a = 0, b = 0;
            Game::MaterialCounts(ent, a, b);
            Log("gun check: def %u, game reads paint kit %d (want %d), fallback %d, item id high %d, attributes %d/%d (were %d/%d), materials %d/%d",
                def, paint, rule->paint, Get<int>(ent, O.econ_FallbackPaintKit), Get<int32_t>(item, O.item_ItemIDHigh), ok0 ? *v0.count : -1,
                ok1 ? *v1.count : -1, u.lists[0].count, u.lists[1].count, a, b);
            void* hud = HudOff() ? nullptr : Game::HudModel(ent);
            const char* hm = hud ? Game::ModelName(hud) : nullptr;
            int ha = -1, hb = -1;
            if (hud) Game::MaterialCounts(hud, ha, hb);
            const char* wmn = Game::ModelName(ent);
            Log("gun check: def %u, quality %d, model '%s', %s finish, body group %d, mesh %llx; first-person model %s%s%s mesh %llx materials %d/%d",
                def, Get<int32_t>(item, O.item_EntityQuality), wmn ? wmn : "?", legacy ? "legacy" : "current", u.bodyWant,
                (unsigned long long)Game::MeshGroupMask(ent), hud ? "'" : "none (not this player's view)", hud ? (hm ? hm : "?") : "",
                hud ? "'" : "", (unsigned long long)(hud ? Game::MeshGroupMask(hud) : 0), ha, hb);
        }
    }
    PresentWeapon(ent,item,handle,xuid,u.key,realDef,rule,legacy);
}

// ------------------------------------------------------------------ gloves (on the player model)
static void RefreshGloves(void* pawn) {
    Breadcrumb("gloves of player %p re-applied (glove type %u)", pawn, Get<uint16_t>((uint8_t*)pawn + O.pawn_EconGloves, O.item_DefIndex));
    At<bool>(pawn, O.pawn_NeedToReApplyGloves) = true;
    At<uint8_t>(pawn, O.pawn_EconGlovesChanged) += 1;
}

// The player model has a body group "first_or_third_person". Its "*_default" choices show the agent's own default
// gloves, and while one of those is chosen CS2 deletes the econ glove model every frame (client.dll: the glove builder
// asks ShowsDefaultGloves(pawn) first). A player who had no econ gloves in the demo is always on "*_default", which is
// why our gloves never stayed. Servers switch it to "*_hide_default_gloves" when a player has gloves; we do the same.
static const char* kFoT = "first_or_third_person";
static int g_gloveBgLog = 8;

// Returns true when it just switched the choice (the gloves then need re-applying).
static bool HideDefaultGloves(void* pawn, Undo& u, ULONGLONG now) {
    if (BodyOff() || now < u.bgCheckAt) return false;
    u.bgCheckAt = now + 100;
    const char* to = nullptr;
    if (Game::BodyGroupIs(pawn, kFoT, "firstperson_default")) to = "firstperson_hide_default_gloves";
    else if (Game::BodyGroupIs(pawn, kFoT, "thirdperson_default")) to = "thirdperson_hide_default_gloves";
    if (!to) return false;
    Step step("hiding the default gloves");
    FirstRunBegin("body-group");
    bool ok = Game::SetBodyGroup(pawn, kFoT, to);
    FirstRunEnd("body-group");
    if (g_gloveBgLog-- > 0) Log("gloves: player model showed its default gloves - switched to '%s' %s", to, ok ? "(ok)" : "FAILED");
    if (ok) { u.bgChanged = true; Breadcrumb("player %p: body group %s", pawn, to); }
    return ok;
}

static void ShowDefaultGloves(void* pawn) {
    if (BodyOff()) return;
    if (Game::BodyGroupIs(pawn, kFoT, "firstperson_hide_default_gloves")) Game::SetBodyGroup(pawn, kFoT, "firstperson_default");
    else if (Game::BodyGroupIs(pawn, kFoT, "thirdperson_hide_default_gloves")) Game::SetBodyGroup(pawn, kFoT, "thirdperson_default");
}

static void ApplyGloves(void* pawn, uint64_t xuid, const ItemRule* rule) {
    uint32_t handle = Game::HandleOf(pawn);
    uint8_t* item = (uint8_t*)pawn + O.pawn_EconGloves;
    ItemRule sameModel;
    if (rule && rule->preserveModel) {
        const auto actual=Get<uint16_t>(item,O.item_DefIndex);
        if (!actual || actual==5028 || actual==5029) return;
        sameModel=*rule; sameModel.def=actual; rule=&sameModel;
    }
    auto it = g_gloves.find(handle);
    if (!rule || !rule->def) {
        if (it != g_gloves.end()) {
            RestoreItem(it->second, item);
            if (it->second.bgChanged || it->second.def == 0) ShowDefaultGloves(pawn);   // 0: the player had no gloves of their own
            RefreshGloves(pawn);
            g_gloves.erase(it);
        }
        return;
    }
    if (it != g_gloves.end() && it->second.ent != pawn) { g_gloves.erase(it); it = g_gloves.end(); }   // a new player entity
    if (it == g_gloves.end()) {
        Undo u{};
        u.ent = pawn;
        SaveItem(u, item);
        it = g_gloves.emplace(handle, std::move(u)).first;
    }
    Undo& u = it->second;
    const bool first = !u.written;
    bool overwritten = WriteItem(item, u, xuid, rule->def, rule->skin, false);
    if (first && g_logBudget-- > 0) {
        AttrVec v0, v1;
        bool ok0 = OpenList(item, O.item_AttributeList, v0), ok1 = OpenList(item, O.item_NetworkedDynamicAttributes, v1);
        Log("gloves: def %u -> %u, attributes %d/%d (were %d/%d), lists %s/%s, flag at 0x%x", u.def, Get<uint16_t>(item, O.item_DefIndex),
            ok0 ? *v0.count : -1, ok1 ? *v1.count : -1, u.lists[0].count, u.lists[1].count, ok0 ? "ok" : "BAD", ok1 ? "ok" : "BAD",
            (unsigned)O.pawn_NeedToReApplyGloves);
    }
    const ULONGLONG nowG = GetTickCount64();
    if (HideDefaultGloves(pawn, u, nowG)) RefreshGloves(pawn);
    else if (u.written && nowG >= u.gloveFixAt && Game::CanSeeGloveEntity() && !Game::GloveEntity(pawn) && Game::CanSetBodyGroups() &&
             !Game::BodyGroupIs(pawn, kFoT, "firstperson_default") && !Game::BodyGroupIs(pawn, kFoT, "thirdperson_default")) {
        // No glove model although nothing hides it (e.g. the game removed it before the body group took effect):
        // ask for it again, once a second at most.
        u.gloveFixAt = nowG + 1000;
        RefreshGloves(pawn);
        static int logged = 4;
        if (logged-- > 0) Log("gloves: no glove model yet - asked the game again");
    }
    if (ShouldRefresh(u, Hash(xuid, rule->def, rule->skin), overwritten, "gloves", item)) {
        // A glove type nobody in the demo wears isn't loaded: load it the way the game loads preview gloves.
        bool skip = false;
        const char* model = nullptr;
        void* kit = nullptr;
        static std::unordered_set<std::string> loaded;
        if (Game::GloveGates(item, skip, model, kit) && model && *model && loaded.insert(model).second) {
            bool ok = Game::PrecacheModel(model);
            Breadcrumb("loaded glove model %s", model);
            if (g_logBudget-- > 0) Log("gloves: loading model '%s' %s", model, ok ? "requested" : "FAILED");
            u.reapplyAt = GetTickCount64() + 1000;
        }
        RefreshGloves(pawn);
        u.checked = false;
        if (g_logBudget-- > 0) Log("gloves: asked the game to re-apply them");
    } else if (u.reapplyAt && GetTickCount64() >= u.reapplyAt) {
        u.reapplyAt = 0;
        RefreshGloves(pawn);
        u.checked = false;
        u.lastRefresh = GetTickCount64();
        if (g_logBudget-- > 0) Log("gloves: re-applied again now that the model is loaded");
    } else if (!u.checked && GetTickCount64() - u.lastRefresh > 1000) {
        // One second later: did the game act on it, and does it read our finish? (item view: [3] paint kit, [16] definition)
        u.checked = true;
        Step step("gloves check");
        using PaintFn = int (*)(void*);
        using DefFn = void* (*)(void*);
        int paint = ((PaintFn)VFunc(item, 3))(item);
        void* defp = ((DefFn)VFunc(item, 16))(item);
        if (g_logBudget-- > 0)
            Log("gloves check: re-apply flag now %d (0 = the game rebuilt them), game reads paint kit %d (want %d), definition %s, source/target %u/%u, quality %d",
                Get<uint8_t>(pawn, O.pawn_NeedToReApplyGloves), paint, rule->skin.paint, defp ? "found" : "NOT FOUND",u.def,rule->def,Get<int32_t>(item,O.item_EntityQuality));
        bool skip = false;
        const char* model = nullptr;
        void* kit = nullptr;
        if (Game::GloveGates(item, skip, model, kit) && g_logBudget-- > 0)
            Log("gloves check: game says skip=%d, glove model '%s', paint kit %s, initialized=%d", skip ? 1 : 0,
                model ? model : "(none)", kit ? "found" : "NOT FOUND", Get<uint8_t>(item, O.item_Initialized));
        if (g_logBudget-- > 0) {
            void* ge = Game::GloveEntity(pawn);
            const char* gm = ge ? Game::ModelName(ge) : nullptr;
            Log("gloves check: glove entity %s%s%s; player model shows default gloves: %s", ge ? "exists, model '" : "MISSING (the game made no glove model)",
                ge ? (gm ? gm : "?") : "", ge ? "'" : "",
                !Game::CanSetBodyGroups() ? "unknown" :
                (Game::BodyGroupIs(pawn, kFoT, "firstperson_default") || Game::BodyGroupIs(pawn, kFoT, "thirdperson_default")) ? "YES" : "no");
        }
    }
}

// ------------------------------------------------------------------ every frame
static void Snapshot(const std::unordered_map<uint64_t, PlayerInfo>& seen) {
    std::vector<PlayerInfo> list;
    for (int i = 1; i <= 64; ++i) {
        void* ctrl = Game::EntityAt(i);
        if (!ctrl) continue;
        const char* dn = Game::DesignerName(ctrl);
        if (!dn || std::strcmp(dn, "cs_player_controller") != 0) continue;
        uint64_t xuid = ControllerXuid(ctrl);
        if (!xuid) continue;   // bots
        PlayerInfo p;
        auto s = seen.find(xuid);
        if (s != seen.end()) p = s->second;
        p.xuid = xuid;
        char name[129] = {};
        std::memcpy(name, (uint8_t*)ctrl + O.ctrl_iszPlayerName, 128);
        p.name = name;
        p.team = Get<uint8_t>(ctrl, O.ent_iTeamNum);
        list.push_back(std::move(p));
    }
    players.swap(list);
    for (auto& p : players) {
        auto r = rules.find(p.xuid);
        if (r != rules.end() && r->second.name != p.name) r->second.name = p.name;
    }
}

// CS2 plays demos with "networked" animation data: every model's animation info comes from a table inside the demo,
// and a model the demo never loaded has no entry - CS2 then crashes in its network code (1.5.4 crash report, traced
// in client.dll to the AnimAssetData table). The game's own setting ag2_use_networked_serialization_context_demo
// switches demo playback to local animation data, which works for any model. We turn it off only when needed.
static bool g_animCtxOk = false;
int demoCurrent = -1;
static ULONGLONG g_demoCheckAt = 0;
static int g_lastDemoVer = 0, g_lastGameVer = 0;

// Local animation data only works for demos recorded with this CS2 version. An older demo's own animation data
// doesn't match the game's any more, and switching crashes CS2 in its animation system (2.1.1 report:
// animationsystem.dll / resourcesystem.dll right after the switch, on a demo 2-3 months old).
static void CheckDemoVersion(ULONGLONG now, bool force) {
    if (!force && now < g_demoCheckAt) return;
    g_demoCheckAt = now + 1000;
    int dv = 0, gv = 0;
    int r = Game::DemoIsCurrent(&dv, &gv);
    if (r >= 0 && (dv != g_lastDemoVer || gv != g_lastGameVer)) {
        g_lastDemoVer = dv; g_lastGameVer = gv;
        Log("demo version %d, game version %d: %s", dv, gv, r == 1 ? "same - any knife/glove model can be used" :
            g_animBad.count(dv) ? "older demo, crashed before - only knife/glove models someone in this demo has" :
            g_animOk.count(dv) ? "older demo, worked before - any knife/glove model can be used" :
            settings.anyModelOldDemos ? "older demo - explicit local-animation model test enabled" :
            gv - dv <= settings.anyModelVersionsBack ? "older demo - any knife/glove model will be tried" :
            "much older demo - only knife/glove models someone in this demo has (others would crash CS2)");
    }
    if (r >= 0) demoCurrent = r;
}

bool anyModelBlocked = false;
static int g_animTestVersion = 0;      // older-demo version being tried right now
static ULONGLONG g_animTestSince = 0;

// May this demo use CS2's own animation data (needed for knife/glove models the demo doesn't have)?
// Same version: yes. Older: yes if it worked before for that version, no if it crashed before, otherwise try it
// when it's at most any_model_versions_back versions old (1.3.2: a demo one version old was refused for no reason).
static bool LocalAnimAllowed() {
    if (demoCurrent == 1) return true;
    if (demoCurrent != 0 || g_lastDemoVer <= 0) return false;
    if (g_animBad.count(g_lastDemoVer)) return false;
    if (settings.anyModelOldDemos || g_animOk.count(g_lastDemoVer)) return true;
    return g_lastGameVer >= g_lastDemoVer && g_lastGameVer - g_lastDemoVer <= settings.anyModelVersionsBack;
}

// Called every pass while a model outside the demo is in use: a demo restart or the console can set it back.
static bool LocalAnimContextReady(void*, ULONGLONG now) {
    if (!LocalAnimAllowed()) return false;
    bool ok = Game::UseLocalAnimData();
    if (ok && !g_animCtxOk) {
        Breadcrumb("demo playback switched to local animation data");
        if (demoCurrent == 0 && !g_animOk.count(g_lastDemoVer) && !g_animTestVersion) {
            // First time for this older version: leave a note; if CS2 crashes now, the next start reads it.
            std::ofstream t(AnimTestPath(), std::ios::trunc);
            t << g_lastDemoVer << "\n";
            g_animTestVersion = g_lastDemoVer;
            g_animTestSince = now;
            Log("trying CS2's own animation data in this demo (version %d, game %d) - if CS2 crashes now, nSkinzLive won't do it "
                "again for this demo version", g_lastDemoVer, g_lastGameVer);
        }
    }
    g_animCtxOk = ok;
    return ok;
}

// Called every pass: after 30 s without a crash the older version counts as working.
static void AnimTestCheck(ULONGLONG now) {
    if (!g_animTestVersion) return;
    if (!Game::AnimDataSwitched()) {   // switched back before the 30 s (demo ended, rule removed): no verdict yet
        if (now - g_animTestSince < 30000) { DeleteFileA(AnimTestPath().c_str()); g_animTestVersion = 0; }
        return;
    }
    if (now - g_animTestSince < 30000) return;
    g_animOk.insert(g_animTestVersion);
    DeleteFileA(AnimTestPath().c_str());
    Log("CS2's own animation data works in demos of version %d - remembered", g_animTestVersion);
    g_animTestVersion = 0;
    SaveAnimationHistory(DllFolder());
}

static const char* NoModelNote(const char* what) {
    static char buf[200];
    if (!LocalAnimAllowed())
        std::snprintf(buf, sizeof(buf), "This demo is %d CS2 versions old%s: only %s models someone in this demo has can be used "
                      "(others crash CS2). They are marked 'in this demo'.", g_lastGameVer - g_lastDemoVer,
                      g_animBad.count(g_lastDemoVer) ? " and CS2 crashed with other models before" : "", what);
    else
        std::snprintf(buf, sizeof(buf), "This CS2 version can't use %s models nobody in the demo has (see nSkinzLive.log).", what);
    return buf;
}


// ------------------------------------------------------------------ T / CT loadouts
// A player's rules have a "both sides" loadout and one for each side. The side's own item wins; anything the side
// doesn't set comes from "both". The team is the player's team right now (2 = T, 3 = CT), so after the half-time
// switch the other side's loadout is used.
static const GunSkin* GunRule(const PlayerRules& pr, int team, uint16_t def) {
    if (const Loadout* s = pr.Team(team)) {
        auto g = s->guns.find(def);
        if (g != s->guns.end()) return &g->second;
    }
    auto g = pr.guns.find(def);
    return g != pr.guns.end() ? &g->second : nullptr;
}
static const ItemRule* KnifeRule(const PlayerRules& pr, int team) {
    if (const Loadout* s = pr.Team(team)) if (s->knife.def) return &s->knife;
    return pr.knife.def ? &pr.knife : nullptr;
}
static const ItemRule* GloveRule(const PlayerRules& pr, int team) {
    if (const Loadout* s = pr.Team(team)) if (s->gloves.def) return &s->gloves;
    return pr.gloves.def ? &pr.gloves : nullptr;
}
static int AgentTeam(const std::string& path) {
    for (int i = 0; i < kNumAgents; ++i) if (!_stricmp(kAgents[i].model, path.c_str())) return kAgents[i].team;
    return 0;
}
// An agent set for both sides is only used on its own side (a T agent on a CT player would be confusing); one set
// on the T or CT tab is used there whatever it is.
static const std::string* AgentRule(const PlayerRules& pr, int team) {
    if (team != 2 && team != 3) return nullptr;
    if (const Loadout* s = pr.Team(team)) if (!s->agent.empty()) return &s->agent;
    return nullptr;   // agents are per side only
}

static std::unordered_set<uint32_t> g_knownWeapons;   // weapon handles seen in an earlier pass
static bool g_firstPass = true;

// Between our main pass and the picture being drawn the game can still put a weapon's mesh back (from the demo's
// own body group). Every later frame stage checks the few weapons we changed and puts our mesh back.
static void ReassertMeshes() {
    for (auto& kv : g_weapons) {
        Undo& u = kv.second;
        if (!u.meshWant) continue;
        void* w = Game::FromHandle(kv.first);
        if (!w || w != u.ent) continue;
        uint64_t m = Game::MeshGroupMask(w);
        if (m && (m & 3) != u.meshWant) { Game::SetMeshGroupMask(w, (m & ~3ull) | u.meshWant); ++g_lateResets; }
        if (HudOff()) continue;
        if (void* hud = Game::HudModel(w)) {
            uint64_t hm = Game::MeshGroupMask(hud);
            if (hm && (hm & 3) != u.meshWant) { Game::SetMeshGroupMask(hud, (hm & ~3ull) | u.meshWant); ++g_lateResets; }
        }
    }
}


// ------------------------------------------------------------------ agents (player models)
// The pawn's model is set with the game's own SetModel, like the knife swap. The demo can send the pawn's model again
// (new round, respawn): then it is set again on the next pass. The player's real model is kept and put back when the
// rule is removed. After a change the game is asked to put the gloves on again, because a new model starts with its
// own default gloves showing.
struct AgentSwap {
    void* ent = nullptr;
    std::string orig, want;
    ULONGLONG lastSet = 0;
    int setsLately = 0;
    ULONGLONG windowAt = 0;
    std::string pending;          // the agent the rule asks for now
    ULONGLONG pendingSince = 0;   // when the rule last changed (0: first one for this pawn - no wait)
};
static std::unordered_map<uint32_t, AgentSwap> g_agents;   // by pawn handle
// Agent models nSkinzLive asked the game to load, and when. Loading is not instant: a model set on a player before it
// has loaded, or one that gets unloaded while the game still animates it, crashed 1.7 when agents were clicked through
// quickly. So a model is loaded first and only used a moment later, and it stays loaded (the load request keeps it)
// until the demo ends, even after switching to another agent.
static std::unordered_map<std::string, ULONGLONG> g_agentLoads;
static std::unordered_map<uint32_t, ULONGLONG> g_agentLastSet;   // by pawn handle, kept when a rule is removed
static int g_agentLog = 60;

static bool AgentOff() { static int v = -1; if (v < 0) v = CrashedLastTime("agent") ? 1 : 0; return v == 1; }

// Returns true while the rule is in force (also while it waits for a moment to apply it).
static bool ApplyAgent(void* pawn, uint32_t handle, const std::string* want, ULONGLONG now) {
    const char* curC = Game::ModelName(pawn);
    const std::string cur = curC ? curC : "";
    auto it = g_agents.find(handle);
    if (it != g_agents.end() && it->second.ent != pawn) { g_agents.erase(it); it = g_agents.end(); }
    if (!want) {
        if (it != g_agents.end()) {
            AgentSwap& a = it->second;
            if (SameModel(cur.c_str(), a.want.c_str()) && !a.orig.empty()) {
                const ULONGLONG last = g_agentLastSet[handle];
                if (now < g_swapAllowedAt || (last && now - last < 1500)) return true;   // not yet: keep the record
                g_agentLastSet[handle] = now;
                Step step("putting a player model back");
                FirstRunBegin("agent");
                Game::SetModel(pawn, a.orig.c_str());
                At<bool>(pawn, O.pawn_NeedToReApplyGloves) = true;
                FirstRunEnd("agent");
                Breadcrumb("pawn %p: player model back to %s", pawn, a.orig.c_str());
            }
            g_agents.erase(it);
        }
        return false;
    }
    if (cur.empty() || now < g_swapAllowedAt) return true;
    AgentSwap& a = g_agents[handle];
    if (!a.ent) { a.ent = pawn; a.orig = cur; }
    if (a.pending != *want) { a.pendingSince = a.pending.empty() ? 0 : now; a.pending = *want; }
    if (SameModel(cur.c_str(), want->c_str())) { a.want = *want; return true; }
    if (!a.want.empty() && !SameModel(cur.c_str(), a.want.c_str())) a.orig = cur;   // the demo set a model of its own

    // 1. load the model and give it time to load
    auto ld = g_agentLoads.find(*want);
    if (ld == g_agentLoads.end()) {
        Step step("loading an agent model");
        FirstRunBegin("agent-load");
        const bool ok = Game::PrecacheModel(want->c_str());
        FirstRunEnd("agent-load");
        g_agentLoads[*want] = ok ? now : ~0ull;
        Breadcrumb("agent model load requested: %s", want->c_str());
        if (g_agentLog-- > 0) Log("agent: loading %s%s", want->c_str(), ok ? "" : " FAILED (not used)");
        return true;
    }
    if (ld->second == ~0ull || now - ld->second < 1500) return true;
    // 2. while the rule is still being changed (clicking through agents in the window), wait until it settles
    if (a.pendingSince && now - a.pendingSince < 1000) return true;
    // 3. at most one model change per player every 1.5 s
    const ULONGLONG last = g_agentLastSet[handle];
    if (last && now - last < 1500) return true;
    if (now >= a.windowAt) { a.windowAt = now + 10000; a.setsLately = 0; }
    if (++a.setsLately > 15) {   // the game keeps putting it back: don't fight it every frame
        if (a.setsLately == 16 && g_agentLog-- > 0) Log("agent: the game keeps resetting the player model of pawn %p - trying less often", pawn);
        if (now - a.lastSet < 2000) return true;
    }
    Step step("changing a player model");
    FirstRunBegin("agent");
    Breadcrumb("pawn %p: player model %s -> %s", pawn, cur.c_str(), want->c_str());
    Game::SetModel(pawn, want->c_str());
    At<bool>(pawn, O.pawn_NeedToReApplyGloves) = true;
    FirstRunEnd("agent");
    a.want = *want;
    a.lastSet = now;
    g_agentLastSet[handle] = now;
    if (!g_agentsSeen.count(*want)) g_agentsSetByUs.insert(*want);
    if (g_agentLog-- > 0) Log("agent: %s -> %s", cur.c_str(), want->c_str());
    return true;
}

// ------------------------------------------------------------------ demo jumps
// Jumping in a demo (demo_gototick, round skip, the demo UI's timeline) makes CS2 go back to the last full snapshot and
// fast-forward from there, running the frame stages for every tick on the way without drawing. Doing the full skins
// pass on each of those (and swapping knives, rebuilding skins and setting models for weapons and players that only
// live for a moment of the fast-forward) made jumps take much longer. So while the demo jumps or fast-forwards,
// nSkinzLive does nothing at all, and applies everything once it has arrived.
static ULONGLONG g_quietUntil = 0;
static bool g_wasQuiet = false;
static double NowMsPrecise() {
    static LARGE_INTEGER freq{};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)freq.QuadPart;
}
static bool DemoIsJumping() {
    static int lastTick = -1, winTick = -1;
    static double winAt = 0;
    const int tick = Game::DemoTick();
    const double now = NowMsPrecise();
    if (tick < 0) { lastTick = winTick = -1; return false; }
    bool fast = false;
    if (lastTick >= 0 && (tick < lastTick || tick > lastTick + 256)) fast = true;   // a jump
    if (winTick < 0 || tick < winTick) { winTick = tick; winAt = now; }
    else if (tick - winTick > 160 && now - winAt < 100) fast = true;   // > 1600 ticks a second: fast-forwarding
    if (now - winAt >= 100) { winTick = tick; winAt = now; }
    lastTick = tick;
    const ULONGLONG ms = GetTickCount64();
    if (fast) g_quietUntil = ms + 350;
    return ms < g_quietUntil;
}

void OnFrameStage(int stage) {
    if (demoPlaying && DemoIsJumping()) {
        if (!g_wasQuiet) { g_wasQuiet = true; Breadcrumb("demo jumping - waiting"); }
        return;
    }
    if (g_wasQuiet) { g_wasQuiet = false; Breadcrumb("demo jump finished - applying"); }
    // The right moment: just before the stage in which CS2 runs PostDataUpdate for every entity the demo just
    // created or changed. Values written now are used by the game's own skin, glove and model building - as if the
    // demo had contained them (the way nSkinz worked in CS:GO). If that stage can't be found, run once per frame.
    const int pdu = Game::PostDataUpdateStage();
    std::unique_lock<std::mutex> guard(lock, std::defer_lock);
    static ULONGLONG lastPdu = 0;
    const ULONGLONG nowMs = GetTickCount64();
    if (pdu >= 0 && stage == pdu) {
        lastPdu = nowMs;
        // The window may be drawing (it holds the lock for a moment): wait at most 2 ms, never stall the game.
        if (!guard.try_lock()) {
            const double until = NowMsPrecise() + 2.0;
            while (!guard.try_lock()) {
                if (NowMsPrecise() > until) return;   // next frame
                SwitchToThread();
            }
        }
    } else if (pdu >= 0 && nowMs - lastPdu < 250) {
        static const bool keepOff = CrashedLastTime("mesh-keep");
        if (!keepOff && guard.try_lock() && demoPlaying && settings.enabled && !g_weapons.empty()) {
            Step step("keeping weapon meshes");
            FirstRunBegin("mesh-keep");
            ReassertMeshes();
            FirstRunEnd("mesh-keep");
        }
        return;   // that stage is running: wait for it
    } else {      // stage unknown, or not running (demo paused): once every few milliseconds instead
        static LARGE_INTEGER freq{}, last{};
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
        if (last.QuadPart && (t.QuadPart - last.QuadPart) * 1000 < freq.QuadPart * 2) return;
        if (!guard.try_lock()) return;
        last = t;
    }

    {
        Step step("checking for demo playback");
        FirstRunBegin("demo-check");
        demoPlaying = Game::IsPlayingDemo();
        FirstRunEnd("demo-check");
    }
    {
        static int wasPlaying = -1;
        if ((int)demoPlaying != wasPlaying) {
            if (wasPlaying >= 0) Log(demoPlaying ? "demo playback started" : "demo playback stopped (demo ended, closed, or the next one is loading)");
            wasPlaying = demoPlaying;
        }
    }
    if (!demoPlaying) {
        // Between demos: forget which knife and glove models the last demo had (the next demo may not have them),
        g_knivesSeen.clear(); knivesInDemo.clear(); g_glovesSeen.clear(); glovesInDemo.clear(); g_agentsSeen.clear(); g_agentsSetByUs.clear(); agentsInDemo.clear(); g_agentLoads.clear();
        // and put CS2's animation setting back, so the next demo (maybe an older one) loads with its own data.
        if (Game::AnimDataSwitched()) { Game::RestoreAnimData(); g_animCtxOk = false; }
        demoCurrent = -1;
        g_lastDemoVer = g_lastGameVer = 0;
        anyModelBlocked = false;
        if (g_animTestVersion) { DeleteFileA(AnimTestPath().c_str()); g_animTestVersion = 0; }
    } else {
        CheckDemoVersion(GetTickCount64(), demoCurrent < 0);
        AnimTestCheck(GetTickCount64());
        anyModelBlocked = !LocalAnimAllowed();
        if (Game::AnimDataSwitched() && demoCurrent == 0 && !LocalAnimAllowed()) {
            Game::RestoreAnimData();
            g_animCtxOk = false;
            Breadcrumb("older demo: animation setting put back");
        }
    }
    if (!demoPlaying || !settings.enabled) {
        g_weapons.clear();
        g_gloves.clear();
        g_knives.clear();
        g_agents.clear();
        g_agentLastSet.clear();
        gloveNote.clear();liveWeaponModels.clear();
        lastStatus = demoPlaying ? "Turned off" : "Not playing a demo - nSkinzLive only works in demos";
        return;
    }

    Step step("reading the entity list");
    void* es = Game::EntitySystem();
    if (!es) { lastStatus = "Can't find the entity list (see nSkinzLive.log)"; return; }
    if (es != g_lastEntitySystem) { g_weapons.clear(); g_gloves.clear(); g_knives.clear(); g_knivesSeen.clear(); knivesInDemo.clear(); g_glovesSeen.clear(); glovesInDemo.clear(); g_agentsSeen.clear(); g_agentsSetByUs.clear(); agentsInDemo.clear(); g_agentLoads.clear(); g_agents.clear(); g_agentLastSet.clear(); g_knownWeapons.clear(); g_firstPass = true; g_lastEntitySystem = es; }

    const ULONGLONG now = GetTickCount64();

    // A jump in the demo (seek, round skip, Refresh now) rebuilds every entity. Touching them while the game is still
    // building them crashed 1.4.2, so forget the old ones and keep hands off for a moment.
    const int tick = Game::DemoTick();
    if (tick >= 0 && g_lastTick >= 0 && (tick < g_lastTick || tick > g_lastTick + 256)) {
        g_swapAllowedAt = now + 700;
        // A jump can rebuild entities at the very same handle and address, so per-entity state can't be trusted:
        // start over. Items that kept our changes are recognised (same values / knife already swapped) and stay right.
        g_weapons.clear(); g_knives.clear(); g_gloves.clear(); g_agents.clear(); g_agentLastSet.clear(); g_pendingRebuild.clear();
        if (g_logBudget-- > 0) Log("demo jumped from tick %d to %d - re-applying", g_lastTick, tick);
    }
    if (tick >= 0) g_lastTick = tick;

    const bool snapshot = now >= g_nextSnapshot;
    if(snapshot) liveWeaponModels.clear();
    std::unordered_map<uint64_t, PlayerInfo> seen;
    std::unordered_set<uint32_t> alive;
    std::unordered_set<uint32_t> seenWeapons;
    int changed = 0;
    liveKeychainModules.clear();
    std::string note,blocked_glove_note;
    int diagWeapons = 0, diagOwned = 0, diagRuled = 0;            // why nothing changes, if nothing does
    std::unordered_map<uint64_t, std::string> diagHeld;           // players with skins set: what they hold

    // Each player's team right now, for their T / CT loadout.
    std::unordered_map<uint64_t, int> teams;
    for (int i = 1; i <= 64; ++i) {
        void* ctrl = Game::EntityAt(i);
        if (!ctrl) continue;
        const char* dn = Game::DesignerName(ctrl);
        if (!dn || std::strcmp(dn, "cs_player_controller") != 0) continue;
        if (uint64_t x = ControllerXuid(ctrl)) teams[x] = Get<uint8_t>(ctrl, O.ent_iTeamNum);
    }
    auto teamOf = [&](uint64_t x) { auto t = teams.find(x); return t != teams.end() ? t->second : 0; };

    // Weapons: guns and knives, wherever they are.
    for (int c = 0; c < Game::HighestChunk(); ++c) {
        uint8_t* chunk = Game::Chunk(es, c);
        if (!chunk) continue;
        for (int k = 0; k < 512; ++k) {
            uint8_t* identity = chunk + O.identitySize * k;
            void* ent = Get<void*>(identity, 0);
            if (!ent) continue;
            const char* dn = Get<const char*>(identity, O.identity_designerName);
            if(dn && dn[0]=='k' && std::strcmp(dn,"keychain_module")==0) {
                static ptrdiff_t kit=-2,seed=-2;
                if(kit==-2 && !Game::SchemaField("C_KeychainModule","m_nKeychainDefID",kit))kit=-1;
                if(seed==-2 && !Game::SchemaField("C_KeychainModule","m_nKeychainSeed",seed))seed=-1;
                liveKeychainModules.push_back({Get<uint32_t>(identity,0x10),Get<uint32_t>(ent,O.ent_hOwnerEntity),
                    kit>0 ? Get<int>(ent,kit):-1,seed>0 ? Get<int>(ent,seed):-1});
                continue;
            }
            if (!dn || std::strncmp(dn, "weapon_", 7) != 0) continue;

            // "Ready" = the game finished building it (VData and a model). Values can be written before that (that's the
            // point: the game then builds it with them); only a knife model swap needs a finished knife.
            const char* mdl = Game::ModelName(ent);
            const bool ready = Get<void*>(ent, O.ent_VData) && mdl && *mdl;
            uint8_t* item = (uint8_t*)ent + O.econ_AttributeManager + O.container_Item;
            uint32_t handle = Get<uint32_t>(identity, 0x10);
            const bool fresh = !g_firstPass && !g_knownWeapons.count(handle);
            seenWeapons.insert(handle);
            {   // a demo jump can rebuild an entity under the same handle: then its old state doesn't apply
                auto w = g_weapons.find(handle);
                if (w != g_weapons.end() && w->second.ent != ent) g_weapons.erase(w);
                auto k = g_knives.find(handle);
                if (k != g_knives.end() && k->second.ent != ent) g_knives.erase(k);
            }
            const uint64_t holder = OwnerXuid(ent);       // who has it in hand (for the player list)
            uint64_t xuid = SkinOwnerXuid(ent);            // whose skin it gets
            ++diagWeapons;
            if (xuid) ++diagOwned;
            if (xuid && rules.count(xuid)) {
                ++diagRuled;
                char b[24];
                std::snprintf(b, sizeof(b), " %u", Get<uint16_t>(item, O.item_DefIndex));
                diagHeld[xuid] += b;
            }
            auto known = g_weapons.find(handle);
            // The definition the gun really has (we may have changed it ourselves).
            uint16_t def = known != g_weapons.end() ? known->second.def : Get<uint16_t>(item, O.item_DefIndex);
            const bool knife = IsKnife(def);
            // Only knives the demo itself has count as "in the demo": a knife we swapped (after a jump our record of it
            // is gone, but the item still says Butterfly) does not. Counting it crashed 1.6: CS2 was then left on the
            // demo's animation data with a knife model that data doesn't have (client.dll+0x35eb83).
            if (knife && (known != g_weapons.end() || !IsOurItem(item)) && g_knivesSeen.insert(def).second)
                knivesInDemo.assign(g_knivesSeen.begin(), g_knivesSeen.end());
            if (snapshot && holder && Get<uint32_t>(ent, O.ent_hOwnerEntity) != 0xffffffffu && holder == xuid) {
                PlayerInfo& p = seen[holder];
                if (knife) { p.knife = def; p.liveKnife = Get<uint16_t>(item, O.item_DefIndex); } else p.guns.push_back(def);
            }

            const GunSkin* rule = nullptr;
            bool keepModel = false;
            uint16_t newDef = def;
            if (xuid) {
                auto pr = rules.find(xuid);
                const int team = teamOf(xuid);
                const ItemRule* kr = pr != rules.end() && knife ? KnifeRule(pr->second, team) : nullptr;
                if (pr != rules.end()) {
                    if (kr) {
                        // A model the demo never loaded isn't known to CS2's network code, and it crashes on it
                        // later (1.5.4 crash report). So only swap to knives that some player in this demo has.
                        const uint16_t want = kr->def;
                        if (kr->preserveModel) { const auto actual=Get<uint16_t>(item,O.item_DefIndex); if(!IsKnife(actual)) continue; rule = &kr->skin; newDef = actual; keepModel = true; }
                        else if (want == def) { rule = &kr->skin; }
                        else if (!(Game::CanSwapModels() && settings.knifeModels))
                            note = "Knife model change is off: the finish shows only when the player's knife is that model.";
                        else if (!g_knivesSeen.count(want) && !settings.anyModel)
                            note = "Nobody in this demo has that knife model. Tick 'Any model' to allow it.";
                        else if (!g_knivesSeen.count(want) && !LocalAnimContextReady(es, now))
                            note = NoModelNote("knife");
                        else { rule = &kr->skin; newDef = want; }
                    } else if (!knife) {
                        rule = GunRule(pr->second, team, def);
                    }
                }
            }
            ApplyWeapon(ent, item, handle, xuid, def, newDef, rule, note, fresh, ready, keepModel);
            if(snapshot && holder && !knife) liveWeaponModels[{holder,def}]=EffectiveWeaponLegacy(ent,item,def);
            if (rule) { alive.insert(handle); ++changed; }
        }
    }
    for (auto it = g_weapons.begin(); it != g_weapons.end();) it = alive.count(it->first) ? std::next(it) : g_weapons.erase(it);
    g_knownWeapons.swap(seenWeapons);
    g_firstPass = false;
    for (auto it = g_knives.begin(); it != g_knives.end();) it = alive.count(it->first) ? std::next(it) : g_knives.erase(it);

    if (g_regenDue && now >= g_regenDue && now - g_lastRegen >= 800) {
        g_regenDue = 0;
        g_regenSecond = now + 1600;
        g_regenWanted = true;
    } else if (g_regenSecond && now >= g_regenSecond) {
        g_regenSecond = 0;
        g_regenWanted = true;
    }
    if (g_regenWanted) {
        g_regenWanted = false;
        g_lastRegen = now;
        // Always rebuilt in place. Up to 2.2 the "Refresh: jump 1 tick back" setting turned this off, and then nothing
        // rebuilt skins after a round change or demo jump - the 2.2 old-demo log had no "rebuilt weapon skins" at all,
        // so guns kept the demo's own skin. Jumping is now only the "Refresh now" button.
        {
            if (Game::CanRegenerate() && !CrashedLastTime("regenerate")) {
                Step step("regenerating weapon skins");
                FirstRunBegin("regenerate");
                Breadcrumb("rebuilt all weapon skins");
                Game::RegenerateWeaponSkins();
                // The routine above only rebuilds guns that already had a custom skin. A gun that was stock in the demo
                // (no skin material yet) is skipped, so its HUD name changed but it kept looking stock. Build those
                // with the same per-weapon step, now that the cache is clear.
                int built = 0;
                for (uint32_t h : alive) {
                    void* w = Game::FromHandle(h);
                    if (!w) continue;
                    int a = 0, b = 0;
                    Game::MaterialCounts(w, a, b);
                    if (a > 0 || b > 0) continue;
                    if (Game::RegenerateWeapon(w, true) == 1) ++built;
                }
                if (built) Breadcrumb("built skins for %d stock gun(s)", built);
                if (built && g_logBudget-- > 0) Log("built skins for %d gun(s) that had none yet", built);
                if (g_logBudget-- > 0) Log("rebuilt weapon skins");
                FirstRunEnd("regenerate");
                // No need to re-make first-person models: CS2 copies the weapon's skin material to the first-person
                // model every frame by itself (client.dll, the arms' per-frame update), once the weapon has one.
            }
            else RequestSeek();
        }
    }

    {
        static ULONGLONG logAt = 0;
        if (now >= logAt) {
            if (logAt && (g_meshResets || g_lateResets) && g_logBudget-- > 0)
                Log("weapon meshes: the game put back the demo's mesh %d time(s) before and %d time(s) after its update in the last 10 s (fixed each time)",
                    g_meshResets, g_lateResets);
            g_meshResets = g_lateResets = 0;
            logAt = now + 10000;
        }
    }

    if (!g_hudDue.empty()) {
        Step step("re-making first-person models");
        FirstRunBegin("hud-remake");
        RunHudRemakes(now);
        FirstRunEnd("hud-remake");
    }

    // Agents and gloves: on each player's pawn.
    std::unordered_set<uint32_t> pawns, agentPawns;
    for (int i = 1; i <= 64; ++i) {
        void* ctrl = Game::EntityAt(i);
        if (!ctrl) continue;
        const char* dn = Game::DesignerName(ctrl);
        if (!dn || std::strcmp(dn, "cs_player_controller") != 0) continue;
        uint64_t xuid = ControllerXuid(ctrl);
        void* pawn = Game::FromHandle(Get<uint32_t>(ctrl, O.ctrl_hPlayerPawn));
        if (!xuid || !pawn || !Game::ModelName(pawn) || !*Game::ModelName(pawn)) continue;
        {
            auto known = g_gloves.find(Game::HandleOf(pawn));
            uint16_t real = known != g_gloves.end() ? known->second.def : Get<uint16_t>((uint8_t*)pawn + O.pawn_EconGloves, O.item_DefIndex);
            if (snapshot) { seen[xuid].gloves = real; seen[xuid].liveGloves = Get<uint16_t>((uint8_t*)pawn + O.pawn_EconGloves, O.item_DefIndex); }
            const bool ours = known == g_gloves.end() && IsOurItem((uint8_t*)pawn + O.pawn_EconGloves);
            if (real && !ours && g_glovesSeen.insert(real).second) glovesInDemo.assign(g_glovesSeen.begin(), g_glovesSeen.end());
        }
        auto pr = rules.find(xuid);
        const int team = Get<uint8_t>(ctrl, O.ent_iTeamNum);
        const uint32_t pawnHandle = Game::HandleOf(pawn);

        // Agent first: a new model resets the body groups the gloves need.
        {
            auto ag = g_agents.find(pawnHandle);
            const bool oursNow = ag != g_agents.end() && ag->second.ent == pawn && SameModel(Game::ModelName(pawn), ag->second.want.c_str());
            const std::string real = oursNow ? ag->second.orig : std::string(Game::ModelName(pawn));
            if (snapshot) seen[xuid].agent = real;
            const std::string* want = pr != rules.end() ? AgentRule(pr->second, team) : nullptr;
            // Only models the demo itself put on someone count as "in the demo" - not one we set that survived a demo
            // jump after our record of it was cleared (that mistake with knives crashed 1.6).
            const bool maybeOurs = !oursNow && want && SameModel(real.c_str(), want->c_str()) && g_agentsSetByUs.count(*want);
            if (!real.empty() && !maybeOurs && g_agentsSeen.insert(real).second) agentsInDemo.assign(g_agentsSeen.begin(), g_agentsSeen.end());
            if (want && (AgentOff() || !settings.agents || !Game::CanSetModel())) want = nullptr;
            if (want && !g_agentsSeen.count(*want)) {   // same as knives: a model the demo never loaded
                if (!settings.anyModel) { want = nullptr; note = "Nobody in this demo has that agent. Tick 'Any model' to allow it."; }
                else if (!LocalAnimContextReady(es, now)) { want = nullptr; note = NoModelNote("player"); }
            }
            if (ApplyAgent(pawn, pawnHandle, want, now)) { agentPawns.insert(pawnHandle); ++changed; }
        }

        const ItemRule* rule = pr != rules.end() ? GloveRule(pr->second, team) : nullptr;
        if (rule && (CrashedLastTime("gloves") || !settings.gloves)) {
            blocked_glove_note="Glove hotpatch disabled after a previous glove fault, or by engine settings.";rule=nullptr;
        }
        // Keep the imported old-Demo guard. The nSkinz test opt-in explicitly
        // requests the upstream local-animation path; failed versions still stop.
        if (rule && !rule->preserveModel && !g_glovesSeen.count(rule->def)) {
            const int wanted=rule->def;
            if (!settings.anyModel) { rule=nullptr;note="Nobody in this demo wears that glove type. Any-model loading is disabled."; }
            else if (!LocalAnimContextReady(es, now)) { rule=nullptr;note=NoModelNote("glove"); }
            if(!rule) blocked_glove_note="Requested glove "+std::to_string(wanted)+" is absent from Demo (demo "+
                std::to_string(g_lastDemoVer)+", game "+std::to_string(g_lastGameVer)+"). "+note;
        }
        {
            Step step("gloves");
            if (rule) FirstRunBegin("gloves");
            ApplyGloves(pawn, xuid, rule);
            if (rule) FirstRunEnd("gloves");
        }
        if (rule) { pawns.insert(Game::HandleOf(pawn)); ++changed; }
    }
    for (auto it = g_gloves.begin(); it != g_gloves.end();) it = pawns.count(it->first) ? std::next(it) : g_gloves.erase(it);
    // Pawns that are gone (or whose player left): forget them. A pawn still there but without a rule was handled above.
    for (auto it = g_agents.begin(); it != g_agents.end();) it = agentPawns.count(it->first) ? std::next(it) : g_agents.erase(it);

    if (snapshot) { Snapshot(seen); g_nextSnapshot = now + 300; }
    knifeNote = note;
    if(gloveNote!=blocked_glove_note) {
        gloveNote=std::move(blocked_glove_note);
        Log(gloveNote.empty() ? "Glove model admission is no longer blocked" : "Glove model blocked: %s",gloveNote.c_str());
    }

    if (g_seekWanted && now >= g_seekAt) {
        g_seekWanted = false;
        int tick = Game::DemoTick();
        if (tick > 0) {
            char cmd[64];
            std::snprintf(cmd, sizeof(cmd), "demo_gototick %d", tick > 1 ? tick - 1 : tick);
            Breadcrumb("asked the demo to jump to tick %d", tick > 1 ? tick - 1 : tick);
            Game::ExecuteCommand(cmd);
            g_swapAllowedAt = now + 1500;
        }
    }
    char buf[200];
    int present = 0;
    for (auto& kv : rules) if (diagHeld.count(kv.first)) ++present;
    std::snprintf(buf, sizeof(buf), "Demo playing - %d item(s) changed  (players with skins set: %d, in this demo: %d)", changed,
                  (int)rules.size(), present);
    lastStatus = buf;
    {   // every 10 s, while skins are set but nothing changes: say why in the log
        static ULONGLONG diagAt = 0;
        static int diagLogs = 0;
        if (now >= diagAt && !rules.empty() && changed == 0 && diagLogs < 30) {
            diagAt = now + 10000;
            ++diagLogs;
            std::string held;
            for (auto& kv : diagHeld) held += " " + std::to_string(kv.first) + " holds defs" + kv.second + ";";
            std::string set;
            for (auto& kv : rules) {
                set += " " + std::to_string(kv.first) + " guns";
                for (auto& g : kv.second.guns) set += " " + std::to_string(g.first);
                if (kv.second.knife.def) set += " knife " + std::to_string(kv.second.knife.def);
                set += ";";
            }
            Log("skins: nothing changed - %d weapons in the demo, %d with an owner, %d belong to players with skins set.%s%s Skins set for:%s",
                diagWeapons, diagOwned, diagRuled, held.empty() ? "" : " They hold:", held.c_str(), set.c_str());
        }
        if (changed) diagAt = 0;
    }
}
}  // namespace Skins

#ifdef NSKINZCS2_TESTS
namespace Skins {
bool ProbeHeldWeaponRuleForTests() {
    constexpr uint64_t original=76561198174527843ull,holder=76561198255963631ull;
    auto* old_vtable=Game::attributeVtable;uintptr_t fake_vtable=0;Game::attributeVtable=&fake_vtable;
    const auto old_listener=presentationListener;presentationListener=nullptr;
    struct Guard {void* table;PresentationListener listener;~Guard(){Game::attributeVtable=table;presentationListener=listener;}} guard{old_vtable,old_listener};
    std::vector<uint8_t> entity(32768);auto* item=entity.data()+O.econ_AttributeManager+O.container_Item;
    At<uint16_t>(item,O.item_DefIndex)=60;
    std::map<uint64_t,PlayerRules> configured;auto& skin=configured[holder].guns[60];skin.paint=548;skin.preserveExtras=true;
    const auto owner=RuleOwnerXuid(original,holder);const auto found=configured.find(owner);
    const GunSkin* rule=found==configured.end() ? nullptr:GunRule(found->second,3,60);
    const uint32_t handle=0x1e456;g_weapons.erase(handle);std::string note;
    ApplyWeapon(entity.data(),item,handle,owner,60,60,rule,note,false,true,true);
    const bool applied=Get<int>(entity.data(),O.econ_FallbackPaintKit)==548;
    g_weapons.erase(handle);
    return applied && RuleOwnerXuid(holder,original)==original && RuleOwnerXuid(original,0)==0;
}
static int presentationTestCalls=0;
static PresentationListener presentationTestObserver=nullptr;
static void PresentationTestListener(const WeaponPresentation& update) {
    if(update.weapon && update.item && update.definition==60 && update.keychain==49) ++presentationTestCalls;
    if(presentationTestObserver)presentationTestObserver(update);
}
bool ProbeWeaponPresentationForTests(PresentationListener observer) {
    auto* old_vtable=Game::attributeVtable;uintptr_t fake_vtable=0;Game::attributeVtable=&fake_vtable;
    const auto old_listener=presentationListener;
    struct Guard {void* table;PresentationListener listener;~Guard(){Game::attributeVtable=table;presentationListener=listener;presentationTestObserver=nullptr;}} guard{old_vtable,old_listener};
    presentationTestObserver=observer;
    std::vector<uint8_t> entity(32768);auto* item=entity.data()+O.econ_AttributeManager+O.container_Item;
    At<uint16_t>(item,O.item_DefIndex)=60;At<int>(entity.data(),O.econ_FallbackPaintKit)=548;
    At<uint32_t>(entity.data(),O.ent_hOwnerEntity)=0x2e321;
    GunSkin rule;rule.writeFinish=false;rule.preserveExtras=true;rule.patches.push_back({299,49,49});
    const uint32_t handle=0x1e123;g_weapons.erase(handle);presentationTestCalls=0;presentationListener=&PresentationTestListener;
    std::string note;ApplyWeapon(entity.data(),item,handle,123,60,60,&rule,note,false,true,true);
    const bool result=presentationTestCalls==1 && Get<int>(entity.data(),O.econ_FallbackPaintKit)==548;
    g_weapons.erase(handle);return result;
}
PatchProbeResult ProbeAttributePatchesForTests(const GunSkin& rule,bool legacy) {
    PatchProbeResult result;
    auto* old_vtable=Game::attributeVtable;
    struct Guard {void* saved;~Guard(){Game::attributeVtable=saved;}} guard{old_vtable};
    uintptr_t fake_vtable=0;Game::attributeVtable=&fake_vtable;
    std::vector<uint8_t> item(4096),attributes[2]={std::vector<uint8_t>(9*O.attrSize),std::vector<uint8_t>(9*O.attrSize)};
    const uint16_t ids[]={6,7,8,113,121,299,300,301,302};
    uint32_t values[]={0,0,0,76,500,17,0,0,0};
    const float floats[]={661.f,123.f,.123f,0,0,0,1.f,2.f,3.f};
    for(int i=0;i<3;++i) std::memcpy(&values[i],&floats[i],4);
    for(int i=6;i<9;++i) std::memcpy(&values[i],&floats[i],4);
    for(int li=0;li<2;++li) {
        auto* vec=item.data()+(li==0 ? O.item_AttributeList : O.item_NetworkedDynamicAttributes)+O.attrlist_Attributes;
        At<int>(vec,0)=9;At<uint8_t*>(vec,8)=attributes[li].data();At<int>(vec,0x10)=9;
        for(int i=0;i<9;++i) {auto* a=attributes[li].data()+i*O.attrSize;At<void*>(a,0)=Game::attributeVtable;At<uint16_t>(a,O.attr_DefIndex)=ids[i];At<uint32_t>(a,O.attr_Value)=values[i];At<uint32_t>(a,O.attr_InitialValue)=values[i];}
    }
    At<uint16_t>(item.data(),O.item_DefIndex)=7;At<int32_t>(item.data(),O.item_EntityQuality)=12;
    std::strcpy(reinterpret_cast<char*>(item.data()+O.item_CustomName),"source_tag");
    Undo undo;SaveItem(undo,item.data());
    WriteItem(item.data(),undo,123,7,rule,true,legacy);
    WriteItem(item.data(),undo,123,7,rule,true,legacy); // Later passes must not snapshot their own added values as source.
    for(int li=0;li<2;++li) {
        AttrVec v;if(!OpenList(item.data(),li==0 ? O.item_AttributeList : O.item_NetworkedDynamicAttributes,v)) return result;
        auto& rows=li==0 ? result.primary : result.networked;
        for(int i=0;i<*v.count;++i) {auto* a=*v.data+i*O.attrSize;rows[Get<uint16_t>(a,O.attr_DefIndex)]=Get<uint32_t>(a,O.attr_Value);}
        if(li==0) result.grew=*v.count>9;
    }
    result.preservedFinish=!rule.writeFinish && result.primary[6]==values[0] && result.primary[7]==values[1] && result.primary[8]==values[2] &&
        Get<int32_t>(item.data(),O.item_EntityQuality)==12 && std::strcmp(reinterpret_cast<char*>(item.data()+O.item_CustomName),"source_tag")==0;
    auto removed=rule;removed.patches.clear();
    WriteItem(item.data(),undo,123,7,removed,true,legacy);
    result.removedOverride=true;
    for(int li=0;li<2;++li) {
        AttrVec v;if(!OpenList(item.data(),li==0 ? O.item_AttributeList : O.item_NetworkedDynamicAttributes,v) || *v.count!=9) {result.removedOverride=false;continue;}
        for(int i=0;i<9;++i) {auto* a=*v.data+i*O.attrSize;result.removedOverride &= Get<uint16_t>(a,O.attr_DefIndex)==ids[i] && Get<uint32_t>(a,O.attr_Value)==values[i];}
    }
    RestoreItem(undo,item.data());result.restored=true;
    for(int li=0;li<2;++li) {
        AttrVec v;if(!OpenList(item.data(),li==0 ? O.item_AttributeList : O.item_NetworkedDynamicAttributes,v) || *v.count!=9) {result.restored=false;continue;}
        for(int i=0;i<9;++i) {auto* a=*v.data+i*O.attrSize;result.restored &= Get<uint16_t>(a,O.attr_DefIndex)==ids[i] && Get<uint32_t>(a,O.attr_Value)==values[i];}
    }
    return result;
}
bool ProbeDefaultGloveItemForTests(uint16_t source,uint16_t target,int paint) {
    auto* old_vtable=Game::attributeVtable;uintptr_t fake_vtable=0;Game::attributeVtable=&fake_vtable;
    struct Guard {void* saved;~Guard(){Game::attributeVtable=saved;}} guard{old_vtable};
    std::vector<uint8_t> pawn(16384);auto* item=pawn.data()+O.pawn_EconGloves;
    At<uint16_t>(item,O.item_DefIndex)=source;
    Undo undo;SaveItem(undo,item);GunSkin skin;skin.preserveExtras=true;skin.paint=paint;
    WriteItem(item,undo,123,target,skin,false);RefreshGloves(pawn.data());
    AttrVec v;if(!OpenList(item,O.item_AttributeList,v)) return false;
    bool found=false;for(int i=0;i<*v.count;++i) {auto* a=*v.data+i*O.attrSize;if(Get<uint16_t>(a,O.attr_DefIndex)==6) found=Get<float>(a,O.attr_Value)==static_cast<float>(paint);}
    return found && Get<uint16_t>(item,O.item_DefIndex)==target && Get<uint8_t>(item,O.item_Initialized) &&
        Get<uint8_t>(pawn.data(),O.pawn_NeedToReApplyGloves) && Get<uint8_t>(pawn.data(),O.pawn_EconGlovesChanged)==1;
}
bool ProbeModelAdmissionForTests(int dv,int gv,bool model_in_demo,bool known_bad) {
    const int old_current=demoCurrent,old_demo=g_lastDemoVer,old_game=g_lastGameVer;
    const auto old_bad=g_animBad;
    demoCurrent=dv==gv ? 1 : 0;g_lastDemoVer=dv;g_lastGameVer=gv;
    if(known_bad) g_animBad.insert(dv);
    const bool admitted=model_in_demo || LocalAnimAllowed();
    demoCurrent=old_current;g_lastDemoVer=old_demo;g_lastGameVer=old_game;g_animBad=old_bad;
    return admitted;
}
void ProbeLoadAnimationHistoryForTests(const std::string& folder) { ReadAnimationHistory(folder); }
void ProbeResetAnimationHistoryForTests() { g_animBad.clear();g_animOk.clear(); }
bool ProbePreserveExtrasForTests() {
    std::vector<uint8_t> item(4096);
    std::vector<uint8_t> attrs[2] = {std::vector<uint8_t>(5 * O.attrSize), std::vector<uint8_t>(5 * O.attrSize)};
    const uint16_t defs[] = {6,7,8,113,299};
    for (int li=0;li<2;++li) {
        auto* vec=item.data()+(li==0 ? O.item_AttributeList : O.item_NetworkedDynamicAttributes)+O.attrlist_Attributes;
        At<int>(vec,0)=5;At<uint8_t*>(vec,8)=attrs[li].data();At<int>(vec,0x10)=5;At<int>(vec,0x14)=0;
        for (int i=0;i<5;++i) {
            auto* a=attrs[li].data()+i*O.attrSize;
            At<uint16_t>(a,O.attr_DefIndex)=defs[i];At<float>(a,O.attr_Value)=static_cast<float>(100+i);
            At<float>(a,O.attr_InitialValue)=static_cast<float>(100+i);
        }
    }
    Undo undo;GunSkin skin;skin.preserveExtras=true;skin.paint=415;skin.seed=3;skin.wear=.02f;
    WriteItem(item.data(),undo,123,515,skin,false);
    for (int li=0;li<2;++li) {
        auto* vec=item.data()+(li==0 ? O.item_AttributeList : O.item_NetworkedDynamicAttributes)+O.attrlist_Attributes;
        if(Get<int>(vec,0)!=5) return false;
        for(int i=3;i<5;++i) {
            auto* a=attrs[li].data()+i*O.attrSize;
            if(Get<uint16_t>(a,O.attr_DefIndex)!=defs[i] || Get<float>(a,O.attr_Value)!=100+i || Get<float>(a,O.attr_InitialValue)!=100+i) return false;
        }
        if(Get<float>(attrs[li].data(),O.attr_Value)!=415.f) return false;
    }
    return Get<uint16_t>(item.data(),O.item_DefIndex)==515;
}
}
#endif
} // namespace nskinzcs2::live_port
