/* CH32V003F4P6 (charging dock U1001): 16K flash, 2K RAM. */
__stack_size = 512;

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 16K
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 2K
}

INCLUDE sections.ld
