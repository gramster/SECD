/*
 * secd.h - Header file for SECD machine
 */

#define NUM_CELLS		100
#define STRING_SPACE	16000

typedef unsigned short	pointer;

extern unsigned long *is_atom_tbl, *is_num_tbl, *cell_store;

#define IVALUE(n)			((long)cell_store[n])
#define SET_IVALUE(n,v)	cell_store[n] = ((long)(v))
#define CAR(n)			( (pointer) (((short *)cell_store)[(n)*2]) )
#define CDR(n)			( (pointer) (((short *)cell_store)[(n)*2+1]) )
#define SET_CAR(n,v)		(((pointer *)cell_store)[(n)*2]) = ((pointer)(v)) 
#define SET_CDR(n,v)		(((pointer *)cell_store)[(n)*2+1]) = ((pointer)(v)) 
#define IS_ATOM(n)		((is_atom_tbl[(n)/32] & (1l<<((n)%32))) != 0l)
#define SET_ATOM(n)		is_atom_tbl[(n)/32] |= (1l<<((n)%32))
#define CLR_ATOM(n)		is_atom_tbl[(n)/32] &= ~(1l<<((n)%32))
#define IS_NUM(n)			((is_num_tbl[(n)/32] & (1l<<((n)%32))) != 0l)
#define SET_NUM(n)		is_num_tbl[(n)/32] |= (1l<<((n)%32))
#define CLR_NUM(n)		is_num_tbl[(n)/32] &= ~(1l<<((n)%32))
#define IS_CONS(n)		(!IS_ATOM(n) && !IS_NUM(n))
#define IS_SYMBOL(n)		(IS_ATOM(n) && !IS_NUM(n))
#define IS_NUMBER(n)		(IS_ATOM(n) && IS_NUM(n))


