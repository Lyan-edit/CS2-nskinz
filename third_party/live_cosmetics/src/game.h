// Copyright (c) 2026 janglijoshi. All rights reserved.
// Third-party component. Original copyright and terms: ../LICENSE and ../NOTICE.md.
#pragma once
#include "util.h"
#include <string>
#include <vector>

// Local authorized integration: isolate upstream symbols from nSkinz.
namespace nskinzcs2::live_port {

// Everything nSkinzLive reads from CS2. Field offsets come from the game's own schema system at start-up, so they
// follow game updates by themselves; the numbers in game.cpp are only a fallback (CS2 build 14196, 2026-11-09).
// Virtual-function indexes cannot be read that way: they are listed in Game::Indexes and can be overridden in
// nSkinzLive.cfg if a CS2 update moves them.

struct Offsets {
    ptrdiff_t identity_designerName, identitySize;
    ptrdiff_t ent_pEntity, ent_iTeamNum, ent_hOwnerEntity;
    ptrdiff_t ctrl_steamID, ctrl_iszPlayerName;
    ptrdiff_t pawn_hController;
    ptrdiff_t econ_AttributeManager, econ_FallbackPaintKit, econ_FallbackSeed, econ_FallbackWear, econ_FallbackStatTrak;
    ptrdiff_t econ_OrigXuidLow, econ_OrigXuidHigh;
    ptrdiff_t container_Item;
    ptrdiff_t item_DefIndex, item_EntityQuality, item_ItemIDHigh, item_ItemIDLow, item_AccountID, item_CustomName;
    ptrdiff_t item_AttributeList, item_NetworkedDynamicAttributes;
    ptrdiff_t attrlist_Attributes;
    ptrdiff_t attr_DefIndex, attr_Value, attr_InitialValue, attrSize;
    ptrdiff_t weapon_CustomEconReloadEventId;
    ptrdiff_t item_Initialized;
    ptrdiff_t ctrl_hPlayerPawn;
    ptrdiff_t pawn_EconGloves, pawn_NeedToReApplyGloves, pawn_EconGlovesChanged;
    ptrdiff_t ent_nSubclassID, ent_pGameSceneNode;
    ptrdiff_t skeleton_modelState, modelState_ModelName;
    ptrdiff_t ent_VData;   // read from the subclass-change function's code, not the schema
    ptrdiff_t modelState_MeshGroupMask, item_ItemID;
    ptrdiff_t modelState_BodyGroupChoices;
};
extern Offsets O;

struct Indexes {
    int clientFrameStageNotify = 36;   // Source2Client002
    int engineIsPlayingDemo = 42;      // Source2EngineToClient001
    int engineExecuteClientCmd = 51;
    int engineGetDemoFile = 69;
    int demoGetDemoTick = 3;           // IDemoFile
    int entitySystemOffset = 0x58;     // GameResourceServiceClientV001 -> CGameEntitySystem*
};

namespace Game {
extern Indexes I;
extern void* client;          // Source2Client002
extern void* engine;          // Source2EngineToClient001
extern void* resourceService; // GameResourceServiceClientV001

bool Init();                  // interfaces + schema; logs every failure
bool IsPlayingDemo();
int DemoTick();               // -1 if unknown
void ExecuteCommand(const char* cmd);

// Entities
void* EntitySystem();
void* IdentityAt(int index);                 // CEntityIdentity* or nullptr
void* EntityAt(int index);                   // entity instance or nullptr
void* FromHandle(uint32_t handle);
const char* DesignerName(void* entity);
uint32_t HandleOf(void* entity);             // the entity's own handle (CEntityIdentity + 0x10)
int HighestChunk();                          // number of 512-entity chunks to walk
uint8_t* Chunk(void* entitySystem, int c);   // a checked chunk of 512 identities, or nullptr

// Engine memory, so the game can later grow or free lists we extended.
void* Alloc(size_t size);
// The function table of a C++ class in client.dll, found through its run-time type information (survives updates).
void* FindVtable(const char* module, const char* mangledName);
extern void* attributeVtable;                // CEconItemAttribute

// Functions inside client.dll, found at start-up (each may be null if a CS2 update moved it; see the log).
int PostDataUpdateStage();                   // FrameStageNotify stage that runs entities' PostDataUpdate, -1 if unknown
bool CanSwapModels();                        // SetModel + subclass change both found
bool CanRegenerate();                        // regenerate_weapon_skins handler found
void SetModel(void* entity, const char* path);
bool CanSetModel();
bool DemoFileVerified();     // GetDemoFile's number was read from the game's code (not the built-in one)
bool CanCheckDemoVersion();
int InterfaceFallbacks();    // interfaces found under another version number
int SchemaMisses();
int ClientVersion();         // CS2 build number, 0 if unknown
void ChangeSubclass(void* entity, uint32_t token);   // the game's own "subclass changed" handling
void RegenerateWeaponSkins();                // what the regenerate_weapon_skins command does, without sv_cheats
int RegenerateWeapon(void* weapon, bool force); // one weapon: 1 done, 0 its materials aren't made yet, -1 not available
bool GloveGates(void* item, bool& skip, const char*& model, void*& paint);   // the game's own glove checks
bool UseLocalAnimData();                     // make demo playback use local animation data; false if not possible
void RestoreAnimData();                      // put that setting back as it was (no-op if never changed)
bool AnimDataSwitched();                     // UseLocalAnimData changed it and it isn't back yet
// Demo version vs game version, as CS2 compares them for TrueView. 1: same, 0: older/other version, -1: unknown.
int DemoIsCurrent(int* demoVersion = nullptr, int* gameVersion = nullptr);
bool PrecacheModel(const char* path);        // load a model the demo didn't (as the game does for preview gloves)
void* GloveEntity(void* pawn);               // the glove model entity the game made for this player, if any
bool CanSeeGloveEntity();
uint64_t MeshGroupMask(void* entity);        // which meshes of the model are shown (bit 0: current model, bit 1: legacy)
bool SetMeshGroupMask(void* entity, uint64_t mask);   // the game's own change handler for it
void MaterialCounts(void* weapon, int& a, int& b);    // the weapon's custom-material counters (-1 if unknown)

// First person: since CS2's 2025 view-model rework, the gun seen in first person is a separate client-side model
// (C_CS2HudModelWeapon, "cs2_hudmodel_weapon") that each weapon makes for itself. It has its own model, mesh choice
// and skin material, made when it is created - so a change to the weapon must be passed on to it, or it must be
// made again (the game itself re-makes it when a knife's model changes).
void* HudModel(void* weapon);                // the weapon's first-person model, or nullptr
bool CanFixHudModels();
void RemoveEntity(void* entity);             // the game's own client-side remove (the weapon then makes a new one)
void* FromHandleExact(uint32_t handle);      // like FromHandle, but the whole handle must match

// Body groups (the game's own functions; choices are named, e.g. "first_or_third_person" = "firstperson_default").
bool CanSetBodyGroups();
int BodyGroup(void* entity, const char* group);                       // index, -1 if the model has no such group
bool BodyGroupIs(void* entity, const char* group, const char* choice);
bool SetBodyGroup(void* entity, const char* group, const char* choice);
bool SetBodyGroupIndex(void* entity, const char* group, int choice);
// The body group choice as the network stores it (CModelState::m_nBodyGroupChoices). The demo's packets write it
// (and CS2 rebuilds the mesh from it), so a request alone gets undone; this sets the stored value itself.
// Returns the previous choice, or -1 if not possible.
int SetNetworkedBodyGroup(void* entity, const char* group, int choice);
const char* ModelName(void* entity);         // current model path, or nullptr

// ---- for the camera (calc) tools
bool SchemaField(const char* cls, const char* field, ptrdiff_t& out);   // quiet schema lookup
void* SceneNode(void* entity);               // CGameSceneNode*, or nullptr
float CurTime();                             // game time (demo time), 0 if unknown
float FrameTime();
void* CvarSystem();                          // ICvar ("VEngineCvar007")
// Model attachments ("muzzle_flash", "shell_eject", ...) and bones, in world space. quat = x, y, z, w.
bool Attachment(void* entity, const char* name, float pos[3], float quat[4]);
bool Bone(void* entity, int index, float pos[3], float quat[4]);
// The view hook: CS2's CViewRender::SetUpView asks cl_demoviewoverride and then calls CalcDemoViewOverride while
// a demo plays. nSkinzLive rewrites those two calls: value() decides whether to take that branch, apply(viewSetup)
// replaces CalcDemoViewOverride. Works next to HLAE (whose own view code runs just before, at the IsPlayingDemo call).
using ViewValueFn = float (*)();
using ViewApplyFn = void (*)(void* viewSetup);
bool InstallViewHook(ViewValueFn value, ViewApplyFn apply);
float OriginalDemoViewOverrideValue();       // what cl_demoviewoverride says
void OriginalCalcDemoViewOverride(void* viewSetup);
extern ptrdiff_t viewOriginOff, viewAnglesOff, viewFovOff;   // in CViewSetup
uint32_t StringToken(const char* s);         // CUtlStringToken (MurmurHash2, lower case)
}
} // namespace nskinzcs2::live_port
