def tag(cls):
    cls.kind = "tag"
    return cls
@tag
class Bag:
    def __init__(self):
        self.xs = [(x * 2) for x in [1, 2, 3]]
print(Bag.kind)
print(Bag().xs)
