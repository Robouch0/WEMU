/*
** EPITECH PROJECT, 2025
** BE-elfanalyzer
** File description:
** my_utils
*/

#include "my_utils.h"

uint8_t be8(uint8_t const *p)
{
	return *p;
}

uint16_t be16(uint8_t const *p)
{
	return ((uint16_t)p[0] << 8) |
	    ((uint16_t)p[1] << 0);
}

uint32_t be32(uint8_t const *p)
{
    return ((uint32_t)p[0] << 24) |
        ((uint32_t)p[1] << 16) |
        ((uint32_t)p[2] << 8) |
        ((uint32_t)p[3] << 0);
}


uint64_t be64(uint8_t const *p)
{
    return ((uint64_t)p[0] << 56) |
        ((uint64_t)p[1] << 48) |
        ((uint64_t)p[2] << 40) |
        ((uint64_t)p[3] << 32) |
        ((uint64_t)p[4] << 24) |
        ((uint64_t)p[5] << 16) |
        ((uint64_t)p[6] << 8) |
        ((uint64_t)p[7] << 0);
}
