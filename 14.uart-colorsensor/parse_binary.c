#include <stdio.h>
#include <string.h>


int main()
{
	FILE *fp;
	unsigned int address = 0x00;
        unsigned char buffer[1024];
        fp = fopen("1.bin","rb");
	size_t number;
        number = fread(buffer,  1,1024, fp);
	printf("read 1.bin file length %d\n",number);
		
	printf("0x%02x  ",address);
	number = 0x138;
	for(int i=0; i<number;i++)
	{
		printf("0x%02X ",buffer[i]);
		if((i+1) % 16 == 0 ){
			printf("\n");
			address += 16;
			printf("0x%02x  ",address);
		}
	}
	fclose(fp);
	printf("\n");
	return 0;
}
        
