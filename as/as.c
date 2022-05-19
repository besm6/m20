/*
 * Assembler for M-20 computer.
 * Copyright (GPL) 2008 Сергей Вакуленко <serge.vakulenko@gmail.com>
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <wchar.h>
#include "config.h"
#include "encoding.h"
#include "gost10859.h"
#include "ieee.h"

#define STSIZE          2000    /* размер таблицы символов */
#define DATSIZE         4096    /* размер памяти в словах */
#define MAXREL          10240   /* макс. перемещений */
#define MAXLIBS         10      /* макс. библиотек */
#define MAXLABELS       1000    /* макс. цифровых меток */

/*
 * Lexical items.
 */
enum {
	LEOF,		/* конец файла */
	LEOL,		/* конец строки */
	LNUM,		/* число */
	LNAME,		/* имя */
	LCMD,		/* машинная инструкция */
	LLSHIFT,	/* << */
	LRSHIFT,	/* >> */
	LEQU,		/* .это */
	LDATA,		/* .перем */
	LCONST,		/* .вещ */
	LORG,		/* .адрес */
	LTEXT,		/* .текст */
};

/*
 * Symbol/expression types.
 */
#define TUNDF	0	/* неизвестный символ */
#define TABS	1	/* константа */
#define TTEXT	2	/* метка или функция */

/*
 * Relocation flags.
 */
#define RA1	1	/* адрес 1 */
#define RA2	2	/* адрес 2 */
#define RA3	4	/* адрес 3 */
#define RRA	8	/* регистр адреса */
#define RLAB	16	/* относительная метка */

struct stab {
	wchar_t *name;
	int len;
	int type;
	int value;
} stab [STSIZE];

struct labeltab {
	int num;
	int value;
} labeltab [MAXLABELS];

struct reltab {
	int addr;
	int sym;
	int flags;
} reltab [MAXREL];

struct libtab {
	char *name;
} libtab [MAXLIBS];

char *infile, *infile1, *outfile;
int debug;
int line;
int filenum;
int count = 1;
int reached;
int blexflag, backlex, blextype;
wchar_t name [256];
int intval;
int extref;
int extflag;
int stabfree;
int nrel;
int nlib;
int nlabels;
int outaddr;

uint64_t ram [DATSIZE];
unsigned char ram_dirty [DATSIZE];

void parse (void);
void relocate (void);
void libraries (void);
void output (void);
void makecmd (int code);
int getexpr (int *s);

/*
 * Инструкции M-20.
 */
const wchar_t *opname [64] = {
	L"п",	L"с",	L"в",	L"ва",	L"д",	L"у",	L"спа",	L"цс",
	L"вв",	L"пем",	L"пм",	L"см",	L"сдма",L"н",	L"пв",	L"дпа",
	L"пкл",	L"со",	L"во",	L"вао",	L"до",	L"уо",	L"спп",	L"цв",
	L"ввк",	L"пен",	L"пн",	L"вм",	L"сдмп",L"нс",	L"пе",	L"дпб",
	L"пмс",	L"сн",	L"вн",	L"ван",	L"к",	L"ун",	L"впа",	L"мрп",
	L"ма",	L"пум",	L"ра",	L"ск",	L"сдса",L"и",	L"пб",	L"ирп",
	L"пнс",	L"сон",	L"вон",	L"ваон",L"ко",	L"уон",	L"впп",	L"цсд",
	L"мб",	L"пун",	L"рс",	L"вк",	L"сдсп",L"или",	L"пу",	L"стоп",
};

void uerror (char *s, ...)
{
	va_list ap;

	va_start (ap, s);
	fprintf (stderr, "as: ");
	if (infile)
		fprintf (stderr, "%s, ", infile);
	fprintf (stderr, "%d: ", line);
	vfprintf (stderr, s, ap);
	va_end (ap);
	fprintf (stderr, "\n");
	if (outfile)
		unlink (outfile);
	exit (1);
}

/*
 * Look up the symbol.
 */
int lookname ()
{
	int i, len;
	struct stab *s;

	len = wcslen (name);
	s = 0;
	if (name[0] == 'L')
		name[0] = '.';
	for (i=0; i<stabfree; ++i) {
		if (! stab[i].len && ! s) {
			s = stab+i;
			continue;
		}
		if (name[0] == '.' && stab[i].len == len+1 &&
		    stab[i].name[0] == 'A'+filenum &&
		    ! wcscmp (stab[i].name+1, name))
			return i;
		if (stab[i].len == len && ! wcscmp (stab[i].name, name))
			return i;
	}
	if (! s)
		s = stab + stabfree++;

	/* Add the new symbol. */
	if (s >= stab + STSIZE)
		uerror ("таблица символов переполнена");
	s->name = malloc ((2 + len) * sizeof (wchar_t));
	if (! s->name)
		uerror ("мало памяти");
	s->len = len;
	if (name[0] == '.') {
		s->name[0] = 'A' + filenum;
		wcscpy (s->name+1, name);
		++s->len;
	} else
		wcscpy (s->name, name);
	s->value = 0;
	s->type = 0;
	return s - stab;
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
			case 'o':
				if (cp [1]) {
					/* -ofile */
					outfile = cp+1;
					while (*++cp);
					--cp;
				} else if (i+1 < argc)
					/* -o file */
					outfile = argv[++i];
				break;
			case 'l':
				if (nlib >= MAXLIBS)
					uerror ("слишком много библиотек");
				if (cp [1]) {
					/* -lname */
					libtab[nlib++].name = cp+1;
					while (*++cp);
					--cp;
				} else if (i+1 < argc)
					/* -l name */
					libtab[nlib++].name = argv[++i];
				break;
			}
			break;
		default:
			infile = argv[i];
			if (! infile1)
				infile1 = infile;
			if (! freopen (infile, "r", stdin))
				uerror ("не могу открыть");
			line = 1;
			parse ();
			infile = 0;
			++filenum;
			break;
		}

	if (! outfile) {
		if (! infile1) {
			printf ("Ассемблер М-20\n");
			printf ("Вызов:\n");
			printf ("\tas20 [-d] [-o outfile.m20] [-l dir] infile.s ...\n\n");
			return -1;
		}
		outfile = malloc (4 + strlen (infile1));
		if (! outfile)
			uerror ("мало памяти");
		strcpy (outfile, infile1);
		cp = strrchr (outfile, '.');
		if (! cp)
			cp = outfile + strlen (outfile);
		strcpy (cp, ".m20");
		if (debug)
			fprintf (stderr, "запись %s\n", outfile);
	}

	if (! freopen (outfile, "w", stdout))
		uerror ("не могу открыть %s", outfile);

	if (! nlib)
		libtab[nlib++].name = "/usr/local/lib/m20";
	libraries ();
	relocate ();
	output ();
	return 0;
}

int lookcmd ()
{
	int i;

	for (i=0; i<64; ++i)
		if (! wcscmp (opname[i], name))
			return i;
	return -1;
}

int hexdig (int c)
{
	if (c <= '9')      return c - '0';
	else if (c <= 'F') return c - 'A' + 10;
	else               return c - 'a' + 10;
}

int is_hex (int c)
{
	if (c >= '0' && c <= '9')
		return 1;
	if (c >= 'A' && c <= 'F')
		return 1;
	if (c >= 'a' && c <= 'f')
		return 1;
	return 0;
}

int is_digit (int c)
{
	if (c >= '0' && c <= '9')
		return 1;
	return 0;
}

int is_octal (int c)
{
	if (c >= '0' && c <= '7')
		return 1;
	return 0;
}

int is_letter (int c)
{
	if (c >= 'a' && c <= 'z')
		return 1;
	if (c >= 'A' && c <= 'Z')
		return 1;
	if (c == '.' || c == '_' || c >= 256)
		return 1;
	return 0;
}

/*
 * Read the integer value.
 * 1234   - decimal
 * 01234  - octal
 * 0x1234 - hexadecimal
 * 0b1101 - binary
 */
void getnum (int c)
{
	intval = 0;
	if (c == '0') {
		c = unicode_getc (stdin);
		if (c == 'x' || c == 'X') {
			while (is_hex (c = unicode_getc (stdin)))
				intval = intval*16 + hexdig (c);
			if (c >= 0)
				unicode_ungetc (c);
			return;
		}
		if (c == 'b' || c == 'B') {
			while ((c = unicode_getc (stdin)) == '0' || c == '1')
				intval = intval*2 + c - '0';
			if (c >= 0)
				unicode_ungetc (c);
			return;
		}
		if (c >= 0)
			unicode_ungetc (c);
		while (is_octal (c = unicode_getc (stdin)))
			intval = intval*8 + hexdig (c);
		if (c >= 0)
			unicode_ungetc (c);
		return;
	}
	if (c >= 0)
		unicode_ungetc (c);
	while (is_digit (c = unicode_getc (stdin)))
		intval = intval*10 + hexdig (c);
	if (c >= 0)
		unicode_ungetc (c);
}

/*
 * Read real value.
 * Return M-20 word.
 */
uint64_t getreal ()
{
	int c;
	char buf [80], *p;

	do {
		c = unicode_getc (stdin);
	} while (c == ' ' || c == '\t');

	p = buf;
	while (c < 256 && strchr ("0123456789.+-eE", c)) {
		*p++ = c;
		c = unicode_getc (stdin);
	}
	unicode_ungetc (c);
	*p = 0;
	return ieee_to_m20 (strtod (buf, 0));
}

void getname (int c, int extname)
{
	wchar_t *cp;

	for (cp=name; c>' ' && c!=':'; c=unicode_getc (stdin)) {
		if (! extname && ! is_letter (c) && ! is_digit (c))
			break;
		*cp++ = c;
	}
	*cp = 0;
	unicode_ungetc (c);
}

/*
 * Read the next lexical item from the input stream.
 */
int getlex (int *pval, int extname)
{
	int c;

	if (blexflag) {
		blexflag = 0;
		*pval = blextype;
		return backlex;
	}
	for (;;) switch (c = unicode_getc (stdin)) {
	case ';':
	case '#':
skiptoeol:      while ((c = unicode_getc (stdin)) != '\n')
			if (c == EOF)
				return LEOF;
	case '\n':
		++line;
		c = unicode_getc (stdin);
		if (c == '#')
			goto skiptoeol;
		unicode_ungetc (c);
		*pval = line;
		return LEOL;
	case ' ':
	case '\t':
		continue;
	case EOF:
		return LEOF;
	case '<':
		if ((c = unicode_getc (stdin)) == '<')
			return LLSHIFT;
		unicode_ungetc (c);
		return '<';
	case '>':
		if ((c = unicode_getc (stdin)) == '>')
			return LRSHIFT;
		unicode_ungetc (c);
		return '>';
	case '\'':
		c = unicode_getc (stdin);
		if (c == '\'')
			uerror ("неверная символьная константа");
		if (c == '\\')
			switch (c = unicode_getc (stdin)) {
			case 'a':  c = 0x07; break;
			case 'b':  c = '\b'; break;
			case 'f':  c = '\f'; break;
			case 'n':  c = '\n'; break;
			case 'r':  c = '\r'; break;
			case 't':  c = '\t'; break;
			case 'v':  c = '\v'; break;
			case '\'': break;
			case '\\': break;
			default: uerror ("неверная символьная константа");
			}
		if (unicode_getc (stdin) != '\'')
			uerror ("неверная символьная константа");
		intval = c;
		return LNUM;
	case '*':       case '/':       case '%':       case '\\':
	case '^':       case '&':       case '|':       case '~':
	case '"':       case ',':       case '[':       case ']':
	case '(':       case ')':       case '{':       case '}':
	case '=':       case ':':       case '+':       case '-':
	case '@':
		return c;
	case '0':       case '1':       case '2':       case '3':
	case '4':       case '5':       case '6':       case '7':
	case '8':       case '9':
		getnum (c);
		return LNUM;
	default:
		if (! is_letter (c))
			uerror ("неверный символ: \\u%x", c);
		getname (c, extname);
		if (name[0] == '.') {
			if (! name[1]) return '.';
			if (! wcscmp (name, L".это"))   return LEQU;
			if (! wcscmp (name, L".перем")) return LDATA;
			if (! wcscmp (name, L".адрес")) return LORG;
			if (! wcscmp (name, L".вещ"))   return LCONST;
			if (! wcscmp (name, L".текст")) return LTEXT;
		}
		if ((*pval = lookcmd()) != -1)
			return LCMD;
		*pval = lookname ();
		return LNAME;
	}
}

void ungetlex (int val, int type)
{
	blexflag = 1;
	backlex = val;
	blextype = type;
}

/*
 * Get the expression term.
 */
int getterm ()
{
	int cval, s;

	switch (getlex (&cval, 0)) {
	default:
		uerror ("пропущен операнд");
	case LNUM:
		cval = unicode_getc (stdin);
		if (cval == L'в' || cval == L'н') {
			if (cval == L'н')
				extref = -intval;
			else
				extref = intval;
			extflag |= RLAB;
			intval = 0;
			return TUNDF;
		}
		unicode_ungetc (cval);
		return TABS;
	case LNAME:
		if (stab[cval].type == TUNDF) {
			intval = 0;
			extref = cval;
		} else
			intval = stab[cval].value;
		return stab[cval].type;
	case '.':
		intval = count;
		return TTEXT;
	case '@':
		extflag |= RRA;
		intval = 0;
		return TABS;
	case '(':
		getexpr (&s);
		if (getlex (&cval, 0) != ')')
			uerror ("неверно расставлены скобки");
		return s;
	}
}

/*
 * Read the expression.
 * Put the type of expression into *s.
 *
 * expr = [term] {op term}...
 * term = LNAME | LNUM | "." | "(" expr ")"
 * op   = "+" | "-" | "&" | "|" | "^" | "~" | "<<" | ">>" | "/" | "*" | "%"
 */
int getexpr (int *s)
{
	int clex, cval, s2, rez;

	/* Get the first item. */
	switch (clex = getlex (&cval, 0)) {
	default:
		ungetlex (clex, cval);
		rez = 0;
		*s = TABS;
		break;
	case LNUM:
	case LNAME:
	case '.':
	case '(':
	case '@':
		ungetlex (clex, cval);
		*s = getterm ();
		rez = intval;
		break;
	}
	for (;;) {
		switch (clex = getlex (&cval, 0)) {
		case '+':
			s2 = getterm ();
			if (*s == TABS) *s = s2;
			else if (s2 != TABS)
				uerror ("слишком сложное выражение");
			rez += intval;
			break;
		case '-':
			s2 = getterm ();
			if (s2 != TABS)
				uerror ("слишком сложное выражение");
			rez -= intval;
			break;
		case '&':
			s2 = getterm ();
			if (*s != TABS || s2 != TABS)
				uerror ("слишком сложное выражение");
			rez &= intval;
			break;
		case '|':
			s2 = getterm ();
			if (*s != TABS || s2 != TABS)
				uerror ("слишком сложное выражение");
			rez |= intval;
			break;
		case '^':
			s2 = getterm ();
			if (*s != TABS || s2 != TABS)
				uerror ("слишком сложное выражение");
			rez ^= intval;
			break;
		case '~':
			s2 = getterm ();
			if (*s != TABS || s2 != TABS)
				uerror ("слишком сложное выражение");
			rez ^= ~intval;
			break;
		case LLSHIFT:
			s2 = getterm ();
			if (*s != TABS || s2 != TABS)
				uerror ("слишком сложное выражение");
			rez <<= intval;
			break;
		case LRSHIFT:
			s2 = getterm ();
			if (*s != TABS || s2 != TABS)
				uerror ("слишком сложное выражение");
			rez >>= intval;
			break;
		case '*':
			s2 = getterm ();
			if (*s != TABS || s2 != TABS)
				uerror ("слишком сложное выражение");
			rez *= intval;
			break;
		case '/':
			s2 = getterm ();
			if (*s != TABS || s2 != TABS)
				uerror ("слишком сложное выражение");
			if (! intval)
				uerror ("деление на ноль");
			rez /= intval;
			break;
		case '%':
			s2 = getterm ();
			if (*s != TABS || s2 != TABS)
				uerror ("слишком сложное выражение");
			if (! intval)
				uerror ("деление (%%) на ноль");
			rez %= intval;
			break;
		default:
			ungetlex (clex, cval);
			intval = rez;
			return rez;
		}
	}
}

/*
 * Store memory word.
 */
void store_word (int addr, uint64_t val)
{
	ram [addr] = val;
	ram_dirty [addr] = 1;
	if (debug)
		fprintf (stderr, "слово %0o: %03o %04o %04o %04o\n", addr,
			(int) (val >> 36), (int) (val >> 24) & 07777,
			(int) (val >> 12) & 07777, (int) val & 07777);
}

void getstring ()
{
	int c, n, cval;
	uint64_t w;

	c = getlex (&cval, 0);
	if (c != '"')
		uerror ("нет параметра .текст");
	n = 0;
	w = 0;
	for (;;) {
		switch (c = unicode_getc (stdin)) {
		case EOF:
			uerror ("незакрытая текстовая строка");
		case '"':
			break;
		case '\\':
			switch (c = unicode_getc (stdin)) {
			case EOF:
				uerror ("незакрытая текстовая строка");
			case '\n':
				continue;
			case '0': case '1': case '2': case '3':
			case '4': case '5': case '6': case '7':
				cval = c & 07;
				c = unicode_getc (stdin);
				if (c>='0' && c<='7') {
					cval = cval<<3 | (c & 7);
					c = unicode_getc (stdin);
					if (c>='0' && c<='7') {
						cval = cval<<3 | (c & 7);
					} else unicode_ungetc (c);
				} else unicode_ungetc (c);
				c = cval;
				break;
			case 't':
				c = '\t';
				break;
			case 'b':
				c = '\b';
				break;
			case 'r':
				c = '\r';
				break;
			case 'n':
				c = '\n';
				break;
			case 'f':
				c = '\f';
				break;
			}
		default:
			w <<= 7;
			w |= unicode_to_gost (c);
			n++;
			if (n % 6 == 0) {
				store_word (count++, w);
				w = 0;
			}
			continue;
		}
		break;
	}
	if (n % 6 != 0) {
		do {
			w <<= 7;
			w |= GOST_SPACE;
			n++;
		} while (n % 6 != 0);
		store_word (count++, w);
	}
}

void parse ()
{
	int clex, cval, tval;

	for (;;) {
		clex = getlex (&cval, 1);
		switch (clex) {
		case LEOF:
			return;
		case LEOL:
			continue;
		case LCMD:
			makecmd (cval);
			break;
		case LNAME:
			clex = getlex (&tval, 0);
			switch (clex) {
			case ':':               /* name: */
				if (stab[cval].type != TUNDF) {
					uerror ("имя определено дважды");
					break;
				}
				stab[cval].value = count;
				stab[cval].type = TTEXT;
				if (! reached)
					reached = 1;
				continue;
			case LEQU:              /* имя .это знач */
				getexpr (&tval);
				if (tval == TUNDF)
					uerror ("неверное значение .это");
				if (stab[cval].type != TUNDF) {
					if (stab[cval].type != tval ||
					    stab[cval].value != intval)
						uerror ("имя определено дважды");
					break;
				}
				stab[cval].type = tval;
				stab[cval].value = intval;
				break;
			case LDATA:             /* имя .перем размер */
				getexpr (&tval);
				if (tval != TABS || intval < 0)
					uerror ("неверный размер .перем");
				if (stab[cval].type != TUNDF) {
					uerror ("имя уже определено");
					break;
				}
				stab[cval].type = TTEXT;
				stab[cval].value = count;
				count += intval;
				break;
			case LCONST:            /* имя .вещ знач */
				if (stab[cval].type != TUNDF)
					uerror ("имя уже определено");
				stab[cval].type = TTEXT;
				stab[cval].value = count;
				store_word (count++, getreal ());
				break;
			case LTEXT:		/* имя .текст "строка" */
				if (stab[cval].type != TUNDF)
					uerror ("имя уже определено");
				stab[cval].type = TTEXT;
				stab[cval].value = count;
				getstring ();
				break;
			default:
				uerror ("неверная команда");
			}
			break;
		case LNUM:
			if (nlabels >= MAXLABELS)
				uerror ("слишком много цифровых меток");
			labeltab[nlabels].num = intval;
			labeltab[nlabels].value = count;
			++nlabels;
			clex = getlex (&tval, 0);
			if (clex != ':')
				uerror ("неверная цифровая метка");
			if (! reached)
				reached = 1;
			continue;
		case LORG:
			getexpr (&tval);
			if (tval != TABS)
				uerror ("неверное значение .адрес");
			count = intval;
			break;
		default:
			uerror ("синтаксическая ошибка");
		}
		clex = getlex (&cval, 0);
		if (clex != LEOL) {
			if (clex == LEOF)
				return;
			uerror ("неверный аргумент команды");
		}
	}
}

int compare_stab (const void *pa, const void *pb)
{
	const struct stab *a = pa, *b = pb;

	if (a->value < b->value)
		return -1;
	return (a->value > b->value);
}

/*
 * Write the resulting hex image.
 */
void output ()
{
	int i, last_addr = -1;
	int flags, op, a1, a2, a3;
	uint64_t cmd;
	struct stab *s;

	utf8_puts ("; ", stdout);
	printf ("%s\n", infile1);
	for (i=0; i<DATSIZE; ++i) {
		if (! ram_dirty [i])
			continue;
		if (i != last_addr+1) {
			printf ("\n:%04o\n", i);
		}
		last_addr = i;
		cmd = ram[i];
		flags = cmd >> 42;
		op = cmd >> 36 & 077;
		a1 = cmd >> 24 & 07777;
		a2 = cmd >> 12 & 07777;
		a3 = cmd & 07777;
		printf ("%o %02o %04o %04o %04o\n", flags, op, a1, a2, a3);
	}

	/*
	 * Находим адрес начала.
	 */
	for (i=0; i<stabfree; ++i) {
		if (stab[i].len == 6 && ! wcscmp (stab[i].name, L"начало")) {
			printf ("\n@%04o\n", stab[i].value);
			break;
		}
	}

	/*
	 * Выдаем таблицу символов.
	 */
	printf ("\n; Таблица символов\n");
	qsort (stab, stabfree, sizeof (stab[0]), compare_stab);
	for (s=stab; s<stab+stabfree; ++s) {
		if (s->name[1] == '.')
			continue;
		switch (s->type) {
		default:     continue;
		case TUNDF:  i = 'U'; break;
		case TTEXT:  i = 'T'; break;
		case TABS:   i = 'A'; break;
		}
		printf ("; %04o  %c  ", s->value, i);
		wchar_puts (s->name, stdout);
		printf ("\n");
	}
	for (i=DATSIZE-1; i>0; --i)
		if (ram_dirty [i])
			break;
	printf ("; %04o  T  <конец>\n", i);
}

/*
 * Resolve pending references, adding
 * modules from libraries.
 */
void libraries ()
{
	struct stab *s;
	int n, undefined;
	char name [256];

	/* For every undefined reference,
	 * add the module from the library. */
	undefined = 0;
	for (s=stab; s<stab+stabfree; ++s) {
		if (s->type != TUNDF)
			continue;

		for (n=0; n<nlib; ++n) {
			sprintf (name, "%s/%ls.lib", libtab[n].name, s->name);
			if (freopen (name, "r", stdin)) {
				infile = name;
				line = 1;
				parse ();
				infile = 0;
				++filenum;
				break;
			}
		}
		if (n >= nlib) {
			fprintf (stderr, "as: неопределено: ");
			wchar_puts (s->name, stderr);
			fprintf (stderr, "\n");
			++undefined;
		}
	}
	if (undefined > 0) {
		fprintf (stderr, "as: останов\n");
		unlink (outfile);
		exit (1);
	}
}

/*
 * Find the relative label address,
 * by the reference address and the label number.
 * Backward references have negative label numbers.
 */
int findlabel (int addr, int sym)
{
	struct labeltab *p;

	if (sym < 0) {
		/* Backward reference. */
		for (p=labeltab+nlabels-1; p>=labeltab; --p)
			if (p->value <= addr && p->num == -sym)
				return p->value;
		uerror ("неопределенная метка %dн по адресу %d", -sym, addr);
	} else {
		/* Forward reference. */
		for (p=labeltab; p<labeltab+nlabels; ++p)
			if (p->value > addr && p->num == sym)
				return p->value;
		uerror ("неопределенная метка %dп по адресу %d", sym, addr);
	}
	return 0;
}

/*
 * Allocate constants and relocate references.
 */
void relocate ()
{
	int n, v;
	struct reltab *r;
	int tsize;

	tsize = 0;
	for (n=0; n<DATSIZE; ++n)
		if (ram_dirty [n])
			++tsize;

	/* Relocate pending references. */
	for (r=reltab; r<reltab+nrel; ++r) {
		if (r->flags & RLAB)
			v = findlabel (r->addr, r->sym);
		else
			v = stab[r->sym].value;

		switch (r->flags & ~RLAB) {
		case RA1:
			v += ram [r->addr] >> 24 & 07777;
			ram [r->addr] &= ~0777700000000LL;
			ram [r->addr] |= (uint64_t) (v & 07777) << 24;
			break;
		case RA2:
			v += ram [r->addr] >> 12 & 07777;
			ram [r->addr] &= ~077770000LL;
			ram [r->addr] |= (uint64_t) (v & 07777) << 12;
			break;
		case RA3:
			v += ram [r->addr] & 07777;
			ram [r->addr] &= ~07777LL;
			ram [r->addr] |= v & 07777;
			break;
		}
	}
	fprintf (stderr, "Занято %d слов памяти\n", tsize);
	if (count > DATSIZE)
		uerror ("Недостаточно памяти для программы: %d слов", count);
	fprintf (stderr, "Свободно %d слов\n", DATSIZE - count);
}

void addreloc (int addr, int sym, int flags)
{
	if (nrel >= MAXREL)
		uerror ("слишком много перемещений");
	reltab[nrel].addr = addr;
	reltab[nrel].sym = sym;
	reltab[nrel].flags = flags;
	++nrel;
	if (debug) {
		fprintf (stderr, "reloc %d", addr);
		if (sym)
			fprintf (stderr, " %d", sym);
		if (flags & RLAB)
			fprintf (stderr, " RLAB");
		fprintf (stderr, "\n");
	}
}

/*
 * Compile the command.
 */
void makecmd (int code)
{
	int type, clex, cval, a1, a2, a3, ra;

	/* Адрес 1. */
	extflag = 0;
	getexpr (&type);
	if (type == TUNDF)
		addreloc (count, extref, extflag | RA1);
	a1 = intval & 07777;
	a2 = a3 = 0;
	ra = 0;
	if (extflag & RRA)
		ra |= 4;

	clex = getlex (&cval, 0);
	if (clex != ',') {
		ungetlex (clex, cval);
	} else {
		/* Адрес 2. */
		extflag = 0;
		getexpr (&type);
		if (type == TUNDF)
			addreloc (count, extref, extflag | RA2);
		a2 = intval & 07777;
		if (extflag & RRA)
			ra |= 2;

		clex = getlex (&cval, 0);
		if (clex != ',') {
			ungetlex (clex, cval);
		} else {
			/* Адрес 3. */
			extflag = 0;
			getexpr (&type);
			if (type == TUNDF)
				addreloc (count, extref, extflag | RA3);
			a3 = intval & 07777;
			if (extflag & RRA)
				ra |= 1;
		}
	}
	store_word (count++, (uint64_t) code << 36 | (uint64_t) ra << 42 |
		(uint64_t) a1 << 24 | a2 << 12 | a3);
}
