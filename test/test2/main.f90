module Mainfile
contains

integer(4) function main()
    use Xmod
    implicit none
 
    real(8) :: x
    integer(4) :: y

    call set_x(x,y)
!    x = add(x+10,y-5)

    print *, "x is: ", x

    main = 0
    return
end function main

end module Mainfile
