#include <stdio.h>

int main() {
    int a = 3;
    for (int i = 0; i < 10; ++i) {
        a += i;
    }
    printf("%d\n", a);
    return 0;
}
