# --------------------------------------------------------------  
# (C)Copyright 2001,2006,                                         
# International Business Machines Corporation,                    
# Sony Computer Entertainment, Incorporated,                      
# Toshiba Corporation,                                            
#                                                                 
# All Rights Reserved.                                            
# --------------------------------------------------------------  
# PROLOG END TAG zYx                                              
	# This file contains a sample implementation of the gotolabel routine 
	# used to restore an SPU execution context. The execution context does
	# not include the floating pointer status register, MFC (memory flow
	# controller) state, or mailbox state.
	#
	# SYNOPSIS
	#	void gotolabel(jmp_buf env, int val)
		
	
 	.text
	.align	6
		
	.global	gotolabel
gotolabel:
	# Restore the current toc, stack and link register.
	
	lqd	$0, 0*16($3)
	lqd	$1, 1*16($3)
	
	# restore all the non-volatile registers

	lqd	$80, 2*16($3)
	lqd	$81, 3*16($3)
	lqd	$82, 4*16($3)
	lqd	$83, 5*16($3)
	lqd	$84, 6*16($3)
	lqd	$85, 7*16($3)
	lqd	$86, 8*16($3)
	lqd	$87, 9*16($3)
	lqd	$88, 10*16($3)
	lqd	$89, 11*16($3)

	lnop			# pipe1 bubble added for instruction fetch
	
	lqd	$90, 12*16($3)
	lqd	$91, 13*16($3)
	lqd	$92, 14*16($3)
	lqd	$93, 15*16($3)
	lqd	$94, 16*16($3)
	lqd	$95, 17*16($3)
	lqd	$96, 18*16($3)
	lqd	$97, 19*16($3)
	lqd	$98, 20*16($3)
	lqd	$99, 21*16($3)

	lqd	$100, 22*16($3)
	lqd	$101, 23*16($3)
	lqd	$102, 24*16($3)
	lqd	$103, 25*16($3)
	lqd	$104, 26*16($3)
	lqd	$105, 27*16($3)
	lqd	$106, 28*16($3)
	lqd	$107, 29*16($3)
	lqd	$108, 30*16($3)
	lqd	$109, 31*16($3)

	hbr	gotolabel_ret, $0

	lqd	$110, 32*16($3)
	lqd	$111, 33*16($3)
	lqd	$112, 34*16($3)
	lqd	$113, 35*16($3)
	lqd	$114, 36*16($3)
	lqd	$115, 37*16($3)
	lqd	$116, 38*16($3)
	lqd	$117, 39*16($3)
	lqd	$118, 40*16($3)
	lqd	$119, 41*16($3)

	lqd	$120, 42*16($3)
	lqd	$121, 43*16($3)
	lqd	$122, 44*16($3)
	lqd	$123, 45*16($3)
	lqd	$124, 46*16($3)
	lqd	$125, 47*16($3)
	
	lqd	$126, 48*16($3)
	lqd	$127, 49*16($3)

	il	$3, 1	# always return 1

gotolabel_ret:	
	bi	$0
