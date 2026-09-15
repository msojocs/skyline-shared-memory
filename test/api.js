'use strict'

const assert = require('node:assert/strict')
const fs = require('node:fs')
const path = require('node:path')

const modulePath = process.env.SKYLINE_SHARED_MEMORY_MODULE || [
  path.join(__dirname, '..', 'build', 'Release', 'sharedMemory.node'),
  path.join(__dirname, '..', 'build', 'sharedMemory.node'),
].find((candidate) => fs.existsSync(candidate))
if (!modulePath) throw new Error('sharedMemory.node build output was not found')
const sharedMemory = require(modulePath)

const key = `codex_shared_memory_api_${process.pid}`
const size = 64
let removed = false

if (process.platform === 'win32') {
  const handle = sharedMemory.setMemory(key, size)
  assert(handle instanceof Uint8Array)
  assert.equal(handle.byteLength, 16)

  const source = Buffer.alloc(size)
  for (let index = 0; index < source.length; index++) {
    source[index] = (index * 13 + 7) & 0xff
  }
  assert.equal(sharedMemory.setMemoryByAddress(handle, source), true)

  const byName = sharedMemory.getMemory(key)
  assert(Buffer.isBuffer(byName))
  assert.equal(byName.byteLength, size)
  assert.deepEqual(Array.from(byName), Array.from(source))

  const byAddress = sharedMemory.getMemoryByAddress(handle)
  assert(Buffer.isBuffer(byAddress))
  assert.deepEqual(Array.from(byAddress), Array.from(source))

  sharedMemory.deleteGetCache(key)
  assert.deepEqual(Array.from(sharedMemory.getMemoryByAddress(handle)), Array.from(source))
  sharedMemory.deleteSetCache(key)
  assert.deepEqual(Array.from(sharedMemory.getMemory(key)), Array.from(source))
  assert.throws(() => sharedMemory.getMemoryByAddress(handle), /Invalid shared memory handle|Unexpected native error/)
  assert.throws(
    () => sharedMemory.setMemory(key, 1024 * 1024 * 1024 + 1),
    /invalid shared memory size|Unexpected native error/,
  )
  assert.throws(() => sharedMemory.getMemoryByAddress(handle), /Invalid shared memory handle|Unexpected native error/)
  assert.deepEqual(Array.from(sharedMemory.getMemory(key)), Array.from(source))

  const target = Buffer.alloc(size + 8, 0xa5)
  assert.equal(sharedMemory.copyMemoryToBuffer(key, target), size)
  assert.deepEqual(Array.from(target.subarray(0, size)), Array.from(source))
  assert.deepEqual(Array.from(target.subarray(size)), Array(8).fill(0xa5))
  sharedMemory.deleteCopyCache(key)
  const creatorTarget = Buffer.alloc(size)
  assert.equal(sharedMemory.copyMemoryToBuffer(key, creatorTarget), size)
  assert.deepEqual(Array.from(creatorTarget), Array.from(source))

  const largerHandle = sharedMemory.setMemory(key, size * 2)
  const largerSource = Buffer.alloc(size * 2, 0x3c)
  assert.equal(sharedMemory.setMemoryByAddress(largerHandle, largerSource), true)
  assert.throws(() => sharedMemory.getMemoryByAddress(handle), /Invalid shared memory handle|Unexpected native error/)
  const cachedTarget = Buffer.alloc(size * 2)
  assert.equal(sharedMemory.copyMemoryToBuffer(key, cachedTarget), size)
  assert.deepEqual(Array.from(cachedTarget.subarray(0, size)), Array.from(source))
  assert.deepEqual(Array.from(cachedTarget.subarray(size)), Array(size).fill(0))
  sharedMemory.deleteCopyCache(key)
  const refreshedTarget = Buffer.alloc(size * 2)
  assert.equal(sharedMemory.copyMemoryToBuffer(key, refreshedTarget), size * 2)
  assert.deepEqual(Array.from(refreshedTarget), Array.from(largerSource))
  const newestHandle = sharedMemory.setMemory(key, size * 3)
  const newestSource = Buffer.alloc(size * 3, 0x6d)
  assert.equal(sharedMemory.setMemoryByAddress(newestHandle, newestSource), true)
  assert.throws(() => sharedMemory.getMemoryByAddress(handle), /Invalid shared memory handle|Unexpected native error/)
  sharedMemory.deleteCopyCache(key)
  const newestTarget = Buffer.alloc(size * 3)
  assert.equal(sharedMemory.copyMemoryToBuffer(key, newestTarget), size * 3)
  assert.deepEqual(Array.from(newestTarget), Array.from(newestSource))
  sharedMemory.removeMemory(key)
  removed = true
  assert.throws(() => sharedMemory.getMemoryByAddress(newestHandle), /Invalid shared memory handle|Unexpected native error/)
  const reuseKey = `${key}_reuse`
  const reuseHandle = sharedMemory.setMemory(reuseKey, size)
  assert.equal(sharedMemory.setMemoryByAddress(reuseHandle, Buffer.alloc(size, 0x7e)), true)
  assert.throws(() => sharedMemory.getMemoryByAddress(newestHandle), /Invalid shared memory handle|Unexpected native error/)
  sharedMemory.removeMemory(reuseKey)
} else {
  const view = sharedMemory.setMemory(key, size)
  assert(view instanceof ArrayBuffer)
  assert.equal(view.byteLength, size)
  const bytes = new Uint8Array(view)
  bytes[0] = 0x5a
  assert.equal(new Uint8Array(sharedMemory.getMemory(key))[0], 0x5a)
}

if (!removed) sharedMemory.removeMemory(key)
console.log(JSON.stringify({ ok: true, platform: process.platform }))
