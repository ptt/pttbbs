def _impl_cc_flatbuffers_compile(ctx):
    print('files:', ctx.files.src, 'outdir:', ctx.outputs.out.dirname)
    args = ["-c"] + ["-o", ctx.outputs.out.dirname, "--filename-suffix", ".fbs"] + [f.path for f in ctx.files.src]

    print('args:', args)

    infile = ctx.files.src[0]

    ctx.actions.run(
        inputs=ctx.files.src,
        outputs=[ctx.outputs.out],
        arguments=args,
        progress_message="flatc $(infile.path)",
        # command="flatc --cpp -o $(ctx.outputs.out.dirname) --filename-suffix .fbs $(infile.path)",
        executable = "/usr/bin/flatc",
    )

    compilation_context = cc_common.create_compilation_context(headers=depset([ctx.outputs.out]))

    return [CcInfo(compilation_context=compilation_context)]


cc_flatbuffers_compile = rule(
    implementation = _impl_cc_flatbuffers_compile,
    attrs = {
        "src": attr.label(allow_files = True),
        "out": attr.output(mandatory = True),
    },
)
