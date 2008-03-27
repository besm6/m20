/*
 * Симулятор для ЭВМ М-20.
 * Copyright (GPL) 2008 Сергей Вакуленко <serge@vak.ru>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <math.h>
#include "config.h"
#include "encoding.h"
#include "ieee.h"

#define DATSIZE         4096    /* размер памяти в словах */

#define LINE_WORD	1	/* виды строк входного файла */
#define LINE_ADDR	2
#define LINE_START	3

/*
 * Разряды машинного слова.
 */
#define BIT46		01000000000000000LL	/* 46-й бит */
#define TAG		00400000000000000LL	/* 45-й бит-признак */
#define SIGN		00200000000000000LL	/* 44-й бит-знак */
#define BIT37		00001000000000000LL	/* 37-й бит */
#define BIT19		00000000001000000LL	/* 19-й бит */
#define MANTISSA	00000777777777777LL	/* биты 36..1 */

/*
 * Разряды условного числа для обращения к внешнему устройству.
 */
#define EXT_DIS_RAM	04000	/* 36 - БМ - блокировка памяти */
#define EXT_DIS_CHECK	02000   /* 35 - БК - блокировка контроля */
#define EXT_TAPE_REV	01000   /* 34 - ОН - обратное движение ленты */
#define EXT_DIS_STOP	00400   /* 33 - БО - блокировка останова */
#define EXT_PUNCH	00200   /* 32 - Пф - перфорация */
#define EXT_PRINT	00100   /* 31 - Пч - печать */
#define EXT_TAPE_FORMAT	00040   /* 30 - РЛ - разметка ленты */
#define EXT_TAPE	00020   /* 29 - Л - лента */
#define EXT_DRUM	00010   /* 28 - Б - барабан */
#define EXT_WRITE	00004   /* 27 - Зп - запись */
#define EXT_UNIT	00003   /* 26,25 - номер барабана или ленты */

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

/* Параметры обмена с внешним устройством. */
int ext_op;			/* УЧ - условное число */
int ext_disk_addr;		/* А_МЗУ - начальный адрес на барабане/ленте */
int ext_ram_start;		/* α_МОЗУ - начальный адрес памяти */
int ext_ram_finish;		/* ω_МОЗУ - конечный адрес памяти */

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
	m = x & MANTISSA;
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
	xm = x & MANTISSA;
	ym = (y & MANTISSA) >> (xexp - yexp);

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
 * Коррекция порядка.
 */
uint64_t add_exponent (uint64_t x, int n)
{
	int exp;

	exp = (int) (x >> 36 & 0177) + n;
	if (exp > 127)
		uerror ("переполнение при сложении порядков");
	if (exp < 0 || (x & MANTISSA) == 0) {
		/* Ноль. */
		x &= TAG;
	}
	return x;
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
	xm = x & MANTISSA;
	ym = y & MANTISSA;

	/* Умножим. */
	rexp = xexp + yexp - 64;
	mul36x36 (xm, ym, &r, &RMR);

	if (! no_norm && ! (r & 0400000000000LL)) {
		/* Нормализация на один разряд влево. */
		--rexp;
		r <<= 1;
		RMR <<= 1;
		if (RMR & BIT37) {
			r |= 1;
			RMR &= MANTISSA;
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

/*
 * Деление двух чисел, с блокировкой округления, если требуется.
 */
uint64_t division (uint64_t x, uint64_t y, int no_round)
{
	int xexp, yexp, rexp;
	uint64_t xm, ym, r;

	/* Извлечем порядок чисел. */
	xexp = x >> 36 & 0177;
	yexp = y >> 36 & 0177;

	/* Извлечем мантиссу чисел. */
	xm = x & MANTISSA;
	ym = y & MANTISSA;
	if (xm >= 2*ym)
		uerror ("переполнение мантиссы при делении");

	/* Поделим. */
	rexp = xexp - yexp + 64;
	r = (double) xm / ym * BIT37;
	if (r >> 36) {
		/* Выход за 36 разрядов, нормализация вправо. */
		if (! no_round) {
			/* Округление. */
			r += 1;
		}
		r >>= 1;
		++rexp;
	}
	if (r == 0 || rexp < 0) {
		/* Нуль. */
		return (x | y) & TAG;
	}
	if (rexp > 127)
		uerror ("переполнение при сложении");

	/* Конструируем результат. */
	r |= (uint64_t) rexp << 36;
	r |= ((x ^ y) & SIGN) | ((x | y) & TAG);
	return r;
}

/*
 * Вычисление квадратного корня, с блокировкой округления, если требуется.
 */
uint64_t square_root (uint64_t x, int no_round)
{
	int exp;
	uint64_t r;
	double q;

	if (x & SIGN)
		uerror ("корень из отрицательного числа");

	/* Извлечем порядок числа. */
	exp = x >> 36 & 0177;

	/* Извлечем мантиссу чисел. */
	r = x & MANTISSA;

	/* Вычисляем корень. */
	if (exp & 1) {
		/* Нечетный порядок. */
		r >>= 1;
	}
	exp = (exp >> 1) + 32;
	q = sqrt ((double) r) * BIT19;
	r = (uint64_t) q;
	if (! no_round) {
		/* Смотрим остаток. */
		if (q - r >= 0.5) {
			/* Округление. */
			r += 1;
		}
	}
	if (r == 0) {
		/* Нуль. */
		return x & TAG;
	}
	if (r & ~MANTISSA)
		uerror ("ошибка квадратного корня");

	/* Конструируем результат. */
	r |= (uint64_t) exp << 36;
	r |= x & TAG;
	return r;
}

void drum_write (int unit, int addr, int first, int last)
{
	/* TODO */
}

int drum_read (int unit, int addr, int first, int last)
{
	/* TODO */
	return 0;
}

void print_octal (int first, int last)
{
	/* TODO */
}

void print_decimal (int first, int last)
{
	/* TODO */
}

void print_text (int first, int last)
{
	/* TODO */
}

/*
 * Подготовка обращения к внешнему устройству.
 * В условном числе должен быть задан один из пяти видов работы:
 * барабан, лента, разметка ленты, печать или перфорация.
 */
void ext_setup (int a1, int a2, int a3)
{
	ext_op = a1;
	ext_disk_addr = a2;
	ext_ram_finish = a3;

	if (ext_op & EXT_WRITE) {
		/* При записи проверка контрольной суммы не производится,
		 * поэтому блокировка останова не имеет смысла. */
		ext_op &= ~EXT_DIS_STOP;
	}
	if (ext_op & EXT_DRUM) {
		/* Для барабана направление движения задавать не надо. */
		ext_op &= ~EXT_TAPE_REV;
		if (ext_op & (EXT_PUNCH | EXT_PRINT |
		    EXT_TAPE_FORMAT | EXT_TAPE))
			uerror ("неверное УЧ для обращения к барабану: %04o", ext_op);
	}
	if (ext_op & EXT_TAPE) {
		if (ext_op & (EXT_PUNCH | EXT_PRINT | EXT_TAPE_FORMAT))
			uerror ("неверное УЧ для обращения к ленте: %04o", ext_op);
	}
	if (ext_op & EXT_TAPE_FORMAT) {
		/* При разметке ленты не имеют значения признаки записи,
		 * блокировки останова и обратного направления движения. */
		ext_op &= ~(EXT_WRITE | EXT_DIS_STOP | EXT_TAPE_REV);
		if (ext_op & (EXT_PUNCH | EXT_PRINT | EXT_DIS_CHECK))
			uerror ("неверное УЧ для разметки ленты: %04o", ext_op);
	}
	if (ext_op & EXT_PRINT) {
		/* При печати не имеют значения признаки записи и
		 * обратного направления движения ленты. */
		ext_op &= ~(EXT_WRITE | EXT_TAPE_REV);
	}
	if (ext_op & EXT_PUNCH) {
		/* При перфорации не имеют значения признаки записи,
		 * блокировки останова и обратного направления движения. */
		ext_op &= ~(EXT_WRITE | EXT_DIS_STOP | EXT_TAPE_REV);
	}
}

/*
 * Выполнение обращения к внешнему устройству.
 * В случае ошибки возвращается 0.
 * Контрольная сумма записи накапливается в параметре sum (не реализовано).
 * Блокировка памяти (EXT_DIS_RAM) и блокировка контроля (EXT_DIS_CHECK)
 * пока не поддерживаются.
 */
int ext_io (int a1, uint64_t *sum)
{
	ext_ram_start = a1;

	*sum = 0;

	if (ext_op & EXT_DRUM) {
		/* Барабан */
		if (ext_op & EXT_WRITE) {
			drum_write ((ext_op & EXT_UNIT), ext_disk_addr,
				ext_ram_start, ext_ram_finish);
			return 1;
		} else {
			if (drum_read ((ext_op & EXT_UNIT), ext_disk_addr,
			  ext_ram_start, ext_ram_finish))
				return 1;
			if (! (ext_op & EXT_DIS_STOP))
				uerror ("ошибка чтения барабана: %d %04o %04o %04o",
					(ext_op & EXT_UNIT), ext_disk_addr,
					ext_ram_start, ext_ram_finish);
			return 0;
		}
	} else if (ext_op & EXT_TAPE) {
		/* Лента */
		uerror ("работа с магнитной лентой не поддерживается");

	} else if (ext_op & EXT_TAPE_FORMAT) {
		/* Разметка ленты */
		uerror ("разметка ленты не поддерживается");

	} else if (ext_op & EXT_PRINT) {
		/* Печать. Параметр EXT_PUNCH (накопление в буфере без выдачи)
		 * пока не реализован. */
		if (ext_op & EXT_DIS_STOP) {
			/* Восьмеричная печать */
			print_octal (ext_ram_start, ext_ram_finish);
		} else if (ext_op & EXT_TAPE_FORMAT) {
			/* Текстовая печать */
			print_text (ext_ram_start, ext_ram_finish);
		} else {
			/* Десятичная печать */
			print_decimal (ext_ram_start, ext_ram_finish);
		}
	} else if (ext_op & EXT_PUNCH) {
		uerror ("вывод на перфокарты не поддерживается");
	} else
		uerror ("неверное УЧ для инструкции МБ: %04o", ext_op);
	return 0;
}

void run ()
{
	int next_address, flags, op, a1, a2, a3, n = 0;
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
		/*
		 * Логические операции
		 */
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
logop:			store (a3, RR);
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
			goto logop;
		case 075: /* или - логическое сложение (или) */
			RR = load (a1) | load (a2);
			goto logop;
		case 013: /* слк - сложение команд */
			x = load (a1);
			y = (x & MANTISSA) + (load (a2) & MANTISSA);
addm:			RR = (x & ~MANTISSA) | (y & MANTISSA);
			store (a3, RR);
			OMEGA = (y & BIT37) != 0;
			cycle (24);
			break;
		case 033: /* вчк - вычитание команд */
			x = load (a1);
			y = (x & MANTISSA) - (load (a2) & MANTISSA);
			goto addm;
		case 053: /* слко - сложение кодов операций */
			x = load (a1);
			y = (x & ~MANTISSA) + (load (a2) & ~MANTISSA);
addop:			RR = (x & MANTISSA) | (y & ~MANTISSA);
			store (a3, RR);
			OMEGA = (y & BIT46) != 0;
			cycle (24);
			break;
		case 073: /* вчко - вычитание кодов операций */
			x = load (a1);
			y = (x & ~MANTISSA) - (load (a2) & ~MANTISSA);
			goto addop;
		case 014: /* сдма - сдвиг мантиссы по адресу */
			n = (a1 & 0177) - 64;
			cycle (61.5 + 1.5 * (n>0 ? n : -n));
shm:			y = load (a2);
			RR = (y & ~MANTISSA);
			if (n > 0)
				RR |= (y & MANTISSA) << n;
			else if (n < 0)
				RR |= (y & MANTISSA) >> -n;
			store (a3, RR);
			OMEGA = (RR == 0);
			break;
		case 034: /* сдм - сдвиг мантиссы по порядку числа */
			n = (int) (load (a1) >> 36 & 0177) - 64;
			cycle (24 + 1.5 * (n>0 ? n : -n));
			goto shm;
		case 054: /* сда - сдвиг по адресу */
			n = (a1 & 0177) - 64;
			cycle (61.5 + 1.5 * (n>0 ? n : -n));
shift:			RR = load (a2);
			if (n > 0)
				RR <<= n;
			else if (n < 0)
				RR >>= -n;
			store (a3, RR);
			OMEGA = (RR == 0);
			break;
		case 074: /* сд - сдвиг по порядку числа */
			x = (int) (load (a1) >> 36 & 0177) - 64;
			cycle (24 + 1.5 * (n>0 ? n : -n));
			goto shift;
		case 007: /* слц - циклическое сложение */
			x = load (a1);
			y = load (a2);
			RR = (x & ~MANTISSA) + (y & ~MANTISSA);
			y = (x & MANTISSA) + (y & MANTISSA);
csum:			if (RR & BIT46) {
				RR += BIT37;
				RR &= ~BIT46;
			}
			if (y & BIT37)
				y += 1;
			RR |= y & MANTISSA;
			store (a3, RR);
			OMEGA = (y & BIT37) != 0;
			cycle (24);
			break;
		case 027: /* вчц - циклическое вычитание */
			x = load (a1);
			y = load (a2);
			RR = (x & ~MANTISSA) - (y & ~MANTISSA);
			y = (x & MANTISSA) - (y & MANTISSA);
			goto csum;
		case 067: /* сдц - циклический сдвиг */
			x = load (a1);
			RR = (x & 07777777) << 24 | (x >> 24 & 07777777);
			store (a3, RR);
			cycle (60);
			break;
		/*
		 * Операции управления
		 */
		case 016: /* пв - передача управления с возвратом */
			RR = 016000000000000LL | (a1 << 12);
			store (a3, RR);
			next_address = a2;
			cycle (24);
			break;
		case 036: /* пе - передача управления по условию Ω=1 */
			RR = load (a1);
			store (a3, RR);
			if (OMEGA)
				next_address = a2;
			cycle (24);
			break;
		case 056: /* пб - передача управления */
			RR = load (a1);
			store (a3, RR);
			next_address = a2;
			cycle (24);
			break;
		case 076: /* по - передача управления по условию Ω=0 */
			RR = load (a1);
			store (a3, RR);
			if (! OMEGA)
				next_address = a2;
			cycle (24);
			break;
		case 077: /* стоп - останов машины */
			RR = 0;
			store (a3, RR);
			cycle (24);
			uerror ("останов: A1=%04o, A2=%04o", a1, a2);
			break;
		case 011: /* цме - переход по < и Ω=1 */
			if (RA < a1 && OMEGA)
				next_address = a2;
			cycle (24);
			break;
		case 031: /* цбре - переход по >= и Ω=1 */
			if (RA >= a1 && OMEGA)
				next_address = a2;
			cycle (24);
			break;
		case 051: /* цмо - переход по < и Ω=0 */
			if (RA < a1 && ! OMEGA)
				next_address = a2;
			cycle (24);
			break;
		case 071: /* цбро - переход по >= и Ω=0 */
			if (RA >= a1 && ! OMEGA)
				next_address = a2;
			cycle (24);
			break;
		case 012: /* цм - переход по < */
			if (RA < a1)
				next_address = a2;
			cycle (24);
			break;
		case 032: /* цбр - переход по >= */
			if (RA >= a1)
				next_address = a2;
			cycle (24);
			break;
		case 052: /* раа - установка регистра адреса адресом */
			RR = 052000000000000LL | (a1 << 12);
			store (a3, RR);
			RA = a2;
			cycle (24);
			break;
		case 072: /* ра - установка регистра адреса числом */
			RR = 052000000000000LL | (a1 << 12);
			store (a3, RR);
			RA = load (a2) >> 12 & 07777;
			cycle (24);
			break;
		case 010: /* вп - ввод с перфокарт */
		case 030: /* впбк - ввод с перфокарт без проверки к.суммы */
			uerror ("ввод с перфокарт не поддерживается");
			break;
		case 050: /* ма - подготовка обращения к внешнему устройству */
			ext_setup (a1, a2, a3);
			cycle (24);
			continue;
		case 070: /* мб - выполнение обращения к внешнему устройству */
			if (ext_op == 07777)
				uerror ("команда МБ не работает без МА");
			if (! ext_io (a1, &RR))
				next_address = a2;
			if ((ext_op & EXT_WRITE) && ! (ext_op & EXT_DIS_CHECK))
				store (a3, RR);
			cycle (24);
			break;
		/*
		 * Арифметические операции
		 */
		case 001: /* сл - сложение с округлением и нормализацией */
		case 021: /* слбо - сложение без округления с нормализацией */
		case 041: /* слбн - сложение с округлением без нормализации */
		case 061: /* слбно - сложение без округления и без нормализации */
			x = load (a1);
			y = load (a2);
add:			RR = addition (x, y, op >> 4 & 1, op >> 5 & 1);
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
			goto add;
		case 003: /* вчм - вычитание модулей с округлением и нормализацией */
		case 023: /* вчмбо - вычитание модулей без округления с нормализацией */
		case 043: /* вчмбн - вычитание модулей с округлением без нормализации */
		case 063: /* вчмбно - вычитание модулей без округления и без нормализации */
			x = load (a1) & ~SIGN;
			y = load (a2) | SIGN;
			goto add;
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
		case 004: /* дел - деление с округлением */
		case 024: /* делбо - деление без округления */
			x = load (a1);
			y = load (a2);
			RR = division (x, y, op >> 4 & 1);
			store (a3, RR);
			OMEGA = (int) (RR >> 36 & 0177) > 0100;
			cycle (136);
			break;
		case 044: /* кор - извлечение корня с округлением */
		case 064: /* корбо - извлечение корня без округления */
			x = load (a1);
			RR = square_root (x, op >> 4 & 1);
			store (a3, RR);
			OMEGA = (int) (RR >> 36 & 0177) > 0100;
			cycle (275);
			break;
		case 047: /* счмр - выдача младших разрядов произведения */
			RR = RMR;
			store (a3, RR);
			OMEGA = (int) (RR >> 36 & 0177) > 0100;
			cycle (24);
			break;
		case 006: /* слпа - сложение порядка с адресом */
			n = (a1 & 0177) - 64;
			y = load (a2);
addexp:			RR = add_exponent (y, n);
			store (a3, RR);
			OMEGA = (int) (RR >> 36 & 0177) > 0100;
			cycle (61.5);
			break;
		case 026: /* слп - сложение порядков чисел */
			x = load (a2);
			n = (int) (x >> 36 & 0177) - 64;
			y = load (a2) | (x & TAG);
			goto addexp;
		case 046: /* вчпа - вычитание адреса из порядка */
			n = 64 - (a1 & 0177);
			y = load (a2);
			goto addexp;
		case 066: /* вчп - вычитание порядков чисел */
			x = load (a2);
			n = 64 - (int) (x >> 36 & 0177);
			y = load (a2) | (x & TAG);
			goto addexp;
		}
		ext_op = 07777;
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
