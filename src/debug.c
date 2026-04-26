#include "debug.h"
#include "vm.h"
#include <signal.h>
#include <string.h>
#include <unistd.h>

#ifdef SCHEME_DEBUG

// ---- Globals set during handler registration ----
static vm_state_t* crash_vm = NULL;

// ---- Async-signal-safe write of a string ----
static void safe_write(const char* s) {
    size_t len = 0;
    while (s[len]) len++;
    write(STDERR_FILENO, s, len);
}

// ---- Write hex unsigned integer to stderr ----
static void safe_write_hex(unsigned long n) {
    static const char hex[] = "0123456789abcdef";
    char buf[20];
    int i = 18;
    buf[19] = '\0';
    if (n == 0) {
        buf[i--] = '0';
    } else {
        while (n > 0) {
            buf[i--] = hex[n & 0xf];
            n >>= 4;
        }
        buf[i--] = 'x';
        buf[i--] = '0';
    }
    safe_write(&buf[i + 1]);
}

// ---- Write decimal unsigned integer ----
static void safe_write_dec(unsigned long n) {
    char buf[24];
    int i = 22;
    buf[23] = '\0';
    if (n == 0) {
        buf[i--] = '0';
    } else {
        while (n > 0) {
            buf[i--] = (char)('0' + (n % 10));
            n /= 10;
        }
    }
    safe_write(&buf[i + 1]);
}

// ---- Decode a word for display ----
static void safe_write_word(word w) {
    if (is_fixnum(w)) {
        safe_write("#<fixnum ");
        safe_write_dec((unsigned long)word_to_fixnum(w));
        safe_write(">");
    } else if (is_char(w)) {
        safe_write("#\\");
        char c = (char)word_to_char(w);
        write(STDERR_FILENO, &c, 1);
    } else if (w == word_true()) {
        safe_write("#t");
    } else if (w == word_false()) {
        safe_write("#f");
    } else if (w == word_nil()) {
        safe_write("()");
    } else if (w == word_eof()) {
        safe_write("#<eof>");
    } else if (is_ptr(w)) {
        word* hdr = ptr_from_word(w);
        safe_write("#<ptr type=");
        safe_write_dec((unsigned long)obj_type(hdr));
        safe_write(" addr=");
        safe_write_hex((unsigned long)(uintptr_t)hdr);
        safe_write(">");
    } else {
        safe_write("#<unknown 0x");
        safe_write_hex((unsigned long)w);
        safe_write(">");
    }
}

// ---- Dump VM state ----
static void dump_vm_state(void) {
    if (!crash_vm) return;

    safe_write("VM state:\n");

    // IP offset
    if (crash_vm->current_code && crash_vm->ip) {
        uint8_t* code_start = (uint8_t*)(crash_vm->current_code + 3);
        unsigned long ip_off = (unsigned long)(crash_vm->ip - code_start);
        unsigned long bc_len = (unsigned long)crash_vm->current_code[2];
        safe_write("  IP offset: 0x");
        safe_write_hex(ip_off);
        safe_write(" of 0x");
        safe_write_hex(bc_len);
        safe_write(" bytes");
        // Find which code object
        for (size_t i = 0; i < crash_vm->code_count; i++) {
            if (crash_vm->code_objects[i] == crash_vm->current_code) {
                safe_write(" (in code object #");
                safe_write_dec((unsigned long)i);
                safe_write(")");
                break;
            }
        }
        safe_write("\n");
    }

    // SP
    if (crash_vm->stack) {
        unsigned long sp_off = (unsigned long)(crash_vm->sp - crash_vm->stack);
        safe_write("  SP: 0x");
        safe_write_hex(sp_off);
        safe_write(" / 0x");
        safe_write_hex((unsigned long)crash_vm->stack_cap);
        safe_write(" words (stack ");
        if (crash_vm->stack_cap > 0) {
            safe_write_dec((unsigned long)(sp_off * 100 / crash_vm->stack_cap));
        } else {
            safe_write("0");
        }
        safe_write("% used)\n");
    }

    // ACC
    safe_write("  ACC: ");
    safe_write_word(crash_vm->acc);
    safe_write("\n");

    // Active error
    if (crash_vm->error_kind != ERR_NONE) {
        safe_write("  Error: kind=");
        safe_write_dec((unsigned long)crash_vm->error_kind);
        safe_write(" \"");
        if (crash_vm->error_msg) {
            safe_write(crash_vm->error_msg);
        }
        safe_write("\"");
        if (crash_vm->error_kind != ERR_NONE) {
            safe_write(" arg=");
            safe_write_word(crash_vm->error_arg);
        }
        safe_write("\n");
    }
}

// ---- Signal name from signum ----
static const char* signal_name(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV: address boundary error";
    case SIGABRT: return "SIGABRT: abort called";
    case SIGFPE:  return "SIGFPE: arithmetic exception";
    case SIGILL:  return "SIGILL: illegal instruction";
    default:      return "UNKNOWN SIGNAL";
    }
}

// ---- Backtrace via glibc backtrace() ----
#if defined(__GLIBC__)
#include <execinfo.h>

static void print_backtrace(void) {
    void* frames[32];
    int n = backtrace(frames, 32);
    char** symbols = backtrace_symbols(frames, n);
    safe_write("Backtrace:\n");
    for (int i = 0; i < n; i++) {
        safe_write("  #");
        safe_write_dec((unsigned long)i);
        safe_write(" ");
        safe_write(symbols[i] ? symbols[i] : "???");
        safe_write("\n");
    }
    // backtrace_symbols uses malloc, but we're crashing anyway so it's acceptable
}

// ---- Backtrace via _Unwind_Backtrace (bionic/Android) ----
#elif defined(__ANDROID__) || defined(__BIONIC__)
#define _GNU_SOURCE
#include <unwind.h>

struct bt_state {
    void** frames;
    int    count;
    int    max;
};

static _Unwind_Reason_Code bt_callback(struct _Unwind_Context* ctx, void* arg) {
    struct bt_state* state = (struct bt_state*)arg;
    if (state->count >= state->max)
        return _URC_END_OF_STACK;
    state->frames[state->count++] = (void*)_Unwind_GetIP(ctx);
    return _URC_NO_REASON;
}

static void print_backtrace(void) {
    void* frames[32];
    struct bt_state state = { frames, 0, 32 };
    _Unwind_Backtrace(bt_callback, &state);

    safe_write("Backtrace (IPs):\n");
    for (int i = 0; i < state.count; i++) {
        safe_write("  #");
        safe_write_dec((unsigned long)i);
        safe_write(" 0x");
        safe_write_hex((unsigned long)(uintptr_t)frames[i]);
        safe_write("\n");
    }
}

// ---- No backtrace available ----
#else
static void print_backtrace(void) {
    safe_write("Backtrace: not available on this platform\n");
}
#endif

// ---- Signal handler ----
static void crash_handler(int sig, siginfo_t* info, void* ctx) {
    (void)ctx;

    safe_write("\n=== SCHEME CRASH [");
    safe_write(signal_name(sig));
    safe_write("] ===\n");

    if (info && (sig == SIGSEGV || sig == SIGFPE)) {
        safe_write("Fault address: 0x");
        safe_write_hex((unsigned long)(uintptr_t)info->si_addr);
        safe_write("\n");
    }

    dump_vm_state();
    print_backtrace();

    safe_write("Aborted\n");

    // Restore default handler and re-raise so the OS can write a core dump
    signal(sig, SIG_DFL);
    raise(sig);
}

void debug_install_handlers(vm_state_t* vm) {
    crash_vm = vm;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO;

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
}

#endif // SCHEME_DEBUG
