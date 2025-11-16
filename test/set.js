const sharedMemory = require('../build/sharedMemory.node');
const sleep = (ms) => new Promise(resolve => setTimeout(resolve, ms));

process.on('uncaughtException', (err) => {
    console.error('未捕获的异常:', err);
    process.exit(1);
});

(async () => {
    try {
        await sleep(1000);
        const key = "2124";
        const length = 4081; // 共享内存的大小（字节）
        
        console.info('-------set--------')
        // 创建共享内存
        let result = sharedMemory.setMemory(key, length);
        if (!result || !result.byteLength) {
            throw new Error('共享内存创建失败');
        }
        let sharedBufferView = new Uint8Array(result)
        
        console.info('------print initial state---------')
        console.log('Buffer length:', sharedBufferView.length, result.byteLength);
        console.log('Initial data (first 10 bytes):', sharedBufferView.slice(0, 10));
        
        console.info('------write---------')
        try {
            // 修改共享内存中的数据，只写入前100个字节
            console.log('开始写入数据...');
            for (let i = 0; i < sharedBufferView.length; i++) {
                // console.log(`已写入 ${i} 字节...`);
                sharedBufferView[i] = i % 256; // 填充一些数据
            }
            console.info('写入数据成功', 4080, sharedBufferView[4080], sharedBufferView[4081]);
        } catch (writeError) {
            console.error('写入数据失败:', writeError);
            console.error('错误堆栈:', writeError.stack);
            throw writeError;
        }
        
        console.info('------print after write---------')
        console.log('Data after write (first 10 bytes):', sharedBufferView.slice(0, 10));
        result = undefined;
        sharedBufferView = undefined;
        console.info('-------sleep 10s--------')
        await sleep(10000); // 减少等待时间
        
        // 清理共享内存
        console.info('-------cleanup--------')
        try {
            const removed = sharedMemory.removeMemory(key);
            console.log('共享内存已清理:', removed);
            try{
                console.info('mem: ', sharedMemory.getMemory(key))
            }
            catch(e){
                console.info('无法获取共享内存，清理成功:', e);
            }
        } catch (cleanupError) {
            console.error('清理共享内存失败:', cleanupError);
            console.error('错误堆栈:', cleanupError.stack);
            throw cleanupError;
        }
    } catch (error) {
        console.error('操作失败:', error);
        console.error('错误堆栈:', error.stack);
        process.exit(1);
    }
})();
