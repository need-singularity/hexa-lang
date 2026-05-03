import asyncio
async def main():
    total = 0
    for x in [1, 2, 3]:
        total = (total + x)
    return total
print(asyncio.run(main()))
