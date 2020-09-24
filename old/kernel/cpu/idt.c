//
// Created by Aaron Gill-Braun on 2020-08-25.
//

#include <stdint.h>

#include <kernel/cpu/asm.h>
#include <kernel/cpu/exception.h>
#include <kernel/cpu/idt.h>
#include <kernel/cpu/interrupt.h>

// Interrupt Descriptor Table
// IDT entries (called) gates map ISRs to the correct interrupts.

idt_gate_t idt[IDT_ENTRIES];
idt_register_t idt_reg;

void set_idt_gate(int vector, uint32_t handler) {
  idt[vector].low_offset = low_16(handler);
  idt[vector].selector = KERNEL_CS;
  idt[vector].zero = 0;
  idt[vector].attr.gate_type = INTERRUPT_GATE_32;
  idt[vector].attr.storage_segment = 0;
  idt[vector].attr.privilege_level = 0;
  idt[vector].attr.present = 1;
  idt[vector].high_offset = high_16(handler);
}

void install_idt() {
  // Exception Handlers
  //   Faults - These can be corrected and the program may continue as if nothing happened.
  //   Traps: Traps are reported immediately after the execution of the trapping instruction.
  //   Aborts: Some severe unrecoverable error.
  set_idt_gate(0, (uint32_t) isr0);   // Divide-by-zero Error (Fault)
  set_idt_gate(1, (uint32_t) isr1);   // Debug (Fault/Trap)
  set_idt_gate(2, (uint32_t) isr2);   // Non-maskable Interrupt (Interrupt)
  set_idt_gate(3, (uint32_t) isr3);   // Breakpoint (Trap)
  set_idt_gate(4, (uint32_t) isr4);   // Overflow (Trap)
  set_idt_gate(5, (uint32_t) isr5);   // Bound Range Exceeded (Fault)
  set_idt_gate(6, (uint32_t) isr6);   // Invalid Opcode (Fault)
  set_idt_gate(7, (uint32_t) isr7);   // Device Not Available (Fault)
  set_idt_gate(8, (uint32_t) isr8);   // Double Fault (Abort)
  set_idt_gate(9, (uint32_t) isr9);   // Intel Reserved
  set_idt_gate(10, (uint32_t) isr10); // Invalid TSS (Fault)
  set_idt_gate(11, (uint32_t) isr11); // Segment Not Present (Fault)
  set_idt_gate(12, (uint32_t) isr12); // Stack-Segment Fault (Fault)
  set_idt_gate(13, (uint32_t) isr13); // General Protection (Fault)
  set_idt_gate(14, (uint32_t) isr14); // Page Fault (Fault)
  set_idt_gate(15, (uint32_t) isr15); // Intel Reserved
  set_idt_gate(16, (uint32_t) isr16); // x87 FPU Floating-Point Exception (Fault)
  set_idt_gate(17, (uint32_t) isr17); // Alignment Check (Fault)
  set_idt_gate(18, (uint32_t) isr18); // Machine Check (Abort)
  set_idt_gate(19, (uint32_t) isr19); // SIMD Floating-Point Exception (Fault)
  set_idt_gate(20, (uint32_t) isr20); // Virtualization Exception (Fault)
  set_idt_gate(21, (uint32_t) isr21); // Intel Reserved
  set_idt_gate(22, (uint32_t) isr22); // Intel Reserved
  set_idt_gate(23, (uint32_t) isr23); // Intel Reserved
  set_idt_gate(24, (uint32_t) isr24); // Intel Reserved
  set_idt_gate(25, (uint32_t) isr25); // Intel Reserved
  set_idt_gate(26, (uint32_t) isr26); // Intel Reserved
  set_idt_gate(27, (uint32_t) isr27); // Intel Reserved
  set_idt_gate(28, (uint32_t) isr28); // Intel Reserved
  set_idt_gate(29, (uint32_t) isr29); // Intel Reserved
  set_idt_gate(30, (uint32_t) isr30); // Security Exception
  set_idt_gate(31, (uint32_t) isr31); // Intel Reserved

  // Interrupt Handlers
  set_idt_gate(32, (uint32_t) isr32); // Programmable Interrupt Timer Interrupt
  set_idt_gate(33, (uint32_t) isr33); // Keybaord Interrupt
  set_idt_gate(34, (uint32_t) isr34); // Cascade (used internally by two PICs)
  set_idt_gate(35, (uint32_t) isr35); // COM2 (if enabled)
  set_idt_gate(36, (uint32_t) isr36); // COM1 (if enabled)
  set_idt_gate(37, (uint32_t) isr37); // LPT2 (if enabled)
  set_idt_gate(38, (uint32_t) isr38); // Floppy Disk
  set_idt_gate(39, (uint32_t) isr39); // LPT1 / Unreliable
  set_idt_gate(40, (uint32_t) isr40); // CMOS real-time clock
  set_idt_gate(41, (uint32_t) isr41); // Free for peripherals / old SCSI / NIC
  set_idt_gate(42, (uint32_t) isr42); // Free for peripherals / SCSI / NIC
  set_idt_gate(43, (uint32_t) isr43); // Free for peripherals / SCSI / NIC
  set_idt_gate(44, (uint32_t) isr44); // PS2 Mouse
  set_idt_gate(45, (uint32_t) isr45); // FPU / Coprocessor / Inter-processor
  set_idt_gate(46, (uint32_t) isr46); // Primary ATA Hard Disk
  set_idt_gate(47, (uint32_t) isr47); // Secondary ATA Hard Disk

  set_idt_gate(48, (uint32_t) isr48); // Available
  set_idt_gate(49, (uint32_t) isr49); // Available
  set_idt_gate(50, (uint32_t) isr50); // Available
  set_idt_gate(51, (uint32_t) isr51); // Available
  set_idt_gate(52, (uint32_t) isr52); // Available
  set_idt_gate(53, (uint32_t) isr53); // Available
  set_idt_gate(54, (uint32_t) isr54); // Available
  set_idt_gate(55, (uint32_t) isr55); // Available
  set_idt_gate(56, (uint32_t) isr56); // Available
  set_idt_gate(57, (uint32_t) isr57); // Available
  set_idt_gate(58, (uint32_t) isr58); // Available
  set_idt_gate(59, (uint32_t) isr59); // Available
  set_idt_gate(60, (uint32_t) isr60); // Available
  set_idt_gate(61, (uint32_t) isr61); // Available
  set_idt_gate(62, (uint32_t) isr62); // Available
  set_idt_gate(63, (uint32_t) isr63); // Available
  set_idt_gate(64, (uint32_t) isr64); // Available
  set_idt_gate(65, (uint32_t) isr65); // Available
  set_idt_gate(66, (uint32_t) isr66); // Available
  set_idt_gate(67, (uint32_t) isr67); // Available
  set_idt_gate(68, (uint32_t) isr68); // Available
  set_idt_gate(69, (uint32_t) isr69); // Available
  set_idt_gate(70, (uint32_t) isr70); // Available
  set_idt_gate(71, (uint32_t) isr71); // Available
  set_idt_gate(72, (uint32_t) isr72); // Available
  set_idt_gate(73, (uint32_t) isr73); // Available
  set_idt_gate(74, (uint32_t) isr74); // Available
  set_idt_gate(75, (uint32_t) isr75); // Available
  set_idt_gate(76, (uint32_t) isr76); // Available
  set_idt_gate(77, (uint32_t) isr77); // Available
  set_idt_gate(78, (uint32_t) isr78); // Available
  set_idt_gate(79, (uint32_t) isr79); // Available
  set_idt_gate(80, (uint32_t) isr80); // Available
  set_idt_gate(81, (uint32_t) isr81); // Available
  set_idt_gate(82, (uint32_t) isr82); // Available
  set_idt_gate(83, (uint32_t) isr83); // Available
  set_idt_gate(84, (uint32_t) isr84); // Available
  set_idt_gate(85, (uint32_t) isr85); // Available
  set_idt_gate(86, (uint32_t) isr86); // Available
  set_idt_gate(87, (uint32_t) isr87); // Available
  set_idt_gate(88, (uint32_t) isr88); // Available
  set_idt_gate(89, (uint32_t) isr89); // Available
  set_idt_gate(90, (uint32_t) isr90); // Available
  set_idt_gate(91, (uint32_t) isr91); // Available
  set_idt_gate(92, (uint32_t) isr92); // Available
  set_idt_gate(93, (uint32_t) isr93); // Available
  set_idt_gate(94, (uint32_t) isr94); // Available
  set_idt_gate(95, (uint32_t) isr95); // Available
  set_idt_gate(96, (uint32_t) isr96); // Available
  set_idt_gate(97, (uint32_t) isr97); // Available
  set_idt_gate(98, (uint32_t) isr98); // Available
  set_idt_gate(99, (uint32_t) isr99); // Available
  set_idt_gate(100, (uint32_t) isr100); // Available
  set_idt_gate(101, (uint32_t) isr101); // Available
  set_idt_gate(102, (uint32_t) isr102); // Available
  set_idt_gate(103, (uint32_t) isr103); // Available
  set_idt_gate(104, (uint32_t) isr104); // Available
  set_idt_gate(105, (uint32_t) isr105); // Available
  set_idt_gate(106, (uint32_t) isr106); // Available
  set_idt_gate(107, (uint32_t) isr107); // Available
  set_idt_gate(108, (uint32_t) isr108); // Available
  set_idt_gate(109, (uint32_t) isr109); // Available
  set_idt_gate(110, (uint32_t) isr110); // Available
  set_idt_gate(111, (uint32_t) isr111); // Available
  set_idt_gate(112, (uint32_t) isr112); // Available
  set_idt_gate(113, (uint32_t) isr113); // Available
  set_idt_gate(114, (uint32_t) isr114); // Available
  set_idt_gate(115, (uint32_t) isr115); // Available
  set_idt_gate(116, (uint32_t) isr116); // Available
  set_idt_gate(117, (uint32_t) isr117); // Available
  set_idt_gate(118, (uint32_t) isr118); // Available
  set_idt_gate(119, (uint32_t) isr119); // Available
  set_idt_gate(120, (uint32_t) isr120); // Available
  set_idt_gate(121, (uint32_t) isr121); // Available
  set_idt_gate(122, (uint32_t) isr122); // Available
  set_idt_gate(123, (uint32_t) isr123); // Available
  set_idt_gate(124, (uint32_t) isr124); // Available
  set_idt_gate(125, (uint32_t) isr125); // Available
  set_idt_gate(126, (uint32_t) isr126); // Available
  set_idt_gate(127, (uint32_t) isr127); // Available
  set_idt_gate(128, (uint32_t) isr128); // Available
  set_idt_gate(129, (uint32_t) isr129); // Available
  set_idt_gate(130, (uint32_t) isr130); // Available
  set_idt_gate(131, (uint32_t) isr131); // Available
  set_idt_gate(132, (uint32_t) isr132); // Available
  set_idt_gate(133, (uint32_t) isr133); // Available
  set_idt_gate(134, (uint32_t) isr134); // Available
  set_idt_gate(135, (uint32_t) isr135); // Available
  set_idt_gate(136, (uint32_t) isr136); // Available
  set_idt_gate(137, (uint32_t) isr137); // Available
  set_idt_gate(138, (uint32_t) isr138); // Available
  set_idt_gate(139, (uint32_t) isr139); // Available
  set_idt_gate(140, (uint32_t) isr140); // Available
  set_idt_gate(141, (uint32_t) isr141); // Available
  set_idt_gate(142, (uint32_t) isr142); // Available
  set_idt_gate(143, (uint32_t) isr143); // Available
  set_idt_gate(144, (uint32_t) isr144); // Available
  set_idt_gate(145, (uint32_t) isr145); // Available
  set_idt_gate(146, (uint32_t) isr146); // Available
  set_idt_gate(147, (uint32_t) isr147); // Available
  set_idt_gate(148, (uint32_t) isr148); // Available
  set_idt_gate(149, (uint32_t) isr149); // Available
  set_idt_gate(150, (uint32_t) isr150); // Available
  set_idt_gate(151, (uint32_t) isr151); // Available
  set_idt_gate(152, (uint32_t) isr152); // Available
  set_idt_gate(153, (uint32_t) isr153); // Available
  set_idt_gate(154, (uint32_t) isr154); // Available
  set_idt_gate(155, (uint32_t) isr155); // Available
  set_idt_gate(156, (uint32_t) isr156); // Available
  set_idt_gate(157, (uint32_t) isr157); // Available
  set_idt_gate(158, (uint32_t) isr158); // Available
  set_idt_gate(159, (uint32_t) isr159); // Available
  set_idt_gate(160, (uint32_t) isr160); // Available
  set_idt_gate(161, (uint32_t) isr161); // Available
  set_idt_gate(162, (uint32_t) isr162); // Available
  set_idt_gate(163, (uint32_t) isr163); // Available
  set_idt_gate(164, (uint32_t) isr164); // Available
  set_idt_gate(165, (uint32_t) isr165); // Available
  set_idt_gate(166, (uint32_t) isr166); // Available
  set_idt_gate(167, (uint32_t) isr167); // Available
  set_idt_gate(168, (uint32_t) isr168); // Available
  set_idt_gate(169, (uint32_t) isr169); // Available
  set_idt_gate(170, (uint32_t) isr170); // Available
  set_idt_gate(171, (uint32_t) isr171); // Available
  set_idt_gate(172, (uint32_t) isr172); // Available
  set_idt_gate(173, (uint32_t) isr173); // Available
  set_idt_gate(174, (uint32_t) isr174); // Available
  set_idt_gate(175, (uint32_t) isr175); // Available
  set_idt_gate(176, (uint32_t) isr176); // Available
  set_idt_gate(177, (uint32_t) isr177); // Available
  set_idt_gate(178, (uint32_t) isr178); // Available
  set_idt_gate(179, (uint32_t) isr179); // Available
  set_idt_gate(180, (uint32_t) isr180); // Available
  set_idt_gate(181, (uint32_t) isr181); // Available
  set_idt_gate(182, (uint32_t) isr182); // Available
  set_idt_gate(183, (uint32_t) isr183); // Available
  set_idt_gate(184, (uint32_t) isr184); // Available
  set_idt_gate(185, (uint32_t) isr185); // Available
  set_idt_gate(186, (uint32_t) isr186); // Available
  set_idt_gate(187, (uint32_t) isr187); // Available
  set_idt_gate(188, (uint32_t) isr188); // Available
  set_idt_gate(189, (uint32_t) isr189); // Available
  set_idt_gate(190, (uint32_t) isr190); // Available
  set_idt_gate(191, (uint32_t) isr191); // Available
  set_idt_gate(192, (uint32_t) isr192); // Available
  set_idt_gate(193, (uint32_t) isr193); // Available
  set_idt_gate(194, (uint32_t) isr194); // Available
  set_idt_gate(195, (uint32_t) isr195); // Available
  set_idt_gate(196, (uint32_t) isr196); // Available
  set_idt_gate(197, (uint32_t) isr197); // Available
  set_idt_gate(198, (uint32_t) isr198); // Available
  set_idt_gate(199, (uint32_t) isr199); // Available
  set_idt_gate(200, (uint32_t) isr200); // Available
  set_idt_gate(201, (uint32_t) isr201); // Available
  set_idt_gate(202, (uint32_t) isr202); // Available
  set_idt_gate(203, (uint32_t) isr203); // Available
  set_idt_gate(204, (uint32_t) isr204); // Available
  set_idt_gate(205, (uint32_t) isr205); // Available
  set_idt_gate(206, (uint32_t) isr206); // Available
  set_idt_gate(207, (uint32_t) isr207); // Available
  set_idt_gate(208, (uint32_t) isr208); // Available
  set_idt_gate(209, (uint32_t) isr209); // Available
  set_idt_gate(210, (uint32_t) isr210); // Available
  set_idt_gate(211, (uint32_t) isr211); // Available
  set_idt_gate(212, (uint32_t) isr212); // Available
  set_idt_gate(213, (uint32_t) isr213); // Available
  set_idt_gate(214, (uint32_t) isr214); // Available
  set_idt_gate(215, (uint32_t) isr215); // Available
  set_idt_gate(216, (uint32_t) isr216); // Available
  set_idt_gate(217, (uint32_t) isr217); // Available
  set_idt_gate(218, (uint32_t) isr218); // Available
  set_idt_gate(219, (uint32_t) isr219); // Available
  set_idt_gate(220, (uint32_t) isr220); // Available
  set_idt_gate(221, (uint32_t) isr221); // Available
  set_idt_gate(222, (uint32_t) isr222); // Available
  set_idt_gate(223, (uint32_t) isr223); // Available
  set_idt_gate(224, (uint32_t) isr224); // Available
  set_idt_gate(225, (uint32_t) isr225); // Available
  set_idt_gate(226, (uint32_t) isr226); // Available
  set_idt_gate(227, (uint32_t) isr227); // Available
  set_idt_gate(228, (uint32_t) isr228); // Available
  set_idt_gate(229, (uint32_t) isr229); // Available
  set_idt_gate(230, (uint32_t) isr230); // Available
  set_idt_gate(231, (uint32_t) isr231); // Available
  set_idt_gate(232, (uint32_t) isr232); // Available
  set_idt_gate(233, (uint32_t) isr233); // Available
  set_idt_gate(234, (uint32_t) isr234); // Available
  set_idt_gate(235, (uint32_t) isr235); // Available
  set_idt_gate(236, (uint32_t) isr236); // Available
  set_idt_gate(237, (uint32_t) isr237); // Available
  set_idt_gate(238, (uint32_t) isr238); // Available
  set_idt_gate(239, (uint32_t) isr239); // Available
  set_idt_gate(240, (uint32_t) isr240); // Available
  set_idt_gate(241, (uint32_t) isr241); // Available
  set_idt_gate(242, (uint32_t) isr242); // Available
  set_idt_gate(243, (uint32_t) isr243); // Available
  set_idt_gate(244, (uint32_t) isr244); // Available
  set_idt_gate(245, (uint32_t) isr245); // Available
  set_idt_gate(246, (uint32_t) isr246); // Available
  set_idt_gate(247, (uint32_t) isr247); // Available
  set_idt_gate(248, (uint32_t) isr248); // Available
  set_idt_gate(249, (uint32_t) isr249); // Available
  set_idt_gate(250, (uint32_t) isr250); // Available
  set_idt_gate(251, (uint32_t) isr251); // Available
  set_idt_gate(252, (uint32_t) isr252); // Available
  set_idt_gate(253, (uint32_t) isr253); // Available
  set_idt_gate(254, (uint32_t) isr254); // Available
  set_idt_gate(255, (uint32_t) isr255); // Available

  idt_reg.base = (uint32_t) &idt;
  idt_reg.limit = IDT_ENTRIES * sizeof(idt_gate_t) - 1;
  load_idt(&idt_reg);
}
