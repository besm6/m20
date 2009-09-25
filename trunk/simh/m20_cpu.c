/*
 * m20_cpu.c: M-20 CPU simulator
 *
 * Copyright (c) 2009, Serge Vakulenko
 *
 * For more information about M-20 computer, visit sites:
 *  - http://www.computer-museum.ru/english/m20.htm
 *  - http://code.google.com/p/m20/
 *  - http://ru.wikipedia.org/wiki/%D0%91%D0%AD%D0%A1%D0%9C
 *
 * M-20 was built using tubes. Later, several transistor-based
 * machines were created, with the same architectore: БЭСМ-3М,
 * БЭСМ-4, М-220, М-220М, М-222. All software compatible with M-20.
 *
 * Release notes for M-20/SIMH
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  1) All addresses and data values are displayed in octal.
 *  2) M-20 processor has no interrupt system.
 *  3) Execution times are in microseconds.
 *  4) Magnetic drum is a "DRUM" device.
 *  5) Magnetic tape is not implemented.
 *  6) Punch reader is not implemented.
 *  7) Card puncher is not implemented.
 *  8) Printer output is sent to console.
 *  9) Square root instruction is performed using sqrt().
 *     All other math is authentic.
 * 10) Instruction mnemonics, register names and stop messages
 *     are in Russian using UTF-8 encoding. It is assumed, that
 *     user locale is UTF-8.
 * 11) A lot of comments in Russian (UTF-8).
 */
#include "m20_defs.h"
#include <math.h>
#include <float.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

t_value M [MEMSIZE];
uint32 RVK, RA, OMEGA;
t_value RK, RR, RMR, RPU1, RPU2, RPU3, RPU4;
double delay;

t_stat cpu_examine (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_deposit (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);

/*
 * CPU data structures
 *
 * cpu_dev      CPU device descriptor
 * cpu_unit     CPU unit descriptor
 * cpu_reg      CPU register list
 * cpu_mod      CPU modifiers list
 */

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX, MEMSIZE) };

REG cpu_reg[] = {
	{ "РВК",  &RVK,   8, 12, 0, 1 },	/* регистр выборки команды */
	{ "РА",   &RA,    8, 12, 0, 1 },	/* регистр адреса */
	{ "Ω",	  &OMEGA, 8, 1,  0, 1 },	/* Ω */
	{ "РК",   &RK,    8, 45, 0, 1 },	/* регистр команды */
	{ "РР",   &RR,    8, 45, 0, 1 },	/* регистр результата */
	{ "РМР",  &RMR,   8, 45, 0, 1 },	/* регистр младших разрядов (М-220) */
	{ "РПУ1", &RPU1,  8, 45, 0, 1 },	/* регистр 1 пульта управления */
	{ "РПУ2", &RPU2,  8, 45, 0, 1 },	/* регистр 2 пульта управления */
	{ "РПУ3", &RPU3,  8, 45, 0, 1 },	/* регистр 3 пульта управления */
	{ "РПУ4", &RPU4,  8, 45, 0, 1 },	/* регистр 4 пульта управления */
	{ 0 }
};

MTAB cpu_mod[] = {
	{ 0 }
};

DEVICE cpu_dev = {
	"CPU", &cpu_unit, cpu_reg, cpu_mod,
	1, 8, 12, 1, 8, 45,
	&cpu_examine, &cpu_deposit, &cpu_reset,
	NULL, NULL, NULL, NULL,
	DEV_DEBUG
};

/*
 * SCP data structures and interface routines
 *
 * sim_name		simulator name string
 * sim_PC		pointer to saved PC register descriptor
 * sim_emax		maximum number of words for examine/deposit
 * sim_devices		array of pointers to simulated devices
 * sim_stop_messages	array of pointers to stop messages
 * sim_load		binary loader
 */

char sim_name[] = "M-20";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 1;	/* максимальное количество слов в машинной команде */

DEVICE *sim_devices[] = {
	&cpu_dev,
	&drum_dev,
	0
};

const char *sim_stop_messages[] = {
	"Неизвестная ошибка",				/* Unknown error */
	"Останов",					/* STOP */
	"Точка останова",				/* Breakpoint */
	"Выход за пределы памяти",			/* Run out end of memory */
	"Неверный код команды",				/* Invalid instruction */
	"Переполнение при сложении",			/* Addition overflow */
	"Переполнение при сложении порядков",		/* Exponent overflow */
	"Переполнение при умножении",			/* Multiplication overflow */
	"Переполнение при делении",			/* Division overflow */
	"Переполнение мантиссы при делении",		/* Division mantissa overflow */
	"Корень из отрицательного числа",		/* SQRT from negative number */
	"Ошибка вычисления корня",			/* SQRT error */
	"Ошибка чтения барабана",			/* Drum read error */
	"Неверная длина чтения барабана",		/* Invalid drum read length */
	"Неверная длина записи барабана",		/* Invalid drum write length */
	"Ошибка записи барабана",			/* Drum write error */
	"Неверное УЧ для обращения к барабану", 	/* Invalid drum control word */
	"Чтение неинициализированного барабана", 	/* Reading uninialized drum data */
	"Неверное УЧ для обращения к ленте",		/* Invalid tape control word */
	"Неверное УЧ для разметки ленты",		/* Invalid tape format word */
	"Обмен с магнитной лентой не реализован",	/* Tape not implemented */
	"Разметка магнитной ленты не реализована",	/* Tape formatting not implemented */
	"Вывод на перфокарты не реализован",		/* Punch not implemented */
	"Ввод с перфокарт не реализован",		/* Punch reader not implemented */
	"Неверное УЧ",					/* Invalid control word */
	"Неверный аргумент команды",			/* Invalid argument of instruction */
	"Останов по несовпадению",			/* Assertion failed */
	"Команда МБ не работает без МА",		/* MB instruction without MA */
};

/*
 * Memory examine
 */
t_stat cpu_examine (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
	if (addr >= MEMSIZE)
		return SCPE_NXM;
	if (vptr)
		*vptr = M [addr];
	return SCPE_OK;
}

/*
 * Memory deposit
 */
t_stat cpu_deposit (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
	if (addr >= MEMSIZE)
		return SCPE_NXM;
	M [addr] = val;
	return SCPE_OK;
}

/*
 * Reset routine
 */
t_stat cpu_reset (DEVICE *dptr)
{
	RA = 0;
	OMEGA = 0;
	RMR = 0;
	RR = 0;
	sim_brk_types = sim_brk_dflt = SWMASK ('E');
	return SCPE_OK;
}

/*
 * Считывание слова из памяти.
 */
t_value load (int addr)
{
	t_value val;

	addr &= 07777;
	if (addr == 0)
		return 0;

	val = M [addr];
	return val;
}

/*
 * Запись слова в памяти.
 */
void store (int addr, t_value val)
{
	addr &= 07777;
	if (addr == 0)
		return;

	M [addr] = val;
}

/*
 * Проверка числа на равенство нулю.
 */
static inline int is_zero (t_value x)
{
	x &= ~(TAG | SIGN);
	return (x == 0);
}

/*
 * Нормализация числа (влево).
 */
t_value normalize (t_value x)
{
	int exp;
	t_value m;

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
	x |= (t_value) exp << 36 | m;
	return x;
}

/*
 * Сложение двух чисел, с блокировкой округления и нормализации,
 * если требуется.
 */
t_stat addition (t_value *result, t_value x, t_value y, int no_round, int no_norm)
{
	int xexp, yexp, rexp;
	t_value xm, ym, r;

	if (is_zero (x)) {
		if (! no_norm)
			y = normalize (y);
		*result = y | (x & TAG);
		return 0;
	}
	if (is_zero (y)) {
zero_y:		if (! no_norm)
			x = normalize (x);
		*result = x | (y & TAG);
		return 0;
	}
	/* Извлечем порядок чисел. */
	xexp = x >> 36 & 0177;
	yexp = y >> 36 & 0177;
	if (yexp > xexp) {
		/* Пусть x - большее, а y - меньшее число (по модулю). */
		t_value t = x;
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
			if (rexp > 127) {
				/* переполнение при сложении */
				return STOP_ADDOVF;
			}
		}
	}

	/* Конструируем результат. */
	r |= (t_value) rexp << 36;
	r ^= (x & SIGN);
	if (! no_norm)
		r = normalize (r);
	*result = r | ((x | y) & TAG);
	return 0;
}

/*
 * Коррекция порядка.
 */
t_stat add_exponent (t_value *result, t_value x, int n)
{
	int exp;

	exp = (int) (x >> 36 & 0177) + n;
	if (exp > 127) {
		/* переполнение при сложении порядков */
		return STOP_EXPOVF;
	}
	if (exp < 0 || (x & MANTISSA) == 0) {
		/* Ноль. */
		x &= TAG;
	}
	*result = x;
	return 0;
}

/*
 * Умножение двух 36-битовых целых чисел, с выдачей двух половин результата.
 */
void mul36x36 (t_value x, t_value y, t_value *hi, t_value *lo)
{
	int yhi, ylo;
	t_value rhi, rlo;

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
t_stat multiplication (t_value *result, t_value x, t_value y, int no_round, int no_norm)
{
	int xexp, yexp, rexp;
	t_value xm, ym, r;

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
		*result = (x | y) & TAG;
		return 0;
	}
	if (rexp > 127) {
		/* переполнение при умножении */
		return STOP_MULOVF;
	}

	/* Конструируем результат. */
	r |= (t_value) rexp << 36;
	r |= ((x ^ y) & SIGN) | ((x | y) & TAG);
	RMR |= (t_value) rexp << 36;
	RMR |= ((x ^ y) & SIGN) | ((x | y) & TAG);
	*result = r;
	return 0;
}

/*
 * Деление двух чисел, с блокировкой округления, если требуется.
 */
t_stat division (t_value *result, t_value x, t_value y, int no_round)
{
	int xexp, yexp, rexp;
	t_value xm, ym, r;

	/* Извлечем порядок чисел. */
	xexp = x >> 36 & 0177;
	yexp = y >> 36 & 0177;

	/* Извлечем мантиссу чисел. */
	xm = x & MANTISSA;
	ym = y & MANTISSA;
	if (xm >= 2*ym) {
		/* переполнение мантиссы при делении */
		return STOP_DIVMOVF;
	}
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
		*result = (x | y) & TAG;
		return 0;
	}
	if (rexp > 127) {
		/* переполнение при делении */
		return STOP_DIVOVF;
	}

	/* Конструируем результат. */
	r |= (t_value) rexp << 36;
	r |= ((x ^ y) & SIGN) | ((x | y) & TAG);
	*result = r;
	return 0;
}

/*
 * Вычисление квадратного корня, с блокировкой округления, если требуется.
 */
t_stat square_root (t_value *result, t_value x, int no_round)
{
	int exp;
	t_value r;
	double q;

	if (x & SIGN) {
		/* корень из отрицательного числа */
		return STOP_NEGSQRT;
	}

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
	r = (t_value) q;
	if (! no_round) {
		/* Смотрим остаток. */
		if (q - r >= 0.5) {
			/* Округление. */
			r += 1;
		}
	}
	if (r == 0) {
		/* Нуль. */
		*result = x & TAG;
		return 0;
	}
	if (r & ~MANTISSA) {
		/* ошибка квадратного корня */
		return STOP_SQRTERR;
	}

	/* Конструируем результат. */
	r |= (t_value) exp << 36;
	r |= x & TAG;
	*result = r;
	return 0;
}

double m20_to_ieee (t_value word)
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

/*
 * Печать десятичных чисел. Из книги Ляшенко:
 * "В одной строке располагается информация из восьми ячеек памяти.
 * Каждое десятичное число из ячейки занимает на бумаге 14 позиций,
 * промежуток между числами занимает две позиции. В первых трёх
 * позициях располагаются признак, знак числа, знак порядка.
 * Минус в первой позиции означает, что число имеет признак."
 */
void print_decimal (int first, int last)
{
	int n;
	t_value x;
	double d;

	/* Не будем бороться за совместимость, сделаем по-современному. */
	for (n=0; ; ++n) {
		x = load (first + n);
		d = m20_to_ieee (x);
		putchar (x & TAG ? '#' : ' ');
		printf ("%13e", d);
		if (first + n >= last) {
			printf ("\r\n");
			break;
		}
		printf ((n & 7) == 7 ? "\r\n" : "  ");
	}
}

/*
 * Печать восьмеричных чисел. Из книги Ляшенко:
 * "В одной строке располагается информация из 8 ячеек памяти.
 * Каждое число занимает 15 позиций с интервалом между числами
 * в одну позицию."
 */
void print_octal (int first, int last)
{
	int n;
	t_value x;

	for (n=0; ; ++n) {
		x = load (first + n);
		printf ("%015llo", x);
		if (first + n >= last) {
			printf ("\r\n");
			break;
		}
		printf ((n & 7) == 7 ? "\r\n" : " ");
	}
}

/*
 * Write Unicode symbol to file.
 * Convert to UTF-8 encoding:
 * 00000000.0xxxxxxx -> 0xxxxxxx
 * 00000xxx.xxyyyyyy -> 110xxxxx, 10yyyyyy
 * xxxxyyyy.yyzzzzzz -> 1110xxxx, 10yyyyyy, 10zzzzzz
 */
static void
utf8_putc (unsigned ch, FILE *fout)
{
	if (ch < 0x80) {
		putc (ch, fout);
		return;
	}
	if (ch < 0x800) {
		putc (ch >> 6 | 0xc0, fout);
		putc ((ch & 0x3f) | 0x80, fout);
		return;
	}
	putc (ch >> 12 | 0xe0, fout);
	putc (((ch >> 6) & 0x3f) | 0x80, fout);
	putc ((ch & 0x3f) | 0x80, fout);
}

/*
 * Печать текстовых данных в кодировке ГОСТ.
 */
void print_text (int first, int last)
{
	int n, i, c;
	t_value x;

	/* GOST-10859 encoding.
	 * Documentation: http://en.wikipedia.org/wiki/GOST_10859 */
	static const unsigned short gost_to_unicode_cyr [128] = {
/* 000-007 */	0x30,   0x31,   0x32,   0x33,   0x34,   0x35,   0x36,   0x37,
/* 010-017 */	0x38,   0x39,   0x2b,   0x2d,   0x2f,   0x2c,   0x2e,   0x20,
/* 020-027 */	0x65,   0x2191, 0x28,   0x29,   0xd7,   0x3d,   0x3b,   0x5b,
/* 030-037 */	0x5d,   0x2a,   0x2018, 0x2019, 0x2260, 0x3c,   0x3e,   0x3a,
/* 040-047 */	0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
/* 050-057 */	0x0418, 0x0419, 0x041a, 0x041b, 0x041c, 0x041d, 0x041e, 0x041f,
/* 060-067 */	0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
/* 070-077 */	0x0428, 0x0429, 0x042b, 0x042c, 0x042d, 0x042e, 0x042f, 0x44,
/* 100-107 */	0x46,   0x47,   0x49,   0x4a,   0x4c,   0x4e,   0x51,   0x52,
/* 110-117 */	0x53,   0x55,   0x56,   0x57,   0x5a,   0x203e, 0x2264, 0x2265,
/* 120-127 */	0x2228, 0x2227, 0x2283, 0xac,   0xf7,   0x2261, 0x25,   0x25c7,
/* 130-137 */	0x7c,   0x2015, 0x5f,   0x21,   0x22,   0x042a, 0xb0,   0x2032,
	};

	for (n=0; ; ++n) {
		x = load (first + n);
		for (i=0; i<6; ++i) {
			c = x >> (35 - 7*i) & 0177;
			c = gost_to_unicode_cyr [c];
			if (! c)
				c = ' ';
			/* Assume we have UTF-8 locale. */
			utf8_putc (c, stdout);
		}
		if (first + n >= last) {
			printf ("\r\n");
			break;
		}
		if ((n & 127) == 127)
			printf ("\r\n");
	}
}

/*
 * Подготовка обращения к внешнему устройству.
 * В условном числе должен быть задан один из пяти видов работы:
 * барабан, лента, разметка ленты, печать или перфорация.
 */
t_stat ext_setup (int a1, int a2, int a3)
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
		    EXT_TAPE_FORMAT | EXT_TAPE)) {
			/* неверное УЧ для обращения к барабану */
			return STOP_DRUMINVAL;
		}
	}
	if (ext_op & EXT_TAPE) {
		if (ext_op & (EXT_PUNCH | EXT_PRINT | EXT_TAPE_FORMAT)) {
			/* неверное УЧ для обращения к ленте */
			return STOP_TAPEINVAL;
		}
	}
	if (ext_op & EXT_PRINT) {
		/* При печати не имеют значения признаки записи и
		 * обратного направления движения ленты. */
		ext_op &= ~(EXT_WRITE | EXT_TAPE_REV);

	} else if (ext_op & EXT_TAPE_FORMAT) {
		/* При разметке ленты не имеют значения признаки записи,
		 * блокировки останова и обратного направления движения. */
		ext_op &= ~(EXT_WRITE | EXT_DIS_STOP | EXT_TAPE_REV);
		if (ext_op & (EXT_PUNCH | EXT_PRINT | EXT_DIS_CHECK)) {
			/* неверное УЧ для разметки ленты */
			return STOP_TAPEFMTINVAL;
		}
	}
	if (ext_op & EXT_PUNCH) {
		/* При перфорации не имеют значения признаки записи,
		 * блокировки останова и обратного направления движения. */
		ext_op &= ~(EXT_WRITE | EXT_DIS_STOP | EXT_TAPE_REV);
	}
	return 0;
}

/*
 * Выполнение обращения к внешнему устройству.
 * В случае ошибки чтения возвращается STOP_READERR.
 * Контрольная сумма записи накапливается в параметре sum (не реализовано).
 * Блокировка памяти (EXT_DIS_RAM) и блокировка контроля (EXT_DIS_CHECK)
 * пока не поддерживаются.
 */
t_stat ext_io (int a1, t_value *sum)
{
	ext_ram_start = a1;

	*sum = 0;

	if (ext_op & EXT_DRUM) {
		/* Барабан */
		return drum (sum);
		/*if (ext_op & EXT_WRITE) {
			return drum_write ((ext_op & EXT_UNIT) << 12 | ext_disk_addr,
				ext_ram_start, ext_ram_finish,
				(ext_op & EXT_DIS_CHECK) ? 0 : sum);
		} else {
			return drum_read ((ext_op & EXT_UNIT) << 12 | ext_disk_addr,
			    ext_ram_start, ext_ram_finish,
			    (ext_op & EXT_DIS_CHECK) ? 0 : sum);
		}*/
	} else if (ext_op & EXT_TAPE) {
		/* Работа с магнитной лентой не поддерживается */
		return STOP_TAPEUNSUPP;

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
		return 0;

	} else if (ext_op & EXT_PUNCH) {
		/* Вывод на перфокарты не поддерживается */
		return STOP_PUNCHUNSUPP;

	} else if (ext_op & EXT_TAPE_FORMAT) {
		/* Разметка ленты не поддерживается */
		return STOP_TAPEFMTUNSUPP;

	} else {
		/* Неверное УЧ для инструкции МБ */
		return STOP_EXTINVAL;
	}
	return 0;
}

/*
 * Execute one instruction, contained in register RK.
 */
t_stat cpu_one_inst ()
{
	int flags, op, a1, a2, a3, n = 0;
	t_value x, y;
	t_stat err;

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
		return STOP_BADCMD;
	/*
	 * Логические операции.
	 */
	case 000: /* пересылка */
		RR = load (a1);
		store (a3, RR);
		/* Омега не изменяется. */
		delay += 24;
		break;
	case 020: /* чтение пультовых тумблеров */
		switch (a1) {
		case 0: RR = 0;    break;
		case 1: RR = RPU1; break;
		case 2: RR = RPU2; break;
		case 3: RR = RPU3; break;
		case 4: RR = RPU4; break;
		case 5: /* RR */   break;
		default: return STOP_INVARG; /* неверный аргумент команды СЧП */
		}
		store (a3, RR);
		/* Омега не изменяется. */
		delay += 24;
		break;
	case 015: /* поразрядное сравнение (исключающее или) */
	case 035: /* поразрядное сравнение с остановом */
		RR = load (a1) ^ load (a2);
logop:		store (a3, RR);
		OMEGA = (RR == 0);
		delay += 24;
		if (op == 035 && ! OMEGA)
			return STOP_ASSERT; /* останов по несовпадению */
		break;
	case 055: /* логическое умножение (и) */
		RR = load (a1) & load (a2);
		goto logop;
	case 075: /* логическое сложение (или) */
		RR = load (a1) | load (a2);
		goto logop;
	case 013: /* сложение команд */
		x = load (a1);
		y = (x & MANTISSA) + (load (a2) & MANTISSA);
addm:		RR = (x & ~MANTISSA) | (y & MANTISSA);
		store (a3, RR);
		OMEGA = (y & BIT37) != 0;
		delay += 24;
		break;
	case 033: /* вычитание команд */
		x = load (a1);
		y = (x & MANTISSA) - (load (a2) & MANTISSA);
		goto addm;
	case 053: /* сложение кодов операций */
		x = load (a1);
		y = (x & ~MANTISSA) + (load (a2) & ~MANTISSA);
addop:		RR = (x & MANTISSA) | (y & ~MANTISSA & WORD);
		store (a3, RR);
		OMEGA = (y & BIT46) != 0;
		delay += 24;
		break;
	case 073: /* вычитание кодов операций */
		x = load (a1);
		y = (x & ~MANTISSA) - (load (a2) & ~MANTISSA);
		goto addop;
	case 014: /* сдвиг мантиссы по адресу */
		n = (a1 & 0177) - 64;
		delay += 61.5 + 1.5 * (n>0 ? n : -n);
shm:		y = load (a2);
		RR = (y & ~MANTISSA);
		if (n > 0)
			RR |= (y & MANTISSA) << n;
		else if (n < 0)
			RR |= (y & MANTISSA) >> -n;
		store (a3, RR);
		OMEGA = ((RR & MANTISSA) == 0);
		break;
	case 034: /* сдвиг мантиссы по порядку числа */
		n = (int) (load (a1) >> 36 & 0177) - 64;
		delay += 24 + 1.5 * (n>0 ? n : -n);
		goto shm;
	case 054: /* сдвиг по адресу */
		n = (a1 & 0177) - 64;
		delay += 61.5 + 1.5 * (n>0 ? n : -n);
shift:		RR = load (a2);
		if (n > 0)
			RR = (RR << n) & WORD;
		else if (n < 0)
			RR >>= -n;
		store (a3, RR);
		OMEGA = (RR == 0);
		break;
	case 074: /* сдвиг по порядку числа */
		n = (int) (load (a1) >> 36 & 0177) - 64;
		delay += 24 + 1.5 * (n>0 ? n : -n);
		goto shift;
	case 007: /* циклическое сложение */
		x = load (a1);
		y = load (a2);
		RR = (x & ~MANTISSA) + (y & ~MANTISSA);
		y = (x & MANTISSA) + (y & MANTISSA);
csum:		if (RR & BIT46)
			RR += BIT37;
		if (y & BIT37)
			y += 1;
		RR &= WORD;
		RR |= y & MANTISSA;
		store (a3, RR);
		OMEGA = (y & BIT37) != 0;
		delay += 24;
		break;
	case 027: /* циклическое вычитание */
		x = load (a1);
		y = load (a2);
		RR = (x & ~MANTISSA) - (y & ~MANTISSA);
		y = (x & MANTISSA) - (y & MANTISSA);
		goto csum;
	case 067: /* циклический сдвиг */
		x = load (a1);
		RR = (x & 07777777) << 24 | (x >> 24 & 07777777);
		store (a3, RR);
		/* Омега не изменяется. */
		delay += 60;
		break;
	/*
	 * Операции управления.
	 * Омега не изменяется.
	 */
	case 016: /* передача управления с возвратом */
		RR = 016000000000000LL | (a1 << 12);
		store (a3, RR);
		RVK = a2;
		delay += 24;
		break;
	case 036: /* передача управления по условию Ω=1 */
		RR = load (a1);
		store (a3, RR);
		if (OMEGA)
			RVK = a2;
		delay += 24;
		break;
	case 056: /* передача управления */
		RR = load (a1);
		store (a3, RR);
		RVK = a2;
		delay += 24;
		break;
	case 076: /* передача управления по условию Ω=0 */
		RR = load (a1);
		store (a3, RR);
		if (! OMEGA)
			RVK = a2;
		delay += 24;
		break;
	case 077: /* останов машины */
		RR = 0;
		store (a3, RR);
		delay += 24;
		/* Если адреса равны 0, считаем что это штатная,
		 * "хорошая" остановка.*/
		return STOP_STOP;
	case 011: /* переход по < и Ω=1 */
		if (RA < a1 && OMEGA)
			RVK = a2;
		RA = a3;
		delay += 24;
		break;
	case 031: /* переход по >= и Ω=1 */
		if (RA >= a1 && OMEGA)
			RVK = a2;
		RA = a3;
		delay += 24;
		break;
	case 051: /* переход по < и Ω=0 */
		if (RA < a1 && ! OMEGA)
			RVK = a2;
		RA = a3;
		delay += 24;
		break;
	case 071: /* переход по >= и Ω=0 */
		if (RA >= a1 && ! OMEGA)
			RVK = a2;
		RA = a3;
		delay += 24;
		break;
	case 012: /* переход по < */
		if (RA < a1)
			RVK = a2;
		RA = a3;
		delay += 24;
		break;
	case 032: /* переход по >= */
		if (RA >= a1)
			RVK = a2;
		RA = a3;
		delay += 24;
		break;
	case 052: /* установка регистра адреса адресом */
		RR = 052000000000000LL | (a1 << 12);
		store (a3, RR);
		RA = a2;
		delay += 24;
		break;
	case 072: /* установка регистра адреса числом */
		RR = 052000000000000LL | (a1 << 12);
		store (a3, RR);
		RA = load (a2) >> 12 & 07777;
		delay += 24;
		break;
	case 010: /* ввод с перфокарт */
	case 030: /* ввод с перфокарт без проверки к.суммы */
		/* ввод с перфокарт не поддерживается */
		return STOP_RPUNCHUNSUPP;
	case 050: /* подготовка обращения к внешнему устройству */
		err = ext_setup (a1, a2, a3);
		if (err)
			return err;
		delay += 24;
		return 0;
	case 070: /* выполнение обращения к внешнему устройству */
		if (ext_op == 07777) {
			return STOP_MBINVAL;
		}
		err = ext_io (a1, &RR);
		if (err) {
			if (err != STOP_READERR ||
			    ! (ext_op & EXT_DIS_STOP))
				return err;
			if (a2)
				RVK = a2;
		}
		if ((ext_op & EXT_WRITE) && ! (ext_op & EXT_DIS_CHECK))
			store (a3, RR);
		delay += 24;
		break;
	/*
	 * Арифметические операции.
	 */
	case 001: /* сложение с округлением и нормализацией */
	case 021: /* сложение без округления с нормализацией */
	case 041: /* сложение с округлением без нормализации */
	case 061: /* сложение без округления и без нормализации */
		x = load (a1);
		y = load (a2);
add:		err = addition (&RR, x, y, op >> 4 & 1, op >> 5 & 1);
		if (err)
			return err;
		store (a3, RR);
		OMEGA = (RR & SIGN) != 0;
		delay += 29.5;
		break;
	case 002: /* вычитание с округлением и нормализацией */
	case 022: /* вычитание без округления с нормализацией */
	case 042: /* вычитание с округлением без нормализации */
	case 062: /* вычитание без округления и без нормализации */
		x = load (a1);
		y = load (a2) ^ SIGN;
		goto add;
	case 003: /* вычитание модулей с округлением и нормализацией */
	case 023: /* вычитание модулей без округления с нормализацией */
	case 043: /* вычитание модулей с округлением без нормализации */
	case 063: /* вычитание модулей без округления и без нормализации */
		x = load (a1) & ~SIGN;
		y = load (a2) | SIGN;
		goto add;
	case 005: /* умножение с округлением и нормализацией */
	case 025: /* умножение без округления с нормализацией */
	case 045: /* умножение с округлением без нормализации */
	case 065: /* умножение без округления и без нормализации */
		x = load (a1);
		y = load (a2);
		err = multiplication (&RR, x, y, op >> 4 & 1, op >> 5 & 1);
		if (err)
			return err;
		store (a3, RR);
		OMEGA = (int) (RR >> 36 & 0177) > 0100;
		delay += 70;
		break;
	case 004: /* деление с округлением */
	case 024: /* деление без округления */
		x = load (a1);
		y = load (a2);
		err = division (&RR, x, y, op >> 4 & 1);
		if (err)
			return err;
		store (a3, RR);
		OMEGA = (int) (RR >> 36 & 0177) > 0100;
		delay += 136;
		break;
	case 044: /* извлечение корня с округлением */
	case 064: /* извлечение корня без округления */
		x = load (a1);
		err = square_root (&RR, x, op >> 4 & 1);
		if (err)
			return err;
		store (a3, RR);
		OMEGA = (int) (RR >> 36 & 0177) > 0100;
		delay += 275;
		break;
	case 047: /* выдача младших разрядов произведения */
		RR = RMR;
		store (a3, RR);
		OMEGA = (RR & MANTISSA) == 0;
		delay += 24;
		break;
	case 006: /* сложение порядка с адресом */
		n = (a1 & 0177) - 64;
		y = load (a2);
addexp:		err = add_exponent (&RR, y, n);
		if (err)
			return err;
		store (a3, RR);
		OMEGA = (int) (RR >> 36 & 0177) > 0100;
		delay += 61.5;
		break;
	case 026: /* сложение порядков чисел */
		x = load (a2);
		n = (int) (x >> 36 & 0177) - 64;
		y = load (a2) | (x & TAG);
		goto addexp;
	case 046: /* вычитание адреса из порядка */
		n = 64 - (a1 & 0177);
		y = load (a2);
		goto addexp;
	case 066: /* вычитание порядков чисел */
		x = load (a2);
		n = 64 - (int) (x >> 36 & 0177);
		y = load (a2) | (x & TAG);
		goto addexp;
	}
	ext_op = 07777;
	return 0;
}

/*
 * Main instruction fetch/decode loop
 */
t_stat sim_instr (void)
{
	t_stat r;
	int ticks;

	/* Restore register state */
	RVK = RVK & 07777;				/* mask RVK */
	sim_cancel_step ();				/* defang SCP step */
	delay = 0;

	/* Main instruction fetch/decode loop */
	for (;;) {
		if (sim_interval <= 0) {		/* check clock queue */
			r = sim_process_event ();
			if (r)
				return r;
		}

		if (RVK >= MEMSIZE) {			/* выход за пределы памяти */
			return STOP_RUNOUT;		/* stop simulation */
		}

		if (sim_brk_summ &&			/* breakpoint? */
		    sim_brk_test (RVK, SWMASK ('E'))) {
			return STOP_IBKPT;		/* stop simulation */
		}

		RK = M [RVK];				/* get instruction */
		if (sim_deb && cpu_dev.dctrl) {
			/*fprintf (sim_deb, "*** (%.0f) %04o: ", sim_gtime(), RVK);*/
			fprintf (sim_deb, "*** %04o: ", RVK);
			fprint_sym (sim_deb, RVK, &RK, 0, SWMASK ('M'));
			fprintf (sim_deb, "\n");
		}
		RVK += 1;				/* increment RVK */

		r = cpu_one_inst ();
		if (r)					/* one instr; error? */
			return r;

		ticks = 1;
		if (delay > 0)				/* delay to next instr */
			ticks += delay - DBL_EPSILON;
		delay -= ticks;				/* count down delay */
		sim_interval -= ticks;

		if (sim_step && (--sim_step <= 0))	/* do step count */
			return SCPE_STOP;
	}
}
