/*
   Copyright (c) 2009-2016, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License,
   which can be found in the LICENSE file in the root directory, or at
   http://opensource.org/licenses/BSD-2-Clause
*/
#ifndef EL_BLAS_COPY_COLFILTER_HPP
#define EL_BLAS_COPY_COLFILTER_HPP

namespace El {
namespace copy {

// (Collect(U),V) |-> (U,V)
template <Device D, typename T>
void ColFilter_impl( const ElementalMatrix<T>& A, ElementalMatrix<T>& B )
{
    EL_DEBUG_CSE
    EL_DEBUG_ONLY(
      if( A.ColDist() != Collect(B.ColDist()) ||
          A.RowDist() != B.RowDist() )
          LogicError("Incompatible distributions");
    )
    AssertSameGrids( A, B );

    B.AlignRowsAndResize
    ( A.RowAlign(), A.Height(), A.Width(), false, false );
    if( !B.Participating() )
        return;

    const Int colStride = B.ColStride();
    const Int colShift = B.ColShift();

    const Int localHeight = B.LocalHeight();
    const Int localWidth = B.LocalWidth();

    const Int rowDiff = B.RowAlign() - A.RowAlign();

    auto syncInfoA = SyncInfoFromMatrix(
        static_cast<Matrix<T,D> const&>(A.LockedMatrix()));
    auto syncInfoB = SyncInfoFromMatrix(
        static_cast<Matrix<T,D> const&>(B.LockedMatrix()));

    auto syncHelper = MakeMultiSync(syncInfoB, syncInfoA);

    if( rowDiff == 0 )
    {
        util::InterleaveMatrix(
            localHeight, localWidth,
            A.LockedBuffer(colShift,0), colStride, A.LDim(),
            B.Buffer(),                 1,         B.LDim(),
            syncInfoB);
    }
    else
    {
#ifdef EL_UNALIGNED_WARNINGS
        if( B.Grid().Rank() == 0 )
            Output("Unaligned ColFilter");
#endif
        const Int rowStride = B.RowStride();
        const Int sendRowRank = Mod( B.RowRank()+rowDiff, rowStride );
        const Int recvRowRank = Mod( B.RowRank()-rowDiff, rowStride );
        const Int localWidthA = A.LocalWidth();
        const Int sendSize = localHeight*localWidthA;
        const Int recvSize = localHeight*localWidth;
        simple_buffer<T,D> buffer(sendSize+recvSize, syncInfoB);
        T* sendBuf = buffer.data();
        T* recvBuf = buffer.data() + sendSize;

        // Pack
        util::InterleaveMatrix(
            localHeight, localWidthA,
            A.LockedBuffer(colShift,0), colStride, A.LDim(),
            sendBuf,                    1,         localHeight,
            syncInfoB);

        // Realign
        mpi::SendRecv(
            sendBuf, sendSize, sendRowRank,
            recvBuf, recvSize, recvRowRank, B.RowComm(), syncInfoB );

        // Unpack
        util::InterleaveMatrix(
            localHeight, localWidth,
            recvBuf,    1, localHeight,
            B.Buffer(), 1, B.LDim(), syncInfoB);
    }
}

template <typename T>
void ColFilter
( const ElementalMatrix<T>& A, ElementalMatrix<T>& B )
{
    EL_DEBUG_CSE
    if (A.GetLocalDevice() != B.GetLocalDevice())
        LogicError(
            "ColFilter: For now, A and B must be on same device.");

    switch (A.GetLocalDevice())
    {
    case Device::CPU:
        ColFilter_impl<Device::CPU>(A,B);
        break;
#ifdef HYDROGEN_HAVE_GPU
    case Device::GPU:
        ColFilter_impl<Device::GPU>(A,B);
        break;
#endif // HYDROGEN_HAVE_GPU
    default:
        LogicError("ColFilter: Bad device.");
    }
}

template<typename T>
void ColFilter
( const BlockMatrix<T>& A, BlockMatrix<T>& B )
{
    EL_DEBUG_CSE
    EL_DEBUG_ONLY(
      if( A.ColDist() != Collect(B.ColDist()) ||
          A.RowDist() != B.RowDist() )
          LogicError("Incompatible distributions");
    )
    AssertSameGrids( A, B );

    const Int height = A.Height();
    const Int width = A.Width();
    const Int rowCut = A.RowCut();
    const Int blockHeight = A.BlockHeight();
    const Int blockWidth = A.BlockWidth();

    B.AlignAndResize
    ( blockHeight, blockWidth, 0, A.RowAlign(), 0, rowCut,
      height, width, false, false );
    // TODO(poulson): Realign if the cuts are different
    if( A.BlockWidth() != B.BlockWidth() || A.RowCut() != B.RowCut() )
    {
        EL_DEBUG_ONLY(
          Output("Performing expensive GeneralPurpose ColFilter");
        )
        GeneralPurpose( A, B );
        return;
    }
    if( !B.Participating() )
        return;

    const Int colStride = B.ColStride();
    const Int colShift = B.ColShift();

    const Int localHeight = B.LocalHeight();
    const Int localWidth = B.LocalWidth();

    const Int rowDiff = B.RowAlign() - A.RowAlign();
    if( rowDiff == 0 )
    {
        util::BlockedColFilter
        ( height, localWidth,
          colShift, colStride, B.BlockHeight(), B.ColCut(),
          A.LockedBuffer(), A.LDim(),
          B.Buffer(), B.LDim() );
    }
    else
    {
#ifdef EL_UNALIGNED_WARNINGS
        if( B.Grid().Rank() == 0 )
            Output("Unaligned ColFilter");
#endif
        const Int rowStride = B.RowStride();
        const Int sendRowRank = Mod( B.RowRank()+rowDiff, rowStride );
        const Int recvRowRank = Mod( B.RowRank()-rowDiff, rowStride );
        const Int localWidthA = A.LocalWidth();
        const Int sendSize = localHeight*localWidthA;
        const Int recvSize = localHeight*localWidth;
        vector<T> buffer;
        FastResize( buffer, sendSize+recvSize );
        T* sendBuf = &buffer[0];
        T* recvBuf = &buffer[sendSize];

        // Pack
        util::BlockedColFilter(
            height, localWidthA,
            colShift, colStride, B.BlockHeight(), B.ColCut(),
            A.LockedBuffer(), A.LDim(),
            sendBuf,          localHeight);

        // Realign
        mpi::SendRecv(
            sendBuf, sendSize, sendRowRank,
            recvBuf, recvSize, recvRowRank, B.RowComm(),
            SyncInfo<Device::CPU>{} );

        // Unpack
        util::InterleaveMatrix(
            localHeight, localWidth,
            recvBuf,    1, localHeight,
            B.Buffer(), 1, B.LDim(), SyncInfo<Device::CPU>{});
    }
}

} // namespace copy
} // namespace El

#endif // ifndef EL_BLAS_COPY_COLFILTER_HPP
