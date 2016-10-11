#include <unistd.h>
#include <fcntl.h>
 
int main(int argc, char **argv)
{
    int filedesc = open(argv[1], O_RDWR);
    if(filedesc < 0)
	{
        	return 1;
	}

    return 0;
}
