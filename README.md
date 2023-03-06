# ChatGPT-web

C++高性能服务器搭建ChatGPT服务

## 介绍

融合当下热门的ChatGPT搭建智能问答服务，前端使用LiteWebChat框架并基于gpt-3.5-turbo API，后端为C++实现的高性能服务器。

<br />

## 功能

- 使用LiteWebChat框架编写ChatGPT对话逻辑，调用gpt-3.5-turbo API得到问答结果；
- 使用非阻塞socket + epoll与线程池实现多线程的Reactor高并发模型；
- 使用正则与状态机解析HTTP请求报文，实现处理静态资源的请求；
- 基于小根堆实现的定时器，关闭超时的非活动连接；
- 利用单例模式与阻塞队列实现异步的日志系统，记录服务器运行状态；

<br />

## 环境要求

- Linux
- C++14

<br />

## 项目部署

**前端**

将`resources`中`chat.html`中修改`142`行为自己的`Openai Api Key`；

<br />

**后端**

根目录下运行以下命令

```sh
make
./bin/server
```

<br />

## 单元测试

```sh
cd test
make
./test
```

<br />

## 压力测试

```sh
./webbench-1.5/webbench -c 100 -t 10 http://ip:port/
./webbench-1.5/webbench -c 1000 -t 10 http://ip:port/
./webbench-1.5/webbench -c 5000 -t 10 http://ip:port/
./webbench-1.5/webbench -c 10000 -t 10 http://ip:port/
```

<br />

## 致谢

Linux高性能服务器编程，游双著.

[markparticle](https://github.com/markparticle/WebServer)、[MorFansLab](https://github.com/MorFansLab/LiteWebChat_Frame)



