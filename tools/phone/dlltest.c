#include <windows.h>
#include <stdio.h>
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        printf("loading %s...\n", argv[i]); fflush(stdout);
        HMODULE h = LoadLibraryA(argv[i]);
        printf("  %s -> %p (err %lu)\n", argv[i], h, h ? 0 : GetLastError()); fflush(stdout);
    }
    return 0;
}
