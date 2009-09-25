/*
 * Дизассемблер для ЭВМ М-20.
 * Copyright (GPL) 2008 Сергей Вакуленко <serge@vak.ru>
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "config.h"
#include "ieee.h"

#define DATSIZE         4096	/* размер памяти в словах */

#define LINE_WORD	1	/* виды строк входного файла */
#define LINE_ADDR	2
#define LINE_START	3

char *infile;
int debug;
int start_address;

uint64_t ram [DATSIZE];
unsigned char ram_dirty [DATSIZE];

void uerror (char *s, ...)
{
	va_list ap;

	va_start (ap, s);
	if (infile)
		fprintf (stderr, "%s: ", infile);
	vfprintf (stderr, s, ap);
	va_end (ap);
	fprintf (stderr, "\n");
	exit (1);
}

/*
 * Пропуск пробелов.
 */
char *skip_spaces (char *p)
{
	if (*p == (char) 0xEF && p[1] == (char) 0xBB && p[2] == (char) 0xBF) {
		/* Skip zero width no-break space. */
		p += 3;
	}
	while (*p == ' ' || *p == '\t')
		++p;
	return p;
}

/*
 * Чтение строки входного файла.
 */
int read_line (int *type, uint64_t *val)
{
	char buf [512], *p;
	int i;
again:
	if (! fgets (buf, sizeof (buf), stdin))
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
		uerror ("invalid input line");

	/* Слово. */
	*type = LINE_WORD;
	*val = *p - '0';
	for (i=0; i<14; ++i) {
		p = skip_spaces (p + 1);
		if (*p < '0' || *p > '7')
			uerror ("short word");
		*val = *val << 3 | (*p - '0');
	}
	return 1;
}

/*
 * Чтение входного файла.
 */
void readimage ()
{
	int addr, type;
	uint64_t word;

	addr = 0;
	while (read_line (&type, &word)) {
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
			uerror ("invalid hex address");
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

const char *opname [64] = {
	"п",	"с",	"в",	"ва",	"д",	"у",	"спа",	"цс",
	"вв",	"пем",	"пм",	"см",	"сдма",	"н",	"пв",	"дпа",
	"пкл",	"со",	"во",	"вао",	"до",	"уо",	"спп",	"цв",
	"ввк",	"пен",	"пн",	"вм",	"сдмп",	"нс",	"пе",	"дпб",
	"пмс",	"сн",	"вн",	"ван",	"к",	"ун",	"впа",	"мрп",
	"ма",	"пум",	"ра",	"ск",	"сдса",	"и",	"пб",	"ирп",
	"пнс",	"сон",	"вон",	"ваон",	"ко",	"уон",	"впп",	"цсд",
	"мб",	"пун",	"рс",	"вк",	"сдсп",	"или",	"пу",	"стоп",
};

/*
 * Печать машинного слова.
 */
void print (int addr, uint64_t cmd)
{
	const char *m;
	int flags, op, a1, a2, a3;

	flags = cmd >> 42 & 7;
	op = cmd >> 36 & 077;
	a1 = cmd >> 24 & 07777;
	a2 = cmd >> 12 & 07777;
	a3 = cmd & 07777;
	printf ("%04o: %o %02o %04o %04o %04o\t",
		addr, flags, op, a1, a2, a3);
	m = opname [op];

	if (! flags && ! a1 && ! a2 && ! a3) {
		/* Команда без аргументов. */
		printf ("%s", m);
		goto done;
	}
	printf ("%s\t", m);
	print_addr (a1, flags & 4);
	if (! (flags & 3) && ! a2 && ! a3) {
		/* Нет аргументов 2 и 3. */
		goto done;
	}

	printf (", ");
	print_addr (a2, flags & 2);
	if (! (flags & 1) && ! a3) {
		/* Нет аргумента 3. */
		goto done;
	}

	printf (", ");
	print_addr (a3, flags & 1);
done:
	printf ("\n");
}

int main (int argc, char **argv)
{
	int i;
	char *cp;

	for (i=1; i<argc; i++)
		switch (argv[i][0]) {
		case '-':
			for (cp=argv[i]; *cp; cp++) switch (*cp) {
			case 'd':
				debug++;
				break;
			}
			break;
		default:
			if (infile)
				goto usage;
			infile = argv[i];
			if (! freopen (infile, "r", stdin))
				uerror ("cannot open");
			break;
		}

	if (! infile) {
usage:          printf ("Дизассемблер M-20\n");
		printf ("Вызов:\n");
		printf ("\tdis20 [-d] infile.m20\n\n");
		return -1;
	}

	readimage ();

	if (start_address != 0)
		printf ("Start address = %04o\n\n", start_address);

	for (i=0; i<DATSIZE; ++i)
		if (ram_dirty [i])
			print (i, ram [i]);
	return 0;
}
