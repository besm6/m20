/*
 * m20_defs.h: M-20 simulator definitions
 *
 * Copyright (c) 2009, Serge Vakulenko
 */
#ifndef _M20_DEFS_H_
#define _M20_DEFS_H_    0

#include "sim_defs.h"				/* simulator defns */

/*
 * Memory
 */
#define MEMSIZE         4096			/* memory size */

/*
 * Simulator stop codes
 */
enum {
	STOP_STOP = 1,				/* STOP */
	STOP_IBKPT,				/* breakpoint */
	STOP_RUNOUT,				/* run out end of memory limits */
	STOP_BADCMD,				/* invalid instruction */
	STOP_ADDOVF,				/* addition overflow */
	STOP_EXPOVF,				/* exponent overflow */
	STOP_MULOVF,				/* multiplication overflow */
};

/*
 * Разряды машинного слова.
 */
#define BIT46		01000000000000000LL	/* 46-й бит */
#define TAG		00400000000000000LL	/* 45-й бит-признак */
#define SIGN		00200000000000000LL	/* 44-й бит-знак */
#define BIT37		00001000000000000LL	/* 37-й бит */
#define BIT19		00000000001000000LL	/* 19-й бит */
#define WORD		00777777777777777LL	/* биты 45..1 */
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

#endif
