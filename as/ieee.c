/*
 * Функции работы с плавающей точкой ЭВМ М-20.
 * Copyright (GPL) 2008 Сергей Вакуленко <serge@vak.ru>
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "config.h"
#include "ieee.h"

typedef union {
        double value;
	uint64_t bits;
} ieee_t;

/*
 * Преобразование вещественного числа в формат М-20.
 *
 * Представление чисел в IEEE 754 (double):
 *	64   63———53 52————–1
 *	знак порядок мантисса
 * Старший (53-й) бит мантиссы не хранится и всегда равен 1.
 *
 * Представление чисел в M-20:
 *	44   43—--37 36————–1
 *      знак порядок мантисса
 */
uint64_t ieee_to_m20 (double d)
{
	ieee_t ieee;
	uint64_t word;
	int exponent;

	ieee.value = d;
	exponent = (int) (ieee.bits >> 52 & 0x7ff) - 1023;
	word = (uint64_t) (exponent + 1 + 64) << 36 |
		(ieee.bits & 0xfffffffffffffLL) >> (53 - 36) | 1LL << 35;
	if (ieee.bits & (1 << 16))
		word |= 1;		/* Округление. */
	if (d < 0)
		word |= 1LL << 43;	/* Знак. */
	return word;
}
