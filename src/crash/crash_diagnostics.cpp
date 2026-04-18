#include "crash/crash_diagnostics.hpp"

#include <array>
#include <csignal>
#include <cstdio>
#include <string>

#include <ucontext.h>
#include <unistd.h>

#if __has_include(<execinfo.h>)
#  include <execinfo.h>
#  define KEEN_HAS_EXECINFO 1
#else
#  include <unwind.h>
#  define KEEN_HAS_EXECINFO 0
#endif

#include <cpptrace/basic.hpp>

namespace keen_pbr3::crash_diagnostics {
namespace {

constexpr std::size_t kAltStackSize = 64 * 1024;
constexpr std::size_t kMaxFrames = 64;
constexpr std::size_t kMaxRegisterCount = 16;
constexpr int kFatalSignals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL};

struct RegisterEntry {
    const char* name;
    std::uintptr_t value;
};

struct RegisterSnapshot {
    RegisterArch arch{RegisterArch::unknown};
    std::array<RegisterEntry, kMaxRegisterCount> entries{};
    std::size_t count{0};
};

alignas(16) std::array<unsigned char, kAltStackSize> g_alt_stack{};

#if !KEEN_HAS_EXECINFO
struct UnwindState {
    std::array<std::uintptr_t, kMaxFrames>* frames;
    std::size_t count;
    std::size_t skip;
};

_Unwind_Reason_Code unwind_cb(struct _Unwind_Context* ctx, void* arg) {
    auto* state = static_cast<UnwindState*>(arg);
    const auto ip = static_cast<std::uintptr_t>(_Unwind_GetIP(ctx));
    if (ip == 0) {
        return _URC_NO_REASON;
    }
    if (state->skip > 0) {
        --state->skip;
        return _URC_NO_REASON;
    }
    if (state->count >= state->frames->size()) {
        return _URC_END_OF_STACK;
    }
    (*state->frames)[state->count++] = ip;
    return _URC_NO_REASON;
}
#endif

template <std::size_t N>
void write_literal(const char (&text)[N]) {
    (void)!write(STDERR_FILENO, text, N - 1);
}

void write_bytes(const char* data, std::size_t size) {
    while (size > 0) {
        const ssize_t written = write(STDERR_FILENO, data, size);
        if (written <= 0) {
            return;
        }
        data += written;
        size -= static_cast<std::size_t>(written);
    }
}

std::size_t cstr_length(const char* text) {
    std::size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

void write_cstr(const char* text) {
    write_bytes(text, cstr_length(text));
}

void write_unsigned(std::uintptr_t value) {
    char buffer[32];
    std::size_t count = 0;
    do {
        buffer[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value != 0);
    for (std::size_t i = 0; i < count / 2; ++i) {
        const char tmp = buffer[i];
        buffer[i] = buffer[count - 1 - i];
        buffer[count - 1 - i] = tmp;
    }
    write_bytes(buffer, count);
}

void write_hex(std::uintptr_t value) {
    constexpr char kHex[] = "0123456789abcdef";
    char buffer[2 + sizeof(std::uintptr_t) * 2];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (std::size_t i = 0; i < sizeof(std::uintptr_t) * 2; ++i) {
        const auto shift = static_cast<unsigned>((sizeof(std::uintptr_t) * 2 - 1 - i) * 4);
        buffer[2 + i] = kHex[(value >> shift) & 0x0fU];
    }
    write_bytes(buffer, sizeof(buffer));
}

const char* signal_name(int signum) {
    switch (signum) {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGBUS: return "SIGBUS";
        case SIGFPE: return "SIGFPE";
        case SIGILL: return "SIGILL";
        default: return "UNKNOWN";
    }
}

const char* arch_name(RegisterArch arch) {
    switch (arch) {
        case RegisterArch::x86: return "x86";
        case RegisterArch::x86_64: return "x86_64";
        case RegisterArch::arm: return "arm";
        case RegisterArch::aarch64: return "aarch64";
        case RegisterArch::mips: return "mips";
        case RegisterArch::unknown: return "unknown";
    }
    return "unknown";
}

void append_hex(std::string& out, std::uintptr_t value) {
    constexpr char kHex[] = "0123456789abcdef";
    out += "0x";
    bool started = false;
    for (std::size_t i = 0; i < sizeof(std::uintptr_t) * 2; ++i) {
        const auto shift = static_cast<unsigned>((sizeof(std::uintptr_t) * 2 - 1 - i) * 4);
        const auto digit = static_cast<unsigned>((value >> shift) & 0x0fU);
        if (digit != 0 || started || i + 1 == sizeof(std::uintptr_t) * 2) {
            out.push_back(kHex[digit]);
            started = true;
        }
    }
}

void push_register(RegisterSnapshot& snapshot, const char* name, std::uintptr_t value) {
    if (snapshot.count >= snapshot.entries.size()) {
        return;
    }
    snapshot.entries[snapshot.count++] = RegisterEntry{name, value};
}

RegisterSnapshot capture_registers(const ucontext_t* context) {
    RegisterSnapshot snapshot;
    if (context == nullptr) {
        return snapshot;
    }

#if defined(__x86_64__)
    snapshot.arch = RegisterArch::x86_64;
    push_register(snapshot, "rip", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_RIP]));
    push_register(snapshot, "rsp", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_RSP]));
    push_register(snapshot, "rbp", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_RBP]));
    push_register(snapshot, "rax", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_RAX]));
    push_register(snapshot, "rbx", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_RBX]));
    push_register(snapshot, "rcx", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_RCX]));
    push_register(snapshot, "rdx", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_RDX]));
    push_register(snapshot, "rsi", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_RSI]));
    push_register(snapshot, "rdi", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_RDI]));
#elif defined(__i386__)
    snapshot.arch = RegisterArch::x86;
    push_register(snapshot, "eip", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_EIP]));
    push_register(snapshot, "esp", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_ESP]));
    push_register(snapshot, "ebp", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_EBP]));
    push_register(snapshot, "eax", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_EAX]));
    push_register(snapshot, "ebx", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_EBX]));
    push_register(snapshot, "ecx", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_ECX]));
    push_register(snapshot, "edx", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_EDX]));
    push_register(snapshot, "esi", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_ESI]));
    push_register(snapshot, "edi", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_EDI]));
#elif defined(__aarch64__)
    snapshot.arch = RegisterArch::aarch64;
    push_register(snapshot, "pc", static_cast<std::uintptr_t>(context->uc_mcontext.pc));
    push_register(snapshot, "sp", static_cast<std::uintptr_t>(context->uc_mcontext.sp));
    push_register(snapshot, "x29", static_cast<std::uintptr_t>(context->uc_mcontext.regs[29]));
    push_register(snapshot, "x30", static_cast<std::uintptr_t>(context->uc_mcontext.regs[30]));
    push_register(snapshot, "x0", static_cast<std::uintptr_t>(context->uc_mcontext.regs[0]));
    push_register(snapshot, "x1", static_cast<std::uintptr_t>(context->uc_mcontext.regs[1]));
    push_register(snapshot, "x2", static_cast<std::uintptr_t>(context->uc_mcontext.regs[2]));
    push_register(snapshot, "x3", static_cast<std::uintptr_t>(context->uc_mcontext.regs[3]));
#elif defined(__arm__)
    snapshot.arch = RegisterArch::arm;
    push_register(snapshot, "pc", static_cast<std::uintptr_t>(context->uc_mcontext.arm_pc));
    push_register(snapshot, "sp", static_cast<std::uintptr_t>(context->uc_mcontext.arm_sp));
    push_register(snapshot, "lr", static_cast<std::uintptr_t>(context->uc_mcontext.arm_lr));
    push_register(snapshot, "fp", static_cast<std::uintptr_t>(context->uc_mcontext.arm_fp));
    push_register(snapshot, "r0", static_cast<std::uintptr_t>(context->uc_mcontext.arm_r0));
    push_register(snapshot, "r1", static_cast<std::uintptr_t>(context->uc_mcontext.arm_r1));
    push_register(snapshot, "r2", static_cast<std::uintptr_t>(context->uc_mcontext.arm_r2));
    push_register(snapshot, "r3", static_cast<std::uintptr_t>(context->uc_mcontext.arm_r3));
#elif defined(__mips__)
    snapshot.arch = RegisterArch::mips;
    push_register(snapshot, "pc", static_cast<std::uintptr_t>(context->uc_mcontext.pc));
    push_register(snapshot, "sp", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[29]));
    push_register(snapshot, "fp", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[30]));
    push_register(snapshot, "ra", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[31]));
    push_register(snapshot, "v0", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[2]));
    push_register(snapshot, "v1", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[3]));
    push_register(snapshot, "a0", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[4]));
    push_register(snapshot, "a1", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[5]));
    push_register(snapshot, "a2", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[6]));
    push_register(snapshot, "a3", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[7]));
#endif

    return snapshot;
}

void write_register_snapshot(const ucontext_t* context) {
    const RegisterSnapshot snapshot = capture_registers(context);
    if (snapshot.count == 0 || snapshot.arch == RegisterArch::unknown) {
        write_literal("registers: unavailable for this architecture\n");
        return;
    }

    write_literal("registers (");
    write_cstr(arch_name(snapshot.arch));
    write_literal("):");
    for (std::size_t i = 0; i < snapshot.count; ++i) {
        write_literal(" ");
        write_cstr(snapshot.entries[i].name);
        write_literal("=");
        write_hex(snapshot.entries[i].value);
    }
    write_literal("\n");
}

std::size_t capture_raw_frames(std::array<std::uintptr_t, kMaxFrames>& frames) {
    const auto count = cpptrace::safe_generate_raw_trace(frames.data(), frames.size(), 2);
    if (count > 0) {
        return count;
    }

#if KEEN_HAS_EXECINFO
    void* raw_frames[kMaxFrames];
    const int frame_count = backtrace(raw_frames, static_cast<int>(kMaxFrames));
    std::size_t out = 0;
    for (int i = 2; i < frame_count && out < frames.size(); ++i) {
        frames[out++] = reinterpret_cast<std::uintptr_t>(raw_frames[i]);
    }
    return out;
#else
    UnwindState state{&frames, 0, 2};
    _Unwind_Backtrace(unwind_cb, &state);
    return state.count;
#endif
}

void write_frame_list() {
    std::array<std::uintptr_t, kMaxFrames> frames{};
    const std::size_t count = capture_raw_frames(frames);

    write_literal("stack pcs:\n");
    if (count == 0) {
        write_literal("  <unavailable>\n");
    } else {
        for (std::size_t i = 0; i < count; ++i) {
            write_literal("  pc[");
            write_unsigned(i);
            write_literal("]: ");
            write_hex(frames[i]);
            write_literal("\n");
        }
    }
    write_literal("(raw addresses - resolve with: addr2line -Cfpie keen-pbr.debug <addr...>)\n");
}

void crash_handler(int signum, siginfo_t* info, void* context_ptr) {
    write_literal("\n=== CRASH (signal ");
    write_cstr(signal_name(signum));
    write_literal(" / ");
    write_unsigned(static_cast<std::uintptr_t>(signum));
    write_literal(") ===\n");

    if (info != nullptr) {
        write_literal("fault address: ");
        write_hex(reinterpret_cast<std::uintptr_t>(info->si_addr));
        write_literal("\n");
    }

    write_register_snapshot(static_cast<ucontext_t*>(context_ptr));
    write_frame_list();

    signal(signum, SIG_DFL);
    raise(signum);
}

} // namespace

void warm_up() {
    try {
        (void)cpptrace::generate_raw_trace(0, 1);
        (void)cpptrace::can_signal_safe_unwind();
        (void)cpptrace::can_get_safe_object_frame();
    } catch (...) {
    }
}

bool install_fatal_signal_handlers() {
    stack_t alt_stack{};
    alt_stack.ss_sp = g_alt_stack.data();
    alt_stack.ss_size = g_alt_stack.size();
    alt_stack.ss_flags = 0;
    if (sigaltstack(&alt_stack, nullptr) != 0) {
        std::perror("sigaltstack");
        return false;
    }

    struct sigaction sa {};
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);

    for (const int signum : kFatalSignals) {
        if (sigaction(signum, &sa, nullptr) != 0) {
            std::perror("sigaction");
            return false;
        }
    }

    return true;
}

std::string format_register_dump_for_test(
    RegisterArch arch,
    std::initializer_list<RegisterValue> values) {
    if (arch == RegisterArch::unknown || values.size() == 0) {
        return "registers: unavailable for this architecture\n";
    }

    std::string out = "registers (";
    out += arch_name(arch);
    out += "):";
    for (const RegisterValue& value : values) {
        out.push_back(' ');
        out += value.name;
        out.push_back('=');
        append_hex(out, value.value);
    }
    out.push_back('\n');
    return out;
}

} // namespace keen_pbr3::crash_diagnostics
