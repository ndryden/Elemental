program main
  use mpi
  implicit none

  ! Handles for Elemental's C++ objects
  integer :: grid, A, B, w, X

  ! Process grid information
  integer :: r, c, p, row, col, rank

  ! Our process's local matrix size (for A and B)
  integer :: mLoc, nLoc

  ! Local buffers for distributed A and B matrices
  real*8, allocatable, dimension(:,:) :: ALoc, BLoc

  ! Indices
  integer :: i, j, iLoc, jLoc

  ! Useful constants
  integer :: iZero = 0
  integer :: n = 10                ! problem size
  integer :: nb = 96               ! algorithmic blocksize
  integer :: comm = MPI_COMM_WORLD ! global communicator

  ! Initialize Elemental and MPI
  call elem_initialize()

  ! Create a process grid and extract the relevant information
  call elem_create_grid( grid, comm )
  call elem_grid_height( grid, r )
  call elem_grid_width( grid, c )
  call elem_grid_size( grid, p )
  call elem_grid_row( grid, row )
  call elem_grid_col( grid, col )
  call elem_grid_rank( grid, rank )

  ! Create buffers for passing into data for distributed matrices 
  call elem_length( n, row, r, mLoc )
  call elem_length( n, col, c, nLoc )
  allocate(ALoc(mLoc,nLoc))
  allocate(BLoc(mLoc,nLoc))

  ! Set entry (i,j) of the A matrix to i+j, which is symmetric 
  do jLoc=1,nLoc
    j = col + (jLoc-1)*c + 1
    do iLoc=1,mLoc
      i = row + (iLoc-1)*r + 1
      ALoc(iLoc,jLoc) = i+j
    end do
  end do

  ! Set B to twice the identity since it is a trivial SPD matrix 
  do jLoc=1,nLoc
    j = col + (jLoc-1)*c + 1
    do iLoc=1,mLoc
      i = row + (iLoc-1)*r + 1
      if( i == j ) then
        BLoc(iLoc,jLoc) = 2.0;
      else
        BLoc(iLoc,jLoc) = 0.0;
      end if
    end do
  end do

  ! Register the distributed matrices, A and B, with Elemental 
  call elem_register_dist_mat( a, n, n, iZero, iZero, ALoc, mLoc, grid )
  call elem_register_dist_mat( b, n, n, iZero, iZero, BLoc, mLoc, grid )

  ! I do not know of a good way to flush the output from F90, as the flush
  ! command is not standard. Thus, I chose not to write to stdout from F90.

  ! Print the input matrices 
  call elem_print_dist_mat( A )
  call elem_print_dist_mat( B )

  ! Set the algorithmic blocksize to 'nb'
  call elem_set_blocksize( nb )

  ! For tuning purposes choose one of the following
  ! (see http://elemental.googlecode.com/hg/doc/build/html/advanced/tuning.html
  !  for more details)
  !
  !call elem_set_normal_tridiag_approach
  call elem_set_square_tridiag_approach
  !call elem_set_default_tridiag_approach
  !
  ! and then choose one of the following
  !
  call elem_set_row_major_tridiag_subgrid
  !call elem_set_col_major_tridiag_subgrid
  !
  ! For sufficiently large numbers of processes, 
  ! 'elem_set_square_tridiag_approach'
  ! will be the fastest, but whether a row-major or column-major ordering of 
  ! the subgrid is best depends upon your network (hence the second choice).

  ! Given the pencil (A,B), solve for (w,X) such that AX=BX diag(w)
  call elem_create_dist_mat_star_star( w, grid )
  call elem_create_dist_mat( X, grid )
  call elem_symmetric_axbx( A, B, w, X )

  ! Print the eigenvalues and eigenvectors
  call elem_print_dist_mat( X )
  call elem_print_dist_mat_star_star( w )

  ! Shut down Elemental and MPI
  call elem_finalize()
end
