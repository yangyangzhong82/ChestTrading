#pragma once

struct Config {
    int  version       = 1;
    bool enableWalMode = true; // 是否启用数据库的 WAL 模式
};
