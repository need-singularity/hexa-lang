class Counter:
    def __init__(self):
        self.n = 0
    def tick(self):
        self.n = (self.n + 1)
c = Counter()
c.tick()
c.tick()
print(c.n)
