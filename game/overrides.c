/**
 * Manual function overrides and ICALL diagnostics
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* The generated register model (g_eax/g_esp are thread-local there) and the
 * XBOX_PTR/MEM32 accessors; the register names below are its macros. */
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"

/* ── ICALL trace ring buffer ───────────────────────────────── */

/*
 * These globals are written by the RECOMP_ICALL macro (defined in
 * recomp_types.h) every time an indirect call is dispatched. When a
 * crash occurs, the VEH handler or recomp_icall_fail_log() can dump
 * the last 16 call targets to help you trace what happened.
 *
 * The runtime owns them: xbox_kernel defines all three in
 * src/kernel/xbox_memory_layout.c, and recomp_types.h declares them extern.
 * Declare, do not define -- a definition here as well is a duplicate symbol,
 * and a project copied from this template failed to link on all three:
 *
 *   xbox_memory_layout.obj : error LNK2005: g_icall_count already defined
 *                            in recomp_manual.obj
 */
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;
extern volatile uint64_t g_icall_count;

typedef void (*recomp_func_t)(void);

/* ── Register state (defined in xbox_memory_layout.c) ──────── */

extern ptrdiff_t g_xbox_mem_offset;

/* ── DOAX overrides ───────────────────────────────────────────
 *
 * A function defined here as sub_XXXXXXXX replaces the lifted one
 * (scripts/regen.sh passes this file to tools.recomp --exclude-manual, which then emits the
 * lifted body as sub_XXXXXXXX_gen so an override can still call it).
 *
 * Several follow the NFSU2 port's overrides (github.com/antoxa2584x/nfsu2-sw,
 * src/recomp_manual.c) at their DOAX (XDK 4928) addresses.
 */

/* ── XAPI fibers: SwitchToFiber (0x0018305A), DeleteFiber (0x00183047) ──
 *
 * SwitchToFiber(fiber), stdcall:
 *     td = fs:[4][[0x003B5258]]          ; XAPI per-thread data
 *     push ebp, esi, edi, ebx, fs:[0]
 *     [td+4]->esp(+0x0C) = esp
 *     esp = fiber->esp ; [td+4] = fiber
 *     pop fs:[0], ebx, edi, esi, ebp ; ret 4   -- into the *other* stack
 * Lifted, the final ret returned to the old native caller and the target
 * fiber never ran: the frame loop (0x000BB310) scheduled the game's first
 * fiber (CreateFiber 0x00182FBB, start wrapper 0x00182F76 -> 0x000B5570)
 * through 0x000B5680 and it never started -- an endless black loading
 * screen. The runtime now runs each fiber on a host thread of its own
 * (xbox_fiber_switch, kernel_bridge.c); this does the guest-visible part:
 * the current-fiber pointer and the SEH chain head, which belongs to the
 * fiber and comes back with it. Registers are the host thread's own. */
extern void xbox_fiber_switch(uint32_t from, uint32_t to);
extern void xbox_fiber_deleted(uint32_t fiber);

void sub_0018305A(void)
{
    uint32_t to = MEM32(esp + 4);
    uint32_t td = MEM32(MEM32(XBOX_FS_BASE + 4) + MEM32(0x003B5258u) * 4);
    uint32_t from = MEM32(td + 4);
    uint32_t seh = MEM32(XBOX_FS_BASE + 0);

    MEM32(from + 0x0C) = esp;           /* as the original records it */
    MEM32(td + 4) = to;
    xbox_fiber_switch(from, to);        /* back when someone switches to us */
    MEM32(td + 4) = from;
    MEM32(XBOX_FS_BASE + 0) = seh;
    esp += 4 + 4;                       /* return address, 1 stdcall arg */
}

extern void sub_00183047_gen(void);

void sub_00183047(void)
{
    xbox_fiber_deleted(MEM32(esp + 4));
    sub_00183047_gen();                 /* frees the fiber's stack */
}

/* ── XInputGetState (0x0023308F), diagnostic ─────────────────
 *
 * stdcall (handle, PXINPUT_STATE), ret 8; XINPUT_STATE = dwPacketNumber,
 * then XINPUT_GAMEPAD { WORD wButtons; BYTE analog[8]; SHORT thumbs[4] }.
 * Called each frame by the input poll 0x000B57D0. GOON_INPUT_TRACE=1 logs
 * every change of what it returns -- the intro skips on Start in xemu but
 * not here, while the USB pad reports carry the press. */
extern void sub_0023308F_gen(void);

void sub_0023308F(void)
{
    static int trace = -1;
    static uint32_t last_buttons = 0xFFFFFFFFu, calls;
    uint32_t handle = MEM32(esp + 4), st = MEM32(esp + 8);

    if (trace < 0) {
        const char *e = getenv("GOON_INPUT_TRACE");
        trace = e && *e == '1';
    }
    sub_0023308F_gen();
    calls++;
    if (trace && st) {
        uint32_t b = MEM16(st + 4) | (uint32_t)MEM8(st + 6) << 16;  /* buttons, A */
        if (b != last_buttons || calls % 300 == 0) {
            fprintf(stderr, "[INPUTSTATE] call %u handle 0x%08X rc 0x%08X packet %u "
                    "buttons 0x%04X A %u\n", calls, handle, eax, MEM32(st),
                    b & 0xFFFF, b >> 16);
            last_buttons = b;
        }
    }
}

/* ── XAPI gamepad report copy (0x0023384E), diagnostic ───────
 *
 * thiscall, ecx = the XInput device (handle). Copies [ecx+0x66] - 2 bytes
 * of the interrupt URB's buffer (+0x32, past its 2-byte header) into the
 * state XInputGetState returns (+0x14); called from the URB completion
 * 0x002336F8 through the gamepad type's table (0x00231EB0). With
 * GOON_INPUT_TRACE=1, log what it sees whenever the report has a button. */
extern void sub_0023384E_gen(void);

void sub_0023384E(void)
{
    static int trace = -1;
    static uint32_t shown;
    uint32_t dev = ecx;

    if (trace < 0) {
        const char *e = getenv("GOON_INPUT_TRACE");
        trace = e && *e == '1';
    }
    {
        static uint32_t calls;
        static uint32_t pressed, total, odd_len;
        total++;
        if (MEM8(dev + 0x34) || MEM8(dev + 0x35)) pressed++;
        if (MEM32(dev + 0x66) != 20) odd_len++;
        if (trace && total % 500 == 0)
            fprintf(stderr, "[REPORTCOPY] %u calls, %u with a button at entry, %u with len != 20; "
                    "now len %u report %02X %02X %02X %02X\n", total, pressed, odd_len,
                    MEM32(dev + 0x66), MEM8(dev + 0x32), MEM8(dev + 0x33),
                    MEM8(dev + 0x34), MEM8(dev + 0x35));
        if (trace && ++calls <= 5)
            fprintf(stderr, "[REPORTCOPY] call %u dev 0x%08X len %u report %02X %02X %02X %02X\n",
                    calls, dev, MEM32(dev + 0x66), MEM8(dev + 0x32), MEM8(dev + 0x33),
                    MEM8(dev + 0x34), MEM8(dev + 0x35));
    }
    if (trace && (MEM8(dev + 0x34) || MEM8(dev + 0x35)) && shown < 20) {
        shown++;
        fprintf(stderr, "[REPORTCOPY] dev 0x%08X len %u report %02X %02X %02X %02X\n",
                dev, MEM32(dev + 0x66), MEM8(dev + 0x32), MEM8(dev + 0x33),
                MEM8(dev + 0x34), MEM8(dev + 0x35));
    }
    sub_0023384E_gen();
    if (trace && shown && shown <= 20 && (MEM8(dev + 0x34) || MEM8(dev + 0x35)))
        fprintf(stderr, "[REPORTCOPY]   -> state buttons %04X\n", MEM16(dev + 0x14));
}

/* ── Idle-time counter thread (0x00189D20) ───────────────────
 *
 * A worker started at the lowest priority (PsCreateSystemThreadEx, start
 * context 0x00189D20) that does nothing but
 *
 *   L: inc dword [0x003B7730]
 *      cmp dword [0x003B7748], 0
 *      je  L
 *
 * -- the title measures idle time by how far it got. On the Xbox the
 * scheduler pre-empts it whenever anything else is ready. Here guest code
 * runs under one lock, and this loop has no kernel call and is not a poll
 * the translator marks, so it never let go: the main thread and the other
 * workers starved in gil_lock right after the title opened its .afs files.
 * Count with the lock released and a 1 ms nap per turn, then run the
 * original's exit path (it re-reads the flag, which is now set). */
#include <unistd.h>
extern int xbox_gil_suspend(void);
extern void xbox_gil_resume(int depth);
extern void sub_00189D20_gen(void);

void sub_00189D20(void)
{
    while (!MEM32(0x003B7748u)) {
        int d;
        MEM32(0x003B7730u) += 1;
        d = xbox_gil_suspend();
        usleep(1000);
        xbox_gil_resume(d);
    }
    sub_00189D20_gen();
}

/* ── DirectSound DSP command post (0x001FB065) ───────────────
 *
 * thiscall, ecx = the DSP-side object, ret 4. Copies a command block into
 * the GP DSP's scratch area, stores the command code at scratch + 0x810 and
 * spins until the DSP zeroes it:
 *
 *     add  ebx, 0x810 ... mov [ebx], eax
 *   L: cmp  dword [ebx], 0
 *     jne  L
 *
 * The scratch block is ***(this + 8 + 0x10), as in NFSU2's 0x0032EB65.
 * There is no DSP: register the word with the runtime's completion list
 * before running the original body (DOAX spun here after the APU started). */
extern int xbox_ApuDspAckWord(uint32_t va);
extern void sub_001FB065_gen(void);

void sub_001FB065(void)
{
    uint32_t obj = MEM32(ecx + 8);
    uint32_t blk = obj ? MEM32(obj + 0x10) : 0;
    uint32_t scratch = blk ? MEM32(blk) : 0;

    if (scratch)
        xbox_ApuDspAckWord(scratch + 0x810);
    sub_001FB065_gen();
}

/* ── DirectSound AC'97 channel reset (0x00200AAE) ────────────
 *
 * thiscall, ecx = channel object, plain ret. Sets RR (bit 1) in the
 * channel's NABM control byte and waits for the controller to clear it --
 * except MSVC hoisted the load out of the loop, so it spins on a register
 * copy forever unless the bit was already clear:
 *
 *     mov  byte [eax+0xFEC0010B], 2
 *     mov  cl, [eax+0xFEC0010B]
 *     and  cl, 2
 *   L: test cl, cl
 *     jne  L
 *
 * Same code as NFSU2's 0x0033518D. DOAX's NABM offsets are at 0x0021402C,
 * the lock / unlock helpers 0x001FB478 / 0x001FB4AB, the re-arm 0x002008B8.
 * Everything is as the original does it, with the reset completing at once. */
void sub_00200AAE(void)
{
    uint32_t ebp_saved = g_ebp, frame, chan, nabm;

    PUSH32(esp, ebp_saved);
    frame = esp;
    g_ebp = frame;
    g_seh_ebp = frame;
    esp -= 8;
    MEM32(frame - 4) = 0;
    PUSH32(esp, esi);
    esi = ecx;
    chan = esi;

    ecx = frame - 8;
    PUSH32(esp, 0x00200AC2u); RECOMP_ABI_CALL(0x001FB478u, sub_001FB478);

    nabm = MEM32(MEM32(chan) * 4 + 0x0021402Cu);
    MEM8(nabm + 0xFEC0010Bu) = 2;
    MEM8(nabm + 0xFEC0010Bu) &= (uint8_t)~2u;   /* reset complete */
    eax = nabm;
    SET_LO8(ecx, 0);

    MEM32(nabm + 0xFEC00100u) = MEM32(chan + 0x1C);
    if (MEM32(chan) == 1)
        MEM32(0xFEC0017Cu) = MEM32(chan + 0x28);

    PUSH32(esp, 1);
    PUSH32(esp, 1);
    ecx = chan;
    PUSH32(esp, MEM8(chan + 0x24));
    PUSH32(esp, MEM8(chan + 0x25));
    eax = MEM8(chan + 0x25);
    g_ebp = frame;
    g_seh_ebp = frame;
    PUSH32(esp, 0x00200B15u); RECOMP_ABI_CALL(0x002008B8u, sub_002008B8);

    ecx = frame - 8;
    g_ebp = frame;
    g_seh_ebp = frame;
    PUSH32(esp, 0x00200B1Du); RECOMP_ABI_CALL(0x001FB4ABu, sub_001FB4AB);

    POP32(esp, esi);
    esp = frame;
    POP32(esp, ebp_saved);
    g_ebp = ebp_saved;
    esp += 4;
}

/* ── D3D fence wait, BlockOnTime (0x001EA050) ────────────────
 *
 * stdcall (time, flags), ret 8. The device is reached through the global at
 * 0x001F2978:
 *   +0x30  write sequence, bumped as fences are inserted
 *   +0x34  pointer to the semaphore the GPU releases as it completes work
 * and this inserts a NOP(5) trap and spins until the semaphore catches up.
 * (NFSU2's XDK 5849 D3D has the same pair at +0x2C / +0x30.)
 *
 * The pushbuffer executor only releases the semaphore once it knows where it
 * is: until then the title spun here forever right after its first clear.
 * Hand it over once and run the original wait. */
#define GOON_D3D_DEVICE_PTR 0x001F2978u
extern void nv2a_pb_set_semaphore_target(uint32_t guest_va);
extern void sub_001EA050_gen(void);

void sub_001EA050(void)
{
    static uint32_t registered;
    uint32_t dev = MEM32(GOON_D3D_DEVICE_PTR);
    uint32_t sem = dev ? MEM32(dev + 0x34) : 0;

    if (sem && sem != registered) {
        nv2a_pb_set_semaphore_target(sem);
        fprintf(stderr, "[D3D] GPU semaphore at 0x%08X\n", sem);
        registered = sem;
    }
    sub_001EA050_gen();
}

/* ── D3D interrupt handlers at DISPATCH_LEVEL ────────────────
 *
 * D3D's GPU interrupt: ISR 0x001EC1C0 (KeConnectInterrupt vector 3, context
 * = the hardware block 0x001F54E0, +0 = 0xFD000000) queues the DPC at block
 * +0x84, routine 0x001ECB70 (KeInitializeDpc at 0x001ED244). The DPC reads
 * PMC_INTR and calls, thiscall with ecx = the block:
 *   0x1000     PGRAPH  0x001EC900
 *   0x100000           0x001EC4C0
 *   0x1000000  vblank  0x001EC3B0
 *   0x100      PFIFO   0x001EC9F0
 * the same shape as NFSU2's XDK 5849 D3D (vblank 0x002F1D80, PGRAPH
 * 0x002F22F0), and the same reasons to wrap them: run at DISPATCH so the
 * DPC and D3D's busy-waits never interleave, report flips retired (block
 * +0x1BC) to the pushbuffer executor, and take the software-method trap the
 * executor posts -- left untaken it stays pending forever (DOAX hung in D3D
 * start-up on "NOP(40) trap still pending"). */
extern uint8_t xbox_KfRaiseIrql(uint8_t irql);
extern void xbox_KfLowerIrql(uint8_t irql);
extern uint8_t xbox_CurrentIrql(void);
extern int xbox_gil_suspend(void);
extern void xbox_gil_resume(int depth);
extern void xbox_Nv2aVblankTaken(void);
extern void nv2a_pb_flip_retired(unsigned n);
extern void nv2a_pb_trap_lock(void);
extern void nv2a_pb_trap_unlock(void);
extern void nv2a_pb_trap_taken(void);

static uint8_t d3d_isr_enter(void)
{
    uint8_t old = xbox_CurrentIrql();

    if (old < 2) {
        int d = xbox_gil_suspend();
        old = xbox_KfRaiseIrql(2);
        xbox_gil_resume(d);
    }
    return old;
}

static void d3d_isr_leave(uint8_t old)
{
    if (old < 2)
        xbox_KfLowerIrql(old);
}

/* Vblank handler, thiscall. Ends by writing PCRTC_INTR and spinning until
 * PMC_INTR bit 24 drops; its callers have read the bits already, so this is
 * the vblank being taken: clear them now and the spin ends at once. */
extern void sub_001EC3B0_gen(void);

void sub_001EC3B0(void)
{
    uint32_t blk = ecx;
    uint32_t head = MEM32(blk + 0x1BC);
    uint8_t old = d3d_isr_enter();

    xbox_Nv2aVblankTaken();
    sub_001EC3B0_gen();
    nv2a_pb_flip_retired(MEM32(blk + 0x1BC) - head);
    d3d_isr_leave(old);
}

/* PGRAPH handler, thiscall. Reads the trap from PGRAPH_INTR / NSOURCE
 * (0x400108) / TRAPPED_ADDR (0x400704) / TRAPPED_DATA (0x400708), hands a
 * NOP (method 0x100) to 0x001EC670 and acknowledges by writing PGRAPH_INTR
 * back -- a no-op on RAM. The executor posts traps under the lock held
 * here, and a call that found one reports it taken, which clears it. */
extern void sub_001EC900_gen(void);

void sub_001EC900(void)
{
    volatile uint32_t *nv = (volatile uint32_t *)XBOX_PTR(0xFD000000u);
    uint32_t blk = ecx;
    uint32_t head = MEM32(blk + 0x1BC);
    uint8_t old = d3d_isr_enter();
    uint32_t nsource;

    nv2a_pb_trap_lock();
    nsource = nv[0x400108 / 4];
    sub_001EC900_gen();
    nv2a_pb_flip_retired(MEM32(blk + 0x1BC) - head);
    if (nsource)
        nv2a_pb_trap_taken();
    nv2a_pb_trap_unlock();
    d3d_isr_leave(old);
}

/* ── Manual function overrides ─────────────────────────────── */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    /*
     * TODO: Add your overrides here. Examples:
     *
     * if (xbox_va == 0x00012345) return traced_sub_00012345;
     * if (xbox_va == 0x00067890) return stub_00067890;
     * if (xbox_va == 0x000ABCDE) return fixed_sub_000ABCDE;
     */

    (void)xbox_va;
    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 */
void recomp_icall_fail_log(uint32_t va)
{
    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu)\n",
            va, (unsigned long long)g_icall_count);

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        int idx = (g_icall_trace_idx - 16 + i) & 15;
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}

/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va)
{
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped %llu time(s) "
                    "(null or wild function pointer, at call #%llu)\n",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count);
    fflush(stderr);
}

/* ── Untranslated instructions ───────────────────────────────────────────
 *
 * The lifter emits RECOMP_UNIMPL(text, va) at every instruction it has no
 * translation for, in place of the bare comment it used to leave. The
 * instruction is still a no-op; this only stops the omission being silent.
 * RECOMP_UNIMPL_TRAP=1 aborts at the first hit, at the guest address of the
 * cause rather than wherever the damage surfaces. */
#include <stdlib.h>

void recomp_unimpl(const char *text, uint32_t va)
{
    static int printed;
    const char *trap = getenv("RECOMP_UNIMPL_TRAP");
    int stop = trap && *trap && *trap != '0';

    if (printed < 50 || stop) {
        printed++;
        fprintf(stderr,
                "[UNIMPL] untranslated instruction REACHED: `%s` at 0x%08X"
                " (a no-op; set RECOMP_UNIMPL_TRAP=1 to stop here)\n",
                text, va);
        fflush(stderr);
    }
    if (stop) abort();
}
