
# SYNOPSIS
#	void fakelabel(Label *env, void (*fp), void *stackbp, int stacksize)

 	.text
	.align	6
	.global	fakelabel
fakelabel:
	stqd	$4, 0*16($3)
	rotqbyi $126, $126, 12
	il	$7, -32
	a	$6, $7, $6
	a	$5, $6, $5
	stqd	$5, 1*16($3)
	bi	$0

