#include <stdio.h>
#include <stdlib.h>

void add_one(int array1[], int array2[], int n) {
  for (int i = 0; i < n; i++) {
    array2[i] = array1[i] + 1;
  }
}

int main(int argc, char **argv) {

  // input error checking is omitted for brevity

  int n = atoi(argv[1]);
  int array1[n];
  int array2[n];

  printf("Enter %d array numbers.\n", n);
  for (int i = 0; i < n; i++) {
    scanf("%d", array1 + i);
  }

  // add one
  add_one(array1, array2, n);

  // output answer
  for (int i = 0; i < n; i++) {
    printf("%d ", array2[i]);
  }
  printf("\n");
}
