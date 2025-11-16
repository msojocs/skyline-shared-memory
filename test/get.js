const sharedMemory = require('../build/sharedMemory.node');
const key = "2124";

try {
    // 获取共享内存
    console.info('-------get--------')
    const result = sharedMemory.getMemory(key);
    if (!result || !result.byteLength) {
        throw new Error('获取共享内存失败或共享内存为空');
    }
    
    const sharedBufferView = new Uint8Array(result)
    console.info('------print data---------')
    console.log('Buffer length:', sharedBufferView.length, sharedBufferView.byteLength);
    console.log('Data:', sharedBufferView);
    
    // 验证数据
    console.info('------verify data---------')
    let isValid = true;
    for (let i = 0; i < sharedBufferView.length; i++) {
        // console.info('verify', i, i % 256)
        if (sharedBufferView[i] !== i % 256) {
            console.error(`数据验证失败: 位置 ${i} 的值为 ${sharedBufferView[i]}，期望值为 ${i % 256}`);
            isValid = false;
            break;
        }
        // console.info('verify', i, 'end')
    }
    if (isValid) {
        console.log('数据验证成功');
    }
    
    // 清理共享内存
    console.info('-------cleanup--------')
    try {
        const removed = sharedMemory.removeMemory(key);
        console.log('共享内存已清理:', removed);
    } catch (cleanupError) {
        console.error('清理共享内存失败:', cleanupError.message);
        throw cleanupError;
    }
} catch (error) {
    console.error('Get 操作失败:', error.message);
    process.exit(1);
}