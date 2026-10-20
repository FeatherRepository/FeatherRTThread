/*
 * npu_model_stub.c — npu_model.c 未生成时的 weak 占位
 *
 * npu_model.c（6.4MB，Vela 生成）不入 git，重建命令见 README.md。未跑生成链时
 * 本文件提供同名 weak 定义，保证 clone 后 `npu_bench` 仍能编译链接；
 * npu_model.c 一旦存在，其**强**定义自动覆盖这里的 weak 定义。
 *
 * 这些符号在 npu_bench.c 里 extern 声明（npu_bench.c:123-128）。跑 npu_bench
 * 命令前会先查 npu_cmd_stream_len：为 0 时直接提示去跑生成链，不让 NPU 空转。
 */
__attribute__((weak)) const unsigned char npu_cmd_stream[]   = { 0x00 };
__attribute__((weak)) const unsigned int  npu_cmd_stream_len = 0;
__attribute__((weak)) const unsigned char npu_weight_blob[]  = { 0x00 };
__attribute__((weak)) const unsigned int  npu_weights_len    = 0;
__attribute__((weak)) const unsigned int  npu_scratch_len    = 0;
__attribute__((weak)) const unsigned int  npu_input_len      = 0;
