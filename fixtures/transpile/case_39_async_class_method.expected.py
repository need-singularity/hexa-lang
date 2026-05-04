import asyncio
class A:
    async def run(self):
        return "ar"
print(asyncio.run(A().run()))
