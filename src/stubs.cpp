// Comix Zone XBLA - Missing kernel stubs
// Xbox 360 APIs not yet implemented in the ReXGlue SDK.

#include "generated/comixzone_config.h"
#include "generated/comixzone_init.h"
#include <rex/runtime/guest/context.h>
#include <rex/runtime/guest/memory.h>
#include <rex/kernel/kernel_state.h>
#include <rex/logging.h>
#include <cstring>

using namespace rex::runtime::guest;

// Simple stub macro that returns a value
#define CZ_STUB_RETURN(name, val) \
    extern "C" PPC_FUNC(name) { (void)base; ctx.r3.u64 = (val); }

#define CZ_STUB(name) CZ_STUB_RETURN(name, 0)

// Content license — override the weak wrapper sub_8212D018 to return full license.
// The SegaVintage framework checks license at startup; without this, the game
// stays stuck on the Backbone Entertainment logo (trial mode). The generated code
// declares sub_8212D018 as PPC_WEAK_FUNC, so our strong definition takes precedence.
// Calling convention: r3 = output pointer for mask, r4 = overlapped (ignored)
// Returns 0 (success) in r3.
extern "C" PPC_FUNC(sub_8212D018) {
    uint32_t mask_ptr = ctx.r3.u32;
    if (mask_ptr)
        PPC_STORE_U32(mask_ptr, 0xFFFFFFFF);  // full license
    ctx.r3.u32 = 0;  // ERROR_SUCCESS
    REXLOG_INFO("[BOOT] sub_8212D018: content license override — full license granted");
}

// User sign-in stubs — critical for startup flow.
// The SegaVintage engine checks sign-in state at the logo screen.
// Without these, the game hangs waiting for a user to sign in.
extern "C" PPC_FUNC(__imp__XamUserGetSigninState) {
    (void)base;
    ctx.r3.u64 = 1;  // SignedInLocally
}

extern "C" PPC_FUNC(__imp__XamUserGetName) {
    // r3 = user index, r4 = buffer, r5 = buffer size
    uint32_t buf = ctx.r4.u32;
    const char* name = "Player1";
    memcpy(base + buf, name, strlen(name) + 1);
    ctx.r3.u32 = 0;
}

extern "C" PPC_FUNC(__imp__XamUserCheckPrivilege) {
    // r3 = user, r4 = privilege, r5 = result*
    uint32_t result_ptr = ctx.r5.u32;
    if (result_ptr)
        PPC_STORE_U32(result_ptr, 1);  // has privilege
    ctx.r3.u32 = 0;
}

CZ_STUB(__imp__XamShowSigninUI)
CZ_STUB(__imp__XamUserWriteProfileSettings)

// XAM UI stubs (non-essential UI dialogs)
CZ_STUB(__imp__XamShowGamerCardUIForXUID)
CZ_STUB(__imp__XamShowAchievementsUI)
CZ_STUB(__imp__XamShowMarketplaceUI)
CZ_STUB(__imp__XamShowFriendsUI)
CZ_STUB(__imp__XamShowPlayersUI)
CZ_STUB(__imp__XamShowPlayerReviewUI)

// XAM user/voice stubs
CZ_STUB_RETURN(__imp__XamUserCreateStatsEnumerator, 1)  // fail = no stats
CZ_STUB(__imp__XamVoiceSubmitPacket)

// Networking stubs
CZ_STUB(__imp__NetDll_XNetQosLookup)  // QoS lookup - return 0 (no QoS data)

// ============================================================================
// Storage device selector fix (sub_820ECB90)
//
// The SDK handles XamShowDeviceSelectorUI headlessly (auto-selects HDD,
// completes overlapped after 100ms). The game's state machine works correctly:
// state transitions 0->1->2 over ~500ms. The overlapped completes within 100ms.
//
// THE PROBLEM: When all conditions are met (state=2, overlapped done, UI closed),
// the original code calls vtable[40] -> sub_820ECA40 -> sub_820F8838 which hangs
// inside a complex screen transition function that manipulates multiple scene
// manager objects through vtable calls that block indefinitely.
//
// THE FIX: Let the state machine run its course, then call vtable[40] as the
// original code does. sub_820F8838 is separately overridden (below) to skip
// the specific vtable call that hangs.
//
// KEY INSIGHT: The success path (device_id != 0) calls vtable[40] directly.
// It does NOT set the done flag at obj+2565 — that's only the cancel path.
//
// Static addresses:
//   overlapped: 0x82303A3C
//   device_id:  0x82303A58
// ============================================================================
static constexpr uint32_t STORAGE_OVERLAPPED_ADDR = 0x82303A3C;
static constexpr uint32_t STORAGE_DEVICE_ID_ADDR  = 0x82303A58;

extern "C" PPC_FUNC_IMPL(__imp__sub_820E8308);   // base class update
extern "C" PPC_FUNC_IMPL(__imp__sub_820E8910);   // state getter: returns [obj+20]+4
extern "C" PPC_FUNC_IMPL(__imp__sub_8212EAC0);   // XGetOverlappedResult
extern "C" PPC_FUNC_IMPL(__imp__sub_8212EA98);   // overlapped cleanup

extern "C" PPC_FUNC(sub_820ECB90) {
    uint32_t obj = ctx.r3.u32;

    // 1. Base class update
    ctx.r3.u32 = obj;
    __imp__sub_820E8308(ctx, base);

    // 2. Check pending flag
    uint8_t pending = PPC_LOAD_U8(obj + 2564);
    if (!pending)
        return;

    // 3. Check state == 2
    ctx.r3.u32 = obj;
    __imp__sub_820E8910(ctx, base);
    if (ctx.r3.u32 != 2)
        return;

    // 4. Check overlapped done
    uint32_t internal_low = PPC_LOAD_U32(STORAGE_OVERLAPPED_ADDR);
    if (internal_low == 997)
        return;

    // 5. Skip UI-active check (SDK headless mode)

    REXLOG_INFO("[STORAGE] Device selector completed");

    // 6. Clear pending flag
    PPC_STORE_U8(obj + 2564, 0);

    // 7. XGetOverlappedResult
    uint32_t orig_sp = ctx.r1.u32;
    ctx.r1.u32 = orig_sp - 112;
    PPC_STORE_U32(ctx.r1.u32, orig_sp);
    PPC_STORE_U32(ctx.r1.u32 + 80, 0);
    ctx.r3.u32 = STORAGE_OVERLAPPED_ADDR;
    ctx.r4.u32 = ctx.r1.u32 + 80;
    ctx.r5.u32 = 0;
    __imp__sub_8212EAC0(ctx, base);

    // 8. Cleanup overlapped
    ctx.r3.u32 = STORAGE_OVERLAPPED_ADDR;
    __imp__sub_8212EA98(ctx, base);
    ctx.r1.u32 = orig_sp;

    // 9. Read device_id and branch
    uint32_t device_id = PPC_LOAD_U32(STORAGE_DEVICE_ID_ADDR);

    if (device_id == 0) {
        // Cancel: set done, call vtable[56]
        PPC_STORE_U8(obj + 2565, 1);
        uint32_t vtable_ptr = PPC_LOAD_U32(obj);
        uint32_t method = PPC_LOAD_U32(vtable_ptr + 56);
        ctx.r3.u32 = obj;
        PPC_CALL_INDIRECT_FUNC(method);
    } else {
        // Success: call vtable[40] for screen transition
        uint32_t vtable_ptr = PPC_LOAD_U32(obj);
        uint32_t method = PPC_LOAD_U32(vtable_ptr + 40);
        ctx.r3.u32 = obj;
        ctx.r4.u32 = 0;
        ctx.r5.u32 = PPC_LOAD_U32(obj + 180);
        PPC_CALL_INDIRECT_FUNC(method);
    }
}

// ============================================================================
// Screen transition fix (sub_820F8838)
//
// This is the SegaVintage engine's main screen transition function. It
// manipulates scene managers, UI overlays, and navigation objects through
// vtable calls to perform fade-out/fade-in screen transitions.
//
// ROOT CAUSE OF HANG: Step 7 calls vtable[104](1) on the fade overlay object
// at 0x822EFD08. On Xbox 360 this triggers a blocking fade-to-black animation.
// In the recomp, the fade never completes so the call blocks forever.
//
// FIX: Skip the fade overlay activation. Screens transition instantly without
// a fade effect, but all other state management proceeds normally.
//
// This function is used by ALL screen transitions in the game (not just
// storage device selection), so the fix benefits every screen change.
// ============================================================================

extern "C" PPC_FUNC_IMPL(__imp__sub_8210CB68);
extern "C" PPC_FUNC_IMPL(__imp__sub_820C7680);
extern "C" PPC_FUNC_IMPL(__imp__sub_820C81C8);
extern "C" PPC_FUNC_IMPL(__imp__sub_820C7AC0);
extern "C" PPC_FUNC_IMPL(__imp__sub_820D2890);
extern "C" PPC_FUNC_IMPL(__imp__sub_820C8278);
extern "C" PPC_FUNC_IMPL(__imp__sub_820D2EA0);
extern "C" PPC_FUNC_IMPL(__imp__sub_820D2798);
extern "C" void __savegprlr_26(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __restgprlr_26(PPCContext& __restrict ctx, uint8_t* base);

extern "C" PPC_FUNC(sub_820F8838) {
    PPC_FUNC_PROLOGUE();
    uint32_t ea{};

    // Prologue
    ctx.r12.u64 = ctx.lr;
    ctx.lr = 0x820F8840;
    __savegprlr_26(ctx, base);
    ea = -144 + ctx.r1.u32;
    PPC_STORE_U32(ea, ctx.r1.u32);
    ctx.r1.u32 = ea;

    ctx.r28.s64 = -2110849024;  // 0x822F0000
    ctx.r27.u64 = ctx.r3.u64;  // param1
    ctx.r30.u64 = ctx.r4.u64;  // screen_id

    REXLOG_INFO("[TRANS] Screen transition: param=0x{:08X} screen_id={}",
                ctx.r27.u32, ctx.r30.u32);

    // Step 1: Scene manager vtable[104] — check if transition allowed
    ctx.r3.u64 = PPC_LOAD_U32(ctx.r28.u32 + 5064);
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r3.u32 + 0);
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r11.u32 + 104);
    ctx.ctr.u64 = ctx.r11.u64;
    ctx.lr = 0x820F8864;
    PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);

    ctx.r11.u64 = ctx.r3.u32 & 0xFF;
    ctx.cr0.compare<int32_t>(ctx.r11.s32, 0, ctx.xer);
    if (ctx.cr0.eq) {
        // Step 2: Scene manager vtable[108] — push new screen
        ctx.r3.u64 = PPC_LOAD_U32(ctx.r28.u32 + 5064);
        ctx.r4.u64 = ctx.r30.u64;
        ctx.r11.u64 = PPC_LOAD_U32(ctx.r3.u32 + 0);
        ctx.r11.u64 = PPC_LOAD_U32(ctx.r11.u32 + 108);
        ctx.ctr.u64 = ctx.r11.u64;
        ctx.lr = 0x820F8884;
        PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);
        ctx.r11.u64 = ctx.r3.u32 & 0xFF;
        ctx.cr0.compare<int32_t>(ctx.r11.s32, 0, ctx.xer);
        ctx.r10.s64 = ctx.cr0.eq ? 1 : 0;
    } else {
        ctx.r10.s64 = 0;
    }

    // Success flag + clear transition-active flag
    ctx.r9.s64 = -2110783488;   // 0x82300000
    ctx.r11.s64 = 0;
    ctx.r31.s64 = -2110849024;  // 0x822F0000
    ctx.r29.u64 = ctx.r10.u32 & 0xFF;
    PPC_STORE_U8(ctx.r9.u32 + 15928, ctx.r11.u8);

    // Step 3: UI overlay vtable[80] — hide current
    ctx.r3.u64 = PPC_LOAD_U32(ctx.r31.u32 + 20104);
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r3.u32 + 0);
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r11.u32 + 80);
    ctx.ctr.u64 = ctx.r11.u64;
    ctx.lr = 0x820F88BC;
    PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);

    // Step 4: UI overlay vtable[352](0) — disable animations
    ctx.r3.u64 = PPC_LOAD_U32(ctx.r31.u32 + 20104);
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r3.u32 + 0);
    ctx.r4.s64 = 0;
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r11.u32 + 352);
    ctx.ctr.u64 = ctx.r11.u64;
    ctx.lr = 0x820F88D4;
    PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);

    // Step 5: Scene manager vtable[124](screen_id) — configure transition
    ctx.r3.u64 = PPC_LOAD_U32(ctx.r28.u32 + 5064);
    ctx.r4.u64 = ctx.r30.u64;
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r3.u32 + 0);
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r11.u32 + 124);
    ctx.ctr.u64 = ctx.r11.u64;
    ctx.lr = 0x820F88EC;
    PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);

    // Step 6: Navigation update
    ctx.r26.s64 = -2110849024;
    ctx.r4.u64 = ctx.r27.u64;
    ctx.r3.u64 = PPC_LOAD_U32(ctx.r26.u32 + 5056);
    ctx.lr = 0x820F88FC;
    sub_8210CB68(ctx, base);

    // Step 7: SKIP fade overlay vtable[104](1).
    // On Xbox 360 this triggers a blocking fade-to-black animation driven by
    // GPU interrupts. In the recomp the GPU never fires those interrupts, so
    // the call blocks forever. Skipping it means instant screen transitions
    // (no fade effect) but the game progresses normally.

    // Step 8: Get navigation state
    ctx.r4.u64 = ctx.r30.u64;
    ctx.r3.u64 = PPC_LOAD_U32(ctx.r26.u32 + 5056);
    ctx.lr = 0x820F8924;
    sub_820C7680(ctx, base);

    // Compute masked navigation value
    ctx.r10.u64 = ctx.r3.u64;
    ctx.r3.u64 = PPC_LOAD_U32(ctx.r26.u32 + 5056);
    ctx.r11.u64 = __builtin_rotateleft64(ctx.r10.u32 | (ctx.r10.u64 << 32), 1) & 0x1;
    ctx.r11.s64 = ctx.r11.s64 + -1;
    ctx.r27.u64 = ctx.r11.u64 & ctx.r10.u64;

    // Step 9-10: Navigation state management
    ctx.lr = 0x820F893C;
    sub_820C81C8(ctx, base);

    ctx.r5.u64 = ctx.r30.u64;
    ctx.r3.u64 = PPC_LOAD_U32(ctx.r26.u32 + 5056);
    ctx.r4.u64 = ctx.r27.u64;
    ctx.lr = 0x820F894C;
    sub_820C7AC0(ctx, base);

    // Step 11: UI overlay vtable[360](1) — start transition
    ctx.r3.u64 = PPC_LOAD_U32(ctx.r31.u32 + 20104);
    ctx.r4.s64 = 1;
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r3.u32 + 0);
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r11.u32 + 360);
    ctx.ctr.u64 = ctx.r11.u64;
    ctx.lr = 0x820F8964;
    PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);

    // Step 12: Set up new screen (branch on success flag)
    ctx.r3.u64 = PPC_LOAD_U32(ctx.r31.u32 + 20104);
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r3.u32 + 0);
    if (ctx.r29.u32 == 0) {
        ctx.r11.u64 = PPC_LOAD_U32(ctx.r11.u32 + 36);
        ctx.ctr.u64 = ctx.r11.u64;
        ctx.lr = 0x820F89B4;
        PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);
    } else {
        ctx.r11.u64 = PPC_LOAD_U32(ctx.r11.u32 + 32);
        ctx.r5.u64 = ctx.r30.u64;
        ctx.r4.s64 = 0;
        ctx.ctr.u64 = ctx.r11.u64;
        ctx.lr = 0x820F8988;
        PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);

        ctx.r3.u64 = PPC_LOAD_U32(ctx.r31.u32 + 20104);
        ctx.r5.s64 = 0;
        ctx.r4.u64 = ctx.r30.u64;
        ctx.r11.u64 = PPC_LOAD_U32(ctx.r3.u32 + 0);
        ctx.r11.u64 = PPC_LOAD_U32(ctx.r11.u32 + 40);
        ctx.ctr.u64 = ctx.r11.u64;
        ctx.lr = 0x820F89A4;
        PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);
    }

    // Step 13: UI overlay vtable[76](success_flag) — finalize
    ctx.r3.u64 = PPC_LOAD_U32(ctx.r31.u32 + 20104);
    ctx.r4.u64 = ctx.r29.u64;
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r3.u32 + 0);
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r11.u32 + 76);
    ctx.ctr.u64 = ctx.r11.u64;
    ctx.lr = 0x820F89CC;
    PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);

    // Step 14: Audio transition
    ctx.r31.s64 = -2110849024;
    ctx.r3.u64 = PPC_LOAD_U32(ctx.r31.u32 + 28628);
    ctx.lr = 0x820F89D8;
    sub_820D2890(ctx, base);

    // Step 15: Scene manager vtable[64] — get cleanup mode
    ctx.r3.u64 = PPC_LOAD_U32(ctx.r28.u32 + 5064);
    ctx.r4.u64 = ctx.r30.u64;
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r3.u32 + 0);
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r11.u32 + 64);
    ctx.ctr.u64 = ctx.r11.u64;
    ctx.lr = 0x820F89F0;
    PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);
    uint32_t cleanup_result = ctx.r3.u32;

    // Step 16: Cleanup based on mode
    if (cleanup_result < 1) {
        ctx.r3.u64 = PPC_LOAD_U32(ctx.r31.u32 + 28628);
        ctx.lr = 0x820F8A44;
        sub_820D2798(ctx, base);
    } else if (cleanup_result == 1) {
        sub_820C8278(ctx, base);
        uint32_t poll_result = ctx.r3.u32 & 0xFF;
        if (poll_result == 0) {
            sub_820C8278(ctx, base);
            uint32_t poll2 = ctx.r3.u32 & 0xFF;
            ctx.r3.u64 = PPC_LOAD_U32(ctx.r31.u32 + 28628);
            if (poll2 == 0) {
                ctx.lr = 0x820F8A44;
                sub_820D2798(ctx, base);
            } else {
                ctx.r4.s64 = 41;
                ctx.r5.s64 = -3;
                ctx.lr = 0x820F8A38;
                sub_820D2EA0(ctx, base);
            }
        } else {
            ctx.r3.u64 = PPC_LOAD_U32(ctx.r31.u32 + 28628);
            ctx.r4.s64 = 37;
            ctx.r5.s64 = -3;
            ctx.lr = 0x820F8A38;
            sub_820D2EA0(ctx, base);
        }
    } else if (cleanup_result == 2) {
        sub_820C8278(ctx, base);
        uint32_t poll_result = ctx.r3.u32 & 0xFF;
        ctx.r3.u64 = PPC_LOAD_U32(ctx.r31.u32 + 28628);
        if (poll_result == 0) {
            ctx.lr = 0x820F8A44;
            sub_820D2798(ctx, base);
        } else {
            ctx.r4.s64 = 41;
            ctx.r5.s64 = -3;
            ctx.lr = 0x820F8A38;
            sub_820D2EA0(ctx, base);
        }
    }

    REXLOG_INFO("[TRANS] Screen transition complete.");

    // Epilogue
    ctx.r1.s64 = ctx.r1.s64 + 144;
    __restgprlr_26(ctx, base);
}

// USB Camera stubs (not used by Comix Zone but may be referenced by framework)
CZ_STUB_RETURN(__imp__XUsbcamCreate, 1)  // fail = no camera
CZ_STUB(__imp__XUsbcamDestroy)
