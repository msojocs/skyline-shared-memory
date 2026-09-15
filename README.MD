# Skyline-SharedMemory

适用于微信开发者工具的共享内存模块。

Electron 显示层可使用 `copyMemoryToBuffer(key, target[, length])`，将共享内存的数据区复制到 JS 分配的 `Buffer`、`TypedArray` 或 `ArrayBuffer`，避免创建 external ArrayBuffer。Linux 和 Windows 均提供该接口。

```js
let target = Buffer.alloc(8 + width * height * 4)
try {
  sharedMemory.copyMemoryToBuffer(key, target)
} catch (error) {
  if (!Number.isInteger(error.requiredSize)) throw error
  target = Buffer.alloc(error.requiredSize)
  sharedMemory.copyMemoryToBuffer(key, target)
}
// 后续帧需要再次 copy；返回的缓冲区是快照。
sharedMemory.deleteCopyCache(key)
```

- 返回复制的字节数；无法打开共享内存时返回 `null`。
- 默认复制整个数据区，包含 Skyline 的帧头，不包含本模块的外层 `SharedMemoryHeader`。`length` 可以指定部分复制，单位为字节。
- 目标视图的偏移和长度会被遵守；容量不足抛出带 `requiredSize` 的 `RangeError`，不修改目标数据。
- `deleteCopyCache(key)` 释放该 key 的读取缓存，不删除生产者的共享内存文件。下一次调用重新打开映射。
- Linux 的复制缓存独立于旧 `getMemory()` / `setMemory()` 视图，清理它不会使旧视图失效。旧接口仍保留 external ArrayBuffer 行为，Electron 显示层应使用复制接口。
- 同名文件被替换后，缓存仍引用原映射，需先调用 `deleteCopyCache`。复制不提供跨进程写入的原子帧保证，调用方仍需遵守生产者的帧通知协议。

构建后可运行 `node test/copy-memory.js`。测试使用独立共享内存 key，并清理测试文件；也可在已有 Electron 进程中调用 `require('./test/copy-memory.js')(addonAbsolutePath)`，验证 Electron 的 V8 内存限制下的行为。
