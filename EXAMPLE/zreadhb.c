/*! \file
Copyright (c) 2003, The Regents of the University of California, through
Lawrence Berkeley National Laboratory (subject to receipt of any required 
approvals from U.S. Dept. of Energy) 

All rights reserved. 

The source code is distributed under BSD license, see the file License.txt
at the top-level directory.
*/

/*! @file 
 * \brief Read a DOUBLE COMPLEX PRECISION matrix stored in Harwell-Boeing format
 *
 * <pre>
 * -- Distributed SuperLU routine (version 9.0) --
 * Lawrence Berkeley National Lab, Univ. of California Berkeley.
 * September 1, 1999
 * </pre>
 */
#include "dcomplex.h"
#include <stdio.h>
#include <stdlib.h>
#include "superlu_zdefs.h"

/*
 * Prototypes
 */
static void ReadVector(FILE *, int_t, int_t *, int_t, int_t);
static void zReadValues(FILE *, int_t, doublecomplex *, int_t, int_t);
static int DumpLine(FILE *);
static int ParseIntFormat(char *, int_t *, int_t *);
static int ParseFloatFormat(char *, int_t *, int_t *);

/*! \brief
 *
 * <pre>
 * Purpose
 * =======
 * 
 * Read a DOUBLE COMPLEX PRECISION matrix stored in Harwell-Boeing format 
 * as described below.
 * 
 * Line 1 (A72,A8) 
 *  	Col. 1 - 72   Title (TITLE) 
 *	Col. 73 - 80  Key (KEY) 
 * 
 * Line 2 (5I14) 
 * 	Col. 1 - 14   Total number of lines excluding header (TOTCRD) 
 * 	Col. 15 - 28  Number of lines for pointers (PTRCRD) 
 * 	Col. 29 - 42  Number of lines for row (or variable) indices (INDCRD) 
 * 	Col. 43 - 56  Number of lines for numerical values (VALCRD) 
 *	Col. 57 - 70  Number of lines for right-hand sides (RHSCRD) 
 *                    (including starting guesses and solution vectors 
 *		       if present) 
 *           	      (zero indicates no right-hand side data is present) 
 *
 * Line 3 (A3, 11X, 4I14) 
 *   	Col. 1 - 3    Matrix type (see below) (MXTYPE) 
 * 	Col. 15 - 28  Number of rows (or variables) (NROW) 
 * 	Col. 29 - 42  Number of columns (or elements) (NCOL) 
 *	Col. 43 - 56  Number of row (or variable) indices (NNZERO) 
 *	              (equal to number of entries for assembled matrices) 
 * 	Col. 57 - 70  Number of elemental matrix entries (NELTVL) 
 *	              (zero in the case of assembled matrices) 
 * Line 4 (2A16, 2A20) 
 * 	Col. 1 - 16   Format for pointers (PTRFMT) 
 *	Col. 17 - 32  Format for row (or variable) indices (INDFMT) 
 *	Col. 33 - 52  Format for numerical values of coefficient matrix (VALFMT) 
 * 	Col. 53 - 72 Format for numerical values of right-hand sides (RHSFMT) 
 *
 * Line 5 (A3, 11X, 2I14) Only present if there are right-hand sides present 
 *    	Col. 1 	      Right-hand side type: 
 *	         	  F for full storage or M for same format as matrix 
 *    	Col. 2        G if a starting vector(s) (Guess) is supplied. (RHSTYP) 
 *    	Col. 3        X if an exact solution vector(s) is supplied. 
 *	Col. 15 - 28  Number of right-hand sides (NRHS) 
 *	Col. 29 - 42  Number of row indices (NRHSIX) 
 *          	      (ignored in case of unassembled matrices) 
 *
 * The three character type field on line 3 describes the matrix type. 
 * The following table lists the permitted values for each of the three 
 * characters. As an example of the type field, RSA denotes that the matrix 
 * is real, symmetric, and assembled. 
 *
 * First Character: 
 *	R Real matrix 
 *	C Complex matrix 
 *	P Pattern only (no numerical values supplied) 
 *
 * Second Character: 
 *	S Symmetric 
 *	U Unsymmetric 
 *	H Hermitian 
 *	Z Skew symmetric 
 *	R Rectangular 
 *
 * Third Character: 
 *	A Assembled 
 *	E Elemental matrices (unassembled) 
 * </pre>
 */

void
zreadhb_dist(int iam, FILE *fp, int_t *nrow, int_t *ncol, int_t *nonz,
	     doublecomplex **nzval, int_t **rowind, int_t **colptr)
{

    register int_t i, numer_lines, rhscrd = 0;
    int_t tmp, colnum, colsize, rownum, rowsize, valnum, valsize;
    char buf[100], type[4];

#if ( DEBUGlevel>=1 )
    CHECK_MALLOC(0, "Enter zreadhb_dist()");
#endif

    /* Line 1 */
    fgets(buf, 100, fp);

    /* Line 2 */
    for (i=0; i<5; i++) {
	fscanf(fp, "%14c", buf); buf[14] = 0;
	tmp = atoi(buf); /*sscanf(buf, "%d", &tmp);*/
	if (i == 3) numer_lines = tmp;
	if (i == 4 && tmp) rhscrd = tmp;
    }
    DumpLine(fp);

    /* Line 3 */
    fscanf(fp, "%3c", type);
    fscanf(fp, "%11c", buf); /* pad */
    type[3] = 0;
#if ( DEBUGlevel>=1 )
    if ( !iam ) printf("Matrix type %s\n", type);
#endif
    
    fscanf(fp, "%14c", buf); *nrow = atoi(buf); 
    fscanf(fp, "%14c", buf); *ncol = atoi(buf); 
    fscanf(fp, "%14c", buf); *nonz = atoi(buf); 
    fscanf(fp, "%14c", buf); tmp = atoi(buf);   
    
    if (tmp != 0)
	if ( !iam ) printf("This is not an assembled matrix!\n");
    if (*nrow != *ncol)
	if ( !iam ) printf("Matrix is not square.\n");
    DumpLine(fp);

    /* Allocate storage for the three arrays ( nzval, rowind, colptr ) */
    zallocateA_dist(*ncol, *nonz, nzval, rowind, colptr);

    /* Line 4: format statement */
    fscanf(fp, "%16c", buf);
    ParseIntFormat(buf, &colnum, &colsize);
    fscanf(fp, "%16c", buf);
    ParseIntFormat(buf, &rownum, &rowsize);
    fscanf(fp, "%20c", buf);
    ParseFloatFormat(buf, &valnum, &valsize);
    fscanf(fp, "%20c", buf);
    DumpLine(fp);

    /* Line 5: right-hand side */    
    if ( rhscrd ) DumpLine(fp); /* skip RHSFMT */

#if ( DEBUGlevel>=1 )
    if ( !iam ) {
	printf("%d rows, %d nonzeros\n", *nrow, *nonz);
	printf("colnum %d, colsize %d\n", colnum, colsize);
	printf("rownum %d, rowsize %d\n", rownum, rowsize);
	printf("valnum %d, valsize %d\n", valnum, valsize);
    }
#endif
    
    ReadVector(fp, *ncol+1, *colptr, colnum, colsize);
#if ( DEBUGlevel>=1 )
    if ( !iam )	printf("read colptr[%d] = %d\n", *ncol, (*colptr)[*ncol]);
#endif
    ReadVector(fp, *nonz, *rowind, rownum, rowsize);
#if ( DEBUGlevel>=1 )
    if ( !iam )	printf("read rowind[%d] = %d\n", *nonz-1, (*rowind)[*nonz-1]);
#endif
    if ( numer_lines ) {
        zReadValues(fp, *nonz, *nzval, valnum, valsize);
    }

    fclose(fp);
#if ( DEBUGlevel>=1 )
    CHECK_MALLOC(0, "Exit zreadhb_dist()");
#endif
}

/* Eat up the rest of the current line */
static int DumpLine(FILE *fp)
{
    register int c;
    while ((c = fgetc(fp)) != '\n') ;
    return 0;
}

static int ParseIntFormat(char *buf, int_t *num, int_t *size)
{
    char *tmp;

    tmp = buf;
    while (*tmp++ != '(') ;
    *num = atoi(tmp); 
    while (*tmp != 'I' && *tmp != 'i') ++tmp;
    ++tmp;
    *size = atoi(tmp); 
    return 0;
}

static int ParseFloatFormat(char *buf, int_t *num, int_t *size)
{
    char *tmp, *period;
    
    tmp = buf;
    while (*tmp++ != '(') ;
    *num = atoi(tmp); 
    while (*tmp != 'E' && *tmp != 'e' && *tmp != 'D' && *tmp != 'd'
	   && *tmp != 'F' && *tmp != 'f') {
       /* May find kP before nE/nD/nF, like (1P6F13.6). In this case the
           num picked up refers to P, which should be skipped. */
        if (*tmp=='p' || *tmp=='P') {
           ++tmp;
           *num = atoi(tmp); /*sscanf(tmp, "%d", num);*/
        } else {
           ++tmp;
        }
    }
    ++tmp;
    period = tmp;
    while (*period != '.' && *period != ')') ++period ;
    *period = '\0';
    *size = atoi(tmp); 

    return 0;
}

static void
ReadVector(FILE *fp, int_t n, int_t *where, int_t perline, int_t persize)
{
    register int_t i, j, item;
    char tmp, buf[100];
    
    i = 0;
    while (i < n) {
	fgets(buf, 100, fp);    /* read a line at a time */
	for (j=0; j<perline && i<n; j++) {
	    tmp = buf[(j+1)*persize];     /* save the char at that place */
	    buf[(j+1)*persize] = 0;       /* null terminate */
	    item = atoi(&buf[j*persize]); 
	    buf[(j+1)*persize] = tmp;     /* recover the char at that place */
	    where[i++] = item - 1;
	}
    }
}

/* Read complex numbers as pairs of (real, imaginary) */
void
zReadValues(FILE *fp, int_t n, doublecomplex *destination, 
             int_t perline, int_t persize)
{
    register int_t i, j, k, s;
    register int_t pair;
    register double realpart;
    char tmp, buf[100];
    
    i = 0;
    pair = 0;
    while (i < n) {
	fgets(buf, 100, fp);    /* read a line at a time */
	for (j=0; j<perline && i<n; j++) {
	    tmp = buf[(j+1)*persize];     /* save the char at that place */
	    buf[(j+1)*persize] = 0;       /* null terminate */
	    s = j*persize;
	    for (k = 0; k < persize; ++k) /* No D_ format in C */
		if ( buf[s+k] == 'D' || buf[s+k] == 'd' ) buf[s+k] = 'E';
	    if ( pair == 0 ) {
	  	/* The value is real part */
		realpart = atof(&buf[s]);
		pair = 1;
	    } else {
		/* The value is imaginary part */
	        destination[i].r = realpart;
		destination[i++].i = atof(&buf[s]);
		pair = 0;
	    }
	    buf[(j+1)*persize] = tmp;     /* recover the char at that place */
	}
    }
}

