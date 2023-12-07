#include <stdio.h>

int main() {
    int z;
    int x = 2;
    int y = 10;
    if (x < y) {
        z = x + y;
    } else {
        z = x - y;
    }
    printf("The value of z is: %d\n", z);
    return 0;
}


