program hello_world
    use mpi
    implicit none

    integer :: ierr, rank, size

    ! Initialize MPI
    call MPI_Init(ierr)

    ! Get the rank of the process
    call MPI_Comm_rank(MPI_COMM_WORLD, rank, ierr)

    ! Get the total number of processes
    call MPI_Comm_size(MPI_COMM_WORLD, size, ierr)

    ! Print a message from each process
    print *, "Hello from process ", rank, " of ", size

    ! Finalize MPI
    call MPI_Finalize(ierr)

end program hello_world