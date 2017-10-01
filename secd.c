#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "secd.h"

unsigned long *is_atom_tbl = NULL, *is_num_tbl = NULL, *cell_store = NULL;

static pointer ff = 0, s, e, c, d, w, t, f, nil;
static int storetop=0;
static char *stringstore = NULL;

typedef enum
{
	LD = 1, LDC, LDF, AP, RTN,
	DUM, RAP, SEL, JOIN, CAR,
	CDR, ATOM, CONS, EQ, ADD,
	SUB, MUL, DIV, REM, LEQ,
	STOP,
	/* The following are LISP ops that are compiled
	   to the SECD primitives */
	QUOTE, IF, LAMBDA, LET, LETREC, APPLY
} SECD_ops;

/******************************/
/* Create and destroy storage */
/******************************/

static pointer symbol(char *s);

static void init_storage(void)
{
	pointer i;
	assert(is_num_tbl == NULL && is_atom_tbl == NULL && cell_store == NULL);
	is_num_tbl = (unsigned long *)calloc((NUM_CELLS+31)/32, sizeof(long));
	assert(is_num_tbl);
	is_atom_tbl = (unsigned long *)calloc((NUM_CELLS+31)/32, sizeof(long));
	assert(is_atom_tbl);
	cell_store = (unsigned long *)calloc(NUM_CELLS, sizeof(long));
	assert(cell_store);
	ff = 0;
	for (i=1; i < NUM_CELLS; i++)
	{
		SET_CDR(i, ff);
		ff = i;
	}
	storetop = 0;
	assert(stringstore == NULL);
	stringstore = calloc(STRING_SPACE, sizeof(char));
	assert(stringstore);
	nil = symbol("NIL");
	t = symbol("T");
	f = symbol("F");
}

static void free_storage(void)
{
	assert(is_num_tbl && is_atom_tbl && cell_store && stringstore);
	free(is_num_tbl);
	free(is_atom_tbl);
	free(cell_store);
	free(stringstore);
}

/*********************/
/* Garbage Collector */
/*********************/

static unsigned long *is_marked = NULL;
static short collect_cnt;

#define IS_MARKED(n)		((is_marked[(n)/32] & (1l<<((n)%32))) != 0l)
#define SET_MARK(n)		is_marked[(n)/32] |= (1l<<((n)%32))

static void mark(pointer n)
{
	if (!IS_MARKED(n))
	{
		SET_MARK(n);
		if (!IS_ATOM(n))
		{
			mark(CAR(n));
			mark(CDR(n));
		}
	}
}

static void collect(void)
{
	pointer i;
	for (i=1; i<NUM_CELLS; i++)
	{
		if (!IS_MARKED(i))
		{
			CDR(i) = ff;
			ff = i;
			collect_cnt++;
		}
	}
}

static void collect_garbage(void)
{
	fprintf(stderr,"Collecting garbage..."); fflush(stderr);
	collect_cnt = 0;
	is_marked = (unsigned long *)calloc((NUM_CELLS+31)/32, sizeof(long));
	assert(is_marked);
	mark(s); mark(e); mark(c); mark(d);
	mark(w); mark(t); mark(f); mark(nil);
	collect();
	free(is_marked);
	if (ff==0) exit(-1); /* FAIL */
	fprintf(stderr,"Done (%d)\n", collect_cnt);
}

/****************/
/* String Store */
/****************/

static pointer store(char *name)
{
	pointer i = 0;
	while (i<storetop)
	{
		if (strcmp(stringstore+i, name)==0)
			goto done;
		else i += strlen(stringstore+i)+1;
	}
	i = storetop;
	assert((storetop + strlen(name) + 1) < STRING_SPACE);
	storetop += strlen(name) + 1;
	strcpy(stringstore + i, name);
done:
	return i;
}

static char *lookup(pointer n)
{
	assert(n==0 || (n>0 && n<STRING_SPACE && stringstore[n-1]=='\0'));
	return stringstore+n;
}

/*************/
/* Selectors */
/*************/

static pointer car(pointer p)
{
	if (IS_CONS(p)) return CAR(p);
	else return nil;
}

static pointer cdr(pointer p)
{
	if (IS_CONS(p)) return CDR(p);
	else return nil;
}

/****************/
/* Constructors */
/****************/

static pointer get_cell(void)
{
	pointer t;
	if (ff == 0) collect_garbage();
	t = ff;
	ff = CDR(ff);
	return t;
}

static pointer symbol(char *s)
{
	pointer t = get_cell();
	SET_ATOM(t);
	CLR_NUM(t);
	SET_IVALUE(t, store(s));
	return t;
}

static pointer number(long n)
{
	pointer t = get_cell();
	SET_ATOM(t);
	SET_NUM(t);
	SET_IVALUE(t, n);
	return t;
}

static pointer cons(pointer pcar, pointer pcdr)
{
	pointer t = get_cell();
	CLR_ATOM(t);
	CLR_NUM(t);
	SET_CAR(t, pcar);
	SET_CDR(t, pcdr);
	return t;
}

/********************/
/* Lexical Analysis */
/********************/

#define TOKEN_LENGTH 32

static char *op_tbl[] =
{
	"LD", "LDC", "LDF", "AP", "RTN",
	"DUM", "RAP", "SEL", "JOIN", "CAR",
	"CDR", "ATOM", "CONS", "EQ", "ADD",
	"SUB", "MUL", "DIV", "REM", "LEQ",
	"STOP",
	"QUOTE", "IF", "LAMBDA", "LET", "LETREC", "APPLY", NULL
};

static long is_op(char *t)
{
	char buf[10];
	int i = 0;
	strncpy(buf,t,9);
	strupr(buf);
	while (op_tbl[i])
		if (strcmp(op_tbl[i], buf) == 0)
		{
			/*fprintf(stderr,"Got op %s\n", op_tbl[i]);*/
			return (long)(i+1);
		}
		else i++;
	return 0l;
}

static char token[TOKEN_LENGTH];
static int ch = ' ';

typedef enum
{
	ENDFILE, NUMERIC, ALPHANUMERIC, DELIMETER
} tokentype_t;

static tokentype_t tokentype;

static void gettoken(void)
{
	long v;
	char *t = token;
	*t = '\0';
	while (!feof(stdin) && isspace(ch))
		ch = getchar();
	if (feof(stdin)) tokentype = ENDFILE;
	else if (isdigit(ch) || ch=='-')
	{
		*t++ = ch;
		ch = getchar();
		while (isdigit(ch))
		{
			*t++ = ch;
			ch = getchar();
		}
		tokentype = NUMERIC;
	}
	else if (isalpha(ch))
	{
		while (isalpha(ch) || isdigit(ch))
		{
			*t++ = ch;
			ch = getchar();
		}
		*t = '\0';
		if ((v = is_op(token)) != 0)
		{
			sprintf(token,"%ld", v);
			tokentype = NUMERIC;
		}
		else
			tokentype = ALPHANUMERIC;
	}
	else
	{
		*t++ = ch;
		ch = getchar();
		tokentype = DELIMETER;
	}
	*t = '\0';
}

static void scan(void)
{
	gettoken();
	if (tokentype == ENDFILE) strcpy(token,")");
}

static pointer getexplist(void);

static pointer getexp(void)
{
	pointer e;
	if (strcmp(token,"(")==0)
	{
		scan();
		e = getexplist();
	}
	else if (tokentype == NUMERIC)
		e = number(atol(token));
	/* THIS IS DIFFERENT TO HENDERSON! */
	else if (tokentype == ALPHANUMERIC)
		e = symbol(token);
	else return nil;
	scan();
	return e;
}

static pointer getexplist(void)
{
	pointer ecar = getexp(), ecdr;
	if (strcmp(token,".")==0)
	{
		scan();
		ecdr = getexp();
	}
	else if (strcmp(token, ")")==0)
		ecdr = nil;
	else
		ecdr = getexplist();
	return cons(ecar, ecdr);
}

/*******************/
/* Output Routines */
/*******************/

static int col = 0;

void puttoken(char *t)
{
	printf("%s ", t);
	col += strlen(t);
	if (col > 65)
	{
		putchar('\n');
		col = 0;
	}
}

void putexp(pointer n)
{
	if (IS_SYMBOL(n))
	{
		puttoken(lookup(IVALUE(n)));
		puttoken(" ");
	}
	else if (IS_NUMBER(n))
	{
		char buf[80];
		sprintf(buf, "%ld ", IVALUE(n));
		puttoken(buf);
	}
	else
	{
		pointer p = n;
		puttoken("(");
		while (IS_CONS(p))
		{
			putexp(car(p));
			p = cdr(p);
		}
		if (!IS_SYMBOL(p) || strcmp(lookup(IVALUE(p)), "NIL"))
		{
			puttoken(".");
			putexp(p);
		}
		puttoken(")");
	}
}

/************/
/* Executor */
/************/

pointer execute(pointer fn, pointer args)
{
	s = cons(args, nil);
	e = nil;
	c = fn;
	d = nil;
	for (;;)
	{
		long op = IVALUE(car(c));
		if (op>=LD && op<=STOP)
			printf("\n\nHandling op %s", op_tbl[op-1]);
		else
			printf("\n\nHandling op %d", op);
		printf("\ns = "); col=0; putexp(s);
		printf("\ne = "); col=0; putexp(e);
		printf("\nc = "); col=0; putexp(c);
		printf("\nd = "); col=0; putexp(d);

		switch (op)
		{
		case LD:
		{
			short lim, i;
			for (w = e, i = 0, lim = IVALUE(car(car(cdr(c)))); i < lim; i++)
				w = cdr(w);
			w = car(w);
			for (i = 0, lim = IVALUE(cdr(car(cdr(c)))); i < lim; i++)
				w = cdr(w);
			w = car(w);
			s = cons(w, s);
			c = cdr(cdr(c));
			break;
		}
		case LDC:
			s = cons(car(cdr(c)), s);
			c = cdr(cdr(c));
			break;
		case LDF:
			s = cons(cons(car(cdr(c)), e), s);
			c = cdr(cdr(c));
			break;
		case AP:
			d = cons(cdr(cdr(s)), cons(e, cons(cdr(c), d)));
			e = cons(car(cdr(s)), cdr(car(s)));
			c = car(car(s));
			s = nil;
			break;
		case RTN:
			s = cons(car(s), car(d));
			e = car(cdr(d));
			c = car(cdr(cdr(d)));
			d = cdr(cdr(cdr(d)));
			break;
		case DUM:
			e = cons(nil, e);
			c = cdr(c);
			break;
		case RAP:
			d = cons(cdr(cdr(s)), cons(cdr(e), cons(cdr(c), d)));
			e = cdr(car(s));
			SET_CAR(e, car(cdr(s)));
			c = car(car(s));
			s = nil;
			break;
		case SEL:
			d = cons(cdr(cdr(cdr(c))), d);
			if (strcmp(lookup(IVALUE(car(s))), "T") == 0)
				c = car(cdr(c));
			else
				c = car(cdr(cdr(c)));
			s = cdr(s);
			break;
		case JOIN:
			c = car(d);
			d = cdr(d);
			break;
		case CAR:
			s = cons(car(car(s)), cdr(s));
			c = cdr(c);
			break;
		case CDR:
			s = cons(cdr(car(s)), cdr(s));
			c = cdr(c);
			break;
		case ATOM:
			if (IS_NUMBER(car(s)) || IS_SYMBOL(car(s)))
				s = cons(t, cdr(s));
			else
				s = cons(f, cdr(s));
			c = cdr(c);
			break;
		case CONS:
			s = cons(cons(car(s), car(cdr(s))), cdr(cdr(s)));
			c = cdr(c);
			break;
		case EQ:
			if (
				(
				(IS_SYMBOL(car(s)) && IS_SYMBOL(car(cdr(s))))
				||
				(IS_NUMBER(car(s)) && IS_NUMBER(car(cdr(s))))
				) && (IVALUE(car(cdr(s))) == IVALUE(car(s))))
					s = cons(t, cdr(cdr(s)));
			else
				s = cons(f, cdr(cdr(s)));
			c = cdr(c);
			break;
		case ADD:
			s = cons(
				number( IVALUE(car(cdr(s))) + IVALUE(car(s)) ),
				cdr(cdr(s))
				);
			c = cdr(c);
			break;
		case SUB:
			s = cons(
				number( IVALUE(car(cdr(s))) - IVALUE(car(s)) ),
				cdr(cdr(s))
				);
			c = cdr(c);
			break;
		case MUL:
			s = cons(
				number( IVALUE(car(cdr(s))) * IVALUE(car(s)) ),
				cdr(cdr(s))
				);
			c = cdr(c);
			break;
		case DIV:
			s = cons(
				number( IVALUE(car(cdr(s))) / IVALUE(car(s)) ),
				cdr(cdr(s))
				);
			c = cdr(c);
			break;
		case REM:
			s = cons(
				number( IVALUE(car(cdr(s))) % IVALUE(car(s)) ),
				cdr(cdr(s))
				);
			c = cdr(c);
			break;
		case LEQ:
			if ( IVALUE(car(cdr(s))) <= IVALUE(car(s)))
				s = cons(t, cdr(cdr(s)));
			else
				s = cons(f, cdr(cdr(s)));
			c = cdr(c);
			break;
		case STOP:
			goto done;
		default:
			fprintf(stderr,"Bad op %ld!\n", IVALUE(car(c)));
			return -1;
		}
	}
done:
	return car(s);
}

/*********/
/* Tests */
/*********/

static void test1(void)
{
	gettoken();
	while (tokentype != ENDFILE)
	{
		puttoken(token);
		gettoken();
	}
}

static void test2(void)
{
	gettoken();
	while (tokentype != ENDFILE)
	{
		if (tokentype==ALPHANUMERIC)
		{
			pointer p = store(token);
			strcpy(token, lookup(p));
		}
		puttoken(token);
		gettoken();
	}
}

static void test3(void)
{
	char buf[20];
	pointer i, l = nil;
	for (i = 1; i<10; i++)
		l = cons(number((long)i), l);
	while (l != nil)
	{
		sprintf(buf, "%ld", IVALUE(car(l)));
		puttoken(buf);
		l = cdr(l);
	}
}

static void test4(int M, int N)
{
	pointer j, l, i, n, r;
	for (j = 1; j<M; j++)
	{
		l = nil;
		for (i = 1; i<N; i++)
			l = cons(number((long)i), l);
	}
}

static void test5(void)
{
	gettoken();
	putexp(getexp());
}

static void test6(void)
{
	gettoken();
	putexp(getexplist());
}

static void test7(void)
{
	pointer fn, args;
	gettoken();
	fn = getexp();
	args = getexplist();
	putexp(fn);
	putexp(args);
}

static void test8(void)
{
	pointer fn, args;
	short result;
	gettoken();
	do
	{
		fn = getexp();
		args = getexplist();
		printf("Function is "); putexp(fn);
		printf("\nArgs are "); col=0; putexp(args);
		result = (short)execute(fn, args);
		if (result<0) break;
		printf("\nResult is "); col=0; putexp(result);
	}
	while (tokentype != ENDFILE);
}

/************/
/* Compiler */
/************/

pointer cadr(pointer x) { return car(cdr(x)); }
pointer cddr(pointer x) { return cdr(cdr(x)); }
pointer cdar(pointer x) { return cdr(car(x)); }
pointer caar(pointer x) { return car(car(x)); }
pointer caddr(pointer x) { return car(cdr(cdr(x))); }
pointer cadddr(pointer x) { return car(cdr(cdr(cdr(x)))); }

short position(pointer x, pointer a)
{
	if (x == car(a)) return 0;
	else return 1 + position(x, cdr(a));
}

int member(pointer x, pointer s)
{
	if (s==nil) return 0;
	else if (x == car(s)) return 1;
	else return member(x, cdr(s));
}

pointer location(pointer x, pointer n)
{
	if (member(x, car(n)))
		return cons(number(0), number(position(x, car(n))));
	else
	{
		pointer z = location(x, cdr(n));
		return cons(number(IVALUE(car(z))+1), cdr(z));
	}
}

pointer vars(pointer d)
{
	if (d == nil) return nil;
	else return cons(caar(d), vars(cdr(d)));
}

pointer exprs(pointer d)
{
	if (d==nil) return nil;
	else return cons(cdar(d), exprs(cdr(d)));
}

pointer comp(pointer expr, pointer nlist, pointer code);

pointer complis(pointer e, pointer n, pointer c)
{
	if (e==nil)
		return cons(number(LDC), cons(nil, c));
	else
		return complis(cdr(e), n, comp(car(e), n, cons(number(CONS), c)));
}

pointer comp(pointer expr, pointer nlist, pointer code)
{
	if (IS_ATOM(expr))
		return cons(number(LD), cons(location(expr, nlist), code));
	else
	{
		long op = IVALUE(car(expr));
		if (op>=LD && op<=APPLY)
			printf("\n\nHandling op %s", op_tbl[op-1]);
		else
			printf("\n\nHandling op %d", op);
		switch (op)
		{
		case QUOTE:
			return cons(number(LDC), cons(cadr(expr), code));
		case ADD:
			return comp(cadr(expr), nlist,
				comp(caddr(expr), nlist, cons(number(ADD), c )));
		case SUB:
			return comp(cadr(expr), nlist,
				comp(caddr(expr), nlist, cons(number(SUB), c )));
		case MUL:
			return comp(cadr(expr), nlist,
				comp(caddr(expr), nlist, cons(number(MUL), c )));
		case DIV:
			return comp(cadr(expr), nlist,
				comp(caddr(expr), nlist, cons(number(DIV), c )));
		case REM:
			return comp(cadr(expr), nlist,
				comp(caddr(expr), nlist, cons(number(REM), c )));
		case EQ:
			return comp(cadr(expr), nlist,
				comp(caddr(expr), nlist, cons(number(EQ), c )));
		case LEQ:
			return comp(cadr(expr), nlist,
				comp(caddr(expr), nlist, cons(number(LEQ), c )));
		case CAR:
			return comp(cadr(expr), nlist, cons(number(CAR), c ));
		case CDR:
			return comp(cadr(expr), nlist, cons(number(CDR), c ));
		case ATOM:
			return comp(cadr(expr), nlist, cons(number(ATOM), c ));
		case CONS:
			return comp(caddr(expr), nlist,
				comp(cadr(expr), nlist, cons(number(CONS), c)));
		case IF:
		{
			pointer elsept, thenpt;
			elsept = comp(cadddr(expr), nlist, cons(number(JOIN), nil));
			thenpt = comp(caddr(expr), nlist, cons(number(JOIN), nil));
			return comp(cadr(expr), nlist, cons(number(SEL),
				cons(thenpt, cons(elsept, code))));
		}
		case LAMBDA:
		{
			pointer body = comp(caddr(expr), cons(cadr(expr), nlist),
				cons(number(RTN), nil));
			return cons(number(LDF), cons(body, code));
		}
		case LET:
		{
			pointer args = exprs(cddr(expr));
			pointer m = cons(vars(cddr(expr)), nlist);
			pointer body = comp(cadr(expr), m, cons(number(RTN), nil));
			return complis(args, nlist, cons(number(LDF), cons(body, cons(number(AP), c))));
		}
		case LETREC:
		{
			pointer args = exprs(cddr(expr));
			pointer m = cons(vars(cddr(expr)), nlist);
			pointer body = comp(cadr(expr), m, cons(number(RTN), nil));
			return cons(number(DUM), complis(args, m, cons(number(LDF), cons(body, cons(number(RAP), c)))));
		}
		default:
			return complis(cdr(expr), nlist, comp(car(expr), nlist, cons(number(AP), c)));
		}
	}
}

pointer compile(pointer expr)
{
	return comp(expr, nil, nil);
}

/********/
/* Main */
/********/

void main(void)
{
	init_storage();
	/* test4(20,20); */
	test8();
	free_storage();
}

