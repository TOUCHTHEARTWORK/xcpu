# --------------------------------------------------------------  
# (C)Copyright 2001,2006,                                         
# International Business Machines Corporation,                    
# Sony Computer Entertainment, Incorporated,                      
# Toshiba Corporation,                                            
#                                                                 
# All Rights Reserved.                                            
# --------------------------------------------------------------  
# PROLOG END TAG zYx                                              
	# This file contains a sample implementation of the setlabel routine used
	# to save the current SPU execution context. The execution context does
	# not include the floating pointer status register, MFC (memory flow
	# controller) state, or mailbox state.
	#
	# NOTE:	 This assembly code may not be fully optimized for performance.
	#
	# SYNOPSIS
	#	int setlabel(jmp_buf env)
		
	
 	.text
	.align	6
		
	.global	setlabel
setlabel:
	# save all the non-volatile registers
	stqd	$80, 2*16($3)
	stqd	$81, 3*16($3)
	stqd	$82, 4*16($3)
	stqd	$83, 5*16($3)
	stqd	$84, 6*16($3)
	stqd	$85, 7*16($3)
	stqd	$86, 8*16($3)
	stqd	$87, 9*16($3)
	stqd	$88, 10*16($3)
	stqd	$89, 11*16($3)

	stqd	$90, 12*16($3)
	stqd	$91, 13*16($3)
	stqd	$92, 14*16($3)
	stqd	$93, 15*16($3)
	stqd	$94, 16*16($3)
	stqd	$95, 17*16($3)
	stqd	$96, 18*16($3)
	stqd	$97, 19*16($3)
	stqd	$98, 20*16($3)
	stqd	$99, 21*16($3)

	stqd	$100, 22*16($3)
	stqd	$101, 23*16($3)
	stqd	$102, 24*16($3)
	stqd	$103, 25*16($3)
	stqd	$104, 26*16($3)
	stqd	$105, 27*16($3)
	stqd	$106, 28*16($3)
	stqd	$107, 29*16($3)
	stqd	$108, 30*16($3)
	stqd	$109, 31*16($3)

	stqd	$110, 32*16($3)
	stqd	$111, 33*16($3)
	stqd	$112, 34*16($3)
	stqd	$113, 35*16($3)
	stqd	$114, 36*16($3)
	stqd	$115, 37*16($3)
	stqd	$116, 38*16($3)
	stqd	$117, 39*16($3)
	stqd	$118, 40*16($3)
	stqd	$119, 41*16($3)

	hbr	setlabel_ret, $0
	lnop			# pipe1 bubble added for instruction fetch
	
	stqd	$120, 42*16($3)
	stqd	$121, 43*16($3)
	stqd	$122, 44*16($3)
	stqd	$123, 45*16($3)
	stqd	$124, 46*16($3)
	stqd	$125, 47*16($3)
	stqd	$126, 48*16($3)
	stqd	$127, 49*16($3)

	# save the current stack and link register
	stqd	$0, 0*16($3)
	stqd	$1, 1*16($3)
	
	# always return 0
	
	il	$3, 0	
setlabel_ret:		
	bi	$0
