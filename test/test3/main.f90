integer(4) function main(argc, argv)

    integer(4) :: argc
    character(len=*), dimension(*) :: argv

    integer(4) :: value

    OPEN(3,file=argv(2))
    READ (3,*) value
    CLOSE(3)       

    print *, "value=",value

    main = 0
    return
end
