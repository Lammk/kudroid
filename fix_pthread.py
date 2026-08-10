import re

with open('src/BionicShim.cpp', 'r') as f:
    content = f.read()

# 1. Add new wrappers
wrappers = """
extern "C" int bionic_pthread_mutex_trylock(void* guestMutex) {
    pthread_mutex_t* hostMutex = findGuestMutex(guestMutex);
    return hostMutex ? ::pthread_mutex_trylock(hostMutex) : -1;
}

extern "C" int bionic_pthread_key_create(void* guestKey, void (*destructor)(void*)) {
    pthread_key_t hostKey;
    int res = ::pthread_key_create(&hostKey, destructor);
    if (res == 0) {
        *static_cast<int*>(guestKey) = static_cast<int>(hostKey);
    }
    return res;
}
extern "C" void* bionic_pthread_getspecific(int guestKey) {
    return ::pthread_getspecific(static_cast<pthread_key_t>(guestKey));
}
extern "C" int bionic_pthread_setspecific(int guestKey, const void* value) {
    return ::pthread_setspecific(static_cast<pthread_key_t>(guestKey), value);
}
extern "C" int bionic_pthread_key_delete(int guestKey) {
    return ::pthread_key_delete(static_cast<pthread_key_t>(guestKey));
}

extern "C" int bionic_pthread_once(int* guest_once, void (*init_routine)(void)) {
    static pthread_mutex_t once_lock = PTHREAD_MUTEX_INITIALIZER;
    ::pthread_mutex_lock(&once_lock);
    if (*guest_once == 0) {
        *guest_once = 1;
        init_routine();
    }
    ::pthread_mutex_unlock(&once_lock);
    return 0;
}
"""

# Insert wrappers before bionic_pthread_create
content = content.replace('extern "C" int bionic_pthread_create', wrappers + '\nextern "C" int bionic_pthread_create')

# 2. Add missing symbols to kSymbols
symbols_to_add = """
    {"pthread_mutexattr_settype", reinterpret_cast<void*>(&bionic_pthread_mutexattr_settype)},
    {"pthread_mutex_trylock", reinterpret_cast<void*>(&bionic_pthread_mutex_trylock)},
    {"pthread_key_create", reinterpret_cast<void*>(&bionic_pthread_key_create)},
    {"pthread_getspecific", reinterpret_cast<void*>(&bionic_pthread_getspecific)},
    {"pthread_setspecific", reinterpret_cast<void*>(&bionic_pthread_setspecific)},
    {"pthread_key_delete", reinterpret_cast<void*>(&bionic_pthread_key_delete)},
    {"pthread_once", reinterpret_cast<void*>(&bionic_pthread_once)},
"""
content = content.replace('{"pthread_cond_init"', symbols_to_add.strip() + '\n    {"pthread_cond_init"')

with open('src/BionicShim.cpp', 'w') as f:
    f.write(content)
