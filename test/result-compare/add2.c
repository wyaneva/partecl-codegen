#include <stdio.h>
#include <stdlib.h>

int add(int a, int b)
{
  return a + b;
}

int main(int argc, char* argv[])
{
  if(argc < 3)
  {
    printf("Please, enter two integers to sum.\n");
    return 0;
  }
  int a = atoi(argv[1]);
  int b = atoi(argv[2]);

  int sum = add(a, b);

  printf("sum = %d\n", sum);
}
