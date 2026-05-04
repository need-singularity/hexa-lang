import asyncio
class ACM:
    async def __aenter__(self):
        return "ax"
    async def __aexit__(self, et, ev, tb):
        return False
async def main():
    async with ACM() as v:
        return v
print(asyncio.run(main()))
