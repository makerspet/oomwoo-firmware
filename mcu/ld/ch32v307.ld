/* CH32V307VCT6 (U13). The split between zero-wait flash and RAM is set by
 * option bytes (192K+128K, 224K+96K, 256K+64K, 288K+32K). 192K flash and 32K
 * RAM fit every setting. Once the production option byte (192K + 128K, for
 * camera line buffers) is programmed, raise RAM to 128K. */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 192K
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 32K
}

INCLUDE sections.ld
