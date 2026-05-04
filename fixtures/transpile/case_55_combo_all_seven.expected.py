import asyncio
def track(cls):
    cls.tracked = True
    return cls
@track
class Stream:
    def __init__(self, xs):
        self.xs = [x for x in xs if (x > 0)]
    def __enter__(self):
        return self
    def __exit__(self, et, ev, tb):
        return False
    async def collect(self):
        d = {i: v for i, v in enumerate(self.xs)}
        return d
async def main():
    with Stream([(-1), 1, 2, (-3), 4]) as s:
        return (await s.collect())
print(Stream.tracked)
print(asyncio.run(main()))
