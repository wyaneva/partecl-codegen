#include <stdlib.h>
#include <stdio.h>

int add(int a, int b){
  return a + b;
}

int main(int argc, char* argv[])
{
  if(argc <= 2)
  {
    printf("Please provide two integers to add.\n");
    return 0;
  }

  int a = atoi(argv[1]);
  int b = atoi(argv[2]);

  printf("%d + %d = %d\n", a, b, add(a, b));
}

