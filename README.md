# supervise

用于监控程序运行，如果运行失败拉起程序

<!-- vim-markdown-toc GFM -->

* [1 特性](#1-特性)
* [2 快速开始](#2-快速开始)
    * [2.1 配置文件 supervise.conf](#21-配置文件-superviseconf)
    * [2.2 状态目录](#22-状态目录)
    * [2.3 运行](#23-运行)
        * [2.3.1 编译产出](#231-编译产出)
        * [2.3.2 使用](#232-使用)
* [3 模块工具原理](#3-模块工具原理)
* [4 Supervise 使用的文件](#4-supervise-使用的文件)
    * [4.1 lock](#41-lock)
        * [4.1.1 作用](#411-作用)
        * [4.1.2 使用](#412-使用)
    * [4.2 status](#42-status)
        * [4.2.1 作用](#421-作用)
        * [4.2.2 使用](#422-使用)
    * [4.3 svcontrol](#43-svcontrol)
        * [4.3.1 作用](#431-作用)
        * [4.3.2 使用](#432-使用)

<!-- vim-markdown-toc -->
## 1 特性

> * 优点：supervise 可以启动 daemon 程序，对于非 daemon 需要采用 nohup 的方式启动。
> * 缺点：每个进程都要配置自己的 supervise，无法做到统一管理。

## 2 快速开始
### 2.1 配置文件 supervise.conf
每个服务可用一个统一的配置文件，若服务中有不需要新功能的模块可将其配置在 [global] 之前，如本配置文件中的 [transmit]。

要同时使用 [global] 及本身特殊的配置项的模块，其本身的配置段应该在 [global] 之后，如本配置文件中的 [redis]

所有有效项需要顶格写。"["与"模块名"及"模块名"与"]"之间不能有间隔。

报警邮件列表、手机列表若在 [global] 中已配置，则其本身的配置段中不要再配。

> * alarm_interval: 报警间隔
>   * 在 alarm_interval 秒内只在第一次服务重启时报警。以后只写日志不报警
>   * 若该项未配置或配置为 0 则其它配置项均无效。最短时间为 60s，小于 60 按 60 计算。
> * alarm_mail: 报警邮箱，可配置多个，以空格或 tab 分隔
>   * 该行不能超过 2048 字符（包括行尾的空白字符），超过部分忽略。也可为空，为空时不报警。
> * alarm_gsm: 报警手机，最多 10 个，以空格或 tab 分隔。需要权限，暂时没用。
> * max_tries: 最大重启次数
>   * 若指定，则在 alarm_interval 时间内最多重启 max_tries, 超过后 supervise 不再重启服务，supervise 发出告警并退出。
>   * 可为空或 0，为空或 0 时重启次数无限制。
> * max_tries_if_coredumped:
>   * 若指定，则在 alarm_interval 时间内 coredumped 次数达到 max_tries_if_coredumped 后不再重启服务，supervise 发出告警并退出。
>   * 可为空或 0，为空或 0 时重启次数无限制。

```
[someModuleThatNoNeedNewFunction]
[global]
alarm_interval : 60
alarm_mail :
alarm_gsm :
max_tries : 10
max_tries_if_coredumped : 10
```
### 2.2 状态目录
运行时必须要设置一个状态目录，supervise 会把要监控的程序的相关信息和日志都放到这个目录中，目录必须要包含"stauts/module_name"目录，其中`module_name`可以自定义。
这个目录的内容如下：
```
status/xxx/
├── control
├── lock
├── ok
├── status
├── __stdout        // module 标准输出
├── __stderr        // module 标准错误
├── supervise.log
└── supervise.log.wf
```

### 2.3 运行
#### 2.3.1 编译产出
```
$ cd supervise
$ make
```
#### 2.3.2 使用

执行下面的命令，就会启动程序并监控程序
```
supervise -f <要监控的程序启动启动命令> -p <状态目录> -F supervise.conf
```
例如：
* 启动进程：
```
CMD="python app.py"
mkdir -p ./status/some-app
# 状态目录支持绝对路径，如 /home/work/status/some-app
./supervise -p ./status/some-app -f "$CMD"
```

* 杀进程：
```
pid=`od -d --skip-bytes=16 ./status/some-app/status | awk '{print $2}'`
sv_pid=`grep 'PPid' /proc/$pid/status | awk '{print $2}'`
kill -15 $sv_pid
kill -15 $pid
```

## 3 模块工具原理

模块的工作原理实际上很简单，supervise 启动的时候 fork 一个子进程，子进程执行 execvp 系统调用，将自己替换成执行的模块，

模块变成 supervise 的子进程在运行，而 supervise 则死循环运行，并通过 waitpid 或者 wait3 系统调用选择非阻塞的方式去侦听子进程的运行情况，

当然同时也会读取 pipe 文件 svcontrol 的命令，然后根据命令去执行不同的动作，

如果子进程因某种原因导致退出，则 supervise 通过 waitpid 或者 wait3 获知，并继续启动模块，如果模块异常导致无法启动，则会使 supervise 陷入死循环，不断的启动模块

## 4 Supervise 使用的文件

supervise 要求在当前目录下有 status 目录，同时 status 目录下面会有通过 supervise 启动的模块名的目录，而该目录下会有三个文件，分别为 lock status svcontrol, 对应的功能和我们可以从中获取的信息如下

### 4.1 lock
#### 4.1.1 作用

supervise 的文件锁，通过该文件锁去控制并发，防止同一个 status 目录下启动多个进程，造成混乱

#### 4.1.2 使用
```
可以通过 /sbin/fuser 这个命令去获取 lock 文件的使用信息
如果 fuser 返回值为 0, 表示 supervise 已经启动，同时 fuser 返回了 supervise 的进程 pid

已经启动：
$/sbin/fuser lock
lock:                11573

没有启动：
$/sbin/fuser lock
Centos 上测试时，返回空
```
### 4.2 status
#### 4.2.1 作用
这个文件是 supervise 用来记录一些信息，可以简单的理解为 char status[20]
```
status[0]-status[11] 没有用
status[12]-status[14] 是标志位，一般没啥用
status[15] 直接为 0
status[16-19] 记录了 supervise 所启动的子进程的 pid
```
#### 4.2.2 使用

可以直接通过 od 命令去读取该文件，一般有用的就是 od -An -j16 -N4 -tu4 status 可以直接拿到 supervise 所负责的子进程的 pid

这个 pid 用四个字节进行存储（Linux 为小头字节序－即低字节在前－高字节在后）
```
static void pidchange()
{
	struct taia now;
	unsigned long u;

	taia_now(&now);
	taia_pack(status, &now);

	u = (unsigned long) pid;
	status[16] = u; u >>= 8;
	status[17] = u; u >>= 8;
	status[18] = u; u >>= 8;
	status[19] = u;
}
```

od 命令（od 指令会读取所给予的文件的内容，并将其内容以八进制字码呈现出来）

> * -A 字码基数 　选择要以何种基数计算字码。
>   * o：八进制（系统默认值）
>   * d：十进制
>   * x：十六进制
>   * n：不打印位移值
> * -j 字符数目 或 --skip-bytes=字符数目 　略过设置的字符数目。
> * -N 字符数目 或 --read-bytes=字符数目 　到设置的字符数目为止。
> * -t 输出格式 或 --format=输出格式 　设置输出格式。
>   * c：ASCII 字符或反斜杠序列（如、n）
>   * d：有符号十进制数
>   * f：浮点数
>   * o：八进制（系统默认值）
>   * u：无符号十进制数，u2 指以 2 个字节进行输出，u1 的 "1" 表示一次显示一个字节
>   * x：十六进制数

> od 例子
```
$echo "wb" > testfile
$od -tu1  testfile
0000000 119  98  10
0000003
$od -tc  testfile
0000000   w   b  \n
0000003
```

### 4.3 svcontrol
#### 4.3.1 作用
可以理解为一个控制接口，supervise 读取这个管道的信息，进而根据管道的信息去控制子进程，而通过控制子进程的方式实际上就是给子进程发信号，因此实际上跟自己通过 kill 命令给 supervise 所在的子进程发信号从功能上没有本质的区别，但是通过控制接口的方式的话，准确性会非常的高
#### 4.3.2 使用
```
直接写命令到该文件中，如 echo 'd' > svcontorl, 让 supervise 去控制子进程，比较常用的命令如下
    d: 停掉子进程，并且不启动
    u: 相对于 d, 启动子进程
    k: 发送一个 kill 信号，因 kill 之后 supervise 马上又重启，因此可以认为是一个重启的动作
    x: 标志 supervise 退出
```
