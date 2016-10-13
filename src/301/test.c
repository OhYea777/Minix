#include <fcntl.h>
#include <stdio.h>

int main(int argc, char **argv)
{
  int filedesc = open(argv[1], O_RDWR);

  printf("Trying to open file: %s\n", argv[1]);

  if (filedesc < 0) {
    printf("Failed to open file\n");

    return 1;
  }

  printf("Opened file\n");

  return 0;
}

