/*
 * Компилятор Си для ЭВМ М-20.
 * Copyright (GPL) 2008 Сергей Вакуленко <serge.vakulenko@gmail.com>
 */
#include <stdio.h>
#include "global.h"

void header (void)
{
}

/*
 * Compute the size of the type in bytes.
 */
int tsize (int type)
{
	if (type & TPTR)
		return 1;               /* near pointers are one-byte */
	switch (type & TMASK) {
	default:     return 0;
	case TVOID:  return 0;          /* void - nothing */
	case TCHAR:  return 1;          /* char - 1 byte */
	case TINT:   return 6;          /* int - 6 bytes */
	}
}

/*
 * Output the assembler name of the symbol.
 * For local symbols the name of the function is appended.
 */
void printname (node_t *n)
{
	if (n->rval) {
		sym_t *s = stab + n->rval;
		if (s->local)
			printf ("%s", func->name);
		printf ("_%s", s->name);
		if (n->lval)
			printf ("+%ld", n->lval);
	} else
		printf ("%ld", n->lval);
}

/*
 * Output the declaration of the symbol.
 */
void declvar (node_t *n)
{
	sym_t *s = stab + n->rval;

	if (s->addr)
		output ("%n .это %D #`", n, s->addr - 1);
	else {
		int size = tsize (s->type);
		if (s->size)
			size *= s->size;
		output ("%n .перем %d #`", n, (size + 5) / 6);
	}
	nprint (n, 0);
}

/*
 * Output the declaration of the function.
 */
void declfun (node_t *n)
{
	output ("1: .перем 1 #`адрес возврата;");
	output ("%n:   #`", n);
	nprint (n, 0);
}

int is_bitmask (long mask)
{
	switch (mask) {
	case 1: case 2: case 4: case 8:
	case 16: case 32: case 64: case 128:
		return 1;
	}
	return 0;
}

int bitmask_to_bitnum (int mask)
{
	switch (mask) {
	case 1:   return 0;
	case 2:	  return 1;
	case 4:   return 2;
	case 8:   return 3;
	case 16:  return 4;
	case 32:  return 5;
	case 64:  return 6;
	case 128: return 7;
	}
	return 0;
}

/*
 * Detect if the operator will be compiled in 1-command expression.
 * Needed for optimizing conditional jumps.
 */
int operword (node_t *n)
{
	if (n->op == OP_ASSIGN && n->left->op == OP_NAME &&
	    n->right->op == OP_NAME) {
		sym_t *l, *r;

		l = stab + n->left->rval;
		r = stab + n->right->rval;
		if (tsize (l->type) == 1 && tsize (r->type) == 1 &&
		    (l->addr || r->addr) &&
		    ((l->addr && l->addr < 0x20) || r->addr < 0x20))
			return 1;
	}
	if (n->op == OP_ASSIGN && n->left->op == OP_NAME &&
	    n->right->op == OP_CONST && ! n->right->lval && ! n->right->rval &&
	    tsize (stab[n->left->rval].type) == 1)
		return 1;
	if (n->op == OP_ASSIGN && n->left->op == OP_NAME &&
	    tsize (stab[n->left->rval].type) == 1 && ! n->right->rval &&
	    n->right->op == OP_CONST && n->right->lval == 0xff)
		return 1;
	if (n->op == OP_ASSIGN && n->left->op == OP_DOT)
		return 1;
	if ((n->op == OP_ORASG || n->op == OP_XORASG || n->op == OP_ANDASG) &&
	    n->left->op == OP_NAME && n->right->op == OP_CONST &&
	    tsize (stab[n->left->rval].type) == 1 &&
	    !n->right->rval && is_bitmask (n->right->lval))
		return 1;
	if (n->op == OP_XORASG && n->left->op == OP_DOT)
		return 1;
	if (n->op == OP_RETVAL && n->left->op == OP_CONST &&
	    tsize (n->left->type) == 1)
		return 1;

	if (n->op == OP_GOTO || n->op == OP_RETURN ||
	    n->op == OP_BREAK || n->op == OP_CONTINUE)
		return 1;
	if (n->op == OP_CALL && ! n->right)
		return 1;
	return 0;
}

/*
 * Try the machine-dependent compilation of the conditional expression.
 * Assume the one-command operator follows.
 * Make the output and return true if successful,
 * otherwise output nothing and return false.
 */
int machif (node_t *n, int trueflag)
{
	/* ! expr */
	while (n->op == OP_NOT) {
		trueflag = !trueflag;
		n = n->left;
	}
	/* var.bit */
	if (n->op == OP_DOT) {
		output (" b%c? %n,%D;", trueflag ? 's' : 'z', n->left,
			n->right->lval & 7);
		return 1;
	}
	/* var & bitmask */
	if (n->op == OP_AND && n->left->op == OP_NAME &&
	    n->right->op == OP_CONST && ! n->right->rval &&
	    is_bitmask (n->right->lval)) {
		output (" b%c? %n,%D;", trueflag ? 's' : 'z', n->left,
			bitmask_to_bitnum (n->right->lval));
		return 1;
	}
	/* var */
	if (n->op == OP_NAME && tsize (n->type) == 1 && trueflag) {
		output (" x? %n;", n);
		return 1;
	}
	/* ! var */
	if (n->op == OP_NAME && tsize (n->type) == 1 && ! trueflag) {
		output (" az; x<=a? %n;", n);
		return 1;
	}
	/* var != 0 */
	if (n->op == OP_NEQ && n->left->op == OP_NAME &&
	    tsize (n->left->type) == 1 && trueflag && n->right->op == OP_CONST &&
	    ! n->right->rval && ! n->right->lval) {
		output (" x? %n;", n->left);
		return 1;
	}
	/* var == 0 */
	if (n->op == OP_EQU && n->left->op == OP_NAME &&
	    tsize (n->left->type) == 1 && n->right->op == OP_CONST &&
	    ! n->right->rval && ! n->right->lval) {
		if (trueflag) output (" az; x<=a? %n;", n->left);
		else	      output (" x? %n;", n->left);
		return 1;
	}
	/* expr ==/!= var/const */
	if ((n->op == OP_EQU || n->op == OP_NEQ) &&
	    (n->right->op == OP_NAME || n->right->op == OP_CONST) &&
	    tsize (n->left->type) == 1 && tsize (n->right->type) == 1) {
		if (n->op == OP_NEQ)
			trueflag = !trueflag;
		expression (n->left, TARG_ACC);
		if (n->right->op == OP_NAME) output (" a^x %n;", n->right);
		else			     output (" a^c %n;", n->right);
		if (trueflag) output (" z?;");
		else          output (" a?;");
		return 1;
	}
	/* var/const ==/!= expr */
	if ((n->op == OP_EQU || n->op == OP_NEQ) &&
	    (n->left->op == OP_NAME || n->left->op == OP_CONST) &&
	    tsize (n->left->type) == 1 && tsize (n->right->type) == 1) {
		if (n->op == OP_NEQ)
			trueflag = !trueflag;
		expression (n->right, TARG_ACC);
		if (n->left->op == OP_NAME) output (" a^x %n;", n->right);
		else			    output (" a^c %n;", n->right);
		if (trueflag) output (" z?;");
		else          output (" a?;");
		return 1;
	}
	/* expr <=/> var/const */
	if ((n->op == OP_LE || n->op == OP_GT) &&
	    (n->right->op == OP_NAME || n->right->op == OP_CONST) &&
	    tsize (n->left->type) == 1 && tsize (n->right->type) == 1) {
		if (n->op == OP_GT)
			trueflag = !trueflag;
		expression (n->left, TARG_ACC);
		if (n->right->op == OP_NAME) output (" a-x %n;", n->right);
		else			     output (" c-a %n;", n->right);
		if (trueflag) output (" c?;");
		else          output (" b?;");
		return 1;
	}
	/* var/const >=/< expr */
	if ((n->op == OP_GE || n->op == OP_LT) &&
	    (n->left->op == OP_NAME || n->left->op == OP_CONST) &&
	    tsize (n->left->type) == 1 && tsize (n->right->type) == 1) {
		if (n->op == OP_LT)
			trueflag = !trueflag;
		expression (n->right, TARG_ACC);
		if (n->left->op == OP_NAME) output (" a-x %n;", n->left);
		else			    output (" c-a %n;", n->left);
		if (trueflag) output (" c?;");
		else          output (" b?;");
		return 1;
	}
	/* expr & var/const */
	if (n->op == OP_AND && tsize (n->right->type) == 1 &&
	    (n->right->op == OP_NAME || n->right->op == OP_CONST)) {
		expression (n->left, TARG_ACC);
		output (" a&%c %n; %c?;", n->right->op == OP_NAME ? 'x' : 'c',
			n->right, trueflag ? 'a' : 'z');
		return 1;
	}
	/* var/const & expr */
	if (n->op == OP_AND && tsize (n->left->type) == 1 &&
	    (n->left->op == OP_NAME || n->left->op == OP_CONST)) {
		expression (n->right, TARG_ACC);
		output (" a&%c %n; %c?;", n->left->op == OP_NAME ? 'x' : 'c',
			n->left, trueflag ? 'a' : 'z');
		return 1;
	}
	/* (expr & var/const) ==/!= var/const */
	if ((n->op == OP_EQU || n->op == OP_NEQ) && n->left->op == OP_AND &&
	    (n->right->op == OP_NAME || n->right->op == OP_CONST) &&
	    (n->left->right->op == OP_NAME || n->left->right->op == OP_CONST) &&
	    tsize (n->left->right->type) == 1 && tsize (n->right->type) == 1) {
		if (n->op == OP_NEQ)
			trueflag = !trueflag;
		expression (n->left->left, TARG_ACC);
		output (" a&%c %n;", n->left->right->op == OP_NAME ? 'x' : 'c',
			n->left->right);
		output (" a^%c %n;", n->right->op == OP_NAME ? 'x' : 'c',
			n->right);
		if (trueflag) output (" z?;");
		else          output (" a?;");
		return 1;
	}

	/* INT var/const >/<= INT var/const */
	if ((n->op == OP_GT || n->op == OP_LE ||
	     n->op == OP_LT || n->op == OP_GE) &&
	    (n->left->op == OP_NAME || n->left->op == OP_CONST) &&
	    (n->right->op == OP_NAME || n->right->op == OP_CONST)) {
		if (n->op == OP_LE) {
			trueflag = !trueflag;
			n->op = OP_GT;
		}
		if (n->op == OP_GE) {
			trueflag = !trueflag;
			n->op = OP_LT;
		}
		machop (n, TARG_ACC);
		if (trueflag) output (" a?;");
		else          output (" a|c 0; z?;");
		return 1;
	}

	/* This mist be the last entry here! */
	/* expr */
	if (n->op != OP_OROR && n->op != OP_ANDAND && tsize (n->type) == 1) {
		if (trueflag) {
			expression (n, TARG_ACC);
			output (" a?;");
		} else {
			expression (n, TARG_COND);
			output (" z?;");
		}
		return 1;
	}
	return 0;
}

/*
 * Try to make the machine-dependent compilation of the operator.
 */
node_t *machoper (node_t *n)
{
	/*
	 * Register to memory and memory to register movs.
	 */
	if (n->op == OP_ASSIGN && n->left->op == OP_NAME &&
	    n->right->op == OP_NAME) {
		sym_t *l, *r;

		l = stab + n->left->rval;
		r = stab + n->right->rval;
		if (tsize (l->type) == 1 && tsize (r->type) == 1 &&
		    (l->addr || r->addr)) {
			if (l->addr && l->addr < 0x20) {
				output (" xtr %n,%n;", n->right, n->left);
				return 0;
			} else if (r->addr < 0x20) {
				output (" rtx %n,%n;", n->right, n->left);
				return 0;
			}
		}
	}

	/*
	 * Zero assignment.
	 */
	if (n->op == OP_ASSIGN && n->left->op == OP_NAME &&
	    n->right->op == OP_CONST && ! n->right->lval && ! n->right->rval) {
		switch (tsize (stab[n->left->rval].type)) {
		case 4:  output (" xz %n+3; xz %n+2;", n->left, n->left);
		case 2:  output (" xz %n+1;", n->left);
		default: output (" xz %n;", n->left);
		}
		return 0;
	}

	/*
	 * 0xFF assignment.
	 */
	if (n->op == OP_ASSIGN && n->left->op == OP_NAME &&
	    tsize (stab[n->left->rval].type) == 1 && ! n->right->rval &&
	    n->right->op == OP_CONST && n->right->lval == 0xff) {
		output (" xs %n;", n->left);
		return 0;
	}

	/*
	 * Left shift assignment of 1.
	 */
	if (n->op == OP_LSASG && n->left->op == OP_NAME &&
	    n->right->op == OP_CONST && !n->right->rval && n->right->lval==1) {
		output (" bz 4,0;");
		switch (tsize (stab[n->left->rval].type)) {
		case 4:  output (" xc<<x %n; xc<<x %n+1; xc<<x %n+2; xc<<x %n+3;",
				n->left, n->left, n->left, n->left); break;
		case 2:  output (" xc<<x %n; xc<<x %n+1;", n->left, n->left); break;
		default: output (" xc<<x %n;", n->left); break;
		}
		return 0;
	}

	/*
	 * Right shift assignment of 1.
	 */
	if (n->op == OP_RSASG && n->left->op == OP_NAME &&
	    n->right->op == OP_CONST && !n->right->rval && n->right->lval==1) {
		output (" bz 4,0;");
		switch (tsize (stab[n->left->rval].type)) {
		case 4:  output (" xc>>x %n+3; xc>>x %n+2;", n->left, n->left);
		case 2:  output (" xc>>x %n+1; xc>>x %n;", n->left, n->left); break;
		default: output (" xc>>x %n;", n->left); break;
		}
		return 0;
	}

	/*
	 * Short/long constant assignment.
	 */
	if (n->op == OP_ASSIGN && n->left->op == OP_NAME &&
	    n->right->op == OP_CONST && ! n->right->rval) {
		switch (tsize (stab[n->left->rval].type)) {
		case 2:
			output (" cta %d; atx %n+1;", n->right->lval >> 8 & 0xff, n->left);
			output (" cta %d; atx %n;", n->right->lval & 0xff, n->left);
			return 0;
		case 4:
			output (" cta %d; atx %n+3;", n->right->lval >> 24 & 0xff, n->left);
			output (" cta %d; atx %n+2;", n->right->lval >> 16 & 0xff, n->left);
			output (" cta %d; atx %n+1;", n->right->lval >> 8 & 0xff, n->left);
			output (" cta %d; atx %n;", n->right->lval & 0xff, n->left);
			return 0;
		}
	}

	/*
	 * Bit assignment.
	 */
	if (n->op == OP_ASSIGN && n->left->op == OP_DOT) {
		if (n->right->op != OP_CONST || n->right->lval > 1) {
			error ("illegal bit value");
			return 0;
		}
		output (" b%c %n,%D;", n->right->lval ? 's' : 'z',
			n->left->left, n->left->right->lval & 7);
		return 0;
	}
	if ((n->op == OP_ORASG || n->op == OP_XORASG || n->op == OP_ANDASG) &&
	    n->left->op == OP_NAME && n->right->op == OP_CONST &&
	    tsize (stab[n->left->rval].type) == 1 &&
	    !n->right->rval && is_bitmask (n->right->lval)) {
		output (" b%c %n,%D;", n->op==OP_ORASG ? 's' :
			n->op==OP_XORASG ? 't' : 'z', n->left,
			bitmask_to_bitnum (n->right->lval));
		return 0;
	}

	/*
	 * Bit toggle.
	 */
	if (n->op == OP_XORASG && n->left->op == OP_DOT) {
		if (n->right->op != OP_CONST || n->right->lval != 1) {
			error ("illegal bit toggle");
			return 0;
		}
		output (" bt %n,%D;", n->left->left,
			n->left->right->lval & 7);
		return 0;
	}

	/*
	 * Return constant.
	 */
	if (n->op == OP_RETVAL && n->left->op == OP_CONST &&
	    tsize (n->left->type) == 1) {
		output (" retc %n;", n->left);
		return 0;
	}

	/*
	 * One-operand IF statement.
	 */
	if (n->op == OP_IF && operword (n->right) && machif (n->left, 1)) {
		operator (n->right);
		return 0;
	}
	return n;
}

/*
 * Machine-dependent conditional expression.
 * Make jumps to true and false labels given.
 * Skip the final "goto true" operator.
 */
node_t *machcond (node_t *n, int true, int false)
{
	if (machif (n, 0)) {
		printgoto (false, -1);
		return 0;
	}
	return n;
}

/*
 * Try to make the machine-dependent compilation of the expression.
 */
node_t *machexpr (node_t *n, int target)
{
	switch (n->op) {
	case OP_NOT:
		if (n->left->op == OP_DOT) {
			output (" cta 0; bz? %n,%D; cta 1;", n->left->left,
				n->left->right->lval & 7);
			return 0;
		}
		break;

	case OP_DOT:
		output (" cta 0; bs? %n,%D; cta 1;",
			n->left, n->right->lval & 7);
		return 0;
	}
	return n;
}

/*
 * Compile the assignment and assignment operations
 * when the left argument is a pointer reference:
 *
 *      *N OP= WREG
 *
 * N is the left hand side, OP is the operation.
 * The value is on the accumulator.
 */
void storeref (node_t *n, int op)
{
	output (" xtr %n,FSR0;", n);

	switch (tsize (stab[n->rval].type)) {
	default:
	case 0:
		error ("invalid pointer type at left hand side of the assignment");
		return;
	case 1:
		switch (op) {
		default:        error ("internal assignment error");
		case OP_ASSIGN: output (" atx INDF0;");   break;
		case OP_ADDASG: output (" x+a INDF0;");   break;
		case OP_SUBASG: output (" x-a INDF0;");   break;
		case OP_ANDASG: output (" x&a INDF0;");   break;
		case OP_ORASG:  output (" x|a INDF0;");   break;
		case OP_XORASG: output (" x^a INDF0;");   break;
		case OP_LSASG:  output (" call ls1p;");   break;
		case OP_RSASG:  output (" call rs1p;");   break;
		case OP_MODASG: output (" call divmod11p; xtr A3,INDF0;"); break;
		case OP_MULASG: output (" a*x INDF0; xtr PRODL,INDF0;"); break;
		case OP_DIVASG:	output (" call divmod11p;"); break;
		}
		break;
	case 2:
		switch (op) {
		case OP_ASSIGN:
			output (" atx INDF0; x++ FSR0; xtr A1,INDF0;");
			break;
		case OP_MULASG:
		case OP_DIVASG:
		case OP_MODASG:
		case OP_ADDASG:
		case OP_SUBASG:
		case OP_LSASG:
		case OP_RSASG:
		case OP_ANDASG:
		case OP_ORASG:
		case OP_XORASG:
		default:
			error ("short pointer assignments not implemented yet");
			return;
		}
		break;
	case 4:
		switch (op) {
		case OP_ASSIGN:
			output (" atx INDF0;");
			output (" x++ FSR0; xtr A1,INDF0;");
			output (" x++ FSR0; xtr A2,INDF0;");
			output (" x++ FSR0; xtr A3,INDF0;");
			break;
		case OP_MULASG:
		case OP_DIVASG:
		case OP_MODASG:
		case OP_ADDASG:
		case OP_SUBASG:
		case OP_LSASG:
		case OP_RSASG:
		case OP_ANDASG:
		case OP_ORASG:
		case OP_XORASG:
		default:
			error ("long pointer assignments not implemented yet");
			return;
		}
		break;
	}
}

/*
 * Compile the assignment-divide by constant.
 * `l' is the left hand side, `r' - the right hand side.
 */
void divasg_by_const (node_t *l, node_t *r)
{
	if (r->lval <= 1)
		return;
	if (l->op == OP_REF) {
		/* Left hand side is the pointer reference (*ptr := val).
		 * It must be the near pointer. */
		if (l->left->op != OP_NAME ||
		    ! (stab[l->left->rval].type & TPTR)) {
			error ("bad pointer at left hand side of the assignment");
			return;
		}
		output (" xtr %n,FSR0;", l->left);
		switch (tsize (stab[l->left->rval].type)) {
		default:
		case 0:
			error ("invalid pointer type at left hand side of the div-assignment");
			return;
		case 1:
			output (" xta INDF0;");
			if (is_bitmask (r->lval)) {
				output (" a*c %d; rtx PRODH,INDF0;", 256/r->lval);
				break;
			}
			output (" atx A1; a*c %d; cta %d; call divcorr1; atx INDF0;",
				1+256/r->lval, r->lval);
			break;
		case 2:
			error ("short pointer div-assignment not implemented yet");
			break;
		case 4:
			error ("long pointer div-assignment not implemented yet");
			break;
		}
		return;
	}
	if (l->op != OP_NAME) {
		error ("invalid left hand side of the div-assignment");
		return;
	}
	switch (tsize (stab[l->rval].type)) {
	default:
	case 0:
		error ("invalid type of left hand side of the div-assignment");
		break;
	case 1:
		output (" xta %n;", l);
		if (is_bitmask (r->lval)) {
			output (" a*c %d; rtx PRODH,%n;", 256/r->lval, l);
			break;
		}
		output (" atx A1; a*c %d; cta %d; call divcorr1; atx %n;",
			1+256/r->lval, r->lval, l);
		break;
	case 2:
		error ("short div-assignment not implemented yet");
		break;
	case 4:
		error ("long div-assignment not implemented yet");
		break;
	}
}

/*
 * Compile the assignment and assignment operations.
 * N is the left hand side, OP is the operation.
 * The value is on the accumulator.
 */
void store (node_t *n, int op)
{
	if (n->op == OP_REF) {
		/* Left hand side is the pointer reference (*ptr := val).
		 * It must be the near pointer. */
		if (n->left->op != OP_NAME ||
		    ! (stab[n->left->rval].type & TPTR)) {
			error ("bad pointer at left hand side of the assignment");
			return;
		}
		storeref (n->left, op);
		return;
	}
	if (n->op != OP_NAME) {
		error ("invalid left hand side of the assignment");
		return;
	}

	switch (tsize (stab[n->rval].type)) {
	default:
	case 0:
		error ("invalid type of left hand side of the assignment");
		return;
	case 6:
		switch (op) {
		default:        error ("internal assignment error"); break;
		case OP_ASSIGN: output (" п 1,,%n;", n); break;
		case OP_ADDASG: output (" с %n,1,%n;", n, n); break;
		case OP_SUBASG: output (" в %n,1,%n;", n, n); break;
		case OP_ANDASG: output (" и %n,1,%n;", n, n); break;
		case OP_ORASG:  output (" или %n,1,%n;", n, n); break;
		case OP_XORASG: output (" н %n,1,%n;", n, n); break;
		case OP_LSASG:
			output (" xtr %n,A2; call ls1; atx %n;", n, n);
			break;
		case OP_RSASG:
			output (" xtr %n,A2; call rs1; atx %n;", n, n);
			break;
		case OP_MULASG:
			output (" у %n,1,%n;", n, n);
			break;
		case OP_DIVASG:
			output (" д %n,1,%n;", n, n);
			break;
		case OP_MODASG:
			output (" atx A2; xtr %n,A1; call divmod11;", n);
			output (" rtx A3,%n;", n);
			break;
		}
		break;
	}
}

void exprincr (node_t *n, int decr)
{
	if (n->op != OP_NAME) {
		error ("invalid %screment argument", decr ? "de" : "in");
		return;
	}
	output (" x%s %n;", decr ? "--" : "++", n);
}

void printgoto (int m, int cond)
{
	if (cond > 0)
		output (" a?;");
	else if (cond == 0)
		output (" z?;");
	output (" goto L%d;", m);
}

void loadconst (node_t *n)
{
	output (" cta %n;", n);
}

void loadname (node_t *n)
{
	switch (tsize (n->type)) {
	case 1:
		output (" и %n,_биты_1_8,1;", n);
		break;
	case 6:
		output (" п %n,,1;", n);
		break;
	default:
		error ("неверный размер переменной");
	}
}

void printstring (char *p)
{
	int delim = '"';

	printf ("%c", delim);
	while (*p) {
		unsigned char c = *p++;

		if (c < ' ' || c == 0177 || c == delim || c == '\\')
			printf ("\\%03o", c);
		else
			printf ("%c", c);
	}
	printf ("%c", delim);
}

void loadstring (char *s)
{
	int m = newlabel ();

	output ("L%d .const ", m);
	for (; *s; ++s) {
		printf ("%d,", (unsigned char) *s);
	}
	printf ("0");
	output ("; cta @L%d; atx A1; cta L%d;", m, m);
}

/*
 * Push the argument 2, freeing the working register.
 */
void pusharg (int sz)
{
	output (" atx A2;");
	if (sz > 1)
		output (" xtr A1,A3;");
}

/*
 * Try to make the machine-dependent compilation of the second argument.
 */
node_t *macharg2 (node_t *n)
{
	if (n->op == OP_NAME) {
		if (tsize (n->type) > 1)
			output (" xtr %n+1,A3;", n);
		output (" xtr %n,A2;", n);
		return 0;
	}
	if (n->op == OP_CONST && !n->rval && !n->lval) {
		if (tsize (n->type) > 1)
			output (" xz A3;");
		output (" xz A2;");
		return 0;
	}
	if (n->op == OP_CONST && !n->rval && tsize (n->type) == 1 &&
	    n->lval == 0xff) {
		output (" xs A2;");
		return 0;
	}
	return n;
}

int badarg1 (node_t *n)
{
	if (! n)
		return 0;
	if (tsize (n->type) > 2)
		return 1;
	if (LEAF (n->op))
		return 0;
	if (badarg1 (n->left))
		return 1;
	if (! BINARY (n->op))
		return 0;
	if (badarg1 (n->right))
		return 1;
	return 0;
}

/*
 * Convert the operand of size ASZ to size SZ.
 */
void cast (int sz, int asz)
{
	if (sz <= asz)
		return;
	if (asz == 1)
		output (" xz A1;");
	if (sz == 4)
		output (" xz A2; xz A3;");
}

/*
 * Print the unary op code.
 */
void printunary (int op, int type, int argtype)
{
	int sz, asz;

	sz = tsize (type);
	asz = tsize (argtype);
	switch (op) {
	case OP_NOT:            /* logical negate op (!x) */
		output (" a?; cta 1; a^c 1;");
		break;
	case OP_COMPL:          /* logical complement op (~x) */
		output (" ac;");
		break;
	case OP_NEG:            /* negate op (-x) */
		output (" anx A0;");
		break;
	case OP_CAST:           /* type cast op */
		/* Nothing to do. */
		break;
	case OP_REF:            /* de-reference op (*x) */
		output (" atx FSR0; xta INDF0;");
		break;
	case OP_ADDR:           /* address op (&x) */
		error ("internal error: &wreg");
		break;
	}
	cast (sz, asz);
}

/*
 * Print the binary op code.
 */
void printbinary (int op, int type, int ltype, node_t *arg, int target)
{
	int sz, lsz, rsz, i;

	sz = tsize (type);
	lsz = tsize (ltype);
	rsz = tsize (arg->type);
	if (rsz > lsz) {
		cast (rsz, lsz);
		lsz = rsz;
	}
	switch (op) {
	case OP_SUB:            /* - */
		if (arg->op == OP_NAME)       output (" в 1,%n,1;", arg);
		else if (arg->op == OP_CONST) output (" c-a %n;", arg);
		else error ("bad subtraction");
		break;
	case OP_ADD:            /* + */
		if (arg->op == OP_NAME)	      output (" с %n,1,1;", arg);
		else if (arg->op == OP_CONST) output (" a+c %n;", arg);
		else error ("bad addition");
		break;
	case OP_MUL:            /* * */
		if (arg->op == OP_NAME)       output (" у %n,1,1;", arg);
		else if (arg->op == OP_CONST) output (" a*c %n; xtr PRODL,A0;", arg);
		else error ("bad multiplication");
		break;
	case OP_AND:            /* & */
		if (arg->op == OP_NAME)       output (" a&x %n;", arg);
		else if (arg->op == OP_CONST) output (" a&c %n;", arg);
		else error ("bad logical and");
		break;
	case OP_OR:             /* | */
		if (arg->op == OP_NAME)       output (" a|x %n;", arg);
		else if (arg->op == OP_CONST) output (" a|c %n;", arg);
		else error ("bad logical or");
		break;
	case OP_XOR:            /* ^ */
		if (arg->op == OP_NAME)       output (" a^x %n;", arg);
		else if (arg->op == OP_CONST) output (" a^c %n;", arg);
		else error ("bad logical xor");
		break;
	case OP_EQU:            /* == */
		if (arg->op == OP_NAME)       output (" a^x %n;", arg);
		else if (arg->op == OP_CONST) output (" a^c %n;", arg);
		else error ("bad == op");
		output (" a?; cta 1; a^c 1;");
		break;
	case OP_NEQ:            /* != */
		if (arg->op == OP_NAME)       output (" a^x %n;", arg);
		else if (arg->op == OP_CONST) output (" a^c %n;", arg);
		else error ("bad != op");
		output (" a?; cta 1;");
		break;
	case OP_LE:             /* <= */
		if (arg->op == OP_NAME)       output (" a-x %n;", arg);
		else if (arg->op == OP_CONST) output (" c-a %n;", arg);
		else error ("bad <= op");
		output (" nc?; cta 1; a^c 1;");
		break;
	case OP_GT:             /* > */
		if (arg->op == OP_NAME)       output (" a-x %n;", arg);
		else if (arg->op == OP_CONST) output (" c-a %n;", arg);
		else error ("bad > op");
		output (" c?; cta 1; a^c 1;");
		break;
	case OP_LT:             /* < */
		if (arg->op == OP_NAME)       output (" a-x %n;", arg);
		else if (arg->op == OP_CONST) output (" c-a %n;", arg);
		else error ("bad < op");
		output (" z?; goto 1f; nc?;1: cta 1; a^c 1;");
		break;
	case OP_GE:             /* >= */
		if (arg->op == OP_NAME)       output (" a-x %n;", arg);
		else if (arg->op == OP_CONST) output (" c-a %n;", arg);
		else error ("bad < op");
		output (" z?; goto 1f; c?; cta 1;1: a^c 1;");
		break;
	case OP_DIV:            /* / */
		if (arg->op == OP_CONST) {
			if (arg->lval <= 1)
				break;
			if (is_bitmask (arg->lval)) {
				output (" a*c %d; xta PRODH;", 256/arg->lval);
				break;
			}
			output (" atx A1; a*c %d; cta %d; call divcorr1;",
				1+256/arg->lval, arg->lval);
			break;
		}
		if (arg->op != OP_NAME)
			error ("bad divide");
		output (" дел 1,%n,1;", arg);
		break;
	case OP_MOD:            /* % */
		if (arg->op == OP_CONST) {
			if (is_bitmask (arg->lval)) {
				output (" a&c %d;", arg->lval - 1);
				break;
			}
			output (" atx A1; a*c %d; cta %d; call divcorr1; xta A1;",
				1+256/arg->lval, arg->lval);
			break;
		}
		output (" atx A1;");
		if (arg->op == OP_NAME)       output (" xtr %n,A2;", arg);
		else if (arg->op == OP_CONST) output (" cta %n; atx A2;", arg);
		else error ("bad remainder");
		output (" call divmod11;");
		output (" xtr A3,A0;");
		break;
	case OP_LSHIFT:         /* << */
		if (arg->op == OP_NAME) {
			output (" atx A2; xta %n; call ls1;", arg);
			if (target == TARG_COND)
				output (ASM_COND);
		} else if (arg->op == OP_CONST) {
			i = arg->lval & 7;
			if (i == 1)
				output (" x<<a A0; a&c 0x7f;");
			else if (i == 4)
				output (" aw; a&c 0xf0;");
			else if (i) {
				if (i >= 4) {
					output (" xw A0;");
					i -= 4;
				}
				while (i-- > 0)
					output (" x<<a A0;");
				output (" a&c %d;", (0xff <<
					(arg->lval & 7)) & 0xff);
			}
		} else error ("bad left shift op");
		break;
	case OP_RSHIFT:         /* >> */
		if (arg->op == OP_NAME) {
			output (" atx A2; xta %n; call rs1;", arg);
			if (target == TARG_COND)
				output (ASM_COND);
		} else if (arg->op == OP_CONST) {
			i = arg->lval & 7;
			if (i == 1)
				output (" x>>a A0; a&c 0x7f;");
			else if (i == 4)
				output (" aw; a&c 0x0f;");
			else if (i) {
				if (i >= 4) {
					output (" xw A0;");
					i -= 4;
				}
				while (i-- > 0)
					output (" x>>a A0;");
				output (" a&c %d;", 0xff >>
					(arg->lval & 7));
			}
		} else error ("bad right shift op");
		break;
	}
	cast (sz, lsz);
}

/*
 * Compile the binary operation.
 */
void machop (node_t *n, int target)
{
	if (! LEAF (n->right->op)) {
		error ("too complex rhs of expression, simplify");
/*
fclose (stdout);
fdopen (2, "w");
dup (2);
nprint (n->right, 0);
*/
		return;
	}
	expr3 (n->left, TARG_ACC);
	printbinary (n->op, n->type, n->left->type, n->right, target);
}

/*
 * The value is on the accumulator;
 * make the conditional testing (Z-bit).
 */
void makecond (int sz)
{
	output (" н 1;");
}
