	.text
	.file	"test1.c"
	.globl	main                            # -- Begin function main
	.p2align	4, 0x90
	.type	main,@function
main:                                   # @main
	.cfi_startproc
# %bb.0:
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset %rbp, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register %rbp
	subq	$432, %rsp                      # imm = 0x1B0
	movl	$0, -20(%rbp)
	movl	$2, -12(%rbp)
	movl	$10, -8(%rbp)
	movl	$0, -4(%rbp)
.LBB0_1:                                # =>This Inner Loop Header: Depth=1
	cmpl	$100, -4(%rbp)
	jge	.LBB0_4
# %bb.2:                                #   in Loop: Header=BB0_1 Depth=1
	movl	-4(%rbp), %eax
	movslq	-4(%rbp), %rcx
	movl	%eax, -432(%rbp,%rcx,4)
# %bb.3:                                #   in Loop: Header=BB0_1 Depth=1
	movl	-4(%rbp), %eax
	addl	$1, %eax
	movl	%eax, -4(%rbp)
	jmp	.LBB0_1
.LBB0_4:
	movl	-12(%rbp), %eax
	cmpl	-8(%rbp), %eax
	jge	.LBB0_6
# %bb.5:
	movl	-12(%rbp), %eax
	addl	-8(%rbp), %eax
	movl	%eax, -16(%rbp)
	jmp	.LBB0_7
.LBB0_6:
	movl	-12(%rbp), %eax
	subl	-8(%rbp), %eax
	movl	%eax, -16(%rbp)
.LBB0_7:
	movl	-16(%rbp), %esi
	movabsq	$.L.str, %rdi
	movb	$0, %al
	callq	printf@PLT
	xorl	%eax, %eax
	addq	$432, %rsp                      # imm = 0x1B0
	popq	%rbp
	.cfi_def_cfa %rsp, 8
	retq
.Lfunc_end0:
	.size	main, .Lfunc_end0-main
	.cfi_endproc
                                        # -- End function
	.type	.L.str,@object                  # @.str
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str:
	.asciz	"The value of z is: %d\n"
	.size	.L.str, 23

	.ident	"clang version 16.0.6 (https://github.com/llvm/llvm-project.git 7cbf1a2591520c2491aa35339f227775f4d3adf6)"
	.section	".note.GNU-stack","",@progbits
