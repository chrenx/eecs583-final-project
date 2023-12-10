	.text
	.file	"hw2correct6.c"
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
	pushq	%rbx
	subq	$104, %rsp
	.cfi_offset %rbx, -24
	movl	$0, -24(%rbp)
	leaq	-112(%rbp), %rax
	movabsq	$.L__const.main.A, %rbx
	movq	%rax, %rdi
	movq	%rbx, %rsi
	movl	$40, %edx
	callq	memcpy@PLT
	leaq	-64(%rbp), %rax
	movq	%rax, %rdi
	xorl	%esi, %esi
	movl	$40, %edx
	callq	memset@PLT
	movl	$37, -16(%rbp)
	movl	$0, -20(%rbp)
	movl	$0, -12(%rbp)
.LBB0_1:                                # =>This Inner Loop Header: Depth=1
	cmpl	$10, -12(%rbp)
	jge	.LBB0_9
# %bb.2:                                #   in Loop: Header=BB0_1 Depth=1
	movl	-16(%rbp), %eax
	shll	$1, %eax
	movslq	-20(%rbp), %rbx
	imull	$23, -112(%rbp,%rbx,4), %ebx
	addl	%ebx, %eax
	addl	-12(%rbp), %eax
	movslq	-12(%rbp), %rbx
	movl	%eax, -64(%rbp,%rbx,4)
	movl	-12(%rbp), %eax
	movl	$7, %ebx
	cltd
	idivl	%ebx
	movl	%edx, %eax
	cmpl	$0, %eax
	jne	.LBB0_7
# %bb.3:                                #   in Loop: Header=BB0_1 Depth=1
	movl	-12(%rbp), %eax
	movl	$2, %ebx
	cltd
	idivl	%ebx
	movl	%edx, %eax
	cmpl	$1, %eax
	jne	.LBB0_5
# %bb.4:                                #   in Loop: Header=BB0_1 Depth=1
	movl	-12(%rbp), %eax
	movl	%eax, -20(%rbp)
	jmp	.LBB0_6
.LBB0_5:                                #   in Loop: Header=BB0_1 Depth=1
	movl	-12(%rbp), %eax
	addl	$1, %eax
	movl	%eax, -16(%rbp)
.LBB0_6:                                #   in Loop: Header=BB0_1 Depth=1
	jmp	.LBB0_7
.LBB0_7:                                #   in Loop: Header=BB0_1 Depth=1
	movslq	-12(%rbp), %rax
	movl	-64(%rbp,%rax,4), %eax
	movabsq	$.L.str, %rbx
	movq	%rbx, %rdi
	movl	%eax, %esi
	movb	$0, %al
	callq	printf@PLT
# %bb.8:                                #   in Loop: Header=BB0_1 Depth=1
	movl	-12(%rbp), %eax
	addl	$1, %eax
	movl	%eax, -12(%rbp)
	jmp	.LBB0_1
.LBB0_9:
	xorl	%eax, %eax
	addq	$104, %rsp
	popq	%rbx
	popq	%rbp
	.cfi_def_cfa %rsp, 8
	retq
.Lfunc_end0:
	.size	main, .Lfunc_end0-main
	.cfi_endproc
                                        # -- End function
	.type	.L__const.main.A,@object        # @__const.main.A
	.section	.rodata,"a",@progbits
	.p2align	4, 0x0
.L__const.main.A:
	.long	0                               # 0x0
	.long	1                               # 0x1
	.long	2                               # 0x2
	.long	3                               # 0x3
	.long	4                               # 0x4
	.long	5                               # 0x5
	.long	6                               # 0x6
	.long	7                               # 0x7
	.long	8                               # 0x8
	.long	9                               # 0x9
	.size	.L__const.main.A, 40

	.type	.L.str,@object                  # @.str
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str:
	.asciz	"%d\n"
	.size	.L.str, 4

	.ident	"clang version 16.0.6 (https://github.com/llvm/llvm-project.git 7cbf1a2591520c2491aa35339f227775f4d3adf6)"
	.section	".note.GNU-stack","",@progbits
