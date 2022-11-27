# PARSER STAGE 实验报告

<center>邢竞择 2020012890</center>

## 实验内容

+ 添加了 `INT` 类型的解析函数
+ 添加了 `Statement` 中对 If、Block、EmptyStmt 的解析
+ 添加了变量定义中初始化值的解析
+ 添加了 `ReturnStmt` 的解析函数
+ 添加了 `If` 块的解析函数
+ 添加了赋值语句的解析函数
+ 添加了 `AND` `Equality` 的解析

## 思考题

### 1

> additive : multiplicative tmp
> tmp : '+' multiplicative tmp | '-' multiplicative tmp | $\varepsilon$

### 2

对任意一个解析函数，当它递归调用另一个解析函数时，若发现返回了空指针，则不断跳过接下来的 `token` 流，直到遇到一个可以接收的 `token`，再尝试进行正常的解析。