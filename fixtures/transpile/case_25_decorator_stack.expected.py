def d1(f):
    def w():
        return (("1[" + f()) + "]")
    return w
def d2(f):
    def w():
        return (("2[" + f()) + "]")
    return w
@d1
@d2
def raw():
    return "r"
print(raw())
