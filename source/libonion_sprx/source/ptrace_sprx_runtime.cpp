/* Copyright (C) 2026 OnionHEN / LightningMods */

#include <onion/sprx_loader.hpp>

#include <onion/log.h>
#include <onion/proc_query.h>
#include <onion/pt.h>
#include <onion/ucred.h>

#include <ps5/kernel.h>
#include <ps5/mdbg.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

namespace onion::sprx {
namespace {

constexpr char kLoadStartModuleNid[] = "wzvqT4UqKX8";
constexpr char kPthreadCreateNid[] = "OxhIB8LB-PQ";
constexpr char kDlsymNid[] = "LwG8g3niqwA"; // sceKernelDlsym
constexpr size_t kPageSize = 0x1000;
constexpr size_t kContextOffsetPath = 0x100;
constexpr size_t kContextOffsetPluginLoad = 0x80;
constexpr size_t kContextOffsetPluginUnload = 0xC0;
constexpr int64_t kPendingResult = INT64_MIN;

/*
 * pthread start routine.  Its single argument is a RemoteContext pointer.
 *   r = sceKernelLoadStartModule(path, 0, 0, 0, 0, 0)
 *   if (r >= 0 && dlsym && dlsym(r, "plugin_load", &fn) == 0) {
 *     plugin_result = fn(0, NULL);
 *     if (plugin_result && dlsym(r, "plugin_unload", &fn) == 0) fn(0, NULL);
 *   }
 *   result = r          // published last: the host frees the pages after it
 * Assembled from intel syntax with clang -arch x86_64; GoldHEN's loader does
 * the same calls in the same order.  No code stays behind in the target.
 */
constexpr std::array<uint8_t, 129> kLoadThreadStub = {
    0x55,                               // push rbp
    0x48, 0x89, 0xE5,                   // mov rbp, rsp
    0x53,                               // push rbx
    0x48, 0x83, 0xE4, 0xF0,             // and rsp, -16
    0x48, 0x83, 0xEC, 0x10,             // sub rsp, 0x10
    0x48, 0x89, 0xFB,                   // mov rbx, rdi
    0x48, 0x8B, 0x7B, 0x08,             // mov rdi, [rbx+0x08]  path
    0x31, 0xF6,                         // xor esi, esi
    0x31, 0xD2,                         // xor edx, edx
    0x31, 0xC9,                         // xor ecx, ecx
    0x45, 0x31, 0xC0,                   // xor r8d, r8d
    0x45, 0x31, 0xC9,                   // xor r9d, r9d
    0xFF, 0x13,                         // call [rbx]           load_start_module
    0x48, 0x63, 0xC0,                   // movsxd rax, eax
    0x48, 0x89, 0x04, 0x24,             // mov [rsp], rax
    0x85, 0xC0,                         // test eax, eax
    0x78, 0x43,                         // js done
    0x48, 0x83, 0x7B, 0x18, 0x00,       // cmp qword [rbx+0x18], 0
    0x74, 0x3C,                         // je done
    0x89, 0xC7,                         // mov edi, eax         handle
    0x48, 0x8B, 0x73, 0x20,             // mov rsi, [rbx+0x20]  "plugin_load"
    0x48, 0x8D, 0x53, 0x38,             // lea rdx, [rbx+0x38]
    0xFF, 0x53, 0x18,                   // call [rbx+0x18]      dlsym
    0x85, 0xC0,                         // test eax, eax
    0x75, 0x2B,                         // jne done
    0x31, 0xFF,                         // xor edi, edi
    0x31, 0xF6,                         // xor esi, esi
    0xFF, 0x53, 0x38,                   // call [rbx+0x38]      plugin_load(0, NULL)
    0x48, 0x63, 0xC0,                   // movsxd rax, eax
    0x48, 0x89, 0x43, 0x30,             // mov [rbx+0x30], rax
    0x85, 0xC0,                         // test eax, eax
    0x74, 0x19,                         // je done
    0x8B, 0x3C, 0x24,                   // mov edi, [rsp]       handle
    0x48, 0x8B, 0x73, 0x28,             // mov rsi, [rbx+0x28]  "plugin_unload"
    0x48, 0x8D, 0x53, 0x38,             // lea rdx, [rbx+0x38]
    0xFF, 0x53, 0x18,                   // call [rbx+0x18]      dlsym
    0x85, 0xC0,                         // test eax, eax
    0x75, 0x07,                         // jne done
    0x31, 0xFF,                         // xor edi, edi
    0x31, 0xF6,                         // xor esi, esi
    0xFF, 0x53, 0x38,                   // call [rbx+0x38]      plugin_unload(0, NULL)
    0x48, 0x8B, 0x04, 0x24,             // done: mov rax, [rsp]
    0x48, 0x89, 0x43, 0x10,             // mov [rbx+0x10], rax  result
    0x48, 0x8B, 0x5D, 0xF8,             // mov rbx, [rbp-8]
    0x48, 0x89, 0xEC,                   // mov rsp, rbp
    0x5D,                               // pop rbp
    0xC3                                // ret
};

struct RemoteContext {
  uint64_t load_start_module; // 0x00
  uint64_t path;              // 0x08
  int64_t result;             // 0x10 module handle or error, written last
  uint64_t dlsym;             // 0x18 0 = plain load (catalog.ini entries)
  uint64_t plugin_load_name;  // 0x20
  uint64_t plugin_unload_name;// 0x28
  int64_t plugin_result;      // 0x30 stays kPendingResult if not called
  uint64_t symbol;            // 0x38 dlsym output
};

static_assert(sizeof(RemoteContext) == 64, "unexpected remote context size");
static_assert(sizeof(RemoteContext) <= kContextOffsetPluginLoad,
              "context overlaps the symbol names");

class ScopedPtrace {
public:
  explicit ScopedPtrace(pid_t pid) noexcept : pid_(pid) {
    previous_authid_ = set_ucred_to_ptrace();
    if (previous_authid_ == 0)
      return;
    if (pt_attach(pid_) == 0)
      attached_ = true;
  }

  ScopedPtrace(const ScopedPtrace &) = delete;
  ScopedPtrace &operator=(const ScopedPtrace &) = delete;

  ~ScopedPtrace() noexcept {
    detach();
    if (previous_authid_ != 0)
      (void)set_proc_authid(getpid(), previous_authid_);
  }

  bool ready() const noexcept { return attached_; }

  bool attach() noexcept {
    if (attached_)
      return true;
    if (pt_attach(pid_) != 0)
      return false;
    attached_ = true;
    return true;
  }

  bool detach() noexcept {
    if (!attached_)
      return true;
    const bool ok = pt_detach(pid_, 0) == 0;
    attached_ = false;
    return ok;
  }

private:
  pid_t pid_;
  uintptr_t previous_authid_ = 0;
  bool attached_ = false;
};

LoadResult result_with(LoadStatus status, pid_t pid) noexcept {
  LoadResult result;
  result.status = status;
  result.pid = pid;
  result.underlying_status = status;
  return result;
}

std::string make_string(std::string_view value) {
  return std::string(value.data(), value.size());
}

} // namespace

bool PtraceSprxRuntime::find_loaded(pid_t pid, std::string_view module_name,
                                    ModuleInfo *out) const noexcept {
  if (pid <= 1 || module_name.empty() || !out)
    return false;
  const std::string name = make_string(module_name);
  uint32_t handle = 0;
  if (kernel_dynlib_handle(pid, name.c_str(), &handle) != 0 || handle == 0)
    return false;
  out->handle = handle;
  return true;
}

LoadResult PtraceSprxRuntime::load(pid_t pid, std::string_view path,
                                   std::string_view module_name,
                                   const LoadOptions &options) noexcept {
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);

  if (pid <= 1 || path.empty() || module_name.empty())
    return result_with(LoadStatus::InvalidArgument, pid);
  if (options.timeout_ms == 0 || options.poll_ms == 0 ||
      options.poll_ms > options.timeout_ms)
    return result_with(LoadStatus::InvalidArgument, pid);
  if (!onion_proc_is_alive(pid)) {
    LOG_WARN("sprx: target not alive pid=%d", static_cast<int>(pid));
    return result_with(LoadStatus::TargetNotFound, pid);
  }

  ScopedPtrace session(pid);
  if (!session.ready()) {
    LOG_ERROR("sprx: ptrace attach failed pid=%d", static_cast<int>(pid));
    return result_with(LoadStatus::AttachFailed, pid);
  }

  const intptr_t load_fn = pt_resolve(pid, kLoadStartModuleNid);
  const intptr_t pthread_create_fn = pt_resolve(pid, kPthreadCreateNid);
  const intptr_t dlsym_fn =
      options.call_plugin_load ? pt_resolve(pid, kDlsymNid) : 0;
  if (load_fn == 0 || pthread_create_fn == 0 ||
      (options.call_plugin_load && dlsym_fn == 0)) {
    LOG_ERROR("sprx: required NID resolution failed pid=%d load=%p pthread=%p dlsym=%p",
              static_cast<int>(pid), reinterpret_cast<void *>(load_fn),
              reinterpret_cast<void *>(pthread_create_fn),
              reinterpret_cast<void *>(dlsym_fn));
    return result_with(LoadStatus::ResolveFailed, pid);
  }

  const intptr_t context_page = pt_mmap(
      pid, 0, kPageSize, PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  const intptr_t code_page = pt_mmap(
      pid, 0, kPageSize, PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  const intptr_t thread_page = pt_mmap(
      pid, 0, kPageSize, PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (context_page <= 0 || code_page <= 0 || thread_page <= 0) {
    bool cleanup_ok = true;
    if (context_page > 0)
      cleanup_ok = pt_munmap(pid, context_page, kPageSize) == 0 && cleanup_ok;
    if (code_page > 0)
      cleanup_ok = pt_munmap(pid, code_page, kPageSize) == 0 && cleanup_ok;
    if (thread_page > 0)
      cleanup_ok = pt_munmap(pid, thread_page, kPageSize) == 0 && cleanup_ok;
    LoadResult result = result_with(LoadStatus::AllocationFailed, pid);
    result.cleanup_ok = cleanup_ok;
    if (!cleanup_ok)
      LOG_ERROR("sprx: allocation rollback failed pid=%d", static_cast<int>(pid));
    return result;
  }

  const auto cleanup = [&]() noexcept {
    bool ok = true;
    ok = pt_munmap(pid, context_page, kPageSize) == 0 && ok;
    ok = pt_munmap(pid, code_page, kPageSize) == 0 && ok;
    ok = pt_munmap(pid, thread_page, kPageSize) == 0 && ok;
    return ok;
  };

  const auto fail_with_cleanup = [&](LoadStatus status) noexcept {
    LoadResult result = result_with(status, pid);
    result.cleanup_ok = cleanup();
    if (!result.cleanup_ok)
      LOG_ERROR("sprx: cleanup failed pid=%d status=%s", static_cast<int>(pid),
                load_status_name(status));
    return result;
  };

  RemoteContext context{};
  context.load_start_module = static_cast<uint64_t>(load_fn);
  context.path = static_cast<uint64_t>(context_page + kContextOffsetPath);
  context.result = kPendingResult;
  context.dlsym = static_cast<uint64_t>(dlsym_fn);
  context.plugin_load_name =
      static_cast<uint64_t>(context_page + kContextOffsetPluginLoad);
  context.plugin_unload_name =
      static_cast<uint64_t>(context_page + kContextOffsetPluginUnload);
  context.plugin_result = kPendingResult;
  static constexpr char kPluginLoad[] = "plugin_load";
  static constexpr char kPluginUnload[] = "plugin_unload";
  const std::string path_copy = make_string(path);
  if (path_copy.size() + 1 > kPageSize - kContextOffsetPath ||
      pt_copyin(pid, &context, context_page, sizeof(context)) != 0 ||
      pt_copyin(pid, kPluginLoad, context.plugin_load_name,
                sizeof(kPluginLoad)) != 0 ||
      pt_copyin(pid, kPluginUnload, context.plugin_unload_name,
                sizeof(kPluginUnload)) != 0 ||
      pt_copyin(pid, path_copy.c_str(), context.path, path_copy.size() + 1) != 0 ||
      pt_copyin(pid, kLoadThreadStub.data(), code_page,
                kLoadThreadStub.size()) != 0) {
    return fail_with_cleanup(LoadStatus::WriteFailed);
  }

  if (pt_mprotect(pid, code_page, kPageSize, PROT_READ | PROT_EXEC) != 0) {
    return fail_with_cleanup(LoadStatus::ProtectFailed);
  }

  const long thread_rc = pt_call(
      pid, pthread_create_fn, static_cast<uint64_t>(thread_page), 0,
      static_cast<uint64_t>(code_page), static_cast<uint64_t>(context_page),
      0, 0);
  if (thread_rc != 0) {
    LoadResult result = result_with(LoadStatus::ThreadCreateFailed, pid);
    result.remote_result = thread_rc;
    result.cleanup_ok = cleanup();
    if (!result.cleanup_ok)
      LOG_ERROR("sprx: cleanup failed pid=%d status=%s", static_cast<int>(pid),
                load_status_name(result.status));
    return result;
  }
  LOG_INFO("sprx: remote loader thread started pid=%d module=%.*s",
           static_cast<int>(pid), static_cast<int>(module_name.size()),
           module_name.data());
  if (!session.detach()) {
    LOG_ERROR("sprx: detach failed after thread creation pid=%d",
              static_cast<int>(pid));
    LoadResult result = result_with(LoadStatus::AttachFailed, pid);
    result.cleanup_ok = false;
    return result;
  }

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(options.timeout_ms);
  ModuleInfo info;
  bool loaded = false;
  while (std::chrono::steady_clock::now() < deadline) {
    if (!onion_proc_is_alive(pid))
      break;
    if (find_loaded(pid, module_name, &info)) {
      loaded = true;
      /* plugin_load runs after the module shows up: wait for the result. */
      int64_t published = kPendingResult;
      if (!options.call_plugin_load ||
          (mdbg_copyout(pid, context_page + offsetof(RemoteContext, result),
                        &published, sizeof(published)) == 0 &&
           published != kPendingResult))
        break;
    }
    const uint64_t poll_us =
        std::min<uint64_t>(static_cast<uint64_t>(options.poll_ms) * 1000ULL,
                           5000000ULL);
    usleep(static_cast<useconds_t>(poll_us));
  }

  LoadResult result = result_with(
      loaded ? LoadStatus::Loaded
             : (onion_proc_is_alive(pid) ? LoadStatus::Timeout
                                         : LoadStatus::TargetExited),
      pid);
  if (loaded)
    result.module_handle = info.handle;

  /* Reattach only after the loader thread has published its return value. */
  if (session.attach()) {
    RemoteContext finished{};
    finished.result = kPendingResult;
    const bool read_ok =
        pt_copyout(pid, context_page, &finished, sizeof(finished)) == 0;
    if (read_ok && finished.result != kPendingResult)
      result.remote_result = finished.result;
    if (read_ok && finished.plugin_result != kPendingResult) {
      result.plugin_load_called = true;
      result.plugin_load_result = static_cast<int32_t>(finished.plugin_result);
    }
    if (read_ok && finished.result != kPendingResult)
      result.cleanup_ok = cleanup();
    else
      result.cleanup_ok = false; // never unmap code while the thread runs
    if (!session.detach())
      result.cleanup_ok = false;
    if (!result.cleanup_ok)
      LOG_ERROR("sprx: post-load cleanup failed pid=%d status=%s",
                static_cast<int>(pid), load_status_name(result.status));
  } else {
    LOG_ERROR("sprx: reattach failed; retaining temporary mappings pid=%d",
              static_cast<int>(pid));
    result.cleanup_ok = false;
  }

  return result;
}

} // namespace onion::sprx
