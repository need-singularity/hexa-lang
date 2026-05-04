import asyncio
async def inner():
    return 7
async def outer():
    v = (await inner())
    return (v + 1)
print(asyncio.run(outer()))
