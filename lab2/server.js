const express = require('express');
const path = require('path');
const app = express();

// 设置 IP 地址和端口
const HOST = '127.0.0.1';
const PORT = 3000;

// 将 `public` 文件夹设为静态文件目录
app.use(express.static(path.join(__dirname, 'public')));

// 将 Parcel 作为中间件
const Bundler = require('parcel-bundler');
const bundler = new Bundler('public/index.html'); // 指定入口文件为 `public/index.html`
app.use(bundler.middleware());

// 启动服务器
app.listen(PORT, HOST, () => {
    console.log(`Server is running on http://${HOST}:${PORT}`);
});
