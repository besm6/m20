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
 * Печать машинного слова.
 */
void print (int addr, uint64_t cmd)
{
	char *m;
	int flags, op, a1, a2, a3;

	flags = cmd >> 42 & 7;
	op = cmd >> 36 & 077;
	a1 = cmd >> 24 & 07777;
	a2 = cmd >> 12 & 07777;
	a3 = cmd & 07777;
	printf ("%04o: %o %02o %04o %04o %04o\t",
		addr, flags, op, a1, a2, a3);

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
	case 050: m = "пву";  break;	/* Подготовка обращения к внешнему устройству */
	case 070: m = "уву";  break;	/* Выполнение обращения к внешнему устройству */
	case 011: m = "цме";  break;	/* Сравнение и установка регистра адреса, переход по < и Ω=1 */
	case 031: m = "цбре"; break;	/* Сравнение и установка регистра адреса, переход по >= и Ω=1 */
	case 051: m = "цмо";  break;	/* Сравнение и установка регистра адреса, переход по < и Ω=0 */
	case 071: m = "цбро"; break;	/* Сравнение и установка регистра адреса, переход по >= и Ω=0 */
	case 012: m = "цм";   break;	/* Сравнение и установка регистра адреса, переход по < */
	case 032: m = "цбр";  break;	/* Сравнение и установка регистра адреса, переход по >= */
	case 052: m = "ура";  break;	/* Установка регистра адреса */
	case 072: m = "ур";   break;	/* Установка регистра адреса по числу */
	case 016: m = "пв";   break;	/* Передача управления с возвратом */
	case 036: m = "пе";   break;	/* Передача управления по условию Ω=1 */
	case 056: m = "пб";   break;	/* Передача управления */
	case 076: m = "по";   break;	/* Передача управления по условию Ω=0 */
	case 077: m = "стоп"; break;	/* Останов машины */

	default:
		printf ("?");
		goto done;
	}

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
usage:          printf ("M-20 Disassembler\n");
		printf ("Usage:\n\tdis20 [-d] infile.m20\n\n");
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
