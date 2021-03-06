#ifndef _X86_IRQFLAGS_H_
#define _X86_IRQFLAGS_H_

#include <asm/smp-processor-id.h>

#ifndef __ASSEMBLY__
/*
 * The use of 'barrier' in the following reflects their use as local-lock
 * operations. Reentrancy must be prevented (e.g., __cli()) /before/ following
 * critical operations are executed. All critical operations must complete
 * /before/ reentrancy is permitted (e.g., __sti()). Alpha architecture also
 * includes these barriers, for example.
 */

#define xen_save_fl(void) vcpu_info_read(evtchn_upcall_mask)

#define xen_restore_fl(f)					\
do {								\
	vcpu_info_t *_vcpu;					\
	barrier();						\
	_vcpu = current_vcpu_info();				\
	if ((_vcpu->evtchn_upcall_mask = (f)) == 0) {		\
		barrier(); /* unmask then check (avoid races) */\
		if (unlikely(_vcpu->evtchn_upcall_pending))	\
			force_evtchn_callback();		\
	}							\
} while (0)

#define xen_irq_disable()					\
do {								\
	vcpu_info_write(evtchn_upcall_mask, 1);			\
	barrier();						\
} while (0)

#define xen_irq_enable()					\
do {								\
	vcpu_info_t *_vcpu;					\
	barrier();						\
	_vcpu = current_vcpu_info();				\
	_vcpu->evtchn_upcall_mask = 0;				\
	barrier(); /* unmask then check (avoid races) */	\
	if (unlikely(_vcpu->evtchn_upcall_pending))		\
		force_evtchn_callback();			\
} while (0)

void xen_safe_halt(void);

void xen_halt(void);

#define __raw_local_save_flags() xen_save_fl()

#define raw_local_irq_restore(flags) xen_restore_fl(flags)

#define raw_local_irq_disable()	xen_irq_disable()

#define raw_local_irq_enable() xen_irq_enable()

/*
 * Used in the idle loop; sti takes one instruction cycle
 * to complete:
 */
static inline void raw_safe_halt(void)
{
	xen_safe_halt();
}

/*
 * Used when interrupts are already enabled or to
 * shutdown the processor:
 */
static inline void halt(void)
{
	xen_halt();
}

/*
 * For spinlocks, etc:
 */
#define __raw_local_irq_save()						\
({									\
	unsigned long flags = __raw_local_save_flags();			\
									\
	raw_local_irq_disable();					\
									\
	flags;								\
})
#else

/* Offsets into shared_info_t. */
#define evtchn_upcall_pending		/* 0 */
#define evtchn_upcall_mask		1

#ifdef CONFIG_X86_64
# define __REG_si %rsi
# define __CPU_num PER_CPU_VAR(cpu_number)
#else
# define __REG_si %esi
# define __CPU_num TI_cpu(%ebp)
#endif

#ifdef CONFIG_XEN_VCPU_INFO_PLACEMENT

#define GET_VCPU_INFO		PER_CPU(vcpu_info, __REG_si)
#define __DISABLE_INTERRUPTS	movb $1,PER_CPU_VAR(vcpu_info+evtchn_upcall_mask)
#define __ENABLE_INTERRUPTS	movb $0,PER_CPU_VAR(vcpu_info+evtchn_upcall_mask)
#define __TEST_PENDING		cmpb $0,PER_CPU_VAR(vcpu_info+evtchn_upcall_pending+0)
#define DISABLE_INTERRUPTS(clb)	__DISABLE_INTERRUPTS
#define ENABLE_INTERRUPTS(clb)	__ENABLE_INTERRUPTS

#define __SIZEOF_DISABLE_INTERRUPTS 8
#define __SIZEOF_TEST_PENDING	8

#else /* CONFIG_XEN_VCPU_INFO_PLACEMENT */

#define sizeof_vcpu_shift	6

#ifdef CONFIG_SMP
#define GET_VCPU_INFO		movl __CPU_num,%esi			; \
				shl $sizeof_vcpu_shift,%esi		; \
				add HYPERVISOR_shared_info,__REG_si
#else
#define GET_VCPU_INFO		mov HYPERVISOR_shared_info,__REG_si
#endif

#define __DISABLE_INTERRUPTS	movb $1,evtchn_upcall_mask(__REG_si)
#define __ENABLE_INTERRUPTS	movb $0,evtchn_upcall_mask(__REG_si)
#define __TEST_PENDING		testb $0xFF,evtchn_upcall_pending(__REG_si)
#define DISABLE_INTERRUPTS(clb)	GET_VCPU_INFO				; \
				__DISABLE_INTERRUPTS
#define ENABLE_INTERRUPTS(clb)	GET_VCPU_INFO				; \
				__ENABLE_INTERRUPTS

#define __SIZEOF_DISABLE_INTERRUPTS 4
#define __SIZEOF_TEST_PENDING	3

#endif /* CONFIG_XEN_VCPU_INFO_PLACEMENT */

#ifndef CONFIG_X86_64
#define INTERRUPT_RETURN		iret
#define ENABLE_INTERRUPTS_SYSEXIT					  \
	movb $0,evtchn_upcall_mask(%esi) /* __ENABLE_INTERRUPTS */	; \
sysexit_scrit:	/**** START OF SYSEXIT CRITICAL REGION ****/		; \
	cmpb $0,evtchn_upcall_pending(%esi) /* __TEST_PENDING */	; \
	jnz  14f	/* process more events if necessary... */	; \
	movl PT_ESI(%esp), %esi						; \
	sysexit								; \
14:	movb $1,evtchn_upcall_mask(%esi) /* __DISABLE_INTERRUPTS */	; \
	TRACE_IRQS_OFF							; \
sysexit_ecrit:	/**** END OF SYSEXIT CRITICAL REGION ****/		; \
	mov  $__KERNEL_PERCPU, %ecx					; \
	push %esp							; \
	mov  %ecx, %fs							; \
	SET_KERNEL_GS %ecx						; \
	call evtchn_do_upcall						; \
	add  $4,%esp							; \
	jmp  ret_from_intr
#endif


#endif /* __ASSEMBLY__ */

#ifndef __ASSEMBLY__
#define raw_local_save_flags(flags)				\
	do { (flags) = __raw_local_save_flags(); } while (0)

#define raw_local_irq_save(flags)				\
	do { (flags) = __raw_local_irq_save(); } while (0)

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return (flags != 0);
}

#define raw_irqs_disabled()						\
({									\
	unsigned long flags = __raw_local_save_flags();			\
									\
	raw_irqs_disabled_flags(flags);					\
})

#else

#ifdef CONFIG_X86_64
#define ARCH_LOCKDEP_SYS_EXIT		call lockdep_sys_exit_thunk
#define ARCH_LOCKDEP_SYS_EXIT_IRQ	\
	TRACE_IRQS_ON; \
	ENABLE_INTERRUPTS(CLBR_NONE); \
	SAVE_REST; \
	LOCKDEP_SYS_EXIT; \
	RESTORE_REST; \
	__DISABLE_INTERRUPTS; \
	TRACE_IRQS_OFF;

#else
#define ARCH_LOCKDEP_SYS_EXIT			\
	pushl %eax;				\
	pushl %ecx;				\
	pushl %edx;				\
	call lockdep_sys_exit;			\
	popl %edx;				\
	popl %ecx;				\
	popl %eax;

#define ARCH_LOCKDEP_SYS_EXIT_IRQ
#endif

#ifdef CONFIG_TRACE_IRQFLAGS
#  define TRACE_IRQS_ON		call trace_hardirqs_on_thunk;
#  define TRACE_IRQS_OFF	call trace_hardirqs_off_thunk;
#else
#  define TRACE_IRQS_ON
#  define TRACE_IRQS_OFF
#endif
#ifdef CONFIG_DEBUG_LOCK_ALLOC
#  define LOCKDEP_SYS_EXIT	ARCH_LOCKDEP_SYS_EXIT
#  define LOCKDEP_SYS_EXIT_IRQ	ARCH_LOCKDEP_SYS_EXIT_IRQ
# else
#  define LOCKDEP_SYS_EXIT
#  define LOCKDEP_SYS_EXIT_IRQ
# endif

#endif /* __ASSEMBLY__ */
#endif
