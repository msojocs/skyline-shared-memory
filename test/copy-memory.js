'use strict'

// Runs under Node or an existing Electron process. Linux fixtures are written
// as files so this test never needs the legacy external-ArrayBuffer API.
const assert = require('node:assert/strict')
const fs = require('node:fs')
const path = require('node:path')

function run(modulePath) {
  const sharedMemory = require(path.resolve(modulePath))
  const key = `copy_memory_test_${process.pid}_${Date.now()}`
  const missingKey = `${key}_missing`
  const filename = `/dev/shm/skyline_${key}.dat`
  const payload = Buffer.from(Array.from({ length: 64 }, (_, i) => (i * 13 + 7) & 255))
  let handle
  let fixtureExists = false

  function writeFixture(bytes) {
    if (process.platform === 'win32') {
      handle = sharedMemory.setMemory(key, bytes.length)
      sharedMemory.setMemoryByAddress(handle, bytes)
    } else {
      const headerSize = ['ia32', 'arm'].includes(process.arch) ? 8 : 16
      const file = Buffer.alloc(headerSize + bytes.length)
      if (headerSize === 16) {
        file.writeBigUInt64LE(BigInt(bytes.length), 0)
        file.writeInt32LE(1, 8)
      } else {
        file.writeUInt32LE(bytes.length, 0)
        file.writeInt32LE(1, 4)
      }
      bytes.copy(file, headerSize)
      fs.writeFileSync(filename, file, { flag: 'wx' })
      fixtureExists = true
    }
  }

  try {
    assert.equal(typeof sharedMemory.copyMemoryToBuffer, 'function')
    assert.equal(typeof sharedMemory.deleteCopyCache, 'function')
    writeFixture(payload)

    const backing = Buffer.alloc(payload.length + 16, 0xa5)
    const target = backing.subarray(8, 8 + payload.length)
    assert.equal(sharedMemory.copyMemoryToBuffer(key, target), payload.length)
    assert.deepEqual(target, payload)
    assert.deepEqual(backing.subarray(0, 8), Buffer.alloc(8, 0xa5))
    assert.deepEqual(backing.subarray(-8), Buffer.alloc(8, 0xa5))

    const arrayBuffer = new ArrayBuffer(payload.length + 16)
    new Uint8Array(arrayBuffer).fill(0x5c)
    const typed = new Uint32Array(arrayBuffer, 8, payload.length / 4)
    assert.equal(sharedMemory.copyMemoryToBuffer(key, typed), payload.length)
    assert.deepEqual(Buffer.from(arrayBuffer, 8, payload.length), payload)
    assert.deepEqual(Buffer.from(arrayBuffer, 0, 8), Buffer.alloc(8, 0x5c))
    assert.deepEqual(Buffer.from(arrayBuffer, payload.length + 8), Buffer.alloc(8, 0x5c))
    const plain = new ArrayBuffer(payload.length)
    assert.equal(sharedMemory.copyMemoryToBuffer(key, plain), payload.length)
    assert.deepEqual(Buffer.from(plain), payload)

    const small = Buffer.alloc(8, 0xcd)
    assert.throws(() => sharedMemory.copyMemoryToBuffer(key, small), error => {
      assert(error instanceof RangeError)
      assert.equal(error.requiredSize, payload.length)
      return true
    })
    assert.deepEqual(small, Buffer.alloc(8, 0xcd))
    assert.equal(sharedMemory.copyMemoryToBuffer(key, small, 5), 5)
    assert.deepEqual(small.subarray(0, 5), payload.subarray(0, 5))
    assert.deepEqual(small.subarray(5), Buffer.alloc(3, 0xcd))
    assert.equal(sharedMemory.copyMemoryToBuffer(key, small, 0), 0)

    for (const length of [-1, 0.5, NaN, Infinity, payload.length + 1]) {
      assert.throws(() => sharedMemory.copyMemoryToBuffer(key, target, length), RangeError)
    }
    assert.throws(() => sharedMemory.copyMemoryToBuffer(key, target, '8'), TypeError)
    assert.throws(() => sharedMemory.copyMemoryToBuffer(key, {}), TypeError)
    assert.throws(() => sharedMemory.copyMemoryToBuffer(key, Buffer.alloc(0)), RangeError)
    assert.throws(() => sharedMemory.copyMemoryToBuffer('../invalid', target), TypeError)
    assert.throws(() => sharedMemory.deleteCopyCache('../invalid'), TypeError)
    assert.throws(() => sharedMemory.deleteCopyCache(), TypeError)
    assert.equal(sharedMemory.copyMemoryToBuffer(missingKey, target), null)
    assert.equal(sharedMemory.deleteCopyCache(missingKey), undefined)

    // A second call must observe writes to the same shared mapping.
    const changed = Buffer.alloc(payload.length, 0x3c)
    if (process.platform === 'win32') {
      sharedMemory.setMemoryByAddress(handle, changed)
    } else {
      const fd = fs.openSync(filename, 'r+')
      try { fs.writeSync(fd, changed, 0, changed.length, fs.statSync(filename).size - changed.length) }
      finally { fs.closeSync(fd) }
    }
    assert.equal(sharedMemory.copyMemoryToBuffer(key, target), payload.length)
    assert.deepEqual(target, changed)

    // Copies remain independent of the mapping. Clearing a reader cache must
    // not remove a producer's file and must allow repeated reads afterward.
    sharedMemory.deleteCopyCache(key)
    sharedMemory.deleteCopyCache(key)
    assert.deepEqual(target, changed)
    if (process.platform !== 'win32') assert(fs.existsSync(filename))
    assert.equal(sharedMemory.copyMemoryToBuffer(key, target), changed.length)

    // Replacing the backing object retains the cached generation until reset.
    if (process.platform !== 'win32') { fs.unlinkSync(filename); fixtureExists = false }
    const replacement = Buffer.alloc(payload.length * 2, 0x7e)
    writeFixture(replacement)
    assert.equal(sharedMemory.copyMemoryToBuffer(key, target), changed.length)
    assert.deepEqual(target, changed)
    sharedMemory.deleteCopyCache(key)
    assert.throws(() => sharedMemory.copyMemoryToBuffer(key, target), error => error.requiredSize === replacement.length)
    const larger = Buffer.alloc(replacement.length)
    assert.equal(sharedMemory.copyMemoryToBuffer(key, larger), replacement.length)
    assert.deepEqual(larger, replacement)

    // Linux legacy views must survive clearing the independent copy cache.
    if (process.platform === 'linux' && !process.versions.electron) {
      const legacy = new Uint8Array(sharedMemory.getMemory(key))
      sharedMemory.deleteCopyCache(key)
      legacy[0] = 0x42
      assert.equal(sharedMemory.copyMemoryToBuffer(key, larger), replacement.length)
      assert.equal(larger[0], 0x42)
    }
    return { ok: true, platform: process.platform, electron: process.versions.electron || null,
      checks: ['buffer-offset', 'typed-array-offset', 'array-buffer', 'required-size-retry',
        'partial-copy', 'invalid-arguments', 'missing-key', 'frame-update', 'cache-reset', 'replacement-generation'] }
  } finally {
    sharedMemory.deleteCopyCache(key)
    if (process.platform === 'win32') sharedMemory.removeMemory(key)
    else if (fixtureExists) fs.unlinkSync(filename)
  }
}

module.exports = run
if (require.main === module) {
  const modulePath = process.env.SKYLINE_SHARED_MEMORY_MODULE || [
    path.join(__dirname, '..', 'build', 'Release', 'sharedMemory.node'),
    path.join(__dirname, '..', 'build', 'sharedMemory.node'),
  ].find(candidate => fs.existsSync(candidate))
  if (!modulePath) throw new Error('sharedMemory.node build output was not found')
  console.log(JSON.stringify(run(modulePath)))
}
