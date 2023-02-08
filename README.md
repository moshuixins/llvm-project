# Hikari-LLVM14

A fork of HikariObfuscator [WIP]

## 原项目链接

[https://github.com/HikariObfuscator/Hikari](https://github.com/HikariObfuscator/Hikari)

## 使用

下载后编译

### Swift 混淆支持

编译 Swift Toolchain 的时间非常长。可以使用[Hanabi](https://github.com/NeHyci/Hanabi)

需要注意的是添加混淆参数的位置是在**Swift Compiler - Other Flags**中的**Other Swift Flags**，并且是在前面加-Xllvm，而不是-mllvm。
关闭优化的地方在**Swift Compiler - Code Generation**中的**Optimization Level**，设置为 _No Optimization [-Onone]_

每次编译前需要先 Shift+Command+K(Clean Build Folder)，因为 Swift 并不会像 OC 一样检测到项目 cflag 的修改就会重新编译

### 混淆选项

-aesSeed

指定 cryptoutils 的随机数生成种子。默认为 0x1337

-enable-allobf

同时启用 AntiClassDump, BogusControlFlow(虚假控制流), Flattening(控制流平坦化), FunctionCallObfusate(混淆函数调用), FunctionWrapper(封装函数调用), IndirectBranch(间接跳转), SplitBasicBlocks(切割基本块), StringEncryption(字符串加密), Substitution(指令替换)。默认关闭

#### AntiClassDump

-enable-acdobf

启用 AntiClassDump。默认关闭

-acd-use-initialize

将动态注册代码添加到+initialize。默认开启

-acd-rename-methodimp

重命名在 IDA 中显示的方法函数(修改为 ACDMethodIMP)。默认关闭

#### FunctionCallObfuscate

-enable-fco

启用 FunctionCallObfuscate。默认关闭

-fcoconfig

FunctionCallObfuscate 的配置文件路径，参照 Hikari 原项目的 wiki

#### AntiHooking (修改过)

整体开启这个功能会使生成的二进制文件大小急剧膨胀，建议只在部分函数开启这个功能(toObfuscate)

支持检测 Objective-C 运行时 Hook。如果检测到就会调用 AHCallBack 函数(从 PreCompiled IR 获取)，如果不存在 AHCallBack，就会退出程序。

目前只支持 arm64，在函数中插入代码检测当前函数是否被 Hook，如果检测到就会调用 AHCallBack 函数(从 PreCompiled IR 获取)，如果不存在 AHCallBack，就会退出程序。

PreCompiled IR 是指自定义的 LLVM Bitcode 文件，可以通过在存在回调函数的源文件的编译命令(C Flags)中加上`-emit-llvm`生成，然后放到指定位置即可

-enable-antihook

启用 AntiHooking。默认关闭

-ah_antirebind

使生成的文件无法被 fishhook 重绑定符号

-adhexrirpath

AntiHooking PreCompiled IR 文件的路径

#### AntiDebugging (修改过)

自动在函数中进行反调试，如果有 InitADB 和 ADBCallBack 函数(从 PreCompiled IR 获取)，就会调用 ADBInit 函数，如果不存在 InitADB 和 ADBCallBack 函数并且是 Apple ARM64 平台，就会自动在 void 返回类型的函数中插入内联汇编反调试，否则不做处理。

-enable-adb

启用 AntiDebugging。默认关闭

-adb_prob

每个函数被添加反调试的概率。默认为 40

-adbextirpath

AntiDebugging PreCompiled IR 文件的路径

#### StringEncryption (修改过)

-enable-strcry

启用 StringEncryption。默认关闭

#### SplitBasicBlocks

-enable-splitobf

启用 SplitBasicBlocks。默认关闭

-split_num

每个基本块切割的数量。默认为 2

#### BogusControlFlow (修改过)

-enable-bcfobf

启用 BogusControlFlow。默认关闭

-bcf_prob

每个基本块被添加虚假控制流的概率。默认为 70

-bcf_loop

虚假控制流在每个函数混淆的次数。默认为 1

-bcf_cond_compl

生成分支条件的表达式复杂程度。默认为 3

-bcf_junkasm

在虚假块中插入花指令，干扰 IDA 对函数的识别。默认关闭

-bcf_junkasm_minnum

在虚假块中花指令的最小数量。默认为 1

-bcf_junkasm_maxnum

在虚假块中花指令的最大数量。默认为 3

-bcf_createfunc

使用函数封装不透明谓词。默认关闭

#### Flattening (修改过)

经过修改，支持混淆存在 C++异常处理的函数

-enable-cffobf

启用 Flattening。默认关闭

#### Substitution (修改过)

-enable-subobf

启用 Substitution。默认关闭

-sub_loop

Substitution 在每个函数混淆的次数。默认为 1

-sub_prob

每个指令被 Substitution 混淆的概率。默认为 50

#### ConstantEncryption (修改过)

修改自https://iosre.com/t/llvm-llvm/11132

-enable-constenc

启用 ConstantEncryption。默认关闭

-constenc_times

ConstantEncryption 在每个函数混淆的次数。默认为 1

-constenc_prob

每个指令被 ConstantEncryption 混淆的概率。默认为 50

-constenc_togv

将数字常量替换为全局变量，以对抗反编译器自动简化表达式。默认关闭

-constenc_subxor

替换 ConstantEncryption 的 XOR 运算，使其变得更加复杂

#### IndirectBranch (修改过)

-enable-indibran

启用 IndirectBranch。默认关闭

-indibran-use-stack

将跳转表地址和索引加载到栈中，再从栈中读取。默认关闭

-indibran-enc-jump-target

加密跳转表和索引。默认关闭

#### FunctionWrapper(修改过)

经过修改，支持混淆存在值传递(passed by value)的函数

-enable-funcwra

启用 FunctionWrapper。默认关闭

-fw_prob

每个函数调用被 FunctionWrapper 混淆的概率。默认为 30

-fw_times

FunctionWrapper 在每个函数调用混淆的次数。默认为 2
