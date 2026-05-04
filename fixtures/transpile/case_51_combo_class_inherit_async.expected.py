import asyncio
class Base:
    async def run(self):
        return "B"
class Sub(Base):
    async def run(self):
        return "S"
async def main():
    return (await Sub().run())
print(asyncio.run(main()))
