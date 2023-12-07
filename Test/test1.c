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
  // int z;
  //   int input;
  //   scanf("%d", &input);
  //   int factor = 5;
  //   int output;

  //   switch(input) {
  //     case 0:
  //       output = factor * input;
  //       break;
  //     case 1:
  //       output = factor * input;
  //       break;
  //     case 2:
  //       output = factor * input;
  //       break;
  //     case 3:
  //       output = factor * input;
  //       break;
  //     case 4:
  //       output = factor * input;
  //       break;
  //     case 5:
  //       output = factor * input;
  //       break;
  //     case 6:
  //       output = factor * input;
  //       break;
  //     default:
  //       output = factor * input;
  //   }

  //   printf("Output: %d\n", output);

  //   return 0;
  int b;
  int e;
  int g;
  int c;
  int a;

  // scanf("%d", &limit);

  for (int i = 0; i < 2; i++) {
    a = i;
    b = e + 1;

    if (i % 2 == 0) {
      c = a + 2;
      int d = b + c;
      e = d - 3;
    } else {
      int f = a * b;
      e = f + c;
    }
    g = a + e;
  }

  printf("The value of g is: %d\n", g);
  // printf("The value of limit is: %d\n", limit);

  return 0;
}