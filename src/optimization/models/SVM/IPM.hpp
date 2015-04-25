/*
   Copyright (c) 2009-2015, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
#include "El.hpp"

// The soft-margin Support Vector Machine (SVM) [1] seeks the solution of the
// Quadratic Program
//
//   min_{w,beta,z} (1/2) || w ||_2^2 + lambda 1^T z
//
//   s.t. |-diag(d) A, -d, -I | | w    | <= | -1 |
//        |      0,     0, -I | | beta |    |  0 |
//                              | z    |
//
// [1] Corinna Cortes and Vladimir Vapnik,
//     "Support-Vector Networks",
//     Journal of Machine Learning, Vol. 20, No. 3, 1995.
//

namespace El {
namespace svm {

template<typename Real>
void IPM
( const Matrix<Real>& A, const Matrix<Real>& d, 
        Real lambda,           Matrix<Real>& x,
  const qp::affine::Ctrl<Real>& ctrl )
{
    DEBUG_ONLY(CallStackEntry cse("svm::IPM"))
    const Int m = A.Height();
    const Int n = A.Width();
    const Range<Int> wInd(0,n), betaInd(n,n+1), zInd(n+1,n+m+1);

    Matrix<Real> Q, c, AHat, b, G, h;

    // Q := | I 0 0 |
    //      | 0 0 0 |
    //      | 0 0 0 |
    // ==============
    Zeros( Q, n+m+1, n+m+1 );
    auto Qww = Q( wInd, wInd );
    FillDiagonal( Qww, Real(1) );

    // c := [0;0;lambda]
    // =================
    Zeros( c, n+m+1, 1 );
    auto cz = c( zInd, IR(0,1) );
    Fill( cz, lambda );

    // AHat = []
    // =========
    Zeros( AHat, 0, n+m+1 );

    // b = []
    // ======
    Zeros( b, 0, 1 );

    // G := |-diag(d) A, -d, -I|
    //      |      0,     0, -I|
    // =========================
    Zeros( G, 2*m, n+m+1 );
    auto G0w    = G( IR(0,m),   wInd    );
    auto G0beta = G( IR(0,m),   betaInd );
    auto G0z    = G( IR(0,m),   zInd    );
    auto G1z    = G( IR(m,2*m), zInd    );
    G0w = A; Scale( Real(-1), G0w ); DiagonalScale( LEFT, NORMAL, d, G0w );
    G0beta = d; Scale( Real(-1), G0beta );
    FillDiagonal( G0z, Real(-1) );
    FillDiagonal( G1z, Real(-1) );

    // h := [-ones(m,1); zeros(m,1)]
    // =============================
    Zeros( h, 2*m, 1 );
    auto h0 = h( IR(0,m), IR(0,1) );
    Fill( h0, Real(-1) );
    
    // Solve the affine QP
    // ===================
    Matrix<Real> y, z, s;
    QP( Q, AHat, G, b, c, h, x, y, z, s, ctrl );
}

template<typename Real>
void IPM
( const AbstractDistMatrix<Real>& A, const AbstractDistMatrix<Real>& d, 
        Real lambda,                       AbstractDistMatrix<Real>& x, 
  const qp::affine::Ctrl<Real>& ctrl )
{
    DEBUG_ONLY(CallStackEntry cse("svm::IPM"))
    const Int m = A.Height();
    const Int n = A.Width();
    const Grid& g = A.Grid();
    const Range<Int> wInd(0,n), betaInd(n,n+1), zInd(n+1,n+m+1);

    DistMatrix<Real> Q(g), c(g), AHat(g), b(g), G(g), h(g);

    // Q := | I 0 0 |
    //      | 0 0 0 |
    //      | 0 0 0 |
    // ==============
    Zeros( Q, n+m+1, n+m+1 );
    auto Qww = Q( wInd, wInd );
    FillDiagonal( Qww, Real(1) );

    // c := [0;0;lambda]
    // ================
    Zeros( c, n+m+1, 1 );
    auto cz = c( zInd, IR(0,1) );
    Fill( cz, lambda );

    // AHat = []
    // =========
    Zeros( AHat, 0, n+m+1 );

    // b = []
    // ======
    Zeros( b, 0, 1 );

    // G := |-diag(d) A, -d, -I|
    //      |      0,     0, -I|
    // =========================
    Zeros( G, 2*m, n+m+1 );
    auto G0w    = G( IR(0,m),   wInd    );
    auto G0beta = G( IR(0,m),   betaInd );
    auto G0z    = G( IR(0,m),   zInd    );
    auto G1z    = G( IR(m,2*m), zInd    );
    G0w = A; Scale( Real(-1), G0w ); DiagonalScale( LEFT, NORMAL, d, G0w );
    G0beta = d; Scale( Real(-1), G0beta );
    FillDiagonal( G0z, Real(-1) );
    FillDiagonal( G1z, Real(-1) );

    // h := [-ones(m,1); zeros(m,1)]
    // =============================
    Zeros( h, 2*m, 1 );
    auto h0 = h( IR(0,m), IR(0,1) );
    Fill( h0, Real(-1) );

    // Solve the affine QP
    // ===================
    DistMatrix<Real> y(g), z(g), s(g);
    QP( Q, AHat, G, b, c, h, x, y, z, s, ctrl );
}

template<typename Real>
void IPM
( const SparseMatrix<Real>& A, const Matrix<Real>& d, 
        Real lambda,                 Matrix<Real>& x,
  const qp::affine::Ctrl<Real>& ctrl )
{
    DEBUG_ONLY(CallStackEntry cse("svm::IPM"))
    const Int m = A.Height();
    const Int n = A.Width();
    const Range<Int> wInd(0,n), betaInd(n,n+1), zInd(n+1,n+m+1);

    SparseMatrix<Real> Q, AHat, G;
    Matrix<Real> c, b, h;

    // Q := | I 0 0 |
    //      | 0 0 0 |
    //      | 0 0 0 |
    // ==============
    Zeros( Q, n+m+1, n+m+1 );
    Q.Reserve( n );
    for( Int e=0; e<n; ++e )
        Q.QueueUpdate( e, e, Real(1) );
    Q.ProcessQueues();

    // c := [0;0;lambda]
    // =================
    Zeros( c, n+m+1, 1 );
    auto cz = c( zInd, IR(0,1) );
    Fill( cz, lambda );

    // AHat = []
    // =========
    Zeros( AHat, 0, n+m+1 );

    // b = []
    // ======
    Zeros( b, 0, 1 );

    // G := |-diag(d) A, -d, -I|
    //      |      0,     0, -I|
    // =========================
    Zeros( G, 2*m, n+m+1 );
    const Int numEntriesA = A.NumEntries(); 
    G.Reserve( numEntriesA+3*m );
    for( Int e=0; e<numEntriesA; ++e )
        G.QueueUpdate( A.Row(e), A.Col(e), -d.Get(A.Row(e),0)*A.Value(e) );
    for( Int e=0; e<m; ++e )
        G.QueueUpdate( e, n, -d.Get(e,0) );
    for( Int e=0; e<m; ++e )
    {
        G.QueueUpdate( e,   e+n+1, Real(-1) );
        G.QueueUpdate( e+m, e+n+1, Real(-1) );
    }
    G.ProcessQueues();

    // h := [-ones(m,1); zeros(m,1)]
    // =============================
    Zeros( h, 2*m, 1 );
    auto h0 = h( IR(0,m), IR(0,1) );
    Fill( h0, Real(-1) );
    
    // Solve the affine QP
    // ===================
    Matrix<Real> y, z, s;
    QP( Q, AHat, G, b, c, h, x, y, z, s, ctrl );
}

template<typename Real>
void IPM
( const DistSparseMatrix<Real>& A, const DistMultiVec<Real>& d, 
        Real lambda,                     DistMultiVec<Real>& x,
  const qp::affine::Ctrl<Real>& ctrl )
{
    DEBUG_ONLY(CallStackEntry cse("svm::IPM"))
    const Int m = A.Height();
    const Int n = A.Width();
    mpi::Comm comm = A.Comm();
    const Int commSize = mpi::Size(comm);

    DistSparseMatrix<Real> Q(comm), AHat(comm), G(comm);
    DistMultiVec<Real> c(comm), b(comm), h(comm);

    // Q := | I 0 0 |
    //      | 0 0 0 |
    //      | 0 0 0 |
    // ==============
    Zeros( Q, n+m+1, n+m+1 );
    {
        // Count the number of local entries in the top-left I
        // ---------------------------------------------------
        Int numLocalUpdates = 0;
        for( Int iLoc=0; iLoc<Q.LocalHeight(); ++iLoc )
            if( Q.GlobalRow(iLoc) < n )
                ++numLocalUpdates;
            else
                break;
        Q.Reserve( numLocalUpdates );
        for( Int iLoc=0; iLoc<Q.LocalHeight(); ++iLoc )
            if( Q.GlobalRow(iLoc) < n )
                Q.QueueLocalUpdate( iLoc, Q.GlobalRow(iLoc), Real(1) );
        Q.ProcessQueues();
    }

    // c := [0;0;lambda]
    // =================
    Zeros( c, n+m+1, 1 );
    for( Int iLoc=0; iLoc<c.LocalHeight(); ++iLoc )
        if( c.GlobalRow(iLoc) > n )
            c.SetLocal( iLoc, 0, lambda );

    // AHat = []
    // =========
    Zeros( AHat, 0, n+m+1 );

    // b = []
    // ======
    Zeros( b, 0, 1 );

    // G := |-diag(d) A, -d, -I|
    //      |      0,     0, -I|
    // =========================
    Zeros( G, 2*m, n+m+1 );
    {
        // Compute the metadata
        // --------------------
        vector<int> sendCounts(commSize,0);
        for( Int e=0; e<A.NumLocalEntries(); ++e )
            ++sendCounts[ G.RowOwner(A.Row(e)) ];
        for( Int iLoc=0; iLoc<d.LocalHeight(); ++iLoc )
            ++sendCounts[ G.RowOwner(d.GlobalRow(iLoc)) ]; 
        // Pack
        // ----
        vector<int> sendOffs;
        const int totalSend = Scan( sendCounts, sendOffs );
        vector<Entry<Real>> sendBuf(totalSend);
        auto offs = sendOffs;
        for( Int e=0; e<A.NumLocalEntries(); ++e )
        {
            Int i = A.Row(e);
            int owner = G.RowOwner(i);
            Real value = -d.GetLocal(i-d.FirstLocalRow(),0)*A.Value(e);
            sendBuf[offs[owner]++] = Entry<Real>{ i, A.Col(e), value };
        }
        for( Int iLoc=0; iLoc<d.LocalHeight(); ++iLoc )
        {
            Int i = d.GlobalRow(iLoc);
            int owner = G.RowOwner(i);
            sendBuf[offs[owner]++] = Entry<Real>{ i, n, -d.GetLocal(iLoc,0) };
        }
        // Exchange and unpack
        // -------------------
        auto recvBuf = mpi::AllToAll( sendBuf, sendCounts, sendOffs, comm );
        G.Reserve( recvBuf.size()+G.LocalHeight() );
        for( auto& entry : recvBuf )
            G.QueueUpdate( entry );
        for( Int iLoc=0; iLoc<G.LocalHeight(); ++iLoc )
        {
            const Int i = G.GlobalRow(iLoc);
            if( i < m )
                G.QueueLocalUpdate( iLoc, i+n+1, Real(-1) );
            else
                G.QueueLocalUpdate( iLoc, i-m+n+1, Real(-1) );
        }
        G.ProcessQueues();
    }

    // h := [-ones(m,1); zeros(m,1)]
    // =============================
    Zeros( h, 2*m, 1 );
    for( Int iLoc=0; iLoc<h.LocalHeight(); ++iLoc )
        if( h.GlobalRow(iLoc) < m )
            h.SetLocal( iLoc, 0, Real(-1) );
        else
            break;

    // Solve the affine QP
    // ===================
    DistMultiVec<Real> y(comm), z(comm), s(comm);
    QP( Q, AHat, G, b, c, h, x, y, z, s, ctrl );
}

} // namespace svm
} // namespace El