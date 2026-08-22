// Simple test shared library for kudroid ELF loader testing.
// Build: aarch64-linux-gnu-gcc -shared -fPIC -o test_lib_arm64.so test_lib.c
// Or native: gcc -shared -fPIC -o test_lib.so test_lib.c

// Exported symbol for kudroid execution test
int kudroid_add(int a, int b) {
    return a + b;
}

int add(int a, int b) {
    return a + b;
}

int get_version(void) {
    return 42;
}

const char* hello(void) {
    return "Hello from test_lib.so!";
}