/*
 * Симулятор для ЭВМ М-20.
 * Copyright (GPL) 2008 Сергей Вакуленко <serge@vak.ru>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include "config.h"
#include "encoding.h"
#include "ieee.h"

#define DATSIZE         4096    /* размер памяти в словах */

#define LINE_WORD	1	/* виды строк входного файла */
#define LINE_ADDR	2
#define LINE_START	3

#define TAG		0400000000000000LL	/* 45-й бит-признак */
#define SIGN		0200000000000000LL	/* 44-й бит-знак */

int debug;
char *infile;
double clock;			/* время выполнения, микросекунды */
int trace_start, trace_end = -1;
int Wflag, Rflag;

int start_address;
int RVK;			/* РВК - регистр выборки команды */
int RA;				/* РА - регистр адреса */
int OMEGA;			/* Ω */
uint64_t RK;			/* РК - регистр команды */
uint64_t RR;			/* РР - регистр результата */
uint64_t RMR;			/* РМР - регистр младших разрядов (М-220) */
uint64_t RPU1;			/* РПУ1 - регистр 1 пульта управления */
uint64_t RPU2;			/* РПУ2 - регистр 2 пульта управления */
uint64_t RPU3;			/* РПУ3 - регистр 3 пульта управления */
uint64_t RPU4;			/* РПУ4 - регистр 4 пульта управления */

uint64_t ram [DATSIZE];
unsigned char ram_dirty [DATSIZE];

void print_cmd (uint64_t cmd);

void quit ()
{
	exit (-1);
}

void uerror (char *s, ...)
{
	va_list ap;

	va_start (ap, s);
	fprintf (stderr, "%04o: ", RVK);
	vfprintf (stderr, s, ap);
	va_end (ap);
	fprintf (stderr, "\r\n");
	quit ();
}

void set_trace (char *str)
{
	char *e;

	trace_start = strtol (str, &e, 0);
	if (! e || *e != ':')
		trace_end = trace_start;
	else
		trace_end = strtol (e+1, 0, 0);
}

void trace (char *str)
{
	if (! debug)
		return;
	printf ("%8.6f) %04o: %s\r\n", clock, RVK, str);
}

/*
 * Пропуск пробелов.
 */
char *skip_spaces (char *p)
{
	while (*p == ' ' || *p == '\t')
		++p;
	return p;
}

/*
 * Чтение строки входного файла.
 */
int read_line (FILE *input, int *type, uint64_t *val)
{
	char buf [512], *p;
	int i;
again:
	if (! fgets (buf, sizeof (buf), input))
		return 0;
	p = skip_spaces (buf);
	if (*p == '\n' || *p == ';')
		goto again;
	if (*p == ':') {
		/* Адрес размещения данных. */
		*type = LINE_ADDR;
		*val = strtol (p+1, 0, 8);
		return 1;
	}
	if (*p == '@') {
		/* Стартовый адрес. */
		*type = LINE_START;
		*val = strtol (p+1, 0, 8);
		return 1;
	}
	if (*p == '=') {
		/* Вещественное число. */
		*type = LINE_WORD;
		*val = ieee_to_m20 (strtod (p+1, 0));
		return 1;
	}
	if (*p < '0' || *p > '7')
		uerror ("неверная строка входного файла");

	/* Слово. */
	*type = LINE_WORD;
	*val = *p - '0';
	for (i=0; i<14; ++i) {
		p = skip_spaces (p + 1);
		if (*p < '0' || *p > '7')
			uerror ("слишком короткое слово");
		*val = *val << 3 | (*p - '0');
	}
	return 1;
}

/*
 * Чтение входного файла.
 */
void readimage (FILE *input)
{
	int addr, type;
	uint64_t word;

	addr = 0;
	start_address = 1;
	while (read_line (input, &type, &word)) {
		switch (type) {
		case LINE_ADDR:
			addr = word;
			break;
		case LINE_WORD:
			ram [addr] = word;
			ram_dirty [addr] = 1;
			++addr;
			break;
		case LINE_START:
			start_address = word;
			break;
		}
		if (addr > DATSIZE)
			uerror ("неверный адрес");
	}
}

/*
 * Подсчитываем время выполнения.
 */
void cycle (double usec)
{
	clock += usec / 1000000;
}

/*
 * Считывание слова из памяти.
 */
uint64_t load (int addr)
{
	uint64_t val;

	addr &= 07777;
	switch (addr) {
	case 0:
		val = 0;
		break;
	default:
		if (! ram_dirty [addr])
			uerror ("чтение неинициализированного слова памяти: %02o",
				addr);
		val = ram [addr];
		break;
	}
	if (Rflag && addr == Rflag)
		printf ("%8.6f) %04o: read %04o value %015llo\r\n",
			clock, RVK, addr, val);
	return val;
}

/*
 * Запись слова в памяти.
 */
void store (int addr, uint64_t val)
{
	addr &= 07777;
	if (Wflag && addr == Wflag)
		printf ("%8.6f) %04o: write %04o value %015llo\r\n",
			clock, RVK, addr, val);
	ram [addr] = val;
	ram_dirty [addr] = 1;
}

/*
 * Проверка числа на равенство нулю.
 */
static inline int is_zero (uint64_t x)
{
	x &= ~(TAG | SIGN);
	return (x == 0);
}

/*
 * Нормализация числа (влево).
 */
uint64_t normalize (uint64_t x)
{
	int exp;
	uint64_t m;

	exp = x >> 36 & 0177;
	m = x & 0777777777777LL;
	if (m == 0) {
zero:		/* Нулевая мантисса, превращаем в ноль. */
		return x & TAG;
	}
	for (;;) {
		if (m & 0400000000000LL)
			break;
		m <<= 1;
		--exp;
		if (exp < 0)
			goto zero;
	}
	x &= TAG | SIGN;
	x |= (uint64_t) exp << 36 | m;
	return x;
}

/*
 * Сложение двух чисел, с блокировкой округления и нормализации,
 * если требуется.
 */
uint64_t addition (uint64_t x, uint64_t y, int no_round, int no_norm)
{
	int xexp, yexp, rexp;
	uint64_t xm, ym, r;

	if (is_zero (x)) {
		if (! no_norm)
			y = normalize (y);
		return y | (x & TAG);
	}
	if (is_zero (y)) {
zero_y:		if (! no_norm)
			x = normalize (x);
		return x | (y & TAG);
	}
	/* Извлечем порядок чисел. */
	xexp = x >> 36 & 0177;
	yexp = y >> 36 & 0177;
	if (yexp > xexp) {
		/* Пусть x - большее, а y - меньшее число (по модулю). */
		uint64_t t = x;
		int texp = xexp;
		x = y;
		xexp = yexp;
		y = t;
		yexp = texp;
	}
	if (xexp - yexp >= 36) {
		/* Пренебрежимо малое слагаемое. */
		goto zero_y;
	}
	/* Извлечем мантиссу чисел. */
	xm = x & 0777777777777LL;
	ym = (y & 0777777777777LL) >> (xexp - yexp);

	/* Сложим. */
	rexp = xexp;
	if ((x ^ y) & SIGN) {
		/* Противоположные знаки. */
		r = xm - ym;
		if (r & SIGN) {
			r = -r;
			r |= SIGN;
		}
	} else {
		/* Числа одного знака. */
		r = xm + ym;
		if (r >> 36) {
			/* Выход за 36 разрядов, нормализация вправо. */
			if (! no_round) {
				/* Округление. */
				r += 1;
			}
			r >>= 1;
			++rexp;
			if (rexp > 127)
				uerror ("переполнение при сложении");
		}
	}

	/* Конструируем результат. */
	r |= (uint64_t) rexp << 36;
	r ^= (x & SIGN);
	if (! no_norm)
		r = normalize (r);
	return r | ((x | y) & TAG);
}

/*
 * Умножение двух 36-битовых целых чисел, с выдачей двух половин результата.
 */
void mul36x36 (uint64_t x, uint64_t y, uint64_t *hi, uint64_t *lo)
{
	int yhi, ylo;
	uint64_t rhi, rlo;

	/* Разбиваем второй множитель на де половины. */
	yhi = y >> 18;
	ylo = y & 0777777;

	/* Частичные 54-битовые произведения. */
	rhi = x * yhi;
	rlo = x * ylo;

	/* Составляем результат. */
	rhi += rlo >> 18;
	*hi = rhi >> 18;
	*lo = (rhi & 0777777) << 18 | (rlo & 0777777);
}

/*
 * Умножение двух чисел, с блокировкой округления и нормализации,
 * если требуется.
 */
uint64_t multiplication (uint64_t x, uint64_t y, int no_round, int no_norm)
{
	int xexp, yexp, rexp;
	uint64_t xm, ym, r;

	/* Извлечем порядок чисел. */
	xexp = x >> 36 & 0177;
	yexp = y >> 36 & 0177;

	/* Извлечем мантиссу чисел. */
	xm = x & 0777777777777LL;
	ym = y & 0777777777777LL;

	/* Умножим. */
	rexp = xexp + yexp - 64;
	mul36x36 (xm, ym, &r, &RMR);

	if (! no_norm && ! (r & 0400000000000LL)) {
		/* Нормализация на один разряд влево. */
		--rexp;
		r <<= 1;
		RMR <<= 1;
		if (RMR & 01000000000000LL) {
			r |= 1;
			RMR &= 0777777777777LL;
		}
	} else if (! no_round) {
		/* Округление. */
		if (RMR & 0400000000000LL) {
			r += 1;
		}
	}
	if (r == 0 || rexp < 0) {
		/* Нуль. */
		return (x | y) & TAG;
	}
	if (rexp > 127)
		uerror ("переполнение при умножении");

	/* Конструируем результат. */
	r |= (uint64_t) rexp << 36;
	r |= ((x ^ y) & SIGN) | ((x | y) & TAG);
	RMR |= (uint64_t) rexp << 36;
	RMR |= ((x ^ y) & SIGN) | ((x | y) & TAG);
	return r;
}

void run ()
{
	int next_address, flags, op, a1, a2, a3;
	uint64_t x, y;

	next_address = start_address;
	RA = 0;
	OMEGA = 0;
	RMR = 0;
	RR = 0;
	for (;;) {
		RVK = next_address;
		if (RVK >= DATSIZE)
			uerror ("выход за пределы памяти");
		if (! ram_dirty [RVK])
			uerror ("выполнение неинициализированного слова памяти: %02o",
				RVK);
		RK = ram [RVK];
		if (debug > 1 ||
		    (RVK >= trace_start && RVK <= trace_end)) {
			printf ("%8.6f) %04o: ", clock, RVK);
			print_cmd (RK);
			printf (", РА=%04o, Ω=%d\r\n", RA, OMEGA);
		}
		next_address = RVK + 1;
		flags = RK >> 42 & 7;
		op = RK >> 36 & 077;
		a1 = RK >> 24 & 07777;
		a2 = RK >> 12 & 07777;
		a3 = RK & 07777;

		/* Есля установлен соответствующий бит признака,
		 * к адресу добавляется значение регистра адреса. */
		if (flags & 4)
			a1 = (a1 + RA) & 07777;
		if (flags & 2)
			a2 = (a2 + RA) & 07777;
		if (flags & 1)
			a3 = (a3 + RA) & 07777;

		switch (op) {
		default:
			uerror ("неверная команда: %02o", op);
			continue;
		case 000: /* зп - пересылка */
			RR = load (a1);
			store (a3, RR);
			/* Омега не изменяется. */
			cycle (24);
			break;
		case 020: /* счп - чтение пультовых тумблеров */
			switch (a1) {
			case 0: RR = 0;    break;
			case 1: RR = RPU1; break;
			case 2: RR = RPU2; break;
			case 3: RR = RPU3; break;
			case 4: RR = RPU4; break;
			case 5: /* RR */   break;
			default: uerror ("неверный аргумент команды СЧП: %04o", a1);
			}
			store (a3, RR);
			/* Омега не изменяется. */
			cycle (24);
			break;
		case 015: /* нтж - поразрядное сравнение (исключаищее или) */
			RR = load (a1) ^ load (a2);
			store (a3, RR);
			OMEGA = (RR == 0);
			cycle (24);
			break;
		case 035: /* нтжс - поразрядное сравнение с остановом */
			RR = load (a1) ^ load (a2);
			store (a3, RR);
			OMEGA = (RR == 0);
			cycle (24);
			if (! OMEGA)
				uerror ("останов по несовпадению: РР=%015llo", RR);
			break;
		case 055: /* и - логическое умножение (и) */
			RR = load (a1) & load (a2);
			store (a3, RR);
			OMEGA = (RR == 0);
			cycle (24);
			break;
		case 075: /* или - логическое сложение (или) */
			RR = load (a1) | load (a2);
			store (a3, RR);
			OMEGA = (RR == 0);
			cycle (24);
			break;
		case 001: /* сл - сложение с округлением и нормализацией */
		case 021: /* слбо - сложение без округления с нормализацией */
		case 041: /* слбн - сложение с округлением без нормализации */
		case 061: /* слбно - сложение без округления и без нормализации */
			x = load (a1);
			y = load (a2);
			RR = addition (x, y, op >> 4 & 1, op >> 5 & 1);
			store (a3, RR);
			OMEGA = (RR & SIGN) != 0;
			cycle (29.5);
			break;
		case 002: /* вч - вычитание с округлением и нормализацией */
		case 022: /* вчбо - вычитание без округления с нормализацией */
		case 042: /* вчбн - вычитание с округлением без нормализации */
		case 062: /* вчбно - вычитание без округления и без нормализации */
			x = load (a1);
			y = load (a2) ^ SIGN;
			RR = addition (x, y, op >> 4 & 1, op >> 5 & 1);
			store (a3, RR);
			OMEGA = (RR & SIGN) != 0;
			cycle (29.5);
			break;
		case 003: /* вчм - вычитание модулей с округлением и нормализацией */
		case 023: /* вчмбо - вычитание модулей без округления с нормализацией */
		case 043: /* вчмбн - вычитание модулей с округлением без нормализации */
		case 063: /* вчмбно - вычитание модулей без округления и без нормализации */
			x = load (a1) & ~SIGN;
			y = load (a2) | SIGN;
			RR = addition (x, y, op >> 4 & 1, op >> 5 & 1);
			store (a3, RR);
			OMEGA = (RR & SIGN) != 0;
			cycle (29.5);
			break;
		case 005: /* умн - умножение с округлением и нормализацией */
		case 025: /* умнбо - умножение без округления с нормализацией */
		case 045: /* умнбн - умножение с округлением без нормализации */
		case 065: /* умнбно - умножение без округления и без нормализации */
			x = load (a1);
			y = load (a2);
			RR = multiplication (x, y, op >> 4 & 1, op >> 5 & 1);
			store (a3, RR);
			OMEGA = (int) (RR >> 36 & 0177) > 0100;
			cycle (70);
			break;
		case 047: /* счмр - выдача младших разрядов произведения */
			RR = RMR;
			store (a3, RR);
			OMEGA = (int) (RR >> 36 & 0177) > 0100;
			cycle (24);
			break;
		case 016: /* пв - передача управления с возвратом */
			RR = 016000000000000LL | (a1 << 12);
			store (a3, RR);
			next_address = a2;
			/* Омега не изменяется. */
			cycle (24);
			break;
		case 036: /* пе - передача управления по условию Ω=1 */
			RR = load (a1);
			store (a3, RR);
			if (OMEGA)
				next_address = a2;
			/* Омега не изменяется. */
			cycle (24);
			break;
		case 056: /* пб - передача управления */
			RR = load (a1);
			store (a3, RR);
			next_address = a2;
			/* Омега не изменяется. */
			cycle (24);
			break;
		case 076: /* по - передача управления по условию Ω=0 */
			RR = load (a1);
			store (a3, RR);
			if (! OMEGA)
				next_address = a2;
			/* Омега не изменяется. */
			cycle (24);
			break;
		case 077: /* стоп - останов машины */
			RR = 0;
			store (a3, RR);
			/* Омега не изменяется. */
			cycle (24);
			uerror ("останов: A1=%04o, A2=%04o", a1, a2);
			break;
#if 0
		/* Арифметические операции */
		case 004: /* дел - Деление с округлением */
		case 024: /* делбо - Деление без округления */
		case 044: /* кор - Извлечение корня с округлением */
		case 064: /* корбо - Извлечение корня без округления */
		case 006: /* слпа - Сложение порядка с адресом */
		case 026: /* слп - Сложение порядков чисел */
		case 046: /* вчпа - Вычитание адреса из порядка */
		case 066: /* вчп - Вычитание порядков чисел */

		/* Логические операции */
		case 013: /* слк - Сложение команд */
		case 033: /* вчк - Вычитание команд */
		case 053: /* слко - Сложение кодов операций */
		case 073: /* вчко - Вычитание кодов операций */
		case 014: /* сдма - Сдвиг мантиссы по адресу */
		case 034: /* сдм - Сдвиг мантиссы по порядку числа */
		case 054: /* сда - Сдвиг по адресу */
		case 074: /* сд - Сдвиг по порядку числа */
		case 007: /* слц - Циклическое сложение */
		case 027: /* вчц - Циклическое вычитание */
		case 067: /* сдц - Циклический сдвиг */

		/* Операции управления */
		case 010: /* вп - Ввод с перфокарт */
		case 030: /* впбк - Ввод с перфокарт без проверки контрольной суммы */
		case 050: /* ма - Подготовка обращения к внешнему устройству */
		case 070: /* мб - Выполнение обращения к внешнему устройству */
		case 011: /* цме - Сравнение и установка регистра адреса, переход по < и Ω=1 */
		case 031: /* цбре - Сравнение и установка регистра адреса, переход по >= и Ω=1 */
		case 051: /* цмо - Сравнение и установка регистра адреса, переход по < и Ω=0 */
		case 071: /* цбро - Сравнение и установка регистра адреса, переход по >= и Ω=0 */
		case 012: /* цм - Сравнение и установка регистра адреса, переход по < */
		case 032: /* цбр - Сравнение и установка регистра адреса, переход по >= */
		case 052: /* раа - Установка регистра адреса */
		case 072: /* ра - Установка регистра адреса по числу */
#endif
		}
	}
}

/*
 * Печать 12-битной адресной части машинной инструкции.
 */
void print_addr (int a, int flag)
{
	char buf [40], *p;

	p = buf;
	if (flag)
		*p++ = '@';
	if (flag && a >= 07700) {
		*p++ = '-';
		a = (a ^ 07777) + 1;
		if (a > 7)
			sprintf (p, "%#o", a);
		else
			sprintf (p, "%o", a);
	} else if (a) {
		if (flag)
			*p++ = '+';
		if (a > 7)
			sprintf (p, "%#o", a);
		else
			sprintf (p, "%o", a);
	} else
		*p = 0;
	printf ("%7s", buf);
}

/*
 * Печать машинной инструкции.
 */
void print_cmd (uint64_t cmd)
{
	char *m;
	int flags, op, a1, a2, a3;

	flags = cmd >> 42 & 7;
	op = cmd >> 36 & 077;
	a1 = cmd >> 24 & 07777;
	a2 = cmd >> 12 & 07777;
	a3 = cmd & 07777;

	switch (op) {
	/* Арифметические операции */
	case 001: m = "сл";     break;	/* Сложение с округлением и нормализацией */
	case 021: m = "слбо";   break;	/* Сложение без округления с нормализацией */
	case 041: m = "слбн";   break;	/* Сложение с округлением без нормализации */
	case 061: m = "слбно";  break;	/* Сложение без округления и без нормализации */
	case 002: m = "вч";     break;	/* Вычитание с округлением и нормализацией */
	case 022: m = "вчбо";   break;	/* Вычитание без округления с нормализацией */
	case 042: m = "вчбн";   break;	/* Вычитание с округлением без нормализации */
	case 062: m = "вчбно";  break;	/* Вычитание без округления и без нормализации */
	case 003: m = "вчм";    break;	/* Вычитание модулей с округлением и нормализацией */
	case 023: m = "вчмбо";  break;	/* Вычитание модулей без округления с нормализацией */
	case 043: m = "вчмбн";  break;	/* Вычитание модулей с округлением без нормализации */
	case 063: m = "вчмбно"; break;	/* Вычитание модулей без округления и без нормализации */
	case 004: m = "дел";    break;	/* Деление с округлением */
	case 024: m = "делбо";  break;	/* Деление без округления */
	case 044: m = "кор";    break;	/* Извлечение корня с округлением */
	case 064: m = "корбо";  break;	/* Извлечение корня без округления */
	case 005: m = "умн";    break;	/* Умножение с округлением и нормализацией */
	case 025: m = "умнбо";  break;	/* Умножение без округления с нормализацией */
	case 045: m = "умнбн";  break;	/* Умножение с округлением без нормализации */
	case 065: m = "умнбно"; break;	/* Умножение без округления и без нормализации */
	case 006: m = "слпа";   break;	/* Сложение порядка с адресом */
	case 026: m = "слп";    break;	/* Сложение порядков чисел */
	case 046: m = "вчпа";   break;	/* Вычитание адреса из порядка */
	case 066: m = "вчп";    break;	/* Вычитание порядков чисел */
	case 047: m = "счмр";   break;	/* Выдача младших разрядов произведения */

	/* Логические операции */
	case 000: m = "зп";   break;	/* Пересылка */
	case 020: m = "счкр"; break;	/* Выборка из регистра КЗУ (чтение пультовых тумблеров) */
	case 013: m = "слк";  break;	/* Сложение команд */
	case 033: m = "вчк";  break;	/* Вычитание команд */
	case 053: m = "слко"; break;	/* Сложение кодов операций */
	case 073: m = "вчко"; break;	/* Вычитание кодов операций */
	case 014: m = "сдма"; break;	/* Сдвиг мантиссы по адресу */
	case 034: m = "сдм";  break;	/* Сдвиг мантиссы по порядку числа */
	case 054: m = "сда";  break;	/* Сдвиг по адресу */
	case 074: m = "сд";   break;	/* Сдвиг по порядку числа */
	case 015: m = "нтж";  break;	/* Поразрядное сравнение (исключаищее или) */
	case 035: m = "нтжс"; break;	/* Поразрядное сравнение и останов машины по несовпадению */
	case 055: m = "и";    break;	/* Логическое умножение (и) */
	case 075: m = "или";  break;	/* Логическое сложение (или) */
	case 007: m = "слц";  break;	/* Циклическое сложение */
	case 027: m = "вчц";  break;	/* Циклическое вычитание */
	case 067: m = "сдц";  break;	/* Циклический сдвиг */

	/* Операции управления */
	case 010: m = "вп";   break;	/* Ввод с перфокарт */
	case 030: m = "впбк"; break;	/* Ввод с перфокарт без проверки контрольной суммы */
	case 050: m = "ма";   break;	/* Подготовка обращения к внешнему устройству */
	case 070: m = "мб";   break;	/* Выполнение обращения к внешнему устройству */
	case 011: m = "цме";  break;	/* Сравнение и установка регистра адреса, переход по < и Ω=1 */
	case 031: m = "цбре"; break;	/* Сравнение и установка регистра адреса, переход по >= и Ω=1 */
	case 051: m = "цмо";  break;	/* Сравнение и установка регистра адреса, переход по < и Ω=0 */
	case 071: m = "цбро"; break;	/* Сравнение и установка регистра адреса, переход по >= и Ω=0 */
	case 012: m = "цм";   break;	/* Сравнение и установка регистра адреса, переход по < */
	case 032: m = "цбр";  break;	/* Сравнение и установка регистра адреса, переход по >= */
	case 052: m = "раа";  break;	/* Установка регистра адреса */
	case 072: m = "ра";   break;	/* Установка регистра адреса по числу */
	case 016: m = "пв";   break;	/* Передача управления с возвратом */
	case 036: m = "пе";   break;	/* Передача управления по условию Ω=1 */
	case 056: m = "пб";   break;	/* Передача управления */
	case 076: m = "по";   break;	/* Передача управления по условию Ω=0 */
	case 077: m = "стоп"; break;	/* Останов машины */

	default:
		printf ("?");
		return;
	}

	if (! flags && ! a1 && ! a2 && ! a3) {
		/* Команда без аргументов. */
		printf ("%s", m);
		return;
	}
	printf ("%s\t", m);
	print_addr (a1, flags & 4);
	if (! (flags & 3) && ! a2 && ! a3) {
		/* Нет аргументов 2 и 3. */
		return;
	}

	printf (", ");
	print_addr (a2, flags & 2);
	if (! (flags & 1) && ! a3) {
		/* Нет аргумента 3. */
		return;
	}

	printf (", ");
	print_addr (a3, flags & 1);
}

int main (int argc, char **argv)
{
	int i;
	char *cp;
	FILE *input = stdin;

	for (i=1; i<argc; i++)
		switch (argv[i][0]) {
		case '-':
			for (cp=argv[i]; *cp; cp++) switch (*cp) {
			case 't':
				debug++;
				break;
			case 'T':
				if (cp [1]) {
					/* -Targ */
					set_trace (cp + 1);
					while (*++cp);
					--cp;
				} else if (i+1 < argc)
					/* -T arg */
					set_trace (argv[++i]);
				break;
			case 'W':
				if (cp [1]) {
					/* -Warg */
					Wflag = strtol (cp+1, 0, 0);
					while (*++cp);
					--cp;
				} else if (i+1 < argc)
					/* -W arg */
					Wflag = strtol (argv[++i], 0, 0);
				break;
			case 'R':
				if (cp [1]) {
					/* -Rarg */
					Rflag = strtol (cp+1, 0, 0);
					while (*++cp);
					--cp;
				} else if (i+1 < argc)
					/* -R arg */
					Rflag = strtol (argv[++i], 0, 0);
				break;
			}
			break;
		default:
			if (infile)
				goto usage;
			infile = argv[i];
			input = fopen (infile, "r");
			if (! input)
				uerror ("не могу открыть файл");
			break;
		}

	if (! infile) {
usage:		printf ("Симулятор M-20\n");
		printf ("Вызов:\n");
		printf ("\tsim [флаги...] infile.m20\n");
		printf ("Флаги:\n");
		printf ("  -t\t\tтрассировка выполнения инструкций\n");
		printf ("  -Tнач:кон\tустановка диапазона трассировки выполнения\n");
		printf ("  -Wадрес\tтрассировка записи по адресу\n");
		printf ("  -Rадрес\tтрассировка чтения по адресу\n");
		return -1;
	}

	readimage (input);
	if (debug)
		printf ("Прочитан файл %s\n", infile);

	if (debug)
		printf ("Пуск...\r\n");
	run ();

	return 0;
}
