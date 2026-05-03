import asyncio
async def one():
    return 1
async def two():
    return 2
async def main():
    results = (await asyncio.gather(one(), two()))
    return results
print(asyncio.run(main()))
