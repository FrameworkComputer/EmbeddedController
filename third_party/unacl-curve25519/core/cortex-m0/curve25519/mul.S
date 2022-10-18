 .syntax unified
 .align	2
	.global	multiply256x256_asm
	.type	multiply256x256_asm, %function
multiply256x256_asm:
	push {r4-r7,lr}
	mov r3, r8
	mov r4, r9
	mov r5, r10
	mov r6, r11
	push {r0-r6}
	mov r12, r0
	mov r10, r2
	mov r11, r1
	adds r0, r2, #0
	//ldm r0!, {r4,r5,r6,r7}
	ldm r0!, {r4,r5}
  adds r0,#8
	ldm r1!, {r2,r3,r6,r7}
	push {r0,r1}
	/////////BEGIN LOW PART //////////////////////
		/////////MUL128/////////////
			//MUL64
			adds r6, r5, #0
			adds r1, r2, #0
			subs r5, r4
			sbcs r0, r0
			eors r5, r0
			subs r5, r0
			subs r1, r3
			sbcs r7, r7
			eors r1, r7
			subs r1, r7
			eors r7, r0
			mov r9, r1
			mov r8, r5
			lsrs r1,r4,#16
			uxth r4,r4
			adds r0, r4, #0
			uxth r5,r2
			lsrs r2,#16
			muls r0,r5//00
			muls r5,r1//10
			muls r4,r2//01
			muls r1,r2//11
			lsls r2,r4,#16
			lsrs r4,r4,#16
			adds r0, r0, r2
			adcs r1,r4
			lsls r2,r5,#16
			lsrs r4,r5,#16
			adds r0, r0, r2
			adcs r1,r4
			lsrs r4, r6,#16
			uxth r6, r6
			uxth r5, r3
			lsrs r3, r3, #16
			adds r2, r6, #0
			muls r2, r5
			muls r5, r4
			muls r6, r3
			muls r3, r4
			lsls r4,r5,#16
			lsrs r5,r5,#16
			adds r2, r2, r4
			adcs r3,r5
			lsls r4,r6,#16
			lsrs r5,r6,#16
			adds r2, r2, r4
			adcs r3,r5
			eors r6, r6
			adds r2, r2, r1
			adcs r3, r6
			mov r1, r9
			mov r5, r8
			mov r8, r0
			lsrs r0, r1,#16
			uxth r1,r1
			adds r4, r1, #0
			lsrs r6,r5,#16
			uxth r5,r5
			muls r1,r5
			muls r4,r6
			muls r5,r0
			muls r0,r6
			lsls r6,r4,#16
			lsrs r4,#16
			adds r1, r1, r6
			adcs r0,r4
			lsls r6,r5,#16
			lsrs r5,#16
			adds r1, r1, r6
			adcs r0,r5
			eors r1,r7
			eors r0,r7
			eors r4, r4
			asrs r7, r7, #1
			adcs r1, r2
			adcs r2, r0
			adcs r7, r4
			mov r0, r8
			adds r1, r1, r0
			adcs r2, r3
			adcs r3, r7
		//////////////////////////
	mov r4, r12
	stm r4!, {r0,r1}
	push {r4}
		push {r0,r1}
		mov r1, r10
		mov r10, r2
		ldm r1, {r0, r1, r4, r5}
		adds r2, r4, #0
		adds r7, r5, #0
		subs r2, r0
		sbcs r7, r1
		sbcs r6, r6
		eors r2, r6
		eors r7, r6
		subs r2, r6
		sbcs r7, r6
		push {r2, r7}
		mov r2, r11
		mov r11, r3
		ldm r2, {r0, r1, r2, r3}
		subs r0, r2
		sbcs r1, r3
		sbcs r7, r7
		eors r0, r7
		eors r1, r7
		subs r0, r7
		sbcs r1, r7
		eors r7, r6
		mov r12, r7
		push {r0, r1}
			//MUL64
			adds r6, r5, #0
			adds r1, r2, #0
			subs r5, r4
			sbcs r0, r0
			eors r5, r0
			subs r5, r0
			subs r1, r3
			sbcs r7, r7
			eors r1, r7
			subs r1, r7
			eors r7, r0
			mov r9, r1
			mov r8, r5
			lsrs r1,r4,#16
			uxth r4,r4
			adds r0, r4, #0
			uxth r5,r2
			lsrs r2,#16
			muls r0,r5//00
			muls r5,r1//10
			muls r4,r2//01
			muls r1,r2//11
			lsls r2,r4,#16
			lsrs r4,r4,#16
			adds r0, r0, r2
			adcs r1,r4
			lsls r2,r5,#16
			lsrs r4,r5,#16
			adds r0, r0, r2
			adcs r1,r4
			lsrs r4, r6,#16
			uxth r6, r6
			uxth r5, r3
			lsrs r3, r3, #16
			adds r2, r6, #0
			muls r2, r5
			muls r5, r4
			muls r6, r3
			muls r3, r4
			lsls r4,r5,#16
			lsrs r5,r5,#16
			adds r2, r2, r4
			adcs r3,r5
			lsls r4,r6,#16
			lsrs r5,r6,#16
			adds r2, r2, r4
			adcs r3,r5
			eors r6, r6
			adds r2, r2, r1
			adcs r3, r6
			mov r1, r9
			mov r5, r8
			mov r8, r0
			lsrs r0, r1,#16
			uxth r1,r1
			adds r4, r1, #0
			lsrs r6,r5,#16
			uxth r5,r5
			muls r1,r5
			muls r4,r6
			muls r5,r0
			muls r0,r6
			lsls r6,r4,#16
			lsrs r4,#16
			adds r1, r1, r6
			adcs r0,r4
			lsls r6,r5,#16
			lsrs r5,#16
			adds r1, r1, r6
			adcs r0,r5
			eors r1,r7
			eors r0,r7
			eors r4, r4
			asrs r7, r7, #1
			adcs r1, r2
			adcs r2, r0
			adcs r7, r4
			mov r0, r8
			adds r1, r1, r0
			adcs r2, r3
			adcs r3, r7
		mov r4, r10
		mov r5, r11
		eors r6, r6
		adds r0, r0, r4
		adcs r1, r5
		adcs r2, r6
		adcs r3, r6
		mov r10, r2
		mov r11, r3
		pop {r2-r5}
		push {r0, r1}
			adds r6, r5, #0
			adds r1, r2, #0
			subs r5, r4
			sbcs r0, r0
			eors r5, r0
			subs r5, r0
			subs r1, r3
			sbcs r7, r7
			eors r1, r7
			subs r1, r7
			eors r7, r0
			mov r9, r1
			mov r8, r5
			lsrs r1,r4,#16
			uxth r4,r4
			adds r0, r4, #0
			uxth r5,r2
			lsrs r2,#16
			muls r0,r5//00
			muls r5,r1//10
			muls r4,r2//01
			muls r1,r2//11
			lsls r2,r4,#16
			lsrs r4,r4,#16
			adds r0, r0, r2
			adcs r1,r4
			lsls r2,r5,#16
			lsrs r4,r5,#16
			adds r0, r0, r2
			adcs r1,r4
			lsrs r4, r6,#16
			uxth r6, r6
			uxth r5, r3
			lsrs r3, r3, #16
			adds r2, r6, #0
			muls r2, r5
			muls r5, r4
			muls r6, r3
			muls r3, r4
			lsls r4,r5,#16
			lsrs r5,r5,#16
			adds r2, r2, r4
			adcs r3,r5
			lsls r4,r6,#16
			lsrs r5,r6,#16
			adds r2, r2, r4
			adcs r3,r5
			eors r6, r6
			adds r2, r2, r1
			adcs r3, r6
			mov r1, r9
			mov r5, r8
			mov r8, r0
			lsrs r0, r1,#16
			uxth r1,r1
			adds r4, r1, #0
			lsrs r6,r5,#16
			uxth r5,r5
			muls r1,r5
			muls r4,r6
			muls r5,r0
			muls r0,r6
			lsls r6,r4,#16
			lsrs r4,#16
			adds r1, r1, r6
			adcs r0,r4
			lsls r6,r5,#16
			lsrs r5,#16
			adds r1, r1, r6
			adcs r0,r5
			eors r1,r7
			eors r0,r7
			eors r4, r4
			asrs r7, r7, #1
			adcs r1, r2
			adcs r2, r0
			adcs r7, r4
			mov r0, r8
			adds r1, r1, r0
			adcs r2, r3
			adcs r3, r7
		pop {r4, r5}
		mov r6, r12
		mov r7, r12
		eors r0, r6
		eors r1, r6
		eors r2, r6
		eors r3, r6
		asrs r6, r6, #1
		adcs r0, r4
		adcs r1, r5
		adcs r4, r2
		adcs r5, r3
		eors r2, r2
		adcs r6,r2
		adcs r7,r2
		pop {r2, r3}
		mov r8, r2
		mov r9, r3
		adds r2, r2, r0
		adcs r3, r1
		mov r0, r10
		mov r1, r11
		adcs r4, r0
		adcs r5, r1
		adcs r6, r0
		adcs r7, r1
	////////END LOW PART/////////////////////
	pop {r0}
	stm r0!, {r2,r3}
	pop {r1,r2}
	push {r0}
	push {r4-r7}
	mov r10, r1
	mov r11, r2
	ldm r1!, {r4, r5}
	ldm r2, {r2, r3}
	/////////BEGIN HIGH PART////////////////
		/////////MUL128/////////////
			//MUL64
			adds r6, r5, #0
			adds r1, r2, #0
			subs r5, r4
			sbcs r0, r0
			eors r5, r0
			subs r5, r0
			subs r1, r3
			sbcs r7, r7
			eors r1, r7
			subs r1, r7
			eors r7, r0
			mov r9, r1
			mov r8, r5
			lsrs r1,r4,#16
			uxth r4,r4
			adds r0, r4, #0
			uxth r5,r2
			lsrs r2,#16
			muls r0,r5//00
			muls r5,r1//10
			muls r4,r2//01
			muls r1,r2//11
			lsls r2,r4,#16
			lsrs r4,r4,#16
			adds r0, r0, r2
			adcs r1,r4
			lsls r2,r5,#16
			lsrs r4,r5,#16
			adds r0, r0, r2
			adcs r1,r4
			lsrs r4, r6,#16
			uxth r6, r6
			uxth r5, r3
			lsrs r3, r3, #16
			adds r2, r6, #0
			muls r2, r5
			muls r5, r4
			muls r6, r3
			muls r3, r4
			lsls r4,r5,#16
			lsrs r5,r5,#16
			adds r2, r2, r4
			adcs r3,r5
			lsls r4,r6,#16
			lsrs r5,r6,#16
			adds r2, r2, r4
			adcs r3,r5
			eors r6, r6
			adds r2, r2, r1
			adcs r3, r6
			mov r1, r9
			mov r5, r8
			mov r8, r0
			lsrs r0, r1,#16
			uxth r1,r1
			adds r4, r1, #0
			lsrs r6,r5,#16
			uxth r5,r5
			muls r1,r5
			muls r4,r6
			muls r5,r0
			muls r0,r6
			lsls r6,r4,#16
			lsrs r4,#16
			adds r1, r1, r6
			adcs r0,r4
			lsls r6,r5,#16
			lsrs r5,#16
			adds r1, r1, r6
			adcs r0,r5
			eors r1,r7
			eors r0,r7
			eors r4, r4
			asrs r7, r7, #1
			adcs r1, r2
			adcs r2, r0
			adcs r7, r4
			mov r0, r8
			adds r1, r1, r0
			adcs r2, r3
			adcs r3, r7
		push {r0,r1}
		mov r1, r10
		mov r10, r2
		ldm r1, {r0, r1, r4, r5}
		adds r2, r4, #0
		adds r7, r5, #0
		subs r2, r0
		sbcs r7, r1
		sbcs r6, r6
		eors r2, r6
		eors r7, r6
		subs r2, r6
		sbcs r7, r6
		push {r2, r7}
		mov r2, r11
		mov r11, r3
		ldm r2, {r0, r1, r2, r3}
		subs r0, r2
		sbcs r1, r3
		sbcs r7, r7
		eors r0, r7
		eors r1, r7
		subs r0, r7
		sbcs r1, r7
		eors r7, r6
		mov r12, r7
		push {r0, r1}
			//MUL64
			adds r6, r5, #0
			adds r1, r2, #0
			subs r5, r4
			sbcs r0, r0
			eors r5, r0
			subs r5, r0
			subs r1, r3
			sbcs r7, r7
			eors r1, r7
			subs r1, r7
			eors r7, r0
			mov r9, r1
			mov r8, r5
			lsrs r1,r4,#16
			uxth r4,r4
			adds r0, r4, #0
			uxth r5,r2
			lsrs r2,#16
			muls r0,r5//00
			muls r5,r1//10
			muls r4,r2//01
			muls r1,r2//11
			lsls r2,r4,#16
			lsrs r4,r4,#16
			adds r0, r0, r2
			adcs r1,r4
			lsls r2,r5,#16
			lsrs r4,r5,#16
			adds r0, r0, r2
			adcs r1,r4
			lsrs r4, r6,#16
			uxth r6, r6
			uxth r5, r3
			lsrs r3, r3, #16
			adds r2, r6, #0
			muls r2, r5
			muls r5, r4
			muls r6, r3
			muls r3, r4
			lsls r4,r5,#16
			lsrs r5,r5,#16
			adds r2, r2, r4
			adcs r3,r5
			lsls r4,r6,#16
			lsrs r5,r6,#16
			adds r2, r2, r4
			adcs r3,r5
			eors r6, r6
			adds r2, r2, r1
			adcs r3, r6
			mov r1, r9
			mov r5, r8
			mov r8, r0
			lsrs r0, r1,#16
			uxth r1,r1
			adds r4, r1, #0
			lsrs r6,r5,#16
			uxth r5,r5
			muls r1,r5
			muls r4,r6
			muls r5,r0
			muls r0,r6
			lsls r6,r4,#16
			lsrs r4,#16
			adds r1, r1, r6
			adcs r0,r4
			lsls r6,r5,#16
			lsrs r5,#16
			adds r1, r1, r6
			adcs r0,r5
			eors r1,r7
			eors r0,r7
			eors r4, r4
			asrs r7, r7, #1
			adcs r1, r2
			adcs r2, r0
			adcs r7, r4
			mov r0, r8
			adds r1, r1, r0
			adcs r2, r3
			adcs r3, r7
		mov r4, r10
		mov r5, r11
		eors r6, r6
		adds r0, r0, r4
		adcs r1, r5
		adcs r2, r6
		adcs r3, r6
		mov r10, r2
		mov r11, r3
		pop {r2-r5}
		push {r0, r1}
			adds r6, r5, #0
			adds r1, r2, #0
			subs r5, r4
			sbcs r0, r0
			eors r5, r0
			subs r5, r0
			subs r1, r3
			sbcs r7, r7
			eors r1, r7
			subs r1, r7
			eors r7, r0
			mov r9, r1
			mov r8, r5
			lsrs r1,r4,#16
			uxth r4,r4
			adds r0, r4, #0
			uxth r5,r2
			lsrs r2,#16
			muls r0,r5//00
			muls r5,r1//10
			muls r4,r2//01
			muls r1,r2//11
			lsls r2,r4,#16
			lsrs r4,r4,#16
			adds r0, r0, r2
			adcs r1,r4
			lsls r2,r5,#16
			lsrs r4,r5,#16
			adds r0, r0, r2
			adcs r1,r4
			lsrs r4, r6,#16
			uxth r6, r6
			uxth r5, r3
			lsrs r3, r3, #16
			adds r2, r6, #0
			muls r2, r5
			muls r5, r4
			muls r6, r3
			muls r3, r4
			lsls r4,r5,#16
			lsrs r5,r5,#16
			adds r2, r2, r4
			adcs r3,r5
			lsls r4,r6,#16
			lsrs r5,r6,#16
			adds r2, r2, r4
			adcs r3,r5
			eors r6, r6
			adds r2, r2, r1
			adcs r3, r6
			mov r1, r9
			mov r5, r8
			mov r8, r0
			lsrs r0, r1,#16
			uxth r1,r1
			adds r4, r1, #0
			lsrs r6,r5,#16
			uxth r5,r5
			muls r1,r5
			muls r4,r6
			muls r5,r0
			muls r0,r6
			lsls r6,r4,#16
			lsrs r4,#16
			adds r1, r1, r6
			adcs r0,r4
			lsls r6,r5,#16
			lsrs r5,#16
			adds r1, r1, r6
			adcs r0,r5
			eors r1,r7
			eors r0,r7
			eors r4, r4
			asrs r7, r7, #1
			adcs r1, r2
			adcs r2, r0
			adcs r7, r4
			mov r0, r8
			adds r1, r1, r0
			adcs r2, r3
			adcs r3, r7
		pop {r4, r5}
		mov r6, r12
		mov r7, r12
		eors r0, r6
		eors r1, r6
		eors r2, r6
		eors r3, r6
		asrs r6, r6, #1
		adcs r0, r4
		adcs r1, r5
		adcs r4, r2
		adcs r5, r3
		eors r2, r2
		adcs r6,r2 //0,1
		adcs r7,r2
		pop {r2, r3}
		mov r8, r2
		mov r9, r3
		adds r2, r2, r0
		adcs r3, r1
		mov r0, r10
		mov r1, r11
		adcs r4, r0
		adcs r5, r1
		adcs r6, r0
		adcs r7, r1
	////////END HIGH PART/////////////////////
	mov r0, r8
	mov r1, r9
	mov r8, r6
	mov r9, r7
	pop {r6, r7}
	adds r0, r0, r6
	adcs r1, r7
	pop {r6, r7}
	adcs r2, r6
	adcs r3, r7
	pop {r7}
	stm r7!, {r0-r3}
	mov r10, r7
	eors r0,r0
	mov r6, r8
	mov r7, r9
	adcs r4, r0
	adcs r5, r0
	adcs r6, r0
	adcs r7, r0
	pop {r0,r1,r2}
	mov r12, r2
	push {r0, r4-r7}
	ldm r1, {r0-r7}
	subs r0, r4
	sbcs r1, r5
	sbcs r2, r6
	sbcs r3, r7
	eors r4, r4
	sbcs r4, r4
	eors r0, r4
	eors r1, r4
	eors r2, r4
	eors r3, r4
	subs r0, r4
	sbcs r1, r4
	sbcs r2, r4
	sbcs r3, r4
	mov r6, r12
	mov r12, r4 //carry
	mov r5, r10
	stm r5!, {r0-r3}
	mov r11, r5
	mov r8, r0
	mov r9, r1
	ldm r6, {r0-r7}
	subs r4, r0
	sbcs r5, r1
	sbcs r6, r2
	sbcs r7, r3
	eors r0, r0
	sbcs r0, r0
	eors r4, r0
	eors r5, r0
	eors r6, r0
	eors r7, r0
	subs r4, r0
	sbcs r5, r0
	sbcs r6, r0
	sbcs r7, r0
	mov r1, r12
	eors r0, r1
	mov r1, r11
	stm r1!, {r4-r7}
	push {r0}
	mov r2, r8
	mov r3, r9
	/////////BEGIN MIDDLE PART////////////////
		/////////MUL128/////////////
			//MUL64
			adds r6, r5, #0
			adds r1, r2, #0
			subs r5, r4
			sbcs r0, r0
			eors r5, r0
			subs r5, r0
			subs r1, r3
			sbcs r7, r7
			eors r1, r7
			subs r1, r7
			eors r7, r0
			mov r9, r1
			mov r8, r5
			lsrs r1,r4,#16
			uxth r4,r4
			adds r0, r4, #0
			uxth r5,r2
			lsrs r2,#16
			muls r0,r5//00
			muls r5,r1//10
			muls r4,r2//01
			muls r1,r2//11
			lsls r2,r4,#16
			lsrs r4,r4,#16
			adds r0, r0, r2
			adcs r1,r4
			lsls r2,r5,#16
			lsrs r4,r5,#16
			adds r0, r0, r2
			adcs r1,r4
			lsrs r4, r6,#16
			uxth r6, r6
			uxth r5, r3
			lsrs r3, r3, #16
			adds r2, r6, #0
			muls r2, r5
			muls r5, r4
			muls r6, r3
			muls r3, r4
			lsls r4,r5,#16
			lsrs r5,r5,#16
			adds r2, r2, r4
			adcs r3,r5
			lsls r4,r6,#16
			lsrs r5,r6,#16
			adds r2, r2, r4
			adcs r3,r5
			eors r6, r6
			adds r2, r2, r1
			adcs r3, r6
			mov r1, r9
			mov r5, r8
			mov r8, r0
			lsrs r0, r1,#16
			uxth r1,r1
			adds r4, r1, #0
			lsrs r6,r5,#16
			uxth r5,r5
			muls r1,r5
			muls r4,r6
			muls r5,r0
			muls r0,r6
			lsls r6,r4,#16
			lsrs r4,#16
			adds r1, r1, r6
			adcs r0,r4
			lsls r6,r5,#16
			lsrs r5,#16
			adds r1, r1, r6
			adcs r0,r5
			eors r1,r7
			eors r0,r7
			eors r4, r4
			asrs r7, r7, #1
			adcs r1, r2
			adcs r2, r0
			adcs r7, r4
			mov r0, r8
			adds r1, r1, r0
			adcs r2, r3
			adcs r3, r7
		push {r0,r1}
		mov r1, r10
		mov r10, r2
		ldm r1, {r0, r1, r4, r5}
		adds r2, r4, #0
		adds r7, r5, #0
		subs r2, r0
		sbcs r7, r1
		sbcs r6, r6
		eors r2, r6
		eors r7, r6
		subs r2, r6
		sbcs r7, r6
		push {r2, r7}
		mov r2, r11
		mov r11, r3
		ldm r2, {r0, r1, r2, r3}
		subs r0, r2
		sbcs r1, r3
		sbcs r7, r7
		eors r0, r7
		eors r1, r7
		subs r0, r7
		sbcs r1, r7
		eors r7, r6
		mov r12, r7
		push {r0, r1}
			//MUL64
			adds r6, r5, #0
			adds r1, r2, #0
			subs r5, r4
			sbcs r0, r0
			eors r5, r0
			subs r5, r0
			subs r1, r3
			sbcs r7, r7
			eors r1, r7
			subs r1, r7
			eors r7, r0
			mov r9, r1
			mov r8, r5
			lsrs r1,r4,#16
			uxth r4,r4
			adds r0, r4, #0
			uxth r5,r2
			lsrs r2,#16
			muls r0,r5//00
			muls r5,r1//10
			muls r4,r2//01
			muls r1,r2//11
			lsls r2,r4,#16
			lsrs r4,r4,#16
			adds r0, r0, r2
			adcs r1,r4
			lsls r2,r5,#16
			lsrs r4,r5,#16
			adds r0, r0, r2
			adcs r1,r4
			lsrs r4, r6,#16
			uxth r6, r6
			uxth r5, r3
			lsrs r3, r3, #16
			adds r2, r6, #0
			muls r2, r5
			muls r5, r4
			muls r6, r3
			muls r3, r4
			lsls r4,r5,#16
			lsrs r5,r5,#16
			adds r2, r2, r4
			adcs r3,r5
			lsls r4,r6,#16
			lsrs r5,r6,#16
			adds r2, r2, r4
			adcs r3,r5
			eors r6, r6
			adds r2, r2, r1
			adcs r3, r6
			mov r1, r9
			mov r5, r8
			mov r8, r0
			lsrs r0, r1,#16
			uxth r1,r1
			adds r4, r1, #0
			lsrs r6,r5,#16
			uxth r5,r5
			muls r1,r5
			muls r4,r6
			muls r5,r0
			muls r0,r6
			lsls r6,r4,#16
			lsrs r4,#16
			adds r1, r1, r6
			adcs r0,r4
			lsls r6,r5,#16
			lsrs r5,#16
			adds r1, r1, r6
			adcs r0,r5
			eors r1,r7
			eors r0,r7
			eors r4, r4
			asrs r7, r7, #1
			adcs r1, r2
			adcs r2, r0
			adcs r7, r4
			mov r0, r8
			adds r1, r1, r0
			adcs r2, r3
			adcs r3, r7
		mov r4, r10
		mov r5, r11
		eors r6, r6
		adds r0, r0, r4
		adcs r1, r5
		adcs r2, r6
		adcs r3, r6
		mov r10, r2
		mov r11, r3
		pop {r2-r5}
		push {r0, r1}
			adds r6, r5, #0
			adds r1, r2, #0
			subs r5, r4
			sbcs r0, r0
			eors r5, r0
			subs r5, r0
			subs r1, r3
			sbcs r7, r7
			eors r1, r7
			subs r1, r7
			eors r7, r0
			mov r9, r1
			mov r8, r5
			lsrs r1,r4,#16
			uxth r4,r4
			adds r0, r4, #0
			uxth r5,r2
			lsrs r2,#16
			muls r0,r5//00
			muls r5,r1//10
			muls r4,r2//01
			muls r1,r2//11
			lsls r2,r4,#16
			lsrs r4,r4,#16
			adds r0, r0, r2
			adcs r1,r4
			lsls r2,r5,#16
			lsrs r4,r5,#16
			adds r0, r0, r2
			adcs r1,r4
			lsrs r4, r6,#16
			uxth r6, r6
			uxth r5, r3
			lsrs r3, r3, #16
			adds r2, r6, #0
			muls r2, r5
			muls r5, r4
			muls r6, r3
			muls r3, r4
			lsls r4,r5,#16
			lsrs r5,r5,#16
			adds r2, r2, r4
			adcs r3,r5
			lsls r4,r6,#16
			lsrs r5,r6,#16
			adds r2, r2, r4
			adcs r3,r5
			eors r6, r6
			adds r2, r2, r1
			adcs r3, r6
			mov r1, r9
			mov r5, r8
			mov r8, r0
			lsrs r0, r1,#16
			uxth r1,r1
			adds r4, r1, #0
			lsrs r6,r5,#16
			uxth r5,r5
			muls r1,r5
			muls r4,r6
			muls r5,r0
			muls r0,r6
			lsls r6,r4,#16
			lsrs r4,#16
			adds r1, r1, r6
			adcs r0,r4
			lsls r6,r5,#16
			lsrs r5,#16
			adds r1, r1, r6
			adcs r0,r5
			eors r1,r7
			eors r0,r7
			eors r4, r4
			asrs r7, r7, #1
			adcs r1, r2
			adcs r2, r0
			adcs r7, r4
			mov r0, r8
			adds r1, r1, r0
			adcs r2, r3
			adcs r3, r7
		pop {r4, r5}
		mov r6, r12
		mov r7, r12
		eors r0, r6
		eors r1, r6
		eors r2, r6
		eors r3, r6
		asrs r6, r6, #1
		adcs r0, r4
		adcs r1, r5
		adcs r4, r2
		adcs r5, r3
		eors r2, r2
		adcs r6,r2 //0,1
		adcs r7,r2
		pop {r2, r3}
		mov r8, r2
		mov r9, r3
		adds r2, r2, r0
		adcs r3, r1
		mov r0, r10
		mov r1, r11
		adcs r4, r0
		adcs r5, r1
		adcs r6, r0
		adcs r7, r1
	//////////END MIDDLE PART////////////////
	pop {r0,r1} //r0,r1
	mov r12, r0 //negative
	eors r2, r0
	eors r3, r0
	eors r4, r0
	eors r5, r0
	eors r6, r0
	eors r7, r0
	push {r4-r7}
	ldm r1!, {r4-r7}
	mov r11, r1 //reference
	mov r1, r9
	eors r1, r0
	mov r10, r4
	mov r4, r8
	asrs r0, #1
	eors r0, r4
	mov r4, r10
	adcs r0, r4
	adcs r1, r5
	adcs r2, r6
	adcs r3, r7
	eors r4, r4
	adcs r4, r4
	mov r10, r4 //carry
	mov r4, r11
	ldm r4, {r4-r7}
	adds r0, r0, r4
	adcs r1, r5
	adcs r2, r6
	adcs r3, r7
	mov r9, r4
	mov r4, r11
	stm r4!, {r0-r3}
	mov r11, r4
	pop {r0-r3}
	mov r4, r9
	adcs r4, r0
	adcs r5, r1
	adcs r6, r2
	adcs r7, r3
	movs r1, #0
	adcs r1, r1
	mov r0, r10
	mov r10, r1 //carry
	asrs r0, #1
	pop {r0-r3}
	adcs r4, r0
	adcs r5, r1
	adcs r6, r2
	adcs r7, r3
	mov r8, r0
	mov r0, r11
	stm r0!, {r4-r7}
	mov r11, r0
	mov r0, r8
	mov r6, r12
	mov r5, r10
	eors r4, r4
	adcs r5, r6
	adcs r6, r4
	adds r0, r0, r5
	adcs r1, r6
	adcs r2, r6
	adcs r3, r6
	mov r7, r11
	stm r7!, {r0-r3}
	pop {r3-r6}
	mov r8, r3
	mov r9, r4
	mov r10, r5
	mov r11, r6
	pop {r4-r7,pc}
	bx	lr
.size	multiply256x256_asm, .-multiply256x256_asm
