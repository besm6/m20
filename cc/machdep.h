/*
 * Компилятор Си для ЭВМ М-20.
 * Copyright (GPL) 2008 Сергей Вакуленко <serge.vakulenko@gmail.com>
 */
#define ASM_TRUE        " зп _истина,,1;"
#define ASM_FALSE       " зп 0,,1;"
#define ASM_RETURN      " пб ,1н;"
#define ASM_GOTO        " пб %n;"
#define ASM_CALL        " пв .+1,%n,%n-1;"
#define ASM_GOTOLAB     " пб М%d;"
#define ASM_COND        " нтж 1;"
#define ASM_DEFLAB      "М%d:;"
