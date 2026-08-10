import re

with open('src/BionicShim.cpp', 'r') as f:
    content = f.read()

wrappers = """
struct android_sigaction {
    union {
        void (*sa_handler)(int);
        void (*sa_sigaction)(int, void*, void*);
    };
    uint64_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

#include <signal.h>

extern "C" int bionic_sigaction(int signum, const struct android_sigaction* act, struct android_sigaction* oldact) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "sigaction(signum=%d)", signum);
    trace(buf);
    
    // For now, just ignore it to prevent it from crashing the host handler,
    // but returning 0 makes IL2CPP think it succeeded.
    // If it's SIGSEGV (11), IL2CPP will crash on intentional null deref.
    return 0;
}
"""

content = content.replace('extern "C" int bionic_pthread_create', wrappers + '\nextern "C" int bionic_pthread_create')

symbols = """
    {"sigaction", reinterpret_cast<void*>(&bionic_sigaction)},
"""
content = content.replace('{"pthread_cond_init"', symbols.strip() + '\n    {"pthread_cond_init"')

with open('src/BionicShim.cpp', 'w') as f:
    f.write(content)
