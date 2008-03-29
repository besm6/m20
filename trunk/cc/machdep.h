/*
 * Компилятор Си для ЭВМ М-20.
 * Copyright (GPL) 2008 Сергей Вакуленко <serge@vak.ru>
 */
#define ASM_TRUE        " cta 1;"
#define ASM_FALSE       " cta 0;"
#define ASM_RETURN      " ret;"
#define ASM_GOTO        " goto %n;"
#define ASM_CALL        " call %n;"
#define ASM_GOTOLAB     " goto L%d;"
#define ASM_COND        " a|c 0;"
#define ASM_DEFLAB      "L%d:;"
