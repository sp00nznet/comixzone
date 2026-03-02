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
}

// User sign-in stubs — critical for startup flow.
// The SegaVintage engine checks sign-in state at the logo screen.
// Without these, the game hangs waiting for a user to sign in.
CZ_STUB_RETURN(__imp__XamUserGetSigninState, 1)  // 1 = SignedInLocally

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

// Storage device selector fix.
//
// The SDK handles XamShowDeviceSelectorUI headlessly (auto-selects HDD,
// completes overlapped after 100ms). The game's state machine works correctly:
// state transitions 0→1→2 over ~500ms. The overlapped completes within 100ms.
//
// THE PROBLEM: When all conditions are met (state=2, overlapped done, UI closed),
// the original code calls vtable[40] → sub_820ECA40 → sub_820F8838 which hangs
// inside a complex screen transition function that waits for conditions that
// never become true in the recompiled environment.
//
// THE FIX: Override the polling function to process the overlapped result normally,
// but skip the vtable[40] call. Instead, set the 'done' flag so the separate
// status checker (sub_820ECB70) picks it up and transitions via sub_820E9CD8
// — a different, non-blocking code path.
//
// Static addresses (from lis r11,-32208 = 0x82300000):
//   overlapped: 0x82300000 + 14908 = 0x82303A3C
//   device_id:  0x82300000 + 14936 = 0x82303A58
static constexpr uint32_t STORAGE_OVERLAPPED_ADDR = 0x82303A3C;
static constexpr uint32_t STORAGE_DEVICE_ID_ADDR  = 0x82303A58;

// Forward-declare generated functions we call
extern "C" PPC_FUNC_IMPL(__imp__sub_820E8308);   // base class update
extern "C" PPC_FUNC_IMPL(__imp__sub_820E8910);   // state getter: returns [obj+20]+4
extern "C" PPC_FUNC_IMPL(__imp__sub_8212EAC0);   // XGetOverlappedResult
extern "C" PPC_FUNC_IMPL(__imp__sub_8212EA98);   // overlapped cleanup

extern "C" PPC_FUNC(sub_820ECB90) {
    uint32_t obj = ctx.r3.u32;

    // 1. Base class update (matches original)
    ctx.r3.u32 = obj;
    __imp__sub_820E8308(ctx, base);

    // 2. Check pending flag at [obj+2564]
    uint8_t pending = PPC_LOAD_U8(obj + 2564);
    if (!pending)
        return;

    // 3. Check state == 2 via sub_820E8910 (screen transition complete)
    ctx.r3.u32 = obj;
    __imp__sub_820E8910(ctx, base);
    if (ctx.r3.u32 != 2)
        return;

    // 4. Check overlapped done (InternalLow != ERROR_IO_PENDING)
    uint32_t internal_low = PPC_LOAD_U32(STORAGE_OVERLAPPED_ADDR);
    if (internal_low == 997)
        return;

    // 5. Skip UI-active check (the SDK's headless mode clears it immediately)

    REXLOG_INFO("[STORAGE] Device selector completed (overlapped_result={}).", internal_low);

    // 6. Clear pending flag
    PPC_STORE_U8(obj + 2564, 0);

    // 7. Allocate PPC stack frame for XGetOverlappedResult call
    uint32_t orig_sp = ctx.r1.u32;
    ctx.r1.u32 = orig_sp - 112;
    PPC_STORE_U32(ctx.r1.u32, orig_sp);  // back-chain

    // 8. XGetOverlappedResult(overlapped, &result_out, wait=false)
    PPC_STORE_U32(ctx.r1.u32 + 80, 0);   // result local
    ctx.r3.u32 = STORAGE_OVERLAPPED_ADDR;
    ctx.r4.u32 = ctx.r1.u32 + 80;
    ctx.r5.u32 = 0;
    __imp__sub_8212EAC0(ctx, base);

    // 9. Cleanup overlapped
    ctx.r3.u32 = STORAGE_OVERLAPPED_ADDR;
    __imp__sub_8212EA98(ctx, base);

    // 10. Restore stack
    ctx.r1.u32 = orig_sp;

    // 11. Read device_id
    uint32_t device_id = PPC_LOAD_U32(STORAGE_DEVICE_ID_ADDR);
    REXLOG_INFO("[STORAGE] device_id=0x{:08X}", device_id);

    if (device_id == 0) {
        // Cancel path: set done flag, call vtable[56] (same as original)
        PPC_STORE_U8(obj + 2565, 1);
        uint32_t vtable_ptr = PPC_LOAD_U32(obj);
        uint32_t method = PPC_LOAD_U32(vtable_ptr + 56);
        ctx.r3.u32 = obj;
        PPC_CALL_INDIRECT_FUNC(method);
    } else {
        // Success: set done flag but SKIP vtable[40] (which hangs in sub_820F8838).
        // The screen manager's status checker (sub_820ECB70) will see done=1
        // and transition via sub_820E9CD8 — the normal, non-blocking path.
        PPC_STORE_U8(obj + 2565, 1);
        REXLOG_INFO("[STORAGE] Device selected. Flagging done for screen manager transition.");
    }
}

// USB Camera stubs (not used by Comix Zone but may be referenced by framework)
CZ_STUB_RETURN(__imp__XUsbcamCreate, 1)  // fail = no camera
CZ_STUB(__imp__XUsbcamDestroy)
