/* crc32.h - CP7: Ethernet CRC-32 (reflected 0xEDB88320, init 0xFFFFFFFF, final XOR). */
#ifndef SETU_CRC32_H
#define SETU_CRC32_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void     crc32_init(void);
uint32_t crc32(const uint8_t *buf, size_t n);
uint32_t crc32_bitwise(const uint8_t *buf, size_t n);         /* slow reference, for the speed table */
size_t   fcs_append(uint8_t *frame, size_t n);                /* appends 4-byte FCS, LSB first     */
bool     fcs_verify(const uint8_t *frame_with_fcs, size_t n); /* residue check                    */
#endif
