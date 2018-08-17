      #include <string.h>
      #include <stdio.h>

      int main()
      {
        char buf[64];
        char *func_name;
        char *instance_name;
        int ret;
	char *name = "Loopback.usb0";
	printf("burns. name %s\n",name);

        ret = snprintf(buf, 64, "%s", name);
        printf("burns. buf %s\n",buf);

        func_name = buf;
        printf("burns. func_name %s\n",func_name);
        instance_name = strchr(func_name, '.');
        printf("burns. after strchr, func_name %s, instance_name %s\n",func_name,instance_name);
        *instance_name = '\0';
        instance_name++;
        printf("burns. after offset, instance_name %s, func_name %s\n",instance_name,func_name);


        return 0;
      }
