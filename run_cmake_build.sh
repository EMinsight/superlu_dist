#!/bin/bash

THISHOST=`hostname -s`
echo "host: $THISHOST"

if [ "$THISHOST" == "ssg1" ]
then
   rm -fr ssg1-build; mkdir ssg1-build; cd ssg1-build;
    export PARMETIS_ROOT=~/lib/static/parmetis-4.0.3 
#    rm -fr int64-build; mkdir int64-build; cd int64-build;
#    export PARMETIS_ROOT=~/lib/static/64-bit/parmetis-4.0.3 
    export PARMETIS_BUILD_DIR=${PARMETIS_ROOT}/build/Linux-x86_64
    echo "ParMetis root: $PARMETIS_ROOT"
    cmake .. \
	-DTPL_PARMETIS_INCLUDE_DIRS="${PARMETIS_ROOT}/include;${PARMETIS_ROOT}/metis/include" \
	-DTPL_PARMETIS_LIBRARIES="${PARMETIS_BUILD_DIR}/libparmetis/libparmetis.a;${PARMETIS_BUILD_DIR}/libmetis/libmetis.a" \
	-DTPL_COMBBLAS_INCLUDE_DIRS="${COMBBLAS_ROOT}/_install/include;${COMBBLAS_R\
OOT}/Applications/BipartiteMatchings" \
	-DTPL_COMBBLAS_LIBRARIES="${COMBBLAS_BUILD_DIR}/libCombBLAS.a" \
	-DCMAKE_C_FLAGS="-std=c99 -g -O3 -DPRNTlevel=0 -DDEBUGlevel=0" \
	-DCMAKE_C_COMPILER=mpicc \
	-DCMAKE_CXX_COMPILER=mpicxx \
	-DTPL_ENABLE_BLASLIB=OFF \
	-DTPL_ENABLE_COMBBLASLIB=OFF \
	-DTPL_ENABLE_LAPACKLIB=OFF \
	-DBUILD_SHARED_LIBS=OFF \
	-DCMAKE_INSTALL_PREFIX=.
fi
#	-DXSDK_INDEX_SIZE=64 \
#	-DCMAKE_INSTALL_PREFIX=install
#   -DTPL_ENABLE_PARMETISLIB=OFF
#    -DCMAKE_CXX_FLAGS="-std=c++11" \


if [ "$NERSC_HOST" == "cori" ]
then
#    rm -fr 64-build; mkdir 64-build; cd 64-build;
#    export PARMETIS_ROOT=~/Cori/lib/parmetis-4.0.3-64
    rm -fr cori-build; mkdir cori-build; cd cori-build;
    export PARMETIS_ROOT=~/Cori/lib/parmetis-4.0.3
#    export PARMETIS_BUILD_DIR=${PARMETIS_ROOT}/shared-build
    export PARMETIS_BUILD_DIR=${PARMETIS_ROOT}/build/Linux-x86_64
    cmake .. \
    -DTPL_PARMETIS_INCLUDE_DIRS="${PARMETIS_ROOT}/include;${PARMETIS_ROOT}/metis/include" \
    -DTPL_PARMETIS_LIBRARIES="${PARMETIS_BUILD_DIR}/libparmetis/libparmetis.a;${PARMETIS_BUILD_DIR}/libmetis/libmetis.a" \
    -DTPL_ENABLE_BLASLIB=OFF \
    -DTPL_BLAS_LIBRARIES="-mkl" \
    -DCMAKE_Fortran_COMPILER=ftn \
    -DCMAKE_C_FLAGS="-std=c99 -fPIC -DPRNTlevel=1 -g -O3" \
    -DCMAKE_INSTALL_PREFIX=. \
#    -DXSDK_INDEX_SIZE=64
#    -DCMAKE_EXE_LINKER_FLAGS="-shared" \
fi


# make VERBOSE=1
# make test
