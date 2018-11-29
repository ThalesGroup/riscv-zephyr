#include <zephyr.h>
#include <misc/printk.h>

void main(void)
{
	int counter = 0;
        char *zephyr_code = (char *)0x60000000;
	while(1)
	{
		printk("I'm alive! counter = %d\n", counter++);
                printk("0x%X 0x%X 0x%X 0x%X \n",zephyr_code[0],zephyr_code[1],zephyr_code[2],zephyr_code[3]);
                printk("0x%X 0x%X 0x%X 0x%X \n",zephyr_code[4],zephyr_code[5],zephyr_code[6],zephyr_code[7]);
		k_sleep(500);
	}
}
