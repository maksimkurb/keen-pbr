#include "crash/crash_diagnostics.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>

#if KEEN_PBR_HAS_LIBUNWIND
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#endif

namespace keen_pbr3::crash_diagnostics {

extern "C" [[gnu::noinline, gnu::used]] void keen_pbr_crash_symbolization_anchor() noexcept {
    asm volatile("" ::: "memory");
}

namespace {

constexpr std::size_t kAltStackSize = std::size_t{64} * 1024;
constexpr std::size_t kMaxFrames = 64;
constexpr std::size_t kMaxRegisterCount = 40;
constexpr std::size_t kMaxMapsBytes = std::size_t{64} * 1024;
constexpr std::size_t kMaxStackBytes = std::size_t{8} * 1024;
constexpr std::size_t kStackChunkBytes = 128;
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

alignas(16) thread_local std::array<unsigned char, kAltStackSize> g_alt_stack{};
thread_local bool g_alt_stack_installed = false;

int g_report_fd = -1;
int g_maps_fd = -1;
int g_mem_fd = -1;
std::array<char, 4096> g_report_path{};
std::array<char, 4096> g_current_path{};
std::array<char, 2048> g_metadata_line{};
std::size_t g_metadata_length = 0;
volatile std::sig_atomic_t g_handler_active = 0;

void write_all_fd(int fd, const char* data, std::size_t size) noexcept {
    if (fd < 0) {
        return;
    }
    while (size > 0) {
        const ssize_t written = ::write(fd, data, size);
        if (written > 0) {
            data += written;
            size -= static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        return;
    }
}

void write_bytes(const char* data, std::size_t size) noexcept {
    write_all_fd(STDERR_FILENO, data, size);
    if (g_report_fd != STDERR_FILENO) {
        write_all_fd(g_report_fd, data, size);
    }
}

template <std::size_t N>
void write_literal(const char (&text)[N]) noexcept {
    write_bytes(text, N - 1);
}

std::size_t cstr_length(const char* text) noexcept {
    std::size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

bool cstr_equal(const char* left, const char* right) noexcept {
    std::size_t index = 0;
    while (left[index] != '\0' && left[index] == right[index]) {
        ++index;
    }
    return left[index] == right[index];
}

void write_cstr(const char* text) noexcept {
    write_bytes(text, cstr_length(text));
}

void write_unsigned(std::uintptr_t value) noexcept {
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

void write_hex(std::uintptr_t value) noexcept {
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

void write_hex_bytes(const unsigned char* data, std::size_t size) noexcept {
    constexpr char kHex[] = "0123456789abcdef";
    char encoded[kStackChunkBytes * 2];
    for (std::size_t i = 0; i < size; ++i) {
        encoded[i * 2] = kHex[data[i] >> 4U];
        encoded[i * 2 + 1] = kHex[data[i] & 0x0fU];
    }
    write_bytes(encoded, size * 2);
}

const char* signal_name(int signum) noexcept {
    switch (signum) {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGBUS: return "SIGBUS";
        case SIGFPE: return "SIGFPE";
        case SIGILL: return "SIGILL";
        default: return "UNKNOWN";
    }
}

const char* arch_name(RegisterArch arch) noexcept {
    switch (arch) {
        case RegisterArch::x86: return "x86";
        case RegisterArch::x86_64: return "x86_64";
        case RegisterArch::arm: return "arm";
        case RegisterArch::aarch64: return "aarch64";
        case RegisterArch::mips: return "mips";
        case RegisterArch::ppc: return "ppc";
        case RegisterArch::ppc64: return "ppc64";
        case RegisterArch::riscv: return "riscv";
        case RegisterArch::loongarch64: return "loongarch64";
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

void push_register(RegisterSnapshot& snapshot, const char* name, std::uintptr_t value) noexcept {
    if (snapshot.count < snapshot.entries.size()) {
        snapshot.entries[snapshot.count++] = RegisterEntry{name, value};
    }
}

RegisterSnapshot capture_registers(const ucontext_t* context) noexcept {
    RegisterSnapshot snapshot;
    if (context == nullptr) {
        return snapshot;
    }

#if defined(__x86_64__)
    snapshot.arch = RegisterArch::x86_64;
    constexpr int regs[] = {REG_RIP, REG_RSP, REG_RBP, REG_RAX, REG_RBX, REG_RCX, REG_RDX,
                            REG_RSI, REG_RDI, REG_R8, REG_R9, REG_R10, REG_R11, REG_R12,
                            REG_R13, REG_R14, REG_R15, REG_EFL};
    constexpr const char* names[] = {"rip", "rsp", "rbp", "rax", "rbx", "rcx", "rdx",
                                     "rsi", "rdi", "r8", "r9", "r10", "r11", "r12",
                                     "r13", "r14", "r15", "eflags"};
    for (std::size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); ++i) {
        push_register(snapshot, names[i], static_cast<std::uintptr_t>(context->uc_mcontext.gregs[regs[i]]));
    }
#elif defined(__i386__)
    snapshot.arch = RegisterArch::x86;
    constexpr int regs[] = {REG_EIP, REG_ESP, REG_EBP, REG_EAX, REG_EBX, REG_ECX,
                            REG_EDX, REG_ESI, REG_EDI, REG_EFL};
    constexpr const char* names[] = {"eip", "esp", "ebp", "eax", "ebx", "ecx",
                                     "edx", "esi", "edi", "eflags"};
    for (std::size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); ++i) {
        push_register(snapshot, names[i], static_cast<std::uintptr_t>(context->uc_mcontext.gregs[regs[i]]));
    }
#elif defined(__aarch64__)
    snapshot.arch = RegisterArch::aarch64;
    push_register(snapshot, "pc", static_cast<std::uintptr_t>(context->uc_mcontext.pc));
    push_register(snapshot, "sp", static_cast<std::uintptr_t>(context->uc_mcontext.sp));
    for (std::size_t i = 0; i < 31; ++i) {
        static constexpr const char* names[] = {
            "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10",
            "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x19", "x20",
            "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28", "x29", "x30"};
        push_register(snapshot, names[i], static_cast<std::uintptr_t>(context->uc_mcontext.regs[i]));
    }
    push_register(snapshot, "pstate", static_cast<std::uintptr_t>(context->uc_mcontext.pstate));
#elif defined(__arm__)
    snapshot.arch = RegisterArch::arm;
    const auto& m = context->uc_mcontext;
    push_register(snapshot, "r0", m.arm_r0); push_register(snapshot, "r1", m.arm_r1);
    push_register(snapshot, "r2", m.arm_r2); push_register(snapshot, "r3", m.arm_r3);
    push_register(snapshot, "r4", m.arm_r4); push_register(snapshot, "r5", m.arm_r5);
    push_register(snapshot, "r6", m.arm_r6); push_register(snapshot, "r7", m.arm_r7);
    push_register(snapshot, "r8", m.arm_r8); push_register(snapshot, "r9", m.arm_r9);
    push_register(snapshot, "r10", m.arm_r10); push_register(snapshot, "fp", m.arm_fp);
    push_register(snapshot, "ip", m.arm_ip); push_register(snapshot, "sp", m.arm_sp);
    push_register(snapshot, "lr", m.arm_lr); push_register(snapshot, "pc", m.arm_pc);
    push_register(snapshot, "cpsr", m.arm_cpsr);
#elif defined(__mips__)
    snapshot.arch = RegisterArch::mips;
    push_register(snapshot, "pc", static_cast<std::uintptr_t>(context->uc_mcontext.pc));
    static constexpr const char* names[] = {
        "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3",
        "t4", "t5", "t6", "t7", "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
        "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"};
    for (std::size_t i = 0; i < 32; ++i) {
        push_register(snapshot, names[i], static_cast<std::uintptr_t>(context->uc_mcontext.gregs[i]));
    }
#elif defined(__powerpc64__)
    snapshot.arch = RegisterArch::ppc64;
#elif defined(__powerpc__)
    snapshot.arch = RegisterArch::ppc;
#elif defined(__riscv)
    snapshot.arch = RegisterArch::riscv;
#elif defined(__loongarch64)
    snapshot.arch = RegisterArch::loongarch64;
#endif
    return snapshot;
}

std::uintptr_t stack_pointer_from(const RegisterSnapshot& snapshot) noexcept {
    for (std::size_t i = 0; i < snapshot.count; ++i) {
        const char* name = snapshot.entries[i].name;
        if (cstr_equal(name, "sp") || cstr_equal(name, "rsp") || cstr_equal(name, "esp")) {
            return snapshot.entries[i].value;
        }
    }
    return 0;
}

void write_register_snapshot(const RegisterSnapshot& snapshot) noexcept {
    write_literal("registers arch=");
    write_cstr(arch_name(snapshot.arch));
    if (snapshot.count == 0) {
        write_literal(" status=unavailable\n");
        return;
    }
    for (std::size_t i = 0; i < snapshot.count; ++i) {
        write_literal(" ");
        write_cstr(snapshot.entries[i].name);
        write_literal("=");
        write_hex(snapshot.entries[i].value);
    }
    write_literal("\n");
}

void write_mappings() noexcept {
    write_literal("maps-begin\n");
    if (g_maps_fd < 0 || ::lseek(g_maps_fd, 0, SEEK_SET) < 0) {
        write_literal("maps-status unavailable=1\n");
        write_literal("maps-end\n");
        return;
    }
    std::array<char, 1024> buffer{};
    std::size_t total = 0;
    bool truncated = false;
    char last = '\n';
    while (total < kMaxMapsBytes) {
        const std::size_t wanted = std::min(buffer.size(), kMaxMapsBytes - total);
        const ssize_t count = ::read(g_maps_fd, buffer.data(), wanted);
        if (count > 0) {
            write_bytes(buffer.data(), static_cast<std::size_t>(count));
            last = buffer[static_cast<std::size_t>(count) - 1];
            total += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    if (total == kMaxMapsBytes) {
        truncated = true;
    }
    if (total > 0 && last != '\n') {
        write_literal("\n");
    }
    write_literal("maps-status bytes=");
    write_unsigned(total);
    write_literal(" truncated=");
    write_unsigned(truncated ? 1 : 0);
    write_literal("\nmaps-end\n");
}

std::uintptr_t write_unwind(void* context_ptr) noexcept {
#if KEEN_PBR_HAS_LIBUNWIND
    unw_cursor_t cursor{};
    auto* context = reinterpret_cast<unw_context_t*>(context_ptr);
    const int init_status = context == nullptr
        ? UNW_EINVAL
        : unw_init_local2(&cursor, context, UNW_INIT_SIGNAL_FRAME);
    write_literal("unwind backend=libunwind init=");
    write_unsigned(static_cast<std::uintptr_t>(init_status < 0 ? -init_status : init_status));
    write_literal("\n");
    if (init_status < 0) {
        write_literal("unwind-status complete=0 frames=0 error=init\n");
        return 0;
    }

    write_literal("unwind-registers");
    for (int reg = 0; reg <= UNW_REG_LAST; ++reg) {
        unw_word_t value = 0;
        const char* name = unw_regname(reg);
        if (name == nullptr || name[0] == '\0' || unw_get_reg(&cursor, reg, &value) < 0) {
            continue;
        }
        write_literal(" ");
        write_cstr(name);
        write_literal("=");
        write_hex(static_cast<std::uintptr_t>(value));
    }
    write_literal("\n");

    std::uintptr_t first_sp = 0;
    std::size_t frames = 0;
    int step_status = 1;
    while (frames < kMaxFrames) {
        unw_word_t ip = 0;
        unw_word_t sp = 0;
        if (unw_get_reg(&cursor, UNW_REG_IP, &ip) < 0 ||
            unw_get_reg(&cursor, UNW_REG_SP, &sp) < 0) {
            step_status = UNW_EBADREG;
            break;
        }
        if (frames == 0) {
            first_sp = static_cast<std::uintptr_t>(sp);
        }
        if (ip == 0) {
            step_status = 0;
            break;
        }
        write_literal("frame index=");
        write_unsigned(frames);
        if (frames == 0) {
            write_literal(" kind=fault pc=");
        } else {
            write_literal(" kind=return pc=");
        }
        write_hex(static_cast<std::uintptr_t>(ip));
        write_literal(" sp=");
        write_hex(static_cast<std::uintptr_t>(sp));
        write_literal("\n");
        ++frames;
        step_status = unw_step(&cursor);
        if (step_status <= 0) {
            break;
        }
    }
    write_literal("unwind-status complete=");
    write_unsigned(step_status == 0 ? 1 : 0);
    write_literal(" frames=");
    write_unsigned(frames);
    write_literal(" error=");
    write_unsigned(step_status < 0 ? static_cast<std::uintptr_t>(-step_status) : 0);
    write_literal("\n");
    return first_sp;
#else
    const auto snapshot = capture_registers(static_cast<const ucontext_t*>(context_ptr));
    std::uintptr_t instruction_pointer = 0;
    for (std::size_t index = 0; index < snapshot.count; ++index) {
        const char* name = snapshot.entries[index].name;
        if (cstr_equal(name, "pc") || cstr_equal(name, "rip") || cstr_equal(name, "eip")) {
            instruction_pointer = snapshot.entries[index].value;
            break;
        }
    }
    const std::uintptr_t stack_pointer = stack_pointer_from(snapshot);
    write_literal("unwind backend=registers init=0\n");
    write_literal("frame index=0 kind=fault pc=");
    write_hex(instruction_pointer);
    write_literal(" sp=");
    write_hex(stack_pointer);
    write_literal("\n");
    write_literal("unwind-status complete=0 frames=1 error=unsupported\n");
    return stack_pointer;
#endif
}

void write_stack_snapshot(std::uintptr_t sp) noexcept {
    if (g_mem_fd < 0 || sp == 0) {
        write_literal("stack-status available=0 bytes=0\n");
        return;
    }
    std::array<unsigned char, kStackChunkBytes> buffer{};
    std::size_t total = 0;
    while (total < kMaxStackBytes) {
        const ssize_t count = ::pread(g_mem_fd, buffer.data(), buffer.size(),
                                      static_cast<off_t>(sp + total));
        if (count <= 0) {
            break;
        }
        write_literal("stack-chunk address=");
        write_hex(sp + total);
        write_literal(" bytes=");
        write_unsigned(static_cast<std::size_t>(count));
        write_literal(" data=");
        write_hex_bytes(buffer.data(), static_cast<std::size_t>(count));
        write_literal("\n");
        total += static_cast<std::size_t>(count);
        if (static_cast<std::size_t>(count) < buffer.size()) {
            break;
        }
    }
    write_literal("stack-status available=");
    write_unsigned(total > 0 ? 1 : 0);
    write_literal(" bytes=");
    write_unsigned(total);
    write_literal(" truncated=");
    write_unsigned(total == kMaxStackBytes ? 1 : 0);
    write_literal("\n");
}

pid_t current_tid() noexcept {
#ifdef SYS_gettid
    return static_cast<pid_t>(::syscall(SYS_gettid));
#else
    return ::getpid();
#endif
}

[[noreturn]] void finish_with_signal(int signum) noexcept {
    if (g_report_fd >= 0) {
        (void)::fsync(g_report_fd);
        if (g_current_path[0] != '\0' && g_report_path[0] != '\0') {
            (void)::rename(g_current_path.data(), g_report_path.data());
        }
    }
    struct sigaction action {};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    (void)::sigaction(signum, &action, nullptr);
#ifdef SYS_tgkill
    (void)::syscall(SYS_tgkill, ::getpid(), current_tid(), signum);
#else
    (void)::kill(::getpid(), signum);
#endif
    _exit(128 + signum);
}

void crash_handler(int signum, siginfo_t* info, void* context_ptr) noexcept {
    if (__atomic_exchange_n(&g_handler_active, 1, __ATOMIC_RELAXED) != 0) {
        write_literal("nested-crash active=1\n=== KPBR-CRASH v1 END ===\n");
        _exit(128 + signum);
    }
    write_literal("\n=== KPBR-CRASH v1 BEGIN ===\n");
    if (g_metadata_length > 0) {
        write_bytes(g_metadata_line.data(), g_metadata_length);
    }
    write_literal("signal name=");
    write_cstr(signal_name(signum));
    write_literal(" number=");
    write_unsigned(static_cast<std::uintptr_t>(signum));
    write_literal(" code=");
    const int code = info == nullptr ? 0 : info->si_code;
    if (code < 0) {
        write_literal("-");
        write_unsigned(static_cast<std::uintptr_t>(-code));
    } else {
        write_unsigned(static_cast<std::uintptr_t>(code));
    }
    write_literal(" fault=");
    write_hex(info == nullptr ? 0 : reinterpret_cast<std::uintptr_t>(info->si_addr));
    write_literal("\nprocess pid=");
    write_unsigned(static_cast<std::uintptr_t>(::getpid()));
    write_literal(" tid=");
    write_unsigned(static_cast<std::uintptr_t>(current_tid()));
    write_literal("\n");

    const RegisterSnapshot registers = capture_registers(static_cast<ucontext_t*>(context_ptr));
    write_register_snapshot(registers);
    write_mappings();
    std::uintptr_t sp = stack_pointer_from(registers);
    if (sp != 0) {
        write_stack_snapshot(sp);
    }
    const std::uintptr_t unwind_sp = write_unwind(context_ptr);
    if (sp == 0) {
        write_stack_snapshot(unwind_sp);
    }
    write_literal("status nested=0\n=== KPBR-CRASH v1 END ===\n");
    finish_with_signal(signum);
}

void terminate_handler() noexcept {
    write_literal("terminate uncaught=1\n");
    (void)::raise(SIGABRT);
    _exit(128 + SIGABRT);
}

bool copy_path(std::array<char, 4096>& output, const std::string& value) noexcept {
    if (value.empty() || value.size() >= output.size()) {
        return false;
    }
    std::memcpy(output.data(), value.data(), value.size());
    output[value.size()] = '\0';
    return true;
}

std::string safe_meta(std::string value) {
    for (char& ch : value) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '=') {
            ch = '_';
        }
    }
    return value.empty() ? "unknown" : value;
}

} // namespace

bool install_for_current_thread() noexcept {
    if (g_alt_stack_installed) {
        return true;
    }
    stack_t stack{};
    stack.ss_sp = g_alt_stack.data();
    stack.ss_size = g_alt_stack.size();
    if (::sigaltstack(&stack, nullptr) != 0) {
        return false;
    }
    g_alt_stack_installed = true;
    return true;
}

bool initialize(const CrashReporterConfig& config) {
    keen_pbr_crash_symbolization_anchor();
    if (!copy_path(g_report_path, config.report_path) ||
        !copy_path(g_current_path, config.report_path + ".current")) {
        std::fprintf(stderr, "crash report path is empty or too long\n");
        return false;
    }

    struct stat partial {};
    if (::stat(g_current_path.data(), &partial) == 0 && partial.st_size > 0) {
        (void)::rename(g_current_path.data(), g_report_path.data());
    }
    g_report_fd = ::open(g_current_path.data(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
    if (g_report_fd < 0) {
        std::perror("open crash report");
    }
    g_maps_fd = ::open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    g_mem_fd = ::open("/proc/self/mem", O_RDONLY | O_CLOEXEC);

    const std::string metadata =
        "meta version=" + safe_meta(config.version) +
        " build=" + safe_meta(config.build) +
        " commit=" + safe_meta(config.commit) +
        " branch=" + safe_meta(config.branch) +
        " target=" + safe_meta(config.target_os) +
        " os_version=" + safe_meta(config.target_version) +
        " arch=" + safe_meta(config.architecture) +
        " variant=" + safe_meta(config.variant) + "\n";
    g_metadata_length = std::min(metadata.size(), g_metadata_line.size());
    std::memcpy(g_metadata_line.data(), metadata.data(), g_metadata_length);

    if (!install_for_current_thread()) {
        std::perror("sigaltstack");
        return false;
    }

    struct sigaction action {};
    action.sa_sigaction = crash_handler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigemptyset(&action.sa_mask);
    for (const int signum : kFatalSignals) {
        if (::sigaction(signum, &action, nullptr) != 0) {
            std::perror("sigaction");
            return false;
        }
    }
    return true;
}

void install_terminate_handler() noexcept {
    std::set_terminate(terminate_handler);
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
