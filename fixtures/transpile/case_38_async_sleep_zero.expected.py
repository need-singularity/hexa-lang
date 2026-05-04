import asyncio
async def main():
    (await asyncio.sleep(0))
    return "ok"
print(asyncio.run(main()))
