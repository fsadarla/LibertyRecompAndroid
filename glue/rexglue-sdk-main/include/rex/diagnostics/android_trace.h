#pragma once

// Android system-trace (ATrace) markers for the audio pipeline. They appear in
// a Perfetto or System Tracing capture of a debuggable build and cost one
// ATrace_isEnabled() check when no trace is running. Other platforms compile
// the markers to nothing.

#include <cstdint>

#if defined(__ANDROID__)
#include <dlfcn.h>
#endif

namespace rex::diagnostics::android_trace {

// Set on the audio worker while it runs the title's mixer callback, so guest
// waits made from inside the callback can be marked without tracing every
// guest thread.
inline thread_local bool in_audio_callback = false;

#if defined(__ANDROID__)

namespace detail {

struct Api {
  bool (*is_enabled)() = nullptr;
  void (*begin_section)(const char*) = nullptr;
  void (*end_section)() = nullptr;
  void (*set_counter)(const char*, int64_t) = nullptr;
};

// Resolved at runtime: ATrace_setCounter needs API 29, above the native
// build's minimum API level.
inline const Api& GetApi() {
  static const Api api = [] {
    Api result;
    if (void* library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL)) {
      result.is_enabled = reinterpret_cast<bool (*)()>(dlsym(library, "ATrace_isEnabled"));
      result.begin_section =
          reinterpret_cast<void (*)(const char*)>(dlsym(library, "ATrace_beginSection"));
      result.end_section = reinterpret_cast<void (*)()>(dlsym(library, "ATrace_endSection"));
      result.set_counter =
          reinterpret_cast<void (*)(const char*, int64_t)>(dlsym(library, "ATrace_setCounter"));
    }
    if (!result.is_enabled || !result.begin_section || !result.end_section) {
      result = {};
    }
    return result;
  }();
  return api;
}

}  // namespace detail

inline bool Enabled() {
  const auto& api = detail::GetApi();
  return api.is_enabled && api.is_enabled();
}

inline void SetCounter(const char* name, int64_t value) {
  const auto& api = detail::GetApi();
  if (api.set_counter && api.is_enabled && api.is_enabled()) {
    api.set_counter(name, value);
  }
}

// A trace section for the current scope. A null name records nothing.
class Section {
 public:
  explicit Section(const char* name) : active_(name && Enabled()) {
    if (active_) {
      detail::GetApi().begin_section(name);
    }
  }
  ~Section() {
    if (active_) {
      detail::GetApi().end_section();
    }
  }
  Section(const Section&) = delete;
  Section& operator=(const Section&) = delete;

 private:
  bool active_;
};

#else

inline bool Enabled() { return false; }
inline void SetCounter(const char*, int64_t) {}

class Section {
 public:
  explicit Section(const char*) {}
  Section(const Section&) = delete;
  Section& operator=(const Section&) = delete;
};

#endif

// Section name for a guest wait: the given name inside the audio callback,
// otherwise null so the wait is not recorded.
inline const char* AudioCallbackWaitName(const char* name) {
  return in_audio_callback ? name : nullptr;
}

}  // namespace rex::diagnostics::android_trace
