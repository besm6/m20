/*
 * Функции работы с плавающей точкой ЭВМ М-20.
 * Copyright (GPL) 2008 Сергей Вакуленко <serge.vakulenko@gmail.com>
 * Copyright (GPL) 2008 Леонид Брухис <leob@mailcom.com>
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include "config.h"
#include "ieee.h"

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
	uint64_t word;
	int exponent;
	int sign;

	sign = d < 0;
	if (sign)
		d = -d;
	d = frexp (d, &exponent);
	/* 0.5 <= d < 1.0 */
	d = ldexp (d, 36);
	word = d;
	if (d - word >= 0.5)
		word += 1;		/* Округление. */
	if (exponent < -64)
		exponent = -64;		/* Близкое к нулю число */
	if (exponent > 63) {
		word = 0xfffffffffLL;
		exponent = 63;		/* Максимальное число */
	}
	word |= ((uint64_t) (exponent + 64)) << 36;
	word |= (uint64_t) sign << 43;	/* Знак. */
	return word;
}

double m20_to_ieee (uint64_t word)
{
	double d;
	int exponent;

	d = word & 0xfffffffffLL;
	exponent = (word >> 36) & 0x7f;
	d = ldexp (d, exponent - 64 - 36);
	if ((word >> 43) & 1)
		d = -d;
	return d;
}
