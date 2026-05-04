def memo(f):
    cache = {}
    def w(x):
        if (x in cache):
            return cache[x]
        v = f(x)
        cache[x] = v
        return v
    return w
@memo
def sq(n):
    return (n * n)
print([sq(i) for i in [1, 2, 3, 1, 2]])
