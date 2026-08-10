#include <iostream>
#include <pthread.h>
int main() {
    std::cout << "mutex: " << sizeof(pthread_mutex_t) << "\n";
    std::cout << "cond: " << sizeof(pthread_cond_t) << "\n";
    std::cout << "rwlock: " << sizeof(pthread_rwlock_t) << "\n";
    return 0;
}
