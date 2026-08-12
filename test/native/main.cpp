#include <cstdio>
#include <cstring>

#include "tiny_test.h"

int main(int argc, char** argv) {
    bool verbose = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        }
    }

    extern void datanetTestSetSerialEcho(bool);
    datanetTestSetSerialEcho(verbose);

    std::printf("DataNet native test suite\n\n");
    return tinytest::run();
}
