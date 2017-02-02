#include <stdio.h>
#include <stdlib.h>

int c;

int addc(int a, int b)
{
  return a + b + c;
}

int main(int argc, char* argv[])
{
  if(argc < 2)
  {
    printf("Please, enter an integer.\n");
    return 0;
  }

  int a = atoi(argv[1]);
  int b = atoi(argv[1]);
  c = 3;

  int sum = addc(a, b);

  printf("%d + %d + %c = %d\n", a, b, c, sum);
}
