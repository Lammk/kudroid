#include <string>
#include <cstring>

extern "C" const char* kudroid_test_gpu(void) {
    std::string log = "[kudroid_gpu] GPU Direct Intercept Test is now obsolete. Please use test_triangle.apk.\n";
    return strdup(log.c_str());
}
