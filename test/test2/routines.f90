module Xmod
contains

real(8) function add(x,y)
    integer(4), intent(in) :: y
    real(8), intent(in) :: x

    add = x+y
    return
end function add

subroutine set_x(x,y)
    integer(4), intent(in) :: y
    real(8) :: x

    x = 10.d0 + y
end subroutine set_x

end module Xmod
