class P:
    def __init__(self):
        self.xs = [i for i in [1, 2, 3, 4, 5] if (i > 2)]
    @property
    def count(self):
        return len(self.xs)
print(P().count)
