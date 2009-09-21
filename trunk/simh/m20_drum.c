/*
 * m20_drum.c: M-20 magnetic drum device
 *
 * Copyright (c) 2009, Serge Vakulenko
 *
 * All drum i/o is performed immediately.
 * There is no interrupt system in M20.
 * No real drum timing is implented.
 */
#include "m20_defs.h"

/*
 * Параметры обмена с внешним устройством.
 */
int ext_op;			/* УЧ - условное число */
int ext_disk_addr;		/* А_МЗУ - начальный адрес на барабане/ленте */
int ext_ram_start;		/* α_МОЗУ - начальный адрес памяти */
int ext_ram_finish;		/* ω_МОЗУ - конечный адрес памяти */

/*
 * DRUM data structures
 *
 * drum_dev	DRUM device descriptor
 * drum_unit	DRUM unit descriptor
 * drum_reg	DRUM register list
 */
UNIT drum_unit = {
	UDATA (NULL, UNIT_FIX+UNIT_ATTABLE, DRUM_SIZE)
};

REG drum_reg[] = {
	{ "УЧ",     &ext_op,         8, 12, 0, 1 },
	{ "А_МЗУ",  &ext_disk_addr,  8, 12, 0, 1 },
	{ "α_МОЗУ", &ext_ram_start,  8, 12, 0, 1 },
	{ "ω_МОЗУ", &ext_ram_finish, 8, 12, 0, 1 },
	{ 0 }
};

MTAB drum_mod[] = {
	{ 0 }
};

t_stat drum_reset (DEVICE *dptr);

DEVICE drum_dev = {
	"DRUM", &drum_unit, drum_reg, drum_mod,
	1, 8, 12, 1, 8, 45,
	NULL, NULL, &drum_reset,
	NULL, NULL, NULL,
	NULL, DEV_DISABLE
};

/*
 * Reset routine
 */
t_stat drum_reset (DEVICE *dptr)
{
	ext_op = 07777;
	ext_disk_addr = 0;
	ext_ram_start = 0;
	ext_ram_finish = 0;
	sim_cancel (&drum_unit);
	return SCPE_OK;
}

/*
 * Подсчет контрольной суммы, как в команде СЛЦ.
 */
t_value compute_checksum (t_value x, t_value y)
{
	t_value sum;

	sum = (x & ~MANTISSA) + (y & ~MANTISSA);
	if (sum & BIT46)
		sum += BIT37;
	y = (x & MANTISSA) + (y & MANTISSA);
	if (y & BIT37)
		y += 1;
	return (sum & ~MANTISSA) | (y & MANTISSA);
}

/*
 * Запись на барабан.
 * Если параметр sum ненулевой, посчитываем и кладём туда контрольную
 * сумму массива. Также запмсываем сумму в слово last+1 на барабане.
 */
t_stat drum_write (int addr, int first, int last, t_value *sum)
{
	int nwords, i;

	nwords = last - first + 1;
	if (nwords <= 0 || nwords+addr > DRUM_SIZE) {
		/* Неверная длина записи на МБ */
		return STOP_BADWLEN;
	}
	fseek (drum_unit.fileref, addr*8, SEEK_SET);
	fxwrite (&M[first], 8, nwords, drum_unit.fileref);
	if (ferror (drum_unit.fileref))
		return SCPE_IOERR;
	if (sum) {
		/* Подсчитываем и записываем контрольную сумму. */
		*sum = 0;
		for (i=first; i<=last; ++i)
			*sum = compute_checksum (*sum, M[i]);
		fxwrite (sum, 8, 1, drum_unit.fileref);
	}
	return 0;
}

/*
 * Чтение с барабана.
 */
t_stat drum_read (int addr, int first, int last, t_value *sum)
{
	int nwords, i;
	t_value old_sum;

	nwords = last - first + 1;
	if (nwords <= 0 || nwords+addr > DRUM_SIZE) {
		/* Неверная длина чтения МБ */
		return STOP_BADRLEN;
	}
	fseek (drum_unit.fileref, addr*8, SEEK_SET);
	i = fxread (&M[first], 8, nwords, drum_unit.fileref);
	if (ferror (drum_unit.fileref))
		return SCPE_IOERR;
	if (i != nwords) {
		/* Чтение неинициализированного барабана */
		return STOP_DRUMINVDATA;
	}
	if (sum) {
		/* Считываем и проверяем контрольную сумму. */
		fxread (&old_sum, 8, 1, drum_unit.fileref);
		*sum = 0;
		for (i=first; i<=last; ++i)
			*sum = compute_checksum (*sum, M[i]);
		if (old_sum != *sum)
			return STOP_READERR;
	}
	return 0;
}

/*
 * Выполнение обращения к барабану.
 * Все параметры находятся в регистрах УЧ, А_МЗУ, α_МОЗУ, ω_МОЗУ.
 */
t_stat drum (t_value *sum)
{
	if (drum_dev.flags & DEV_DIS) {
		/* Device not attached. */
		return SCPE_UNATT;
	}
	if (ext_op & EXT_WRITE) {
		return drum_write ((ext_op & EXT_UNIT) << 12 | ext_disk_addr,
			ext_ram_start, ext_ram_finish,
			(ext_op & EXT_DIS_CHECK) ? 0 : sum);
	} else {
		return drum_read ((ext_op & EXT_UNIT) << 12 | ext_disk_addr,
		    ext_ram_start, ext_ram_finish,
		    (ext_op & EXT_DIS_CHECK) ? 0 : sum);
	}
}
