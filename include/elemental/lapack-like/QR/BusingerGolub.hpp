/*
   Copyright (c) 2009-2013, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
#pragma once
#ifndef LAPACK_QR_BUSINGERGOLUB_HPP
#define LAPACK_QR_BUSINGERGOLUB_HPP

#include "elemental/blas-like/level2/Gemv.hpp"
#include "elemental/blas-like/level2/Ger.hpp"
#include "elemental/lapack-like/Reflector.hpp"
#include "elemental/matrices/Zeros.hpp"

#include <algorithm>

namespace elem {
namespace qr {

template<typename F>
inline BASE(F)
ColumnNorms( const Matrix<F>& A, std::vector<BASE(F)>& norms )
{
#ifndef RELEASE
    CallStackEntry entry("qr::ColumnNorms");
#endif
    typedef BASE(F) Real;
    const int m = A.Height();
    const int n = A.Width();
    Real maxNorm = 0;
    norms.resize( n );
    for( int j=0; j<n; ++j )
    {
        norms[j] = blas::Nrm2( m, A.LockedBuffer(0,j), 1 );
        maxNorm = std::max( maxNorm, norms[j] );
    }
    return maxNorm;
}

template<typename Real>
inline mpi::ValueInt<Real>
FindPivot( const std::vector<Real>& norms, int col )
{
#ifndef RELEASE
    CallStackEntry entry("qr::FindPivot");
#endif
    const int n = norms.size();
    const Real* maxNorm = std::max_element( &norms[col], &norms[0]+n );
    mpi::ValueInt<Real> pivot;
    pivot.value = *maxNorm;
    pivot.index = maxNorm - &norms[0];
    return pivot;
}

template<typename Real>
inline void
BusingerGolub
( Matrix<Real>& A, Matrix<int>& p, int maxSteps, Real tol, 
  bool alwaysRecompute=false )
{
#ifndef RELEASE
    CallStackEntry entry("qr::BusingerGolub");
    if( maxSteps > std::min(A.Height(),A.Width()) )
        throw std::logic_error("Too many steps requested");
    if( p.Viewing() && (p.Height() != maxSteps || p.Width() != 1) )
        throw std::logic_error
        ("p must be a vector of the same height as the number of steps");
#endif
    if( !p.Viewing() )
        p.ResizeTo( maxSteps, 1 );

    Matrix<Real>
        ATL, ATR,  A00, a01,     A02,  aLeftCol, ARightPan,
        ABL, ABR,  a10, alpha11, a12,
                   A20, a21,     A22;

    Matrix<Real> z;

    const int m = A.Height();
    const int n = A.Width();
    std::vector<Real> swapBuf( m );

    // Initialize two copies of the column norms, one will be consistently
    // updated, but the original copy will be kept to determine when the 
    // updated quantities are no longer accurate.
    std::vector<Real> origNorms;
    const Real maxOrigNorm = ColumnNorms( A, origNorms );
    std::vector<Real> norms = origNorms;
    const Real updateTol = Sqrt(lapack::MachineEpsilon<Real>());

    PartitionDownLeftDiagonal
    ( A, ATL, ATR,
         ABL, ABR, 0 );
    for( int col=0; col<maxSteps; ++col )
    {
        RepartitionDownDiagonal
        ( ATL, /**/ ATR,  A00, /**/ a01,     A02,
         /*************/ /**********************/
               /**/       a10, /**/ alpha11, a12,
          ABL, /**/ ABR,  A20, /**/ a21,     A22, 1 );

        View2x1( aLeftCol, alpha11,
                           a21 );

        View2x1( ARightPan, a12,
                            A22 );

        //--------------------------------------------------------------------//
        // Find the next column pivot
        const mpi::ValueInt<Real> pivot = FindPivot( norms, col );
        if( pivot.value <= tol*maxOrigNorm )
        {
            p.ResizeTo( col, 1 );
            break;
        }
        p.Set( col, 0, pivot.index );

        // Perform the swap
        if( col != pivot.index )
        {
            MemSwap( A.Buffer(0,col), A.Buffer(0,pivot.index), &swapBuf[0], m );
            norms[pivot.index] = norms[col];
            origNorms[pivot.index] = origNorms[col];
        }

        // Compute and apply the Householder reflector for this column
        const Real tau = Reflector( alpha11, a21 );
        const Real alpha = alpha11.Get(0,0);
        alpha11.Set(0,0,1);
        Zeros( z, ARightPan.Width(), 1 );
        Gemv( TRANSPOSE, Real(1), ARightPan, aLeftCol, Real(0), z );
        Ger( -tau, aLeftCol, z, ARightPan );
        alpha11.Set(0,0,alpha);

        // Update the column norm estimates in the same manner as LAWN 176
        for( int k=0; k<a12.Width(); ++k )
        {
            const int j = k + col+1;    
            if( norms[j] != Real(0) )
            {
                Real gamma = Abs(a12.Get(0,k)) / norms[j];
                gamma = std::max( Real(0), (Real(1)-gamma)*(Real(1)+gamma) );
                const Real ratio = norms[j] / origNorms[j];
                const Real phi = gamma*(ratio*ratio);
                if( phi <= updateTol || alwaysRecompute )
                {
                    norms[j] = blas::Nrm2( m-(col+1), A.Buffer(col+1,j), 1 );
                    origNorms[j] = norms[j];
                }
                else
                    norms[j] *= Sqrt(gamma);
            }
        }
        //--------------------------------------------------------------------//

        SlidePartitionDownDiagonal
        ( ATL, /**/ ATR,  A00, a01,     /**/ A02,
               /**/       a10, alpha11, /**/ a12,
         /*************/ /**********************/
          ABL, /**/ ABR,  A20, a21,     /**/ A22 );
    }
}

template<typename Real>
inline void
BusingerGolub
( Matrix<Real>& A, Matrix<int>& p, int numSteps, bool alwaysRecompute=false )
{
#ifndef RELEASE
    CallStackEntry entry("qr::BusingerGolub");
#endif
    // Use a tolerance of -1 so that we guarantee numSteps iterations
    BusingerGolub( A, p, numSteps, Real(-1), alwaysRecompute );
}

template<typename Real>
inline void
BusingerGolub( Matrix<Real>& A, Matrix<int>& p, bool alwaysRecompute=false )
{
#ifndef RELEASE
    CallStackEntry entry("qr::BusingerGolub");
#endif
    const int numSteps = std::min(A.Height(),A.Width());
    BusingerGolub( A, p, numSteps, alwaysRecompute );
}

template<typename Real> 
inline void
BusingerGolub
( Matrix<Complex<Real> >& A,
  Matrix<Complex<Real> >& t,
  Matrix<int>& p,
  int maxSteps,
  Real tol,
  bool alwaysRecompute=false )
{
#ifndef RELEASE
    CallStackEntry entry("qr::BusingerGolub");
    if( maxSteps > std::min(A.Height(),A.Width()) )
        throw std::logic_error("Too many steps requested");
    if( t.Viewing() && (t.Height() != maxSteps || t.Width() != 1) )
        throw std::logic_error
        ("t must be a vector of the same height as the number of steps");
    if( p.Viewing() && (p.Height() != maxSteps || p.Width() != 1) )
        throw std::logic_error
        ("p must be a vector of the same height as the number of steps");
#endif
    if( !p.Viewing() )
        p.ResizeTo( maxSteps, 1 );
    if( !t.Viewing() )
        t.ResizeTo( maxSteps, 1 );

    typedef Complex<Real> C;

    Matrix<C>
        ATL, ATR,  A00, a01,     A02,  aLeftCol, ARightPan,
        ABL, ABR,  a10, alpha11, a12,
                   A20, a21,     A22;
    Matrix<C> z;

    const int m = A.Height();
    const int n = A.Width();
    std::vector<C> swapBuf( m );

    // Initialize two copies of the column norms, one will be consistently
    // updated, but the original copy will be kept to determine when the 
    // updated quantities are no longer accurate.
    std::vector<Real> origNorms;
    const Real maxOrigNorm = ColumnNorms( A, origNorms );
    std::vector<Real> norms = origNorms;
    const Real updateTol = Sqrt(lapack::MachineEpsilon<Real>());

    PartitionDownLeftDiagonal
    ( A, ATL, ATR,
         ABL, ABR, 0 );
    for( int col=0; col<maxSteps; ++col )
    {
        RepartitionDownDiagonal
        ( ATL, /**/ ATR,  A00, /**/ a01,     A02,
         /*************/ /**********************/
               /**/       a10, /**/ alpha11, a12,
          ABL, /**/ ABR,  A20, /**/ a21,     A22, 1 );

        View2x1( aLeftCol, alpha11,
                           a21 );

        View2x1( ARightPan, a12,
                            A22 );

        //--------------------------------------------------------------------//
        // Find the next column pivot
        const mpi::ValueInt<Real> pivot = FindPivot( norms, col );
        if( pivot.value <= tol*maxOrigNorm )
        {
            p.ResizeTo( col, 1 );
            t.ResizeTo( col, 1 );
            break;
        }
        p.Set( col, 0, pivot.index );
 
        // Perform the swap
        if( col != pivot.index )
        {
            MemSwap( A.Buffer(0,col), A.Buffer(0,pivot.index), &swapBuf[0], m );
            norms[pivot.index] = norms[col];
            origNorms[pivot.index] = origNorms[col];
        }

        // Compute and apply the Householder reflector for this column
        const C tau = Reflector( alpha11, a21 );
        t.Set( col, 0, tau );
        const C alpha = alpha11.Get(0,0);
        alpha11.Set(0,0,1);
        Zeros( z, ARightPan.Width(), 1 );
        Gemv( ADJOINT, C(1), ARightPan, aLeftCol, C(0), z );
        Ger( -Conj(tau), aLeftCol, z, ARightPan );
        alpha11.Set(0,0,alpha);

        // Update the column norm estimates in the same manner as LAWN 176
        for( int k=0; k<a12.Width(); ++k )
        {
            const int j = k + col+1;    
            if( norms[j] != Real(0) )
            {
                Real gamma = Abs(a12.Get(0,k)) / norms[j];
                gamma = std::max( Real(0), (Real(1)-gamma)*(Real(1)+gamma) );
                const Real ratio = norms[j] / origNorms[j];
                const Real phi = gamma*(ratio*ratio);
                if( phi <= updateTol || alwaysRecompute )
                {
                    norms[j] = blas::Nrm2( m-(col+1), A.Buffer(col+1,j), 1 );
                    origNorms[j] = norms[j];
                }
                else
                    norms[j] *= Sqrt(gamma);
            }
        }
        //--------------------------------------------------------------------//

        SlidePartitionDownDiagonal
        ( ATL, /**/ ATR,  A00, a01,     /**/ A02,
               /**/       a10, alpha11, /**/ a12,
         /*************/ /**********************/
          ABL, /**/ ABR,  A20, a21,     /**/ A22 );
    }
}

template<typename Real> 
inline void
BusingerGolub
( Matrix<Complex<Real> >& A,
  Matrix<Complex<Real> >& t,
  Matrix<int>& p,
  int numSteps,
  bool alwaysRecompute=false )
{
#ifndef RELEASE
    CallStackEntry entry("qr::BusingerGolub");
#endif
    // Use a tolerance of -1 so that we always perform numSteps iterations
    BusingerGolub( A, t, p, numSteps, Real(-1), alwaysRecompute );
}

template<typename Real> 
inline void
BusingerGolub
( Matrix<Complex<Real> >& A,
  Matrix<Complex<Real> >& t,
  Matrix<int>& p,
  bool alwaysRecompute=false )
{
#ifndef RELEASE
    CallStackEntry entry("qr::BusingerGolub");
#endif
    const int numSteps = std::min(A.Height(),A.Width());
    BusingerGolub( A, t, p, numSteps, alwaysRecompute );
}

template<typename F>
inline mpi::ValueInt<BASE(F)>
FindColumnPivot
( const DistMatrix<F>& A, const std::vector<BASE(F)>& norms, int col )
{
#ifndef RELEASE
    CallStackEntry entry("qr::FindColumnPivot");
#endif
    typedef BASE(F) Real;
    const int rowShift = A.RowShift();
    const int rowStride = A.RowStride();
    const int localColsBefore = Length( col, rowShift, rowStride );
    const mpi::ValueInt<Real> localPivot = FindPivot( norms, localColsBefore );
    mpi::ValueInt<Real> pivot;
    pivot.value = localPivot.value;
    pivot.index = rowShift + localPivot.index*rowStride;
    mpi::AllReduce( &pivot, 1, mpi::MAXLOC, A.Grid().RowComm() );
    return pivot;
}

template<typename F>
inline BASE(F)
ColumnNorms( const DistMatrix<F>& A, std::vector<BASE(F)>& norms )
{
#ifndef RELEASE
    CallStackEntry entry("qr::ColumnNorms");
#endif
    typedef BASE(F) Real;
    const int localHeight = A.LocalHeight();
    const int localWidth = A.LocalWidth();
    mpi::Comm colComm = A.Grid().ColComm();
    mpi::Comm rowComm = A.Grid().RowComm();

    // Carefully perform the local portion of the computation
    std::vector<Real> localScales(localWidth,0), 
                      localScaledSquares(localWidth,1);
    for( int jLocal=0; jLocal<localWidth; ++jLocal )
    {
        for( int iLocal=0; iLocal<localHeight; ++iLocal )
        {
            const Real alphaAbs = Abs(A.GetLocal(iLocal,jLocal));    
            if( alphaAbs != 0 )
            {
                if( alphaAbs <= localScales[jLocal] )
                {
                    const Real relScale = alphaAbs/localScales[jLocal];
                    localScaledSquares[jLocal] += relScale*relScale;
                }
                else
                {
                    const Real relScale = localScales[jLocal]/alphaAbs;
                    localScaledSquares[jLocal] = 
                        localScaledSquares[jLocal]*relScale*relScale + 1;
                    localScales[jLocal] = alphaAbs;
                }
            }
        }
    }

    // Find the maximum relative scales 
    std::vector<Real> scales(localWidth);
    mpi::AllReduce
    ( &localScales[0], &scales[0], localWidth, mpi::MAX, colComm );

    // Equilibrate the local scaled sums to the maximum scale
    for( int jLocal=0; jLocal<localWidth; ++jLocal )
    {
        if( scales[jLocal] != 0 )
        {
            const Real relScale = localScales[jLocal]/scales[jLocal];
            localScaledSquares[jLocal] *= relScale*relScale;
        }
    }

    // Now sum the local contributions (can ignore results where scale is 0)
    std::vector<Real> scaledSquares(localWidth); 
    mpi::AllReduce
    ( &localScaledSquares[0], &scaledSquares[0], localWidth, 
      mpi::SUM, colComm );

    // Finish the computation
    Real maxLocalNorm = 0;
    norms.resize( localWidth );
    for( int jLocal=0; jLocal<localWidth; ++jLocal )
    {
        if( scales[jLocal] != 0 )
            norms[jLocal] = scales[jLocal]*Sqrt(scaledSquares[jLocal]);
        else
            norms[jLocal] = 0;
        maxLocalNorm = std::max( maxLocalNorm, norms[jLocal] );
    }
    Real maxNorm;
    mpi::AllReduce( &maxLocalNorm, &maxNorm, 1, mpi::MAX, rowComm );

    return maxNorm;
}

template<typename F>
inline void
ReplaceColumnNorms
( const DistMatrix<F>& A, std::vector<int>& inaccurateNorms, 
  std::vector<BASE(F)>& norms, std::vector<BASE(F)>& origNorms )
{
#ifndef RELEASE
    CallStackEntry entry("qr::ReplaceColumnNorms");
#endif
    typedef BASE(F) Real;
    const int localHeight = A.LocalHeight();
    const int numInaccurate = inaccurateNorms.size();
    mpi::Comm colComm = A.Grid().ColComm();

    // Carefully perform the local portion of the computation
    std::vector<Real> localScales(numInaccurate,0), 
                      localScaledSquares(numInaccurate,1);
    for( int s=0; s<numInaccurate; ++s )
    {
        const int jLocal = inaccurateNorms[s];
        for( int iLocal=0; iLocal<localHeight; ++iLocal )
        {
            const Real alphaAbs = Abs(A.GetLocal(iLocal,jLocal));    
            if( alphaAbs != 0 )
            {
                if( alphaAbs <= localScales[s] )
                {
                    const Real relScale = alphaAbs/localScales[s];
                    localScaledSquares[s] += relScale*relScale;
                }
                else
                {
                    const Real relScale = localScales[s]/alphaAbs;
                    localScaledSquares[s] = 
                        localScaledSquares[s]*relScale*relScale + 1;
                    localScales[s] = alphaAbs;
                }
            }
        }
    }

    // Find the maximum relative scales 
    std::vector<Real> scales(numInaccurate);
    mpi::AllReduce
    ( &localScales[0], &scales[0], numInaccurate, mpi::MAX, colComm );

    // Equilibrate the local scaled sums to the maximum scale
    for( int s=0; s<numInaccurate; ++s )
    {
        if( scales[s] != 0 )
        {
            const Real relScale = localScales[s]/scales[s];
            localScaledSquares[s] *= relScale*relScale;
        }
    }

    // Now sum the local contributions (can ignore results where scale is 0)
    std::vector<Real> scaledSquares(numInaccurate); 
    mpi::AllReduce
    ( &localScaledSquares[0], &scaledSquares[0], numInaccurate,
      mpi::SUM, colComm );

    // Finish the computation
    for( int s=0; s<numInaccurate; ++s )
    {
        const int jLocal = inaccurateNorms[s];
        if( scales[s] != 0 )
            norms[jLocal] = scales[s]*Sqrt(scaledSquares[s]);
        else
            norms[jLocal] = 0;
        origNorms[jLocal] = norms[jLocal];
    }
}

template<typename Real>
inline void
BusingerGolub
( DistMatrix<Real>& A, DistMatrix<int,VR,STAR>& p, int maxSteps,
  Real tol, bool alwaysRecompute=false )
{
#ifndef RELEASE
    CallStackEntry entry("qr::BusingerGolub");
    if( maxSteps > std::min(A.Height(),A.Width()) )
        throw std::logic_error("Too many steps requested");
    if( p.Viewing() && (p.Height() != maxSteps || p.Width() != 1) )
        throw std::logic_error
        ("p must be a vector of the same height as the number of steps");
    if( A.Grid() != p.Grid() )
        throw std::logic_error("A and p must have the same grid");
#endif
    const Grid& g = A.Grid();
    if( !p.Viewing() )
        p.ResizeTo( maxSteps, 1 );

    DistMatrix<Real>
        ATL(g), ATR(g),  A00(g), a01(g),     A02(g),  aLeftCol(g), ARightPan(g),
        ABL(g), ABR(g),  a10(g), alpha11(g), a12(g),
                         A20(g), a21(g),     A22(g);

    DistMatrix<Real> z(g);

    const int m = A.Height();
    const int n = A.Width();
    const int mLocal = A.LocalHeight();
    const int nLocal = A.LocalWidth();
    const int rowAlign = A.RowAlignment();
    const int rowShift = A.RowShift();
    const int rowStride = A.RowStride();
    std::vector<Real> swapBuf( mLocal );

    // Initialize two copies of the column norms, one will be consistently
    // updated, but the original copy will be kept to determine when the 
    // updated quantities are no longer accurate.
    std::vector<Real> origNorms( nLocal );
    const Real maxOrigNorm = ColumnNorms( A, origNorms );
    std::vector<Real> norms = origNorms;
    const Real updateTol = Sqrt(lapack::MachineEpsilon<Real>());
    std::vector<int> inaccurateNorms;

    // Temporary distributions
    DistMatrix<Real,MC,STAR> aLeftCol_MC_STAR(g);
    DistMatrix<Real,MR,STAR> z_MR_STAR(g);
    DistMatrix<Real,STAR,MR> a12_STAR_MR(g);

    PartitionDownLeftDiagonal
    ( A, ATL, ATR,
         ABL, ABR, 0 );
    for( int col=0; col<maxSteps; ++col )
    {
        RepartitionDownDiagonal
        ( ATL, /**/ ATR,  A00, /**/ a01,     A02,
         /*************/ /**********************/
               /**/       a10, /**/ alpha11, a12,
          ABL, /**/ ABR,  A20, /**/ a21,     A22, 1 );

        View2x1( aLeftCol, alpha11,
                           a21 );

        View2x1( ARightPan, a12,
                            A22 );

        aLeftCol_MC_STAR.AlignWith( ARightPan );
        z_MR_STAR.AlignWith( ARightPan );
        //--------------------------------------------------------------------//
        // Find the next column pivot
        const mpi::ValueInt<Real> pivot = FindColumnPivot( A, norms, col );
        if( pivot.value <= tol*maxOrigNorm )
        {
            p.ResizeTo( col, 1 );
            break;
        }
        p.Set( col, 0, pivot.index );

        // Perform the swap
        const int colOwner = (col+rowAlign) % rowStride;
        const int pivotColOwner = (pivot.index+rowAlign) % rowStride;
        const bool myCol = ( g.Col() == colOwner );
        const bool myPivotCol = ( g.Col() == pivotColOwner );
        if( col != pivot.index )
        {
            if( myCol && myPivotCol )
            {
                const int colLocal = (col-rowShift) / rowStride;
                const int pivotColLocal = (pivot.index-rowShift) / rowStride;
                MemSwap
                ( A.Buffer(0,colLocal), A.Buffer(0,pivotColLocal),
                  &swapBuf[0], mLocal );
                norms[pivotColLocal] = norms[colLocal];
                origNorms[pivotColLocal] = origNorms[colLocal];
            }
            else if( myCol )
            {
                const int colLocal = (col-rowShift) / rowStride;
                mpi::SendRecv
                ( A.Buffer(0,colLocal), mLocal,
                  pivotColOwner, 0, pivotColOwner, 0, g.RowComm() );
                mpi::Send( &norms[colLocal], 1, pivotColOwner, 0, g.RowComm() );
            }
            else if( myPivotCol )
            {
                const int pivotColLocal = (pivot.index-rowShift) / rowStride;
                mpi::SendRecv
                ( A.Buffer(0,pivotColLocal), mLocal,
                  colOwner, 0, colOwner, 0, g.RowComm() );
                mpi::Recv( &norms[pivotColLocal], 1, colOwner, 0, g.RowComm() );
            }
        }

        // Compute the Householder reflector
        const Real tau = Reflector( alpha11, a21 );

        // Apply the Householder reflector
        const bool myDiagonalEntry = ( g.Row() == alpha11.ColAlignment() &&
                                       g.Col() == alpha11.RowAlignment() );
        Real alpha = 0;
        if( myDiagonalEntry )
        {
            alpha = alpha11.GetLocal(0,0);
            alpha11.SetLocal(0,0,1);
        }
        aLeftCol_MC_STAR = aLeftCol;
        Zeros( z_MR_STAR, ARightPan.Width(), 1 );
        LocalGemv
        ( TRANSPOSE, Real(1), ARightPan, aLeftCol_MC_STAR, Real(0), z_MR_STAR );
        z_MR_STAR.SumOverCol();
        Ger
        ( -tau,
          aLeftCol_MC_STAR.LockedMatrix(),
          z_MR_STAR.LockedMatrix(),
          ARightPan.Matrix() );
        if( myDiagonalEntry )
            alpha11.SetLocal(0,0,alpha);

        // Update the column norm estimates in the same manner as LAWN 176.
        // However, we do so in two steps in order to lower the communication
        // latency:
        //   1) Each process first computes which of its column norms are
        //      too inaccurate and need to be recomputed.
        //   2) Each process communicates within its process column in order
        //      to replace the inaccurate column norms.
        // Step 1: Perform all of the easy updates and mark inaccurate norms
        a12_STAR_MR = a12;
        const int a12LocalWidth = a12_STAR_MR.LocalWidth();
        const int a12RowShift = a12_STAR_MR.RowShift();
        inaccurateNorms.resize(0);
        for( int kLocal=0; kLocal<a12LocalWidth; ++kLocal )
        {
            const int k = a12RowShift + kLocal*rowStride;
            const int j = k + col+1;
            const int jLocal = (j-rowShift) / rowStride;
            if( norms[jLocal] != Real(0) )
            {
                const Real beta = Abs(a12_STAR_MR.GetLocal(0,kLocal));
                Real gamma = beta / norms[jLocal];
                gamma = std::max( Real(0), (Real(1)-gamma)*(Real(1)+gamma) );
                const Real ratio = norms[jLocal] / origNorms[jLocal];
                const Real phi = gamma*(ratio*ratio);
                if( phi <= updateTol || alwaysRecompute )
                    inaccurateNorms.push_back( jLocal );
                else
                    norms[jLocal] *= Sqrt(gamma);
            }
        }
        // Step 2: Compute the replacement norms and also reset origNorms
        ReplaceColumnNorms( A, inaccurateNorms, norms, origNorms );
        //--------------------------------------------------------------------//
        aLeftCol_MC_STAR.FreeAlignments();
        z_MR_STAR.FreeAlignments();

        SlidePartitionDownDiagonal
        ( ATL, /**/ ATR,  A00, a01,     /**/ A02,
               /**/       a10, alpha11, /**/ a12,
         /*************/ /**********************/
          ABL, /**/ ABR,  A20, a21,     /**/ A22 );
    }
}

template<typename Real>
inline void
BusingerGolub
( DistMatrix<Real>& A, DistMatrix<int,VR,STAR>& p, int numSteps,
  bool alwaysRecompute=false )
{
#ifndef RELEASE
    CallStackEntry entry("qr::BusingerGolub");
#endif
    // Use a tolerance of -1 so that we always perform numSteps iterations
    BusingerGolub( A, p, numSteps, Real(-1), alwaysRecompute );
}

template<typename Real>
inline void
BusingerGolub
( DistMatrix<Real>& A, DistMatrix<int,VR,STAR>& p, bool alwaysRecompute=false )
{
#ifndef RELEASE
    CallStackEntry entry("qr::BusingerGolub");
#endif
    const int numSteps = std::min(A.Height(),A.Width());
    BusingerGolub( A, p, numSteps, alwaysRecompute );
}

template<typename Real>
inline void
BusingerGolub
( DistMatrix<Complex<Real> >& A, 
  DistMatrix<Complex<Real>,MD,STAR>& t, 
  DistMatrix<int,VR,STAR>& p,
  int maxSteps, Real tol,
  bool alwaysRecompute=false )
{
#ifndef RELEASE
    CallStackEntry entry("qr::BusingerGolub");
    if( maxSteps > std::min(A.Height(),A.Width()) )
        throw std::logic_error("Too many steps requested");
    if( t.Viewing() && (t.Height() != maxSteps || t.Width() != 1) )
        throw std::logic_error
        ("t must be a vector of the same height as the number of steps");
    if( p.Viewing() && (p.Height() != maxSteps || p.Width() != 1) )
        throw std::logic_error
        ("p must be a vector of the same height as the number of steps");
    if( A.Grid() != p.Grid() || A.Grid() != t.Grid() )
        throw std::logic_error("A, t, and p must have the same grid");
#endif
    typedef Complex<Real> C;
    const Grid& g = A.Grid();
    if( !t.Viewing() )
        t.ResizeTo( maxSteps, 1 );
    if( !p.Viewing() )
        p.ResizeTo( maxSteps, 1 );

    DistMatrix<C>
        ATL(g), ATR(g),  A00(g), a01(g),     A02(g),  aLeftCol(g), ARightPan(g),
        ABL(g), ABR(g),  a10(g), alpha11(g), a12(g),
                         A20(g), a21(g),     A22(g);
    DistMatrix<C> z(g);

    const int m = A.Height();
    const int n = A.Width();
    const int mLocal = A.LocalHeight();
    const int nLocal = A.LocalWidth();
    const int rowAlign = A.RowAlignment();
    const int rowShift = A.RowShift();
    const int rowStride = A.RowStride();
    std::vector<C> swapBuf( mLocal );

    // Initialize two copies of the column norms, one will be consistently
    // updated, but the original copy will be kept to determine when the 
    // updated quantities are no longer accurate.
    std::vector<Real> origNorms( nLocal );
    const Real maxOrigNorm = ColumnNorms( A, origNorms );
    std::vector<Real> norms = origNorms;
    const Real updateTol = Sqrt(lapack::MachineEpsilon<Real>());
    std::vector<int> inaccurateNorms;

    // Temporary distributions
    DistMatrix<C,MC,STAR> aLeftCol_MC_STAR(g);
    DistMatrix<C,MR,STAR> z_MR_STAR(g);
    DistMatrix<C,STAR,MR> a12_STAR_MR(g);

    PartitionDownLeftDiagonal
    ( A, ATL, ATR,
         ABL, ABR, 0 );
    for( int col=0; col<maxSteps; ++col )
    {
        RepartitionDownDiagonal
        ( ATL, /**/ ATR,  A00, /**/ a01,     A02,
         /*************/ /**********************/
               /**/       a10, /**/ alpha11, a12,
          ABL, /**/ ABR,  A20, /**/ a21,     A22, 1 );

        View2x1( aLeftCol, alpha11,
                           a21 );

        View2x1( ARightPan, a12,
                            A22 );

        aLeftCol_MC_STAR.AlignWith( ARightPan );
        z_MR_STAR.AlignWith( ARightPan );
        //--------------------------------------------------------------------//
        // Find the next column pivot
        const mpi::ValueInt<Real> pivot = FindColumnPivot( A, norms, col );
        if( pivot.value <= tol*maxOrigNorm )
        {
            p.ResizeTo( col, 1 );
            t.ResizeTo( col, 1 );
            break;
        }
        p.Set( col, 0, pivot.index );

        // Perform the swap
        const int colOwner = (col+rowAlign) % rowStride;
        const int pivotColOwner = (pivot.index+rowAlign) % rowStride;
        const bool myCol = ( g.Col() == colOwner );
        const bool myPivotCol = ( g.Col() == pivotColOwner );
        if( col != pivot.index )
        {
            if( myCol && myPivotCol )
            {
                const int colLocal = (col-rowShift) / rowStride;
                const int pivotColLocal = (pivot.index-rowShift) / rowStride;
                MemSwap
                ( A.Buffer(0,colLocal), A.Buffer(0,pivotColLocal),
                  &swapBuf[0], mLocal );
                norms[pivotColLocal] = norms[colLocal];
                origNorms[pivotColLocal] = origNorms[colLocal];
            }
            else if( myCol )
            {
                const int colLocal = (col-rowShift) / rowStride;
                mpi::SendRecv
                ( A.Buffer(0,colLocal), mLocal,
                  pivotColOwner, 0, pivotColOwner, 0, g.RowComm() );
                mpi::Send( &norms[colLocal], 1, pivotColOwner, 0, g.RowComm() );
            }
            else if( myPivotCol )
            {
                const int pivotColLocal = (pivot.index-rowShift) / rowStride;
                mpi::SendRecv
                ( A.Buffer(0,pivotColLocal), mLocal,
                  colOwner, 0, colOwner, 0, g.RowComm() );
                mpi::Recv( &norms[pivotColLocal], 1, colOwner, 0, g.RowComm() );
            }
        }

        // Compute the Householder reflector
        const C tau = Reflector( alpha11, a21 );
        t.Set( A00.Width(), 0, tau );

        // Apply the Householder reflector
        const bool myDiagonalEntry = ( g.Row() == alpha11.ColAlignment() &&
                                       g.Col() == alpha11.RowAlignment() );
        C alpha = 0;
        if( myDiagonalEntry )
        {
            alpha = alpha11.GetLocal(0,0);
            alpha11.SetLocal(0,0,1);
        }
        aLeftCol_MC_STAR = aLeftCol;
        Zeros( z_MR_STAR, ARightPan.Width(), 1 );
        LocalGemv
        ( ADJOINT, C(1), ARightPan, aLeftCol_MC_STAR, C(0), z_MR_STAR );
        z_MR_STAR.SumOverCol();
        Ger
        ( -Conj(tau),
          aLeftCol_MC_STAR.LockedMatrix(),
          z_MR_STAR.LockedMatrix(),
          ARightPan.Matrix() );
        if( myDiagonalEntry )
            alpha11.SetLocal(0,0,alpha);

        // Update the column norm estimates in the same manner as LAWN 176.
        // However, we do so in two steps in order to lower the communication
        // latency:
        //   1) Each process first computes which of its column norms are
        //      too inaccurate and need to be recomputed.
        //   2) Each process communicates within its process column in order
        //      to replace the inaccurate column norms.
        // Step 1: Perform all of the easy updates and mark inaccurate norms
        a12_STAR_MR = a12;
        const int a12LocalWidth = a12_STAR_MR.LocalWidth();
        const int a12RowShift = a12_STAR_MR.RowShift();
        inaccurateNorms.resize(0);
        for( int kLocal=0; kLocal<a12LocalWidth; ++kLocal )
        {
            const int k = a12RowShift + kLocal*rowStride;
            const int j = k + col+1;
            const int jLocal = (j-rowShift) / rowStride;
            if( norms[jLocal] != Real(0) )
            {
                const Real beta = Abs(a12_STAR_MR.GetLocal(0,kLocal));
                Real gamma = beta / norms[jLocal];
                gamma = std::max( Real(0), (Real(1)-gamma)*(Real(1)+gamma) );
                const Real ratio = norms[jLocal] / origNorms[jLocal];
                const Real phi = gamma*(ratio*ratio);
                if( phi <= updateTol || alwaysRecompute )
                    inaccurateNorms.push_back( jLocal );
                else
                    norms[jLocal] *= Sqrt(gamma);
            }
        }
        // Step 2: Compute the replacement norms and also reset origNorms
        ReplaceColumnNorms( A, inaccurateNorms, norms, origNorms );
        //--------------------------------------------------------------------//
        aLeftCol_MC_STAR.FreeAlignments();
        z_MR_STAR.FreeAlignments();

        SlidePartitionDownDiagonal
        ( ATL, /**/ ATR,  A00, a01,     /**/ A02,
               /**/       a10, alpha11, /**/ a12,
         /*************/ /**********************/
          ABL, /**/ ABR,  A20, a21,     /**/ A22 );
    }
}

template<typename Real>
inline void
BusingerGolub
( DistMatrix<Complex<Real> >& A, 
  DistMatrix<Complex<Real>,MD,STAR>& t, 
  DistMatrix<int,VR,STAR>& p,
  int numSteps,
  bool alwaysRecompute=false )
{
#ifndef RELEASE
    CallStackEntry entry("qr::BusingerGolub");
#endif
    // Use a tolerance of -1 so that we always perform numSteps iterations
    BusingerGolub( A, t, p, numSteps, Real(-1), alwaysRecompute );
}

template<typename Real>
inline void
BusingerGolub
( DistMatrix<Complex<Real> >& A, 
  DistMatrix<Complex<Real>,MD,STAR>& t, 
  DistMatrix<int,VR,STAR>& p,
  bool alwaysRecompute=false )
{
#ifndef RELEASE
    CallStackEntry entry("qr::BusingerGolub");
#endif
    const int numSteps = std::min(A.Height(),A.Width());
    BusingerGolub( A, t, p, numSteps, alwaysRecompute );
}

} // namespace qr
} // namespace elem

#endif // ifndef LAPACK_QR_BUSINGERGOLUB_HPP
