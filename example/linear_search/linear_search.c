#include <stdio.h>
#include <stdlib.h>

int find(int array[], int n, int number) {
  for (int i = 0; i < n; i++) {
    if (number == array[i]) {
      return i;
    }
  }
  return -1;
}

int main(int argc, char **argv) {

  // input error checking is omitted for brevity

  int n = atoi(argv[1]);
  int array[n];

  printf("Enter %d array numbers.\n", n);
  for (int i = 0; i < n; i++) {
    scanf("%d", array + i);
  }
  printf("Enter a number to find.\n");
  int number;
  scanf("%d", &number);

  // perform a search
  int idx = find(array, n, number);

  // output answer
  printf("Number found at idx: %d\n", idx);
}
