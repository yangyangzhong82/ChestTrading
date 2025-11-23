#pragma once

struct Config {
    int  version                                         = 1;
    bool enableWalMode                                   = true;  // 是否启用数据库的 WAL 模式
    int  floatingTextUpdateIntervalSeconds = 1; // 悬浮字更新间隔，单位秒
    int  databaseThreadPoolSize            = 4;   // 数据库线程池线程数量
};
