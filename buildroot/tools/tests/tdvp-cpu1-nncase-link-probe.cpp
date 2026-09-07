// SPDX-License-Identifier: MIT
// Build-only dependency probe. Not installed or executed by the image builder.
#include <cstdio>
#include <istream>
#include <vector>
#include <nncase/version.h>
#include <nncase/runtime/interpreter.h>
#include <nncase/runtime/runtime_tensor.h>
#include <nncase/runtime/runtime_op_utility.h>
#include <nncase/functional/ai2d/ai2d_builder.h>

// Non-static references force real model/tensor/AI2D dependencies into the
// executable (no --gc-sections). main deliberately calls none of them.
void tdvp_link_nncase_model(std::istream &stream)
{
    using namespace nncase;
    using namespace nncase::runtime;
    interpreter interpreter;
    interpreter.load_model(stream).expect("compile-only model reference");
    auto input = host_runtime_tensor::create(typecode_t::dt_uint8, dims_t{1, 3, 8, 8},
                                             hrt::pool_shared).expect("compile-only tensor reference");
    hrt::sync(input, sync_op_t::sync_write_back, true).unwrap();
    std::vector<value_t> inputs{input.impl()};
    interpreter.entry_function().expect("compile-only entry reference")->invoke(inputs).unwrap();
}

void tdvp_link_nncase_ai2d(nncase::F::k230::ai2d_builder &builder,
                          nncase::runtime::runtime_tensor &input,
                          nncase::runtime::runtime_tensor &output)
{
    builder.build_schedule().unwrap();
    builder.invoke(input, output).unwrap();
}

int main(void)
{
    std::printf("nncase %s CPU1 link probe only; no model or hardware operation\n", NNCASE_VERSION);
    return 0;
}
