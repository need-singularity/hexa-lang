def add_marker(cls):
    cls.marker = "yes"
    return cls
@add_marker
class M:
    pass
print(M.marker)
