/**
 * Marvel Future Fight (MFF) v11.6.0 - Native Hooks
 * Compatible with Dobby or And64InlineHook.
 * Uses IL2CPP dynamic symbol resolution (no hardcoded offsets).
 *
 * Build: ARM64 only (arm64-v8a). For Magisk Zygisk module → zygisk/arm64-v8a.so
 *
 * Method names (icall / class::method) are placeholders. Update them using
 * Il2CppDumper or Zygisk-Il2CppDumper output for MFF v11.6.0 (script.json or dump).
 */

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>
#include <android/log.h>
#include <sys/system_properties.h>

/* ========== Feature config (tune for MFF v11.6.0) ========== */
#define MFF_DMG_MULTIPLIER        100.0f   /* DMG x100: damage dealt multiplier */
#define MFF_DEFENSE_FACTOR        0.01f    /* Defense: incoming damage scale (0.01 = 1%) */
#define MFF_SKILL_CD_VALUE        0.0f     /* No Skill CD: cooldown return value */
#define MFF_GOD_MODE_IS_DEAD      false    /* God Mode: IsDead() return value */
#define MFF_DMG_CLAMP_MAX         999999.0f
#define MFF_DMG_CLAMP_MIN         0.0f

#define LOG_TAG "MFF11.6"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* ========== Hook framework: set to 1 to use Dobby, 0 for And64InlineHook ========== */
#define USE_DOBBY 1

#if USE_DOBBY
extern "C" int DobbyHook(void *address, void *replace_func, void **origin_func);
#else
extern "C" int A64HookFunction(void *target, void *replace, void **origin);
#endif

/* ========== IL2CPP types (minimal for resolution) ========== */
struct Il2CppDomain;
struct Il2CppAssembly;
struct Il2CppImage;
struct Il2CppClass;
struct MethodInfo {
    void *methodPointer;
    void *invoker_method;
    const char *name;
    Il2CppClass *klass;
    // ... other fields
};

typedef Il2CppDomain *(*il2cpp_domain_get_t)();
typedef Il2CppAssembly **(*il2cpp_domain_get_assemblies_t)(Il2CppDomain *, size_t *);
typedef Il2CppImage *(*il2cpp_assembly_get_image_t)(Il2CppAssembly *);
typedef Il2CppClass *(*il2cpp_class_from_name_t)(Il2CppImage *, const char *, const char *);
typedef MethodInfo *(*il2cpp_class_get_method_from_name_t)(Il2CppClass *, const char *, int);
typedef void *(*il2cpp_resolve_icall_t)(const char *);

static il2cpp_domain_get_t il2cpp_domain_get;
static il2cpp_domain_get_assemblies_t il2cpp_domain_get_assemblies;
static il2cpp_assembly_get_image_t il2cpp_assembly_get_image;
static il2cpp_class_from_name_t il2cpp_class_from_name;
static il2cpp_class_get_method_from_name_t il2cpp_class_get_method_from_name;
static il2cpp_resolve_icall_t il2cpp_resolve_icall;

static void *libil2cpp_handle = nullptr;
static volatile bool g_hooks_installed = false;
static pthread_mutex_t g_resolve_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ========== Safety guard ========== */
static const int SAFETY_DELAY_SECONDS = 4;   // Delay after libil2cpp load before hooking
static const int MAX_RESOLVE_RETRIES = 20;   // Retry resolution over 20 seconds
static const int RESOLVE_RETRY_MS = 1000;

static bool is_target_process() {
    char cmdline[256] = {0};
    FILE *f = fopen("/proc/self/cmdline", "r");
    if (!f) return false;
    if (!fgets(cmdline, sizeof(cmdline), f)) { fclose(f); return false; }
    fclose(f);
    return strstr(cmdline, "mherosgb") != nullptr;
}

static void *resolve_icall(const char *name) {
    if (!il2cpp_resolve_icall) return nullptr;
    void *addr = il2cpp_resolve_icall(name);
    if (addr) LOGI("icall %s -> %p", name, addr);
    return addr;
}

/* Try multiple icall names; return first non-null. */
static void *resolve_icall_first(const char **names, int count) {
    for (int i = 0; i < count; i++) {
        void *p = resolve_icall(names[i]);
        if (p) return p;
    }
    return nullptr;
}

static void *resolve_method(const char *image_name, const char *namesp, const char *klass, const char *method, int param_count) {
    if (!libil2cpp_handle || !il2cpp_domain_get || !il2cpp_domain_get_assemblies ||
        !il2cpp_assembly_get_image || !il2cpp_class_from_name || !il2cpp_class_get_method_from_name)
        return nullptr;

    Il2CppDomain *domain = il2cpp_domain_get();
    if (!domain) return nullptr;

    size_t size = 0;
    Il2CppAssembly **assemblies = il2cpp_domain_get_assemblies(domain, &size);
    if (!assemblies || size == 0) return nullptr;

    for (size_t i = 0; i < size; i++) {
        Il2CppImage *img = il2cpp_assembly_get_image(assemblies[i]);
        if (!img) continue;
        (void)image_name;

        Il2CppClass *cls = il2cpp_class_from_name(img, namesp, klass);
        if (!cls) continue;

        MethodInfo *method_info = il2cpp_class_get_method_from_name(cls, method, param_count);
        if (!method_info || !method_info->methodPointer) continue;

        LOGI("method %s.%s::%s -> %p", namesp, klass, method, method_info->methodPointer);
        return method_info->methodPointer;
    }
    return nullptr;
}

/* ========== 1. DMG MULTIPLIER (x100) ========== */
/* Hook damage-dealt path: multiply outgoing damage by MFF_DMG_MULTIPLIER. */
/* Signature: adjust to match MFF v11.6.0 (e.g. (ctx, targetId, amount) or (attacker, target, amount)). */
using DamageDealt_fn = void (*)(void *ctx, int targetId, float amount);
static DamageDealt_fn orig_DamageDealt = nullptr;
static void hook_DamageDealt(void *ctx, int targetId, float amount) {
    if (!orig_DamageDealt) return;
    float dmg = amount * MFF_DMG_MULTIPLIER;
    if (std::isnan(dmg) || dmg < MFF_DMG_CLAMP_MIN) dmg = MFF_DMG_CLAMP_MIN;
    if (dmg > MFF_DMG_CLAMP_MAX) dmg = MFF_DMG_CLAMP_MAX;
    orig_DamageDealt(ctx, targetId, dmg);
}

/* Alternate signature: (void* attacker, void* target, float amount) - uncomment and hook if game uses this */
using DamageDealt_Obj_fn = void (*)(void *attacker, void *target, float amount);
static DamageDealt_Obj_fn orig_DamageDealt_Obj = nullptr;
static void hook_DamageDealt_Obj(void *attacker, void *target, float amount) {
    if (!orig_DamageDealt_Obj) return;
    float dmg = amount * MFF_DMG_MULTIPLIER;
    if (std::isnan(dmg) || dmg < MFF_DMG_CLAMP_MIN) dmg = MFF_DMG_CLAMP_MIN;
    if (dmg > MFF_DMG_CLAMP_MAX) dmg = MFF_DMG_CLAMP_MAX;
    orig_DamageDealt_Obj(attacker, target, dmg);
}

/* ========== 2. DEFENSE MULTIPLIER ========== */
/* Hook damage-received path: scale incoming damage by MFF_DEFENSE_FACTOR (e.g. 0.01 = 1%). */
using DamageReceived_fn = void (*)(void *ctx, float amount);
static DamageReceived_fn orig_DamageReceived = nullptr;
static void hook_DamageReceived(void *ctx, float amount) {
    if (!orig_DamageReceived) return;
    float reduced = amount * MFF_DEFENSE_FACTOR;
    if (std::isnan(reduced) || reduced < 0.0f) reduced = 0.0f;
    orig_DamageReceived(ctx, reduced);
}

/* TakeDamage(entity, damage): primary defense hook when game uses TakeDamage for incoming damage. */
using TakeDamage_fn = void (*)(void *entity, float damage);
static TakeDamage_fn orig_TakeDamage = nullptr;
static void hook_TakeDamage(void *entity, float damage) {
    if (!orig_TakeDamage) return;
    float reduced = damage * MFF_DEFENSE_FACTOR;
    if (std::isnan(reduced) || reduced < 0.0f) reduced = 0.0f;
    orig_TakeDamage(entity, reduced);
}

/* TakeDamage int variant: some games use int damage. */
using TakeDamageInt_fn = void (*)(void *entity, int32_t damage);
static TakeDamageInt_fn orig_TakeDamageInt = nullptr;
static void hook_TakeDamageInt(void *entity, int32_t damage) {
    if (!orig_TakeDamageInt) return;
    int32_t reduced = (int32_t)((float)damage * MFF_DEFENSE_FACTOR);
    if (reduced < 0) reduced = 0;
    orig_TakeDamageInt(entity, reduced);
}

/* ========== 3. NO SKILL COOLDOWN (CD) ========== */
/* Hook cooldown getter: return MFF_SKILL_CD_VALUE (0 = always ready). */
using GetCooldown_fn = float (*)(void *skill);
static GetCooldown_fn orig_GetCooldown = nullptr;
static float hook_GetCooldown(void *skill) {
    (void)skill;
    return MFF_SKILL_CD_VALUE;
}

/* Bool variant: IsSkillReady / CanUseSkill -> return true. */
using IsSkillReady_fn = bool (*)(void *skill);
static IsSkillReady_fn orig_IsSkillReady = nullptr;
static bool hook_IsSkillReady(void *skill) {
    (void)skill;
    return true;
}

/* ========== 4. GOD MODE ========== */
/* Hook death check: return MFF_GOD_MODE_IS_DEAD (false) so player is never considered dead. */
using IsDead_fn = bool (*)(void *entity);
static IsDead_fn orig_IsDead = nullptr;
static bool hook_IsDead(void *entity) {
    (void)entity;
    return MFF_GOD_MODE_IS_DEAD;
}

/* get_isDead property getter (same effect). */
using GetIsDead_fn = bool (*)(void *entity);
static GetIsDead_fn orig_GetIsDead = nullptr;
static bool hook_GetIsDead(void *entity) {
    (void)entity;
    return MFF_GOD_MODE_IS_DEAD;
}

/* ========== Install hooks (only after resolution succeeds) ========== */
static bool try_resolve_and_hook() {
    pthread_mutex_lock(&g_resolve_mutex);
    if (g_hooks_installed) {
        pthread_mutex_unlock(&g_resolve_mutex);
        return true;
    }

    libil2cpp_handle = dlopen("libil2cpp.so", RTLD_NOW);
    if (!libil2cpp_handle) {
        pthread_mutex_unlock(&g_resolve_mutex);
        return false;
    }

#define DLSYM_IL2CPP(name) do { \
        auto p = (void*)dlsym(libil2cpp_handle, #name); \
        if (!p) { LOGE("dlsym %s failed", #name); pthread_mutex_unlock(&g_resolve_mutex); return false; } \
        name = (name##_t)p; \
    } while(0)

    DLSYM_IL2CPP(il2cpp_domain_get);
    il2cpp_domain_get_assemblies = (il2cpp_domain_get_assemblies_t)dlsym(libil2cpp_handle, "il2cpp_domain_get_assemblies");
    il2cpp_assembly_get_image = (il2cpp_assembly_get_image_t)dlsym(libil2cpp_handle, "il2cpp_assembly_get_image");
    DLSYM_IL2CPP(il2cpp_class_from_name);
    DLSYM_IL2CPP(il2cpp_class_get_method_from_name);
    DLSYM_IL2CPP(il2cpp_resolve_icall);

    if (!il2cpp_domain_get_assemblies) il2cpp_domain_get_assemblies = (il2cpp_domain_get_assemblies_t)dlsym(libil2cpp_handle, "il2cpp_domain_get_assemblies");
    if (!il2cpp_assembly_get_image) il2cpp_assembly_get_image = (il2cpp_assembly_get_image_t)dlsym(libil2cpp_handle, "il2cpp_assembly_get_image");

    /* ---------- 1. DMG MULTIPLIER: resolve damage-dealt function ---------- */
    static const char *dmg_dealt_names[] = {
        "Battle.Character::ApplyDamage",
        "Battle.Character::DealDamage",
        "Battle.BattleManager::ApplyDamage",
        "DamageUtil::ApplyDamage",
    };
    void *addr_damage_dealt = resolve_icall_first(dmg_dealt_names, sizeof(dmg_dealt_names) / sizeof(dmg_dealt_names[0]));
    if (!addr_damage_dealt) addr_damage_dealt = resolve_method(nullptr, "Battle", "Character", "ApplyDamage", 2);
    if (!addr_damage_dealt) addr_damage_dealt = resolve_method(nullptr, "Battle", "BattleManager", "ApplyDamage", 2);

    /* ---------- 2. DEFENSE: resolve damage-received / TakeDamage ---------- */
    static const char *defense_names[] = {
        "Battle.Character::TakeDamage",
        "Battle.Character::OnDamage",
        "Battle.Character::ApplyDamageReceived",
    };
    void *addr_damage_recv = resolve_icall_first(defense_names, sizeof(defense_names) / sizeof(defense_names[0]));
    if (!addr_damage_recv) addr_damage_recv = resolve_method(nullptr, "Battle", "Character", "TakeDamage", 1);
    if (!addr_damage_recv) addr_damage_recv = resolve_method(nullptr, "Battle", "Character", "OnDamage", 1);

    /* ---------- 3. NO SKILL CD: resolve cooldown / ready check ---------- */
    static const char *cd_names[] = {
        "Battle.Skill::GetRemainCooldown",
        "Battle.Skill::get_remainCooldown",
        "Battle.SkillController::GetRemainCooldown",
    };
    void *addr_cooldown = resolve_icall_first(cd_names, sizeof(cd_names) / sizeof(cd_names[0]));
    if (!addr_cooldown) addr_cooldown = resolve_method(nullptr, "Battle", "Skill", "GetRemainCooldown", 0);
    if (!addr_cooldown) addr_cooldown = resolve_method(nullptr, "Battle", "Skill", "get_remainCooldown", 0);
    void *addr_skill_ready = resolve_icall("Battle.Skill::get_isReady");
    if (!addr_skill_ready) addr_skill_ready = resolve_method(nullptr, "Battle", "Skill", "get_isReady", 0);

    /* ---------- 4. GOD MODE: resolve death check ---------- */
    static const char *god_names[] = {
        "Battle.Character::get_isDead",
        "Battle.Character::IsDead",
        "Battle.Character::CheckDeath",
    };
    void *addr_is_dead = resolve_icall_first(god_names, sizeof(god_names) / sizeof(god_names[0]));
    if (!addr_is_dead) addr_is_dead = resolve_method(nullptr, "Battle", "Character", "get_isDead", 0);
    if (!addr_is_dead) addr_is_dead = resolve_method(nullptr, "Battle", "Character", "IsDead", 0);

    int hooked = 0;
#if USE_DOBBY
    if (addr_damage_dealt && DobbyHook(addr_damage_dealt, (void*)hook_DamageDealt, (void**)&orig_DamageDealt) == 0) hooked++;
    if (addr_damage_recv  && DobbyHook(addr_damage_recv,  (void*)hook_TakeDamage,     (void**)&orig_TakeDamage) == 0) hooked++;
    if (addr_cooldown     && DobbyHook(addr_cooldown,     (void*)hook_GetCooldown,    (void**)&orig_GetCooldown) == 0) hooked++;
    if (addr_skill_ready  && DobbyHook(addr_skill_ready,  (void*)hook_IsSkillReady,   (void**)&orig_IsSkillReady) == 0) hooked++;
    if (addr_is_dead      && DobbyHook(addr_is_dead,      (void*)hook_IsDead,         (void**)&orig_IsDead) == 0) hooked++;
#else
    if (addr_damage_dealt && A64HookFunction(addr_damage_dealt, (void*)hook_DamageDealt, (void**)&orig_DamageDealt) == 0) hooked++;
    if (addr_damage_recv  && A64HookFunction(addr_damage_recv,  (void*)hook_TakeDamage,     (void**)&orig_TakeDamage) == 0) hooked++;
    if (addr_cooldown     && A64HookFunction(addr_cooldown,     (void*)hook_GetCooldown,    (void**)&orig_GetCooldown) == 0) hooked++;
    if (addr_skill_ready  && A64HookFunction(addr_skill_ready,  (void*)hook_IsSkillReady,   (void**)&orig_IsSkillReady) == 0) hooked++;
    if (addr_is_dead      && A64HookFunction(addr_is_dead,      (void*)hook_IsDead,         (void**)&orig_IsDead) == 0) hooked++;
#endif

    if (hooked > 0) {
        g_hooks_installed = true;
        LOGI("MFF 11.6.0: DMG x%.0f, Defense x%.2f, NoCD, GodMode | hooks=%d",
             (double)MFF_DMG_MULTIPLIER, (double)MFF_DEFENSE_FACTOR, hooked);
    } else {
        LOGE("No hooks installed - check icall/method names for MFF v11.6.0");
    }

    pthread_mutex_unlock(&g_resolve_mutex);
    return g_hooks_installed;
}

static void *init_thread(void *) {
    if (!is_target_process()) {
        LOGI("Not MFF process, skip");
        return nullptr;
    }
    LOGI("MFF process detected, safety delay %d sec", SAFETY_DELAY_SECONDS);
    sleep(SAFETY_DELAY_SECONDS);

    for (int i = 0; i < MAX_RESOLVE_RETRIES && !g_hooks_installed; i++) {
        if (try_resolve_and_hook())
            break;
        usleep(RESOLVE_RETRY_MS * 1000);
    }
    return nullptr;
}

/* ========== Entry (constructor or JNI_OnLoad if you load from Java) ========== */
__attribute__((constructor))
static void mff_hook_init() {
    pthread_t t;
    pthread_create(&t, nullptr, init_thread, nullptr);
    pthread_detach(t);
}
