/*
 * m20_sys.c: M-20 simulator interface
 *
 * Copyright (c) 2009, Serge Vakulenko
 *
 * This file implements three essential functions:
 *
 * sim_load()   - loading and dumping memory and CPU state
 *		  in a way, specific for M20 architecture
 * fprint_sym() - print a machune instruction using
 *  		  opcode mnemonic or in a digital format
 * parse_sym()	- scan a string and build an instruction
 *		  word from it
 */
#include "m20_defs.h"
#include <math.h>

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
t_value ieee_to_m20 (double d)
{
	t_value word;
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
	word |= ((t_value) (exponent + 64)) << 36;
	word |= (t_value) sign << 43;	/* Знак. */
	return word;
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
t_stat m20_read_line (FILE *input, int *type, t_value *val)
{
	char buf [512], *p;
	int i;
again:
	if (! fgets (buf, sizeof (buf), input)) {
		*type = 0;
		return SCPE_OK;
	}
	p = skip_spaces (buf);
	if (*p == '\n' || *p == ';')
		goto again;
	if (*p == ':') {
		/* Адрес размещения данных. */
		*type = ':';
		*val = strtol (p+1, 0, 8);
		return SCPE_OK;
	}
	if (*p == '@') {
		/* Стартовый адрес. */
		*type = '@';
		*val = strtol (p+1, 0, 8);
		return SCPE_OK;
	}
	if (*p == '=') {
		/* Вещественное число. */
		*type = '=';
		*val = ieee_to_m20 (strtod (p+1, 0));
		return SCPE_OK;
	}
	if (*p < '0' || *p > '7') {
		/* неверная строка входного файла */
		return SCPE_FMT;
	}

	/* Слово. */
	*type = '=';
	*val = *p - '0';
	for (i=0; i<14; ++i) {
		p = skip_spaces (p + 1);
		if (*p < '0' || *p > '7') {
			/* слишком короткое слово */
			return SCPE_FMT;
		}
		*val = *val << 3 | (*p - '0');
	}
	return SCPE_OK;
}

/*
 * Load memory from file.
 */
t_stat m20_load (FILE *input)
{
	int addr, type;
	t_value word;
	t_stat err;

	addr = 1;
	RVK = 1;
	for (;;) {
		err = m20_read_line (input, &type, &word);
		if (err)
			return err;
		switch (type) {
		case 0:			/* EOF */
			return SCPE_OK;
		case ':':		/* address */
			addr = word;
			break;
		case '=':		/* word */
			M [addr] = word;
			/* ram_dirty [addr] = 1; */
			++addr;
			break;
		case '@':		/* start address */
			RVK = word;
			break;
		}
		if (addr > MEMSIZE)
			return SCPE_FMT;
	}
	return SCPE_OK;
}

/*
 * Dump memory to file.
 */
t_stat m20_dump (FILE *of, char *fnam)
{
	int i, last_addr = -1;
	t_value cmd;

	fprintf (of, "; %s\n", fnam);
	for (i=1; i<MEMSIZE; ++i) {
		if (M [i] == 0)
			continue;
		if (i != last_addr+1) {
			fprintf (of, "\n:%04o\n", i);
		}
		last_addr = i;
		cmd = M [i];
		fprintf (of, "%o %02o %04o %04o %04o\n",
			(int) (cmd >> 42) & 7,
			(int) (cmd >> 36) & 077,
			(int) (cmd >> 24) & 07777,
			(int) (cmd >> 12) & 07777,
			(int) cmd & 07777);
	}
	return SCPE_OK;
}

/*
 * Loader/dumper
 */
t_stat sim_load (FILE *fi, char *cptr, char *fnam, int dump_flag)
{
	if (dump_flag)
		return m20_dump (fi, fnam);

	return m20_load (fi);
}

const char *m20_opname [64] = {
	"зп",	"сл",	"вч",	"вчм",	"дел",	"умн",	"слпа",	"слц",
	"вп",	"цме",	"цм",	"слк",	"сдма",	"нтж",	"пв",	"17",
	"счп",	"слбо",	"вчбо",	"вчмбо","делбо","умнбо","слп",	"вчц",
	"впбк",	"цбре",	"цбр",	"вчк",	"сдм",	"нтжс",	"пе",	"37",
	"40",	"слбн",	"вчбн",	"вчмбн","кор",	"умнбн","вчпа",	"счмр",
	"ма",	"цмо",	"раа",	"слко",	"сда",	"и",	"пб",	"57",
	"60",	"слбно","вчбно","вчмбно","корбо","умнбно","вчп","сдц",
	"мб",	"цбро",	"ра",	"вчко",	"сд",	"или",	"по",	"стоп",
};

int m20_instr_to_opcode (char *instr)
{
	int i;

	for (i=0; i<64; ++i)
		if (strcmp (m20_opname[i], instr) == 0)
			return i;
	return -1;
}

/*
 * Печать 12-битной адресной части машинной инструкции.
 */
void m20_fprint_addr (FILE *of, int a, int flag)
{
	if (flag)
		putc ('@', of);

	if (flag && a >= 07700) {
		fprintf (of, "-%o", (a ^ 07777) + 1);
	} else if (a) {
		if (flag)
			putc ('+', of);
		fprintf (of, "%o", a);
	}
}

/*
 * Печать машинной инструкции.
 */
void m20_fprint_cmd (FILE *of, t_value cmd)
{
	const char *m;
	int flags, op, a1, a2, a3;

	flags = cmd >> 42 & 7;
	op = cmd >> 36 & 077;
	a1 = cmd >> 24 & 07777;
	a2 = cmd >> 12 & 07777;
	a3 = cmd & 07777;
	m = m20_opname [op];

	if (! flags && ! a1 && ! a2 && ! a3) {
		/* Команда без аргументов. */
		printf ("%s", m);
		return;
	}
	printf ("%s ", m);
	m20_fprint_addr (of, a1, flags & 4);
	if (! (flags & 3) && ! a2 && ! a3) {
		/* Нет аргументов 2 и 3. */
		return;
	}

	printf (", ");
	m20_fprint_addr (of, a2, flags & 2);
	if (! (flags & 1) && ! a3) {
		/* Нет аргумента 3. */
		return;
	}

	printf (", ");
	m20_fprint_addr (of, a3, flags & 1);
}

/*
 * Symbolic decode
 *
 * Inputs:
 *	*of	= output stream
 *	addr	= current PC
 *	*val	= pointer to data
 *	*uptr	= pointer to unit
 *	sw	= switches
 * Outputs:
 *	return	= status code
 */
t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
	UNIT *uptr, int32 sw)
{
	t_value cmd;

	if (uptr && (uptr != &cpu_unit))		/* must be CPU */
		return SCPE_ARG;

	cmd = val[0];
	if (sw & SWMASK ('M')) {			/* symbolic decode? */
		m20_fprint_cmd (of, cmd);
		return SCPE_OK;
	}
	fprintf (of, "%o %02o %04o %04o %04o",
		(int) (cmd >> 42) & 7,
		(int) (cmd >> 36) & 077,
		(int) (cmd >> 24) & 07777,
		(int) (cmd >> 12) & 07777,
		(int) cmd & 07777);
	return SCPE_OK;
}

char *m20_parse_offset (char *cptr, int *offset)
{
	char *tptr, gbuf[CBUFSIZE];

	cptr = get_glyph (cptr, gbuf, 0);	/* get address */
	*offset = strtotv (gbuf, &tptr, 8);
	if ((tptr == gbuf) || (*tptr != 0) || (*offset > 07777))
		return 0;
	return cptr;
}

char *m20_parse_address (char *cptr, int *address, int *relative)
{
	cptr = skip_spaces (cptr);			/* absorb spaces */
	if (*cptr >= '0' && *cptr <= '7')
		return m20_parse_offset (cptr, address); /* get address */

	if (*cptr != '@')
		return 0;
	*relative |= 1;
	cptr = skip_spaces (cptr+1);			/* next char */
	if (*cptr == '+') {
		cptr = skip_spaces (cptr+1);		/* next char */
		cptr = m20_parse_offset (cptr, address);
		if (! cptr)
			return 0;
	} else if (*cptr == '-') {
		cptr = skip_spaces (cptr+1);		/* next char */
		cptr = m20_parse_offset (cptr, address);
		if (! cptr)
			return 0;
		*address = (- *address) & 07777;
	} else
		return 0;
	return cptr;
}

/*
 * Instruction parse
 */
t_stat parse_instruction (char *cptr, t_value *val, int32 sw)
{
	int opcode, ra, a1, a2, a3;
	char gbuf[CBUFSIZE];

	cptr = get_glyph (cptr, gbuf, 0);		/* get opcode */
	opcode = m20_instr_to_opcode (gbuf);
	if (opcode < 0)
		return SCPE_ARG;
	ra = 0;
	cptr = m20_parse_address (cptr, &a1, &ra);	/* get address 1 */
	if (! cptr)
		return SCPE_ARG;
	ra <<= 1;
	cptr = m20_parse_address (cptr, &a2, &ra);	/* get address 1 */
	if (! cptr)
		return SCPE_ARG;
	ra <<= 1;
	cptr = m20_parse_address (cptr, &a3, &ra);	/* get address 1 */
	if (! cptr)
		return SCPE_ARG;

	val[0] = (t_value) opcode << 36 | (t_value) ra << 42 |
		(t_value) a1 << 24 | a2 << 12 | a3;
	if (*cptr != 0)
		return SCPE_2MARG;
	return SCPE_OK;
}

/*
 * Symbolic input
 *
 * Inputs:
 *	*cptr   = pointer to input string
 *	addr    = current PC
 *	*uptr   = pointer to unit
 *	*val    = pointer to output values
 *	sw      = switches
 * Outputs:
 *	status  = error status
 */
t_stat parse_sym (char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
	int32 i;

	if (uptr && (uptr != &cpu_unit))		/* must be CPU */
		return SCPE_ARG;
	cptr = skip_spaces (cptr);			/* absorb spaces */
	if (! parse_instruction (cptr, val, sw))	/* symbolic parse? */
		return SCPE_OK;

	val[0] = 0;
	for (i=0; i<14; i++) {
		if (*cptr == 0)
			return SCPE_OK;
		if (*cptr < '0' || *cptr > '7')
			return SCPE_ARG;
		val[0] = (val[0] << 3) | (*cptr - '0');
		cptr = skip_spaces (cptr+1);		/* next char */
	}
	if (*cptr != 0)
		return SCPE_ARG;
	return SCPE_OK;
}
