//
// Test file for format string checker
//

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define __fmt_format(str_idx, first_arg) \
      __attribute__((annotate("fmt_format:" #str_idx ":" #first_arg)))

// Test the clang-tidy options for custom format functions
void kprintf(const char *format, ...);
void panic(const char *fmt, ...);
void log_error(const char *file, int line, const char *fmt, ...);

// Test the format attributes for dynamic analysis
int fmt_format(char *buf, size_t size, const char *format, ...) __fmt_format(3, 4);
void debug(int level, const char *fmt, ...) __fmt_format(2, 3);

// Test custom type formats registered in the clang-tidy options
typedef struct test {

} test_t;

struct thread {

};

void test_printf_formats() {
    int x = 42;
    const char *str = "hello";
    size_t sz = 100;
    ssize_t ssz = -100;
    
    // Valid printf formats
    kprintf("Simple string\n");
    kprintf("Integer: %d\n", x);
    kprintf("String: %s, Number: %d\n", str, x);
    kprintf("Hex: %x, Pointer: %p\n", x, &x);
    kprintf("%zd\n", ssz);
    kprintf("%u%u\n", x, x);
    
    // Extended format specifiers from fmtlib
    kprintf("Long long: %lld\n", (long long)x);
    kprintf("Size_t: %zu\n", sz);
    kprintf("Binary: %b\n", x);
    kprintf("Memory: %M\n", sz);
    
    // These should now generate errors since custom types are rust-style only
    kprintf("Error: %err\n", -5);  // Should error - custom types not allowed in printf
    kprintf("Process: %pr\n", (void*)0);  // Should error - custom types not allowed in printf
    
    // Invalid printf formats - should trigger warnings
    kprintf("Missing argument: %d\n");  // Missing argument
    kprintf("Extra argument: %d\n", x, x);  // Extra argument
    kprintf("Invalid specifier: %q\n", x);  // Invalid specifier
    kprintf("Incomplete specifier: %");  // Incomplete
}

void test_fmt_formats() {
    int a = 1, b = 2, c = 3;
    char buffer[256];
    
    // Valid fmt-style formats
    fmt_format(buffer, 256, "Simple {}", a);
    fmt_format(buffer, 256, "Multiple {} and {}", a, b);
    fmt_format(buffer, 256, "Indexed {1} then {0}", a, b);
    fmt_format(buffer, 256, "Formatted {:d}", a);
    fmt_format(buffer, 256, "Aligned {:>10}", a);
    fmt_format(buffer, 256, "Fill and align {:$_^10}", a);
    
    // Type specifiers
    fmt_format(buffer, 256, "Binary: {:b}", a);
    fmt_format(buffer, 256, "Hex: {:x}", a);
    fmt_format(buffer, 256, "Upper hex: {:!x}", a);  // ! flag for uppercase
    
    // Custom types - ONLY allowed in fmt-style format
    test_t t;
    struct thread td;
    fmt_format(buffer, 256, "Custom int type: {:err}", -5);
    fmt_format(buffer, 256, "Custom int type: {:err}", "invalid"); // Should warn - type mismatch
    fmt_format(buffer, 256, "Custom struct type: {:test}", &t);
    fmt_format(buffer, 256, "Custom struct type: {:test}", t); // Should warn - type mismatch
    fmt_format(buffer, 256, "Path: {:path}", (void*)0);
    fmt_format(buffer, 256, "Process: {:pr}", (void*)0x1000);
    fmt_format(buffer, 256, "Thread: {:td}", &td);
    fmt_format(buffer, 256, "Thread: {:td}", td); // Should warn - type mismatch
    fmt_format(buffer, 256, "Vnode: {:vn}", (void*)0x2000);
    
    // Format-only specifiers (no type, just formatting)
    fmt_format(buffer, 256, "Padded: {:$=^49}", a);  // Should not warn about args
    fmt_format(buffer, 256, "Right align: {:>10}", a);
    fmt_format(buffer, 256, "Zero pad: {:010}", a);
    
    // Invalid fmt-style formats - should trigger warnings
    fmt_format(buffer, 256, "Missing close {", a);  // Unclosed
    fmt_format(buffer, 256, "Missing arg {}");  // Missing argument
    fmt_format(buffer, 256, "Index out of range {2}", a, b);  // Bad index
}

void test_mixed_formats() {
    int x = 10;
    char buffer[256];
    
    // Mixed format styles in same string
    fmt_format(buffer, 256, "Printf: %d, Rust: {}", x, x);
    fmt_format(buffer, 256, "Escaped: %% and {{}}");
    
    // Dynamic width and precision
    fmt_format(buffer, 256, "{:*}", 5, x);  // Width from arg
    fmt_format(buffer, 256, "{:.*}", 3, x);  // Precision from arg
    fmt_format(buffer, 256, "{:*1}", x, 10);  // Width from arg 1
}

void test_format_attributes() {
    // Test functions with format attributes
    debug(1, "Debug message: %d", 42);  // Valid
    debug(1, "Missing arg: %d");  // Should warn - missing argument
    
    log_error(__FILE__, __LINE__, "Error: %s at %d", "test", 100);  // Valid
    log_error(__FILE__, __LINE__, "Bad: %d %s", 42);  // Should warn - missing arg
    
    panic("Critical error: %s", "out of memory");  // Valid
    panic("Bad format: %q", 123);  // Should warn - invalid specifier
}

void test_type_checking() {
    int x = 42;
    float f = 3.14f;
    const char *str = "hello";
    void *ptr = (void*)0x1000;
    size_t sz = 100;
    
    // Valid type matches
    kprintf("Integer: %d\n", x);
    kprintf("Float: %f\n", f);
    kprintf("String: %s\n", str);
    kprintf("Pointer: %p\n", ptr);
    kprintf("Size: %zu\n", sz);
    kprintf("Invalid: %q\n", sz);
    
    // Type mismatches - should trigger warnings
    kprintf("Wrong: %d\n", f);      // float to %d
    kprintf("Wrong: %s\n", x);      // int to %s
    kprintf("Wrong: %f\n", str);    // string to %f
    kprintf("Wrong: %d\n", ptr);    // pointer to %d
    
    // Rust-style type mismatches
    char buffer[256];
    fmt_format(buffer, 256, "Wrong: {:d}", f);    // float to {:d}
    fmt_format(buffer, 256, "Wrong: {:s}", x);    // int to {:s}
    fmt_format(buffer, 256, "Wrong: {:f}", str);  // string to {:f}
    
    // Custom types - these should all error now since % format doesn't support custom types
    kprintf("Process: %pr\n", ptr);  // Should error - custom types not in printf
    kprintf("Error: %err\n", x);     // Should error - custom types not in printf
    kprintf("Wrong: %pr\n", x);      // Should error - custom types not in printf
    kprintf("Wrong: %err\n", ptr);   // Should error - custom types not in printf
    
    // Correct way - custom types in rust-style format
    char buffer2[256];
    fmt_format(buffer2, 256, "Process: {:pr}", ptr);   // Valid - pointer to custom type
    fmt_format(buffer2, 256, "Error: {:err}", x);      // Valid - int to error code
    fmt_format(buffer2, 256, "Wrong: {:pr}", x);       // Invalid - int to custom pointer type
    fmt_format(buffer2, 256, "Wrong: {:err}", ptr);    // Invalid - pointer to error code
}
