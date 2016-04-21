#使用说明
##Server:
###编译：
```
  make
```
###参数：
```  -f: 前台运行```
##Client:
###编译：
```
  编译并安装Python2.7
  ./configure --enable-shared
  make
  make install
  
  编译程序：
  make
```
###参数：
```
  -f: 前台运行
  -r: 初次连接若连接不上就进行重连
  -n: Server ip地址
  -p: Server端口号
  -P: 连接密码
  -L: 启动任务
  --upload-mod: 上传模块文件
  --upload-list:  上传列表文件
  --subproc:  每个任务节点新建子进程个数
  --threads:  每个子进程中的线程个数
  --report-level: 生成的报告详细等级 （1 or 2）
  --load-mod: 加载模块文件
  --load-list:  加载列表文件
  --get-report: 获取任务报告
  --timeo:  设置每个线程执行的超时时间
  --task-abort: 终止任务
```


