#include <stdio.h>

int main() {
    int z;
    int x;
    int y = 10;
    scanf("%d", &x);
    if (x < y) {
        z = x + y;
    } else {
        z = x - y;
    }
    printf("The value of z is: %d\n", z);
    return 0;
}