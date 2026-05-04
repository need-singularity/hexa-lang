def tag(t):
    def inner(f):
        def wrap():
            return ((t + ":") + f())
        return wrap
    return inner
@tag("X")
def name():
    return "n"
print(name())
