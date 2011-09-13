integer(4) function main(argc, argv)

    integer(4) :: argc
    character(len=*), dimension(*) :: argv

    integer(4), dimension(0:10) :: xs
    integer(4) :: i, sum

    sum = 0

    do i = 0,10
       xs(i) = i
       print *, xs(i)
       sum = sum + i
    enddo

    print *, "and their sum is: ", sum

    main = 0
    return
end
