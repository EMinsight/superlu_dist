/*! \file
Copyright (c) 2003, The Regents of the University of California, through
Lawrence Berkeley National Laboratory (subject to receipt of any required
approvals from U.S. Dept. of Energy)

All rights reserved.

The source code is distributed under BSD license, see the file License.txt
at the top-level directory.
*/

/*! @file
 * \brief Improves the computed solution to a system of linear equations and provides error bounds and backward error estimates
 *
 * <pre>
 * -- Distributed SuperLU routine (version 9.0) --
 * Lawrence Berkeley National Lab, Univ. of California Berkeley.
 * March 15, 2003
 *
 * Last modified:
 * December 31, 2015
 * </pre>
 */

#include <math.h>
#include "superlu_zdefs.h"

/*! \brief
 *
 * <pre>
 * Purpose
 * =======
 *
 * PZGSRFS improves the computed solution to a system of linear
 * equations and provides error bounds and backward error estimates
 * for the solution.
 *
 * Arguments
 * =========
 *
 * options (input) superlu_dist_options_t* (global)
 *         The structure defines the input parameters to control
 *         how the LU decomposition and triangular solve are performed.
 *
 * n      (input) int (global)
 *        The order of the system of linear equations.
 *
 * A      (input) SuperMatrix*
 *	  The original matrix A, or the scaled A if equilibration was done.
 *        A is also permuted into diag(R)*A*diag(C)*Pc'. The type of A can be:
 *        Stype = SLU_NR_loc; Dtype = SLU_Z; Mtype = SLU_GE.
 *
 * anorm  (input) double
 *        The norm of the original matrix A, or the scaled A if
 *        equilibration was done.
 *
 * LUstruct (input) zLUstruct_t*
 *        The distributed data structures storing L and U factors.
 *        The L and U factors are obtained from pdgstrf for
 *        the possibly scaled and permuted matrix A.
 *        See superlu_zdefs.h for the definition of 'zLUstruct_t'.
 *
 * ScalePermstruct (input) zScalePermstruct_t* (global)
 *         The data structure to store the scaling and permutation vectors
 *         describing the transformations performed to the matrix A.
 *
 * grid   (input) gridinfo_t*
 *        The 2D process mesh. It contains the MPI communicator, the number
 *        of process rows (NPROW), the number of process columns (NPCOL),
 *        and my process rank. It is an input argument to all the
 *        parallel routines.
 *        Grid can be initialized by subroutine SUPERLU_GRIDINIT.
 *        See superlu_defs.h for the definition of 'gridinfo_t'.
 *
 * B      (input) doublecomplex* (local)
 *        The m_loc-by-NRHS right-hand side matrix of the possibly
 *        equilibrated system. That is, B may be overwritten by diag(R)*B.
 *
 * ldb    (input) int (local)
 *        Leading dimension of matrix B.
 *
 * X      (input/output) doublecomplex* (local)
 *        On entry, the solution matrix Y, as computed by PZGSTRS, of the
 *            transformed system A1*Y = Pc*Pr*B. where
 *            A1 = Pc*Pr*diag(R)*A*diag(C)*Pc' and Y = Pc*diag(C)^(-1)*X.
 *        On exit, the improved solution matrix Y.
 *
 *        In order to obtain the solution X to the original system,
 *        Y should be permutated by Pc^T, and premultiplied by diag(C)
 *        if DiagScale = COL or BOTH.
 *        This must be done after this routine is called.
 *
 * ldx    (input) int (local)
 *        Leading dimension of matrix X.
 *
 * nrhs   (input) int
 *        Number of right-hand sides.
 *
 * SOLVEstruct (output) zSOLVEstruct_t* (global)
 *        Contains the information for the communication during the
 *        solution phase.
 *
 * berr   (output) double*, dimension (nrhs)
 *         The componentwise relative backward error of each solution
 *         vector X(j) (i.e., the smallest relative change in
 *         any element of A or B that makes X(j) an exact solution).
 *
 * stat   (output) SuperLUStat_t*
 *        Record the statistics about the refinement steps.
 *        See util.h for the definition of SuperLUStat_t.
 *
 * info   (output) int*
 *        = 0: successful exit
 *        < 0: if info = -i, the i-th argument had an illegal value
 *
 * Internal Parameters
 * ===================
 *
 * ITMAX is the maximum number of steps of iterative refinement.
 * </pre>
 */
void
pzgsrfs(superlu_dist_options_t *options, int_t n,
        SuperMatrix *A, double anorm, zLUstruct_t *LUstruct,
	zScalePermstruct_t *ScalePermstruct, gridinfo_t *grid,
	doublecomplex *B, int_t ldb, doublecomplex *X, int_t ldx, int nrhs,
	zSOLVEstruct_t *SOLVEstruct,
	double *berr, SuperLUStat_t *stat, int *info)
{
#define ITMAX 20

    doublecomplex *ax, *R, *dx, *temp, *work, *B_col, *X_col;
    double *rtemp;
    int_t count, i, j, lwork, nz;
    int   iam;
    double eps, lstres;
    double s, safmin, safe1, safe2;

    /* Data structures used by matrix-vector multiply routine. */
    pzgsmv_comm_t *gsmv_comm = SOLVEstruct->gsmv_comm;
    NRformat_loc *Astore;
    int_t        m_loc, fst_row;


    /* Initialization. */
    Astore = (NRformat_loc *) A->Store;
    m_loc = Astore->m_loc;
    fst_row = Astore->fst_row;
    iam = grid->iam;

    /* Test the input parameters. */
    *info = 0;
    if ( n < 0 ) *info = -1;
    else if ( A->nrow != A->ncol || A->nrow < 0 || A->Stype != SLU_NR_loc
	      || A->Dtype != SLU_Z || A->Mtype != SLU_GE )
	*info = -2;
    else if ( ldb < SUPERLU_MAX(0, m_loc) ) *info = -10;
    else if ( ldx < SUPERLU_MAX(0, m_loc) ) *info = -12;
    else if ( nrhs < 0 ) *info = -13;
    if (*info != 0) {
	i = -(*info);
	pxerr_dist("PZGSRFS", grid, i);
	return;
    }

    /* Quick return if possible. */
    if ( n == 0 || nrhs == 0 ) {
	return;
    }


#if ( DEBUGlevel>=1 )
    CHECK_MALLOC(iam, "Enter pzgsrfs()");
#endif

    lwork = 2 * m_loc;  /* For ax/R/dx and temp */
    if ( !(work = doublecomplexMalloc_dist(lwork)) )
	ABORT("Malloc fails for work[]");
    ax = R = dx = work;
    temp = ax + m_loc;
    rtemp = (double *) temp;

    /* NZ = maximum number of nonzero elements in each row of A, plus 1 */
    nz     = A->ncol + 1;
    eps    = dmach_dist("Epsilon");
    safmin = dmach_dist("Safe minimum");

    /* Set SAFE1 essentially to be the underflow threshold times the
       number of additions in each row. */
    safe1  = nz * safmin;
    safe2  = safe1 / eps;

#if ( DEBUGlevel>=1 )
    if ( !iam ) printf(".. eps = %e\tanorm = %e\tsafe1 = %e\tsafe2 = %e\n",
		       eps, anorm, safe1, safe2);
#endif

    /* Do for each right-hand side ... */
    for (j = 0; j < nrhs; ++j) {
	count = 0;
	lstres = 3.;
	B_col = &B[j*ldb];
	X_col = &X[j*ldx];

	while (1) { /* Loop until stopping criterion is satisfied. */

	    /* Compute residual R = B - op(A) * X,
	       where op(A) = A, A**T, or A**H, depending on TRANS. */

	    /* Matrix-vector multiply. */
	    pzgsmv(0, A, grid, gsmv_comm, X_col, ax);

	    /* Compute residual, stored in R[]. */
	    for (i = 0; i < m_loc; ++i) z_sub(&R[i], &B_col[i], &ax[i]);

	    /* Compute abs(op(A))*abs(X) + abs(B), stored in temp[]. */
	    pzgsmv(1, A, grid, gsmv_comm, X_col, temp);
            /* NOTE: rtemp is aliased to temp */
	    for (i = 0; i < m_loc; ++i) rtemp[i] += slud_z_abs1(&B_col[i]);

	    s = 0.0;
	    for (i = 0; i < m_loc; ++i) {
		if ( rtemp[i] > safe2 ) {
		    s = SUPERLU_MAX(s, slud_z_abs1(&R[i]) / rtemp[i]);
		} else if ( rtemp[i] != 0.0 ) {
		    s = SUPERLU_MAX(s, (safe1 + slud_z_abs1(&R[i])) / rtemp[i]);
                }
                /* If temp[i] is exactly 0.0 (computed by PxGSMV), then
                   we know the true residual also must be exactly 0.0. */
	    }
	    MPI_Allreduce( &s, &berr[j], 1, MPI_DOUBLE, MPI_MAX, grid->comm );

#if ( PRNTlevel>= 1 )
	    if ( !iam )
		printf("(%2d) .. Step " IFMT ": berr[j] = %e\n", iam, count, berr[j]);
#endif
	    if ( berr[j] > eps && berr[j] * 2 <= lstres && count < ITMAX ) {
		/* Compute new dx. */
		pzgstrs(options, n, LUstruct, ScalePermstruct, grid,
			dx, m_loc, fst_row, m_loc, 1,
			SOLVEstruct, stat, info);

		/* Update solution. */
		for (i = 0; i < m_loc; ++i)
                    z_add(&X_col[i], &X_col[i], &dx[i]);

		lstres = berr[j];
		++count;
	    } else {
		break;
	    }
	} /* end while */

	stat->RefineSteps = count;

    } /* for j ... */

    /* Deallocate storage. */
    SUPERLU_FREE(work);

#if ( DEBUGlevel>=1 )
    CHECK_MALLOC(iam, "Exit pzgsrfs()");
#endif

} /* PZGSRFS */




/*! \brief
 *
 * <pre>
 * Purpose
 * =======
 *
 * PZGSRFS3D improves the computed solution to a system of linear
 * equations and provides error bounds and backward error estimates
 * for the solution.
 *
 * Arguments
 * =========
 *
 * options (input) superlu_dist_options_t* (global)
 *         The structure defines the input parameters to control
 *         how the LU decomposition and triangular solve are performed.
 *
 * n      (input) int (global)
 *        The order of the system of linear equations.
 *
 * A      (input) SuperMatrix*
 *	  The original matrix A, or the scaled A if equilibration was done.
 *        A is also permuted into diag(R)*A*diag(C)*Pc'. The type of A can be:
 *        Stype = SLU_NR_loc; Dtype = SLU_Z; Mtype = SLU_GE.
 *
 * anorm  (input) double
 *        The norm of the original matrix A, or the scaled A if
 *        equilibration was done.
 *
 * LUstruct (input) zLUstruct_t*
 *        The distributed data structures storing L and U factors.
 *        The L and U factors are obtained from pdgstrf for
 *        the possibly scaled and permuted matrix A.
 *        See superlu_zdefs.h for the definition of 'zLUstruct_t'.
 *
 * ScalePermstruct (input) zScalePermstruct_t* (global)
 *         The data structure to store the scaling and permutation vectors
 *         describing the transformations performed to the matrix A.
 *
 * grid   (input) gridinfo_t*
 *        The 2D process mesh. It contains the MPI communicator, the number
 *        of process rows (NPROW), the number of process columns (NPCOL),
 *        and my process rank. It is an input argument to all the
 *        parallel routines.
 *        Grid can be initialized by subroutine SUPERLU_GRIDINIT.
 *        See superlu_defs.h for the definition of 'gridinfo_t'.
 *
 * B      (input) doublecomplex* (local)
 *        The m_loc-by-NRHS right-hand side matrix of the possibly
 *        equilibrated system. That is, B may be overwritten by diag(R)*B.
 *
 * ldb    (input) int (local)
 *        Leading dimension of matrix B.
 *
 * X      (input/output) doublecomplex* (local)
 *        On entry, the solution matrix Y, as computed by PZGSTRS, of the
 *            transformed system A1*Y = Pc*Pr*B. where
 *            A1 = Pc*Pr*diag(R)*A*diag(C)*Pc' and Y = Pc*diag(C)^(-1)*X.
 *        On exit, the improved solution matrix Y.
 *
 *        In order to obtain the solution X to the original system,
 *        Y should be permutated by Pc^T, and premultiplied by diag(C)
 *        if DiagScale = COL or BOTH.
 *        This must be done after this routine is called.
 *
 * ldx    (input) int (local)
 *        Leading dimension of matrix X.
 *
 * nrhs   (input) int
 *        Number of right-hand sides.
 *
 * SOLVEstruct (output) zSOLVEstruct_t* (global)
 *        Contains the information for the communication during the
 *        solution phase.
 *
 * berr   (output) double*, dimension (nrhs)
 *         The componentwise relative backward error of each solution
 *         vector X(j) (i.e., the smallest relative change in
 *         any element of A or B that makes X(j) an exact solution).
 *
 * stat   (output) SuperLUStat_t*
 *        Record the statistics about the refinement steps.
 *        See util.h for the definition of SuperLUStat_t.
 *
 * info   (output) int*
 *        = 0: successful exit
 *        < 0: if info = -i, the i-th argument had an illegal value
 *
 * Internal Parameters
 * ===================
 *
 * ITMAX is the maximum number of steps of iterative refinement.
 * </pre>
 */
void
pzgsrfs3d(superlu_dist_options_t *options, int_t n,
        SuperMatrix *A, double anorm, zLUstruct_t *LUstruct,
	zScalePermstruct_t *ScalePermstruct, gridinfo3d_t *grid3d,
	ztrf3Dpartition_t*  trf3Dpartition, doublecomplex *B, int_t ldb, doublecomplex *X, int_t ldx, int nrhs,
	zSOLVEstruct_t *SOLVEstruct,
	double *berr, SuperLUStat_t *stat, int *info)
{
#define ITMAX 20

    gridinfo_t* grid = &(grid3d->grid2d);
    doublecomplex *ax, *R, *dx, *temp, *work, *B_col, *X_col;
    double *rtemp;
    int_t count, i, j, lwork, nz;
    int   iam;
    double eps, lstres;
    double s, safmin, safe1, safe2;

    /* Data structures used by matrix-vector multiply routine. */
    pzgsmv_comm_t *gsmv_comm = SOLVEstruct->gsmv_comm;
    NRformat_loc *Astore;
    int_t        m_loc, fst_row;


    /* Initialization. */
    Astore = (NRformat_loc *) A->Store;
    m_loc = Astore->m_loc;
    fst_row = Astore->fst_row;
    iam = grid->iam;

    /* Test the input parameters. */
    *info = 0;
    if ( n < 0 ) *info = -1;
    else if ( A->nrow != A->ncol || A->nrow < 0 || A->Stype != SLU_NR_loc
	      || A->Dtype != SLU_Z || A->Mtype != SLU_GE )
	*info = -2;
    else if ( ldb < SUPERLU_MAX(0, m_loc) ) *info = -10;
    else if ( ldx < SUPERLU_MAX(0, m_loc) ) *info = -12;
    else if ( nrhs < 0 ) *info = -13;
    if (*info != 0) {
	i = -(*info);
	pxerr_dist("PZGSRFS3D", grid, i);
	return;
    }

    /* Quick return if possible. */
    if ( n == 0 || nrhs == 0 ) {
	return;
    }


#if ( DEBUGlevel>=1 )
    CHECK_MALLOC(iam, "Enter pzgsrfs3D()");
#endif

    lwork = 2 * m_loc;  /* For ax/R/dx and temp */
    if ( !(work = doublecomplexMalloc_dist(lwork)) )
	ABORT("Malloc fails for work[]");
    ax = R = dx = work;
    temp = ax + m_loc;
    rtemp = (double *) temp;

    /* NZ = maximum number of nonzero elements in each row of A, plus 1 */
    nz     = A->ncol + 1;
    eps    = dmach_dist("Epsilon");
    safmin = dmach_dist("Safe minimum");

    /* Set SAFE1 essentially to be the underflow threshold times the
       number of additions in each row. */
    safe1  = nz * safmin;
    safe2  = safe1 / eps;

#if ( DEBUGlevel>=1 )
    if ( !iam ) printf(".. eps = %e\tanorm = %e\tsafe1 = %e\tsafe2 = %e\n",
		       eps, anorm, safe1, safe2);
#endif

    /* Do for each right-hand side ... */
    for (j = 0; j < nrhs; ++j) {
	count = 0;
	lstres = 3.;
	B_col = &B[j*ldb];
	X_col = &X[j*ldx];

	while (1) { /* Loop until stopping criterion is satisfied. */

	    /* Compute residual R = B - op(A) * X,
	       where op(A) = A, A**T, or A**H, depending on TRANS. */

if (!grid3d->zscp.Iam){
	    /* Matrix-vector multiply. */
	    pzgsmv(0, A, grid, gsmv_comm, X_col, ax);

	    /* Compute residual, stored in R[]. */
	    for (i = 0; i < m_loc; ++i) z_sub(&R[i], &B_col[i], &ax[i]);

	    /* Compute abs(op(A))*abs(X) + abs(B), stored in temp[]. */
	    pzgsmv(1, A, grid, gsmv_comm, X_col, temp);
            /* NOTE: rtemp is aliased to temp */
	    for (i = 0; i < m_loc; ++i) rtemp[i] += slud_z_abs1(&B_col[i]);

	    s = 0.0;
	    for (i = 0; i < m_loc; ++i) {
		if ( rtemp[i] > safe2 ) {
		    s = SUPERLU_MAX(s, slud_z_abs1(&R[i]) / rtemp[i]);
		} else if ( rtemp[i] != 0.0 ) {
		    s = SUPERLU_MAX(s, (safe1 + slud_z_abs1(&R[i])) / rtemp[i]);
                }
                /* If temp[i] is exactly 0.0 (computed by PxGSMV), then
                   we know the true residual also must be exactly 0.0. */
	    }
	    MPI_Allreduce( &s, &berr[j], 1, MPI_DOUBLE, MPI_MAX, grid->comm );
        }
        MPI_Bcast( &berr[j], 1, MPI_DOUBLE, 0,  grid3d->zscp.comm);

#if ( PRNTlevel>= 0 )
	    if ( !grid3d->iam )
		printf("(%2d) .. Step " IFMT ": berr[j] = %e\n", iam, count, berr[j]);
#endif
	    if ( berr[j] > eps && berr[j] * 2 <= lstres && count < ITMAX ) {
		/* Compute new dx. */
        if (get_new3dsolve()){
            pzgstrs3d_newsolve (options, n, LUstruct,ScalePermstruct, trf3Dpartition, grid3d, dx,
            m_loc, fst_row, m_loc, 1,SOLVEstruct, stat, info);
        }else{
            pzgstrs3d (options, n, LUstruct,ScalePermstruct, trf3Dpartition, grid3d, dx,
            m_loc, fst_row, m_loc, 1,SOLVEstruct, stat, info);
        }

		/* Update solution. */
        if (!grid3d->zscp.Iam)
		for (i = 0; i < m_loc; ++i)
                    z_add(&X_col[i], &X_col[i], &dx[i]);

		lstres = berr[j];
		++count;
	    } else {
		break;
	    }
	} /* end while */

	stat->RefineSteps = count;

    } /* for j ... */

    /* Deallocate storage. */
    SUPERLU_FREE(work);

#if ( DEBUGlevel>=1 )
    CHECK_MALLOC(iam, "Exit pzgsrfs3d()");
#endif

} /* PZGSRFS3D */

