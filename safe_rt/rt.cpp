#include <iostream>

extern "C" {

// Export as C function symbols
// Place all function prototypes here
void test();

}

void test() {
    std::cout << "TEST!!" << std::endl;
}
