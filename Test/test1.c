// #include <stdio.h>

// int main() {
//   int a = 3;
//   for (int i = 0; i < 10; ++i) {
//     a += i;
//   }
//   printf("%d\n", a);
//   return 0;
// }

#include <stdio.h>

int main() {
    int z;
    int x = 2;
    int y = 10;
    // scanf("%d", &x);
    int a[100];

    for (int i = 0; i < 100; i++) {
      a[i] = i;
    }

    if (x < y) {
        z = x + y;
    } else {
        z = x - y;
    }

    printf("The value of z is: %d\n", z);

    return 0;
}